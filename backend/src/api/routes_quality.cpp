#include "routes.h"
#include "common/util.h"
#include "quality/static_analysis.h"
#include "quality/quality_score.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <cctype>
#include <vector>

static constexpr const char* kJson = "application/json; charset=utf-8";

static int get_int_param_quality(const httplib::Request& req, const std::string& key, int defv) {
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
}

static std::string get_str_param_quality(const httplib::Request& req, const std::string& key, const std::string& defv) {
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
    full_name_out = txt ? reinterpret_cast<const char*>(txt) : "";
    sqlite3_finalize(stmt);
    return !full_name_out.empty();
}

static std::string col_text(sqlite3_stmt* stmt, int idx)
{
    const unsigned char* txt = sqlite3_column_text(stmt, idx);
    return txt ? reinterpret_cast<const char*>(txt) : "";
}

static nlohmann::json parse_json_body(const httplib::Request& req)
{
    if (req.body.empty()) return nlohmann::json::object();
    return nlohmann::json::parse(req.body);
}

static std::string merge_config_with_path(const nlohmann::json& body,
                                          const httplib::Request& req,
                                          const std::string& base_config_json)
{
    std::string path = body.value("path", body.value("target", get_str_param_quality(req, "path", "")));
    nlohmann::json config = nlohmann::json::object();
    if (!base_config_json.empty()) {
        try {
            config = nlohmann::json::parse(base_config_json);
            if (!config.is_object()) config = nlohmann::json::object();
        } catch (...) {
            config = nlohmann::json::object();
        }
    }
    if (!path.empty()) config["path"] = path;
    return config.dump();
}

static bool is_tree_ignored_dir(const std::filesystem::path& p)
{
    static const std::unordered_set<std::string> kIgnored = {
        ".git", "node_modules", "dist", "build", "out",
        "vendor", "third_party", ".vscode",".github",".idea","docs","doc","docs_old",
        "example","examples","test","tests","testing",".gitignore","config","configs",
        "scripts",".circleci",".gitlab-ci.yml","assets","resource","resources","cmake",
        "cmake-build-debug",".vs",".vscode"
    };
    for (const auto& part : p) {
        std::string name = part.string();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (kIgnored.count(name)) return true;
    }
    return false;
}

static std::string to_lower_copy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static bool is_supported_source_file(const std::filesystem::path& p)
{
    const std::string ext = to_lower_copy(p.extension().string());
    return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx"
        || ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx"
        || ext == ".py" || ext == ".java";
}

struct QualityTaskRow {
    int id = 0;
    int repo_id = 0;
    std::string branch;
    std::string tools;
    std::string mode;
    int max_files = 0;
    std::string config_json;
};

static bool db_get_quality_task(Db& db, int task_id, QualityTaskRow& out)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, repo_id, branch, tools, mode, max_files, config_json "
        "FROM quality_analysis_tasks WHERE id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, task_id);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.id = sqlite3_column_int(stmt, 0);
        out.repo_id = sqlite3_column_int(stmt, 1);
        out.branch = col_text(stmt, 2);
        out.tools = col_text(stmt, 3);
        out.mode = col_text(stmt, 4);
        out.max_files = sqlite3_column_int(stmt, 5);
        out.config_json = col_text(stmt, 6);
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

static void set_task_status(Db& db, int task_id, const std::string& status)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE quality_analysis_tasks SET status=?1, updated_at=datetime('now') WHERE id=?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, task_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void post_quality_analyze_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    std::string full_name;
    if (!db_get_repo_full_name(db, rid, full_name)) {
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", kJson);
        return;
    }

    nlohmann::json body = nlohmann::json::object();
    try { body = parse_json_body(req); } catch (...) { body = nlohmann::json::object(); }

    std::string tool = body.value("tool", get_str_param_quality(req, "tool", "cppcheck"));
    std::string tools = body.value("tools", get_str_param_quality(req, "tools", tool));
    
    std::string ref = body.value("ref", get_str_param_quality(req, "ref", "main"));
    //std::string ref = body.value("ref", get_str_param_quality(req, "ref", "master"));
    
    std::string mode = body.value("mode", get_str_param_quality(req, "mode", "full"));
    int max_files = std::max(0, body.value("max_files", get_int_param_quality(req, "max_files", 2000)));
    std::string config_json = body.contains("config") ? body["config"].dump() : "{}";
    config_json = merge_config_with_path(body, req, config_json);

    auto result = run_static_analysis_task(db, rid, 0, full_name, ref, tools, mode, max_files, config_json);
    res.set_content(quality_result_to_json(result), kJson);
}

