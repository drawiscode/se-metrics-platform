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

#include "routes.h"

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
        register_routes(app, db);

        
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