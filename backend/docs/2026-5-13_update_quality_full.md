# 2.2 代码质量分析子系统 — 完整文档

> **版本**: v2.2 | **最后更新**: 2026-05-14
> **状态**: ✅ 后端完成 · ✅ 前端完成 · ✅ 可完整测试

---

## 一、子系统概述

### 1.1 目标

回答以下工程管理核心问题：

> "代码是否在变差？主要问题集中在哪些模块？是否出现质量突增？"

### 1.2 核心能力

| 能力 | 说明 | 状态 |
|------|------|------|
| **分析任务管理** | 创建/查看/运行/删除分析任务，支持 Pending→Running→Finished/Failed 状态机 | ✅ |
| **外部工具调用** | 调用 cppcheck 与 clang-tidy 对本地代码进行静态分析 | ✅ |
| **结果结构化入库** | 解析工具输出，按问题粒度（文件/行号/规则/等级）落库，支持问题追踪（首次出现/最后出现/修复时间） | ✅ |
| **指标与趋势** | 问题总数/新增/修复、每千行密度、严重性分布、跨版本趋势 | ✅ |
| **Top 排行** | 问题最多的文件/目录/规则，含 error 占比 | ✅ |
| **质量评分** | 基于严重性加权计算 0–100 分，可配置基线阈值 | ✅ |
| **质量退化检测** | 当前评分/error 数低于基线时标记"质量退化" | ✅ |
| **前端可视化** | 评分仪表盘、严重性分布条形图、趋势折线图、问题列表筛选分页、基线配置面板 | ✅ |

---

## 二、架构设计

### 2.1 整体数据流

```
┌──────────────┐     POST /api/repos/{id}/quality/analyze     ┌──────────────┐
│   前端页面    │ ──────────────────────────────────────────→ │  后端 API     │
│ QualityView  │                                              │ routes_quality│
│ RepoDetail   │ ←── JSON 响应（评分/问题/趋势/排行）───── │              │
└──────────────┘                                              └──────┬───────┘
                                                                    │
                          ┌─────────────────────────────────────────┤
                          ▼                                         ▼
                  ┌───────────────┐                        ┌──────────────┐
                  │ static_analysis│                        │ quality_score│
                  │  .cpp          │                        │  .cpp         │
                  └───────┬───────┘                        └──────┬───────┘
                          │                                       │
              ┌───────────┼───────────┐                           │
              ▼           ▼           ▼                           ▼
        ┌─────────┐ ┌─────────┐ ┌─────────┐              ┌──────────────┐
        │ cppcheck │ │clang-tidy│ │ git     │              │  SQLite DB   │
        │  (XML)   │ │ (stdout) │ │ clone   │              │ quality_*    │
        └─────────┘ └─────────┘ └─────────┘              └──────────────┘
```

### 2.2 模块职责

| 源文件 | 职责 |
|--------|------|
| `quality/static_analysis.h/.cpp` | Git clone/pull 仓库 → 收集 C++ 源文件 → 调用 cppcheck/clang-tidy → 解析输出 → 结构化落库 |
| `quality/quality_score.h/.cpp` | 从 `quality_issues` 表聚合计算质量评分（严重性加权）、输出 JSON |
| `api/routes_quality.cpp` | 所有质量相关 API 的 Handler 函数 + 路由注册 |
| `api/routes_put.cpp` | `PUT /api/quality/issues/{id}` — 更新问题状态 |
| `api/routes_delete.cpp` | `DELETE /api/quality/tasks/{id}` / `runs/{id}` / `issues/{id}` |

---

## 三、数据库表结构

### 3.1 `quality_issues` — 质量问题明细

```sql
CREATE TABLE quality_issues (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    repo_id           INTEGER NOT NULL,          -- 所属仓库
    tool              TEXT NOT NULL,             -- cppcheck | clang-tidy
    issue_key         TEXT NOT NULL DEFAULT '',  -- FNV1a 哈希，用于问题去重与追踪
    file_path         TEXT NOT NULL,             -- 相对路径
    line              INTEGER NOT NULL DEFAULT 0,
    column            INTEGER NOT NULL DEFAULT 0,
    rule_id           TEXT NOT NULL DEFAULT '',  -- 规则 ID（如 uninitvar）
    severity          TEXT NOT NULL DEFAULT '',  -- error|warning|style|performance|portability|information
    message           TEXT NOT NULL DEFAULT '',  -- 问题描述
    status            TEXT NOT NULL DEFAULT 'active',  -- active|fixed|ignored|false_positive
    first_seen_at     TEXT NOT NULL DEFAULT (datetime('now')),
    first_seen_run_id INTEGER NOT NULL DEFAULT 0,
    last_seen_run_id  INTEGER NOT NULL DEFAULT 0,
    last_seen_at      TEXT,
    fixed_at          TEXT,
    FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
);
```

