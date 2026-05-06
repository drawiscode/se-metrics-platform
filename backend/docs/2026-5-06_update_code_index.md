# 2026-5-06 新增 2.4(6) 代码拉取与切块入库

## 改动背景
为支持代码级 RAG 检索与结构化分析，实现了仓库代码的自动拉取（clone/pull）、多重过滤、分块切分与知识库入库流程。该功能为后续代码结构、风险、专家等智能问答提供底层数据支撑。

## 新增文件清单

| 文件 | 类型 | 行数 | 说明 |
|------|------|------|------|
| `src/ai/code_index.h` | 新增 | 48 | 代码索引主流程声明、结构体 |
| `src/ai/code_index.cpp` | 新增 | 300+ | clone/pull、目录树生成、文件过滤、切块、入库、embedding |
| `src/api/routes_code.cpp` | 新增 | 80+ | 代码索引 API 路由注册 |
| **新增合计** | | **400+** | |

## 修改文件清单

| 文件 | 改动行数 | 说明 |
|------|---------|------|
| `CMakeLists.txt` | +2 | 新增 code_index.cpp 到 add_executable |
| `src/api/routes.cpp` | +3 | 注册代码索引路由 |
| `src/ai/knowledge_base.cpp` | +10 | type 分区清理支持 code/repo_tree |
| **修改合计** | **+15** | |

## 新增/变更 API 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/repos/{id}/code/index` | 拉取代码并切块入库，支持 full/hot 模式 |

## 主要实现说明

### 1. 仓库代码拉取（clone/pull）
- 支持通过 GITHUB_TOKEN 拉取私有仓库。
- clone/pull 只拉取指定分支最新快照（--depth 1），节省空间。
- 本地目录结构：`{REPO_CLONE_ROOT}/{repo_id}`，如 data/repo_cache/16。
- 已存在则自动 fetch + checkout，保证快照一致。

### 2. 目录树结构块生成
- 递归遍历目录，过滤无关目录（.git、node_modules 等），默认 4 层（可调）。
- 生成 repo_tree 块，便于结构化检索。

### 3. 代码文件多重过滤
- 仅处理允许扩展名（.cpp, .py, .js, .vue, .md 等）。
- 跳过二进制、大文件（>256KB）、无关目录。
- 支持 hot 模式，仅索引核心路径（src/include/lib/app/docs/README）。

### 4. 代码切块与入库
- 每 250 行切一块，自动带文件名、行号、语言头部。
- 每块生成唯一 source_id（路径@sha#Lx-Ly），便于溯源。
- 入库 type=code，支持后续 embedding。

### 5. embedding 生成与降级
- 入库后自动批量生成 embedding，embedding 失败时降级为关键词检索。

## 验证结果
1. `POST /api/repos/16/code/index?ref=main&mode=full` — 成功拉取并切块 30+ 文件，生成 100+ code 块
2. 本地 clone 目录同步更新，sha 一致
3. 数据库 knowledge_chunks 表新增 code/repo_tree 块，embedding 正常生成
4. 过滤统计准确，性能可控

## 后续建议
- 可根据实际项目结构调整目录树递归深度（建议 6~8 层）
- 支持增量索引、索引状态查询等扩展
