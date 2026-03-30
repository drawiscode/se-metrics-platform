// 2.4(2) AI 问答助手 - 实现
#include "ai_assistant.h"
#include "knowledge_base.h"
#include "db/db.h"
#include "common/util.h"
#include "repo_metrics/metrics.h"
#include "repo_metrics/health.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <iostream>
#include <algorithm>

// ============================================================
// 辅助: URL 解析（用于 LLM API 调用）
// ============================================================
struct UrlParts {
    std::string host;
    int port = 443;
    bool use_ssl = true;
    std::string path_prefix;  // 例如 "/v1" 之类的前缀路径
};

static UrlParts parse_url(const std::string& url)
{
    UrlParts p;
    std::string rest = url;

    // 解析协议
    if (rest.rfind("https://", 0) == 0) {
        rest = rest.substr(8);
        p.use_ssl = true;
        p.port = 443;
    } else if (rest.rfind("http://", 0) == 0) {
        rest = rest.substr(7);
        p.use_ssl = false;
        p.port = 80;
    }

    // 解析路径
    auto slash = rest.find('/');
    if (slash != std::string::npos) {
        p.path_prefix = rest.substr(slash);
        rest = rest.substr(0, slash);
    }

    // 解析端口
    auto colon = rest.find(':');
    if (colon != std::string::npos) {
        p.host = rest.substr(0, colon);
        try { p.port = std::stoi(rest.substr(colon + 1)); } catch (...) {}
    } else {
        p.host = rest;
    }

    // 去除尾部斜杠
    while (!p.path_prefix.empty() && p.path_prefix.back() == '/')
        p.path_prefix.pop_back();

    return p;
}

// ============================================================
// 辅助: 获取仓库的 full_name
// ============================================================
static std::string get_repo_full_name(Db& db, int repo_id)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    std::string full_name;
    const char* sql = "SELECT full_name FROM repos WHERE id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            auto txt = sqlite3_column_text(stmt, 0);
            if (txt) full_name = (const char*)txt;
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return full_name;
}

// ============================================================
// 辅助: 构建系统提示词
// ============================================================
static std::string build_system_prompt(const std::string& full_name)
{
    return
        "你是一个软件工程 AI 助手，负责分析项目 '" + full_name + "' 的开发数据。\n"
        "你可以回答关于项目历史、协作模式、代码质量、团队动态等方面的问题。\n"
        "回答要求:\n"
        "1. 基于提供的证据数据作答，引用具体证据（如 'Issue #123', 'PR #45', 'Commit abc1234'）。\n"
        "2. 如果证据不足，明确指出还需要补充哪些数据。\n"
        "3. 回答要简洁、可操作。\n"
        "4. 使用与用户提问相同的语言回答。\n";
}

// ============================================================
// 辅助: 组装上下文（当前指标 + 检索到的证据）
// ============================================================
static std::string build_context(Db& db, int repo_id,
                                 const std::vector<KnowledgeChunk>& evidence)
{
    std::string ctx;

    // 当前项目指标概览
    try {
        RepoMetrics m = compute_repo_metrics(db, repo_id);
        HealthScore h = compute_health_from_metrics(m);

        ctx += "## 当前项目指标\n";
        ctx += "- 最近 7 天提交数: " + std::to_string(m.commits_last_7d) + "\n";
        ctx += "- 最近 30 天活跃贡献者: " + std::to_string(m.active_contributors_30d) + "\n";
        ctx += "- 当前打开的 Issue 数: " + std::to_string(m.open_issues) + "\n";
        ctx += "- Issue 平均关闭天数: " + std::to_string(m.avg_issue_close_days) + "\n";
        ctx += "- 最近 30 天合并的 PR: " + std::to_string(m.prs_merged_last_30d) + "\n";
        ctx += "- PR 合并率: " + std::to_string(static_cast<int>(m.prs_merge_rate * 100)) + "%\n";
        ctx += "- 最近 90 天 Release 数: " + std::to_string(m.releases_last_90d) + "\n";
        ctx += "- 健康度评分: " + std::to_string(static_cast<int>(h.score)) + "/100\n";
        ctx += "  - 活跃度: " + std::to_string(static_cast<int>(h.activity)) + "\n";
        ctx += "  - 响应速度: " + std::to_string(static_cast<int>(h.responsiveness)) + "\n";
        ctx += "  - 协作质量: " + std::to_string(static_cast<int>(h.quality)) + "\n";
        ctx += "  - 发布节奏: " + std::to_string(static_cast<int>(h.release)) + "\n\n";
    } catch (...) {
        ctx += "## 当前指标: 无法获取\n\n";
    }

    // 检索到的相关证据
    if (!evidence.empty()) {
        ctx += "## 相关证据\n";
        for (size_t i = 0; i < evidence.size(); i++) {
            const auto& e = evidence[i];
            ctx += "[证据 " + std::to_string(i + 1) + "] ";
            ctx += "(" + e.source_type + " #" + e.source_id + ") ";
            ctx += e.title + "\n";
            ctx += e.content + "\n\n";
        }
    }

    return ctx;
}

