#include "routes.h"
#include "common/util.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace {
static constexpr const char* kJson = "application/json; charset=utf-8";

static int get_int_param(const httplib::Request& req, const std::string& key, int defv)
{
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
}

static std::string get_str_param(const httplib::Request& req, const std::string& key)
{
    if (!req.has_param(key)) return "";
    return req.get_param_value(key);
}

static void get_repo_tasks_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    //std::cout<<"sure it is the get_repo_tasks_handler"<<std::endl;

    const int repo_id = std::stoi(req.matches[1]);
    const int limit = std::max(1, std::min(500, get_int_param(req, "limit", 50)));
    const int offset = std::max(0, get_int_param(req, "offset", 0));
    const std::string status = get_str_param(req, "status"); // open/done/空=全部

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    std::string sql =
        "SELECT id, repo_id, title, priority, status, reason, actions_json, expected_benefit, verify, "
        "source, ai_conversation_id, created_at, done_at, updated_at "
        "FROM tasks WHERE repo_id=?1 ";
    if (!status.empty()) sql += "AND status=?2 ";
    sql += "ORDER BY (status='open') DESC, CASE priority WHEN 'P0' THEN 0 WHEN 'P1' THEN 1 ELSE 2 END, created_at DESC "
           "LIMIT ?3 OFFSET ?4;";

    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        const char* msg = sqlite3_errmsg(sdb);
        res.set_content(std::string("{\"error\":\"db prepare failed\",\"detail\":\"") +
                            util::json_escape(msg ? msg : "") + "\"}",
                        kJson);
        return;
    }

    int bind = 1;
    sqlite3_bind_int(stmt, bind++, repo_id);
    if (!status.empty()) sqlite3_bind_text(stmt, bind++, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, bind++, limit);
    sqlite3_bind_int(stmt, bind++, offset);

    nlohmann::json items = nlohmann::json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json t;
        t["id"] = sqlite3_column_int(stmt, 0);
        t["repo_id"] = sqlite3_column_int(stmt, 1);
        auto col_text = [&](int i)->std::string{
            const unsigned char* p = sqlite3_column_text(stmt, i);
            return p ? (const char*)p : "";
        };
        t["title"] = col_text(2);
        t["priority"] = col_text(3);
        t["status"] = col_text(4);
        t["reason"] = col_text(5);
        t["actions_json"] = col_text(6);
        t["expected_benefit"] = col_text(7);
        t["verify"] = col_text(8);
        t["source"] = col_text(9);
        t["ai_conversation_id"] = sqlite3_column_type(stmt, 10) == SQLITE_NULL ? nullptr : nlohmann::json(sqlite3_column_int(stmt, 10));
        t["created_at"] = col_text(11);
        t["done_at"] = col_text(12);
        t["updated_at"] = col_text(13);
        items.push_back(std::move(t));
    }
    sqlite3_finalize(stmt);

    nlohmann::json out;
    out["items"] = items;
    res.set_content(out.dump(), kJson);
}

static bool upsert_one_task(sqlite3* sdb, int repo_id, const nlohmann::json& t, std::string& err, int& changed)
{
    const std::string title = t.value("title", "");
    if (title.empty()) { err = "title required"; return false; }

    const std::string priority = t.value("priority", "P1");
    const std::string reason = t.value("reason", "");
    const std::string expected_benefit = t.value("expected_benefit", "");
    const std::string verify = t.value("verify", "");
    const std::string source = t.value("source", "ai");
    const int ai_conversation_id = t.value("ai_conversation_id", 0);

    std::string actions_json = "[]";
    if (t.contains("actions")) actions_json = t["actions"].dump();
    else if (t.contains("actions_json")) actions_json = t.value("actions_json", "[]");

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO tasks(repo_id,title,priority,status,reason,actions_json,expected_benefit,verify,source,ai_conversation_id) "
        "VALUES(?1,?2,?3,'open',?4,?5,?6,?7,?8,?9) "
        "ON CONFLICT(repo_id,title) DO UPDATE SET "
        "priority=excluded.priority, "
        "reason=excluded.reason, "
        "actions_json=excluded.actions_json, "
        "expected_benefit=excluded.expected_benefit, "
        "verify=excluded.verify, "
        "source=excluded.source, "
        "ai_conversation_id=excluded.ai_conversation_id, "
        "updated_at=datetime('now') "
        "WHERE tasks.status!='done';";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) { err = "db prepare failed"; return false; }

    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, priority.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, reason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, actions_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, expected_benefit.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, verify.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, source.c_str(), -1, SQLITE_TRANSIENT);
    if (ai_conversation_id > 0) sqlite3_bind_int(stmt, 9, ai_conversation_id);
    else sqlite3_bind_null(stmt, 9);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) { err = "db step failed"; return false; }

    changed += sqlite3_changes(sdb);
    return true;
}

