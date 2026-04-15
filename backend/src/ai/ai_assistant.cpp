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
#include <cctype>
#include <sstream>
#include <ctime>
#include <iomanip>

// ============================================================
// 辅助: URL 解析（用于 LLM API 调用）
// ============================================================
struct UrlParts {
    std::string host;
    int port = 443;
    bool use_ssl = true;
    std::string path_prefix;  // 例如 "/v1" 之类的前缀路径
};

struct RepoSummary {
    int id = 0;
    std::string full_name;
    int chunk_count = 0;
};

static std::vector<RepoSummary> list_repo_summaries(Db& db)
{
    std::vector<RepoSummary> items;
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT r.id, r.full_name, COUNT(k.id) AS chunk_count "
        "FROM repos r "
        "LEFT JOIN knowledge_chunks k ON k.repo_id = r.id "
        "GROUP BY r.id, r.full_name "
        "ORDER BY r.id ASC;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return items;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RepoSummary r;
        r.id = sqlite3_column_int(stmt, 0);
        auto v1 = sqlite3_column_text(stmt, 1);
        r.full_name = v1 ? (const char*)v1 : "";
        r.chunk_count = sqlite3_column_int(stmt, 2);
        items.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return items;
}

static bool is_repo_inventory_question(const std::string& question)
{
    auto contains = [&](const std::string& needle) {
        return question.find(needle) != std::string::npos;
    };

    if (contains("哪些仓库") || contains("仓库列表") || contains("有几个仓库") ||
        contains("几个仓库") || contains("可用仓库") || contains("知识库里有")) {
        return true;
    }

    std::string lower = question;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return lower.find("which repos") != std::string::npos ||
           lower.find("which repositories") != std::string::npos ||
           lower.find("how many repos") != std::string::npos ||
           lower.find("how many repositories") != std::string::npos ||
           lower.find("repo list") != std::string::npos ||
           lower.find("repository list") != std::string::npos;
}

static std::string build_repo_inventory_answer(const std::vector<RepoSummary>& repos)
{
    int indexed_repo_count = 0;
    int total_chunks = 0;
    for (const auto& r : repos) {
        total_chunks += r.chunk_count;
        if (r.chunk_count > 0) indexed_repo_count++;
    }

    std::ostringstream oss;
    oss << "当前系统中共有 " << repos.size() << " 个仓库记录，已构建知识库的仓库有 "
        << indexed_repo_count << " 个（知识块总数 " << total_chunks << "）。\n\n";

    if (repos.empty()) {
        oss << "目前还没有任何仓库，请先调用 POST /api/repos 添加仓库。";
        return oss.str();
    }

    oss << "仓库列表如下：\n";
    for (const auto& r : repos) {
        oss << "- repo_id=" << r.id << " | " << r.full_name
            << " | knowledge_chunks=" << r.chunk_count;
        if (r.chunk_count <= 0) {
            oss << "（未构建知识库）";
        }
        oss << "\n";
    }

    oss << "\n你可以直接提问，也可以在请求里传 repo_id 以聚焦到单仓库。";
    return oss.str();
}