static void get_quality_issues_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    std::string tool = get_str_param_quality(req, "tool", "");
    std::string severity = get_str_param_quality(req, "severity", "");
    std::string status = get_str_param_quality(req, "status", "active");
    int limit = std::max(1, std::min(500, get_int_param_quality(req, "limit", 100)));
    int offset = std::max(0, get_int_param_quality(req, "offset", 0));

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    std::string sql =
        "SELECT id, tool, issue_key, file_path, line, column, rule_id, severity, message, "
        "status, first_seen_at, last_seen_at, fixed_at, first_seen_run_id, last_seen_run_id "
        "FROM quality_issues WHERE repo_id=?1 ";

    int next_param = 2;
    int tool_param = 0;
    int severity_param = 0;
    int status_param = 0;
    if (!tool.empty()) {
        tool_param = next_param++;
        sql += "AND tool=?" + std::to_string(tool_param) + " ";
    }
    if (!severity.empty()) {
        severity_param = next_param++;
        sql += "AND severity=?" + std::to_string(severity_param) + " ";
    }
    if (!status.empty() && status != "all") {
        status_param = next_param++;
        sql += "AND status=?" + std::to_string(status_param) + " ";
    }
    int limit_param = next_param++;
    int offset_param = next_param++;
    sql += "ORDER BY status ASC, last_seen_at DESC, id DESC LIMIT ?" +
           std::to_string(limit_param) + " OFFSET ?" + std::to_string(offset_param) + ";";

    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }

    sqlite3_bind_int(stmt, 1, rid);
    if (tool_param) sqlite3_bind_text(stmt, tool_param, tool.c_str(), -1, SQLITE_TRANSIENT);
    if (severity_param) sqlite3_bind_text(stmt, severity_param, severity.c_str(), -1, SQLITE_TRANSIENT);
    if (status_param) sqlite3_bind_text(stmt, status_param, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, limit_param, limit);
    sqlite3_bind_int(stmt, offset_param, offset);

    nlohmann::json items = nlohmann::json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json item;
        item["id"] = sqlite3_column_int(stmt, 0);
        item["tool"] = col_text(stmt, 1);
        item["issue_key"] = col_text(stmt, 2);
        item["file_path"] = col_text(stmt, 3);
        item["line"] = sqlite3_column_int(stmt, 4);
        item["column"] = sqlite3_column_int(stmt, 5);
        item["rule_id"] = col_text(stmt, 6);
        item["severity"] = col_text(stmt, 7);
        item["message"] = col_text(stmt, 8);
        item["status"] = col_text(stmt, 9);
        item["first_seen_at"] = col_text(stmt, 10);
        item["last_seen_at"] = col_text(stmt, 11);
        item["fixed_at"] = col_text(stmt, 12);
        item["first_seen_run_id"] = sqlite3_column_int(stmt, 13);
        item["last_seen_run_id"] = sqlite3_column_int(stmt, 14);
        items.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);

    std::string count_sql = "SELECT COUNT(*) FROM quality_issues WHERE repo_id=?1 ";
    next_param = 2;
    tool_param = 0;
    severity_param = 0;
    status_param = 0;
    if (!tool.empty()) {
        tool_param = next_param++;
        count_sql += "AND tool=?" + std::to_string(tool_param) + " ";
    }
    if (!severity.empty()) {
        severity_param = next_param++;
        count_sql += "AND severity=?" + std::to_string(severity_param) + " ";
    }
    if (!status.empty() && status != "all") {
        status_param = next_param++;
        count_sql += "AND status=?" + std::to_string(status_param) + " ";
    }
    count_sql += ";";

    int total = static_cast<int>(items.size());
    stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, count_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, rid);
        if (tool_param) sqlite3_bind_text(stmt, tool_param, tool.c_str(), -1, SQLITE_TRANSIENT);
        if (severity_param) sqlite3_bind_text(stmt, severity_param, severity.c_str(), -1, SQLITE_TRANSIENT);
        if (status_param) sqlite3_bind_text(stmt, status_param, status.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            total = sqlite3_column_int(stmt, 0);
        }
    }
    if (stmt) sqlite3_finalize(stmt);

    nlohmann::json out;
    out["items"] = items;
    out["total"] = total;
    out["limit"] = limit;
    out["offset"] = offset;
    res.set_content(out.dump(), kJson);
}

