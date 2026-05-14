#include "routes.h"
#include "common/util.h"
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <algorithm>

static constexpr const char* kJson = "application/json; charset=utf-8";

static void put_repo_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    auto enabled_s = req.get_param_value("enabled");
    if (enabled_s != "0" && enabled_s != "1")
    {
        res.status = 400;
        res.set_content(R"({"error":"enabled must be 0 or 1"})", kJson);
        return;
    }
    int enabled = (enabled_s == "1") ? 1 : 0;

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE repos SET enabled=?1 WHERE id=?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }
    sqlite3_bind_int(stmt, 1, enabled);
    sqlite3_bind_int(stmt, 2, rid);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        res.status = 500;
        res.set_content(R"({"error":"db step failed"})", kJson);
        return;
    }

    if (sqlite3_changes(sdb) == 0)
    {
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", kJson);
        return;
    }

    res.set_content(std::string("{\"ok\":true,\"id\":") + std::to_string(rid) + ",\"enabled\":" + std::to_string(enabled) + "}", kJson);
}

// ---- 2.2 质量分析子系统 ----

// PUT /api/quality/issues/(\d+) — 更新单个质量问题状态
static void put_quality_issue_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int issue_id = std::stoi(req.matches[1]);

    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); }
    catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid json"})", kJson);
        return;
    }

    const std::string new_status = body.value("status", "");
    if (new_status != "active" && new_status != "fixed" && new_status != "ignored" && new_status != "false_positive") {
        res.status = 400;
        res.set_content(R"({"error":"status must be active|fixed|ignored|false_positive"})", kJson);
        return;
    }

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    // 先验证 issue 存在
    const char* check_sql = "SELECT id FROM quality_issues WHERE id=?1;";
    if (sqlite3_prepare_v2(sdb, check_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }
    sqlite3_bind_int(stmt, 1, issue_id);
    const int found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    stmt = nullptr;

    if (!found) {
        res.status = 404;
        res.set_content(R"({"error":"quality issue not found"})", kJson);
        return;
    }

    std::string update_sql;
    if (new_status == "fixed") {
        update_sql = "UPDATE quality_issues SET status=?1, fixed_at=datetime('now'), last_seen_at=datetime('now') WHERE id=?2;";
    } else if (new_status == "active") {
        update_sql = "UPDATE quality_issues SET status=?1, fixed_at=NULL, last_seen_at=datetime('now') WHERE id=?2;";
    } else {
        update_sql = "UPDATE quality_issues SET status=?1, last_seen_at=datetime('now') WHERE id=?2;";
    }

    if (sqlite3_prepare_v2(sdb, update_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }
    sqlite3_bind_text(stmt, 1, new_status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, issue_id);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        res.status = 500;
        res.set_content(R"({"error":"db step failed"})", kJson);
        return;
    }

    nlohmann::json out;
    out["ok"] = true;
    out["id"] = issue_id;
    out["status"] = new_status;
    res.set_content(out.dump(), kJson);
}

void register_put_routes(httplib::Server& app, Db& db)
{
    app.Put(R"(/api/repos/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                put_repo_handler(db, req, res);
            });

    // 2.2 质量分析：更新单个 issue 状态
    app.Put(R"(/api/quality/issues/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { put_quality_issue_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });
}