// 2.4(4) 自动周报生成 - 实现
#include "weekly_report.h"
#include "db/db.h"
#include "common/util.h"
#include "repo_metrics/metrics.h"
#include "repo_metrics/health.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

#include <httplib.h>

namespace {

// ============================================================
// 辅助: URL 解析（复用 ai_assistant.cpp 的模式）
// ============================================================
struct UrlParts {
    std::string host;
    int port = 443;
    bool use_ssl = true;
    std::string path_prefix;
};

static UrlParts parse_url(const std::string& url)
{
    UrlParts p;
    std::string rest = url;
    if (rest.rfind("https://", 0) == 0) {
        rest = rest.substr(8); p.use_ssl = true; p.port = 443;
    } else if (rest.rfind("http://", 0) == 0) {
        rest = rest.substr(7); p.use_ssl = false; p.port = 80;
    }
    auto slash = rest.find('/');
    if (slash != std::string::npos) {
        p.path_prefix = rest.substr(slash);
        rest = rest.substr(0, slash);
    }
    auto colon = rest.find(':');
    if (colon != std::string::npos) {
        p.host = rest.substr(0, colon);
        try { p.port = std::stoi(rest.substr(colon + 1)); } catch (...) {}
    } else {
        p.host = rest;
    }
    while (!p.path_prefix.empty() && p.path_prefix.back() == '/')
        p.path_prefix.pop_back();
    return p;
}

// ============================================================
// 辅助: 调用 LLM（与 ai_assistant.cpp 相同模式）
// ============================================================
static std::string call_llm(const std::string& system_prompt,
                            const std::string& user_message,
                            const std::string& api_base,
                            const std::string& api_key,
                            const std::string& model,
                            std::string& error_out)
{
    auto url = parse_url(api_base);
    if (url.host.empty()) { error_out = "LLM API URL invalid"; return ""; }

    nlohmann::json body;
    body["model"] = model;
    body["messages"] = nlohmann::json::array({
        {{"role", "system"}, {"content", system_prompt}},
        {{"role", "user"},   {"content", user_message}}
    });
    body["max_tokens"] = 4096;
    body["temperature"] = 0.3;
    std::string body_str = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

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

#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) || defined(HTTPLIB_OPENSSL_SUPPORT)
    if (url.use_ssl) {
        httplib::SSLClient cli(url.host, url.port);
        cli.set_read_timeout(120, 0);
        cli.set_connection_timeout(10, 0);
        auto res = cli.Post(path, headers, body_str, "application/json");
        if (!res) { error_out = "LLM request failed (SSL)"; return ""; }
        if (res->status != 200) { error_out = "LLM status " + std::to_string(res->status); return ""; }
        response_text = res->body;
    } else
#endif
    {
        httplib::Client cli(url.host, url.port);
        cli.set_read_timeout(120, 0);
        cli.set_connection_timeout(10, 0);
        auto res = cli.Post(path, headers, body_str, "application/json");
        if (!res) { error_out = "LLM request failed"; return ""; }
        if (res->status != 200) { error_out = "LLM status " + std::to_string(res->status); return ""; }
        response_text = res->body;
    }

    try {
        auto j = nlohmann::json::parse(response_text);
        if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
            return j["choices"][0]["message"]["content"].get<std::string>();
        }
        error_out = "LLM response format error";
        return "";
    } catch (const std::exception& e) {
        error_out = std::string("LLM parse error: ") + e.what();
        return "";
    }
}

// ============================================================
// 辅助: 获取当前日期和 7 天前日期
// ============================================================
static std::pair<std::string, std::string> get_week_range()
{
    std::time_t now = std::time(nullptr);
    std::time_t week_ago = now - 7 * 24 * 3600;

    std::tm tm_now{}, tm_ago{};
#ifdef _WIN32
    localtime_s(&tm_now, &now);
    localtime_s(&tm_ago, &week_ago);
#else
    localtime_r(&now, &tm_now);
    localtime_r(&week_ago, &tm_ago);
#endif

    std::ostringstream s_now, s_ago;
    s_now << std::put_time(&tm_now, "%Y-%m-%d");
    s_ago << std::put_time(&tm_ago, "%Y-%m-%d");
    return {s_ago.str(), s_now.str()};
}

// ============================================================
// 辅助: SQL 标量查询
// ============================================================
static int query_int(sqlite3* sdb, const std::string& sql, int repo_id,
                     const std::string& p1 = "", const std::string& p2 = "")
{
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return 0;
    int bind = 1;
    sqlite3_bind_int(stmt, bind++, repo_id);
    if (!p1.empty()) sqlite3_bind_text(stmt, bind++, p1.c_str(), -1, SQLITE_TRANSIENT);
    if (!p2.empty()) sqlite3_bind_text(stmt, bind++, p2.c_str(), -1, SQLITE_TRANSIENT);
    int val = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) val = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return val;
}