static void get_quality_summary_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    std::string tool = get_str_param_quality(req, "tool", "");

    QualityScore q = compute_quality_score(db, rid, tool);
    sqlite3* sdb = db.handle();

    int lines_analyzed = 0;
    int latest_run_id = 0;
    int latest_new_issues = 0;
    std::string latest_status;
    std::string latest_started_at;
    {
        sqlite3_stmt* stmt = nullptr;
        std::string sql =
            "SELECT id, status, started_at, lines_analyzed, issues_new "
            "FROM quality_analysis_runs WHERE repo_id=?1 ";
        if (!tool.empty()) {
            sql += "AND instr(',' || replace(tools, ' ', '') || ',', ',' || ?2 || ',')>0 ";
        }
        sql += "ORDER BY started_at DESC, id DESC LIMIT 1;";
        if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, rid);
            if (!tool.empty()) sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                latest_run_id = sqlite3_column_int(stmt, 0);
                latest_status = col_text(stmt, 1);
                latest_started_at = col_text(stmt, 2);
                lines_analyzed = sqlite3_column_int(stmt, 3);
                latest_new_issues = sqlite3_column_int(stmt, 4);
            }
        }
        if (stmt) sqlite3_finalize(stmt);
    }

    double min_score = 80.0;
    int max_new_issues = 0;
    int max_error_issues = 0;
    bool has_baseline = false;
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT min_score, max_new_issues, max_error_issues "
            "FROM quality_baselines WHERE repo_id=?1;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, rid);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                has_baseline = true;
                min_score = sqlite3_column_double(stmt, 0);
                max_new_issues = sqlite3_column_int(stmt, 1);
                max_error_issues = sqlite3_column_int(stmt, 2);
            }
        }
        if (stmt) sqlite3_finalize(stmt);
    }

    lines_analyzed = q.lines_analyzed > 0 ? q.lines_analyzed : lines_analyzed;
    const double density_per_kloc =
        lines_analyzed > 0 ? (static_cast<double>(q.total_issues) * 1000.0 / lines_analyzed) : 0.0;
    const bool score_degraded = has_baseline && q.score < min_score;
    const int active_errors = q.severity_counts.count("error") ? q.severity_counts["error"] : 0;
    const bool error_degraded = has_baseline && max_error_issues >= 0 && active_errors > max_error_issues;
    const bool new_issues_degraded = has_baseline && max_new_issues >= 0 && latest_new_issues > max_new_issues;

    nlohmann::json out;
    out["tool"] = tool;
    out["quality"] = nlohmann::json::parse(quality_score_to_json(q));
    out["lines_analyzed"] = lines_analyzed;
    out["density_per_kloc"] = density_per_kloc;
    out["latest_run"] = {
        {"id", latest_run_id},
        {"status", latest_status},
        {"started_at", latest_started_at},
        {"issues_new", latest_new_issues}
    };
    out["baseline"] = {
        {"configured", has_baseline},
        {"min_score", min_score},
        {"max_new_issues", max_new_issues},
        {"max_error_issues", max_error_issues},
        {"latest_new_issues", latest_new_issues},
        {"active_error_issues", active_errors},
        {"degraded", score_degraded || error_degraded || new_issues_degraded},
        {"score_degraded", score_degraded},
        {"error_degraded", error_degraded},
        {"new_issues_degraded", new_issues_degraded}
    };
    res.set_content(out.dump(), kJson);
}

