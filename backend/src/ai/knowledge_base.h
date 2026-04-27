// 2.4(1) 工程知识库模块
// 将 issues/PRs/commits/releases 等数据索引为可检索的知识块，支撑 RAG 问答
#pragma once

#include <string>
#include <vector>

class Db;

// 单条知识块
struct KnowledgeChunk {
    int id = 0;
    int repo_id = 0;
    std::string source_type;   // "issue" / "pull_request" / "commit" / "release"
    std::string source_id;     // 原始标识，如 issue 编号、commit sha 前缀
    std::string title;
    std::string content;
    std::string author;
    std::string event_time;
    double score = 0.0;        // 检索相关度评分（越高越相关）
    std::vector<float> embedding; // 向量嵌入（可为空，表示尚未生成）
};

// 构建索引的结果统计
struct BuildIndexResult {
    int issues_indexed = 0;
    int pulls_indexed = 0;
    int commits_indexed = 0;
    int releases_indexed = 0;
    int embeddings_generated = 0;  // 成功生成向量的知识块数量
    int total() const { return issues_indexed + pulls_indexed + commits_indexed + releases_indexed; }
};

// 为指定仓库构建/重建知识索引（会先清除旧数据）
BuildIndexResult build_knowledge_index(Db& db, int repo_id);

// 关键词检索知识库，返回按相关度排序的 Top-K 结果。
// repo_id <= 0 表示检索所有仓库。
std::vector<KnowledgeChunk> search_knowledge(Db& db, int repo_id,
                                             const std::string& query, int top_k = 10);

// 向量相关工具函数
std::vector<float> call_embedding_api(const std::string& text);
double cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);

// 序列化为 JSON
std::string build_result_to_json(const BuildIndexResult& r);
std::string knowledge_chunks_to_json(const std::vector<KnowledgeChunk>& chunks);