// ============================================================
// 辅助: 聚合一周数据，组装上下文
// ============================================================
static std::string build_report_context(Db& db, int repo_id,
                                        const std::string& week_start,
                                        const std::string& week_end,
                                        nlohmann::json& metrics_snapshot)
{
    sqlite3* sdb = db.handle();
    std::ostringstream ctx;

    // 基础指标
    RepoMetrics m = compute_repo_metrics(db, repo_id);
    HealthScore h = compute_health_from_metrics(m);

    metrics_snapshot["health_score"] = static_cast<int>(h.score);
    metrics_snapshot["activity"] = static_cast<int>(h.activity);
    metrics_snapshot["responsiveness"] = static_cast<int>(h.responsiveness);
    metrics_snapshot["quality"] = static_cast<int>(h.quality);
    metrics_snapshot["release"] = static_cast<int>(h.release);

    ctx << "## 当前健康度\n"
        << "- 总分: " << static_cast<int>(h.score) << "/100\n"
        << "- 活跃度: " << static_cast<int>(h.activity)
        << ", 响应: " << static_cast<int>(h.responsiveness)
        << ", 协作: " << static_cast<int>(h.quality)
        << ", 发布: " << static_cast<int>(h.release) << "\n\n";

    // 本周 commit 数
    int commits = query_int(sdb,
        "SELECT COUNT(*) FROM commits WHERE repo_id=?1 AND committed_at >= ?2 AND committed_at <= ?3;",
        repo_id, week_start, week_end + "T23:59:59");
    metrics_snapshot["commits_this_week"] = commits;

    // 本周新开 issue 数
    int new_issues = query_int(sdb,
        "SELECT COUNT(*) FROM issues WHERE repo_id=?1 AND created_at >= ?2 AND created_at <= ?3;",
        repo_id, week_start, week_end + "T23:59:59");
    metrics_snapshot["new_issues"] = new_issues;

    // 本周关闭 issue 数
    int closed_issues = query_int(sdb,
        "SELECT COUNT(*) FROM issues WHERE repo_id=?1 AND closed_at >= ?2 AND closed_at <= ?3;",
        repo_id, week_start, week_end + "T23:59:59");
    metrics_snapshot["closed_issues"] = closed_issues;

    // 本周新开 PR 数
    int new_prs = query_int(sdb,
        "SELECT COUNT(*) FROM pull_requests WHERE repo_id=?1 AND created_at >= ?2 AND created_at <= ?3;",
        repo_id, week_start, week_end + "T23:59:59");
    metrics_snapshot["new_prs"] = new_prs;

    // 本周合并 PR 数
    int merged_prs = query_int(sdb,
        "SELECT COUNT(*) FROM pull_requests WHERE repo_id=?1 AND merged_at >= ?2 AND merged_at <= ?3;",
        repo_id, week_start, week_end + "T23:59:59");
    metrics_snapshot["merged_prs"] = merged_prs;

    // 本周 release 数
    int releases = query_int(sdb,
        "SELECT COUNT(*) FROM releases WHERE repo_id=?1 AND published_at >= ?2 AND published_at <= ?3;",
        repo_id, week_start, week_end + "T23:59:59");
    metrics_snapshot["releases"] = releases;

    ctx << "## 本周活动 (" << week_start << " ~ " << week_end << ")\n"
        << "- 新增提交: " << commits << "\n"
        << "- 新开 Issue: " << new_issues << ", 关闭 Issue: " << closed_issues << "\n"
        << "- 新开 PR: " << new_prs << ", 合并 PR: " << merged_prs << "\n"
        << "- 新 Release: " << releases << "\n\n";

    // Top 贡献者
    ctx << "## 本周 Top 贡献者\n";
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT author_login, COUNT(*) AS cnt FROM commits "
            "WHERE repo_id=?1 AND committed_at >= ?2 AND committed_at <= ?3 "
            "AND author_login IS NOT NULL AND TRIM(author_login)<>'' "
            "GROUP BY author_login ORDER BY cnt DESC LIMIT 10;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, repo_id);
            sqlite3_bind_text(stmt, 2, week_start.c_str(), -1, SQLITE_TRANSIENT);
            std::string end_ts = week_end + "T23:59:59";
            sqlite3_bind_text(stmt, 3, end_ts.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                auto login = sqlite3_column_text(stmt, 0);
                int cnt = sqlite3_column_int(stmt, 1);
                ctx << "- " << (login ? (const char*)login : "?") << ": " << cnt << " commits\n";
            }
            sqlite3_finalize(stmt);
        }
    }
    ctx << "\n";

    // 风险告警摘要
    int alerts = query_int(sdb,
        "SELECT COUNT(*) FROM risk_alert_events WHERE repo_id=?1 AND created_at >= ?2;",
        repo_id, week_start);
    metrics_snapshot["risk_alerts"] = alerts;
    if (alerts > 0) {
        ctx << "## 本周风险告警 (" << alerts << " 条)\n";
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT alert_type, severity, metric_name, current_value, baseline_value "
            "FROM risk_alert_events WHERE repo_id=?1 AND created_at >= ?2 "
            "ORDER BY created_at DESC LIMIT 5;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, repo_id);
            sqlite3_bind_text(stmt, 2, week_start.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                auto type_raw = sqlite3_column_text(stmt, 0);
                auto sev_raw = sqlite3_column_text(stmt, 1);
                auto metric_raw = sqlite3_column_text(stmt, 2);
                double current = sqlite3_column_double(stmt, 3);
                double baseline = sqlite3_column_double(stmt, 4);
                ctx << "- [" << (sev_raw ? (const char*)sev_raw : "?") << "] "
                    << (type_raw ? (const char*)type_raw : "?") << ": "
                    << (metric_raw ? (const char*)metric_raw : "?")
                    << " (current=" << current << ", baseline=" << baseline << ")\n";
            }
            sqlite3_finalize(stmt);
        }
        ctx << "\n";
    }

    return ctx.str();
}