**关键索引**:
- `idx_quality_issues_repo_tool(repo_id, tool)`
- `idx_quality_issues_repo_file(repo_id, file_path)`
- `idx_quality_issues_repo_status(repo_id, status, tool)`
- `idx_quality_issues_key(repo_id, tool, issue_key)`

**状态说明**:
| status | 含义 | 评分时是否计入 |
|--------|------|:---:|
| `active` | 当前活跃问题 | ✅ |
| `fixed` | 已被工具确认修复（下次分析未再现） | ❌ |
| `ignored` | 人工标记忽略 | ❌ |
| `false_positive` | 人工标记误报 | ❌ |

### 3.2 `quality_analysis_tasks` — 分析任务

```sql
CREATE TABLE quality_analysis_tasks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    repo_id     INTEGER NOT NULL,
    branch      TEXT NOT NULL DEFAULT 'main',
    tools       TEXT NOT NULL DEFAULT 'cppcheck',   -- cppcheck|clang-tidy|all
    mode        TEXT NOT NULL DEFAULT 'full',       -- full|hot
    max_files   INTEGER NOT NULL DEFAULT 2000,
    config_json TEXT NOT NULL DEFAULT '{}',
    schedule    TEXT NOT NULL DEFAULT 'manual',     -- manual|weekly|on_release
    status      TEXT NOT NULL DEFAULT 'Pending',    -- Pending|Running|Finished|Failed
    last_run_id INTEGER,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
);
```

### 3.3 `quality_analysis_runs` — 分析运行记录

```sql
CREATE TABLE quality_analysis_runs (
    id                    INTEGER PRIMARY KEY AUTOINCREMENT,
    task_id               INTEGER,                  -- NULL=即时分析
    repo_id               INTEGER NOT NULL,
    branch                TEXT NOT NULL DEFAULT 'main',
    tools                 TEXT NOT NULL DEFAULT '',
    mode                  TEXT NOT NULL DEFAULT 'full',
    max_files             INTEGER NOT NULL DEFAULT 0,
    status                TEXT NOT NULL DEFAULT 'Running',  -- Running|Finished|Failed
    config_json           TEXT NOT NULL DEFAULT '{}',
    started_at            TEXT NOT NULL DEFAULT (datetime('now')),
    finished_at           TEXT,
    analyzed_files        INTEGER NOT NULL DEFAULT 0,
    lines_analyzed        INTEGER NOT NULL DEFAULT 0,
    issues_total          INTEGER NOT NULL DEFAULT 0,
    issues_new            INTEGER NOT NULL DEFAULT 0,
    issues_fixed          INTEGER NOT NULL DEFAULT 0,
    issues_by_severity_json TEXT NOT NULL DEFAULT '{}',
    score                 REAL NOT NULL DEFAULT 0,
    baseline_score        REAL,                     -- 基线评分（可为NULL）
    degraded              INTEGER NOT NULL DEFAULT 0,
    output_json           TEXT NOT NULL DEFAULT '{}',
    error                 TEXT NOT NULL DEFAULT '',
    FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE,
    FOREIGN KEY (task_id) REFERENCES quality_analysis_tasks(id) ON DELETE SET NULL
);
```

### 3.4 `quality_run_issues` — 运行问题关联

```sql
CREATE TABLE quality_run_issues (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id    INTEGER NOT NULL,
    repo_id   INTEGER NOT NULL,
    tool      TEXT NOT NULL,
    issue_key TEXT NOT NULL DEFAULT '',
    file_path TEXT NOT NULL,
    line      INTEGER NOT NULL DEFAULT 0,
    column    INTEGER NOT NULL DEFAULT 0,
    rule_id   TEXT NOT NULL DEFAULT '',
    severity  TEXT NOT NULL DEFAULT '',
    message   TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (run_id) REFERENCES quality_analysis_runs(id) ON DELETE CASCADE,
    FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
);
```

### 3.5 `quality_baselines` — 质量基线

