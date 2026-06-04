---

**前端 API 使用说明（详细版）**

**Base URL**
- 本地开发默认：`http://127.0.0.1:8080`
- 示例统一用 `$BaseUrl` 变量。

**通用约定**
- 所有响应为 JSON。
- POST/PUT 建议 `Content-Type: application/json`。
- 分页接口统一使用 `limit` / `offset`。
- 需要 GitHub 数据的接口依赖后端环境变量 `GITHUB_TOKEN`。
- 质量分析依赖外部工具（`cppcheck`、`clang-tidy`、`pylint`、`checkstyle` 等），未安装会失败。

**返回结构通用说明**
- 成功响应通常包含 `ok: true` 或 `items` 列表。
- 失败响应通常为 `{ "error": "..." }`，并返回 4xx/5xx 状态码。

---

**一、健康检查**

**GET /api/health**
- 描述：服务是否可用。
- 参数：无。
- 使用场景：前端启动时探活、健康检测。
- 示例：
```powershell
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/health"
```
- 成功响应：
```json
{ "ok": true }
```

---

**二、仓库管理（Repo）**

**POST /api/repos**
- 描述：创建仓库元数据（只建表，不同步）。
- 参数（query）：
  - `full_name` (string, required) — 仓库全名 `owner/repo`
- 使用场景：用户第一次录入仓库。
- 示例：
```powershell
Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/repos?full_name=octocat/Hello-World"
```
- 成功响应：
```json
{ "ok": true, "repo_id": 1, "full_name": "octocat/Hello-World" }
```
- 常见错误：
  - 400：缺少 `full_name`
  - 409：仓库已存在

**GET /api/repos**
- 描述：列出已注册仓库（最多 200 条）。
- 参数：无。
- 示例：
```powershell
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos"
```
- 成功响应：
```json
{ "items": [ { "id": 1, "full_name": "owner/repo", "enabled": 1 } ] }
```

**GET /api/repos/{repo_id}**
- 描述：获取仓库信息。
- 参数：路径参数 `repo_id`。
- 成功响应：
```json
{ "id": 1, "full_name": "owner/repo", "enabled": 1 }
```

**PUT /api/repos/{repo_id}?enabled=0|1**
- 描述：启用/禁用仓库。
- 参数（query）：`enabled` 只能是 `0` 或 `1`。
- 示例：
```powershell
Invoke-RestMethod -Method Put -Uri "$BaseUrl/api/repos/1?enabled=0"
```
- 成功响应：
```json
{ "ok": true, "id": 1, "enabled": 0 }
```

**DELETE /api/repos/{repo_id}**
- 描述：删除仓库记录，同时尝试删除本地 clone 目录。
- 示例：
```powershell
Invoke-RestMethod -Method Delete -Uri "$BaseUrl/api/repos/1"
```
- 成功响应：
```json
{ "ok": true }
```

---

**三、仓库同步（GitHub 数据）**

**POST /api/repos/{repo_id}/sync**
- 描述：从 GitHub 同步 snapshot / issues / pulls / commits / releases。
- 参数（query）：
  - `mode` = `incremental`（默认）| `full`
  - `issues_page_start`, `issues_pages_count`
  - `pulls_page_start`, `pulls_pages_count`
  - `commits_page_start`, `commits_pages_count`
  - `releases_page_start`, `releases_pages_count`
- 使用场景：首次或增量同步仓库数据。
- 示例：
```powershell
Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/repos/1/sync?mode=full&issues_pages_count=3&pulls_pages_count=3&commits_pages_count=5"
```
- 成功响应：
```json
{ "ok": true, "repo_id": 1, "issues_upserted": 100, "pulls_upserted": 20, "commits_upserted": 200, "releases_upserted": 2 }
```

**POST /api/repos/{repo_id}/sync/commit_files**
- 描述：拉取 commit 的文件变更，生成热力图基础数据。
- 参数（query）：`limit` (默认 30)。
- 示例：
```powershell
Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/repos/1/sync/commit_files?limit=20"
```
- 成功响应：
```json
{ "ok": true, "repo_id": 1, "limit_commits": 20, "total_files_processed": 2345 }
```

---

**四、仓库数据查询（Issues / PR / Commits / Releases）**

**GET /api/repos/{repo_id}/issues**
- 参数：`limit`(1..200), `offset`, `state`=open|closed|空。
- 示例：
```powershell
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/issues?limit=50&state=open"
```
- 成功响应：
```json
{ "items": [ { "number": 123, "state": "open", "title": "...", "author_login": "...", "is_pull_request": 0 } ] }
```

**GET /api/repos/{repo_id}/pulls**
- 参数：`limit`(1..200), `offset`, `state`=open|closed|空。
- 成功响应：
```json
{ "items": [ { "number": 45, "state": "closed", "merged_at": "...", "author_login": "..." } ] }
```

**GET /api/repos/{repo_id}/commits**
- 参数：`limit`(1..500), `offset`。
- 成功响应：
```json
{ "items": [ { "sha": "...", "author_login": "...", "committed_at": "..." } ] }
```

