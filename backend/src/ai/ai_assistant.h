// 2.4(2) AI 问答助手模块
// 基于 RAG 流程: 检索知识库证据 → 组装 Prompt → 调用 LLM → 返回带引用的回答
#pragma once

#include <string>
#include <vector>

class Db;

// 一条引用证据
struct AiEvidence {
    int repo_id = 0;
    std::string source_type;   // "issue" / "pull_request" / "commit" / "release"
    std::string source_id;
    std::string title;
    std::string snippet;       // 内容摘要（前 200 字符）
};

// AI 回答结果
struct AiAnswer {
    std::string answer;                  // LLM 生成的回答文本
    std::vector<AiEvidence> evidence;    // 引用的证据列表
    std::string model;                   // 使用的模型名称
    bool success = false;
    std::string error;                   // 失败时的错误信息
};

// 完整 RAG 问答流程: 检索 → 组装上下文 → 调用 LLM → 格式化输出。
// repo_id <= 0 表示不限定仓库，走全局知识库问答。
AiAnswer ask_question(Db& db, int repo_id, const std::string& question);

// 序列化为 JSON
std::string ai_answer_to_json(const AiAnswer& answer);