```sql
CREATE TABLE quality_baselines (
    repo_id          INTEGER PRIMARY KEY,
    min_score        REAL NOT NULL DEFAULT 80.0,     -- 最低评分阈值
    max_new_issues   INTEGER NOT NULL DEFAULT 0,     -- 单次分析允许最大新增问题数
    max_error_issues INTEGER NOT NULL DEFAULT 0,     -- 最大 error 级问题数
    updated_at       TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
);
```

---

## 四、API 接口完整文档

### 4.1 触发分析 — `POST /api/repos/{id}/quality/analyze`

拉取仓库代码并执行静态分析。

**请求体 (JSON)**:
```json
{
    "tool": "cppcheck",
    "tools": "cppcheck",
    "ref": "main",
    "mode": "full",
    "max_files": 2000,
    "config": {}
}
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `tool` / `tools` | string | `"cppcheck"` | 分析工具：`cppcheck`、`clang-tidy`、`all` |
| `ref` | string | `"main"` | Git 分支或 tag |
| `mode` | string | `"full"` | 分析模式：`full`=全量、`hot`=仅热点目录 |
| `max_files` | int | `2000` | 分析文件数上限 |

**响应示例**:
```json
{
    "ok": true,
    "tool": "cppcheck",
    "status": "Finished",
    "run_id": 42,
    "task_id": 0,
    "analyzed_files": 156,
    "lines_analyzed": 28450,
    "issues_inserted": 87,
    "issues_new": 12,
    "issues_fixed": 5,
    "severity_stats": { "error": 8, "warning": 34, "style": 45 },
    "output_files": { "cppcheck": "data/quality/cppcheck_1_20260513_143022.xml" }
}
```

**PowerShell 测试**:
```powershell
$BaseUrl = "http://127.0.0.1:8080"

# 触发 cppcheck 全量分析
$resp = Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/repos/1/quality/analyze" `
  -ContentType "application/json" `
  -Body '{"tool":"cppcheck","ref":"main","mode":"full","max_files":500}'

$resp | ConvertTo-Json -Depth 5
```

---

### 4.2 查询问题列表 — `GET /api/repos/{id}/quality/issues`

**查询参数**:

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `tool` | string | `""` (全部) | 过滤工具 |
| `severity` | string | `""` (全部) | 过滤等级 |
| `status` | string | `"active"` | `active`/`fixed`/`ignored`/`false_positive`/`all` |
| `limit` | int | `100` | 每页条数（最大 500） |
| `offset` | int | `0` | 分页偏移 |

**PowerShell 测试**:
```powershell
# 查询活跃 error
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/1/quality/issues?severity=error&status=active&limit=20" | ConvertTo-Json -Depth 5

# 查询已修复问题
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/1/quality/issues?status=fixed&limit=20" | ConvertTo-Json -Depth 5

# 查询全部问题
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/1/quality/issues?status=all&limit=50" | ConvertTo-Json -Depth 5
```

**响应结构**:
```json
{
    "items": [
        {
            "id": 1234,
            "tool": "cppcheck",
            "issue_key": "a1b2c3d4e5f67890",
            "file_path": "src/main.cpp",
            "line": 42,
            "column": 0,
            "rule_id": "uninitvar",
            "severity": "error",
            "message": "Variable 'x' is not assigned a value.",
            "status": "active",
            "first_seen_at": "2026-05-13 10:30:00",
            "last_seen_at": "2026-05-13 14:30:00",
            "fixed_at": null,
            "first_seen_run_id": 40,
            "last_seen_run_id": 42
        }
    ],
    "limit": 100,
    "offset": 0
}
```

---

### 4.3 质量评分总览 — `GET /api/repos/{id}/quality/summary`

**PowerShell 测试**:
```powershell
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/1/quality/summary" | ConvertTo-Json -Depth 5
```

**响应结构**:
```json
{
    "tool": "",
    "quality": {
        "score": 78.5,
        "penalty": 128.0,
        "total_issues": 87,
        "files_with_issues": 23,
        "severity": { "error": 8, "warning": 34, "style": 45 }
    },
    "lines_analyzed": 28450,
    "density_per_kloc": 3.06,
    "latest_run": {
        "id": 42,
        "status": "Finished",
        "started_at": "2026-05-13 14:30:22"
    },
    "baseline": {
        "configured": true,
        "min_score": 80.0,
        "max_new_issues": 20,
        "max_error_issues": 10,
        "degraded": true,
        "score_degraded": true,
        "error_degraded": false
    }
}
```

