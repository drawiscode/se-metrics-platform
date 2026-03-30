#include "routes.h"
#include <sqlite3.h>

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

// 删除 repo 下所有 issues
static void delete_repo_issues_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM issues WHERE repo_id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500; res.set_content(R"({"error":"db prepare failed"})", "application/json"); return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) { res.status = 500; res.set_content(R"({"error":"db step failed"})", "application/json"); return; }
    if (sqlite3_changes(sdb) == 0) { res.status = 404; res.set_content(R"({"error":"no issues found"})", "application/json"); return; }
    res.set_content(R"({"ok":true})", "application/json");
}

// 删除单个 issue by number
static void delete_repo_issue_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    int number = std::stoi(req.matches[2]);
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM issues WHERE repo_id=?1 AND number=?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500; res.set_content(R"({"error":"db prepare failed"})", "application/json"); return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_int(stmt, 2, number);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) { res.status = 500; res.set_content(R"({"error":"db step failed"})", "application/json"); return; }
    if (sqlite3_changes(sdb) == 0) { res.status = 404; res.set_content(R"({"error":"issue not found"})", "application/json"); return; }
    res.set_content(R"({"ok":true})", "application/json");
}

// 删除 repo 下所有 pull requests
static void delete_repo_pulls_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM pull_requests WHERE repo_id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500; res.set_content(R"({"error":"db prepare failed"})", "application/json"); return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) { res.status = 500; res.set_content(R"({"error":"db step failed"})", "application/json"); return; }
    if (sqlite3_changes(sdb) == 0) { res.status = 404; res.set_content(R"({"error":"no pulls found"})", "application/json"); return; }
    res.set_content(R"({"ok":true})", "application/json");
}

// 删除单个 pull by number
static void delete_repo_pull_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    int number = std::stoi(req.matches[2]);
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM pull_requests WHERE repo_id=?1 AND number=?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500; res.set_content(R"({"error":"db prepare failed"})", "application/json"); return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_int(stmt, 2, number);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) { res.status = 500; res.set_content(R"({"error":"db step failed"})", "application/json"); return; }
    if (sqlite3_changes(sdb) == 0) { res.status = 404; res.set_content(R"({"error":"pull request not found"})", "application/json"); return; }
    res.set_content(R"({"ok":true})", "application/json");
}

// 删除 repo 下所有 commits（会触发外键级联删除 commit_files）
static void delete_repo_commits_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM commits WHERE repo_id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500; res.set_content(R"({"error":"db prepare failed"})", "application/json"); return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) { res.status = 500; res.set_content(R"({"error":"db step failed"})", "application/json"); return; }
    if (sqlite3_changes(sdb) == 0) { res.status = 404; res.set_content(R"({"error":"no commits found"})", "application/json"); return; }
    res.set_content(R"({"ok":true})", "application/json");
}

// 删除单个 commit by sha
static void delete_repo_commit_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    std::string sha = req.matches[2];
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM commits WHERE repo_id=?1 AND sha=?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500; res.set_content(R"({"error":"db prepare failed"})", "application/json"); return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_text(stmt, 2, sha.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) { res.status = 500; res.set_content(R"({"error":"db step failed"})", "application/json"); return; }
    if (sqlite3_changes(sdb) == 0) { res.status = 404; res.set_content(R"({"error":"commit not found"})", "application/json"); return; }
    res.set_content(R"({"ok":true})", "application/json");
}

// 删除 repo 下所有 releases
static void delete_repo_releases_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM releases WHERE repo_id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500; res.set_content(R"({"error":"db prepare failed"})", "application/json"); return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) { res.status = 500; res.set_content(R"({"error":"db step failed"})", "application/json"); return; }
    if (sqlite3_changes(sdb) == 0) { res.status = 404; res.set_content(R"({"error":"no releases found"})", "application/json"); return; }
    res.set_content(R"({"ok":true})", "application/json");
}

// 删除单个 release by tag_name
static void delete_repo_release_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    std::string tag = req.matches[2];
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM releases WHERE repo_id=?1 AND tag_name=?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        res.status = 500; res.set_content(R"({"error":"db prepare failed"})", "application/json"); return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_text(stmt, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) { res.status = 500; res.set_content(R"({"error":"db step failed"})", "application/json"); return; }
    if (sqlite3_changes(sdb) == 0) { res.status = 404; res.set_content(R"({"error":"release not found"})", "application/json"); return; }
    res.set_content(R"({"ok":true})", "application/json");
}


void register_delete_routes(httplib::Server& app, Db& db)
{
    app.Delete(R"(/api/repos/(\d+))",
               [&db](const httplib::Request& req, httplib::Response& res)
               {
                   delete_repo_handler(db, req, res);
               });

    // issues
    app.Delete(R"(/api/repos/(\d+)/issues)",
               [&db](const httplib::Request& req, httplib::Response& res)
               {
                   delete_repo_issues_handler(db, req, res);
               });
    app.Delete(R"(/api/repos/(\d+)/issues/(\d+))",
               [&db](const httplib::Request& req, httplib::Response& res)
               {
                   delete_repo_issue_handler(db, req, res);
               });

    // pulls
    app.Delete(R"(/api/repos/(\d+)/pulls)",
               [&db](const httplib::Request& req, httplib::Response& res)
               {
                   delete_repo_pulls_handler(db, req, res);
               });
    app.Delete(R"(/api/repos/(\d+)/pulls/(\d+))",
               [&db](const httplib::Request& req, httplib::Response& res)
               {
                   delete_repo_pull_handler(db, req, res);
               });

    // commits
    app.Delete(R"(/api/repos/(\d+)/commits)",
               [&db](const httplib::Request& req, httplib::Response& res)
               {
                   delete_repo_commits_handler(db, req, res);
               });
    app.Delete(R"(/api/repos/(\d+)/commits/([0-9a-fA-F]+))",
               [&db](const httplib::Request& req, httplib::Response& res)
               {
                   delete_repo_commit_handler(db, req, res);
               });

    // releases
    app.Delete(R"(/api/repos/(\d+)/releases)",
               [&db](const httplib::Request& req, httplib::Response& res)
               {
                   delete_repo_releases_handler(db, req, res);
               });
    app.Delete(R"(/api/repos/(\d+)/releases/(.+))",
               [&db](const httplib::Request& req, httplib::Response& res)
               {
                   delete_repo_release_handler(db, req, res);
               });
}