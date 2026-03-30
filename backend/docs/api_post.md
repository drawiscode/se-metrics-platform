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
根据repo_id读取本地的full_name,通过full_name和token从GitHub上同步信息

### 方法与路径
POST /api/repos/{repo_id}/sync

### 参数(均为可选参数，均为正整数)

- issues_page_start (int, default=1) — issues 起始页
- issues_pages_count (int, default=1) — issues 要拉取的页数数
- pulls_page_start (int, default=1) — pulls 起始页
- pulls_pages_count (int, default=1) — pulls 要拉取的页数
- commits_page_start (int, default=1) — commits 起始页
- commits_pages_count (int, default=1) — commits 要拉取的页数
- releases_page_start (int, default=1) — releases 起始页
- releases_pages_count (int, default=1) — releases 要拉取的页数

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
- 同步使用服务端环境变量中的 GITHUB_TOKEN(从 env 读取),请确保服务有权限访问对应仓库.  
- 同步过程会写入 repo_sync_runs 表(用于查看历史与错误信息).  
- 若需全量拉取直到末页，请将 pages_count 设置为较大数或在代码中调整默认行为(当前默认仅 1 页以避免误触发超额请求).

---

## POST /api/repos/{repo_id}/sync/commit_files

### 描述
针对指定仓库,遍历本地 commits 表中已保存的 commit(按时间倒序),逐条调用 GitHub Commit API 拉取该 commit 的 files 信息,并将每个文件的 additions/deletions/churn 等写入本地 commit_files 表.用于生成代码热力图与模块热点数据.

### 方法与路径
POST /api/repos/{repo_id}/sync/commit_files

### Query 参数
- （当前实现无必需 query 参数；未来可扩展 limit/start 等分页参数）

### 请求示例（PowerShell）
Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/repos/1/sync/commit_files"


### 响应示例（200 OK）
{
  "ok": true,
  "repo_id": 1,
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
- 同步会在 repo_sync_runs 表记录一次运行(started -> ok/error),可查询该表定位错误信息.
- 若仓库 commit 数量很多，请分批同步或按需扩展接口以支持 limit/start 参数，避免触发 GitHub 速率限制.

## 备注
- 若需新增路由或调整参数，请同时更新此文档与 routes_get.cpp 的注册代码.