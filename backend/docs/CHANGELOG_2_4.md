# 2.4 AI 智能分析模块 - 开发者修改说明

## 概述

本次新增 **2.4(1) 工程知识库** 和 **2.4(2) AI 问答助手** 两个子模块，实现了基于 RAG（检索增强生成）的工程智能问答能力。

---

## 新增文件

| 文件 | 行数 | 职责 |
|------|------|------|
| `src/ai/knowledge_base.h` | 37 | 知识库接口：`KnowledgeChunk` 结构体、`build_knowledge_index()`、`search_knowledge()` |
| `src/ai/knowledge_base.cpp` | 303 | 知识库实现：从 issues/PRs/commits/releases 提取知识块入库；基于关键词的检索与评分排序 |
| `src/ai/ai_assistant.h` | 30 | AI 助手接口：`AiAnswer`、`AiEvidence` 结构体、`ask_question()` |
| `src/ai/ai_assistant.cpp` | 275 | RAG 完整流程：检索证据 → 拼装指标+证据上下文 → 调用 LLM API → 保存对话 → 返回带引用的回答 |
| `src/api/routes_ai.cpp` | 175 | 4 个 API 端点的路由注册与 handler |
| `config/config.env.example` | 20 | 配置文件模板（含 LLM 配置说明） |

**新增代码总量：约 840 行**

## 修改文件

| 文件 | 改动说明 |
|------|----------|
| `src/db/db.cpp` | 新增 `knowledge_chunks` 表和 `ai_conversations` 表（+25 行）；修复 `commit_files` 表的列名与约束不一致的问题（`repo_id` → `commit_id`） |
| `src/api/routes.cpp` | 声明并调用 `register_ai_routes()`（+2 行） |
| `CMakeLists.txt` | 新增 3 个源文件；重构为跨平台兼容版本（支持 vcpkg/系统包/conda） |
| `backend/.gitignore` | 新增 `config/config.env` 排除规则，防止密钥泄露 |

## 新增 API 接口

### 知识库

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/repos/{id}/knowledge/build` | 为指定仓库构建知识索引（从已同步的数据中提取） |
| `GET` | `/api/repos/{id}/knowledge/search?q=关键词&top=10` | 关键词检索知识库，返回按相关度排序的结果 |

### AI 问答

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/ai/ask` | AI 问答，请求体 `{"repo_id":1, "question":"..."}` |
| `GET` | `/api/ai/conversations?repo_id=1&limit=20` | 查询对话历史 |

## 新增数据库表

```sql
-- 知识块表
knowledge_chunks (id, repo_id, source_type, source_id, title, content, author, event_time, created_at)

-- AI 对话记录表
ai_conversations (id, repo_id, question, answer, evidence_json, model, created_at)
```

## 环境配置

### 新增配置项（config.env）

```
LLM_API_BASE=https://api.openai.com     # LLM API 地址（OpenAI 兼容格式）
LLM_API_KEY=sk-xxxxxxxxxxxxxxxxxxxx      # LLM API Key
LLM_MODEL=gpt-3.5-turbo                  # 模型名称
```

支持的 LLM 服务（任何兼容 OpenAI Chat Completions 格式的服务均可）：
- OpenAI：`https://api.openai.com`
- DeepSeek：`https://api.deepseek.com`
- 阿里通义千问：`https://dashscope.aliyuncs.com/compatible-mode/v1`
- 本地部署（如 Ollama）：`http://localhost:11434`

**注意**：不配置 LLM 相关字段时，知识库构建和检索功能仍可正常使用，仅 AI 问答不可用。

## 使用流程

```bash
# 1. 确保仓库已同步数据
POST /api/repos/1/sync

# 2. 构建知识索引（首次使用或数据更新后执行）
POST /api/repos/1/knowledge/build

# 3. 检索知识库（可独立使用，不需要 LLM）
GET /api/repos/1/knowledge/search?q=bug+fix&top=5

# 4. AI 问答（需要 LLM 配置）
POST /api/ai/ask  {"repo_id":1, "question":"最近有哪些严重的 bug？"}

# 5. 查看历史对话
GET /api/ai/conversations?repo_id=1
```

## 架构说明

```
用户提问
   │
   ▼
┌─────────────────────────┐
│  1. 关键词提取            │  从问题中提取中文/英文关键词
│  2. 知识库检索 (Top-10)   │  在 knowledge_chunks 表中 LIKE 匹配 + 评分排序
│  3. 获取当前指标          │  调用 compute_repo_metrics() + compute_health()
│  4. 组装 Prompt           │  系统提示 + 指标上下文 + 检索证据 + 用户问题
│  5. 调用 LLM API          │  OpenAI 兼容格式，HTTPS 请求
│  6. 返回带引用的回答      │  answer + evidence[] 列表
│  7. 保存对话记录          │  写入 ai_conversations 表
└─────────────────────────┘
```

## Bug 修复

- **`commit_files` 表定义错误**：原 schema 中列名定义为 `repo_id`，但 `UNIQUE` 约束和 `FOREIGN KEY` 引用的是 `commit_id`，导致在全新数据库上无法建表。已修正列名为 `commit_id`。
