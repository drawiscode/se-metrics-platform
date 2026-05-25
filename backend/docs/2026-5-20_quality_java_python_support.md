# 2026-05-20 质量分析子系统 Java/Python 支持说明

本文档说明本次对 2.2 代码质量分析子系统的扩展：在原有 C/C++ 静态分析能力基础上，新增 Python 与 Java 代码质量分析，并复用现有任务、运行记录、问题落库、趋势、Top 列表和评分流程。

## 1. 背景与结论

原实现主要面向 C/C++：

| 工具 | 语言 | 说明 |
| --- | --- | --- |
| `cppcheck` | C/C++ | 静态缺陷、风格、可移植性检查 |
| `clang-tidy` | C/C++ | 基于 Clang AST 的诊断 |
| `cpplint` | C/C++ | Google C++ 风格检查 |
| `flawfinder` | C/C++ | C/C++ 安全风险函数扫描 |

本次新增：

| 工具 | 语言 | 输出格式 | 平台工具名 |
| --- | --- | --- | --- |
| `pylint` | Python | JSON | `pylint` |
| `checkstyle` | Java | XML | `checkstyle` |

现在质量分析子系统可以通过同一套接口分析 C/C++、Python、Java 代码。

## 2026-05-25 补充：质量洞察与运行正确性修正

本次巡检补充了几个质量分析闭环修正：

- `GET /api/repos/{repo_id}/quality/issues` 新增 `total` 字段，前端分页不再估算总数。
- `quality_analysis_tasks` 列表查询修复了无 `status` 过滤时的分页占位符错位。
- 趋势图只读取 `Finished` 运行，避免失败运行以空结果污染趋势分。
- 基线退化判断同时考虑 `min_score`、`max_new_issues`、`max_error_issues`。
- 修复扫描结果同步逻辑：只会把本次实际扫描过的文件中的旧问题标记为 `fixed`，避免热点模式或文件上限扫描误修复未扫描文件的问题。

新增接口：

```http
GET /api/repos/{repo_id}/quality/insights
GET /api/repos/{repo_id}/quality/insights?tool=cppcheck
```

返回当前风险级别、热点文件、高频规则、最近两次完成运行的评分变化和建议动作。前端 `QualityView` 已新增“质量洞察”卡片。

## 2. 后端实现

主要修改文件：

- `backend/src/quality/static_analysis.cpp`
- `backend/src/quality/quality_score.cpp`
- `backend/config/config.env.example`

### 2.1 文件收集

新增语言文件识别：

| 函数 | 匹配文件 |
| --- | --- |
| `is_python_source` | `.py` |
| `is_java_source` | `.java` |

新增统一文件收集函数：

```cpp
collect_source_files(repo_dir, hot_mode, max_files, is_source)
```

并基于它封装：

```cpp
collect_cpp_files(...)
collect_python_files(...)
collect_java_files(...)
```

这样不同工具只扫描对应语言的源文件，避免 Python/Java 工具误扫 C++ 文件。

### 2.2 工具名称规范化

`canonical_tool_name` 新增别名：

| 输入 | 规范化结果 |
| --- | --- |
| `python` / `py-lint` / `py_lint` | `pylint` |
| `java` / `java-checkstyle` / `java_checkstyle` | `checkstyle` |

因此 API 可以传：

```text
tools=pylint
tools=python
tools=checkstyle
tools=java
```

### 2.3 Python: pylint

新增执行函数：

```cpp
execute_pylint(...)
```

默认命令形态：

```bash
pylint --output-format=json --score=n <file>
```

可通过环境变量配置：

```env
PYLINT_BIN=pylint
PYLINT_ARGS=--score=n
```

新增解析函数：

```cpp
parse_pylint_json(...)
```

解析字段映射：

| pylint 字段 | QualityIssue 字段 |
| --- | --- |
| `path` / `abspath` | `file_path` |
| `line` | `line` |
| `column` | `column` |
| `message-id` + `symbol` | `rule_id` |
| `type` | `severity` |
| `message` | `message` |

严重级别映射：

| pylint type | 平台 severity |
| --- | --- |
| `fatal` / `error` | `error` |
| `warning` | `warning` |
| `convention` / `refactor` | `style` |
| `information` / `info` | `information` |

### 2.4 Java: checkstyle

新增执行函数：

```cpp
execute_checkstyle(...)
```

默认命令形态：

```bash
checkstyle -f xml -c /google_checks.xml <file>
```

