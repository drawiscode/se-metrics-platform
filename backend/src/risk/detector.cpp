#include "detector.h"

#include "common/util.h"
#include "db/db.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <vector>

namespace {

// 异常规则使用的按日时间序列数据点
struct TimeValue {
    std::string day;
    double value = 0.0;
};

// 告警候选项（尚未落库）
struct CandidateAlert {
    std::string alert_type;
    std::string metric_name;
    std::string window_start;
    std::string window_end;
    double current_value = 0.0;
    double baseline_value = 0.0;
    double threshold_value = 0.0;
    std::string severity;
    std::string scope_type = "repo";
    std::string scope_id;
    std::string suggested_action;
    std::string evidence_json;
};

// 使用中位数和 MAD 构建基线，规则更可解释，且比均值/方差更抗异常点
double median_of(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    if (n % 2 == 1) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

double mad_of(const std::vector<double>& values, double med)
{
    if (values.empty()) return 0.0;
    std::vector<double> dev;
    dev.reserve(values.size());
    for (double v : values) dev.push_back(std::abs(v - med));
    return median_of(dev);
}

bool repo_exists(Db& db, int repo_id)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    const char* sql = "SELECT 1 FROM repos WHERE id=?1 LIMIT 1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        ok = sqlite3_step(stmt) == SQLITE_ROW;
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

// 写入一次扫描运行记录（生命周期开始）
int insert_run(Db& db, int repo_id)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    int run_id = 0;
    const char* sql =
        "INSERT INTO risk_alert_runs(repo_id, status) VALUES (?1, 'running');";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            run_id = static_cast<int>(sqlite3_last_insert_rowid(sdb));
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return run_id;
}

void finish_run(Db& db, int run_id, const std::string& status,
                const std::string& summary_json, const std::string& error)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE risk_alert_runs "
        "SET finished_at=datetime('now'), status=?1, summary_json=?2, error=?3 "
        "WHERE id=?4;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, summary_json.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, error.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, run_id);
        sqlite3_step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
}

// 同一天内按 alert_type 去重，避免重复告警噪音
bool has_today_open_alert(Db& db, int repo_id, const std::string& alert_type)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    int cnt = 0;
    const char* sql =
        "SELECT COUNT(*) FROM risk_alert_events "
        "WHERE repo_id=?1 AND alert_type=?2 AND status='open' "
        "AND date(created_at)=date('now');";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        sqlite3_bind_text(stmt, 2, alert_type.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) cnt = sqlite3_column_int(stmt, 0);
    }
    if (stmt) sqlite3_finalize(stmt);
    return cnt > 0;
}

int insert_alert_event(Db& db, int repo_id, int run_id, const CandidateAlert& a)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    int id = 0;
    const char* sql =
        "INSERT INTO risk_alert_events(" 
        "run_id, repo_id, alert_type, metric_name, window_start, window_end, "
        "current_value, baseline_value, threshold_value, severity, scope_type, scope_id, "
        "suggested_action, status, evidence_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, 'open', ?14);";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, run_id);
        sqlite3_bind_int(stmt, 2, repo_id);
        sqlite3_bind_text(stmt, 3, a.alert_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, a.metric_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, a.window_start.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, a.window_end.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 7, a.current_value);
        sqlite3_bind_double(stmt, 8, a.baseline_value);
        sqlite3_bind_double(stmt, 9, a.threshold_value);
        sqlite3_bind_text(stmt, 10, a.severity.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 11, a.scope_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 12, a.scope_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 13, a.suggested_action.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 14, a.evidence_json.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            id = static_cast<int>(sqlite3_last_insert_rowid(sdb));
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return id;
}

