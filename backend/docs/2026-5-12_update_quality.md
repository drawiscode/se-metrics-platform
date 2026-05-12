# 2026-5-12 新增 2.2 代码质量分析子系统

## 改动背景
为支持代码质量可视化、静态分析落库与评分展示，新增质量分析子系统。该子系统支持本地拉取仓库代码后调用 cppcheck/clang-tidy 分析，将问题结构化入库，并提供查询与评分接口，便于前端做趋势与评分展示。

## 新增文件清单

| 文件 | 类型 | 行数 | 说明 |
|------|------|------|------|
| `src/quality/static_analysis.h` | 新增 | 80+ | 静态分析主流程声明、结构体 |
| `src/quality/static_analysis.cpp` | 新增 | 500+ | clone/pull、本地工具调用、解析输出、落库 |
| `src/api/routes_quality.cpp` | 新增 | 160+ | 质量分析 API 路由与查询接口 |
| `src/quality/quality_score.h` | 新增 | 40+ | 质量评分结构与接口 |
| `src/quality/quality_score.cpp` | 新增 | 160+ | 质量评分计算逻辑 |
| **新增合计** | | **900+** | |

## 修改文件清单

| 文件 | 改动行数 | 说明 |
|------|---------|------|
| `src/db/db.cpp` | +30 | 新增 `quality_issues` 表结构与索引 |
| `src/api/routes.cpp` | +3 | 注册质量分析路由 |
| `CMakeLists.txt` | +2 | 编译质量分析与评分模块 |
| `config/config.env.example` | +12 | 增加 clang-tidy/cppcheck 配置说明 |
| `src/repo_metrics/health.cpp` | +10 | 健康评分算法优化（对数缩放与权重调整） |
| `src/api/routes_get.cpp` | +40 | 新增总评分接口 `/api/repos/{id}/score` |
| **修改合计** | **+90** | |

## 新增/变更 API 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/repos/{id}/quality/analyze` | 触发静态分析（cppcheck/clang-tidy） |
| GET | `/api/repos/{id}/quality/issues` | 查询质量问题列表（支持筛选分页） |
| GET | `/api/repos/{id}/quality/summary` | 质量评分与严重性分布汇总 |
| GET | `/api/repos/{id}/score` | 仓库总分（健康评分 + 质量评分） |

## 主要实现说明

### 1. 本地仓库拉取与分析
- 复用 repo clone 逻辑：`{REPO_CLONE_ROOT}/{repo_id}`，不存在则 clone，存在则 fetch/checkout。
- 静态分析在本地执行，确保可控与可复现。

### 2. cppcheck/clang-tidy 调用
- cppcheck: 输出 XML，解析 `<error>` 节点。
- clang-tidy: 解析控制台输出行，提取 file/line/rule/severity。
- Windows 下增加 `cmd /c` 包装，避免路径空格导致执行失败。

### 3. 质量问题结构化入库
- 新增 `quality_issues` 表：file_path/line/column/rule_id/severity/message/first_seen_at。
- 每次分析会清理该仓库与工具的旧问题，再写入新结果。

### 4. 质量评分算法
- 按严重性权重计算惩罚值（error/warning/performance/style 等）。
- 以“每个受影响文件平均惩罚”映射为 0-100 分。
- 提供 `quality_score_to_json` 输出，便于可视化。

### 5. 总评分整合
- 总分 = 健康评分 * 0.6 + 质量评分 * 0.4。
- 保留 `health` 与 `quality` 子项，方便前端展示仪表盘与分项。

## 配置说明（新增/变更）

- `CLANG_TIDY_BIN` / `CPPCHECK_BIN`: 工具路径
- `CLANG_TIDY_COMPILE_COMMANDS`: `compile_commands.json` 所在目录
- `CLANG_TIDY_ARGS`: 可选扩展参数
- `QUALITY_OUTPUT_DIR`: cppcheck 输出文件目录

## 验证结果（待执行）
1. `POST /api/repos/{id}/quality/analyze?tool=cppcheck&ref=main&mode=full`
2. `GET /api/repos/{id}/quality/issues?tool=cppcheck&limit=100&offset=0`
3. `GET /api/repos/{id}/quality/summary?tool=cppcheck`
4. `GET /api/repos/{id}/score?tool=cppcheck`

## 注意：前端代码还没有做出相应更新，同时clang-tidy的内容还没有做出测试，不确定稳定性