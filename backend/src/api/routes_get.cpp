#include "routes.h"
#include "common/util.h"
#include "repo_metrics/metrics.h"
#include "repo_metrics/health.h"
#include "repo_metrics/github_client.h"
#include "repo_metrics/hotspots.h"

#include <sqlite3.h>

static void Print_HotFiles(const int rid, const std::vector<HotFile>& hot_files)
{
    std::cerr << "Hot files for repo_id=" << rid << ":\n";
    for (const auto& f : hot_files)
    {
        std::cerr << "  " << f.filename << " (commits=" << f.commits
                  << ", additions=" << f.additions
                  << ", deletions=" << f.deletions
                  << ", churn=" << f.churn() << ")\n";
    }
}

static void Print_HotDirs(const int rid, const std::vector<HotDir>& hot_dirs)
{
    std::cerr << "Hot Dirs for repo_id=" << rid << ":\n";
    for (const auto& f : hot_dirs)
    {
        std::cerr << "  " << f.dir << " (commits=" << f.commits
                  << ", additions=" << f.additions
                  << ", deletions=" << f.deletions
                  << ", churn=" << f.churn() << ")\n";
    }
}

static int get_int_param(const httplib::Request& req, const std::string& key, int defv) {
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
}


static void health_handler(const httplib::Request&, httplib::Response& res)
{
    res.set_content(R"({"ok":true})", "application/json");
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
    

    std::string out = std::string("{\"id\":") + std::to_string(id)
        + ",\"full_name\":\"" + util::json_escape(fn ? (const char*)fn : "") + "\""
        + ",\"enabled\":" + std::to_string(enabled)
        + "}";
        
    //应该放在这里结束，而不是前面
    sqlite3_finalize(stmt);
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


    // 为了简单，用两套 SQL 避免动态 bind 索引混乱
    const char* sql_all =
        "SELECT number, state, title, created_at, updated_at, closed_at, comments, author_login, is_pull_request "
        "FROM issues WHERE repo_id=?1 "
        "ORDER BY number DESC LIMIT ?2 OFFSET ?3;";

    const char* sql_state =
        "SELECT number, state, title, created_at, updated_at, closed_at, comments, author_login, is_pull_request "
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

        const int is_pr = sqlite3_column_int(stmt, 8);

        out += "{\n";
        out += "  \"number\": " + std::to_string(number) + ",\n";
        out += "  \"state\": \"" + util::json_escape(st ? (const char*)st : "") + "\",\n";
        out += "  \"title\": \"" + util::json_escape(title ? (const char*)title : "") + "\",\n";
        out += "  \"created_at\": \"" + util::json_escape(created_at ? (const char*)created_at : "") + "\",\n";
        out += "  \"updated_at\": \"" + util::json_escape(updated_at ? (const char*)updated_at : "") + "\",\n";
        out += "  \"closed_at\": \"" + util::json_escape(closed_at ? (const char*)closed_at : "") + "\",\n";
        out += "  \"comments\": " + std::to_string(comments) + ",\n";
        out += "  \"author_login\": \"" + util::json_escape(author ? (const char*)author : "") + "\",\n";
        out += "  \"is_pull_request\": " + std::to_string(is_pr) + "\n";
        out += "}\n";
    }

    out += "]}";
    sqlite3_finalize(stmt);

    res.set_content(out, "application/json");
}


