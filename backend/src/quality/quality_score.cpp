#include "quality_score.h"
#include "common/util.h"
#include "db/db.h"

#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace {

double clamp(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

double severity_weight(const std::string& sev)
{
    const std::string s = to_lower(util::trim(sev));
    if (s == "error") return 10.0;
    if (s == "warning") return 4.0;
    if (s == "performance") return 3.0;
    if (s == "portability") return 3.0;
    if (s == "style") return 0.4;
    if (s == "information") return 0.1;
    return 1.0;
}

double tool_weight(const std::string& tool)
{
    const std::string t = to_lower(util::trim(tool));
    if (t == "cpplint") return 0.08;
    if (t == "flawfinder") return 0.75;
    return 1.0;
}

double score_from_penalty(double penalty, int lines_analyzed, int files_with_issues)
{
    if (penalty <= 0.0) return 100.0;

    double density = 0.0;
    if (lines_analyzed > 0) {
        density = penalty * 1000.0 / static_cast<double>(lines_analyzed);
    } else {
        density = penalty / static_cast<double>(std::max(1, files_with_issues));
    }
    return clamp(100.0 - 10.0 * std::log1p(density), 0.0, 100.0);
}

int latest_lines_analyzed(Db& db, int repo_id, const std::string& tool)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    const char* sql = nullptr;
    if (tool.empty()) {
        sql =
            "SELECT MAX(lines_analyzed) FROM quality_analysis_runs "
            "WHERE repo_id=?1 AND status='Finished' AND lines_analyzed>0 "
            "AND instr(tools, ',')>0;";
    } else {
        sql =
            "SELECT lines_analyzed FROM quality_analysis_runs "
            "WHERE repo_id=?1 AND status='Finished' AND lines_analyzed>0 AND tools=?2 "
            "ORDER BY started_at DESC, id DESC LIMIT 1;";
    }

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, repo_id);
    if (!tool.empty()) sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
    int lines = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        lines = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    if (lines > 0) return lines;

    if (tool.empty()) {
        const char* any_sql =
            "SELECT MAX(lines_analyzed) FROM quality_analysis_runs "
            "WHERE repo_id=?1 AND status='Finished' AND lines_analyzed>0;";
        if (sqlite3_prepare_v2(sdb, any_sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
        sqlite3_bind_int(stmt, 1, repo_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            lines = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return lines;
    }

    const char* fallback_sql =
        "SELECT lines_analyzed FROM quality_analysis_runs "
        "WHERE repo_id=?1 AND status='Finished' AND lines_analyzed>0 "
        "AND instr(',' || replace(tools, ' ', '') || ',', ',' || ?2 || ',')>0 "
        "ORDER BY started_at DESC, id DESC LIMIT 1;";
    if (sqlite3_prepare_v2(sdb, fallback_sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        lines = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return lines;
}

}

QualityScore compute_quality_score(Db& db, int repo_id, const std::string& tool)
{
    QualityScore q;
    sqlite3* sdb = db.handle();

    // Total issues + distinct files
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT COUNT(*), COUNT(DISTINCT file_path) "
            "FROM quality_issues WHERE repo_id=?1 "
            "AND (?2='' OR tool=?2) "
            "AND (status IS NULL OR status='' OR status='active');";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, repo_id);
            sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                q.total_issues = sqlite3_column_int(stmt, 0);
                q.files_with_issues = sqlite3_column_int(stmt, 1);
            }
        }
        sqlite3_finalize(stmt);
    }

    // Severity distribution
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT tool, severity, COUNT(*) "
            "FROM quality_issues WHERE repo_id=?1 "
            "AND (?2='' OR tool=?2) "
            "AND (status IS NULL OR status='' OR status='active') "
            "GROUP BY tool, severity;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, repo_id);
            sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* tool_txt = sqlite3_column_text(stmt, 0);
                const unsigned char* sev = sqlite3_column_text(stmt, 1);
                int count = sqlite3_column_int(stmt, 2);
                std::string tool_key = tool_txt ? reinterpret_cast<const char*>(tool_txt) : "";
                std::string key = sev ? reinterpret_cast<const char*>(sev) : "";
                q.severity_counts[key] += count;
                q.penalty += severity_weight(key) * tool_weight(tool_key) * count;
            }
        }
        sqlite3_finalize(stmt);
    }

    if (q.total_issues == 0) {
        q.score = 100.0;
        return q;
    }

    q.lines_analyzed = latest_lines_analyzed(db, repo_id, tool);
    q.score = score_from_penalty(q.penalty, q.lines_analyzed, q.files_with_issues);
    return q;
}

QualityScore compute_quality_score_for_run(Db& db, int run_id)
{
    QualityScore q;
    sqlite3* sdb = db.handle();

    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT lines_analyzed FROM quality_analysis_runs WHERE id=?1;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, run_id);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                q.lines_analyzed = sqlite3_column_int(stmt, 0);
            }
        }
        sqlite3_finalize(stmt);
    }

    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT COUNT(*), COUNT(DISTINCT file_path) "
            "FROM quality_run_issues WHERE run_id=?1;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, run_id);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                q.total_issues = sqlite3_column_int(stmt, 0);
                q.files_with_issues = sqlite3_column_int(stmt, 1);
            }
        }
        sqlite3_finalize(stmt);
    }

    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT tool, severity, COUNT(*) "
            "FROM quality_run_issues WHERE run_id=?1 "
            "GROUP BY tool, severity;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, run_id);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* tool_txt = sqlite3_column_text(stmt, 0);
                const unsigned char* sev = sqlite3_column_text(stmt, 1);
                int count = sqlite3_column_int(stmt, 2);
                std::string tool_key = tool_txt ? reinterpret_cast<const char*>(tool_txt) : "";
                std::string key = sev ? reinterpret_cast<const char*>(sev) : "";
                q.severity_counts[key] += count;
                q.penalty += severity_weight(key) * tool_weight(tool_key) * count;
            }
        }
        sqlite3_finalize(stmt);
    }

    q.score = score_from_penalty(q.penalty, q.lines_analyzed, q.files_with_issues);
    return q;
}

std::string quality_score_to_json(const QualityScore& q)
{
    std::ostringstream oss;
    oss << "{"
        << "\"score\":" << q.score
        << ",\"penalty\":" << q.penalty
        << ",\"total_issues\":" << q.total_issues
        << ",\"files_with_issues\":" << q.files_with_issues
        << ",\"lines_analyzed\":" << q.lines_analyzed
        << ",\"severity\":{";

    bool first = true;
    for (const auto& kv : q.severity_counts) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << util::json_escape(kv.first) << "\":" << kv.second;
    }

    oss << "}}";
    return oss.str();
}