---

### 4.4 质量趋势 — `GET /api/repos/{id}/quality/trend`

**查询参数**: `limit` (int, default=20, max=100) — 返回最近 N 次运行的趋势数据。

**PowerShell 测试**:
```powershell
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/1/quality/trend?limit=10" | ConvertTo-Json -Depth 5
```

**响应**: 按时间升序排列的趋势点数组（`items`），每项包含 `run_id`、`started_at`、`score`、`issues_total`、`issues_new`、`issues_fixed`、`density_per_kloc`、`degraded`。

---

### 4.5 Top 排行 — `GET /api/repos/{id}/quality/top`

**查询参数**:

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `by` | string | `"file"` | 分组维度：`file` / `dir` / `rule` |
| `tool` | string | `""` (全部) | 过滤工具 |
| `limit` | int | `20` | 返回 Top N |

**PowerShell 测试**:
```powershell
# 问题最多的文件 Top 10
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/1/quality/top?by=file&limit=10" | ConvertTo-Json -Depth 5

# 问题最多的目录 Top 15
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/1/quality/top?by=dir&limit=15" | ConvertTo-Json -Depth 5

# 最高频规则 Top 20
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/1/quality/top?by=rule&limit=20" | ConvertTo-Json -Depth 5
```

**响应**:
```json
{
    "by": "file",
    "items": [
        { "name": "src/core/engine.cpp", "total": 23, "errors": 5 },
        { "name": "src/utils/parser.cpp", "total": 15, "errors": 2 }
    ]
}
```

---

### 4.6 分析任务管理

#### 4.6.1 创建任务 — `POST /api/repos/{id}/quality/tasks`

**请求体**:
```json
{
    "branch": "main",
    "tools": "cppcheck",
    "mode": "full",
    "max_files": 2000,
    "schedule": "manual",
    "run_now": false,
    "config": {}
}
```

| 参数 | 说明 |
|------|------|
| `schedule` | `manual`(手动) / `weekly`(每周) / `on_release`(发布时) |
| `run_now` | `true` 则创建后立即执行 |

**PowerShell 测试**:
```powershell
# 创建并立即运行
$resp = Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/repos/1/quality/tasks" `
  -ContentType "application/json" `
  -Body '{"tools":"cppcheck","mode":"full","run_now":true}'
$resp | ConvertTo-Json -Depth 5
```

#### 4.6.2 查看任务列表 — `GET /api/repos/{id}/quality/tasks`

**查询参数**: `status` (可选，如 `Pending`/`Running`/`Finished`/`Failed`)、`limit`、`offset`

**PowerShell 测试**:
```powershell
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/1/quality/tasks?limit=20" | ConvertTo-Json -Depth 5
```

#### 4.6.3 运行指定任务 — `POST /api/quality/tasks/{id}/run`

**PowerShell 测试**:
```powershell
Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/quality/tasks/1/run" | ConvertTo-Json -Depth 5
```

#### 4.6.4 删除任务 — `DELETE /api/quality/tasks/{id}`

**PowerShell 测试**:
```powershell
Invoke-RestMethod -Method DELETE -Uri "$BaseUrl/api/quality/tasks/1"
```

---

### 4.7 运行记录 — `GET /api/repos/{id}/quality/runs`

**查询参数**: `limit`、`offset`

**PowerShell 测试**:
```powershell
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/1/quality/runs?limit=10" | ConvertTo-Json -Depth 5
```

#### 删除运行记录 — `DELETE /api/quality/runs/{id}`

```powershell
Invoke-RestMethod -Method DELETE -Uri "$BaseUrl/api/quality/runs/42"
```

---

### 4.8 质量基线

#### 4.8.1 获取基线 — `GET /api/repos/{id}/quality/baseline`

```powershell
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/1/quality/baseline" | ConvertTo-Json -Depth 5
```

**响应**:
```json
{
    "configured": true,
    "min_score": 80.0,
    "max_new_issues": 20,
    "max_error_issues": 10,
    "updated_at": "2026-05-13 15:00:00"
}
```

#### 4.8.2 设置基线 — `PUT /api/repos/{id}/quality/baseline`

**请求体**:
```json
{
    "min_score": 75.0,
    "max_new_issues": 30,
    "max_error_issues": 5
}
```

