#include "routes.h"
#include "common/util.h"
#include "repo_metrics/metrics.h"
#include "repo_metrics/health.h"
#include "repo_metrics/github_client.h"
#include "repo_metrics/hotspots.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <sqlite3.h>

static void Print_HotFiles(const int rid, const std::vector<HotFile>& hot_files)
{
    return;
    
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
    return;

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

static int get_repo_ci_consecutive_failures(Db& db, int repo_id, int max_check)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT conclusion "
        "FROM ci_workflow_runs "
        "WHERE repo_id=?1 AND status='completed' "
        "ORDER BY created_at DESC, run_id DESC "
        "LIMIT ?2;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_int(stmt, 2, std::max(1, std::min(50, max_check)));

    int consecutive = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char* c = sqlite3_column_text(stmt, 0);
        const std::string conclusion = c ? (const char*)c : "";
        if (conclusion == "success") break;
        consecutive++;
    }

    sqlite3_finalize(stmt);
    return consecutive;
}

static void get_repo_ci_runs_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    const int limit = std::max(1, std::min(200, get_int_param(req, "limit", 50)));
    const int offset = std::max(0, get_int_param(req, "offset", 0));
    const std::string status = req.has_param("status") ? req.get_param_value("status") : "";
    const std::string conclusion = req.has_param("conclusion") ? req.get_param_value("conclusion") : "";

    std::string sql =
        "SELECT run_id, workflow_id, name, head_branch, event, status, conclusion, "
        "created_at, updated_at, run_started_at, html_url, actor_login, run_attempt "
        "FROM ci_workflow_runs WHERE repo_id=?";

    const bool has_status = !status.empty();
    const bool has_conclusion = !conclusion.empty();
    if (has_status) {
        sql += " AND status=?";
    }
    if (has_conclusion) {
        sql += " AND conclusion=?";
    }
    sql += " ORDER BY created_at DESC, run_id DESC LIMIT ? OFFSET ?;";

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }

    int bi = 1;
    sqlite3_bind_int(stmt, bi++, rid);
    if (has_status) sqlite3_bind_text(stmt, bi++, status.c_str(), -1, SQLITE_TRANSIENT);
    if (has_conclusion) sqlite3_bind_text(stmt, bi++, conclusion.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, bi++, limit);
    sqlite3_bind_int(stmt, bi++, offset);

    std::string out = R"({"items":[)";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (!first) out += ",";
        first = false;

        const long long run_id = sqlite3_column_int64(stmt, 0);
        const long long workflow_id = sqlite3_column_int64(stmt, 1);
        const unsigned char* name = sqlite3_column_text(stmt, 2);
        const unsigned char* head_branch = sqlite3_column_text(stmt, 3);
        const unsigned char* event = sqlite3_column_text(stmt, 4);
        const unsigned char* st = sqlite3_column_text(stmt, 5);
        const unsigned char* con = sqlite3_column_text(stmt, 6);
        const unsigned char* created_at = sqlite3_column_text(stmt, 7);
        const unsigned char* updated_at = sqlite3_column_text(stmt, 8);
        const unsigned char* run_started_at = sqlite3_column_text(stmt, 9);
        const unsigned char* html_url = sqlite3_column_text(stmt, 10);
        const unsigned char* actor_login = sqlite3_column_text(stmt, 11);
        const int run_attempt = sqlite3_column_int(stmt, 12);

        out += std::string("{\"run_id\":") + std::to_string(run_id)
            + ",\"workflow_id\":" + std::to_string(workflow_id)
            + ",\"name\":\"" + util::json_escape(name ? (const char*)name : "") + "\""
            + ",\"head_branch\":\"" + util::json_escape(head_branch ? (const char*)head_branch : "") + "\""
            + ",\"event\":\"" + util::json_escape(event ? (const char*)event : "") + "\""
            + ",\"status\":\"" + util::json_escape(st ? (const char*)st : "") + "\""
            + ",\"conclusion\":\"" + util::json_escape(con ? (const char*)con : "") + "\""
            + ",\"created_at\":\"" + util::json_escape(created_at ? (const char*)created_at : "") + "\""
            + ",\"updated_at\":\"" + util::json_escape(updated_at ? (const char*)updated_at : "") + "\""
            + ",\"run_started_at\":\"" + util::json_escape(run_started_at ? (const char*)run_started_at : "") + "\""
            + ",\"html_url\":\"" + util::json_escape(html_url ? (const char*)html_url : "") + "\""
            + ",\"actor_login\":\"" + util::json_escape(actor_login ? (const char*)actor_login : "") + "\""
            + ",\"run_attempt\":" + std::to_string(run_attempt)
            + "}";
    }

    sqlite3_finalize(stmt);
    out += "]}";
    res.set_content(out, "application/json");
}

