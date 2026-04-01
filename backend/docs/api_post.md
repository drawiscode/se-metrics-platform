# POST 接口文档 




## POST /api/repos

### 描述
创建仓库元数据(在本地建 repo 记录),返回 repo_id.

### 方法与路径
POST /api/repos

### 参数
- query
  - full_name (string, required) — 仓库全名，格式 owner/repo

### 请求示例（PowerShell）
Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/repos?full_name=octocat/Hello-World"

### 响应示例
200 OK
{
  "repo_id": 1,(这个数值可能每个人不一样，具体是逐次递增1)
  "full_name": "octocat/Hello-World",
  ...
}

### 状态码
- 200 成功并返回 repo_id
- 400 参数缺失或格式错误
- 409 仓库已存在
- 500 服务端错误

### 错误示例
400 Bad Request
{ "error": "missing full_name" }

### 注意
- full_name 必须存在于 GitHub 并且能被服务访问(需要 token).
- 创建只是元数据建表，不会立即同步所有数据.

### 相关
- POST /api/repos/{repo_id}/sync


---

## POST /api/repos/{repo_id}/sync

### 描述
根据 repo_id 读取本地的 full_name，通过 full_name 和 token 从 GitHub 上同步信息。支持两种模式：
- incremental（默认）：使用上次同步时保存的 cursor 进行增量拉取，仅拉取新增/变更的数据，并在全部子任务成功时推进 cursor。
- full：不使用 cursor，按页参数进行全量拉取。

### 方法与路径
POST /api/repos/{repo_id}/sync

### 参数(均为可选参数，均为正整数)

- mode (string, optional, default=incremental) — 同步模式，"incremental" 或 "full"。
  - incremental：从数据库读取存储的 cursors（issues/pulls/commits/releases），仅拉取增量；pages_count 系作为安全上限（默认很大，避免被意外限制）。
  - full：忽略 cursors，按 pages_count 控制要拉取的页数（默认较小，避免误触发大量请求）。
- issues_page_start (int, default=1) — issues 起始页
- issues_pages_count (int, default=1 在 full 模式；在 incremental 模式下作为安全上限，默认值很大用于允许尽可能多拉取)
- pulls_page_start (int, default=1) — pulls 起始页
- pulls_pages_count (int, default=1 在 full 模式；在 incremental 模式下作为安全上限)
- commits_page_start (int, default=1) — commits 起始页
- commits_pages_count (int, default=1 在 full 模式；在 incremental 模式下作为安全上限)
- releases_page_start (int, default=1) — releases 起始页
- releases_pages_count (int, default=1 在 full 模式；在 incremental 模式下作为安全上限)


实现细节说明（便于调试）
- incremental 模式下，服务会调用 db_get_sync_state 从数据库读取四个 cursor（issues_cursor, pulls_cursor, commits_cursor, releases_cursor）。若读取失败，接口会返回 500 并且响应体为 {"error":"failed_to_read_repo_sync_state"}（实际代码字符串为 "failed to read repo_sync_state"）。
- 同步过程中会写入 repo_sync_runs 表（每次同步记录一个 run，先插入 started 状态，结束时更新为 ok/error），用于查看历史与错误信息。
- 子任务：sync_repo_snapshot、sync_repo_issues、sync_repo_pulls、sync_repo_commits、sync_repo_releases。只有当上述所有子任务全部成功时，incremental 模式才会推进并更新数据库中的 cursor。cursor 在数据库中的字段名示例为：issues_updated_cursor、pulls_updated_cursor、commits_since_cursor、releases_cursor。
- pages_count 在 incremental 模式下被当作“安全上限”（代码中默认值为一个很大的数），如果需要在增量模式下严格限制每次拉取页数，请显式传入对应的 pages_count 参数。

### 请求示例（PowerShell）
Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/repos/1/sync?issues_page_start=10&issues_pages_count=6&pulls_page_start=1&pulls_pages_count=5"

### 响应示例
200 OK
{
 "ok": true,
  "repo_id": 1,
  "issues_upserted": 600,
  "pulls_upserted": 120,
  "commits_upserted": 500,
  "releases_upserted": 5
}

### 状态码
- 200 成功并返回 repo_id
- 400 参数缺失或格式错误
- 404 repo_id 未找到  
- 502 GitHub API 报错或解析失败(同步中途返回)
- 500 服务端内部错误




### 注意
- 增量模式下会忽略page_start和page_count参数防止产生对增量模式的污染
- 同步使用服务端环境变量中的 GITHUB_TOKEN(从 env 读取),请确保服务有权限访问对应仓库.  
- 同步过程会写入 repo_sync_runs 表(用于查看历史与错误信息).  
- 若需全量拉取直到末页，请将 pages_count 设置为较大数或在代码中调整默认行为(当前默认仅 1 页以避免误触发超额请求).
- incremental 模式下只有全部子任务成功才会推进并写入新的 cursors（数据库字段示例：issues_updated_cursor、pulls_updated_cursor、commits_since_cursor、releases_cursor）。
---

## POST /api/repos/{repo_id}/sync/commit_files

### 描述
针对指定仓库,遍历本地 commits 表中已保存的 commit(按时间倒序),逐条调用 GitHub Commit API 拉取该 commit 的 files 信息,并将每个文件的 additions/deletions/churn 等写入本地 commit_files 表.用于生成代码热力图与模块热点数据.

### 方法与路径
POST /api/repos/{repo_id}/sync/commit_files

### Query 参数
- limit (int, optional, default=30) — 单次最多处理多少个 commit，避免一次处理过多导致 503 / 限流。默认 30。

### 请求示例（PowerShell）
Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/repos/1/sync/commit_files?limit=20"


### 响应示例（200 OK）
{
  "ok": true,
  "repo_id": 1,
  "limit_commits": 30,
  "total_files_processed": 2345
}

### 状态码
- 200 成功（返回处理的文件数）  
- 404 repo_id 未找到  
- 502 GitHub API 返回或 JSON 解析失败(同步中断时)  
- 500 服务端内部错误

### 注意与调试
- 该接口依赖本地 commits 表已有 sha；在调用此接口前，请先通过 POST /api/repos/{id}/sync 或其它方式同步 commits.
- 同步结果写入 commit_files 表(包含 commit_id/sha/filename/additions/deletions/changes/committed_at/raw_json). 
- 同步会在 repo_sync_runs 表记录一次运行(started -> ok/error),可查询该表定位错误信息。
- limit 参数用于限制本次调用处理的 commits 数量，默认 30；若仓库 commit 数量很多，请分批同步或按需调整 limit，避免触发 GitHub 速率限制。
- 当 sync_commit_files 失败时，函数会负责设置响应并在 repo_sync_runs 中记录错误（接口会直接返回失败状态并在服务端日志中打印相关信息）。

## 备注
- 若需新增路由或调整参数，请同时更新此文档与 routes_post.cpp 的注册代码.