// ============================================================
// 辅助: 保存对话记录到数据库
// ============================================================
static void save_conversation(Db& db, int repo_id,
                              const std::string& question,
                              const std::string& answer,
                              const std::string& evidence_json,
                              const std::string& model)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO ai_conversations(repo_id, question, answer, evidence_json, model) "
        "VALUES (?1, ?2, ?3, ?4, ?5);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int (stmt, 1, repo_id);
        sqlite3_bind_text(stmt, 2, question.c_str(),      -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, answer.c_str(),         -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, evidence_json.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, model.c_str(),          -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
}

// ============================================================
// 辅助: 调用 LLM API（兼容 OpenAI Chat Completions 格式）
// 支持 OpenAI / DeepSeek / 本地兼容服务
// ============================================================
static std::string call_llm(const std::string& system_prompt,
                            const std::string& user_message,
                            const std::string& api_base,
                            const std::string& api_key,
                            const std::string& model,
                            std::string& error_out)
{
    auto url = parse_url(api_base);
    if (url.host.empty()) {
        error_out = "LLM_API_BASE URL 无效";
        return "";
    }

    // 构造请求体
    nlohmann::json body;
    body["model"] = model;
    body["messages"] = nlohmann::json::array({
        {{"role", "system"}, {"content", system_prompt}},
        {{"role", "user"},   {"content", user_message}}
    });
    body["max_tokens"] = 2048;
    body["temperature"] = 0.3;

    std::string body_str = body.dump();

    // 智能拼接路径: 如果用户的 API_BASE 已经包含 /v1，就只追加 /chat/completions
    std::string path;
    if (url.path_prefix.size() >= 3 &&
        url.path_prefix.substr(url.path_prefix.size() - 3) == "/v1") {
        path = url.path_prefix + "/chat/completions";
    } else {
        path = url.path_prefix + "/v1/chat/completions";
    }

    httplib::Headers headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Authorization", "Bearer " + api_key);

    std::string response_text;

    // 根据协议选择 SSL 或普通 HTTP 客户端
#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) || defined(HTTPLIB_OPENSSL_SUPPORT)
    if (url.use_ssl) {
        httplib::SSLClient cli(url.host, url.port);
        cli.set_read_timeout(60, 0);
        cli.set_connection_timeout(10, 0);

        auto res = cli.Post(path, headers, body_str, "application/json");
        if (!res) {
            error_out = "LLM API 请求失败 (SSL): " + httplib::to_string(res.error());
            return "";
        }
        if (res->status != 200) {
            error_out = "LLM API 返回状态 " + std::to_string(res->status) + ": " + res->body;
            return "";
        }
        response_text = res->body;
    } else
#endif
    {
        httplib::Client cli(url.host, url.port);
        cli.set_read_timeout(60, 0);
        cli.set_connection_timeout(10, 0);

        auto res = cli.Post(path, headers, body_str, "application/json");
        if (!res) {
            error_out = "LLM API 请求失败";
            return "";
        }
        if (res->status != 200) {
            error_out = "LLM API 返回状态 " + std::to_string(res->status) + ": " + res->body;
            return "";
        }
        response_text = res->body;
    }

    // 解析 OpenAI 格式的响应
    try {
        auto j = nlohmann::json::parse(response_text);
        if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
            return j["choices"][0]["message"]["content"].get<std::string>();
        }
        error_out = "LLM 响应格式异常";
        return "";
    } catch (const std::exception& e) {
        error_out = std::string("LLM 响应解析失败: ") + e.what();
        return "";
    }
}

