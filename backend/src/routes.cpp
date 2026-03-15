
// filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/src/routes.cpp
#include "routes.h"

#include <string>
#include <sqlite3.h>

// 复用你 main.cpp 里的 json_escape（这里先声明，定义仍在 main.cpp）
// 更工程化的做法是把 json_escape 也挪到 util.h/cpp，但这步先最小拆分。
static std::string json_escape(const std::string& s);

static void health_handler(const httplib::Request&, httplib::Response& res)
{
    res.set_content(R"({"ok":true})", "application/json");
}

static void post_projects_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res)
{
    auto name = req.get_param_value("name");
    if (name.empty())
    {
        res.status = 400;
        res.set_content(R"({"error":"missing name"})", "application/json");
        return;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO projects(name) VALUES (?1);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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

    long long id = sqlite3_last_insert_rowid(db);
    res.set_content(std::string("{\"id\":") + std::to_string(id) + ",\"name\":\"" + json_escape(name) + "\"}", "application/json");
}

static void post_repos_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res)
{
    auto full_name = req.get_param_value("full_name");
    if (full_name.empty())
    {
        res.status = 400;
        res.set_content(R"({"error":"missing full_name"})", "application/json");
        return;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO repos(full_name) VALUES (?1);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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

    long long id = sqlite3_last_insert_rowid(db);
    res.set_content(std::string("{\"id\":") + std::to_string(id) + ",\"full_name\":\"" + json_escape(full_name) + "\"}", "application/json");
}

static void post_project_repos_handler(sqlite3 *db,const httplib::Request& req,httplib::Response& res)
{
    int project_id = std::stoi(req.matches[1]);
    int repo_id = std::stoi(req.matches[2]);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO project_repos(project_id, repo_id) VALUES (?1, ?2);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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

static void get_repos_handler(sqlite3* db, const httplib::Request&, httplib::Response& res)
{
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, full_name, enabled FROM repos ORDER BY id DESC LIMIT 200;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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
            + ",\"full_name\":\"" + json_escape(fn ? (const char*)fn : "") + "\""
            + ",\"enabled\":" + std::to_string(enabled)
            + "}";
    }
    out += "]}";

    sqlite3_finalize(stmt);
    res.set_content(out, "application/json");
}

static void get_projects_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res)
{
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, name FROM projects ORDER BY id DESC LIMIT 200;";
     if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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
                    + ",\"name\":\"" + json_escape(nm ? (const char*)nm : "") + "\""
                    + "}";
            }
            out += "]}";
            sqlite3_finalize(stmt);
            res.set_content(out, "application/json");
}

static void get_project_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res)
{
    int project_id = std::stoi(req.matches[1]);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, name FROM projects WHERE id = ?1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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
        + ",\"name\":\"" + json_escape(nm ? (const char*)nm : "") + "\""
        + "}";

    sqlite3_finalize(stmt);
    res.set_content(out, "application/json");
}

static void put_project_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res)
{
    int pid = std::stoi(req.matches[1]);
    auto name = req.get_param_value("name");
    if (name.empty())
    {
        res.status = 400;
        res.set_content(R"({"error":"missing name"})", "application/json");
        return;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE projects SET name=?1 WHERE id=?2;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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

    if (sqlite3_changes(db) == 0)
    {
        res.status = 404;
        res.set_content(R"({"error":"project not found"})", "application/json");
        return;
    }

    res.set_content(std::string("{\"id\":") + std::to_string(pid) + ",\"name\":\"" + json_escape(name) + "\"}", "application/json");
}

static void delete_project_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res)
{
    int pid = std::stoi(req.matches[1]);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM projects WHERE id=?1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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

    if (sqlite3_changes(db) == 0)
    {
        res.status = 404;
        res.set_content(R"({"error":"project not found"})", "application/json");
        return;
    }

    res.set_content(R"({"ok":true})", "application/json");
}

static void get_project_repos_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res)
{
    int pid = std::stoi(req.matches[1]);

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT r.id, r.full_name, r.enabled "
        "FROM project_repos pr "
        "JOIN repos r ON r.id = pr.repo_id "
        "WHERE pr.project_id = ?1 "
        "ORDER BY r.id DESC LIMIT 200;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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
            + ",\"full_name\":\"" + json_escape(fn ? (const char*)fn : "") + "\""
            + ",\"enabled\":" + std::to_string(enabled)
            + "}";
    }
    out += "]}";
    sqlite3_finalize(stmt);
    res.set_content(out, "application/json");
}