static void post_quality_task_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);

    nlohmann::json body;
    try { body = parse_json_body(req); }
    catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid json"})", kJson);
        return;
    }

    const std::string branch = body.value("branch", body.value("ref", "main"));
    const std::string tools = body.value("tools", body.value("tool", "cppcheck"));
    const std::string mode = body.value("mode", "full");
    const int max_files = std::max(0, body.value("max_files", 2000));
    const std::string schedule = body.value("schedule", "manual");
    const std::string config_json = body.contains("config") ? body["config"].dump() : "{}";
    const std::string merged_config_json = merge_config_with_path(body, req, config_json);
    const bool run_now = body.value("run_now", false);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO quality_analysis_tasks(repo_id, branch, tools, mode, max_files, config_json, schedule, status) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 'Pending');";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }

    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_text(stmt, 2, branch.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, tools.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, max_files);
    sqlite3_bind_text(stmt, 6, merged_config_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, schedule.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.status = 500;
        res.set_content(R"({"error":"db step failed"})", kJson);
        return;
    }

    const int task_id = static_cast<int>(sqlite3_last_insert_rowid(sdb));
    nlohmann::json out;
    out["ok"] = true;
    out["task_id"] = task_id;

    if (run_now) {
        std::string full_name;
        if (!db_get_repo_full_name(db, rid, full_name)) {
            res.status = 404;
            res.set_content(R"({"error":"repo not found"})", kJson);
            return;
        }
        set_task_status(db, task_id, "Running");
        auto result = run_static_analysis_task(db, rid, task_id, full_name, branch, tools, mode, max_files, merged_config_json);
        out["run"] = nlohmann::json::parse(quality_result_to_json(result));
    }

    res.set_content(out.dump(), kJson);
}

static void get_quality_tasks_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    const int limit = std::max(1, std::min(200, get_int_param_quality(req, "limit", 50)));
    const int offset = std::max(0, get_int_param_quality(req, "offset", 0));
    const std::string status = get_str_param_quality(req, "status", "");

    std::string sql =
        "SELECT id, repo_id, branch, tools, mode, max_files, config_json, schedule, status, "
        "last_run_id, created_at, updated_at "
        "FROM quality_analysis_tasks WHERE repo_id=?1 ";
    if (!status.empty()) sql += "AND status=?2 ";
    const int limit_param = status.empty() ? 2 : 3;
    const int offset_param = status.empty() ? 3 : 4;
    sql += "ORDER BY created_at DESC, id DESC LIMIT ?" + std::to_string(limit_param)
        + " OFFSET ?" + std::to_string(offset_param) + ";";

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }

    int bi = 1;
    sqlite3_bind_int(stmt, bi++, rid);
    if (!status.empty()) sqlite3_bind_text(stmt, bi++, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, limit_param, limit);
    sqlite3_bind_int(stmt, offset_param, offset);

    nlohmann::json items = nlohmann::json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json item;
        item["id"] = sqlite3_column_int(stmt, 0);
        item["repo_id"] = sqlite3_column_int(stmt, 1);
        item["branch"] = col_text(stmt, 2);
        item["tools"] = col_text(stmt, 3);
        item["mode"] = col_text(stmt, 4);
        item["max_files"] = sqlite3_column_int(stmt, 5);
        item["config_json"] = col_text(stmt, 6);
        item["schedule"] = col_text(stmt, 7);
        item["status"] = col_text(stmt, 8);
        item["last_run_id"] = sqlite3_column_type(stmt, 9) == SQLITE_NULL ? nullptr : nlohmann::json(sqlite3_column_int(stmt, 9));
        item["created_at"] = col_text(stmt, 10);
        item["updated_at"] = col_text(stmt, 11);
        items.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);

    nlohmann::json out;
    out["items"] = items;
    res.set_content(out.dump(), kJson);
}

static void post_quality_task_run_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int task_id = std::stoi(req.matches[1]);
    QualityTaskRow task;
    if (!db_get_quality_task(db, task_id, task)) {
        res.status = 404;
        res.set_content(R"({"error":"quality task not found"})", kJson);
        return;
    }

    std::string full_name;
    if (!db_get_repo_full_name(db, task.repo_id, full_name)) {
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", kJson);
        return;
    }

    set_task_status(db, task_id, "Running");
    auto result = run_static_analysis_task(db, task.repo_id, task_id, full_name,
                                           task.branch, task.tools, task.mode,
                                           task.max_files, task.config_json);
    res.set_content(quality_result_to_json(result), kJson);
}

