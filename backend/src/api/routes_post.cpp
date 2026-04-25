#include "routes.h"
#include "common/util.h"
#include "ai/knowledge_base.h"
#include <sqlite3.h>
#include "repo_metrics/github_client.h"
#include <nlohmann/json.hpp>
#include <algorithm>

#include "ai/ai_assistant.h"
static void print_debug_pages(int pages,std::string prefix)
{
    if(pages<=0) 
    {
        std::cerr <<"error:the "<<prefix<< " pages is "<<pages<<std::endl;
        return;
    }
    std::cerr << prefix << " pages: " << pages << std::endl;
}

static std::string max_iso8601(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    // GitHub ISO8601 Zulu 时间可用字典序比较
    return (a < b) ? b : a;
}

static void get_repo_commits(std::vector<RepoCommitData>& out, Db& db, int rid)
{
    out.clear(); // 确保从空开始

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT sha, author_login, committed_at FROM commits WHERE repo_id=?1 ORDER BY committed_at DESC;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr<< "get_repo_commits:DB prepare failed in get_repo_commits\n";
        return;
    }

    sqlite3_bind_int(stmt, 1, rid);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char* sha = sqlite3_column_text(stmt, 0);
        const unsigned char* author = sqlite3_column_text(stmt, 1);
        const unsigned char* committed_at = sqlite3_column_text(stmt, 2);

        RepoCommitData item;
        item.sha = sha ? reinterpret_cast<const char*>(sha) : "";
        item.author_login = author ? reinterpret_cast<const char*>(author) : "";
        item.committed_at = committed_at ? reinterpret_cast<const char*>(committed_at) : "";

        out.emplace_back(std::move(item));
    }

    sqlite3_finalize(stmt);
}

static int get_int_query(const httplib::Request& req, const char* name, int def)
{
    if (!req.has_param(name)) return def;
    try { return std::stoi(req.get_param_value(name)); }
    catch (...) { return def; }
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


static bool db_get_sync_state(Db& db, int repo_id,
                              std::string& issues_cursor,
                              std::string& pulls_cursor,
                              std::string& commits_cursor,
                              std::string& releases_cursor)
{
    sqlite3* sdb = db.handle();
    const char* sql =
        "SELECT issues_updated_cursor, pulls_updated_cursor, commits_since_cursor, releases_cursor "
        "FROM repo_sync_state WHERE repo_id=?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, repo_id);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        auto col = [&](int i) -> std::string {
            const unsigned char* t = sqlite3_column_text(stmt, i);
            return t ? reinterpret_cast<const char*>(t) : "";
        };
        issues_cursor = col(0);
        pulls_cursor = col(1);
        commits_cursor = col(2);
        releases_cursor = col(3);
        sqlite3_finalize(stmt);
        return true;
    }
    sqlite3_finalize(stmt);

    // 没有行则创建
    const char* ins =
        "INSERT OR IGNORE INTO repo_sync_state(repo_id, issues_updated_cursor, pulls_updated_cursor, commits_since_cursor, releases_cursor) "
        "VALUES(?, '', '', '', '');";
    sqlite3_stmt* ist = nullptr;
    if (sqlite3_prepare_v2(sdb, ins, -1, &ist, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(ist, 1, repo_id);
    rc = sqlite3_step(ist);
    sqlite3_finalize(ist);
    if (rc != SQLITE_DONE) return false;

    issues_cursor.clear(); pulls_cursor.clear(); commits_cursor.clear(); releases_cursor.clear();
    return true;
}


static bool db_insert_repo(Db& db, const std::string& full_name, long long& new_id)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO repos(full_name) VALUES (?1);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, full_name.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return false;
    new_id = sqlite3_last_insert_rowid(sdb);
    return new_id > 0;
}

// POST /api/repos:只创建仓库repo，不同步issues等信息
static void post_repos_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    auto full_name = req.get_param_value("full_name");
    if (full_name.empty())
    {
        res.status = 400;
        res.set_content(R"({"error":"missing full_name"})", "application/json");
        return;
    }

    long long rid_ll = 0;
    if (!db_insert_repo(db, full_name, rid_ll))
    {
        res.status = 409;
        res.set_content(R"({"error":"repo exists or insert failed"})", "application/json");
        return;
    }

    res.set_content(
        std::string("{\"ok\":true,\"repo_id\":") + std::to_string((int)rid_ll) +
        ",\"full_name\":\"" + util::json_escape(full_name) + "\"}",
        "application/json");
}