// ============================================================
// 公共接口: 完整 RAG 问答
// ============================================================
AiAnswer ask_question(Db& db, int repo_id, const std::string& question)
{
    AiAnswer answer;

    // 1. 获取仓库信息
    std::string full_name = get_repo_full_name(db, repo_id);
    if (full_name.empty()) {
        answer.error = "仓库不存在";
        return answer;
    }

    // 2. 从知识库检索相关证据（Top 10）
    auto evidence = search_knowledge(db, repo_id, question, 10);

    // 3. 组装 Prompt
    std::string system_prompt = build_system_prompt(full_name);
    std::string context = build_context(db, repo_id, evidence);
    std::string user_message = context + "## 问题\n" + question;

    // 4. 读取 LLM 配置（环境变量）
    std::string api_base = util::get_env("LLM_API_BASE", "https://api.openai.com");
    std::string api_key  = util::get_env("LLM_API_KEY",  "");
    std::string model    = util::get_env("LLM_MODEL",    "gpt-3.5-turbo");

    if (api_key.empty()) {
        answer.error = "未配置 LLM_API_KEY 环境变量，请在 config.env 中设置";
        return answer;
    }

    // 5. 调用 LLM
    std::string llm_error;
    std::string llm_response = call_llm(system_prompt, user_message,
                                        api_base, api_key, model, llm_error);
    if (!llm_error.empty()) {
        answer.error = llm_error;
        return answer;
    }

    // 6. 组装返回结果
    answer.answer  = llm_response;
    answer.model   = model;
    answer.success = true;

    for (const auto& e : evidence) {
        AiEvidence ev;
        ev.source_type = e.source_type;
        ev.source_id   = e.source_id;
        ev.title       = e.title;
        ev.snippet     = e.content.size() > 200 ? e.content.substr(0, 200) + "..." : e.content;
        answer.evidence.push_back(ev);
    }

    // 7. 保存对话记录
    std::string evidence_json = knowledge_chunks_to_json(evidence);
    save_conversation(db, repo_id, question, llm_response, evidence_json, model);

    return answer;
}

// ============================================================
// JSON 序列化
// ============================================================
std::string ai_answer_to_json(const AiAnswer& a)
{
    std::string out = "{";
    out += "\"success\":" + std::string(a.success ? "true" : "false");

    if (!a.error.empty())
        out += ",\"error\":\"" + util::json_escape(a.error) + "\"";

    if (!a.answer.empty())
        out += ",\"answer\":\"" + util::json_escape(a.answer) + "\"";

    if (!a.model.empty())
        out += ",\"model\":\"" + util::json_escape(a.model) + "\"";

    out += ",\"evidence\":[";
    for (size_t i = 0; i < a.evidence.size(); i++) {
        if (i > 0) out += ",";
        const auto& e = a.evidence[i];
        out += "{\"source_type\":\"" + util::json_escape(e.source_type) + "\"";
        out += ",\"source_id\":\"" + util::json_escape(e.source_id) + "\"";
        out += ",\"title\":\"" + util::json_escape(e.title) + "\"";
        out += ",\"snippet\":\"" + util::json_escape(e.snippet) + "\"";
        out += "}";
    }
    out += "]";

    out += "}";
    return out;
}