static void get_quality_runs_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    const int limit = std::max(1, std::min(200, get_int_param_quality(req, "limit", 50)));
    const int offset = std::max(0, get_int_param_quality(req, "offset", 0));

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, task_id, branch, tools, mode, max_files, status, started_at, finished_at, "
        "analyzed_files, lines_analyzed, issues_total, issues_new, issues_fixed, "
        "issues_by_severity_json, score, baseline_score, degraded, output_json, error "
        "FROM quality_analysis_runs WHERE repo_id=?1 "
        "ORDER BY started_at DESC, id DESC LIMIT ?2 OFFSET ?3;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_int(stmt, 2, limit);
    sqlite3_bind_int(stmt, 3, offset);

    nlohmann::json items = nlohmann::json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int run_id = sqlite3_column_int(stmt, 0);
        QualityScore run_score = compute_quality_score_for_run(db, run_id);
        const bool has_baseline = sqlite3_column_type(stmt, 16) != SQLITE_NULL;
        const double baseline_score = has_baseline ? sqlite3_column_double(stmt, 16) : 0.0;
        const bool degraded = sqlite3_column_int(stmt, 17) != 0;
        nlohmann::json item;
        item["id"] = run_id;
        item["task_id"] = sqlite3_column_type(stmt, 1) == SQLITE_NULL ? nullptr : nlohmann::json(sqlite3_column_int(stmt, 1));
        item["branch"] = col_text(stmt, 2);
        item["tools"] = col_text(stmt, 3);
        item["mode"] = col_text(stmt, 4);
        item["max_files"] = sqlite3_column_int(stmt, 5);
        item["status"] = col_text(stmt, 6);
        item["started_at"] = col_text(stmt, 7);
        item["finished_at"] = col_text(stmt, 8);
        item["analyzed_files"] = sqlite3_column_int(stmt, 9);
        item["lines_analyzed"] = sqlite3_column_int(stmt, 10);
        item["issues_total"] = sqlite3_column_int(stmt, 11);
        item["issues_new"] = sqlite3_column_int(stmt, 12);
        item["issues_fixed"] = sqlite3_column_int(stmt, 13);
        try { item["issues_by_severity"] = nlohmann::json::parse(col_text(stmt, 14)); }
        catch (...) { item["issues_by_severity"] = nlohmann::json::object(); }
        item["score"] = run_score.score;
        item["baseline_score"] = has_baseline ? nlohmann::json(baseline_score) : nullptr;
        item["degraded"] = degraded;
        try { item["output"] = nlohmann::json::parse(col_text(stmt, 18)); }
        catch (...) { item["output"] = nlohmann::json::object(); }
        item["error"] = col_text(stmt, 19);
        items.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);

    nlohmann::json out;
    out["items"] = items;
    res.set_content(out.dump(), kJson);
}

static void get_quality_trend_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    const int limit = std::max(1, std::min(100, get_int_param_quality(req, "limit", 20)));

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, started_at, status, issues_total, issues_new, issues_fixed, score, "
        "lines_analyzed, degraded, issues_by_severity_json, baseline_score "
        "FROM quality_analysis_runs WHERE repo_id=?1 AND status='Finished' "
        "ORDER BY started_at DESC, id DESC LIMIT ?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_int(stmt, 2, limit);

    nlohmann::json items = nlohmann::json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int run_id = sqlite3_column_int(stmt, 0);
        const int lines = sqlite3_column_int(stmt, 7);
        const int total = sqlite3_column_int(stmt, 3);
        QualityScore run_score = compute_quality_score_for_run(db, run_id);
        nlohmann::json item;
        item["run_id"] = run_id;
        item["started_at"] = col_text(stmt, 1);
        item["status"] = col_text(stmt, 2);
        item["issues_total"] = total;
        item["issues_new"] = sqlite3_column_int(stmt, 4);
        item["issues_fixed"] = sqlite3_column_int(stmt, 5);
        item["score"] = run_score.score;
        item["lines_analyzed"] = lines;
        item["density_per_kloc"] = lines > 0 ? (static_cast<double>(total) * 1000.0 / lines) : 0.0;
        item["degraded"] = sqlite3_column_int(stmt, 8) != 0;
        try { item["severity"] = nlohmann::json::parse(col_text(stmt, 9)); }
        catch (...) { item["severity"] = nlohmann::json::object(); }
        items.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);
    std::reverse(items.begin(), items.end());

    nlohmann::json out;
    out["items"] = items;
    res.set_content(out.dump(), kJson);
}