**GET /api/repos/{repo_id}/releases**
- 参数：`limit`(1..200), `offset`。
- 成功响应：
```json
{ "items": [ { "tag_name": "v1.0", "name": "...", "draft": 0, "prerelease": 0 } ] }
```

---

**五、仓库指标与健康**

**GET /api/repos/{repo_id}/metrics**
- 描述：仓库活跃度/协作指标。
- 示例：
```powershell
Invoke-RestMethod -Method Get -Uri "$BaseUrl/api/repos/1/metrics"
```
- 成功响应：
```json
{ "metrics": { "commits_last_7d": 10, "active_contributors_30d": 5, "open_issues": 3 } }
```

**GET /api/repos/{repo_id}/health**
- 描述：基于 metrics 计算的健康分。
- 成功响应：
```json
{ "health": { "score": 78.5, "activity": 80, "responsiveness": 70, "quality": 85, "release": 60 } }
```

**GET /api/repos/{repo_id}/score**
- 描述：综合评分（健康 + 质量）。
- 参数：`tool` (默认 `cppcheck`)。
- 成功响应：
```json
{ "overall": 82.1, "weights": { "health": 0.6, "quality": 0.4 }, "health": { ... }, "quality": { ... } }
```

---

**六、热点与活动趋势**

**GET /api/repos/{repo_id}/hotfiles**
- 参数：`days`(默认 0), `top`(1..200)。
- 成功响应：
```json
{ "items": [ { "filename": "src/main.cpp", "commits": 12, "additions": 80, "deletions": 20 } ] }
```

**GET /api/repos/{repo_id}/hotdirs**
- 参数：`days`(默认 0), `top`(1..200), `dir_depth`(1..10)。
- 成功响应：
```json
{ "items": [ { "dirname": "src/ai", "commits": 20, "additions": 300, "deletions": 120 } ] }
```

**GET /api/repos/{repo_id}/activity**
- 参数：`days`(默认 30, 最小 1)。
- 成功响应：
```json
{ "items": [ { "date": "2026-05-01", "commits": 5 } ] }
```

---

**七、CI 运行与健康**

**GET /api/repos/{repo_id}/ci/runs**
- 参数：`limit`(1..200), `offset`, `status`, `conclusion`。
- 成功响应：
```json
{ "items": [ { "run_id": 123, "name": "CI", "status": "completed", "conclusion": "success" } ] }
```

**GET /api/repos/{repo_id}/ci/health**
- 描述：CI 健康度（24h 失败率 + 连续失败）。
- 成功响应：
```json
{ "repo_id": 1, "health_level": "warning", "score": 76.5, "failure_rate_24h": 0.2 }
```

**GET /api/repos/{repo_id}/ci/trend**
- 参数：`days`(1..60)。
- 成功响应：
```json
{ "repo_id": 1, "days": 7, "items": [ { "date": "2026-05-01", "completed": 5, "failed": 1 } ] }
```

---

**八、质量分析（Quality）**

**POST /api/repos/{repo_id}/quality/analyze**
- 描述：触发分析（支持 C/C++/Python/Java）。
- Body（JSON）：
```json
{ "tools": "cppcheck,clang-tidy,pylint", "ref": "main", "mode": "full", "max_files": 2000, "config": {}, "path": "" }
```
- 说明：
  - `tools` 支持 `cppcheck`/`clang-tidy`/`cpplint`/`flawfinder`/`pylint`/`checkstyle`
  - `path` 可选，限制为指定文件或目录（相对仓库根）
- 成功响应：
```json
{ "ok": true, "status": "Finished", "run_id": 42, "analyzed_files": 156, "issues_new": 12 }
```

**GET /api/repos/{repo_id}/quality/issues**
- 参数：`tool`, `severity`, `status`(active|fixed|ignored|false_positive|all), `limit`(1..500), `offset`
- 成功响应：
```json
{ "items": [ { "id": 1, "tool": "cppcheck", "file_path": "src/main.cpp", "line": 42, "severity": "error" } ], "limit": 100, "offset": 0, "total": 123 }
```

**GET /api/repos/{repo_id}/quality/summary**
- 描述：评分与严重性汇总。
- 成功响应：
```json
{ "quality": { "score": 78.5, "total_issues": 87, "severity": { "error": 8 } }, "latest_run": { ... } }
```

**GET /api/repos/{repo_id}/quality/trend**
- 参数：`limit`(默认 20)。
- 成功响应：
```json
{ "items": [ { "run_id": 42, "score": 78.5, "issues_total": 87, "started_at": "..." } ] }
```

**GET /api/repos/{repo_id}/quality/top**
- 参数：`by`=file|dir|rule, `tool`, `limit`。
- 成功响应：
```json
{ "by": "file", "items": [ { "name": "src/main.cpp", "total": 12, "errors": 3 } ] }
```

**GET /api/repos/{repo_id}/quality/insights**
- 参数：`tool`(可选)。
- 描述：返回风险级别、热点文件、规则、趋势变化与建议动作。

**GET /api/repos/{repo_id}/quality/baseline**
- 描述：查看基线阈值。

**PUT /api/repos/{repo_id}/quality/baseline**
- Body：
```json
{ "min_score": 75.0, "max_new_issues": 30, "max_error_issues": 5 }
```

