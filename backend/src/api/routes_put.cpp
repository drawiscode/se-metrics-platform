#include "routes.h"
#include <sqlite3.h>


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


void register_put_routes(httplib::Server& app, Db& db)
{
    app.Put(R"(/api/repos/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res)
            {
                put_repo_handler(db, req, res);
            });
}