**PowerShell 测试**:
```powershell
Invoke-RestMethod -Method PUT -Uri "$BaseUrl/api/repos/1/quality/baseline" `
  -ContentType "application/json" `
  -Body '{"min_score":75.0,"max_new_issues":30,"max_error_issues":5}' | ConvertTo-Json -Depth 5
```

---

### 4.9 问题状态更新 — `PUT /api/quality/issues/{id}`

标记问题为已修复、忽略或误报。

**请求体**:
```json
{ "status": "fixed" }
```

`status` 取值: `active` | `fixed` | `ignored` | `false_positive`

**行为说明**:
- `fixed`: 设置 `fixed_at=datetime('now')`
- `active` (恢复): 清除 `fixed_at=NULL`
- `ignored` / `false_positive`: 仅更新 `status`，不影响时间戳

**PowerShell 测试**:
```powershell
# 标记问题 #1234 为已修复
Invoke-RestMethod -Method PUT -Uri "$BaseUrl/api/quality/issues/1234" `
  -ContentType "application/json" `
  -Body '{"status":"fixed"}'

# 标记为误报
Invoke-RestMethod -Method PUT -Uri "$BaseUrl/api/quality/issues/1234" `
  -ContentType "application/json" `
  -Body '{"status":"false_positive"}'

# 恢复为活跃
Invoke-RestMethod -Method PUT -Uri "$BaseUrl/api/quality/issues/1234" `
  -ContentType "application/json" `
  -Body '{"status":"active"}'
```

---

### 4.10 删除单个问题 — `DELETE /api/quality/issues/{id}`

```powershell
Invoke-RestMethod -Method DELETE -Uri "$BaseUrl/api/quality/issues/1234"
```

---

### 4.11 API 接口速查表

| 方法 | 路径 | 说明 |
|:----:|------|------|
| **POST** | `/api/repos/{id}/quality/analyze` | 触发静态分析 |
| **GET** | `/api/repos/{id}/quality/issues` | 查询问题列表（筛选+分页） |
| **GET** | `/api/repos/{id}/quality/summary` | 质量评分与严重性汇总 |
| **GET** | `/api/repos/{id}/quality/trend` | 质量趋势数据 |
| **GET** | `/api/repos/{id}/quality/top` | Top 排行（文件/目录/规则） |
| **POST** | `/api/repos/{id}/quality/tasks` | 创建分析任务 |
| **GET** | `/api/repos/{id}/quality/tasks` | 查看任务列表 |
| **POST** | `/api/quality/tasks/{id}/run` | 运行指定任务 |
| **DELETE** | `/api/quality/tasks/{id}` | 删除任务 |
| **GET** | `/api/repos/{id}/quality/runs` | 查看运行记录 |
| **DELETE** | `/api/quality/runs/{id}` | 删除运行记录 |
| **GET** | `/api/repos/{id}/quality/baseline` | 获取质量基线 |
| **PUT** | `/api/repos/{id}/quality/baseline` | 设置质量基线 |
| **PUT** | `/api/quality/issues/{id}` | 更新问题状态 |
| **DELETE** | `/api/quality/issues/{id}` | 删除单个问题 |

---

## 五、质量评分算法详解

### 5.1 严重性权重

| 严重等级 | 权重 | 说明 |
|----------|:----:|------|
| `error` | 10.0 | 运行时错误/未定义行为 |
| `warning` | 6.0 | 可能导致 bug 的警告 |
| `performance` | 4.0 | 性能相关问题 |
| `portability` | 4.0 | 可移植性问题 |
| `style` | 2.0 | 代码风格问题 |
| `information` | 0.5 | 信息性提示 |
| 其他 | 1.0 | 未识别等级的默认权重 |

### 5.2 评分公式

```
penalty = Σ (severity_weight(level) × count)       — 对所有 active 问题
penalty_per_file = penalty / max(1, distinct_file_count)
score = clamp(100 - penalty_per_file × 2, 0, 100)
```

**设计理念**:
- 问题越分散（涉及文件越多），对整体代码质量影响越大
- `×2` 系数使得单个 error 级问题在只有一个文件时扣 20 分
- 无问题时得分 100

### 5.3 质量退化判定

```
degraded = (baseline 已配置) AND (
    score < baseline.min_score          — 评分低于基线
    OR active_errors > baseline.max_error_issues  — error 数超限
)
```

---

## 六、前端页面说明

### 6.1 仓库详情页 — 质量卡片

**路径**: `/repos/{id}` → RepoDetailView

