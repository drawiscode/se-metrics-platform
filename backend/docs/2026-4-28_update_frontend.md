
# 2026-4-28 前端更新：周报页面 + 隐形专家页面（并补齐依赖接口/修复）

## 概述

本次更新围绕 README 的 2.4(4) 自动周报与 2.4(5) 隐形专家识别，补齐了**前端承载页面与入口**，同时针对 Windows 环境下“仓库介绍 intro 的 prompt 入库乱码”问题，将该 prompt 调整为 ASCII-only 英文提示（仍要求模型输出中文），降低编码链路风险。

主要结果：
- 新增周报页面：`/repos/:id/reports`
- 新增专家页面（方案 2）：`/repos/:id/experts`
- 仓库列表/仓库详情均新增入口按钮，使用体验对齐
- 后端新增周报详情接口：`GET /api/repos/{id}/reports/{report_id}`（前端查看历史周报时按 id 拉全文）
- 仓库介绍生成 prompt 改为英文（ASCII-only）以规避 Windows 编码乱码

---

## 新增/修改文件

### 新增文件（前端）

| 文件 | 说明 |
|------|------|
| `frontend/src/views/WeeklyReportsView.vue` | 周报页：最新/历史/查看/生成 |
| `frontend/src/views/ExpertsView.vue` | 专家页：全局榜/模块榜/重算 |

### 修改文件（前端）

| 文件 | 改动说明 |
|------|----------|
| `frontend/src/main.js` | 新增路由：`/repos/:id/reports`、`/repos/:id/experts` |
| `frontend/src/views/ReposView.vue` | 仓库列表快捷入口新增「周报」「专家」按钮（与任务/AI并列） |
| `frontend/src/views/RepoDetailView.vue` | 顶部操作区新增「隐形专家」入口跳转专家页 |

### 修改文件（后端：为前端补齐依赖）

| 文件 | 改动说明 |
|------|----------|
| `backend/src/api/routes_report.cpp` | 新增周报全文详情接口：`GET /api/repos/{id}/reports/{report_id}` |
| `backend/src/report/weekly_report.h` / `weekly_report.cpp` | 新增 `get_weekly_report_by_id()`：按 id 获取指定周报全文 |
| `backend/src/api/routes_post.cpp` | `build_repo_intro_prompt()` 改为 ASCII-only 英文提示（仍要求中文输出） |

---

## 路由与入口

### (1) 周报页面
- 页面路由：`/repos/:id/reports`
- 入口：仓库列表页每行快捷按钮「周报」

### (2) 隐形专家页面（方案 2）
- 页面路由：`/repos/:id/experts`
- 入口（推荐）：
	1. 仓库详情页顶部操作区「隐形专家」（符合“仓库洞察概览”）
	2. 仓库列表页每行快捷按钮「专家」（访问成本更低）

---

## 前端页面功能与接口对接

### 周报页面（/repos/:id/reports）
- 最新周报：`GET /api/repos/{id}/report/latest`
- 历史列表：`GET /api/repos/{id}/reports?limit=10`
- 查看指定周报：`GET /api/repos/{id}/reports/{report_id}`
- 生成周报：`POST /api/repos/{id}/report/generate`

说明：历史列表接口为控制响应体积，不返回 `report_text` 正文，前端点击“查看”时按 id 再拉一次全文。

### 专家页面（/repos/:id/experts）
- 全局专家榜：`GET /api/repos/{id}/experts?top=20`
	- 展示：排名、login、primary_module、commit_count、files_touched、last_active、pagerank
- 模块专家榜：`GET /api/repos/{id}/experts/module?dir=src/ai&top=10`
	- 展示：排名、login、commit_count、lines_changed
- 重新计算并写入知识库：`POST /api/repos/{id}/experts/build`
	- 返回：`knowledge_chunks_written`

---

## 关键修复说明

### (1) “查看周报不显示正文”
- 根因：周报历史列表序列化刻意省略 `report_text`（仅返回预览/摘要字段），前端直接展示 `selected.report_text` 会为空。
- 修复：新增按 id 获取周报全文的详情接口；前端点击查看时调用详情接口再展示正文。

### (2) intro 生成 prompt 入库乱码（Windows 编码链路）
- 现象：`build_repo_intro_prompt()` 使用中文字符串字面量时，在 Windows 环境下写入 `ai_conversations.question` 可能出现乱码。
- 修复：将该 prompt 改为 ASCII-only 英文提示，但明确要求输出中文（降低源文件/控制台编码对入库文本的影响）。

---



## 代码增删统计

- 统计口径：`git diff --cached --numstat`
- 总计新增：461
- 总计删除：6

7       6       backend/src/api/routes_post.cpp
28      0       backend/src/api/routes_report.cpp
28      0       backend/src/report/weekly_report.cpp
3       0       backend/src/report/weekly_report.h
4       0       frontend/src/main.js
197     0       frontend/src/views/ExpertsView.vue
1       0       frontend/src/views/RepoDetailView.vue
2       0       frontend/src/views/ReposView.vue
188     0       frontend/src/views/WeeklyReportsView.vue

