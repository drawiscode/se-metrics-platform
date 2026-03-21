#include "routes.h"
#include "common/util.h"
#include <sqlite3.h>


static int get_int_param(const httplib::Request& req, const std::string& key, int defv) {
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
}


static void health_handler(const httplib::Request&, httplib::Response& res)
{
    res.set_content(R"({"ok":true})", "application/json");
}


static void not_implemented(httplib::Response& res, const char* what)
{
    res.status = 501;
    res.set_content(std::string("{\"error\":\"") + what + " not implemented\"}", "application/json");
}


static void get_repos_handler(Db& db, const httplib::Request&, httplib::Response& res)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, full_name, enabled FROM repos ORDER BY id DESC LIMIT 200;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }

    std::string out = R"({"items":[)";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (!first) out += ",";
        first = false;
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char* fn = sqlite3_column_text(stmt, 1);
        int enabled = sqlite3_column_int(stmt, 2);

        out += "{\"id\":" + std::to_string(id)
            + ",\"full_name\":\"" + util::json_escape(fn ? (const char*)fn : "") + "\""
            + ",\"enabled\":" + std::to_string(enabled)
            + "}";
    }
    out += "]}";

    sqlite3_finalize(stmt);
    res.set_content(out, "application/json");
}


static void get_repo_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, full_name, enabled FROM repos WHERE id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_int(stmt, 1, rid);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", "application/json");
        return;
    }

    int id = sqlite3_column_int(stmt, 0);
    const unsigned char* fn = sqlite3_column_text(stmt, 1);
    int enabled = sqlite3_column_int(stmt, 2);
    sqlite3_finalize(stmt);

    std::string out = std::string("{\"id\":") + std::to_string(id)
        + ",\"full_name\":\"" + util::json_escape(fn ? (const char*)fn : "") + "\""
        + ",\"enabled\":" + std::to_string(enabled)
        + "}";
    res.set_content(out, "application/json");
}


static void get_repo_snapshots_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, ts, stars, forks, open_issues, watchers, pushed_at "
        "FROM repo_snapshots WHERE repo_id=?1 ORDER BY id DESC LIMIT 100;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_int(stmt, 1, rid);

    std::string out = R"({"items":[)";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (!first) out += ",";
        first = false;

        const int id = sqlite3_column_int(stmt, 0);
        const unsigned char* ts = sqlite3_column_text(stmt, 1);
        const int stars = sqlite3_column_int(stmt, 2);
        const int forks = sqlite3_column_int(stmt, 3);
        const int open_issues = sqlite3_column_int(stmt, 4);
        const int watchers = sqlite3_column_int(stmt, 5);
        const unsigned char* pushed = sqlite3_column_text(stmt, 6);

        out += "{\"id\":" + std::to_string(id)
            + ",\"ts\":\"" + util::json_escape(ts ? (const char*)ts : "") + "\""
            + ",\"stars\":" + std::to_string(stars)
            + ",\"forks\":" + std::to_string(forks)
            + ",\"open_issues\":" + std::to_string(open_issues)
            + ",\"watchers\":" + std::to_string(watchers)
            + ",\"pushed_at\":\"" + util::json_escape(pushed ? (const char*)pushed : "") + "\""
            + "}";
    }
    out += "]}";
    sqlite3_finalize(stmt);

    res.set_content(out, "application/json");
}