**POST /api/repos/{repo_id}/quality/tasks**
- Body：
```json
{ "tools": "cppcheck", "mode": "full", "max_files": 2000, "schedule": "manual", "run_now": false, "config": {} }
```

**GET /api/repos/{repo_id}/quality/tasks**
- 参数：`status`, `limit`, `offset`。

**POST /api/quality/tasks/{task_id}/run**
- 描述：立即运行某个任务。

**GET /api/repos/{repo_id}/quality/runs**
- 参数：`limit`, `offset`。

**PUT /api/quality/issues/{issue_id}**
- Body：
```json
{ "status": "fixed" }
```

**DELETE /api/quality/tasks/{task_id}**
**DELETE /api/quality/runs/{run_id}**
**DELETE /api/quality/issues/{issue_id}**

---

**九、代码树（前端文件/目录选择）**

**GET /api/repos/{repo_id}/tree**
- 参数：
  - `ref` (默认 `main`)
  - `max` (默认 5000, 最大 50000)
- 成功响应：
```json
{ "items": [ { "path": "src/app", "type": "dir" }, { "path": "src/app/main.cpp", "type": "file" } ], "truncated": false, "max": 5000 }
```

---

**十、AI / 知识库**

**POST /api/repos/{repo_id}/knowledge/build**
- 描述：构建知识索引（issues / PR / commits / releases）。
- 成功响应：
```json
{ "ok": true, "repo_id": 1, "result": { "issues_indexed": 10, "pulls_indexed": 5, "embeddings_generated": 100 } }
```

**GET /api/repos/{repo_id}/knowledge/search**
- 参数：`q`(必填), `top`(1..50)。
- 成功响应：
```json
{ "items": [ { "source_type": "issue", "source_id": "123", "title": "...", "snippet": "...", "score": 0.89 } ] }
```

**POST /api/ai/ask**
- Body：
```json
{ "repo_id": 1, "question": "最近的质量问题有哪些？" }
```
- 成功响应：
```json
{ "answer": "...", "evidence": [ { "source_type": "issue", "source_id": "123", "title": "...", "snippet": "..." } ], "model": "deepseek-chat", "success": true }
```

**GET /api/ai/conversations**
- 参数：`repo_id`(可选), `limit`(1..100)
- 成功响应：
```json
{ "items": [ { "id": 1, "repo_id": 1, "question": "...", "answer": "...", "model": "...", "created_at": "..." } ] }
```

**GET /api/ai/conversations/{id}**
- 描述：获取单条对话。

---

**十一、代码索引（向量检索）**

**POST /api/repos/{repo_id}/code/index**
- 参数（query）：`ref`, `mode`(full/hot), `max_files`, `max_total_kb`
- 成功响应：
```json
{ "ok": true, "repo_id": 1, "repo_head_sha": "...", "indexed_files": 120, "indexed_chunks": 400 }
```

---

**十二、专家识别**

**GET /api/repos/{repo_id}/experts**
- 参数：`top`(1..100)
- 成功响应：
```json
{ "repo_id": 1, "items": [ { "author": "alice", "score": 0.32 } ] }
```

**GET /api/repos/{repo_id}/experts/module**
- 参数：`dir`(必填), `top`(1..50)
- 成功响应：
```json
{ "repo_id": 1, "module": "src/ai/", "items": [ { "author": "bob", "score": 0.41 } ] }
```

**POST /api/repos/{repo_id}/experts/build**
- 描述：重建专家知识库。

---

**十三、周报**

**POST /api/repos/{repo_id}/report/generate**
- 描述：生成周报（可能较慢）。

**GET /api/repos/{repo_id}/reports**
- 参数：`limit`(1..50)

**GET /api/repos/{repo_id}/reports/{report_id}**

**GET /api/repos/{repo_id}/report/latest**

---

**十四、风险检测**

**POST /api/repos/{repo_id}/risk/scan**
- 参数：`days`(7..180, 默认 30)
- 描述：扫描并生成风险告警。

**GET /api/repos/{repo_id}/risk/alerts**
- 参数：`status`, `severity`, `limit`, `offset`

**GET /api/repos/{repo_id}/risk/alerts/summary**
- 参数：`days`(1..180)

---

**十五、系统日志**

**GET /api/system/logs/operations**
- 参数：`type`, `status`, `limit`, `offset`

**GET /api/system/logs/ai-usage**
- 参数：`repo_id`, `limit`, `offset`

**GET /api/system/logs/stats/today**
- 描述：当天请求/错误/AI 调用统计

---

**使用建议（新手快速上手）**
1. 先 `POST /api/repos` 创建仓库。
2. 用 `POST /api/repos/{id}/sync` 同步 GitHub 数据。
3. 打开 Quality 页面，调用 `POST /api/repos/{id}/quality/analyze` 完成质量分析。
4. 使用 `GET /api/repos/{id}/quality/issues` / `summary` / `trend` 展示质量面板。
5. 如需 AI 问答，先 `POST /api/repos/{id}/knowledge/build` 再用 `POST /api/ai/ask`。