// 告警同步写入 knowledge_chunks，供 AI/RAG 检索引用
void insert_alert_knowledge_chunk(Db& db, int repo_id, int alert_id, const CandidateAlert& a)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    std::string title = "Risk Alert: " + a.alert_type;
    std::string content =
        "metric=" + a.metric_name +
        ", current=" + std::to_string(a.current_value) +
        ", baseline=" + std::to_string(a.baseline_value) +
        ", threshold=" + std::to_string(a.threshold_value) +
        ", severity=" + a.severity +
        "\n" + a.suggested_action;

    const char* sql =
        "INSERT INTO knowledge_chunks(repo_id, source_type, source_id, title, content, author, event_time) "
        "VALUES (?1, 'risk_alert', ?2, ?3, ?4, 'system', datetime('now'));";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        std::string source_id = std::to_string(alert_id);
        sqlite3_bind_text(stmt, 2, source_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
}

std::vector<TimeValue> query_daily_values(Db& db, int repo_id, const std::string& sql,
                                          const std::string& lookback)
{
    std::vector<TimeValue> rows;
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return rows;
    }

    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, lookback.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TimeValue tv;
        auto d = sqlite3_column_text(stmt, 0);
        tv.day = d ? reinterpret_cast<const char*>(d) : "";
        tv.value = sqlite3_column_double(stmt, 1);
        rows.push_back(std::move(tv));
    }

    sqlite3_finalize(stmt);
    return rows;
}

void maybe_add_candidate(std::vector<CandidateAlert>& out, const CandidateAlert& c)
{
    if (c.current_value > c.threshold_value) {
        out.push_back(c);
    }
}

// 规则 1：单日代码 churn 突增
std::vector<CandidateAlert> detect_churn_spike(Db& db, int repo_id, int lookback_days)
{
    std::vector<CandidateAlert> out;
    const std::string sql =
        "SELECT date(c.committed_at) AS d, "
        "SUM(COALESCE(NULLIF(cf.changes, 0), (cf.additions + cf.deletions))) AS churn "
        "FROM commits c JOIN commit_files cf ON cf.commit_id = c.id "
        "WHERE c.repo_id=?1 AND c.committed_at >= datetime('now', ?2) "
        "GROUP BY date(c.committed_at) ORDER BY d ASC;";

    const std::string lookback = "-" + std::to_string(lookback_days) + " days";
    auto rows = query_daily_values(db, repo_id, sql, lookback);
    if (rows.size() < 7) return out;

    const TimeValue latest = rows.back();
    std::vector<double> base;
    for (size_t i = 0; i + 1 < rows.size(); ++i) base.push_back(rows[i].value);

    const double med = median_of(base);
    const double mad = std::max(1.0, mad_of(base, med));
    const double threshold = std::max(200.0, med + 3.0 * mad);

    if (latest.value > threshold) {
        nlohmann::json ev = {
            {"latest_day", latest.day},
            {"latest_value", latest.value},
            {"baseline_median", med},
            {"baseline_mad", mad}
        };

        CandidateAlert a;
        a.alert_type = "code_churn_spike";
        a.metric_name = "daily_churn";
        a.window_start = rows.front().day;
        a.window_end = latest.day;
        a.current_value = latest.value;
        a.baseline_value = med;
        a.threshold_value = threshold;
        a.severity = latest.value > threshold * 1.5 ? "critical" : "warning";
        a.scope_id = std::to_string(repo_id);
        a.suggested_action = "Review recent high-churn commits, prioritize hotspot files, and add regression tests.";
        a.evidence_json = ev.dump();
        out.push_back(std::move(a));
    }

    return out;
}

