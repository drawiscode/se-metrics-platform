# 2026-4-28 新增 2.4(4) 自动周报 + 2.4(5) 隐形专家识别

## 改动背景
README 中 2.4(4) 自动周报生成和 2.4(5) PageRank 隐形专家识别两个模块此前完全未实现。本次完成两个模块的后端实现，复用现有 DB 数据和 LLM 调用基础设施。

## 新增文件清单

| 文件 | 类型 | 行数 | 说明 |
|------|------|------|------|
| `src/expert/pagerank.h` | 新增 | 40 | ExpertScore/ModuleExpert 结构体 + 函数声明 |
| `src/expert/pagerank.cpp` | 新增 | 435 | PageRank 算法核心：协作图构建、迭代计算、模块专家查询、知识库写入、JSON 序列化 |
| `src/api/routes_expert.cpp` | 新增 | 102 | 专家识别 API 路由（3 个端点） |
| `src/report/weekly_report.h` | 新增 | 33 | WeeklyReport 结构体 + 函数声明 |
| `src/report/weekly_report.cpp` | 新增 | 547 | 周报生成核心：数据聚合、LLM 调用、DB 存储、知识库写入、JSON 序列化 |
| `src/api/routes_report.cpp` | 新增 | 99 | 周报 API 路由（3 个端点） |
| `docs/2026-4-28_update_expert_report.md` | 新增 | 本文件 | 修改日志 |
| **新增合计** | | **1256** | |

## 修改文件清单

| 文件 | 改动行数 | 说明 |
|------|---------|------|
| `CMakeLists.txt` | +4 | 新增 4 个 .cpp 到 add_executable |
| `src/api/routes.cpp` | +5 | 前置声明 + 调用 register_expert_routes、register_report_routes |
| `src/db/db.cpp` | +15 | 新增 weekly_reports 表定义 + 索引 |
| **修改合计** | **+24** | |

## 总计新增代码: 1280 行

## 新增 API 接口

### 2.4(5) 隐形专家识别
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/repos/{id}/experts?top=20` | 获取全局 PageRank 专家排名 |
| GET | `/api/repos/{id}/experts/module?dir=xxx&top=10` | 按模块目录获取专家 |
| POST | `/api/repos/{id}/experts/build` | 重新计算专家图谱并写入知识库 |

### 2.4(4) 自动周报生成
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/repos/{id}/report/generate` | 生成周报（调用 LLM） |
| GET | `/api/repos/{id}/reports?limit=10` | 查询周报历史 |
| GET | `/api/repos/{id}/report/latest` | 获取最新周报 |

## 新增数据库表
```sql
CREATE TABLE IF NOT EXISTS weekly_reports (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    repo_id INTEGER NOT NULL,
    week_start TEXT NOT NULL,
    week_end TEXT NOT NULL,
    report_text TEXT NOT NULL,
    metrics_json TEXT,
    model TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
);
```

## 算法说明

### PageRank 专家识别
1. 从 `commits` + `commit_files` 构建开发者协作图（共同修改同一目录 → 建立边）
2. 从 `pull_requests.raw_json` 解析 `requested_reviewers` 提取 review 关系（权重 2.0）
3. PageRank 迭代（damping=0.85, 20 次迭代）
4. 每个开发者附带统计：commit 数、涉及文件数、最近活跃时间、主要负责模块
5. 结果写入 `knowledge_chunks`（source_type="expert"）供 RAG 引用

### 周报生成
1. 聚合最近 7 天数据：commit/PR/Issue/Release 数量、Top 贡献者、风险告警
2. 计算健康度评分（复用 compute_repo_metrics + compute_health_from_metrics）
3. 调用 LLM 生成结构化 Markdown 周报
4. 存入 `weekly_reports` 表，同时写入 `knowledge_chunks` 供 RAG 引用

## 验证结果
1. 编译通过
2. `GET /api/repos/1/experts?top=5` — 返回 3 个专家（仓库仅 3 个贡献者）
3. `POST /api/repos/1/experts/build` — 写入 4 条知识块
4. `POST /api/repos/1/report/generate` — 成功生成周报（qwen3-max）
5. `GET /api/repos/1/reports` — 返回 1 条周报记录
6. `GET /api/repos/1/report/latest` — 返回最新周报