static void get_repo_pulls_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);

    const int limit = std::max(1, std::min(200, get_int_param(req, "limit", 100)));
    const int offset = std::max(0, get_int_param(req, "offset", 0));
    const std::string state = req.has_param("state") ? req.get_param_value("state") : ""; // open/closed/""(all)

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    const char* sql_all =
        "SELECT number, state, title, created_at, updated_at, closed_at, merged_at, author_login "
        "FROM pull_requests WHERE repo_id=?1 "
        "ORDER BY number DESC LIMIT ?2 OFFSET ?3;";

    const char* sql_state =
        "SELECT number, state, title, created_at, updated_at, closed_at, merged_at, author_login "
        "FROM pull_requests WHERE repo_id=?1 AND state=?2 "
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
        const unsigned char* merged_at = sqlite3_column_text(stmt, 6);
        const unsigned char* author = sqlite3_column_text(stmt, 7);

        out += "{";
        out += "\"number\":" + std::to_string(number) + ",";
        out += "\"state\":\"" + util::json_escape(st ? (const char*)st : "") + "\",";
        out += "\"title\":\"" + util::json_escape(title ? (const char*)title : "") + "\",";
        out += "\"created_at\":\"" + util::json_escape(created_at ? (const char*)created_at : "") + "\",";
        out += "\"updated_at\":\"" + util::json_escape(updated_at ? (const char*)updated_at : "") + "\",";
        out += "\"closed_at\":\"" + util::json_escape(closed_at ? (const char*)closed_at : "") + "\",";
        out += "\"merged_at\":\"" + util::json_escape(merged_at ? (const char*)merged_at : "") + "\",";
        out += "\"author_login\":\"" + util::json_escape(author ? (const char*)author : "") + "\"";
        out += "}";
    }

    out += "]}";
    sqlite3_finalize(stmt);

    res.set_content(out, "application/json");
}


static void get_repo_commits_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);

    const int limit = std::max(1, std::min(500, get_int_param(req, "limit", 100)));
    const int offset = std::max(0, get_int_param(req, "offset", 0));

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT sha, author_login, committed_at FROM commits WHERE repo_id=?1 ORDER BY committed_at DESC LIMIT ?2 OFFSET ?3;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }

    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_int(stmt, 2, limit);
    sqlite3_bind_int(stmt, 3, offset);

    std::string out = R"({"items":[)";
    bool first = true;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (!first) out += ",";
        first = false;

        const unsigned char* sha = sqlite3_column_text(stmt, 0);
        const unsigned char* author = sqlite3_column_text(stmt, 1);
        const unsigned char* committed_at = sqlite3_column_text(stmt, 2);

        out += "{";
        out += "\"sha\":\"" + util::json_escape(sha ? (const char*)sha : "") + "\",";
        out += "\"author_login\":\"" + util::json_escape(author ? (const char*)author : "") + "\",";
        out += "\"committed_at\":\"" + util::json_escape(committed_at ? (const char*)committed_at : "") + "\"";
        out += "}";
    }

    out += "]}";
    sqlite3_finalize(stmt);
    res.set_content(out, "application/json");
}


static void get_repo_releases_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
     const int rid = std::stoi(req.matches[1]);

    const int limit = std::max(1, std::min(200, get_int_param(req, "limit", 100)));
    const int offset = std::max(0, get_int_param(req, "offset", 0));

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT tag_name, name, draft, prerelease, published_at FROM releases WHERE repo_id=?1 ORDER BY published_at DESC LIMIT ?2 OFFSET ?3;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }

    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_int(stmt, 2, limit);
    sqlite3_bind_int(stmt, 3, offset);

    std::string out = R"({"items":[)";
    bool first = true;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (!first) out += ",";
        first = false;

        const unsigned char* tag = sqlite3_column_text(stmt, 0);
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        const int draft = sqlite3_column_int(stmt, 2);
        const int prerelease = sqlite3_column_int(stmt, 3);
        const unsigned char* published_at = sqlite3_column_text(stmt, 4);

        out += "{";
        out += "\"tag_name\":\"" + util::json_escape(tag ? (const char*)tag : "") + "\",";
        out += "\"name\":\"" + util::json_escape(name ? (const char*)name : "") + "\",";
        out += "\"draft\":" + std::to_string(draft) + ",";
        out += "\"prerelease\":" + std::to_string(prerelease) + ",";
        out += "\"published_at\":\"" + util::json_escape(published_at ? (const char*)published_at : "") + "\"";
        out += "}";
    }

    out += "]}";
    sqlite3_finalize(stmt);
    res.set_content(out, "application/json");
}