static void post_repo_tasks_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
   /* const auto ct = req.get_header_value("Content-Type");
    const auto cl = req.get_header_value("Content-Length");
    std::cerr << "post_repo_tasks_handler: Content-Type=" << ct
              << " Content-Length=" << cl
              << " body_size=" << req.body.size()
              << " body_head=" << req.body.substr(0, 200) << std::endl;*/
    const int repo_id = std::stoi(req.matches[1]);

    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid json"})", kJson);
        return;
    }

    nlohmann::json items = nlohmann::json::array();
    if (body.is_array()) items = body;
    else if (body.contains("items") && body["items"].is_array()) items = body["items"];
    else items = nlohmann::json::array({ body });

    sqlite3* sdb = db.handle();
    int changed = 0;
    std::string err;

    for (size_t i = 0; i < items.size(); i++) {
        const auto& t = items[i];
       // std::cerr<<"index: "<< i<<std::endl;

        if (!upsert_one_task(sdb, repo_id, t, err, changed)) {
            res.status = 400;
            nlohmann::json out;
            out["error"] = err;
            out["index"] = (int)i;
            out["bad_item"] = t;
            res.set_content(out.dump(), kJson);

            std::cerr<<"(error)index: "<< i <<std::endl;
            std::cerr<<err<<std::endl;
            return;
        }
        
    }

    nlohmann::json out;
    out["ok"] = true;
    out["changed"] = changed;
    out["count"] = (int)items.size();
    res.set_content(out.dump(), kJson);
}

static void patch_task_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int task_id = std::stoi(req.matches[1]);

    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body); }
    catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid json"})", kJson);
        return;
    }

    const std::string status = body.value("status", "");
    if (status != "open" && status != "done") {
        res.status = 400;
        res.set_content(R"({"error":"status must be open/done"})", kJson);
        return;
    }

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql_done =
        "UPDATE tasks SET status='done', done_at=datetime('now'), updated_at=datetime('now') WHERE id=?1;";
    const char* sql_open =
        "UPDATE tasks SET status='open', done_at=NULL, updated_at=datetime('now') WHERE id=?1;";

    const char* sql = (status == "done") ? sql_done : sql_open;

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db error"})", kJson);
        return;
    }
    sqlite3_bind_int(stmt, 1, task_id);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        res.status = 500;
        res.set_content(R"({"error":"db error"})", kJson);
        return;
    }

    nlohmann::json out;
    out["ok"] = true;
    out["changed"] = sqlite3_changes(sdb);
    res.set_content(out.dump(), kJson);
}

static void delete_task_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int task_id = std::stoi(req.matches[1]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM tasks WHERE id=?1;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db error"})", kJson);
        return;
    }
    sqlite3_bind_int(stmt, 1, task_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    nlohmann::json out;
    out["ok"] = true;
    out["changed"] = sqlite3_changes(sdb);
    res.set_content(out.dump(), kJson);
}

} // namespace

void register_tasks_routes(httplib::Server& app, Db& db)
{
   // std::cout << "[routes] register_tasks_routes OK" << std::endl;

    app.Get(R"(/api/repos/(\d+)/tasks)", [&](const httplib::Request& req, httplib::Response& res) {
        get_repo_tasks_handler(db, req, res);
    });

    app.Post(R"(/api/repos/(\d+)/tasks)", [&](const httplib::Request& req, httplib::Response& res) {
        post_repo_tasks_handler(db, req, res);
    });

    app.Patch(R"(/api/tasks/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        patch_task_handler(db, req, res);
    });

    app.Delete(R"(/api/tasks/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        delete_task_handler(db, req, res);
    });
}