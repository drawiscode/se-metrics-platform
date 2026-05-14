#include "quality_score.h"
#include "common/util.h"
#include "db/db.h"

#include <sqlite3.h>
#include <algorithm>
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
    if (s == "warning") return 6.0;
    if (s == "performance") return 4.0;
    if (s == "portability") return 4.0;
    if (s == "style") return 2.0;
    if (s == "information") return 0.5;
    return 1.0;
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
            "SELECT severity, COUNT(*) "
            "FROM quality_issues WHERE repo_id=?1 "
            "AND (?2='' OR tool=?2) "
            "AND (status IS NULL OR status='' OR status='active') "
            "GROUP BY severity;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, repo_id);
            sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* sev = sqlite3_column_text(stmt, 0);
                int count = sqlite3_column_int(stmt, 1);
                std::string key = sev ? (const char*)sev : "";
                q.severity_counts[key] = count;
                q.penalty += severity_weight(key) * count;
            }
        }
        sqlite3_finalize(stmt);
    }

    if (q.total_issues == 0) {
        q.score = 100.0;
        return q;
    }

    const int denom = std::max(1, q.files_with_issues);
    const double penalty_per_file = q.penalty / static_cast<double>(denom);
    q.score = clamp(100.0 - penalty_per_file * 2.0, 0.0, 100.0);
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
