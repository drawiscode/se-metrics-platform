#include "routes.h"
#include "common/util.h"
#include "risk/detector.h"

#include <algorithm>

namespace {

static constexpr const char* kJson = "application/json; charset=utf-8";

static int get_int_param(const httplib::Request& req, const std::string& key, int defv)
{
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
}

static std::string get_str_param(const httplib::Request& req, const std::string& key)
{
    if (!req.has_param(key)) return "";
    return req.get_param_value(key);
}

static void post_repo_risk_scan_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int repo_id = std::stoi(req.matches[1]);
    const int days = std::max(7, std::min(180, get_int_param(req, "days", 30)));

    auto result = scan_repo_risks(db, repo_id, days);
    if (!result.success) {
        res.status = 400;
    }

    res.set_content(risk_scan_result_to_json(result), kJson);
}

static void get_repo_risk_alerts_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int repo_id = std::stoi(req.matches[1]);
    const int limit = std::max(1, std::min(500, get_int_param(req, "limit", 50)));
    const int offset = std::max(0, get_int_param(req, "offset", 0));

    std::string status = get_str_param(req, "status");
    std::string severity = get_str_param(req, "severity");

    auto alerts = list_risk_alerts(db, repo_id, status, severity, limit, offset);
    std::string body = std::string("{\"items\":") + risk_alerts_to_json(alerts) + "}";
    res.set_content(body, kJson);
}

static void get_repo_risk_summary_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int repo_id = std::stoi(req.matches[1]);
    const int days = std::max(1, std::min(180, get_int_param(req, "days", 7)));

    std::string body = std::string("{\"summary\":") + risk_alerts_summary_to_json(db, repo_id, days) + "}";
    res.set_content(body, kJson);
}

} // namespace

void register_risk_routes(httplib::Server& app, Db& db)
{
    app.Post(R"(/api/repos/(\d+)/risk/scan)",
             [&db](const httplib::Request& req, httplib::Response& res) {
                 try {
                     post_repo_risk_scan_handler(db, req, res);
                 } catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                 }
             });

    app.Get(R"(/api/repos/(\d+)/risk/alerts)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try {
                    get_repo_risk_alerts_handler(db, req, res);
                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Get(R"(/api/repos/(\d+)/risk/alerts/summary)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try {
                    get_repo_risk_summary_handler(db, req, res);
                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });
}
