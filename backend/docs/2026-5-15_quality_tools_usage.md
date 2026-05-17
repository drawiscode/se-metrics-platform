# 2026-05-15 质量分析子系统工具链使用与测试说明

本文档说明 2.2 代码质量分析子系统在扩展 `clang-tidy`、`cpplint`、`flawfinder` 后的配置、使用方式和测试方法。

## 1. 当前支持的分析工具

| 工具 | 作用 | 输出解析方式 | 备注 |
| --- | --- | --- | --- |
| `cppcheck` | C/C++ 静态缺陷、风格、可移植性检查 | XML | 已有主分析工具 |
| `clang-tidy` | 基于 Clang AST 的现代 C/C++ 诊断 | stdout/stderr 日志 | 优先使用 `compile_commands.json`，缺失时可生成 fallback compile database |
| `cpplint` | Google C++ 风格检查 | stdout/stderr 日志 | 偏代码规范，噪声较多，建议按项目配置过滤规则 |
| `flawfinder` | C/C++ 安全风险函数扫描 | stdout/stderr 日志 | 偏安全风险，适合作为安全辅助扫描 |

所有工具的诊断都会统一转换为 `QualityIssue`，落库到：

- `quality_issues`: 当前问题状态表，支持 active/fixed/ignored/false_positive。
- `quality_run_issues`: 单次运行问题快照表，用于趋势和修复统计。
- `quality_analysis_runs`: 每次分析运行记录。

## 1.1 评分模型说明

质量评分不是“项目能否编译/运行”的分数，而是静态分析问题密度的健康度。项目可以正常运行，但仍然可能存在风格、潜在 bug、可维护性或安全风险诊断。

更新后的评分模型做了两点处理：

1. 按工具类型加权。`cppcheck`、`clang-tidy` 更偏缺陷诊断，权重较高；`cpplint` 偏风格检查，权重较低；`flawfinder` 偏安全启发式扫描，权重介于两者之间。
2. 按问题密度做对数扣分。大量风格类问题不会再线性扣到 0 分，但问题越密集，扣分仍会逐步加重。

当前核心权重：

| 维度 | 权重 |
| --- | --- |
| `error` | 10.0 |
| `warning` | 4.0 |
| `performance` | 3.0 |
| `portability` | 3.0 |
| `style` | 0.4 |
| `information` | 0.1 |
| `cpplint` 工具权重 | 0.08 |
| `flawfinder` 工具权重 | 0.75 |
| 其他工具权重 | 1.0 |

单次运行记录使用本次 `quality_run_issues` 和本次 `lines_analyzed` 计算分数；仓库汇总页使用当前 active issue 计算分数。这样单独运行 `clang-tidy` 时，不会被历史 `cpplint` 或 `flawfinder` 问题拖低到 0 分。

## 2. 本机工具安装

本机完成以下工具配置：

```powershell
~\Microsoft Visual Studio\2026\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe --version
# LLVM version 20.1.8

~\Python\Python313\Scripts\cpplint.exe --version
# cpplint 2.0.2

~\Python\Python313\Scripts\flawfinder.exe --version
# 2.0.19
```

`cpplint` 和 `flawfinder` 通过以下命令安装到用户 Python Scripts 目录：

```powershell
python -m pip install --user cpplint flawfinder
```

该 Scripts 目录未加入全局 PATH，因此后端通过 `backend/config/config.env` 中的绝对路径调用工具。

## 3. 后端配置

`backend/config/config.env` 中质量分析相关配置如下：

```env
CPPCHECK_BIN="C:/Program Files/Cppcheck/cppcheck.exe"
CLANG_TIDY_BIN=~/Microsoft Visual Studio/2026/Community/VC/Tools/Llvm/x64/bin/clang-tidy.exe
CPPLINT_BIN=~/Python/Python313/Scripts/cpplint.exe
FLAWFINDER_BIN=~/Python/Python313/Scripts/flawfinder.exe

QUALITY_ALL_TOOLS=cppcheck,clang-tidy

CLANG_TIDY_FALLBACK_COMPILE_COMMANDS=true
CLANG_TIDY_FALLBACK_COMPILER=~/Microsoft Visual Studio/2026/Community/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe
CLANG_TIDY_FALLBACK_ARGS=/std:c++17 /nologo
CLANG_TIDY_ARGS=

CPPLINT_ARGS=
FLAWFINDER_ARGS=--quiet

QUALITY_OUTPUT_DIR=~/se-metrics-platform/data/quality
REPO_CLONE_ROOT=~/se-metrics-platform/data/repo_cache
```

