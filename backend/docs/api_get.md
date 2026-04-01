# GET 接口文档

## GET /api/health

### 描述
服务健康检查。

### 方法：
GET /api/health

### 请求参数
无

### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/health"

### 响应示例(200 OK)
{ "ok": true }
状态码：200

---

## GET /api/repos

### 描述
列出已注册的仓库（最多 200 条，按 id 降序）。

### 方法
GET /api/repos

### 请求参数
无

### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos" | ConvertTo-Json -Depth 10

### 响应示例(200 OK)
{
  "items": 
  [
    {"id": 2, "full_name": "owner/repo2", "enabled":1},
    {"id": 1, "full_name": "owner/repo1", "enabled":1}
  ]
}

### 状态码
200、500(DB 错误)

---

## GET /api/repos/{repo_id}

### 描述
获取单个仓库元信息(id, full_name, enabled)

### 方法
GET /api/repos/{repo_id}

### 请求参数
路径参数 repo_id(整数)

### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1" | ConvertTo-Json -Depth 10

### 响应示例(200 OK)
{ "id":1, "full_name":"owner/repo", "enabled":1 }
状态码：200、404(未找到)、500(DB 错误)

---

## GET /api/repos/{repo_id}/snapshots

### 描述
返回 repo_snapshots 中最近的快照(最多 100 条)。

### 方法
GET /api/repos/{repo_id}/snapshots

### 请求参数
路径参数 repo_id（整数）

### 示例（PowerShell）
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/snapshots" | ConvertTo-Json -Depth 10

### 响应示例(200 OK)
{ "items": [ {"id":10,"ts":"2026-03-01T..","stars":10,"forks":2,"open_issues":3,"watchers":5,"pushed_at":"2026-03-01T.."}, ... ] }
状态码：200、500(DB 错误)

---

## GET /api/repos/{repo_id}/issues

### 描述
分页查询 issues(包含是否为 PR 的标记)

### 方法
GET /api/repos/{repo_id}/issues

### 查询参数
- limit (int, default=100, max=200)
- offset (int, default=0)
- state (string, optional) — "open" / "closed" / 省略表示全部

### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/issues?limit=50&offset=0&state=open" | ConvertTo-Json -Depth 10

响应示例（200 OK）：
{ "items": [ { "number": 123, "state":"open", "title":"...", "created_at":"...", "updated_at":"...", "closed_at":"", "comments":0, "author_login":"u", "is_pull_request":0 }, ... ] }
状态码：200、500（DB 错误）

---

## GET /api/repos/{repo_id}/pulls

### 描述
分页查询 pull requests。

### 方法
GET /api/repos/{repo_id}/pulls

### 查询参数
- limit (int, default=100, max=200)
- offset (int, default=0)
- state (string, optional) — "open" / "closed" / 省略表示全部

### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/pulls?limit=50&state=closed" | ConvertTo-Json -Depth 10

### 响应示例(200 OK)
{ "items": [ { "number": 45, "state":"closed", "title":"...", "created_at":"...", "updated_at":"...", "closed_at":"...", "merged_at":"...", "author_login":"u" }, ... ] }
状态码：200、500(DB 错误)

---

## GET /api/repos/{repo_id}/commits

### 描述
分页查询 commits(按 committed_at 降序)

### 方法
GET /api/repos/{repo_id}/commits

### 查询参数
- limit (int, default=100, max=500)
- offset (int, default=0)

### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/commits?limit=100&offset=0" | ConvertTo-Json -Depth 10

### 响应示例(200 OK)
{ "items": [ { "sha":"abcd...", "author_login":"u", "committed_at":"2026-03-01T..." }, ... ] }
状态码：200、500（DB 错误）

---

## GET /api/repos/{repo_id}/releases

### 描述
分页查询 releases(按 published_at 降序)

### 方法
GET /api/repos/{repo_id}/releases

### 查询参数
- limit (int, default=100, max=200)
- offset (int, default=0)

### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/releases?limit=50" | ConvertTo-Json -Depth 10

### 响应示例(200 OK)
{ "items": [ { "tag_name":"v1.0","name":"Release 1","draft":0,"prerelease":0,"published_at":"2026-03-01T..." }, ... ] }
状态码：200、500（DB 错误）

---

## GET /api/repos/{repo_id}/metrics

### 描述
返回仓库协作/活跃度等计算后的度量(compute_repo_metrics)

### 方法
GET /api/repos/{repo_id}/metrics

### 请求参数
无(可在后续扩展 window 等)

### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/metrics" | ConvertTo-Json -Depth 10

### 响应示例(200 OK)
{ "metrics": { "commits_last_7d": 10, "active_contributors_30d": 5, "open_issues":3, "avg_issue_close_days":2.5, "prs_merged_last_30d":4, "prs_merge_rate":0.8, "releases_last_90d":1, "last_push":"2026-03-.." } }
状态码：200、500（计算或 DB 错误）

---

## GET /api/repos/{repo_id}/health

