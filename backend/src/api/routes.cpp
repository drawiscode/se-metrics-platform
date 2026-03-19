// filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/src/routes.cpp
#include "routes.h"
#include "common/util.h"
#include "db/db.h"
#include "repo_metrics/github_client.h"

#include <cmath>
#include <nlohmann/json.hpp>
#include <string>
//#include <sqlite3.h>


static void health_handler(const httplib::Request&, httplib::Response& res)
{
    res.set_content(R"({"ok":true})", "application/json");
}

static void post_projects_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    auto name = req.get_param_value("name");
    if (name.empty())
    {
        res.status = 400;
        res.set_content(R"({"error":"missing name"})", "application/json");
        return;
    }

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO projects(name) VALUES (?1);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        res.status = 409;
        res.set_content(R"({"error":"project exists or insert failed"})", "application/json");
        return;
    }

    long long id = sqlite3_last_insert_rowid(sdb);
    res.set_content(std::string("{\"id\":") + std::to_string(id) + ",\"name\":\"" + util::json_escape(name) + "\"}", "application/json");
}

static void post_repos_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    auto full_name = req.get_param_value("full_name");
    if (full_name.empty())
    {
        res.status = 400;
        res.set_content(R"({"error":"missing full_name"})", "application/json");
        return;
    }

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO repos(full_name) VALUES (?1);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_text(stmt, 1, full_name.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        res.status = 409;
        res.set_content(R"({"error":"repo exists or insert failed"})", "application/json");
        return;
    }

    long long id = sqlite3_last_insert_rowid(sdb);
    res.set_content(std::string("{\"id\":") + std::to_string(id) + ",\"full_name\":\"" + util::json_escape(full_name) + "\"}", "application/json");
}

static void post_project_repos_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int project_id = std::stoi(req.matches[1]);
    int repo_id = std::stoi(req.matches[2]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO project_repos(project_id, repo_id) VALUES (?1, ?2);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_int(stmt, 1, project_id);
    sqlite3_bind_int(stmt, 2, repo_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        res.status = 409;
        res.set_content(R"({"error":"association exists or insert failed"})", "application/json");
        return;
    }

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

static void get_projects_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, name FROM projects ORDER BY id DESC LIMIT 200;";
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
        const unsigned char* nm = sqlite3_column_text(stmt, 1);

        out += "{\"id\":" + std::to_string(id)
            + ",\"name\":\"" + util::json_escape(nm ? (const char*)nm : "") + "\""
            + "}";
    }
    out += "]}";
    sqlite3_finalize(stmt);
    res.set_content(out, "application/json");
}

static void get_project_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int project_id = std::stoi(req.matches[1]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, name FROM projects WHERE id = ?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_int(stmt, 1, project_id);

    if (sqlite3_step(stmt) != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        res.status = 404;
        res.set_content(R"({"error":"project not found"})", "application/json");
        return;
    }

    int id = sqlite3_column_int(stmt, 0);
    const unsigned char* nm = sqlite3_column_text(stmt, 1);

    std::string out = "{\"id\":" + std::to_string(id)
        + ",\"name\":\"" + util::json_escape(nm ? (const char*)nm : "") + "\""
        + "}";

    sqlite3_finalize(stmt);
    res.set_content(out, "application/json");
}

static void put_project_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int pid = std::stoi(req.matches[1]);
    auto name = req.get_param_value("name");
    if (name.empty())
    {
        res.status = 400;
        res.set_content(R"({"error":"missing name"})", "application/json");
        return;
    }

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE projects SET name=?1 WHERE id=?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, pid);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        res.status = 500;
        res.set_content(R"({"error":"db step failed"})", "application/json");
        return;
    }

    if (sqlite3_changes(sdb) == 0)
    {
        res.status = 404;
        res.set_content(R"({"error":"project not found"})", "application/json");
        return;
    }

    res.set_content(std::string("{\"id\":") + std::to_string(pid) + ",\"name\":\"" + util::json_escape(name) + "\"}", "application/json");
}