static bool db_commit_files_exist_for_sha(Db& db, int repo_id, const std::string& sha)
{
    sqlite3* sdb = db.handle();
    if (!sdb) return false;

    sqlite3_stmt* stmt = nullptr;
    // 用 sha + repo_id 双保险：commit_files 存 sha，但 sha 在不同 repo 理论上不会冲突，这里仍加 repo 约束到 commits
    const char* sql =
        "SELECT 1 "
        "FROM commit_files cf "
        "JOIN commits c ON c.id = cf.commit_id "
        "WHERE c.repo_id=?1 AND cf.sha=?2 "
        "LIMIT 1;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, sha.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
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

static int db_upsert_ci_workflow_run(Db& db, int repo_id, const WorkflowRunData& run)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO ci_workflow_runs(" 
        "repo_id, run_id, workflow_id, name, head_branch, event, status, conclusion, "
        "created_at, updated_at, run_started_at, html_url, actor_login, run_attempt, raw_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15) "
        "ON CONFLICT(repo_id, run_id) DO UPDATE SET "
        "workflow_id=excluded.workflow_id, name=excluded.name, head_branch=excluded.head_branch, "
        "event=excluded.event, status=excluded.status, conclusion=excluded.conclusion, "
        "created_at=excluded.created_at, updated_at=excluded.updated_at, run_started_at=excluded.run_started_at, "
        "html_url=excluded.html_url, actor_login=excluded.actor_login, run_attempt=excluded.run_attempt, "
        "raw_json=excluded.raw_json;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)run.run_id);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)run.workflow_id);
    sqlite3_bind_text(stmt, 4, run.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, run.head_branch.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, run.event.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, run.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, run.conclusion.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, run.created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, run.updated_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, run.run_started_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, run.html_url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, run.actor_login.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 14, run.run_attempt);
    sqlite3_bind_text(stmt, 15, run.raw_json.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 1 : 0;
}

static bool sync_repo_ci_workflow_runs(Db& db,
                                       int repo_id,
                                       const std::string& full_name,
                                       const std::string& token,
                                       long long run_id,
                                       httplib::Response& res,
                                       int& upserted_out,
                                       int pages_count)
{
    upserted_out = 0;
    const int per_page = 100;
    const int max_pages = std::max(1, std::min(10, pages_count));

    for (int page = 1; page <= max_pages; ++page)
    {
        GitHubResponse gh = github_list_workflow_runs(full_name, token, per_page, page, "", "", "");
        std::vector<WorkflowRunData> runs;
        std::string err;
        int http_status = 0;

        if (!parse_workflow_runs_from_github(gh, runs, err, http_status))
        {
            if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
            res.status = 502;
            res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
            return false;
        }

        if (runs.empty()) break;

        for (const auto& run : runs)
        {
            upserted_out += db_upsert_ci_workflow_run(db, repo_id, run);
        }

        if ((int)runs.size() < per_page) break;
    }

    std::cout<<"ci_workflow ends"<<std::endl;
    return true;
}

static double db_ci_failure_rate_24h(Db& db, int repo_id, int& completed_out)
{
    completed_out = 0;

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT "
        "COUNT(*) AS completed_count, "
        "SUM(CASE WHEN conclusion IS NOT NULL AND conclusion!='' AND conclusion!='success' THEN 1 ELSE 0 END) AS failed_count "
        "FROM ci_workflow_runs "
        "WHERE repo_id=?1 AND status='completed' AND created_at >= datetime('now','-24 hours');";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0.0;
    sqlite3_bind_int(stmt, 1, repo_id);

    int failed = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        completed_out = sqlite3_column_int(stmt, 0);
        failed = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);

    if (completed_out <= 0) return 0.0;
    return (double)failed / (double)completed_out;
}

static int db_ci_consecutive_failures(Db& db, int repo_id, int max_check)
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
        std::string conclusion = c ? (const char*)c : "";
        if (conclusion == "success") break;
        consecutive++;
    }

    sqlite3_finalize(stmt);
    return consecutive;
}

static bool db_has_recent_open_ci_alert(Db& db, int repo_id, int within_hours)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT 1 FROM risk_alert_events "
        "WHERE repo_id=?1 AND alert_type='ci_pipeline_unhealthy' AND status='open' "
        "AND created_at >= datetime('now', ?2) LIMIT 1;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, repo_id);
    const std::string window = "-" + std::to_string(std::max(1, within_hours)) + " hours";
    sqlite3_bind_text(stmt, 2, window.c_str(), -1, SQLITE_TRANSIENT);
    const bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

static bool db_insert_ci_health_alert(Db& db,
                                      int repo_id,
                                      double failure_rate_24h,
                                      int completed_24h,
                                      int consecutive_failures)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    nlohmann::json evidence;
    evidence["failure_rate_24h"] = failure_rate_24h;
    evidence["completed_runs_24h"] = completed_24h;
    evidence["consecutive_failures"] = consecutive_failures;

    const std::string severity = (failure_rate_24h >= 0.7 || consecutive_failures >= 5)
        ? "critical"
        : "warning";
    const std::string suggested_action =
        "Inspect failing workflow jobs, prioritize fixing flaky tests/build steps, and verify required checks on default branch.";
    const std::string evidence_json = evidence.dump();

    const char* sql =
        "INSERT INTO risk_alert_events(" 
        "run_id, repo_id, alert_type, metric_name, window_start, window_end, "
        "current_value, baseline_value, threshold_value, severity, scope_type, scope_id, "
        "suggested_action, status, evidence_json) "
        "VALUES (NULL, ?1, 'ci_pipeline_unhealthy', 'ci_failure_rate_24h', datetime('now','-24 hours'), datetime('now'), "
        "?2, 0.20, 0.40, ?3, 'repo', ?4, ?5, 'open', ?6);";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_double(stmt, 2, failure_rate_24h);
    sqlite3_bind_text(stmt, 3, severity.c_str(), -1, SQLITE_TRANSIENT);
    const std::string scope_id = std::to_string(repo_id);
    sqlite3_bind_text(stmt, 4, scope_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, suggested_action.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, evidence_json.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
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


static bool insert_snapshot_to_db(Db& db, int rid, const std::string& full_name, const GitHubResponse& gh, long long run_id, httplib::Response& res)
{
    RepoSnapshotData data;
    std::string err;
    int http_status = 0;
    //从github的response中解析出snapshot数据，如果失败了就记录错误信息并返回
    //data中存储了stars、forks、open_issues、watchers、pushed_at等信息，以及原始的json字符串，用于插入字符串
    if (!fetch_repo_snapshot_from_github(gh, data, err, http_status)) 
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
        err = "db insert snapshot failed";
        if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
        res.status = 500;
        res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
        return false;
    }
    return true;
}

