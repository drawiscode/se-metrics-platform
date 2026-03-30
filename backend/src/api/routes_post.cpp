#include "routes.h"
#include "common/util.h"
#include <sqlite3.h>
#include "repo_metrics/github_client.h"

static void print_deubg_pages(int pages,std::string prefix)
{
    if(pages<=0) 
    {
        std::cerr <<"error:the "<<prefix<< " pages is "<<pages<<std::endl;
        return;
    }
    std::cerr << prefix << "pages: " << pages << std::endl;
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


static bool sync_repo_issues(Db& db, int rid, const std::string& full_name, const std::string& token,
                            long long run_id, httplib::Response& res, int& upserted_out,
                            int page_start, int pages_count)
{
    upserted_out = 0;
    const int per_page = 100;
    int page = std::max(1,page_start);
    int page_end = page_start + pages_count - 1;

    while(1)
    {
        if(page > page_end) break;

        //debug 信息
        print_deubg_pages(page,"issues");

        auto gh_issues = github_list_issues(full_name, token, "all", per_page, page, "");
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
        
        if (items.empty())//// 如果当前页没有 items，说明已经到尾
        {
            break;
        }

        for (const auto& it : items)
        {
            // issues 表只存真正的 issue，PR 交给 pulls 表
            if (it.is_pull_request) continue;
            upserted_out += db_upsert_issue(db, rid, it);
        }
    
        if ((int)items.size() < per_page)  // 如果本页少于 per_page，说明已经是最后一页
        {
            break;
        }
        page++;
    }
    return true;
}


static bool sync_repo_pulls(Db& db, int rid, const std::string& full_name, const std::string& token,
                           long long run_id, httplib::Response& res, int& upserted_out,
                           int page_start, int pages_count)
{
    upserted_out = 0;
    const int per_page = 100;
    int page = std::max(1,page_start);
    int page_end = page_start + pages_count - 1;

    while(1)
    {
        if( page > page_end) break;

        //debug 信息
        print_deubg_pages(page,"pulls");

        auto gh_pulls = github_list_pulls(full_name, token, "all", per_page, page, "");
        std::vector<RepoPullRequestData> items;
        std::string err;
        int http_status = 0;

        if (!parse_repo_pulls_from_github(gh_pulls, items, err, http_status))
        {
            if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
            res.status = 502;
            res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
            return false;
        }

        if (items.empty())//// 如果当前页没有 items，说明已经到尾
        {
            break;
        }

        for (const auto& it : items)
        {
            upserted_out += db_upsert_pullrequest(db, rid, it);
        }

        if ((int)items.size() < per_page)  // 如果本页少于 per_page，说明已经是最后一页
        {
            break;
        }
        page++;
    }
    return true;
}


static bool sync_repo_commits(Db& db,int rid, const std::string& full_name, const std::string& token,
                            long long run_id, httplib::Response& res,int& upserted_out,
                            int page_start, int pages_count)
{
    upserted_out = 0;
    const int per_page = 100;
    int page = std::max(1,page_start);
    int page_end = page_start + pages_count - 1;

    while(1)
    {
        //debug 信息
       

        if(page_end > 0 && page > page_end) break;

         print_deubg_pages(page,"commits");
        auto gh_commits = github_list_commits(full_name, token, 100, 1, "", "");
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

        if (items.empty())//// 如果当前页没有 items，说明已经到尾
        {
            break;
        }

        for (const auto& it : items)
        {
            upserted_out += db_upsert_commit(db, rid, it);
        }

        if ((int)items.size() < per_page)  // 如果本页少于 per_page，说明已经是最后一页
        {
            break;
        }
        page++;
    }
    return true;
}

static bool sync_repo_releases(Db& db,int rid, const std::string& full_name, const std::string& token,
                            long long run_id, httplib::Response& res,int& upserted_out,
                            int page_start, int pages_count)
{
    upserted_out = 0;
    const int per_page = 100;
    int page = std::max(1,page_start);
    int page_end = page_start + pages_count - 1;

    while(1)
    {
        //debug 信息
      

        if(page_end > 0 && page > page_end) break;

          print_deubg_pages(page,"releases");
        auto gh_releases = github_list_releases(full_name, token, 100, 1);
        std::vector<RepoReleaseData> items;
        std::string err;
        int http_status = 0;

        if (!parse_repo_releases_from_github(gh_releases, items, err, http_status))
        {
            if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
            res.status = 502;
            res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
            return false;
        }

        if (items.empty())//// 如果当前页没有 items，说明已经到尾
        {
            break;
        }

        for (const auto& it : items)
        {
            upserted_out += db_upsert_release(db, rid, it);
        }

        if ((int)items.size() < per_page)  // 如果本页少于 per_page，说明已经是最后一页
        {
            break;
        }
        page++;
    }

    return true;
}


// 新增：按 commits 列表同步 commit files 到 commit_files 表
// 返回 true 表示过程完成（可能部分失败），total_files_out 为尝试写入的文件数量
static bool sync_commit_files(Db& db, int repo_id, const std::string& full_name, const std::string& token,
                              long long run_id, httplib::Response& res, int& total_files_out)
{
    total_files_out = 0;
    // 获取本地 commits 列表（按时间降序）
    std::vector<RepoCommitData> commits;
    get_repo_commits(commits, db, repo_id);
    if (commits.empty()) return true;

    int count=0;

    for (const auto& c : commits)
    {
        // 请求 GitHub commit 详情（包含 files）

        std::cerr<< count << ":sync_commit_files: fetching files for commit " << c.sha << "\n";

        auto gh = github_get_commit_with_file(full_name, token, c.sha);
        std::vector<CommitFileData> files;
        std::string err;
        int http_status = 0;

        if(!parse_commit_files_from_github(gh, files, err, http_status))
        {
            if (run_id > 0) db_finish_sync_run(db, run_id, "error", err);
            res.status = 502;
            res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", "application/json");
            return false;
        }

        for(const auto& f : files)
        {
            total_files_out++;
            db_upsert_commit_file(db, repo_id, c.sha, f);
        }
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
    // Judge_GitHub_Token(token);

    int issues_page_start = get_int_query(req, "issues_page_start", 1);
    int issues_pages_count = get_int_query(req, "issues_pages_count", 1);

    int pulls_page_start = get_int_query(req, "pulls_page_start", 1);
    int pulls_pages_count = get_int_query(req, "pulls_pages_count", 1);

    int commits_page_start = get_int_query(req, "commits_page_start", 1);
    int commits_pages_count = get_int_query(req, "commits_pages_count", 1);

    int releases_page_start = get_int_query(req, "releases_page_start", 1);
    int releases_pages_count = get_int_query(req, "releases_pages_count", 1);

    const long long run_id = db_insert_sync_run(db, rid, "error", "started"); // 先占位，后面会更新
 
    if (!sync_repo_snapshot(db, rid, full_name, token, run_id, res)) return;

    int issues_upserted = 0;
    if (!sync_repo_issues(db, rid, full_name, token, run_id, res, issues_upserted,
                        issues_page_start, issues_pages_count)) return;

    int pulls_upserted = 0;
    if (!sync_repo_pulls(db, rid, full_name, token, run_id, res, pulls_upserted,
                        pulls_page_start, pulls_pages_count)) return;
   
    int commits_upserted = 0;
    if(!sync_repo_commits(db, rid, full_name, token, run_id, res, commits_upserted,
                        commits_page_start, commits_pages_count)) return;

    int releases_upserted = 0;
    if(!sync_repo_releases(db, rid, full_name, token, run_id, res, releases_upserted,
                        releases_page_start, releases_pages_count)) return;


    if (run_id > 0) db_finish_sync_run(db, run_id, "ok", "");

     res.set_content(
        std::string("{\"ok\":true,\"repo_id\":") + std::to_string(rid) +
        ",\"issues_upserted\":" + std::to_string(issues_upserted) +
        ",\"pulls_upserted\":" + std::to_string(pulls_upserted) +
        ",\"commits_upserted\":" + std::to_string(commits_upserted) +
        ",\"releases_upserted\":" + std::to_string(releases_upserted) +
        "}",
        "application/json");
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

    const long long run_id = db_insert_sync_run(db, rid, "error", "started");
    int total_files = 0;
    if (!sync_commit_files(db, rid, full_name, token, run_id, res, total_files)) {
        // sync_commit_files 会在失败时设置 res + db_finish_sync_run
        std::cerr<< "sync_commit_files failed for repo_id=" << rid << "\n";
        return;
    }

    if (run_id > 0) db_finish_sync_run(db, run_id, "ok", "");

    std::string out = std::string("{\"ok\":true,\"repo_id\":") + std::to_string(rid) +
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