static void delete_project_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int pid = std::stoi(req.matches[1]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM projects WHERE id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_int(stmt, 1, pid);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        res.status = 500;
        res.set_content(R"({"error":"db step failed"})", "application/json");
        return;
    }

    if (sqlite3_changes(sdb) == 0)
    {
        res.status = 404;
        res.set_content(R"({"error":"project not found"})", "application/json");
        return;
    }

    res.set_content(R"({"ok":true})", "application/json");
}

static void get_project_repos_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int pid = std::stoi(req.matches[1]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT r.id, r.full_name, r.enabled "
        "FROM project_repos pr "
        "JOIN repos r ON r.id = pr.repo_id "
        "WHERE pr.project_id = ?1 "
        "ORDER BY r.id DESC LIMIT 200;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_int(stmt, 1, pid);

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

static void delete_project_repo_link_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int pid = std::stoi(req.matches[1]);
    int rid = std::stoi(req.matches[2]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM project_repos WHERE project_id=?1 AND repo_id=?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_int(stmt, 1, pid);
    sqlite3_bind_int(stmt, 2, rid);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        res.status = 500;
        res.set_content(R"({"error":"db step failed"})", "application/json");
        return;
    }

    if (sqlite3_changes(sdb) == 0)
    {
        res.status = 404;
        res.set_content(R"({"error":"link not found"})", "application/json");
        return;
    }

    res.set_content(R"({"ok":true})", "application/json");
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

static void put_repo_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    auto enabled_s = req.get_param_value("enabled");
    if (enabled_s != "0" && enabled_s != "1")
    {
        res.status = 400;
        res.set_content(R"({"error":"enabled must be 0 or 1"})", "application/json");
        return;
    }
    int enabled = (enabled_s == "1") ? 1 : 0;

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE repos SET enabled=?1 WHERE id=?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_int(stmt, 1, enabled);
    sqlite3_bind_int(stmt, 2, rid);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        res.status = 500;
        res.set_content(R"({"error":"db step failed"})", "application/json");
        return;
    }

    if (sqlite3_changes(sdb) == 0)
    {
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", "application/json");
        return;
    }

    res.set_content(std::string("{\"ok\":true,\"id\":") + std::to_string(rid) + ",\"enabled\":" + std::to_string(enabled) + "}", "application/json");
}

static void delete_repo_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM repos WHERE id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", "application/json");
        return;
    }
    sqlite3_bind_int(stmt, 1, rid);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        res.status = 500;
        res.set_content(R"({"error":"db step failed"})", "application/json");
        return;
    }

    if (sqlite3_changes(sdb) == 0)
    {
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", "application/json");
        return;
    }

    res.set_content(R"({"ok":true})", "application/json");
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

static long long db_insert_sync_run(Db& db, int repo_id, const std::string& status, const std::string& error)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO repo_sync_runs(repo_id, status, error) VALUES (?1, ?2, ?3);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, error.empty() ? nullptr : error.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    return sqlite3_last_insert_rowid(sdb);
}

static bool db_finish_sync_run(Db& db, long long run_id, const std::string& status, const std::string& error)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE repo_sync_runs "
        "SET finished_at=datetime('now'), status=?1, error=?2 "
        "WHERE id=?3;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, error.empty() ? nullptr : error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)run_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

static long long db_insert_snapshot(Db& db, int repo_id,const std::string& full_name,const RepoSnapshotData &data)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO repo_snapshots(repo_id, full_name, stars, forks, open_issues, watchers, pushed_at, raw_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, full_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, data.stars);
    sqlite3_bind_int(stmt, 4, data.forks);
    sqlite3_bind_int(stmt, 5, data.open_issues);
    sqlite3_bind_int(stmt, 6, data.watchers);
    sqlite3_bind_text(stmt, 7, data.pushed_at.empty() ? nullptr : data.pushed_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, data.raw_json.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    return sqlite3_last_insert_rowid(sdb);
}




