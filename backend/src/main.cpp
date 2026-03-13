// filepath: /e:/大三下/软件工程综合实验/lab/se-metrics-platform/backend/src/main.cpp

/*编译运行方式*/

/*生成工程（CMake Configure）*/
//cmake --help
//cmake -S . -B build -G "Visual Studio 18 2026" -A x64

/*编译生成 exe（CMake Build）*/
//cmake --build build --config Debug
//


#include <iostream>
#include <string>
#include <cstdlib>

#include "httplib.h"
#include <sqlite3.h>

static std::string get_env(const char* name, const std::string& def) 
{
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : def;
}

static void exec_sql(sqlite3* db, const std::string& sql) 
{
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) 
    {
        std::string msg = err ? err : "unknown sqlite error";
        sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

static void init_schema(sqlite3* db) 
{
    // 最小表：projects / repos / project_repos
    const char* schema = R"SQL(
    PRAGMA foreign_keys = ON;

    CREATE TABLE IF NOT EXISTS projects (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT NOT NULL UNIQUE,
        created_at TEXT NOT NULL DEFAULT (datetime('now'))
    );

    CREATE TABLE IF NOT EXISTS repos (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        full_name TEXT NOT NULL UNIQUE, -- owner/repo
        enabled INTEGER NOT NULL DEFAULT 1,
        created_at TEXT NOT NULL DEFAULT (datetime('now'))
    );

    CREATE TABLE IF NOT EXISTS project_repos (
        project_id INTEGER NOT NULL,
        repo_id INTEGER NOT NULL,
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        PRIMARY KEY (project_id, repo_id),
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    )SQL";
    exec_sql(db, schema);
}

static std::string json_escape(const std::string& s) 
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
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

int main() 
{
    try 
    {
        const std::string db_path = get_env("DEVINSIGHT_DB", "data/dev.db");
        const int port = std::stoi(get_env("PORT", "8080"));

        // open db
        sqlite3* db = nullptr;
        if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) 
        {
            std::cerr << "open sqlite failed: " << sqlite3_errmsg(db) << "\n";
            return 1;
        }
        init_schema(db);

        httplib::Server app;

        app.Get("/api/health", [](const httplib::Request&, httplib::Response& res) 
        {
            res.set_content(R"({"ok":true})", "application/json");
        });

        // POST /api/projects?name=xxx
        app.Post("/api/projects", [db](const httplib::Request& req, httplib::Response& res) 
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
        });

        // POST /api/repos?full_name=owner/repo
        app.Post("/api/repos", [db](const httplib::Request& req, httplib::Response& res) 
        {
            auto full = req.get_param_value("full_name");
            if (full.empty() || full.find('/') == std::string::npos) 
            {
                res.status = 400;
                res.set_content(R"({"error":"full_name must be owner/repo"})", "application/json");
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
            sqlite3_bind_text(stmt, 1, full.c_str(), -1, SQLITE_TRANSIENT);

            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            if (rc != SQLITE_DONE) 
            {
                res.status = 409;
                res.set_content(R"({"error":"repo exists or insert failed"})", "application/json");
                return;
            }

            long long id = sqlite3_last_insert_rowid(db);
            res.set_content(std::string("{\"id\":") + std::to_string(id) + ",\"full_name\":\"" + json_escape(full) + "\"}", "application/json");
        });

        // POST /api/projects/{pid}/repos/{rid}
        app.Post(R"(/api/projects/(\d+)/repos/(\d+))", [db](const httplib::Request& req, httplib::Response& res) {
            int pid = std::stoi(req.matches[1]);
            int rid = std::stoi(req.matches[2]);

            sqlite3_stmt* stmt = nullptr;
            const char* sql = "INSERT INTO project_repos(project_id, repo_id) VALUES (?1, ?2);";
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
                res.status = 409;
                res.set_content(R"({"error":"link exists or invalid ids"})", "application/json");
                return;
            }
            res.set_content(R"({"ok":true})", "application/json");
        });

        // GET /api/repos
        app.Get("/api/repos", [db](const httplib::Request&, httplib::Response& res) 
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
        });

        std::cout << "DevInsight backend listening on http://127.0.0.1:" << port << "\n";
        std::cout << "DB: " << db_path << "\n";
        app.listen("0.0.0.0", port);

        sqlite3_close(db);
        return 0;
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}