在仓库详情页中新增的"代码质量"卡片显示：
- 圆形评分环（绿 ≥80 / 黄 ≥60 / 红 <60）
- 问题总数、每千行密度
- 质量退化警告
- "详情 →" 链接跳转完整质量分析页

### 6.2 质量分析页 — QualityView

**路径**: `/repos/{id}/quality` → QualityView

包含 **6 个标签页**：

| 标签页 | 功能 |
|--------|------|
| **问题列表** | 筛选（工具/等级/状态）+ 分页 + 行内状态变更（标记已修复/忽略/误报） |
| **趋势图** | SVG 双折线图（质量评分 + 问题总数）+ 明细表格 |
| **Top 排行** | 按文件/目录/规则分组，含 error 占比可视化 |
| **分析任务** | 创建/查看/运行/删除任务，状态筛选 |
| **运行记录** | 历史运行列表（文件数/行数/问题数/评分/退化标记） |
| **质量基线** | 配置最低评分/最大新增数/最大 error 数阈值 |

**操作栏**: 一键触发分析（可选工具/模式/文件上限），实时显示结果摘要。

---

## 七、配置说明

在 `backend/config/config.env` 中添加以下配置项：

```ini
# ---- 2.2 代码质量分析配置 ----

# 工具可执行文件路径（不配置则使用系统 PATH 中的默认值）
CPPCHECK_BIN=cppcheck
CLANG_TIDY_BIN=clang-tidy

# clang-tidy 需要 compile_commands.json 所在目录
# 可设置为项目 build 目录或手动指定
CLANG_TIDY_COMPILE_COMMANDS=/path/to/build

# cppcheck 额外参数（可选）
CPPCHECK_ARGS=--std=c++17

# clang-tidy 额外参数（可选）
CLANG_TIDY_ARGS=-checks=-*,clang-analyzer-*,bugprone-*

# cppcheck 输出目录（默认 data/quality）
QUALITY_OUTPUT_DIR=data/quality

# 忽略的规则 ID（逗号分隔，默认忽略 missingInclude）
QUALITY_IGNORE_RULES=missingInclude,missingIncludeSystem

# 拉取仓库代码的本地缓存根目录
REPO_CLONE_ROOT=data/repo_cache

# 是否分析头文件（默认 false，仅分析 .cpp/.c）
CPPCHECK_ANALYZE_HEADERS=true

# cppcheck 额外 include 目录（逗号分隔）
CPPCHECK_INCLUDE_DIRS=/usr/local/include,third_party/include
```

### 环境依赖

- **cppcheck**: 需安装并可在 PATH 中找到，或通过 `CPPCHECK_BIN` 指定完整路径
  - 下载: https://cppcheck.sourceforge.io/
  - Windows: `choco install cppcheck` 或手动下载
  - Linux: `apt install cppcheck`
- **clang-tidy** (可选): 需安装 LLVM/Clang 工具链，且项目需有 `compile_commands.json`
  - `apt install clang-tidy` 或 `brew install llvm`
- **Git**: 用于 clone/pull 仓库代码

---

## 八、完整测试流程

### 8.1 准备工作

```powershell
# 1. 确保后端已编译并启动
cd ~/build
./devinsight_backend.exe
# 看到 "DevInsight backend listening on http://127.0.0.1:8080" 表示启动成功

# 2. 设置基础 URL
$BaseUrl = "http://127.0.0.1:8080"

# 3. 确认服务正常
Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/health"
# 应返回: {"ok":true}
```

### 8.2 端到端测试步骤

