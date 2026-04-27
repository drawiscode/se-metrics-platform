// 2.4(5) 隐形专家识别 API 路由
#include "routes.h"
#include "common/util.h"
#include "expert/pagerank.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <string>

static constexpr const char* kJson = "application/json; charset=utf-8";

static int get_int_param_expert(const httplib::Request& req, const std::string& key, int defv) {
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
}

// ============================================================
// GET /api/repos/{id}/experts?top=20
// 获取全局 PageRank 专家排名
// ============================================================
static void get_experts_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    int top = std::max(1, std::min(100, get_int_param_expert(req, "top", 20)));

    auto experts = compute_expert_pagerank(db, rid, top);
    std::string out = "{\"repo_id\":" + std::to_string(rid)
                    + ",\"items\":" + experts_to_json(experts) + "}";
    res.set_content(out, kJson);
}

// ============================================================
// GET /api/repos/{id}/experts/module?dir=src/ai&top=10
// 按模块目录获取专家排名
// ============================================================
static void get_module_experts_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    std::string dir = req.has_param("dir") ? req.get_param_value("dir") : "";
    int top = std::max(1, std::min(50, get_int_param_expert(req, "top", 10)));

    if (dir.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"missing 'dir' parameter"})", kJson);
        return;
    }

    // 确保 dir 以 / 结尾，方便 LIKE 匹配
    if (dir.back() != '/') dir += '/';

    auto experts = compute_module_experts(db, rid, dir, top);
    std::string out = "{\"repo_id\":" + std::to_string(rid)
                    + ",\"module\":\"" + util::json_escape(dir) + "\""
                    + ",\"items\":" + module_experts_to_json(experts) + "}";
    res.set_content(out, kJson);
}

// ============================================================
// POST /api/repos/{id}/experts/build
// 重新计算专家图谱并写入知识库
// ============================================================
static void post_experts_build_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    int count = build_expert_knowledge(db, rid);
    std::string out = "{\"ok\":true,\"repo_id\":" + std::to_string(rid)
                    + ",\"knowledge_chunks_written\":" + std::to_string(count) + "}";
    res.set_content(out, kJson);
}

// ============================================================
// 注册路由
// ============================================================
void register_expert_routes(httplib::Server& app, Db& db)
{
    app.Get(R"(/api/repos/(\d+)/experts/module)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_module_experts_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Get(R"(/api/repos/(\d+)/experts)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_experts_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Post(R"(/api/repos/(\d+)/experts/build)",
             [&db](const httplib::Request& req, httplib::Response& res) {
                 try { post_experts_build_handler(db, req, res); }
                 catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                 }
             });
}
