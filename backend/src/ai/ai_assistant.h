// 2.4(2) AI 问答助手模块
// 基于 RAG 流程: 检索知识库证据 → 组装 Prompt → 调用 LLM → 返回带引用的回答
// 2.6 更新: 支持多轮对话上下文（thread_id）
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
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
    double cost_usd = 0.0;
    int duration_ms = 0;
    bool success = false;
    std::string error;                   // 失败时的错误信息
    int thread_id = 0;                   // 所属对话线程 ID（0 表示单轮，未关联线程）
    int conversation_id = 0;             // 本次保存的对话记录 ID
};

// 完整 RAG 问答流程: 检索 → 组装上下文 → 调用 LLM → 格式化输出。
// repo_id <= 0 表示不限定仓库，走全局知识库问答。
// thread_id > 0 时加载该线程的历史消息拼入 LLM 上下文，实现多轮对话。
AiAnswer ask_question(Db& db, int repo_id, const std::string& question,
                      int thread_id = 0);

// 创建新对话线程，返回 thread_id
int create_conversation_thread(Db& db, int repo_id, const std::string& title = "");

// 删除对话线程及其所有消息
bool delete_conversation_thread(Db& db, int thread_id);

// 序列化为 JSON
std::string ai_answer_to_json(const AiAnswer& answer);