// 规则 2：open issue 积压突增
std::vector<CandidateAlert> detect_issue_backlog_spike(Db& db, int repo_id, int lookback_days)
{
    std::vector<CandidateAlert> out;
    const std::string sql =
        "SELECT date(ts) AS d, MAX(open_issues) AS open_issues "
        "FROM repo_snapshots "
        "WHERE repo_id=?1 AND ts >= datetime('now', ?2) "
        "GROUP BY date(ts) ORDER BY d ASC;";

    const std::string lookback = "-" + std::to_string(lookback_days) + " days";
    auto rows = query_daily_values(db, repo_id, sql, lookback);
    if (rows.size() < 7) return out;

    const TimeValue latest = rows.back();
    std::vector<double> base;
    for (size_t i = 0; i + 1 < rows.size(); ++i) base.push_back(rows[i].value);

    const double med = median_of(base);
    const double mad = std::max(1.0, mad_of(base, med));
    const double threshold = med + 2.5 * mad;

    if (latest.value > std::max(threshold, med + 5.0)) {
        nlohmann::json ev = {
            {"latest_day", latest.day},
            {"latest_value", latest.value},
            {"baseline_median", med},
            {"baseline_mad", mad}
        };

        CandidateAlert a;
        a.alert_type = "issue_backlog_spike";
        a.metric_name = "open_issues";
        a.window_start = rows.front().day;
        a.window_end = latest.day;
        a.current_value = latest.value;
        a.baseline_value = med;
        a.threshold_value = threshold;
        a.severity = latest.value > threshold * 1.4 ? "critical" : "warning";
        a.scope_id = std::to_string(repo_id);
        a.suggested_action = "Review newly opened issues, assign owners, and clear high-priority backlog first.";
        a.evidence_json = ev.dump();
        out.push_back(std::move(a));
    }

    return out;
}

// 规则 3：PR 合并耗时（小时）突增
std::vector<CandidateAlert> detect_pr_merge_latency_spike(Db& db, int repo_id, int lookback_days)
{
    std::vector<CandidateAlert> out;
    const std::string sql =
        "SELECT date(merged_at) AS d, AVG((julianday(merged_at)-julianday(created_at))*24.0) AS hours "
        "FROM pull_requests "
        "WHERE repo_id=?1 AND merged_at IS NOT NULL AND merged_at >= datetime('now', ?2) "
        "GROUP BY date(merged_at) ORDER BY d ASC;";

    const std::string lookback = "-" + std::to_string(lookback_days) + " days";
    auto rows = query_daily_values(db, repo_id, sql, lookback);
    if (rows.size() < 7) return out;

    const TimeValue latest = rows.back();
    std::vector<double> base;
    for (size_t i = 0; i + 1 < rows.size(); ++i) base.push_back(rows[i].value);

    const double med = median_of(base);
    const double mad = std::max(0.5, mad_of(base, med));
    const double threshold = std::max(24.0, med + 2.5 * mad);

    if (latest.value > threshold) {
        nlohmann::json ev = {
            {"latest_day", latest.day},
            {"latest_value", latest.value},
            {"baseline_median", med},
            {"baseline_mad", mad}
        };

        CandidateAlert a;
        a.alert_type = "pr_merge_latency_spike";
        a.metric_name = "pr_merge_latency_hours";
        a.window_start = rows.front().day;
        a.window_end = latest.day;
        a.current_value = latest.value;
        a.baseline_value = med;
        a.threshold_value = threshold;
        a.severity = latest.value > threshold * 1.4 ? "critical" : "warning";
        a.scope_id = std::to_string(repo_id);
        a.suggested_action = "Check PR review backlog and reviewer allocation, and prioritize long-waiting PRs.";
        a.evidence_json = ev.dump();
        out.push_back(std::move(a));
    }

    return out;
}

RiskAlertEvent read_alert_from_stmt(sqlite3_stmt* stmt)
{
    RiskAlertEvent e;
    e.id = sqlite3_column_int(stmt, 0);
    e.repo_id = sqlite3_column_int(stmt, 1);
    auto c2 = sqlite3_column_text(stmt, 2); e.alert_type = c2 ? (const char*)c2 : "";
    auto c3 = sqlite3_column_text(stmt, 3); e.metric_name = c3 ? (const char*)c3 : "";
    auto c4 = sqlite3_column_text(stmt, 4); e.window_start = c4 ? (const char*)c4 : "";
    auto c5 = sqlite3_column_text(stmt, 5); e.window_end = c5 ? (const char*)c5 : "";
    e.current_value = sqlite3_column_double(stmt, 6);
    e.baseline_value = sqlite3_column_double(stmt, 7);
    e.threshold_value = sqlite3_column_double(stmt, 8);
    auto c9 = sqlite3_column_text(stmt, 9); e.severity = c9 ? (const char*)c9 : "";
    auto c10 = sqlite3_column_text(stmt, 10); e.scope_type = c10 ? (const char*)c10 : "";
    auto c11 = sqlite3_column_text(stmt, 11); e.scope_id = c11 ? (const char*)c11 : "";
    auto c12 = sqlite3_column_text(stmt, 12); e.suggested_action = c12 ? (const char*)c12 : "";
    auto c13 = sqlite3_column_text(stmt, 13); e.status = c13 ? (const char*)c13 : "";
    auto c14 = sqlite3_column_text(stmt, 14); e.evidence_json = c14 ? (const char*)c14 : "";
    auto c15 = sqlite3_column_text(stmt, 15); e.created_at = c15 ? (const char*)c15 : "";
    return e;
}

} // 匿名命名空间