static void get_repo_ci_health_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    int completed_24h = 0;
    int failed_24h = 0;
    const char* sql_24h =
        "SELECT COUNT(*), "
        "SUM(CASE WHEN conclusion IS NOT NULL AND conclusion!='' AND conclusion!='success' THEN 1 ELSE 0 END) "
        "FROM ci_workflow_runs "
        "WHERE repo_id=?1 AND status='completed' AND created_at >= datetime('now','-24 hours');";

    if (sqlite3_prepare_v2(sdb, sql_24h, -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, rid);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            completed_24h = sqlite3_column_int(stmt, 0);
            failed_24h = sqlite3_column_int(stmt, 1);
        }
    }
    if (stmt) sqlite3_finalize(stmt);

    double avg_duration_hours_7d = 0.0;
    const char* sql_duration =
        "SELECT AVG((julianday(updated_at) - julianday(run_started_at)) * 24.0) "
        "FROM ci_workflow_runs "
        "WHERE repo_id=?1 AND run_started_at!='' AND updated_at!='' AND updated_at >= datetime('now','-7 days');";

    stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql_duration, -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, rid);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            avg_duration_hours_7d = sqlite3_column_double(stmt, 0);
        }
    }
    if (stmt) sqlite3_finalize(stmt);

    std::string latest_run_at;
    const char* sql_latest =
        "SELECT MAX(created_at) FROM ci_workflow_runs WHERE repo_id=?1;";

    stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql_latest, -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, rid);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const unsigned char* latest = sqlite3_column_text(stmt, 0);
            latest_run_at = latest ? (const char*)latest : "";
        }
    }
    if (stmt) sqlite3_finalize(stmt);

    const int consecutive_failures = get_repo_ci_consecutive_failures(db, rid, 20);
    const double failure_rate_24h = completed_24h > 0 ? ((double)failed_24h / (double)completed_24h) : 0.0;

    double score = 100.0;
    score -= std::min(70.0, failure_rate_24h * 100.0 * 0.70);
    score -= std::min(30.0, (double)consecutive_failures * 10.0);
    if (completed_24h == 0) score -= 10.0;
    if (score < 0.0) score = 0.0;

    std::string health_level = "healthy";
    if (score < 60.0) health_level = "critical";
    else if (score < 80.0) health_level = "warning";

    std::string out = std::string("{\"repo_id\":") + std::to_string(rid)
        + ",\"health_level\":\"" + util::json_escape(health_level) + "\""
        + ",\"score\":" + std::to_string(score)
        + ",\"completed_24h\":" + std::to_string(completed_24h)
        + ",\"failed_24h\":" + std::to_string(failed_24h)
        + ",\"failure_rate_24h\":" + std::to_string(failure_rate_24h)
        + ",\"consecutive_failures\":" + std::to_string(consecutive_failures)
        + ",\"avg_duration_hours_7d\":" + std::to_string(avg_duration_hours_7d)
        + ",\"latest_run_at\":\"" + util::json_escape(latest_run_at) + "\""
        + "}";

    res.set_content(out, "application/json");
}

static void get_repo_ci_trend_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    const int days = std::max(1, std::min(60, get_int_param(req, "days", 7)));
    const std::string window = "-" + std::to_string(days) + " days";

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT date(created_at) AS d, "
        "COUNT(*) AS completed_count, "
        "SUM(CASE WHEN conclusion IS NOT NULL AND conclusion!='' AND conclusion!='success' THEN 1 ELSE 0 END) AS failed_count, "
        "AVG((julianday(updated_at) - julianday(run_started_at)) * 24.0) AS avg_duration_hours "
        "FROM ci_workflow_runs "
        "WHERE repo_id=?1 AND status='completed' AND created_at >= datetime('now', ?2) "
        "GROUP BY date(created_at) "
        "ORDER BY d ASC;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }

    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_text(stmt, 2, window.c_str(), -1, SQLITE_TRANSIENT);

    std::string out = std::string("{\"repo_id\":") + std::to_string(rid) +
        ",\"days\":" + std::to_string(days) +
        ",\"items\":[";

    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (!first) out += ",";
        first = false;

        const unsigned char* d = sqlite3_column_text(stmt, 0);
        const int completed = sqlite3_column_int(stmt, 1);
        const int failed = sqlite3_column_int(stmt, 2);
        const double avg_duration = sqlite3_column_double(stmt, 3);
        const double failure_rate = completed > 0 ? ((double)failed / (double)completed) : 0.0;

        out += std::string("{\"date\":\"") + util::json_escape(d ? (const char*)d : "") +
            "\",\"completed\":" + std::to_string(completed) +
            ",\"failed\":" + std::to_string(failed) +
            ",\"failure_rate\":" + std::to_string(failure_rate) +
            ",\"avg_duration_hours\":" + std::to_string(avg_duration) +
            "}";
    }

    sqlite3_finalize(stmt);
    out += "]}";
    res.set_content(out, "application/json");
}

static void get_repo_intro_handler(Db& db,const httplib::Request &req,httplib::Response &res)
{
    const int rid = std::stoi(req.matches[1]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql ="SELECT intro_text, intro_updated_at FROM repos WHERE id=?1;";

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

    const unsigned char* intro = sqlite3_column_text(stmt, 0);
    const unsigned char* updated = sqlite3_column_text(stmt, 1);

    nlohmann::json out;
    out["ok"] = true;
    out["repo_id"] = rid;
    out["intro_text"] = intro ? (const char*)intro : "";
    out["intro_updated_at"] = updated ? (const char*)updated : "";
    sqlite3_finalize(stmt);

    res.set_content(out.dump(), "application/json; charset=utf-8");
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

    app.Get(R"(/api/repos/(\d+)/ci/runs)",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_ci_runs_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/ci/health)",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_ci_health_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/ci/trend)",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_ci_trend_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+)/intro)",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_intro_handler(db, req, res);
            });
}