### 描述
基于 metrics 计算的仓库健康评分(compute_health_from_metrics)

### 方法
GET /api/repos/{repo_id}/health

### 请求参数
无

### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/health" | ConvertTo-Json -Depth 10

### 响应示例(200 OK)
{ "health": { "score": 78.5, "activity": 80, "responsiveness":70, "quality":85, "release":60 } }
状态码：200、500（计算或 DB 错误）

---

## GET /api/repos/{repo_id}/hotfiles

### 描述
返回仓库的“热点”文件(由 compute_hot_files() 计算)
 
### 方法
GET /api/repos/{repo_id}/hotfiles
 
### 参数
- days (int, 默认 = 0) : 考察的时间窗口（天）；0 表示不限（考虑全部历史）
- top  (int, 默认 = 20, 范围 1..200) : 要返回的最大文件数（会被夹到 [1,200]）
 
### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/hotfiles" | ConvertTo-Json -Depth 10

### 行为
- 从请求中读取 repo_id、days 和 top 参数。
- 调用 compute_hot_files(db, repo_id, days, top_n) 获取热点文件列表。
- 将结果序列化为 JSON 并设置响应的 Content-Type 为 "application/json"。
 
### 成功响应（HTTP 200）结构
{
    "items": 
    [
        {
            "filename": "<经过 JSON 转义的文件名>",
            "commits": <int>,
            "additions": <int>,
            "deletions": <int>
        },
        ...
    ]
}
 
### 注意
- 在将文件名包含到 JSON 中之前会进行 JSON 转义。
- days 会被强制为 >= 0；top 会被夹到 [1,200] 范围内（至少 1，最多 200）。
- 若发生内部错误（例如数据库错误或计算失败），应返回相应的错误状态（例如 500）。

---


## GET /api/repos/{repo_id}/hotdirs

### 描述
返回仓库的“热点”目录（由 compute_hot_dirs(db, res, repo_id, days, top_n, dir_depth) 计算）。

### 方法
GET /api/repos/{repo_id}/hotdirs

### 参数
- days (int, 默认 = 0)：考察的时间窗口（天）；0 表示不限（考虑全部历史）。
- top (int, 默认 = 20, 范围 1..200)：要返回的最大目录数（会被夹到 [1,200]）。
- dir_depth (int, 默认 = 2, 范围 1..10)：目录深度，1 表示顶层目录，较大值表示更深的目录层级。

### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/hotdirs" | ConvertTo-Json -Depth 10

### 行为
从请求中读取 repo_id、days、top 与 dir_depth 参数（并做边界限制）。
调用 compute_hot_dirs(db, res, repo_id, days, top_n, dir_depth) 获取热点目录列表；如果 compute_hot_dirs 将 res.status 设为 500，则直接返回（错误已由 compute_hot_dirs 填充）。
将结果序列化为 JSON 并设置响应的 Content-Type 为 "application/json"。

### 成功响应（HTTP 200）结构
{ "items": [ { "dirname": "<经过 JSON 转义的目录名>", "commits": <int>, "additions": <int>, "deletions": <int> }, ... ] }

### 注意
dirname 可能为空字符串（在代码中被转换为 ""），dir_depth 在服务端会被限制到 1..10。错误情况通常返回 HTTP 500 和相应的错误 JSON。

---

## GET /api/repos/{repo_id}/activity

### 描述
返回仓库在指定时间窗口内按日期分组的提交次数（基于 commits.committed_at 的日期部分聚合）。

### 方法
GET /api/repos/{repo_id}/activity

### 参数
days (int, 默认 = 30，最小 = 1)：向前回溯的天数窗口；如 days=30 则查询最近 30 天（包含今天）。

### 示例(PowerShell)
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/activity?days=30" | ConvertTo-Json -Depth 10

### 行为
从请求中读取 repo_id 和 days（days 至少为 1）。
构造时间窗口字符串，例如 "-30 days"，并在 SQL 查询中以 committed_at >= datetime('now', ?) 过滤。
执行 SQL，按 date(committed_at) 分组并按日期升序返回。
将结果序列化为 JSON 并设置响应的 Content-Type 为 "application/json"。
如果数据库准备/执行失败，返回 HTTP 500，并通常以 {"error":"db prepare failed"} 之类的 JSON 报错。

### 成功响应（HTTP 200）结构
{ "items": [ { "date": "YYYY-MM-DD", "commits": <int> }, ... ] }

### 注意
返回的 date 格式为 YYYY-MM-DD（由 SQL 的 date(committed_at) 产生）；空日期不会出现，未命中的日期不会在结果中列出（只返回有提交的日期）。


## 备注
- 所有返回的时间字段均为 ISO 8601 字符串（UTC），字符串字段已做简单 JSON 转义。  
- 若在 PowerShell 使用 Invoke-RestMethod 查看时出现截断，可使用:
- ($resp = Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/issues").items | ForEach-Object { $_ | Format-List -Property * -Force }
- 若需新增路由或调整参数，请同时更新此文档与 routes_get.cpp 的注册代码。