// 过滤非法 UTF-8 字节，避免 nlohmann::json 在 dump 阶段抛异常。
static std::string sanitize_utf8_lossy(const std::string& input)
{
    std::string out;
    out.reserve(input.size());

    auto is_cont = [](unsigned char c) { return (c & 0xC0) == 0x80; };

    for (size_t i = 0; i < input.size();) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        if (c <= 0x7F) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }

        // 2-byte sequence: C2..DF 80..BF
        if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 < input.size()) {
                unsigned char c1 = static_cast<unsigned char>(input[i + 1]);
                if (is_cont(c1)) {
                    out.append(input, i, 2);
                    i += 2;
                    continue;
                }
            }
            out.push_back('?');
            ++i;
            continue;
        }

        // 3-byte sequence with range checks.
        if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 < input.size()) {
                unsigned char c1 = static_cast<unsigned char>(input[i + 1]);
                unsigned char c2 = static_cast<unsigned char>(input[i + 2]);
                bool ok = is_cont(c1) && is_cont(c2);
                if (ok) {
                    if (c == 0xE0) ok = (c1 >= 0xA0);      // no overlong
                    if (c == 0xED) ok = (c1 <= 0x9F);      // no surrogates
                }
                if (ok) {
                    out.append(input, i, 3);
                    i += 3;
                    continue;
                }
            }
            out.push_back('?');
            ++i;
            continue;
        }

        // 4-byte sequence with range checks.
        if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 < input.size()) {
                unsigned char c1 = static_cast<unsigned char>(input[i + 1]);
                unsigned char c2 = static_cast<unsigned char>(input[i + 2]);
                unsigned char c3 = static_cast<unsigned char>(input[i + 3]);
                bool ok = is_cont(c1) && is_cont(c2) && is_cont(c3);
                if (ok) {
                    if (c == 0xF0) ok = (c1 >= 0x90);      // no overlong
                    if (c == 0xF4) ok = (c1 <= 0x8F);      // <= U+10FFFF
                }
                if (ok) {
                    out.append(input, i, 4);
                    i += 4;
                    continue;
                }
            }
            out.push_back('?');
            ++i;
            continue;
        }

        // 其他非法前导字节
        out.push_back('?');
        ++i;
    }

    return out;
}

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

static std::string get_current_date_ymd()
{
    const std::time_t now = std::time(nullptr);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d");
    return oss.str();
}

static std::string build_system_prompt(const std::string& full_name,
                                       bool global_scope,
                                       const std::string& current_date)
{
    if (global_scope) {
        return
            "你是一个软件工程 AI 助手，负责分析平台中多个仓库的开发数据。\n"
            "你可以回答关于项目历史、协作模式、代码质量、团队动态等方面的问题。\n"
            "当前系统日期为 " + current_date + "。\n"
            "回答要求:\n"
            "1. 基于提供的证据数据作答，引用具体证据（如 'Issue #123', 'PR #45', 'Commit abc1234'）。\n"
            "2. 如果证据不足，明确指出还需要补充哪些数据。\n"
            "3. 如果上下文片段中没有相关信息，请明确告诉用户'根据现有知识库无法回答这个问题'，不要编造答案。\n"
            "4. 回答要简洁、可操作。\n"
            "5. 使用与用户提问相同的语言回答。\n"
            "6. 如果证据来自不同仓库，请明确标注仓库来源。\n"
            "7. 时间判断必须以当前系统日期和提供证据为准，不要评价日期是'未来'或'过去'，也不要讨论现实时间线是否合理。\n";
    }

    return
        "你是一个软件工程 AI 助手，负责分析项目 '" + full_name + "' 的开发数据。\n"
        "你可以回答关于项目历史、协作模式、代码质量、团队动态等方面的问题。\n"
        "当前系统日期为 " + current_date + "。\n"
        "回答要求:\n"
        "1. 基于提供的证据数据作答，引用具体证据（如 'Issue #123', 'PR #45', 'Commit abc1234'）。\n"
        "2. 如果证据不足，明确指出还需要补充哪些数据。\n"
        "3. 如果上下文片段中没有相关信息，请明确告诉用户'根据现有知识库无法回答这个问题'，不要编造答案。\n"
        "4. 回答要简洁、可操作。\n"
        "5. 使用与用户提问相同的语言回答。\n"
        "6. 时间判断必须以当前系统日期和提供证据为准，不要评价日期是'未来'或'过去'，也不要讨论现实时间线是否合理。\n";
}