static std::string top_group_sql(const std::string& by)
{
    if (by == "rule") {
        return "SELECT rule_id AS name, COUNT(*) AS total, "
               "SUM(CASE WHEN severity='error' THEN 1 ELSE 0 END) AS errors "
               "FROM quality_issues WHERE repo_id=?1 AND status='active' "
               "AND (?2='' OR tool=?2) GROUP BY rule_id ORDER BY total DESC LIMIT ?3;";
    }
    if (by == "dir") {
        return "SELECT CASE WHEN instr(file_path,'/')>0 THEN substr(file_path,1,instr(file_path,'/')-1) ELSE '.' END AS name, "
               "COUNT(*) AS total, SUM(CASE WHEN severity='error' THEN 1 ELSE 0 END) AS errors "
               "FROM quality_issues WHERE repo_id=?1 AND status='active' "
               "AND (?2='' OR tool=?2) GROUP BY name ORDER BY total DESC LIMIT ?3;";
    }
    return "SELECT file_path AS name, COUNT(*) AS total, "
           "SUM(CASE WHEN severity='error' THEN 1 ELSE 0 END) AS errors "
           "FROM quality_issues WHERE repo_id=?1 AND status='active' "
           "AND (?2='' OR tool=?2) GROUP BY file_path ORDER BY total DESC LIMIT ?3;";
}

static void get_quality_top_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    const std::string by = get_str_param_quality(req, "by", "file");
    const std::string tool = get_str_param_quality(req, "tool", "");
    const int limit = std::max(1, std::min(100, get_int_param_quality(req, "limit", 20)));
    const std::string sql = top_group_sql(by);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, limit);

    nlohmann::json items = nlohmann::json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json item;
        item["name"] = col_text(stmt, 0);
        item["total"] = sqlite3_column_int(stmt, 1);
        item["errors"] = sqlite3_column_int(stmt, 2);
        items.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);

    nlohmann::json out;
    out["by"] = by;
    out["items"] = items;
    res.set_content(out.dump(), kJson);
}

static void get_repo_tree_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    std::string full_name;
    if (!db_get_repo_full_name(db, rid, full_name)) {
        res.status = 404;
        res.set_content(R"({"error":"repo not found"})", kJson);
        return;
    }

    const std::string ref = get_str_param_quality(req, "ref", "main");
    const int max_items = std::max(1, std::min(50000, get_int_param_quality(req, "max", 5000)));

    std::string repo_dir;
    std::string err;
    if (!ensure_repo_checkout_for_quality(rid, full_name, ref, repo_dir, err)) {
        res.status = 500;
        res.set_content(std::string("{\"error\":\"") + util::json_escape(err) + "\"}", kJson);
        return;
    }

    nlohmann::json items = nlohmann::json::array();
    bool truncated = false;

    std::error_code ec;
    std::vector<std::string> file_paths;
    std::unordered_set<std::string> dir_paths;
    const std::filesystem::path root_path(repo_dir);
    for (auto it = std::filesystem::recursive_directory_iterator(root_path, ec);
         it != std::filesystem::recursive_directory_iterator();
         ++it) {
        if (ec) break;
        const auto& p = it->path();
        if (is_tree_ignored_dir(p)) {
            it.disable_recursion_pending();
            continue;
        }

        if (!it->is_regular_file()) continue;
        if (!is_supported_source_file(p)) continue;

        std::string rel = std::filesystem::relative(p, root_path, ec).generic_string();
        if (ec || rel.empty()) continue;
        if (rel.rfind("../", 0) == 0) continue;

        file_paths.push_back(rel);

        std::filesystem::path parent = std::filesystem::path(rel).parent_path();
        while (!parent.empty()) {
            std::string parent_str = parent.generic_string();
            if (!parent_str.empty()) dir_paths.insert(parent_str);
            parent = parent.parent_path();
        }
    }

    std::sort(file_paths.begin(), file_paths.end());
    file_paths.erase(std::unique(file_paths.begin(), file_paths.end()), file_paths.end());

    std::vector<std::string> dirs(dir_paths.begin(), dir_paths.end());
    std::sort(dirs.begin(), dirs.end());

    auto append_item = [&](const std::string& path, const char* type) {
        if (static_cast<int>(items.size()) >= max_items) {
            truncated = true;
            return false;
        }
        nlohmann::json item;
        item["path"] = path;
        item["type"] = type;
        items.push_back(std::move(item));
        return true;
    };

    for (const auto& dir : dirs) {
        if (!append_item(dir, "dir")) break;
    }
    if (!truncated) {
        for (const auto& file : file_paths) {
            if (!append_item(file, "file")) break;
        }
    }

    nlohmann::json out;
    out["items"] = items;
    out["truncated"] = truncated;
    out["max"] = max_items;
    res.set_content(out.dump(), kJson);
}

