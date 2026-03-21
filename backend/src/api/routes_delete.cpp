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



void register_delete_routes(httplib::Server& app, Db& db)
{
    app.Delete(R"(/api/repos/(\d+))",
               [&db](const httplib::Request& req, httplib::Response& res)
               {
                   delete_repo_handler(db, req, res);
               });
}