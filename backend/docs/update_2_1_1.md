# 2.1 数据同步与活跃度指标（增量同步）

## 概述

本次更新聚焦 README 2.1 的 **数据采集与增量同步（P1）**，并补齐一处同步分页 Bug。

- 修复：`sync_repo_commits`（及 releases，已同步修复）**分页参数 page 未生效**的问题
- 新增：基于 `repo_sync_state` 的 **增量同步**（issues / commits / releases），PR 增量采用 **Search API** 方案
- 新增：活跃度折线数据接口 `GET /api/repos/{id}/activity?days=N`

---

## 修改/新增内容总览

### 修复

- **修复 commits 同步分页 bug**:原实现循环 `page++`，但请求仍固定 `page=1`，导致永远拉取第一页，结果不完整。
  - 影响文件：`src/api/routes_post.cpp`
  - 影响函数：`sync_repo_commits()`
  - 修复方式：将循环变量 `page` 真实传入 `github_list_commits(..., page, ...)`（以及 releases 同理，已同步修复）

- **修复 GET /api/repos/{repo_id}**: 路由返回full_name显示乱码的问题
  - 影响文件：`src/api/routes_get.cpp`
  - 影响函数：`get_repo_handler`
  - 修复方式：将sqlite3_finalize(stmt)延后释放
---

## 新增/修改文件

### 修改文件

| 文件 | 改动说明 |
|------|----------|
| `src/api/routes_post.cpp` | 引入增量同步：读取/写回 `repo_sync_state` 游标；扩展 `sync_repo_issues/sync_repo_pulls/sync_repo_commits/sync_repo_releases` 支持 `since_cursor`；PR 增量分支使用 Search API 并增加单个 PR JSON 安全解析。 |
| `src/repo_metrics/github_client.cpp` | 新增 `github_search_issues_prs()`（Search API），并确保 `q/sort/order` 使用 `simple_encode_url()` 做 URL 编码；新增 `github_get_pull()` 获取单个 PR 详情。 |
| `src/repo_metrics/github_client.h` | 声明 `github_search_issues_prs()` 与 `github_get_pull()`。 |
| `src/api/routes_get.cpp` | 新增活跃度折线接口 `GET /api/repos/{id}/activity?days=N`（按天聚合 commits）。 |
|相关文档的说明 `api_get.md` 和 `api_post.md` 说明的更新 |

---

## 数据库相关

### 依赖表（已存在）

本次增量同步依赖如下表结构（已在 `init_schema()` 中存在）：

- `repo_sync_state(repo_id PRIMARY KEY, issues_updated_cursor, pulls_updated_cursor, commits_since_cursor, releases_cursor, updated_at)`
- `issues(updated_at, is_pull_request, ...)`
- `pull_requests(updated_at, merged_at, ...)`
- `commits(committed_at, ...)`
- `releases(published_at, ...)`

---

## API 变化 / 新增接口

### 同步接口：支持增量模式（默认）

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/repos/{id}/sync` | 默认增量同步（从 `repo_sync_state` 游标继续） |
| `POST` | `/api/repos/{id}/sync?mode=full` | 全量同步（仍支持 `*_page_start/*_pages_count` 控制页数） |

#### 同步行为说明

- **增量同步（默认）**
  - issues：按 `issues_updated_cursor` 调用 GitHub issues since（`updated_at > cursor`）
  - pulls：使用 Search API 查询 `updated:>=cursor` 的 PR 列表，再获取每个 PR 详情
  - commits：按 `commits_since_cursor` 调用 commit list since（`committed_at > cursor`）
  - releases：按 `releases_cursor`（published_at）推进游标（draft 可能为空）

- **全量同步**
  - 沿用原有分页拉取方式（page_start/pages_count）

### 活跃度折线接口（新增）

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/repos/{id}/activity?days=30` | 返回最近 N 天按天聚合的 commit 次数序列 |

返回示例：
```json
{
  "items": [
    { "date": "2026-03-01", "commits": 3 },
    { "date": "2026-03-02", "commits": 0 }
  ]
}