// ============================================================
// 辅助: 保存周报到 DB
// ============================================================
static int save_report(Db& db, int repo_id, const std::string& week_start,
                       const std::string& week_end, const std::string& report_text,
                       const std::string& metrics_json, const std::string& model)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO weekly_reports(repo_id, week_start, week_end, report_text, metrics_json, model) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, week_start.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, week_end.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, report_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, metrics_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, model.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    int id = (rc == SQLITE_DONE) ? static_cast<int>(sqlite3_last_insert_rowid(sdb)) : 0;
    sqlite3_finalize(stmt);
    return id;
}

// ============================================================
// 辅助: 将周报写入 knowledge_chunks 供 RAG 引用
// ============================================================
static void save_report_to_knowledge(Db& db, int repo_id,
                                     const std::string& week_start,
                                     const std::string& week_end,
                                     const std::string& report_text)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    std::string title = "Weekly Report " + week_start + " ~ " + week_end;
    std::string source_id = "week_" + week_start;
    // 截取前 2000 字符作为知识块内容
    std::string content = report_text;
    if (content.size() > 2000) content = content.substr(0, 2000) + "...";

    const char* sql =
        "INSERT INTO knowledge_chunks(repo_id, source_type, source_id, title, content, author, event_time) "
        "VALUES (?1, 'weekly_report', ?2, ?3, ?4, 'system', ?5);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        sqlite3_bind_text(stmt, 2, source_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, week_end.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// ============================================================
// 辅助: 获取仓库名
// ============================================================
static std::string get_repo_name(Db& db, int repo_id)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    std::string name;
    const char* sql = "SELECT full_name FROM repos WHERE id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            auto v = sqlite3_column_text(stmt, 0);
            if (v) name = (const char*)v;
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return name;
}

} // anonymous namespace

// ============================================================
// 公共接口: 生成周报
// ============================================================
WeeklyReport generate_weekly_report(Db& db, int repo_id)
{
    WeeklyReport report;
    report.repo_id = repo_id;

    auto [week_start, week_end] = get_week_range();
    report.week_start = week_start;
    report.week_end = week_end;

    std::string repo_name = get_repo_name(db, repo_id);

    // 聚合数据
    nlohmann::json metrics_snapshot;
    std::string context = build_report_context(db, repo_id, week_start, week_end, metrics_snapshot);
    report.metrics_json = metrics_snapshot.dump();

    // 构建 Prompt
    std::string system_prompt =
        "你是一个软件工程项目周报生成器。根据提供的项目数据生成结构化的 Markdown 周报。\n"
        "周报模板：\n"
        "# 项目周报\n"
        "## 本期概览\n"
        "  - 总结本周关键数字（commit/PR/Issue 数量、主要贡献者）\n"
        "## 协作与交付\n"
        "  - PR 合并情况、Issue 处理效率\n"
        "## 质量与稳定性\n"
        "  - 健康度评分、风险告警情况\n"
        "## 风险与建议\n"
        "  - 本周最大风险点、下周优先任务建议\n\n"
        "要求：\n"
        "1. 基于提供的数据生成，不要编造数据\n"
        "2. 语言简洁、可操作\n"
        "3. 如果某项数据为零或缺失，客观描述现状\n"
        "4. 使用中文撰写\n";

    std::string user_message =
        "请为项目 '" + repo_name + "' 生成本周(" + week_start + " ~ " + week_end + ")的项目周报。\n\n"
        + context;

    // 调用 LLM
    std::string api_base = util::get_env("LLM_API_BASE", "https://api.openai.com");
    std::string api_key = util::get_env("LLM_API_KEY", "");
    std::string model = util::get_env("LLM_MODEL", "gpt-3.5-turbo");
    report.model = model;

    if (api_key.empty()) {
        report.report_text = "Error: LLM API Key not configured";
        return report;
    }

    std::string error;
    std::string llm_output = call_llm(system_prompt, user_message, api_base, api_key, model, error);
    if (!error.empty()) {
        report.report_text = "Error generating report: " + error;
        std::cerr << "[report] LLM error: " << error << "\n";
        return report;
    }

    report.report_text = llm_output;

    // 保存到 DB
    int id = save_report(db, repo_id, week_start, week_end, llm_output, report.metrics_json, model);
    report.id = id;

    // 写入知识库
    save_report_to_knowledge(db, repo_id, week_start, week_end, llm_output);

    std::cerr << "[report] repo_id=" << repo_id << " weekly report generated (id=" << id << ")\n";
    return report;
}