说明：

- `QUALITY_ALL_TOOLS` 控制前端选择“全部工具”或 API 传 `tools=all` 时实际展开的工具列表。
- 默认保持 `cppcheck,clang-tidy`，没有把 `cpplint`、`flawfinder` 放入默认 all，是为了避免风格/安全辅助扫描的噪声影响常规质量分。
- 若需要全量工具一起运行，可改为：

```env
QUALITY_ALL_TOOLS=cppcheck,clang-tidy,cpplint,flawfinder
```

## 4. clang-tidy 编译数据库策略

`clang-tidy` 最理想的输入是仓库内真实的 `compile_commands.json`。子系统查找顺序：

1. `CLANG_TIDY_COMPILE_COMMANDS` 指定目录。
2. 仓库根目录的 `compile_commands.json`。
3. 仓库 `build/compile_commands.json`。
4. 如果开启 `CLANG_TIDY_FALLBACK_COMPILE_COMMANDS=true`，自动在 `QUALITY_OUTPUT_DIR` 下生成轻量 fallback compile database。

fallback 适合快速可用，但它不一定包含项目真实宏、第三方 include、编译选项。对复杂 C++ 项目，建议用 CMake 生成真实编译数据库：

```powershell
cmake -S <repo-path> -B <repo-path>\build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

然后配置：

```env
CLANG_TIDY_COMPILE_COMMANDS=<repo-path>/build
```

## 5. 后端 API 使用方法

### 5.1 运行单个工具

```powershell
$BaseUrl = "http://localhost:8080"
$RepoId = 1

Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/repos/$RepoId/quality/analyze" `
  -ContentType "application/json" `
  -Body '{"tools":"clang-tidy","ref":"main","mode":"full","max_files":200}'
```

可用工具名：

```text
cppcheck
clang-tidy
clang_tidy
cpplint
flawfinder
all
```

### 5.2 运行多个工具

```powershell
Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/repos/$RepoId/quality/analyze" `
  -ContentType "application/json" `
  -Body '{"tools":"cppcheck,clang-tidy,cpplint,flawfinder","ref":"main","mode":"full","max_files":200}'
```

### 5.3 查询问题列表

```powershell
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$RepoId/quality/issues?tool=clang-tidy&status=active&limit=100"
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$RepoId/quality/issues?tool=cpplint&status=active&limit=100"
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$RepoId/quality/issues?tool=flawfinder&status=active&limit=100"
```

### 5.4 查询评分汇总

```powershell
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$RepoId/quality/summary"
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$RepoId/quality/summary?tool=clang-tidy"
```

### 5.5 查询运行记录

```powershell
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$RepoId/quality/runs?limit=20"
```

`output_json` 中会记录各工具输出文件路径，例如：

```json
{
  "tools": {
    "cppcheck": "E:/Code/se-metrics-platform/data/quality/cppcheck_1_20260515_120000.xml",
    "clang-tidy": "E:/Code/se-metrics-platform/data/quality/clang_tidy_1_20260515_120000.log"
  }
}
```

## 6. 前端使用方法

进入仓库详情页：

```text
/repos/{id}/quality
```

操作步骤：

1. 在工具下拉框选择 `cppcheck`、`clang-tidy`、`cpplint`、`flawfinder` 或 `全部工具`。
2. 选择 `全量模式` 或 `热点模式`。
3. 设置文件上限，建议初次测试使用 `50` 到 `200`。
4. 点击“运行分析”。
5. 在“问题列表”中按工具、严重性、状态筛选。
6. 在“运行记录”中查看每次运行状态、输出文件、错误信息。

## 7. 本地工具连通性测试

### 7.1 验证工具可执行

```powershell
& "E:\Software\Microsoft Visual Studio\2026\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe" --version
& "C:\Users\26766\AppData\Roaming\Python\Python313\Scripts\cpplint.exe" --version
& "C:\Users\26766\AppData\Roaming\Python\Python313\Scripts\flawfinder.exe" --version
```

### 7.2 手工扫描单个文件