// 对外扫描入口
RiskScanResult scan_repo_risks(Db& db, int repo_id, int lookback_days)
{
    RiskScanResult result;
    result.repo_id = repo_id;

    if (!repo_exists(db, repo_id)) {
        result.error = "repo not found";
        return result;
    }

    const int days = std::max(7, std::min(180, lookback_days));
    result.run_id = insert_run(db, repo_id);

    std::vector<CandidateAlert> candidates;
    // 执行全部规则并汇总候选告警
    {
        auto v = detect_churn_spike(db, repo_id, days);
        candidates.insert(candidates.end(), v.begin(), v.end());
    }
    {
        auto v = detect_issue_backlog_spike(db, repo_id, days);
        candidates.insert(candidates.end(), v.begin(), v.end());
    }
    {
        auto v = detect_pr_merge_latency_spike(db, repo_id, days);
        candidates.insert(candidates.end(), v.begin(), v.end());
    }

    // 对未重复候选落库，并同步到知识库
    for (const auto& c : candidates) {
        if (has_today_open_alert(db, repo_id, c.alert_type)) {
            continue;
        }

        const int alert_id = insert_alert_event(db, repo_id, result.run_id, c);
        if (alert_id <= 0) continue;

        insert_alert_knowledge_chunk(db, repo_id, alert_id, c);

        RiskAlertEvent out;
        out.id = alert_id;
        out.repo_id = repo_id;
        out.alert_type = c.alert_type;
        out.metric_name = c.metric_name;
        out.window_start = c.window_start;
        out.window_end = c.window_end;
        out.current_value = c.current_value;
        out.baseline_value = c.baseline_value;
        out.threshold_value = c.threshold_value;
        out.severity = c.severity;
        out.scope_type = c.scope_type;
        out.scope_id = c.scope_id;
        out.suggested_action = c.suggested_action;
        out.status = "open";
        out.evidence_json = c.evidence_json;
        result.alerts.push_back(std::move(out));
    }

    result.alerts_created = static_cast<int>(result.alerts.size());
    result.success = true;

    nlohmann::json summary = {
        {"repo_id", repo_id},
        {"lookback_days", days},
        {"alerts_created", result.alerts_created}
    };

    if (result.run_id > 0) {
        finish_run(db, result.run_id, "ok", summary.dump(), "");
    }

    return result;
}