// ============================================================
// 公共接口: 获取最新周报
// ============================================================
WeeklyReport get_latest_report(Db& db, int repo_id)
{
    WeeklyReport r;
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, repo_id, week_start, week_end, report_text, metrics_json, model, created_at "
        "FROM weekly_reports WHERE repo_id=?1 ORDER BY id DESC LIMIT 1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return r;
    sqlite3_bind_int(stmt, 1, repo_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        r.id = sqlite3_column_int(stmt, 0);
        r.repo_id = sqlite3_column_int(stmt, 1);
        auto v2 = sqlite3_column_text(stmt, 2); r.week_start = v2 ? (const char*)v2 : "";
        auto v3 = sqlite3_column_text(stmt, 3); r.week_end = v3 ? (const char*)v3 : "";
        auto v4 = sqlite3_column_text(stmt, 4); r.report_text = v4 ? (const char*)v4 : "";
        auto v5 = sqlite3_column_text(stmt, 5); r.metrics_json = v5 ? (const char*)v5 : "";
        auto v6 = sqlite3_column_text(stmt, 6); r.model = v6 ? (const char*)v6 : "";
        auto v7 = sqlite3_column_text(stmt, 7); r.created_at = v7 ? (const char*)v7 : "";
    }
    sqlite3_finalize(stmt);
    return r;
}

// ============================================================
// 公共接口: 查询周报历史
// ============================================================
std::vector<WeeklyReport> list_weekly_reports(Db& db, int repo_id, int limit)
{
    std::vector<WeeklyReport> results;
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, repo_id, week_start, week_end, report_text, metrics_json, model, created_at "
        "FROM weekly_reports WHERE repo_id=?1 ORDER BY id DESC LIMIT ?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;
    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_int(stmt, 2, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        WeeklyReport r;
        r.id = sqlite3_column_int(stmt, 0);
        r.repo_id = sqlite3_column_int(stmt, 1);
        auto v2 = sqlite3_column_text(stmt, 2); r.week_start = v2 ? (const char*)v2 : "";
        auto v3 = sqlite3_column_text(stmt, 3); r.week_end = v3 ? (const char*)v3 : "";
        auto v4 = sqlite3_column_text(stmt, 4); r.report_text = v4 ? (const char*)v4 : "";
        auto v5 = sqlite3_column_text(stmt, 5); r.metrics_json = v5 ? (const char*)v5 : "";
        auto v6 = sqlite3_column_text(stmt, 6); r.model = v6 ? (const char*)v6 : "";
        auto v7 = sqlite3_column_text(stmt, 7); r.created_at = v7 ? (const char*)v7 : "";
        results.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return results;
}

// ============================================================
// JSON 序列化
// ============================================================
std::string report_to_json(const WeeklyReport& r)
{
    nlohmann::json j;
    j["id"] = r.id;
    j["repo_id"] = r.repo_id;
    j["week_start"] = r.week_start;
    j["week_end"] = r.week_end;
    j["report_text"] = r.report_text;
    j["model"] = r.model;
    j["created_at"] = r.created_at;
    // metrics_json 作为嵌套对象
    if (!r.metrics_json.empty()) {
        try {
            j["metrics"] = nlohmann::json::parse(r.metrics_json);
        } catch (...) {
            j["metrics"] = nullptr;
        }
    }
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string reports_to_json(const std::vector<WeeklyReport>& reports)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : reports) {
        nlohmann::json j;
        j["id"] = r.id;
        j["repo_id"] = r.repo_id;
        j["week_start"] = r.week_start;
        j["week_end"] = r.week_end;
        j["model"] = r.model;
        j["created_at"] = r.created_at;
        // 列表中省略 report_text 正文，只返回摘要信息
        j["report_preview"] = r.report_text.size() > 200
            ? r.report_text.substr(0, 200) + "..."
            : r.report_text;
        arr.push_back(j);
    }
    return arr.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}