```powershell
# ====== 第一步: 添加仓库 ======
$repo = Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/repos?full_name=torvalds/linux"
# 使用你自己的目标仓库，建议先用小型 C++ 项目测试
$repoId = $repo.repo_id
Write-Host "Repo ID: $repoId"

# ====== 第二步: 同步仓库数据（可选，但建议先同步以获取 commits/issues） ======
Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/repos/$repoId/sync?mode=full&issues_pages_count=3&pulls_pages_count=3&commits_pages_count=5"

# ====== 第三步: 运行代码质量分析 ======
$analysis = Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/repos/$repoId/quality/analyze" `
  -ContentType "application/json" `
  -Body '{"tool":"cppcheck","ref":"main","mode":"full","max_files":500}'

Write-Host "状态: $($analysis.status)"
Write-Host "分析文件: $($analysis.analyzed_files)"
Write-Host "分析行数: $($analysis.lines_analyzed)"
Write-Host "问题数: $($analysis.issues_inserted)"
Write-Host "新增: $($analysis.issues_new)"
Write-Host "修复: $($analysis.issues_fixed)"

# ====== 第四步: 查看质量评分 ======
$summary = Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$repoId/quality/summary"
Write-Host "质量评分: $($summary.quality.score)/100"
Write-Host "问题总数: $($summary.quality.total_issues)"
Write-Host "密度: $([math]::Round($summary.density_per_kloc, 2))/KLOC"

# ====== 第五步: 查看问题列表 ======
# 查看所有 error
$errors = Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$repoId/quality/issues?severity=error&limit=10"
Write-Host "Error 数量: $($errors.items.Count)"

# 查看所有 warning
$warnings = Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$repoId/quality/issues?severity=warning&limit=10"
Write-Host "Warning 数量: $($warnings.items.Count)"

# ====== 第六步: Top 排行 ======
$topFiles = Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$repoId/quality/top?by=file&limit=10"
Write-Host "=== 问题最多的文件 ==="
foreach ($f in $topFiles.items) {
    Write-Host "  $($f.name): $($f.total) 问题 ($($f.errors) error)"
}

$topRules = Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$repoId/quality/top?by=rule&limit=10"
Write-Host "=== 最高频规则 ==="
foreach ($r in $topRules.items) {
    Write-Host "  $($r.name): $($r.total) 次 ($($r.errors) error)"
}

# ====== 第七步: 标记问题状态 ======
# 获取第一个 error 问题
if ($errors.items.Count -gt 0) {
    $issueId = $errors.items[0].id
    Write-Host "标记问题 #$issueId 为误报..."
    Invoke-RestMethod -Method PUT -Uri "$BaseUrl/api/quality/issues/$issueId" `
      -ContentType "application/json" `
      -Body '{"status":"false_positive"}'
    Write-Host "已标记"
}

# ====== 第八步: 配置质量基线 ======
Invoke-RestMethod -Method PUT -Uri "$BaseUrl/api/repos/$repoId/quality/baseline" `
  -ContentType "application/json" `
  -Body '{"min_score":70.0,"max_new_issues":50,"max_error_issues":20}'

# 验证基线
$baseline = Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$repoId/quality/baseline"
Write-Host "基线已配置: min_score=$($baseline.min_score), max_new=$($baseline.max_new_issues), max_error=$($baseline.max_error_issues)"

# ====== 第九步: 查看趋势 ======
$trend = Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$repoId/quality/trend?limit=10"
Write-Host "趋势数据点数: $($trend.items.Count)"
foreach ($t in $trend.items) {
    Write-Host "  $($t.started_at): score=$($t.score), total=$($t.issues_total), new=+$($t.issues_new), fixed=-$($t.issues_fixed)"
}

# ====== 第十步: 任务管理 ======
# 创建定时任务
$task = Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/repos/$repoId/quality/tasks" `
  -ContentType "application/json" `
  -Body '{"tools":"cppcheck","mode":"full","schedule":"weekly","run_now":false}'
Write-Host "创建任务: task_id=$($task.task_id)"

# 查看任务列表
$tasks = Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$repoId/quality/tasks"
Write-Host "任务数: $($tasks.items.Count)"

# 手动运行任务
if ($task.task_id) {
    Invoke-RestMethod -Method POST -Uri "$BaseUrl/api/quality/tasks/$($task.task_id)/run" | ConvertTo-Json -Depth 3
}

# 删除任务
if ($task.task_id) {
    Invoke-RestMethod -Method DELETE -Uri "$BaseUrl/api/quality/tasks/$($task.task_id)"
    Write-Host "任务已删除"
}

# ====== 第十一步: 运行记录 ======
$runs = Invoke-RestMethod -Method GET -Uri "$BaseUrl/api/repos/$repoId/quality/runs?limit=10"
Write-Host "运行记录数: $($runs.items.Count)"

Write-Host "`n===== 全部测试完成! ====="
```

### 8.3 前端测试

```
1. 启动前端开发服务器:
   cd frontend
   npm install   (首次)
   npm run dev

2. 打开浏览器访问 http://localhost:5173

3. 操作流程:
   Repos 列表 → 点击仓库进入详情 → 查看"代码质量"卡片 →
   点击"详情 →"进入质量分析页 →
   选择工具和模式 → 点击"🔍 运行分析" →
   切换标签页查看问题列表/趋势图/Top排行/任务/基线