static long long db_insert_issue(Db& db, int repo_id,const std::string& full_name,const RepoIssueData &data)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO pull_requests(repo_id, number, state, title, created_at, updated_at, closed_at, merge_at, author_login, raw_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, repo_id);                             // repo_id
    sqlite3_bind_int(stmt, 2, data.number);                         // number
    sqlite3_bind_text(stmt, 3, data.state.c_str(), -1, SQLITE_TRANSIENT);        // state
    sqlite3_bind_text(stmt, 4, data.title.c_str(), -1, SQLITE_TRANSIENT);        // title
    sqlite3_bind_text(stmt, 5, data.created_at.c_str(), -1, SQLITE_TRANSIENT);   // created_at
    sqlite3_bind_text(stmt, 6, data.updated_at.c_str(), -1, SQLITE_TRANSIENT);   // updated_at
    sqlite3_bind_text(stmt, 7,
                      data.closed_at.empty() ? nullptr : data.closed_at.c_str(),
                      -1, SQLITE_TRANSIENT);                                      // closed_at
    sqlite3_bind_int(stmt, 8, data.comments);                                     // comments
    sqlite3_bind_text(stmt, 9, data.author_login.c_str(), -1, SQLITE_TRANSIENT); // author_login
    sqlite3_bind_int(stmt, 10, data.is_pull_request ? 1 : 0);                     // is_pull_request
    sqlite3_bind_text(stmt, 11, data.raw_json.c_str(), -1, SQLITE_TRANSIENT); // raw_json

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    return sqlite3_last_insert_rowid(sdb);
}

static long long db_insert_pullrequest(Db& db, int repo_id,const std::string& full_name,const RepoPullRequestData &data)
{
    return -1;
}

static bool insert_snapshot_to_db(Db& db, int rid, const std::string& full_name, const GitHubResponse& gh, const std::string& token, long long run_id, httplib::Response& res)
{
    RepoSnapshotData data;
    std::string err;
    int http_status = 0;
    //从github的response中解析出snapshot数据，如果失败了就记录错误信息并返回
    //data中存储了stars、forks、open_issues、watchers、pushed_at等信息，以及原始的json字符串，用于插入字符串
    if (!fetch_repo_snapshot_from_github(gh,full_name, token, data, err, http_status)) 
    {
        if (run_id > 0) 
        {
            db_finish_sync_run(db, run_id, "error", err);
        }
        res.status = (http_status >= 400 && http_status < 600) ? 502 : 502;
        res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
        return false;
    }
    const long long snapshot_id = db_insert_snapshot(db, rid, full_name, data);
    if (snapshot_id <= 0)
    {
        std::string err = "db insert snapshot failed";
        if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
        res.status = 500;
        res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
        return false;
    }
    return true;
}

// 加载issues信息到表中
static bool insert_issue_to_db(Db& db, int rid, const std::string& full_name, const GitHubResponse& gh, const std::string& token, long long run_id, httplib::Response& res)
{
    RepoIssueData data;
    std::string err;
    int http_status = 0;
    if (!fetch_repo_issue_from_github(gh, full_name, token, data,err, http_status))
    {
        if (run_id > 0)
        {
            db_finish_sync_run(db, run_id, "error", err);
        }
        res.status = (http_status >= 400 && http_status < 600) ? 502 : 502;
        res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
        return false;
    }
    const long long issues_id = db_insert_issue(db, rid, full_name, data);
    if (issues_id <= 0)
    {
        std::string err = "db insert issue failed";
        if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
        res.status = 500;
        res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
        return false;
    }
    return true;
}