// ============================================================
// 辅助: 组装上下文（当前指标 + 检索到的证据）
// ============================================================
static std::string build_context(Db& db, int repo_id,
                                 const std::vector<KnowledgeChunk>& evidence)
{
    std::string ctx;

    if (repo_id > 0) {
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
    } else {
        ctx += "## 当前范围\n";
        ctx += "- 未指定 repo_id，将基于平台全局知识库回答。\n\n";

        auto repos = list_repo_summaries(db);
        int indexed_repo_count = 0;
        int total_chunks = 0;
        for (const auto& r : repos) {
            total_chunks += r.chunk_count;
            if (r.chunk_count > 0) indexed_repo_count++;
        }

        ctx += "## 全局仓库概览\n";
        ctx += "- 仓库总数: " + std::to_string(repos.size()) + "\n";
        ctx += "- 已构建知识库仓库数: " + std::to_string(indexed_repo_count) + "\n";
        ctx += "- 知识块总数: " + std::to_string(total_chunks) + "\n";
        for (const auto& r : repos) {
            ctx += "- repo_id=" + std::to_string(r.id) + ": " + r.full_name +
                   " (knowledge_chunks=" + std::to_string(r.chunk_count) + ")\n";
        }
        ctx += "\n";
    }

    // 检索到的相关证据
    if (!evidence.empty()) {
        ctx += "## 相关证据\n";
        for (size_t i = 0; i < evidence.size(); i++) {
            const auto& e = evidence[i];
            ctx += "[证据 " + std::to_string(i + 1) + "] ";
            ctx += "(repo_id=" + std::to_string(e.repo_id) + ", " + e.source_type + " #" + e.source_id + ") ";
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
    const std::string safe_system_prompt = sanitize_utf8_lossy(system_prompt);
    const std::string safe_user_message = sanitize_utf8_lossy(user_message);
    const std::string safe_model = sanitize_utf8_lossy(model);

    nlohmann::json body;
    body["model"] = safe_model;
    body["messages"] = nlohmann::json::array({
        {{"role", "system"}, {"content", safe_system_prompt}},
        {{"role", "user"},   {"content", safe_user_message}}
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

    const bool global_scope = repo_id <= 0;

    if (global_scope && is_repo_inventory_question(question)) {
        auto repos = list_repo_summaries(db);
        answer.answer = build_repo_inventory_answer(repos);
        answer.model = "local-db";
        answer.success = true;
        save_conversation(db, 0, question, answer.answer, "[]", answer.model);
        return answer;
    }

    std::string full_name;
    if (!global_scope) {
        full_name = get_repo_full_name(db, repo_id);
        if (full_name.empty()) {
            answer.error = "仓库不存在";
            return answer;
        }
    }

    // 1. 从知识库检索相关证据（Top 10）
    auto evidence = search_knowledge(db, repo_id, question, 10);

    // 2. 组装 Prompt
    const std::string current_date = get_current_date_ymd();
    std::string system_prompt = build_system_prompt(full_name, global_scope, current_date);
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

    // 3. 调用 LLM
    std::string llm_error;
    std::string llm_response = call_llm(system_prompt, user_message,
                                        api_base, api_key, model, llm_error);
    std::cerr << "Raw LLM Response: " << llm_response << std::endl;
    if (!llm_error.empty()) {
        answer.error = llm_error;
        return answer;
    }

    // 4. 组装返回结果
    answer.answer  = llm_response;
    answer.model   = model;
    answer.success = true;

    for (const auto& e : evidence) {
        AiEvidence ev;
        ev.repo_id     = e.repo_id;
        ev.source_type = e.source_type;
        ev.source_id   = e.source_id;
        ev.title       = e.title;
        ev.snippet     = e.content;
        answer.evidence.push_back(ev);
    }

    // 5. 保存对话记录
    std::string evidence_json = knowledge_chunks_to_json(evidence);
    save_conversation(db, repo_id, question, llm_response, evidence_json, model);

    return answer;
}

// ============================================================
// JSON 序列化
// ============================================================
std::string ai_answer_to_json(const AiAnswer& a)
{
    nlohmann::json j;
    j["success"] = a.success;

    if (!a.error.empty()) {
        j["error"] = a.error;
    }

    if (!a.answer.empty()) {
        j["answer"] = a.answer;
    }

    if (!a.model.empty()) {
        j["model"] = a.model;
    }

    j["evidence"] = nlohmann::json::array();
    for (const auto& e : a.evidence) {
        j["evidence"].push_back({
            {"repo_id", e.repo_id},
            {"source_type", e.source_type},
            {"source_id", e.source_id},
            {"title", e.title},
            {"snippet", e.snippet}
        });
    }

    // 统一输出 ASCII 转义，避免在某些终端/客户端因编码探测失败而出现乱码。
    return j.dump(-1, ' ', true, nlohmann::json::error_handler_t::replace);
}