```powershell
& "C:\Users\26766\AppData\Roaming\Python\Python313\Scripts\cpplint.exe" --filter=-legal/copyright backend\src\quality\static_analysis.cpp

& "C:\Users\26766\AppData\Roaming\Python\Python313\Scripts\flawfinder.exe" --quiet backend\src\quality\static_analysis.cpp
```

`cpplint` 发现问题时通常返回非 0，这是正常行为。后端会解析输出中的诊断并落库。

### 7.3 编译验证

```powershell
cmake -S backend -B backend\build
cmake --build backend\build --config Debug
```

### 7.4 前端构建验证

```powershell
cd frontend
npm run build
```

## 8. 常见问题

### 8.1 clang-tidy 没有任何问题输出

优先检查：

- `CLANG_TIDY_BIN` 是否指向存在的 exe。
- 目标仓库是否有 `.cpp/.cc/.cxx/.c` 翻译单元。
- 是否有真实 `compile_commands.json`。
- fallback compile database 是否生成在 `QUALITY_OUTPUT_DIR`。

复杂项目建议提供真实 `compile_commands.json`，fallback 只保证子系统可运行，不保证诊断完整。

### 8.2 cpplint 问题过多

`cpplint` 默认规则较严格，可通过 `CPPLINT_ARGS` 调整。例如：

```env
CPPLINT_ARGS=--filter=-legal/copyright,-whitespace/line_length
```

平台中 `cpplint` 的高置信度诊断不会再映射为质量平台的 `error`，而是映射为 `warning`。原因是 `cpplint` 输出末尾的 `[1-5]` 是规则置信度，不代表编译错误或运行错误。

### 8.3 flawfinder 报 system/popen 等风险

`flawfinder` 是基于危险函数名称的启发式扫描，命中不等于漏洞。建议把结果作为安全 review 输入，确认后可在平台中标记为 `ignored` 或 `false_positive`。

### 8.4 all 没有运行 cpplint/flawfinder

这是当前默认配置。需要修改：

```env
QUALITY_ALL_TOOLS=cppcheck,clang-tidy,cpplint,flawfinder
```

修改 `config.env` 后需要重启后端进程。

### 8.5 为什么 httplib.h 问题很多

`backend/src/httplib.h` 是第三方单头库，不是本项目业务代码。静态分析工具会对它报出大量风格、安全或兼容性诊断，但这些诊断通常不应该计入本项目质量评分。

默认配置已排除：

```env
QUALITY_IGNORE_PATHS=backend/src/httplib.h,src/httplib.h
```

如果项目还有其他第三方、生成代码或 vendored 文件，也建议加入该配置，例如：

```env
QUALITY_IGNORE_PATHS=backend/src/httplib.h,src/httplib.h,third_party/,vendor/,generated/
```

修改后需要重启后端并重新运行分析。重新运行后，被过滤掉的历史 active 问题会在该工具本次运行中被标记为 fixed，不再进入汇总评分。

### 8.6 为什么 clang-tidy 出现 file not found 或 exceptions disabled

如果项目没有真实 `compile_commands.json`，平台会生成 fallback compile database。fallback 可能缺少第三方 include 路径或编译选项，从而出现：

- `'sqlite3.h' file not found`
- `'nlohmann/json.hpp' file not found`
- `cannot use 'try' with exceptions disabled`

对当前项目，`config.env` 已补充：

```env
CLANG_TIDY_FALLBACK_ARGS=/std:c++17 /EHsc /nologo /IE:/Code/se-metrics-platform/vcpkg/installed/x64-windows/include
```

更稳妥的方式仍然是提供真实 `compile_commands.json`。

## 9. 建议测试顺序

1. 使用 `max_files=50` 跑 `cppcheck`，确认原流程正常。
2. 使用 `max_files=50` 跑 `clang-tidy`，确认 compile database 或 fallback 生效。
3. 单独跑 `cpplint`，评估规则噪声，再配置 `CPPLINT_ARGS`。
4. 单独跑 `flawfinder`，确认安全风险类问题可落库。
5. 调整 `QUALITY_ALL_TOOLS`，再测试多工具联合运行。
6. 查看 `/quality/runs`、`/quality/issues`、`/quality/summary`，确认运行记录、问题列表、评分汇总同步更新。