static void get_quality_insights_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    const std::string tool = get_str_param_quality(req, "tool", "");
    sqlite3* sdb = db.handle();

    QualityScore current = compute_quality_score(db, rid, tool);
    const int active_errors = current.severity_counts.count("error") ? current.severity_counts["error"] : 0;

    nlohmann::json top_file = nullptr;
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT file_path, COUNT(*) AS total, "
            "SUM(CASE WHEN severity='error' THEN 1 ELSE 0 END) AS errors "
            "FROM quality_issues WHERE repo_id=?1 AND status='active' "
            "AND (?2='' OR tool=?2) GROUP BY file_path "
            "ORDER BY errors DESC, total DESC, file_path ASC LIMIT 1;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, rid);
            sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                top_file = {
                    {"path", col_text(stmt, 0)},
                    {"total", sqlite3_column_int(stmt, 1)},
                    {"errors", sqlite3_column_int(stmt, 2)}
                };
            }
        }
        if (stmt) sqlite3_finalize(stmt);
    }

    nlohmann::json top_rule = nullptr;
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT rule_id, COUNT(*) AS total "
            "FROM quality_issues WHERE repo_id=?1 AND status='active' "
            "AND (?2='' OR tool=?2) GROUP BY rule_id "
            "ORDER BY total DESC, rule_id ASC LIMIT 1;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, rid);
            sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                top_rule = {
                    {"rule_id", col_text(stmt, 0)},
                    {"total", sqlite3_column_int(stmt, 1)}
                };
            }
        }
        if (stmt) sqlite3_finalize(stmt);
    }

    double latest_score = current.score;
    double previous_score = current.score;
    int latest_issues = current.total_issues;
    int previous_issues = current.total_issues;
    std::string latest_started_at;
    {
        sqlite3_stmt* stmt = nullptr;
        std::string sql =
            "SELECT id, started_at FROM quality_analysis_runs "
            "WHERE repo_id=?1 AND status='Finished' ";
        if (!tool.empty()) {
            sql += "AND instr(',' || replace(tools, ' ', '') || ',', ',' || ?2 || ',')>0 ";
        }
        sql += "ORDER BY started_at DESC, id DESC LIMIT 2;";
        if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, rid);
            if (!tool.empty()) sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
            int idx = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const int run_id = sqlite3_column_int(stmt, 0);
                QualityScore run_score = compute_quality_score_for_run(db, run_id);
                if (idx == 0) {
                    latest_score = run_score.score;
                    latest_issues = run_score.total_issues;
                    latest_started_at = col_text(stmt, 1);
                } else {
                    previous_score = run_score.score;
                    previous_issues = run_score.total_issues;
                }
                ++idx;
            }
        }
        if (stmt) sqlite3_finalize(stmt);
    }

    const double score_delta = latest_score - previous_score;
    const int issue_delta = latest_issues - previous_issues;

    std::string risk_level = "clean";
    if (active_errors > 0 || current.score < 60.0) risk_level = "critical";
    else if (current.score < 80.0 || current.total_issues > 0 || score_delta < -1.0) risk_level = "watch";

    nlohmann::json actions = nlohmann::json::array();
    if (active_errors > 0) {
        actions.push_back("优先清理 active error 问题，避免真实缺陷和安全风险继续累积。");
    }
    if (!top_file.is_null()) {
        actions.push_back("先处理热点文件 " + top_file.value("path", std::string{})
            + "，它集中了 " + std::to_string(top_file.value("total", 0)) + " 个活跃问题。");
    }
    if (!top_rule.is_null() && top_rule.value("total", 0) >= 3) {
        actions.push_back("将规则 " + top_rule.value("rule_id", std::string{})
            + " 做成团队级修复清单，适合批量治理。");
    }
    if (score_delta < -1.0) {
        actions.push_back("最近完成扫描评分下降 "
            + std::to_string(std::abs(score_delta)).substr(0, 4)
            + " 分，建议回看最新提交或变更范围。");
    }
    if (actions.empty()) {
        actions.push_back("当前质量信号稳定，可以保持周期扫描并把基线设为团队准入条件。");
    }

    nlohmann::json out;
    out["tool"] = tool;
    out["risk_level"] = risk_level;
    out["current_score"] = current.score;
    out["active_issues"] = current.total_issues;
    out["active_errors"] = active_errors;
    out["top_file"] = top_file;
    out["top_rule"] = top_rule;
    out["trend"] = {
        {"latest_started_at", latest_started_at},
        {"latest_score", latest_score},
        {"previous_score", previous_score},
        {"score_delta", score_delta},
        {"latest_issues", latest_issues},
        {"previous_issues", previous_issues},
        {"issue_delta", issue_delta}
    };
    out["actions"] = actions;
    res.set_content(out.dump(), kJson);
}