static bool db_update_sync_cursor(Db& db, int repo_id, const char* col_name, const std::string& cursor)
{
    sqlite3* sdb = db.handle();
    std::string sql = std::string("UPDATE repo_sync_state SET ") + col_name + "=?, updated_at=datetime('now') WHERE repo_id=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, cursor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, repo_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// upsert：避免 UNIQUE(repo_id, number) 冲突导致重复同步失败
static int db_upsert_issue(Db& db, int repo_id, const RepoIssueData& data)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO issues(repo_id, number, state, title, created_at, updated_at, closed_at, comments, author_login, is_pull_request, raw_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11) "
        "ON CONFLICT(repo_id, number) DO UPDATE SET "
        "state=excluded.state, title=excluded.title, created_at=excluded.created_at, updated_at=excluded.updated_at, "
        "closed_at=excluded.closed_at, comments=excluded.comments, author_login=excluded.author_login, "
        "is_pull_request=excluded.is_pull_request, raw_json=excluded.raw_json;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_int(stmt, 2, data.number);
    sqlite3_bind_text(stmt, 3, data.state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, data.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, data.created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, data.updated_at.c_str(), -1, SQLITE_TRANSIENT);
    if (data.closed_at.empty()) sqlite3_bind_null(stmt, 7);
    else sqlite3_bind_text(stmt, 7, data.closed_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, data.comments);
    if (data.author_login.empty()) sqlite3_bind_null(stmt, 9);
    else sqlite3_bind_text(stmt, 9, data.author_login.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 10, data.is_pull_request ? 1 : 0);
    sqlite3_bind_text(stmt, 11, data.raw_json.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 1 : 0;
}

static int db_upsert_pullrequest(Db& db, int repo_id, const RepoPullRequestData& data)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO pull_requests(repo_id, number, state, title, created_at, updated_at, closed_at, merged_at, author_login, raw_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10) "
        "ON CONFLICT(repo_id, number) DO UPDATE SET "
        "state=excluded.state, title=excluded.title, created_at=excluded.created_at, updated_at=excluded.updated_at, "
        "closed_at=excluded.closed_at, merged_at=excluded.merged_at, author_login=excluded.author_login, raw_json=excluded.raw_json;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_int(stmt, 2, data.number);
    sqlite3_bind_text(stmt, 3, data.state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, data.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, data.created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, data.updated_at.c_str(), -1, SQLITE_TRANSIENT);
    if (data.closed_at.empty()) sqlite3_bind_null(stmt, 7);
    else sqlite3_bind_text(stmt, 7, data.closed_at.c_str(), -1, SQLITE_TRANSIENT);
    if (data.merged_at.empty()) sqlite3_bind_null(stmt, 8);
    else sqlite3_bind_text(stmt, 8, data.merged_at.c_str(), -1, SQLITE_TRANSIENT);
    if (data.author_login.empty()) sqlite3_bind_null(stmt, 9);
    else sqlite3_bind_text(stmt, 9, data.author_login.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, data.raw_json.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 1 : 0;
}

static int db_upsert_release(Db& db, int repo_id, const RepoReleaseData& data)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO releases(repo_id, tag_name, name, draft, prerelease, published_at, raw_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) "
        "ON CONFLICT(repo_id, tag_name) DO UPDATE SET "
        "name=excluded.name, "
        "draft=excluded.draft, "
        "prerelease=excluded.prerelease, "
        "published_at=excluded.published_at, "
        "raw_json=excluded.raw_json;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, data.tag_name.c_str(), -1, SQLITE_TRANSIENT);

    if (data.name.empty())
        sqlite3_bind_null(stmt, 3);
    else
        sqlite3_bind_text(stmt, 3, data.name.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_int(stmt, 4, data.draft ? 1 : 0);
    sqlite3_bind_int(stmt, 5, data.prerelease ? 1 : 0);

    if (data.published_at.empty())
        sqlite3_bind_null(stmt, 6);
    else
        sqlite3_bind_text(stmt, 6, data.published_at.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_text(stmt, 7, data.raw_json.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 1 : 0;
}

static int db_upsert_commit(Db& db, int repo_id, const RepoCommitData& data)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO commits(repo_id, sha, author_login, committed_at, raw_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5) "
        "ON CONFLICT(repo_id, sha) DO UPDATE SET "
        "author_login=excluded.author_login, "
        "committed_at=excluded.committed_at, "
        "raw_json=excluded.raw_json;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, data.sha.c_str(), -1, SQLITE_TRANSIENT);

    if (data.author_login.empty())
        sqlite3_bind_null(stmt, 3);
    else
        sqlite3_bind_text(stmt, 3, data.author_login.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_text(stmt, 4, data.committed_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, data.raw_json.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 1 : 0;
}

static int db_upsert_commit_file(Db& db, int repo_id,  const std::string& sha, const CommitFileData& f)
{
    sqlite3* sdb = db.handle();
    if (!sdb) return false;

    // 确保 commits 表中有该 sha（插入或忽略）
    const char* sql_insert_commit = "INSERT OR IGNORE INTO commits(repo_id, sha, author_login, committed_at, raw_json) VALUES (?1, ?2, NULL, ?3, ?4);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql_insert_commit, -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, repo_id);
        sqlite3_bind_text(stmt, 2, sha.c_str(), -1, SQLITE_TRANSIENT);
        if (f.committed_at.empty()) sqlite3_bind_null(stmt, 3);
        else sqlite3_bind_text(stmt, 3, f.committed_at.c_str(), -1, SQLITE_TRANSIENT);
        // raw_json may be empty
        if (f.raw_json.empty()) sqlite3_bind_text(stmt, 4, "", -1, SQLITE_TRANSIENT);
        else sqlite3_bind_text(stmt, 4, f.raw_json.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    if (stmt) { sqlite3_finalize(stmt); stmt = nullptr; }


    // 查询 commit_id（不能直接用 repo_id）
    sqlite3_int64 commit_id = 0;
    const char* sql_select_commit = "SELECT id FROM commits WHERE repo_id=?1 AND sha=?2 LIMIT 1;";
    if (sqlite3_prepare_v2(sdb, sql_select_commit, -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, repo_id);
        sqlite3_bind_text(stmt, 2, sha.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            commit_id = sqlite3_column_int64(stmt, 0);
        }
    }
    if (stmt) { sqlite3_finalize(stmt); stmt = nullptr; }

    if (commit_id == 0) return false; // 没有找到对应的 commit，返回失败
    // upsert 到 commit_files（以 UNIQUE(commit_id, filename) 为准）
    const char* sql_upsert =
        "INSERT INTO commit_files(commit_id, sha, filename, additions, deletions, changes, committed_at, raw_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
        "ON CONFLICT(commit_id, filename) DO UPDATE SET "
        "additions=excluded.additions, deletions=excluded.deletions, changes=excluded.changes, committed_at=excluded.committed_at, raw_json=excluded.raw_json;";

    if (sqlite3_prepare_v2(sdb, sql_upsert, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int64(stmt, 1, commit_id);
    sqlite3_bind_text(stmt, 2, sha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, f.filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, f.additions);
    sqlite3_bind_int(stmt, 5, f.deletions);
    sqlite3_bind_int(stmt, 6, f.changes);
    if (f.committed_at.empty()) sqlite3_bind_null(stmt, 7);
    else sqlite3_bind_text(stmt, 7, f.committed_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, f.raw_json.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

static bool sync_repo_snapshot(Db& db, int rid, const std::string& full_name, const std::string& token,
                              long long run_id, httplib::Response& res)
{
    auto gh_repo = github_get_repo(full_name, token);
    return insert_snapshot_to_db(db, rid, full_name, gh_repo, run_id, res);
}


// ===== 扩展：issues 增量 =====
static bool sync_repo_issues(Db& db, int rid, const std::string& full_name, const std::string& token,
                             long long run_id, httplib::Response& res, int& upserted_out,
                             int page_start, int pages_count,
                             const std::string& since_cursor,
                             std::string& new_cursor_out)
{
    upserted_out = 0;
    new_cursor_out = since_cursor;

    const int per_page = 100;
    int page = std::max(1,page_start);
    int page_end = page_start + pages_count - 1;

    while(1)
    {
        if (page_end > 0 && page > page_end) break;

        //debug 信息
        print_debug_pages(page,"issues");
        auto gh_issues = github_list_issues(full_name, token, "all", per_page, page, since_cursor);

        std::vector<RepoIssueData> items;
        std::string err;
        int http_status = 0;
        if (!parse_repo_issues_from_github(gh_issues, items, err, http_status))
        {
            if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
            res.status = 502;
            res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
            return false;
        }
        
        if (items.empty())// 如果当前页没有 items，说明已经到尾
        {
            break;
        }

        for (const auto& it : items)
        {
            upserted_out += db_upsert_issue(db, rid, it);
            new_cursor_out = max_iso8601(new_cursor_out, it.updated_at);
        }
    
        if ((int)items.size() < per_page)  // 如果本页少于 per_page，说明已经是最后一页
        {
            break;
        }
        page++;
    }
    return true;
}


// ===== 扩展：pulls 增量 增量时走 Search API =====
static bool sync_repo_pulls(Db& db, int rid, const std::string& full_name, const std::string& token,
                            long long run_id, httplib::Response& res, int& upserted_out,
                            int page_start, int pages_count,
                            const std::string& since_cursor,
                            std::string& new_cursor_out)
{
    upserted_out = 0;
    new_cursor_out = since_cursor;

    const int per_page = 100;

    // ===== 增量分支：Search API =====
    if (!since_cursor.empty())
    {
        int page = 1;
        while (1)
        {
            // search query：repo:owner/name is:pr updated:>=<cursor>
            // 注意：cursor 用 ISO8601 Z，可直接拼。这里不加空格以减少 url encode 需求。
            const std::string q = "repo:" + full_name + " is:pr updated:>=" + since_cursor;

            auto gh = github_search_issues_prs(token, q, per_page, page, "updated", "asc");

            std::string err;
            int http_status = 0;

            if (!gh.error.empty() || gh.status < 200 || gh.status >= 300)
            {
                err = !gh.error.empty() ? gh.error : ("github http " + std::to_string(gh.status));
                if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
                res.status = 502;
                res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
                return false;
            }

            nlohmann::json j;
            try { j = nlohmann::json::parse(gh.body); }
            catch (...) 
            {
                err = "parse search results failed";
                if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
                res.status = 502;
                res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
                return false;
            }

            if (!j.contains("items") || !j["items"].is_array()) break;
            const auto& items = j["items"];
            if (items.empty()) break;

            for (const auto& it : items)
            {
                const int number = it.value("number", 0);
                if (number <= 0) continue;

                // 拉 PR 详情补 merged_at
                auto gh_pr = github_get_pull(full_name, token, number);
                RepoPullRequestData pr;
                if (!parse_pull_from_github_json(gh_pr, pr, err, http_status))
                {
                    if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
                    res.status = 502;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
                    return false;
                }

                upserted_out += db_upsert_pullrequest(db, rid, pr);
                new_cursor_out = max_iso8601(new_cursor_out, pr.updated_at);
            }

            print_debug_pages(page, "pulls");
            if ((int)items.size() < per_page) break;
            page++;
        }
        return true;
    }

    //全量分支
    int page = std::max(1, page_start);
    int page_end = page_start + pages_count - 1;

    while (1)
    {
        if (page_end > 0 && page > page_end) break;

        print_debug_pages(page, "pulls");
        auto gh = github_list_pulls(full_name, token, "all", per_page, page, since_cursor);

        std::vector<RepoPullRequestData> items;
        std::string err;
        int http_status = 0;
        if (!parse_repo_pulls_from_github(gh, items, err, http_status))
        {
            if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
            res.status = 502;
            res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
            return false;
        }

        if (items.empty()) break;

        for (const auto& it : items)
        {
            upserted_out += db_upsert_pullrequest(db, rid, it);
            new_cursor_out = max_iso8601(new_cursor_out, it.updated_at);
        }

        if ((int)items.size() < per_page) break;
        page++;
    }
    return true;
}


// ===== 扩展：commits 增量 =====
static bool sync_repo_commits(Db& db, int rid, const std::string& full_name, const std::string& token,
                              long long run_id, httplib::Response& res, int& upserted_out,
                              int page_start, int pages_count,
                              const std::string& since_cursor,
                              std::string& new_since_cursor_out)
{
    upserted_out = 0;
    new_since_cursor_out = since_cursor;

    const int per_page = 100;
    int page = std::max(1, page_start);
    int page_end = page_start + pages_count - 1;

    while (1)
    {
        if (page_end > 0 && page > page_end) break;

        print_debug_pages(page, "commits");
        auto gh_commits = github_list_commits(full_name, token, per_page, page, since_cursor, "");

        std::vector<RepoCommitData> items;
        std::string err;
        int http_status = 0;

        if (!parse_repo_commits_from_github(gh_commits, items, err, http_status))
        {
            if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
            res.status = 502;
            res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
            return false;
        }

        if (items.empty()) break;

        for (const auto& it : items)
        {
            upserted_out += db_upsert_commit(db, rid, it);
            new_since_cursor_out = max_iso8601(new_since_cursor_out, it.committed_at);
        }

        if ((int)items.size() < per_page) break;
        page++;
    }
    return true;
}

// ===== 扩展：releases 增量（按 published_at 推进 cursor） =====
static bool sync_repo_releases(Db& db, int rid, const std::string& full_name, const std::string& token,
                               long long run_id, httplib::Response& res, int& upserted_out,
                               int page_start, int pages_count,
                               const std::string& since_cursor,
                               std::string& new_cursor_out)
{
    upserted_out = 0;
    new_cursor_out = since_cursor;

    const int per_page = 100;
    int page = std::max(1, page_start);
    int page_end = page_start + pages_count - 1;

    while (1)
    {
        if (page_end > 0 && page > page_end) break;

        print_debug_pages(page, "releases");
        auto gh = github_list_releases(full_name, token, per_page, page);

        std::vector<RepoReleaseData> items;
        std::string err;
        int http_status = 0;
        if (!parse_repo_releases_from_github(gh, items, err, http_status))
        {
            if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
            res.status = 502;
            res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
            return false;
        }

        if (items.empty()) break;

        for (const auto& it : items)
        {
            // 注意：draft release 可能 published_at 为空
            upserted_out += db_upsert_release(db, rid, it);
            if (!it.published_at.empty())
                new_cursor_out = max_iso8601(new_cursor_out, it.published_at);
        }

        // 增量：如果 cursor 非空，可以提前截断（假设 API 默认按时间倒序，遇到 <=cursor 说明后面更旧）
        if (!since_cursor.empty())
        {
            bool any_newer = false;
            for (const auto& it : items)
            {
                if (!it.published_at.empty() && it.published_at > since_cursor) { any_newer = true; break; }
            }
            if (!any_newer) break;
        }

        if ((int)items.size() < per_page) break;
        page++;
    }

    return true;
}

// 增量版：每次只处理“本地 commits 里尚未同步 commit_files 的部分”，并可通过 limit 控制批量
static bool sync_commit_files(Db& db, int repo_id, const std::string& full_name, const std::string& token,
                              long long run_id, httplib::Response& res,
                              int& total_files_out,
                              int limit_commits /*新增*/)
{
    total_files_out = 0;

    // 获取本地 commits 列表（按时间降序）
    std::vector<RepoCommitData> commits;
    get_repo_commits(commits, db, repo_id);
    if (commits.empty()) return true;

    int processed_commits = 0;
    int scanned = 0;

    for (const auto& c : commits)
    {
        scanned++;

        // 增量：如果这个 sha 已经有 commit_files 记录，则跳过
        if (db_commit_files_exist_for_sha(db, repo_id, c.sha))
            continue;

        // batch 限制
        if (limit_commits > 0 && processed_commits >= limit_commits)
            break;

        // std::cerr << "sync_commit_files: fetching files for commit " << c.sha
        //         << " (processed=" << processed_commits << ", scanned=" << scanned << ")\n";

        GitHubResponse gh;
        if (!github_get_commit_with_retry(full_name, token, c.sha, gh, 5))// 最多重试五次
        {
            std::cout<<"github_get_commit_with_retry wrongs!"<<std::endl;
            std::string err = !gh.error.empty()
                ? gh.error
                : ("github status " + std::to_string(gh.status));

            if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
            res.status = 502;
            res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
            return false;
        }

        std::vector<CommitFileData> files;
        std::string err;
        int http_status = 0;

        if (!parse_commit_files_from_github(gh, files, err, http_status))
        {
            std::cout<<"parse_commit_files_from_github wrongs!"<<std::endl;
            if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
            res.status = 502;
            res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
            return false;
        }

        for (const auto& f : files)
        {
            total_files_out++;
            db_upsert_commit_file(db, repo_id, c.sha, f);
        }

        processed_commits++;
    }

    return true;
}

static bool db_repo_intro_exists(Db &db, int repo_id)
{
    sqlite3* sdb = db.handle();
    if(!sdb) return false;
     
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT intro_text FROM repos WHERE id=?1;";
    if (sqlite3_prepare_v2(sdb,sql,-1,&stmt,nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt,1,repo_id);

    bool exists = false;
    if(sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char* t = sqlite3_column_text(stmt,0);
        std::string v = t ? (const char*)t: "";
        exists = !v.empty();
    }
    sqlite3_finalize(stmt);
    return exists;
}

static bool db_update_repo_intro(Db &db, int repo_id, const std::string& intro)
{
    sqlite3 *sdb =db.handle();
    if(!sdb) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE repos SET intro_text=?1, intro_updated_at=datetime('now') WHERE id=?2;";
    int prc = sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr);
    if (prc != SQLITE_OK)
    {
        std::cerr << "[sync][warn] update intro prepare failed: " << sqlite3_errmsg(sdb) << "\n";
        return false;
    }

    sqlite3_bind_text(stmt,1,intro.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,2,repo_id);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        std::cerr << "[sync][warn] update intro step failed: rc=" << rc
                  << " msg=" << sqlite3_errmsg(sdb) << "\n";
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

static std::string build_repo_intro_prompt(const std::string& full_name)
{
    // 纯文本、100-200字、不要Markdown
    std::string p;
    p += "请为 GitHub 仓库生成一段【中文纯文本介绍】，100-200字。\n";
    p += "目标：说明该仓库是做什么的、主要用途/适用场景。\n";
    p += "约束：不要Markdown，不要列表符号，不要带代码块；不要杜撰具体数字或功能细节，证据不足请用“可能/推测/通常”措辞。\n";
    p += "仓库全名：";
    p += full_name;
    return p;
}


static void generate_and_store_repo_intro(Db& db, int repo_id, const std::string& full_name, std::string& intro_warning)
{
    // 生成 repo intro（基于 snapshot 和最新的几个 issues/pulls/commits）
    try
    {
        //仅第一次同步时生成 intro(降低 后续同步的时间成本)
        if(!full_name.empty() && !db_repo_intro_exists(db, repo_id))
        {
            auto ai = ask_question(db, repo_id, build_repo_intro_prompt(full_name));
            std::string intro = ai.answer;

            intro = util::trim(intro);
            std::cerr<<"the intro generated success!"<<std::endl;

            if(!intro.empty())
            {
                std::cerr<<intro<<std::endl;
                if(!db_update_repo_intro(db, repo_id, intro))
                {
                    intro_warning = "failed to update repo intro to db";
                    std::cerr << "[sync][warn] " << intro_warning << "\n";
                }
            }
            else
            {
                intro_warning = "ai intro empty";
                std::cerr<<"the intro generated by ai is empty!"<<std::endl;
            }
        }
    }
    catch(const std::exception& ex)
    {
        intro_warning = std::string("exception when generating repo intro: ") + ex.what();
        std::cerr << "[sync][warn] " << intro_warning << "\n";
    }
    catch(...)
    {
        intro_warning = "unknown exception when generating repo intro";
        std::cerr << "[sync][warn] " << intro_warning << "\n";
    }
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

    //增量模式
    const std::string mode = req.has_param("mode") ? req.get_param_value("mode") : "incremental";
    const bool incremental = (mode != "full");

    // full 模式仍保留页参数；incremental 模式下忽略 page_start/page_count，避免误用漏增量
    int issues_page_start = get_int_query(req, "issues_page_start", 1);
    int issues_pages_count = get_int_query(req, "issues_pages_count", 1);

    int pulls_page_start = get_int_query(req, "pulls_page_start", 1);
    int pulls_pages_count = get_int_query(req, "pulls_pages_count", 1);

    int commits_page_start = get_int_query(req, "commits_page_start", 1);
    int commits_pages_count = get_int_query(req, "commits_pages_count", 1);

    int releases_page_start = get_int_query(req, "releases_page_start", 1);
    int releases_pages_count = get_int_query(req, "releases_pages_count", 1);

    int ci_pages_count = get_int_query(req, "ci_pages_count", 2);


    //防止增量模式下其他参数产生污染
    if (incremental)
    {
        issues_page_start = 1;
        pulls_page_start = 1;
        commits_page_start = 1;
        releases_page_start = 1;

        // 作为安全上限：防止无限分页（也可设更大或直接不限制）
        issues_pages_count = 50;
        pulls_pages_count = 50;
        commits_pages_count = 50;
        releases_pages_count = 50;
        ci_pages_count = 3;
    }

    std::string issues_cursor, pulls_cursor, commits_cursor, releases_cursor;
    if (incremental)
    {
        if (!db_get_sync_state(db, rid, issues_cursor, pulls_cursor, commits_cursor, releases_cursor))
        {
            res.status = 500;
            res.set_content(R"({"error":"failed to read repo_sync_state"})", "application/json");
            return;
        }
    }

    const long long run_id = db_insert_sync_run(db, rid, "error", "started"); // 先占位，后面会更新
    std::string snapshot_warn;   // ✅ snapshot 失败不致命：记录 warning 继续跑
    {
        httplib::Response tmp;
        tmp.status = 200;
        if (!sync_repo_snapshot(db, rid, full_name, token, run_id, tmp))
        {
            // tmp.body 形如 {"error":"..."}，这里直接取出来当 warning
            snapshot_warn = tmp.body.empty() ? "snapshot failed" : tmp.body;
            std::cerr << "[sync][warn] snapshot failed: " << snapshot_warn << "\n";
            // 不 return，继续同步其它数据
        }
    }

    int issues_upserted = 0, pulls_upserted = 0, commits_upserted = 0, releases_upserted = 0, ci_runs_upserted = 0;
    std::string new_issues_cursor = issues_cursor;
    std::string new_pulls_cursor = pulls_cursor;
    std::string new_commits_cursor = commits_cursor;
    std::string new_releases_cursor = releases_cursor;

    if (!sync_repo_issues(db, rid, full_name, token, run_id, res, issues_upserted,
                          issues_page_start, issues_pages_count,
                          incremental ? issues_cursor : std::string(""),
                          new_issues_cursor)) return;

    if (!sync_repo_pulls(db, rid, full_name, token, run_id, res, pulls_upserted,
                         pulls_page_start, pulls_pages_count,
                         incremental ? pulls_cursor : std::string(""),
                         new_pulls_cursor)) return;

    if (!sync_repo_commits(db, rid, full_name, token, run_id, res, commits_upserted,
                           commits_page_start, commits_pages_count,
                           incremental ? commits_cursor : std::string(""),
                           new_commits_cursor)) return;

    if (!sync_repo_releases(db, rid, full_name, token, run_id, res, releases_upserted,
                            releases_page_start, releases_pages_count,
                            incremental ? releases_cursor : std::string(""),
                            new_releases_cursor)) return;

    if (!sync_repo_ci_workflow_runs(db, rid, full_name, token, run_id, res,
                                    ci_runs_upserted, ci_pages_count)) return;

    int ci_completed_24h = 0;
    const double ci_failure_rate_24h = db_ci_failure_rate_24h(db, rid, ci_completed_24h);
    const int ci_consecutive_failures = db_ci_consecutive_failures(db, rid, 20);

    int ci_alerts_created = 0;
    const bool ci_unhealthy = (ci_completed_24h >= 5 && ci_failure_rate_24h >= 0.40)
        || (ci_consecutive_failures >= 3);
    if (ci_unhealthy && !db_has_recent_open_ci_alert(db, rid, 12)) {
        if (db_insert_ci_health_alert(db, rid, ci_failure_rate_24h, ci_completed_24h, ci_consecutive_failures)) {
            ci_alerts_created++;
        }
    }

    // 只有全部成功才推进 cursor
    if (incremental)
    {
        if (new_issues_cursor != issues_cursor) db_update_sync_cursor(db, rid, "issues_updated_cursor", new_issues_cursor);
        if (new_pulls_cursor != pulls_cursor) db_update_sync_cursor(db, rid, "pulls_updated_cursor", new_pulls_cursor);
        if (new_commits_cursor != commits_cursor) db_update_sync_cursor(db, rid, "commits_since_cursor", new_commits_cursor);
        if (new_releases_cursor != releases_cursor) db_update_sync_cursor(db, rid, "releases_cursor", new_releases_cursor);
    }

    // 同步成功后立即重建知识库索引，保证前端点击同步后可直接进行 RAG 查询。
    const BuildIndexResult kb_build = build_knowledge_index(db, rid);

    //全部成功后顺带生成一下仓库介绍intro(一定要放在最后，避免前面任何一步失败都不生成 intro；intro 生成失败也不影响前面数据的同步结果)
    std::string intro_warning;
    generate_and_store_repo_intro(db, rid, full_name, intro_warning);


    if (run_id > 0) db_finish_sync_run(db, run_id, "ok", "");

     // ✅ 返回 warning 给前端
    nlohmann::json out;
    out["ok"] = true;
    out["repo_id"] = rid;
    out["issues_upserted"] = issues_upserted;
    out["pulls_upserted"] = pulls_upserted;
    out["commits_upserted"] = commits_upserted;
    out["releases_upserted"] = releases_upserted;
    out["ci_runs_upserted"] = ci_runs_upserted;
    out["kb_issues_indexed"] = kb_build.issues_indexed;
    out["kb_pulls_indexed"] = kb_build.pulls_indexed;
    out["kb_commits_indexed"] = kb_build.commits_indexed;
    out["kb_releases_indexed"] = kb_build.releases_indexed;
    out["kb_total_indexed"] = kb_build.total();

    nlohmann::json warnings = nlohmann::json::array();
    if (!intro_warning.empty()) warnings.push_back(std::string("repo intro: ") + intro_warning);
    if (!snapshot_warn.empty()) warnings.push_back(std::string("snapshot skipped: ") + snapshot_warn);
    if (!warnings.empty()) out["warnings"] = warnings;

    res.set_content(out.dump(), "application/json; charset=utf-8");
}


static void post_repo_sync_commit_files_handler(Db& db, const httplib::Request& req, httplib::Response& res)
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

    // 新增：单次最多处理多少个 commit (默认30) (避免一次跑太多触发 503/限流)
    const int limit_commits = get_int_query(req, "limit", 30);

    const long long run_id = db_insert_sync_run(db, rid, "error", "started");
    int total_files = 0;

    if (!sync_commit_files(db, rid, full_name, token, run_id, res, total_files, limit_commits)) 
    {
        // sync_commit_files 会在失败时设置 res + db_finish_sync_run
        std::cerr<< "sync_commit_files failed for repo_id=" << rid << "\n";
        return;
    }

    if (run_id > 0) db_finish_sync_run(db, run_id, "ok", "");

    std::string out = std::string("{\"ok\":true,\"repo_id\":") + std::to_string(rid) +
                      ",\"limit_commits\":" + std::to_string(limit_commits) +
                      ",\"total_files_processed\":" + std::to_string(total_files) + "}";
    res.set_content(out, "application/json");
}


void register_post_routes(httplib::Server& app, Db& db)
{
    app.Post("/api/repos",
             [&db](const httplib::Request& req, httplib::Response& res)
             {
                 post_repos_handler(db, req, res); 
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
                     res.set_content(std::string("{\"error\":\"") +
                                         util::json_escape(e.what()) + "\"}",
                                     "application/json");
                 }
                 catch (...)
                 {
                     res.status = 500;
                     res.set_content(R"({"error":"unknown server error"})",
                                     "application/json");
                 }
             });

    app.Post(R"(/api/repos/(\d+)/sync/commit_files)",
             [&db](const httplib::Request& req, httplib::Response& res)
             {
                 try
                 {
                     post_repo_sync_commit_files_handler(db, req, res);
                 }
                 catch (const std::exception& e)
                 {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") +
                                         util::json_escape(e.what()) + "\"}",
                                     "application/json");
                 }
                 catch (...)
                 {
                     res.status = 500;
                     res.set_content(R"({"error":"unknown server error"})",
                                     "application/json");
                 }
             });

}