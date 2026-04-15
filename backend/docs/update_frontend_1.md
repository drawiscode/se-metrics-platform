# 前端（Vue + Vite）接入 - 更新说明

## 概述

本次更新增加了一个**独立前端**（Vue 3 + Vite，JS 模板），用于对接并展示后端已实现的 **2.1 仓库协作度量** 与 **2.4 工程知识库 / AI 问答** 能力。

- 新增：`frontend/` 前端工程（与后端独立启动、独立开发）
- 新增：开发期使用 Vite Proxy 代理 `/api/*` → `http://127.0.0.1:8080`，后端**无需开启 CORS**
- 实现：仓库列表 / 仓库详情（metrics、health、activity、hotfiles、hotdirs）/ AI 问答与对话历史（含详情页）

---

## 新增/修改文件

### 新增目录

| 目录 | 职责 |
|------|------|
| `frontend/` | 前端工程根目录（Vue 3 + Vite，独立于后端） |
| `frontend/src/api/` | 前端 API 请求封装（统一错误处理） |
| `frontend/src/views/` | 页面：Repos / RepoDetail / AI |

### 新增/修改文件（前端）

| 文件 | 改动说明 |
|------|----------|
| `frontend/vite.config.js` | 配置 dev server 代理：`/api` 转发到后端 `http://127.0.0.1:8080` |
| `frontend/package.json` | 前端依赖与脚本：`vue`、`vue-router@4`、`vite` |
| `frontend/src/main.js` | 注册 Router；路由：`/repos`、`/repos/:id`、`/ai`、`/ai/conversations/:id` |
| `frontend/src/App.vue` | 基础布局：顶部导航 + RouterView |
| `frontend/src/api/client.js` | `apiGet/apiPost` + `ApiError`，统一处理 HTTP 错误与 JSON 解析 |
| `frontend/src/views/RepoView.vue` 或 `frontend/src/views/ReposView.vue` | 仓库列表：展示 repos；支持添加 repo；支持触发 sync |
| `frontend/src/views/RepoDetailView.vue` | 仓库详情：metrics/health/activity/hotfiles/hotdirs |
| `frontend/src/views/AiView.vue` | AI 问答页：POST `/api/ai/ask`；对话历史：GET `/api/ai/conversations`；输入 repo_id 自动刷新历史 |
| `frontend/src/views/AiConversationDetailView.vue` | AI 对话详情页：展示单条对话 question/answer/evidence |

---

## 前后端通信流程

前端页面中调用：
- `apiGet('/api/repos')`

开发期浏览器实际访问为：
- `http://127.0.0.1:5173/api/repos`

由于 `vite.config.js` 设置了代理，Vite 会将请求转发为：
- `http://127.0.0.1:8080/api/repos`

后端由 `routes_get.cpp` 中 `register_get_routes()` 注册的：
- `GET /api/repos` → `get_repos_handler()` → SQLite 查询 `repos` 表 → 返回 `{ "items": [...] }`

同理，AI 页面对接的接口为：
- `POST /api/ai/ask`
- `GET /api/ai/conversations?repo_id=...&limit=...`
- `GET /api/ai/conversations/{id}`（对话详情）

---

## 使用方式

### (1) 启动后端（8080）

确保已配置 `backend/config/config.env`，然后启动后端。

健康检查：
- `GET http://127.0.0.1:8080/api/health`

### (2) 启动前端（5173）

在仓库根目录打开 PowerShell：

```powershell
cd E:\study\SoftwareLab\lab\se-metrics-platform\frontend
npm install
npm run dev