static void get_repo_metrics_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    RepoMetrics m = compute_repo_metrics(db, rid);
    std::string body = "{\"metrics\":" + repo_metrics_to_json(m)  + "}";
    res.set_content(body, "application/json");
}


static void get_repo_health_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    RepoMetrics m = compute_repo_metrics(db, rid);
    HealthScore h = compute_health_from_metrics(m);
    std::string body = "{\"health\":" + repo_health_to_json(h) + "}";
    res.set_content(body, "application/json");
}


static void get_repo_hotfiles_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    const int days = std::max(0, get_int_param(req, "days", 0));            // days_window，0 表示不限
    const int top_n = std::max(1, std::min(200, get_int_param(req, "top", 20))); // top_n，1..200

    std::vector<HotFile> files = compute_hot_files(db, res, rid, days, top_n);
    if(res.status == 500)
    {
        return;
    }

    Print_HotFiles(rid, files);

    std::string out = R"({"items":[)";
    bool first = true;
    for (const auto& f : files)
    {
        if (!first) out += ",";
        first = false;
        out += "{\"filename\":\"" + util::json_escape(f.filename.empty() ? "" : f.filename.c_str()) + "\""
            + ",\"commits\":" + std::to_string(f.commits)
            + ",\"additions\":" + std::to_string(f.additions)
            + ",\"deletions\":" + std::to_string(f.deletions)
            + "}";
    }
    out += "]}";
    res.set_content(out, "application/json");
}


static void get_repo_hotdirs_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    const int days = std::max(0, get_int_param(req, "days", 0));            // days_window，0 表示不限
    const int top_n = std::max(1, std::min(200, get_int_param(req, "top", 20))); // top_n，1..200
    const int dir_depth = std::max(1, std::min(10, get_int_param(req, "dir_depth", 2))); // 目录深度，1..10，默认2

    std::vector<HotDir> dirs = compute_hot_dirs(db, res, rid, days, top_n, dir_depth);
    if(res.status == 500)
    {
        return;
    }

    Print_HotDirs(rid, dirs);

    std::string out = R"({"items":[)";
    bool first = true;
    for (const auto& d : dirs)
    {
        if (!first) out += ",";
        first = false;
        out += "{\"dirname\":\"" + util::json_escape(d.dir.empty() ? "" : d.dir.c_str()) + "\""
            + ",\"commits\":" + std::to_string(d.commits)
            + ",\"additions\":" + std::to_string(d.additions)
            + ",\"deletions\":" + std::to_string(d.deletions)
            + "}";
    }
    out += "]}";
    res.set_content(out, "application/json");
}


static void get_repo_activity_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    const int days = std::max(1, get_int_param(req, "days", 30));

    sqlite3* sdb = db.handle();

    const char* sql =
        "SELECT date(committed_at) AS d, COUNT(*) "
        "FROM commits "
        "WHERE repo_id=? AND committed_at >= datetime('now', ?) "
        "GROUP BY date(committed_at) "
        "ORDER BY d ASC;";

    std::string window = "-" + std::to_string(days) + " days";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }

    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_text(stmt, 2, window.c_str(), -1, SQLITE_TRANSIENT);

    std::string out = R"({"items":[)";
    bool first = true;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char* d = sqlite3_column_text(stmt, 0);
        int cnt = sqlite3_column_int(stmt, 1);

        if (!first) out += ",";
        first = false;

        out += std::string("{\"date\":\"") + (d ? reinterpret_cast<const char*>(d) : "") +
               "\",\"commits\":" + std::to_string(cnt) + "}";
    }

    sqlite3_finalize(stmt);
    out += "]}";

    res.set_content(out, "application/json");
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

    app.Get(R"(/api/repos/(\d+)/hotfiles)",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_hotfiles_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/hotdirs)",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_hotdirs_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/activity)",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_activity_handler(db, req, res);
            });
}