static void get_quality_baseline_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT min_score, max_new_issues, max_error_issues, updated_at "
        "FROM quality_baselines WHERE repo_id=?1;";
    sqlite3* sdb = db.handle();
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }
    sqlite3_bind_int(stmt, 1, rid);

    nlohmann::json out;
    out["configured"] = false;
    out["min_score"] = 80.0;
    out["max_new_issues"] = 0;
    out["max_error_issues"] = 0;
    out["updated_at"] = nullptr;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out["configured"] = true;
        out["min_score"] = sqlite3_column_double(stmt, 0);
        out["max_new_issues"] = sqlite3_column_int(stmt, 1);
        out["max_error_issues"] = sqlite3_column_int(stmt, 2);
        out["updated_at"] = col_text(stmt, 3);
    }
    sqlite3_finalize(stmt);
    res.set_content(out.dump(), kJson);
}

static void put_quality_baseline_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const int rid = std::stoi(req.matches[1]);
    nlohmann::json body;
    try { body = parse_json_body(req); }
    catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"invalid json"})", kJson);
        return;
    }

    const double min_score = std::max(0.0, std::min(100.0, body.value("min_score", 80.0)));
    const int max_new_issues = std::max(0, body.value("max_new_issues", 0));
    const int max_error_issues = std::max(0, body.value("max_error_issues", 0));

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO quality_baselines(repo_id, min_score, max_new_issues, max_error_issues, updated_at) "
        "VALUES (?1, ?2, ?3, ?4, datetime('now')) "
        "ON CONFLICT(repo_id) DO UPDATE SET "
        "min_score=excluded.min_score, "
        "max_new_issues=excluded.max_new_issues, "
        "max_error_issues=excluded.max_error_issues, "
        "updated_at=datetime('now');";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db prepare failed"})", kJson);
        return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    sqlite3_bind_double(stmt, 2, min_score);
    sqlite3_bind_int(stmt, 3, max_new_issues);
    sqlite3_bind_int(stmt, 4, max_error_issues);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.status = 500;
        res.set_content(R"({"error":"db step failed"})", kJson);
        return;
    }

    nlohmann::json out;
    out["ok"] = true;
    out["min_score"] = min_score;
    out["max_new_issues"] = max_new_issues;
    out["max_error_issues"] = max_error_issues;
    res.set_content(out.dump(), kJson);
}

void register_quality_routes(httplib::Server& app, Db& db)
{
    app.Post(R"(/api/repos/(\d+)/quality/analyze)",
             [&db](const httplib::Request& req, httplib::Response& res) {
                 try { post_quality_analyze_handler(db, req, res); }
                 catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                 }
             });

    app.Get(R"(/api/repos/(\d+)/quality/issues)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_quality_issues_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Get(R"(/api/repos/(\d+)/quality/summary)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_quality_summary_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Get(R"(/api/repos/(\d+)/tree)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_repo_tree_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Post(R"(/api/repos/(\d+)/quality/tasks)",
             [&db](const httplib::Request& req, httplib::Response& res) {
                 try { post_quality_task_handler(db, req, res); }
                 catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                 }
             });

    app.Get(R"(/api/repos/(\d+)/quality/tasks)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_quality_tasks_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Post(R"(/api/quality/tasks/(\d+)/run)",
             [&db](const httplib::Request& req, httplib::Response& res) {
                 try { post_quality_task_run_handler(db, req, res); }
                 catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                 }
             });

    app.Get(R"(/api/repos/(\d+)/quality/runs)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_quality_runs_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Get(R"(/api/repos/(\d+)/quality/trend)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_quality_trend_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Get(R"(/api/repos/(\d+)/quality/top)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_quality_top_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Get(R"(/api/repos/(\d+)/quality/insights)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_quality_insights_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Get(R"(/api/repos/(\d+)/quality/baseline)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_quality_baseline_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });

    app.Put(R"(/api/repos/(\d+)/quality/baseline)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { put_quality_baseline_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJson);
                }
            });
}