static bool insert_pr_to_db(Db& db, int rid, const std::string& full_name, const GitHubResponse& gh, const std::string& token, long long run_id, httplib::Response& res)
{
    RepoPullRequestData data;
    std::string err;
    int http_status = 0;
    if (!fetch_repo_pull_from_github(gh, full_name, token, data, err, http_status))
    {
        if (run_id > 0)
        {
            db_finish_sync_run(db, run_id, "error", err);
        }
        res.status = (http_status >= 400 && http_status < 600) ? 502 : 502;
        res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
        return false;
    }
    const long long pr_id = db_insert_pullrequest(db, rid, full_name, data);
    if (pr_id <= 0)
    {
        std::string err = "db insert pullrequest failed";
        if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
        res.status = 500;
        res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
        return false;
    }
    return true;
}


//加载新的信息到各种表中
static void post_repo_sync_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);

    std::string full_name;
    if (!db_get_repo_full_name(db, rid, full_name))
    {
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", "application/json");
        return;
    }

    const std::string token = util::get_env("GITHUB_TOKEN", "");

    //看下token到底拿到没有
    Judge_GitHub_Token(token);

    const long long run_id = db_insert_sync_run(db, rid, "error", "started"); // 先占位，后面会更新
    (void)run_id;

    auto gh = github_get_repo(full_name, token);//得到github的response，里面有status和body等信息，body是json字符串

    //加载snapshot的信息到表中
    if(!insert_snapshot_to_db(db, rid, full_name, gh, token, run_id, res)){return;}

    // 加载issues的信息到表中
    if(!insert_issue_to_db(db, rid, full_name, gh, token, run_id, res)){return;}

    // 加载PR的信息到表中
    if(!insert_pr_to_db(db, rid, full_name, gh, token, run_id, res)){return;}

    if (run_id > 0) db_finish_sync_run(db, run_id, "ok", "");

    res.set_content(std::string("{\"ok\":true,\"repo_id\":") + std::to_string(rid) + "}",
                    "application/json");
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

static int get_int_param(const httplib::Request& req, const std::string& key, int defv) {
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
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


static void not_implemented(httplib::Response& res, const char* what)
{
    res.status = 501;
    res.set_content(std::string("{\"error\":\"") + what + " not implemented\"}", "application/json");
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


void register_routes(httplib::Server& app, Db& db)
{
    app.Get("/api/health", health_handler);

    app.Post("/api/projects",
             [&db](const httplib::Request& req, httplib::Response& res)
             {
                 post_projects_handler(db, req, res);
             });

    app.Post("/api/repos",
             [&db](const httplib::Request& req, httplib::Response& res)
             {
                 post_repos_handler(db, req, res);
             });

    app.Post(R"(/api/projects/(\d+)/repos/(\d+))",
             [&db](const httplib::Request& req, httplib::Response& res)
             {
                 post_project_repos_handler(db, req, res);
             });

    app.Get("/api/repos",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repos_handler(db, req, res);
            });

    app.Get("/api/projects",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_projects_handler(db, req, res);
            });

    app.Get(R"(/api/projects/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_project_handler(db, req, res);
            });

    app.Put(R"(/api/projects/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                put_project_handler(db, req, res);
            });

    app.Delete(R"(/api/projects/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                delete_project_handler(db, req, res);
            });

    app.Get(R"(/api/projects/(\d+)/repos)",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_project_repos_handler(db, req, res);
            });

    app.Delete(R"(/api/projects/(\d+)/repos/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                delete_project_repo_link_handler(db, req, res);
            });

    app.Get(R"(/api/repos/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_handler(db, req, res);
            });

    app.Put(R"(/api/repos/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                put_repo_handler(db, req, res);
            });

    app.Delete(R"(/api/repos/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                delete_repo_handler(db, req, res);
            });

    app.Post(R"(/api/repos/(\d+)/sync)",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                try
                {
                    post_repo_sync_handler(db, req, res);
                }
                catch (const std::exception& e)
                {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}",
                                    "application/json");
                }
                catch (...)
                {
                    res.status = 500;
                    res.set_content(R"({"error":"unknown server error"})", "application/json");
                }
            });

    app.Get(R"(/api/repos/(\d+)/snapshots)",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_snapshots_handler(db, req, res);
            });


    // 数据查看（落库结果）
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

    // 指标/评分
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