```

---

## 九、问题追踪机制详解

### 9.1 Issue Key 生成

每个问题由以下字段组合生成唯一标识（FNV1a-64 哈希）：
```
tool(小写) | file_path(小写+正斜杠) | line | column | rule_id(小写) | message
```

例如 cppcheck 在 `src/main.cpp:42` 检测到的 `uninitvar` 问题，即使行号因代码变更而偏移，只要问题本质不变就会被识别为同一问题。

### 9.2 问题生命周期

```
首次分析 → status='active', first_seen_run_id=N, last_seen_run_id=N
  ├─ 下次分析仍存在 → last_seen_run_id 更新为最新 run_id
  ├─ 下次分析消失 → status='fixed', fixed_at=now
  ├─ 人工标记忽略 → status='ignored'
  ├─ 人工标记误报 → status='false_positive'
  └─ 人工恢复 → status='active', fixed_at=NULL
```

### 9.3 新增/修复统计

```
issues_new = 本次分析中发现但上次不存在 OR 上次已 fixed 的问题
issues_fixed = 上次分析中存在但本次未再现的 active 问题
```

---

## 十、文件清单

### 10.1 后端源文件

| 文件 | 行数 | 说明 |
|------|:----:|------|
| `src/quality/static_analysis.h` | 45 | 静态分析结构体与接口声明 |
| `src/quality/static_analysis.cpp` | ~1340 | 核心引擎：Git clone/pull、cppcheck/clang-tidy 调用、XML/stdout 解析、issue 追踪去重、落库 |
| `src/quality/quality_score.h` | 16 | 评分结构体与接口 |
| `src/quality/quality_score.cpp` | 105 | 评分计算（严重性加权、密度归一化） |
| `src/api/routes_quality.cpp` | ~900 | 全部质量 API Handler + 路由注册 |
| `src/api/routes_put.cpp` | +60 | `PUT /api/quality/issues/{id}` 状态更新 |
| `src/api/routes_delete.cpp` | +80 | 质量相关 DELETE 端点 |
| `src/db/db.cpp` | +70 | 5 张质量表 schema + 迁移逻辑 |
| **后端合计** | **~2600** | |

### 10.2 前端源文件

| 文件 | 行数 | 说明 |
|------|:----:|------|
| `frontend/src/views/QualityView.vue` | ~520 | 完整质量分析页（6 标签页 + SVG 图表 + 操作栏） |
| `frontend/src/views/RepoDetailView.vue` | +70 | 质量概览卡片 + 导航链接 |
| `frontend/src/main.js` | +2 | 新增路由注册 |
| **前端合计** | **~590** | |

### 10.3 配置文件

| 文件 | 说明 |
|------|------|
| `backend/config/config.env.example` | 含 2.2 配置项说明 |
| `backend/CMakeLists.txt` | 已注册 quality 模块编译 |

---

## 十一、已知限制与注意事项

1. **clang-tidy 稳定性**: clang-tidy 分析需要项目提供 `compile_commands.json`。若项目无此文件，分析将失败并提示 `compile_commands.json not found`。建议优先使用 cppcheck。

2. **大型仓库分析耗时**: 对超大仓库（>5000 文件）运行 `mode=full` 可能耗时较长。建议首次使用时设置较小的 `max_files`（如 500），或使用 `mode=hot` 仅分析热点目录。

3. **cppcheck 返回值**: cppcheck 在发现问题时也可能返回非零退出码。代码已处理此情况——只要 XML 输出非空，结果即被视为有效。

4. **Windows 路径空格**: 代码已通过 `cmd /c` 包装处理 Windows 路径空格问题，无需额外配置。

5. **Git 认证**: 分析前需要 clone 仓库，需确保 `GITHUB_TOKEN` 已配置且对目标仓库有读取权限。公开仓库可免 token 访问（但受 API 限流影响）。

6. **问题去重依赖 issue_key**: 如果同一文件中的同一规则在不同行出现，会生成不同的 `issue_key`。这意味着"行号变更"会导致问题被标记为 fixed+new 各一次。

---

## 十二、版本历史

| 日期 | 版本 | 变更 |
|------|------|------|
| 2026-05-12 | v2.2-alpha | 初始实现：静态分析引擎、质量评分、API 路由 |
| 2026-05-13 | v2.2 | 完成前端 QualityView、问题状态管理 API、DELETE 端点、质量基线配置、完整文档 |
