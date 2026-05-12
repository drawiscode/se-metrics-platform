#include "routes.h"
#include "common/util.h"
#include "quality/static_analysis.h"
#include "quality/quality_score.h"

#include <sqlite3.h>
#include <string>

static constexpr const char* kJson = "application/json; charset=utf-8";

static int get_int_param_quality(const httplib::Request& req, const std::string& key, int defv) {
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
}

static std::string get_str_param_quality(const httplib::Request& req, const std::string& key, const std::string& defv) {
    if (!req.has_param(key)) return defv;
    return req.get_param_value(key);
}

static bool db_get_repo_full_name(Db& db, int repo_id, std::string& full_name_out)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT full_name FROM repos WHERE id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, repo_id);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) { sqlite3_finalize(stmt); return false; }
    const unsigned char* txt = sqlite3_column_text(stmt, 0);
    full_name_out = txt ? (const char*)txt : "";
    sqlite3_finalize(stmt);
    return !full_name_out.empty();
}

static void post_quality_analyze_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    std::string full_name;
    if (!db_get_repo_full_name(db, rid, full_name)) {
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", kJson);
        return;
    }

    std::string tool = get_str_param_quality(req, "tool", "cppcheck");
    std::string ref = get_str_param_quality(req, "ref", "main");
    std::string mode = get_str_param_quality(req, "mode", "full");
    int max_files = std::max(0, get_int_param_quality(req, "max_files", 2000));

    auto result = run_static_analysis(db, rid, full_name, ref, tool, mode, max_files);
    res.set_content(quality_result_to_json(result), kJson);
}

static void get_quality_issues_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    std::string tool = get_str_param_quality(req, "tool", "");
    std::string severity = get_str_param_quality(req, "severity", "");
    int limit = std::max(1, std::min(500, get_int_param_quality(req, "limit", 100)));
    int offset = std::max(0, get_int_param_quality(req, "offset", 0));

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    std::string sql =
        "SELECT id, tool, file_path, line, column, rule_id, severity, message, first_seen_at "
        "FROM quality_issues WHERE repo_id=?1 ";

    if (!tool.empty()) sql += "AND tool=?2 ";
    if (!severity.empty()) sql += (tool.empty() ? "AND severity=?2 " : "AND severity=?3 ");
    sql += "ORDER BY id DESC LIMIT ?4 OFFSET ?5;";

    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }

    int bi = 1;
    sqlite3_bind_int(stmt, bi++, rid);
    if (!tool.empty()) sqlite3_bind_text(stmt, bi++, tool.c_str(), -1, SQLITE_TRANSIENT);
    if (!severity.empty()) sqlite3_bind_text(stmt, bi++, severity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, bi++, limit);
    sqlite3_bind_int(stmt, bi++, offset);

    std::string out = R"({"items":[)";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) out += ",";
        first = false;
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char* t = sqlite3_column_text(stmt, 1);
        const unsigned char* f = sqlite3_column_text(stmt, 2);
        int line = sqlite3_column_int(stmt, 3);
        int col = sqlite3_column_int(stmt, 4);
        const unsigned char* rule = sqlite3_column_text(stmt, 5);
        const unsigned char* sev = sqlite3_column_text(stmt, 6);
        const unsigned char* msg = sqlite3_column_text(stmt, 7);
        const unsigned char* first_seen = sqlite3_column_text(stmt, 8);

        out += "{\"id\":" + std::to_string(id)
            + ",\"tool\":\"" + util::json_escape(t ? (const char*)t : "") + "\""
            + ",\"file_path\":\"" + util::json_escape(f ? (const char*)f : "") + "\""
            + ",\"line\":" + std::to_string(line)
            + ",\"column\":" + std::to_string(col)
            + ",\"rule_id\":\"" + util::json_escape(rule ? (const char*)rule : "") + "\""
            + ",\"severity\":\"" + util::json_escape(sev ? (const char*)sev : "") + "\""
            + ",\"message\":\"" + util::json_escape(msg ? (const char*)msg : "") + "\""
            + ",\"first_seen_at\":\"" + util::json_escape(first_seen ? (const char*)first_seen : "") + "\"";
        out += "}";
    }
    out += "]}";

    sqlite3_finalize(stmt);
    res.set_content(out, kJson);
}

static void get_quality_summary_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    std::string tool = get_str_param_quality(req, "tool", "");

    QualityScore q = compute_quality_score(db, rid, tool);
    std::string out = "{\"quality\":" + quality_score_to_json(q)
        + ",\"tool\":\"" + util::json_escape(tool) + "\"}";
    res.set_content(out, kJson);
}

void register_quality_routes(httplib::Server& app, Db& db)
{
    app.Post(R"(/api/repos/(\d+)/quality/analyze)",
             [&db](const httplib::Request& req, httplib::Response& res) {
                 try { post_quality_analyze_handler(db, req, res); }
                 catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                 }
             });

    app.Get(R"(/api/repos/(\d+)/quality/issues)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_quality_issues_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Get(R"(/api/repos/(\d+)/quality/summary)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_quality_summary_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });
}