static void get_repo_issues_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);

    const int limit = std::max(1, std::min(200, get_int_param(req, "limit", 100)));
    const int offset = std::max(0, get_int_param(req, "offset", 0));
    const std::string state = req.has_param("state") ? req.get_param_value("state") : ""; // open/closed/""(all)

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    std::string sql =
        "SELECT number, state, title, created_at, updated_at, closed_at, comments, author_login, assignee_login, is_pull_request "
        "FROM issues WHERE repo_id=?1 ";
    if (!state.empty()) sql += "AND state=?2 ";
    sql += "ORDER BY number DESC LIMIT ?X OFFSET ?Y;";

    // 为了简单，用两套 SQL 避免动态 bind 索引混乱
    const char* sql_all =
        "SELECT number, state, title, created_at, updated_at, closed_at, comments, author_login, assignee_login, is_pull_request "
        "FROM issues WHERE repo_id=?1 "
        "ORDER BY number DESC LIMIT ?2 OFFSET ?3;";

    const char* sql_state =
        "SELECT number, state, title, created_at, updated_at, closed_at, comments, author_login, assignee_login, is_pull_request "
        "FROM issues WHERE repo_id=?1 AND state=?2 "
        "ORDER BY number DESC LIMIT ?3 OFFSET ?4;";

    const char* sql_used = state.empty() ? sql_all : sql_state;

    if (sqlite3_prepare_v2(sdb, sql_used, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }

    int bi = 1;
    sqlite3_bind_int(stmt, bi++, rid);
    if (!state.empty()) sqlite3_bind_text(stmt, bi++, state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, bi++, limit);
    sqlite3_bind_int(stmt, bi++, offset);

    std::string out = R"({"items":[)";
    bool first = true;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (!first) out += ",";
        first = false;

        const int number = sqlite3_column_int(stmt, 0);
        const unsigned char* st = sqlite3_column_text(stmt, 1);
        const unsigned char* title = sqlite3_column_text(stmt, 2);
        const unsigned char* created_at = sqlite3_column_text(stmt, 3);
        const unsigned char* updated_at = sqlite3_column_text(stmt, 4);
        const unsigned char* closed_at = sqlite3_column_text(stmt, 5);
        const int comments = sqlite3_column_int(stmt, 6);
        const unsigned char* author = sqlite3_column_text(stmt, 7);
        const unsigned char* assignee = sqlite3_column_text(stmt, 8);
        const int is_pr = sqlite3_column_int(stmt, 9);

        out += "{";
        out += "\"number\":" + std::to_string(number);
        out += ",\"state\":\"" + util::json_escape(st ? (const char*)st : "") + "\"";
        out += ",\"title\":\"" + util::json_escape(title ? (const char*)title : "") + "\"";
        out += ",\"created_at\":\"" + util::json_escape(created_at ? (const char*)created_at : "") + "\"";
        out += ",\"updated_at\":\"" + util::json_escape(updated_at ? (const char*)updated_at : "") + "\"";
        out += ",\"closed_at\":\"" + util::json_escape(closed_at ? (const char*)closed_at : "") + "\"";
        out += ",\"comments\":" + std::to_string(comments);
        out += ",\"author_login\":\"" + util::json_escape(author ? (const char*)author : "") + "\"";
        out += ",\"assignee_login\":\"" + util::json_escape(assignee ? (const char*)assignee : "") + "\"";
        out += ",\"is_pull_request\":" + std::to_string(is_pr);
        out += "}";
    }

    out += "]}";
    sqlite3_finalize(stmt);

    res.set_content(out, "application/json");
}


static void get_repo_pulls_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    (void)db; (void)req;
    not_implemented(res, "pulls");
}


static void get_repo_commits_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    (void)db; (void)req;
    not_implemented(res, "commits");
}


static void get_repo_releases_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    (void)db; (void)req;
    not_implemented(res, "releases");
}


static void get_repo_metrics_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    (void)db; (void)req;
    not_implemented(res, "metrics");
}


static void get_repo_health_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    (void)db; (void)req;
    not_implemented(res, "health");
}

// 对外提供一个注册函数，只注册 GET 路由
void register_get_routes(httplib::Server& app, Db& db)
{
    app.Get("/api/health", health_handler);

    app.Get("/api/repos",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repos_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/snapshots)",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_snapshots_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/issues)", 
            [&db](const httplib::Request& req, httplib::Response& res) 
            {
                get_repo_issues_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/pulls)", 
            [&db](const httplib::Request& req, httplib::Response& res) 
            {
                get_repo_pulls_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/commits)", 
            [&db](const httplib::Request& req, httplib::Response& res) 
            {
                get_repo_commits_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/releases)", 
            [&db](const httplib::Request& req, httplib::Response& res) 
            {
                get_repo_releases_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/metrics)", 
            [&db](const httplib::Request& req, httplib::Response& res) 
            {
                get_repo_metrics_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/health)",
            [&db](const httplib::Request& req, httplib::Response& res) 
            {
                get_repo_health_handler(db, req, res);
            });
}