可通过环境变量配置：

```env
CHECKSTYLE_BIN=checkstyle
CHECKSTYLE_CONFIG=/google_checks.xml
CHECKSTYLE_ARGS=
```

新增解析函数：

```cpp
parse_checkstyle_xml(...)
```

解析字段映射：

| checkstyle XML | QualityIssue 字段 |
| --- | --- |
| `<file name="">` | `file_path` |
| `<error line="">` | `line` |
| `<error column="">` | `column` |
| `<error source="">` | `rule_id` |
| `<error severity="">` | `severity` |
| `<error message="">` | `message` |

`source` 会取最后一段作为规则名，例如：

```text
com.puppycrawl.tools.checkstyle.checks.naming.MemberNameCheck
```

会归一为：

```text
MemberNameCheck
```

## 3. 任务流程影响

`run_static_analysis_task` 已新增两个分支：

```cpp
if (tool_lower == "pylint") {
    auto files = collect_python_files(repo_dir, hot_mode, max_files);
    ToolExecutionResult pylint = execute_pylint(repo_dir, files, repo_id);
    merge_tool_result(result, db, repo_id, run_id, pylint);
}

if (tool_lower == "checkstyle") {
    auto files = collect_java_files(repo_dir, hot_mode, max_files);
    ToolExecutionResult checkstyle = execute_checkstyle(repo_dir, files, repo_id);
    merge_tool_result(result, db, repo_id, run_id, checkstyle);
}
```

新工具复用现有流程：

1. 拉取或更新仓库代码。
2. 按语言收集源文件。
3. 调用外部工具。
4. 解析输出为 `QualityIssue`。
5. 归一化路径、规则、严重等级。
6. 去重。
7. 写入 `quality_run_issues`。
8. upsert 当前问题到 `quality_issues`。
9. 标记本次未再出现的问题为 `fixed`。
10. 更新 `quality_analysis_runs` 和任务状态。

数据库结构没有变化，因为现有表已经通过 `tool` 字段区分不同分析工具。

## 4. 评分影响

`backend/src/quality/quality_score.cpp` 新增工具权重：

| 工具 | 权重 |
| --- | --- |
| `pylint` | `0.5` |
| `checkstyle` | `0.25` |

原因：

- `pylint` 同时包含潜在缺陷、风格和可维护性问题，权重低于缺陷类 C/C++ 工具，但高于纯风格工具。
- `checkstyle` 主要是 Java 代码规范检查，默认权重较低，避免格式/命名类问题过度拉低质量分。

严重级别权重沿用原模型：

| severity | 权重 |
| --- | --- |
| `error` | `10.0` |
| `warning` | `4.0` |
| `performance` | `3.0` |
| `portability` | `3.0` |
| `style` | `0.4` |
| `information` | `0.1` |

## 5. 配置项

`backend/config/config.env.example` 新增：

```env
PYLINT_BIN=pylint
CHECKSTYLE_BIN=checkstyle

PYLINT_ARGS=--score=n
CHECKSTYLE_CONFIG=/google_checks.xml
CHECKSTYLE_ARGS=
```

如果工具不在系统 `PATH` 中，应改为绝对路径，例如：

```env
PYLINT_BIN=C:/Users/<user>/AppData/Roaming/Python/Python313/Scripts/pylint.exe
CHECKSTYLE_BIN=C:/tools/checkstyle/checkstyle.bat
CHECKSTYLE_CONFIG=C:/tools/checkstyle/google_checks.xml
```

`QUALITY_ALL_TOOLS` 默认仍保持：

```env
QUALITY_ALL_TOOLS=cppcheck,clang-tidy
```

这是为了避免选择“全部工具”时，因为本机未安装 `pylint` 或 `checkstyle` 导致分析失败。

如果确认工具都已安装，可以改成：

```env
QUALITY_ALL_TOOLS=cppcheck,clang-tidy,cpplint,flawfinder,pylint,checkstyle
```

修改 `config.env` 后需要重启后端进程。

## 5.1 本机环境配置记录

本机已按便携式方式配置 Python/Java 分析工具，不依赖系统级 Java PATH。

### pylint

本机已存在 `pylint`：

```text
E:/Anaconda/Scripts/pylint.exe
```

验证结果：

```powershell
E:\Anaconda\Scripts\pylint.exe --version
```

输出版本：

```text
pylint 3.3.5
astroid 3.3.8
Python 3.13.5
```

### Checkstyle

