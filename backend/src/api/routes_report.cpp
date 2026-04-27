// 2.4(4) 自动周报生成 API 路由
#include "routes.h"
#include "common/util.h"
#include "report/weekly_report.h"

#include <nlohmann/json.hpp>
#include <string>

static constexpr const char* kJson = "application/json; charset=utf-8";

static int get_int_param_report(const httplib::Request& req, const std::string& key, int defv) {
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
}

// ============================================================
// POST /api/repos/{id}/report/generate
// 生成周报
// ============================================================
static void post_generate_report_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    auto report = generate_weekly_report(db, rid);

    if (report.id <= 0 && report.report_text.find("Error") == 0) {
        res.status = 500;
        res.set_content("{\"error\":\"" + util::json_escape(report.report_text) + "\"}", kJson);
        return;
    }

    res.set_content(report_to_json(report), kJson);
}

// ============================================================
// GET /api/repos/{id}/reports?limit=10
// 查询周报历史
// ============================================================
static void get_reports_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    int limit = std::max(1, std::min(50, get_int_param_report(req, "limit", 10)));

    auto reports = list_weekly_reports(db, rid, limit);
    std::string out = "{\"repo_id\":" + std::to_string(rid)
                    + ",\"items\":" + reports_to_json(reports) + "}";
    res.set_content(out, kJson);
}

// ============================================================
// GET /api/repos/{id}/report/latest
// 获取最新周报
// ============================================================
static void get_latest_report_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    auto report = get_latest_report(db, rid);

    if (report.id <= 0) {
        res.status = 404;
        res.set_content(R"({"error":"no report found"})", kJson);
        return;
    }

    res.set_content(report_to_json(report), kJson);
}

// ============================================================
// 注册路由
// ============================================================
void register_report_routes(httplib::Server& app, Db& db)
{
    app.Post(R"(/api/repos/(\d+)/report/generate)",
             [&db](const httplib::Request& req, httplib::Response& res) {
                 try { post_generate_report_handler(db, req, res); }
                 catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                 }
             });

    // 注意: latest 必须在 reports 之前注册，否则 /report/latest 会被 /reports 捕获
    app.Get(R"(/api/repos/(\d+)/report/latest)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_latest_report_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Get(R"(/api/repos/(\d+)/reports)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_reports_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });
}