std::vector<RiskAlertEvent> list_risk_alerts(Db& db,
                                             int repo_id,
                                             const std::string& status,
                                             const std::string& severity,
                                             int limit,
                                             int offset)
{
    std::vector<RiskAlertEvent> items;

    std::string sql =
        "SELECT id, repo_id, alert_type, metric_name, window_start, window_end, "
        "current_value, baseline_value, threshold_value, severity, scope_type, scope_id, "
        "suggested_action, status, evidence_json, created_at "
        "FROM risk_alert_events WHERE repo_id=?1";

    int bind_idx = 2;
    bool has_status = !status.empty();
    bool has_severity = !severity.empty();

    if (has_status) {
        sql += " AND status=?" + std::to_string(bind_idx++);
    }
    if (has_severity) {
        sql += " AND severity=?" + std::to_string(bind_idx++);
    }

    sql += " ORDER BY id DESC LIMIT ?" + std::to_string(bind_idx++) +
           " OFFSET ?" + std::to_string(bind_idx++) + ";";

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return items;
    }

    int bi = 1;
    sqlite3_bind_int(stmt, bi++, repo_id);
    if (has_status) {
        sqlite3_bind_text(stmt, bi++, status.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (has_severity) {
        sqlite3_bind_text(stmt, bi++, severity.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bi++, std::max(1, std::min(500, limit)));
    sqlite3_bind_int(stmt, bi++, std::max(0, offset));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        items.push_back(read_alert_from_stmt(stmt));
    }

    sqlite3_finalize(stmt);
    return items;
}

std::string risk_alerts_to_json(const std::vector<RiskAlertEvent>& alerts)
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto& a : alerts) {
        j.push_back({
            {"id", a.id},
            {"repo_id", a.repo_id},
            {"alert_type", a.alert_type},
            {"metric_name", a.metric_name},
            {"window_start", a.window_start},
            {"window_end", a.window_end},
            {"current_value", a.current_value},
            {"baseline_value", a.baseline_value},
            {"threshold_value", a.threshold_value},
            {"severity", a.severity},
            {"scope_type", a.scope_type},
            {"scope_id", a.scope_id},
            {"suggested_action", a.suggested_action},
            {"status", a.status},
            {"evidence_json", a.evidence_json},
            {"created_at", a.created_at}
        });
    }
    return j.dump();
}

std::string risk_scan_result_to_json(const RiskScanResult& result)
{
    nlohmann::json j;
    j["success"] = result.success;
    j["repo_id"] = result.repo_id;
    j["run_id"] = result.run_id;
    j["alerts_created"] = result.alerts_created;
    if (!result.error.empty()) j["error"] = result.error;

    nlohmann::json items = nlohmann::json::array();
    for (const auto& a : result.alerts) {
        items.push_back({
            {"id", a.id},
            {"alert_type", a.alert_type},
            {"metric_name", a.metric_name},
            {"current_value", a.current_value},
            {"baseline_value", a.baseline_value},
            {"threshold_value", a.threshold_value},
            {"severity", a.severity},
            {"suggested_action", a.suggested_action}
        });
    }
    j["alerts"] = items;

    return j.dump();
}

std::string risk_alerts_summary_to_json(Db& db, int repo_id, int days)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    const int window_days = std::max(1, std::min(180, days));
    const std::string window = "-" + std::to_string(window_days) + " days";

    int total = 0;
    int critical = 0;
    int open = 0;

    const char* sql =
        "SELECT COUNT(*), "
        "SUM(CASE WHEN severity='critical' THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN status='open' THEN 1 ELSE 0 END) "
        "FROM risk_alert_events "
        "WHERE repo_id=?1 AND created_at >= datetime('now', ?2);";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        sqlite3_bind_text(stmt, 2, window.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            total = sqlite3_column_int(stmt, 0);
            critical = sqlite3_column_int(stmt, 1);
            open = sqlite3_column_int(stmt, 2);
        }
    }
    if (stmt) sqlite3_finalize(stmt);

    nlohmann::json top_types = nlohmann::json::array();
    const char* sql_top =
        "SELECT alert_type, COUNT(*) AS c "
        "FROM risk_alert_events "
        "WHERE repo_id=?1 AND created_at >= datetime('now', ?2) "
        "GROUP BY alert_type ORDER BY c DESC LIMIT 5;";

    stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql_top, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        sqlite3_bind_text(stmt, 2, window.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto t = sqlite3_column_text(stmt, 0);
            int c = sqlite3_column_int(stmt, 1);
            top_types.push_back({
                {"alert_type", t ? (const char*)t : ""},
                {"count", c}
            });
        }
    }
    if (stmt) sqlite3_finalize(stmt);

    nlohmann::json out = {
        {"repo_id", repo_id},
        {"days", window_days},
        {"total", total},
        {"critical", critical},
        {"open", open},
        {"top_types", top_types}
    };

    return out.dump();
}
