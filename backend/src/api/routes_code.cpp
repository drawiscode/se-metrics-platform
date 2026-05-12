#include "routes.h"
#include "common/util.h"
#include "ai/code_index.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <string>

static constexpr const char* kJson = "application/json; charset=utf-8";

static int get_int_param_code(const httplib::Request& req, const std::string& key, int defv) {
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
}

static std::string get_str_param_code(const httplib::Request& req, const std::string& key, const std::string& defv) {
    if (!req.has_param(key)) return defv;
    return req.get_param_value(key);
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

static std::string map_to_json(const std::map<std::string, int>& m)
{
    nlohmann::json j = nlohmann::json::object();
    for (const auto& kv : m) {
        j[kv.first] = kv.second;
    }
    return j.dump();
}

// ============================================================
// POST /api/repos/{id}/code/index
// ============================================================
static void post_code_index_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    std::string full_name;
    if (!db_get_repo_full_name(db, rid, full_name)) {//找仓库是否存在
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", kJson);
        return;
    }

    std::string ref = get_str_param_code(req, "ref", "main");
    std::string mode = get_str_param_code(req, "mode", "full");
    int max_files = std::max(0, get_int_param_code(req, "max_files", 2000));
    int max_total_kb = std::max(0, get_int_param_code(req, "max_total_kb", 200000));

    auto result = build_code_index(db, rid, full_name, ref, mode, max_files, max_total_kb);

    std::string out = "{\"ok\":true,\"repo_id\":" + std::to_string(rid)
                    + ",\"repo_head_sha\":\"" + util::json_escape(result.repo_head_sha) + "\""
                    + ",\"mode\":\"" + util::json_escape(mode) + "\""
                    + ",\"indexed_files\":" + std::to_string(result.indexed_files)
                    + ",\"indexed_chunks\":" + std::to_string(result.indexed_chunks)
                    + ",\"embeddings_generated\":" + std::to_string(result.embeddings_generated)
                    + ",\"skipped_files_reason_stats\":" + map_to_json(result.skipped_reason)
                    + "}";
    res.set_content(out, kJson);
}

// ============================================================
// 注册路由
// ============================================================
void register_code_routes(httplib::Server& app, Db& db)
{
    app.Post(R"(/api/repos/(\d+)/code/index)",
             [&db](const httplib::Request& req, httplib::Response& res) {
                 try { post_code_index_handler(db, req, res); }
                 catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                 }
             });
}