本机没有系统级 `java` 和 `checkstyle`，因此下载到项目本地 `tools/` 目录：

```text
tools/java/jdk-21.0.11+10-jre/
tools/checkstyle/checkstyle-13.3.0-all.jar
tools/checkstyle/checkstyle.cmd
```

`checkstyle.cmd` 是后端调用的包装脚本，内部使用项目本地 JRE 执行 Checkstyle jar：

```bat
@echo off
set "SCRIPT_DIR=%~dp0"
"%SCRIPT_DIR%..\java\jdk-21.0.11+10-jre\bin\java.exe" -jar "%SCRIPT_DIR%checkstyle-13.3.0-all.jar" %*
```

验证命令：

```powershell
tools\java\jdk-21.0.11+10-jre\bin\java.exe -version
tools\checkstyle\checkstyle.cmd --version
```

验证结果：

```text
openjdk version "21.0.11" 2026-04-21 LTS
Checkstyle version: 13.3.0
```

### config.env 当前配置

`backend/config/config.env` 当前已配置为：

```env
PYLINT_BIN=E:/Anaconda/Scripts/pylint.exe
CHECKSTYLE_BIN=E:/Code/se-metrics-platform/tools/checkstyle/checkstyle.cmd

QUALITY_ALL_TOOLS=cppcheck,clang-tidy,cpplint,flawfinder,pylint,checkstyle

PYLINT_ARGS=--score=n
CHECKSTYLE_CONFIG=/google_checks.xml
CHECKSTYLE_ARGS=
```

### Git 忽略策略

便携式 JRE 和 Checkstyle jar 体积较大，不应提交到仓库。因此 `.gitignore` 已新增：

```gitignore
tools/
```

需要在新机器上重新准备 `tools/` 目录，或改用系统已安装的 Java/Checkstyle 路径。

## 6. API 使用

单独分析 Python：

```powershell
Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/repos/$RepoId/quality/analyze" `
  -ContentType "application/json" `
  -Body '{"tools":"pylint","ref":"main","mode":"full","max_files":200}'
```

单独分析 Java：

```powershell
Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/repos/$RepoId/quality/analyze" `
  -ContentType "application/json" `
  -Body '{"tools":"checkstyle","ref":"main","mode":"full","max_files":200}'
```

多工具联合分析：

```powershell
Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/repos/$RepoId/quality/analyze" `
  -ContentType "application/json" `
  -Body '{"tools":"cppcheck,clang-tidy,pylint,checkstyle","ref":"main","mode":"full","max_files":200}'
```

查询问题：

```powershell
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$RepoId/quality/issues?tool=pylint&status=active&limit=100"
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$RepoId/quality/issues?tool=checkstyle&status=active&limit=100"
```

查询评分汇总：

```powershell
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$RepoId/quality/summary?tool=pylint"
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$RepoId/quality/summary?tool=checkstyle"
```

## 7. 前端变化

`frontend/src/views/QualityView.vue` 的工具下拉框新增：

```html
<option value="pylint">pylint</option>
<option value="checkstyle">checkstyle</option>
```

新增位置：

- 运行分析工具选择框。
- 问题列表工具筛选框。

前端请求体仍使用现有字段：

```json
{
  "tool": "pylint",
  "tools": "pylint",
  "ref": "main",
  "mode": "full",
  "max_files": 2000
}
```

## 8. 验证结果

本次已完成构建验证：

```powershell
cmake --build backend\build
npm --prefix frontend run build
```

结果：

- 后端编译通过，生成 `backend/build/Debug/devinsight_backend.exe`。
- 前端 Vite 构建通过。

本次没有实际调用 `pylint` 或 `checkstyle` 扫描真实仓库，因为这依赖本机是否已安装对应外部工具，以及 Java 项目的 Checkstyle 配置文件路径是否正确。

## 9. 注意事项

1. `pylint` 和 `checkstyle` 是外部命令，后端只负责任务编排、输出解析和落库。运行前需要确保工具可执行。
2. `checkstyle` 默认配置为 `/google_checks.xml`，如果本机命令行版本不支持该内置路径，需要配置 `CHECKSTYLE_CONFIG` 为实际 XML 文件路径。
3. 选择 `tools=all` 时是否运行新工具取决于 `QUALITY_ALL_TOOLS`。
4. Python/Java 工具没有新增数据库字段，历史数据兼容。
5. `max_files` 会分别作用于对应语言的文件收集结果；多工具联合运行时，每个工具按自己的语言重新收集文件。