static void delete_project_repo_link_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res)
{
     int pid = std::stoi(req.matches[1]);
    int rid = std::stoi(req.matches[2]);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM project_repos WHERE project_id=?1 AND repo_id=?2;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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

    if (sqlite3_changes(db) == 0)
    {
        res.status = 404;
        res.set_content(R"({"error":"link not found"})", "application/json");
        return;
    }

    res.set_content(R"({"ok":true})", "application/json");
}

static void get_repo_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, full_name, enabled FROM repos WHERE id=?1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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
        + ",\"full_name\":\"" + json_escape(fn ? (const char*)fn : "") + "\""
        + ",\"enabled\":" + std::to_string(enabled)
        + "}";
    res.set_content(out, "application/json");
}

static void put_repo_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res)
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

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE repos SET enabled=?1 WHERE id=?2;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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

    if (sqlite3_changes(db) == 0)
    {
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", "application/json");
        return;
    }

    res.set_content(std::string("{\"ok\":true,\"id\":") + std::to_string(rid) + ",\"enabled\":" + std::to_string(enabled) + "}", "application/json");
}

static void delete_repo_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM repos WHERE id=?1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
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

    if (sqlite3_changes(db) == 0)
    {
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", "application/json");
        return;
    }

    res.set_content(R"({"ok":true})", "application/json");
}


void register_routes(httplib::Server& app, sqlite3* db)
{
    // 使用函数指针注册路由
    app.Get("/api/health", health_handler);

    app.Post("/api/projects",
             [db](const httplib::Request& req, httplib::Response& res)
             {
                 post_projects_handler(db, req, res);
             });

    app.Post("/api/repos",
             [db](const httplib::Request& req, httplib::Response& res)
             {
                 post_repos_handler(db, req, res);
             });

    app.Post(R"(/api/projects/(\d+)/repos/(\d+))",
             [db](const httplib::Request& req, httplib::Response& res)
             {
                 post_project_repos_handler(db, req, res);
             });

    app.Get("/api/repos",
            [db](const httplib::Request& req, httplib::Response& res)
            {
                get_repos_handler(db, req, res);
            });

    app.Get("/api/projects",
            [db](const httplib::Request& req, httplib::Response& res)
            {
                get_projects_handler(db, req, res);
            });

    // GET /api/projects/{pid}
    app.Get(R"(/api/projects/(\d+))",
            [db](const httplib::Request& req, httplib::Response& res)
            {
                get_project_handler(db, req, res);
            });

    // PUT /api/projects/{pid}?name=xxx (update project name)
    app.Put(R"(/api/projects/(\d+))",
            [db](const httplib::Request& req, httplib::Response& res)
            {
                put_project_handler(db, req, res);
            });

    // DELETE /api/projects/{pid}
    app.Delete(R"(/api/projects/(\d+))",
            [db](const httplib::Request& req, httplib::Response& res)
            {
                delete_project_handler(db, req, res);
            });

    // GET /api/projects/{pid}/repos (list repos linked to a project)
    app.Get(R"(/api/projects/(\d+)/repos)",
            [db](const httplib::Request& req, httplib::Response& res)
            {
                get_project_repos_handler(db, req, res);
            });

    // DELETE /api/projects/{pid}/repos/{rid} (unlink)
    app.Delete(R"(/api/projects/(\d+)/repos/(\d+))",
            [db](const httplib::Request& req, httplib::Response& res)
            {
                delete_project_repo_link_handler(db, req, res);
            });

    // GET /api/repos/{rid}
    app.Get(R"(/api/repos/(\d+))",
            [db](const httplib::Request& req, httplib::Response& res)
            {
                get_repo_handler(db, req, res);
            });

    // PUT /api/repos/{rid}?enabled=0|1 (toggle enabled)
    app.Put(R"(/api/repos/(\d+))",
            [db](const httplib::Request& req, httplib::Response& res)
            {
                put_repo_handler(db, req, res);
            });

    // DELETE /api/repos/{rid}
    app.Delete(R"(/api/repos/(\d+))",
            [db](const httplib::Request& req, httplib::Response& res)
            {
                delete_repo_handler(db, req, res);
            });
}

// 从 main.cpp 里把 json_escape 的实现搬过来会更干净。
// 这里为了“先拆路由”，给一个最小实现占位：你应删除这段并把 json_escape 挪到 shared util。
static std::string json_escape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '\"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default: o += c; break;
        }
    }
    return o;
}