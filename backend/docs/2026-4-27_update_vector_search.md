# 2026-4-27 向量检索改进 RAG 模块

## 改动背景
原有 RAG 检索完全基于 SQL `LIKE` 关键词匹配，无法处理语义相似但用词不同的查询（如用户问"性能问题"，知识库中是"fix slow query"）。本次引入 **Embedding 向量检索**，采用混合检索策略（关键词 + 向量），在不引入新外部依赖的前提下显著提升召回率。

## 技术方案
- **Embedding 模型**: DashScope `text-embedding-v3`（复用现有 LLM API Key）
- **向量存储**: SQLite BLOB 列（原始 float32 字节序列化）
- **向量检索**: 纯 C++ 内存暴力搜索（cosine similarity），适用于万级以内的知识块规模
- **检索策略**: 混合检索 — 关键词归一化分数 * 0.4 + 向量相似度 * 0.6，合并去重后取 Top-K
- **降级机制**: Embedding API 不可用时自动退回纯关键词检索，不影响现有功能

## 修改文件清单

| 文件 | 改动类型 | 改动行数 | 说明 |
|------|---------|---------|------|
| `config/config.env.example` | 修改 | +7 | 新增 `EMBEDDING_API_BASE`、`EMBEDDING_API_KEY`、`EMBEDDING_MODEL` 配置项 |
| `src/db/db.cpp` | 修改 | +7 | `knowledge_chunks` 表新增 `embedding BLOB` 列（ALTER TABLE 迁移） |
| `src/ai/knowledge_base.h` | 修改 | +5 | `KnowledgeChunk` 新增 `embedding` 字段；`BuildIndexResult` 新增 `embeddings_generated`；新增函数声明 |
| `src/ai/knowledge_base.cpp` | 修改 | +约300 | 核心改动：新增 Embedding API 调用、cosine similarity、批量向量生成、向量检索、混合检索逻辑；重构 `search_knowledge` 为混合检索 |
| `backend/docs/2026-4-27_update_vector_search.md` | 新增 | 本文件 | 修改日志 |

### 新增代码统计（约 320 行）
- `call_embedding_api_batch()` — 批量调用 Embedding API（~80 行）
- `call_embedding_api()` — 单条便捷接口（~10 行）
- `cosine_similarity()` — 向量余弦相似度计算（~10 行）
- `serialize_embedding()` / `deserialize_embedding()` — 向量序列化/反序列化（~15 行）
- `store_embedding()` — 存储向量到 DB（~10 行）
- `generate_embeddings_for_repo()` — 批量生成某仓库所有知识块的向量（~50 行）
- `vector_search()` — 向量检索路径（~50 行）
- `fallback_recent_chunks()` — 兜底逻辑提取为独立函数（~30 行）
- `keyword_search()` — 原关键词检索逻辑提取为独立函数（~60 行）
- 混合合并逻辑在重写的 `search_knowledge()` 中（~50 行）
- URL 解析、结构体定义等辅助代码（~30 行）

### 修改代码统计（约 20 行）
- `build_knowledge_index()` 末尾新增一行调用 `generate_embeddings_for_repo()`
- `build_result_to_json()` 新增 `embeddings_generated` 字段输出
- `search_knowledge()` 完全重写为混合检索入口

## API 变化
- `POST /api/repos/{id}/knowledge/build` 返回值新增 `embeddings_generated` 字段
- 其余 API 接口无变化，完全向后兼容

## 配置说明
在 `config/config.env` 中添加（不配则自动退回纯关键词检索）：
```
EMBEDDING_MODEL=text-embedding-v3
# 以下两项不配时自动复用 LLM_API_BASE 和 LLM_API_KEY
# EMBEDDING_API_BASE=https://dashscope.aliyuncs.com/compatible-mode/v1
# EMBEDDING_API_KEY=sk-xxxx
```

## 验证方式
1. 编译通过
2. 启动服务，调用 `POST /api/repos/{id}/knowledge/build`，确认返回 `embeddings_generated > 0`
3. 调用 `POST /api/ai/ask` 测试语义类问题（如"代码质量下降"）能否检索到相关 chunk
4. 清空 `EMBEDDING_API_KEY` 后重启，确认仍能退回纯关键词检索
