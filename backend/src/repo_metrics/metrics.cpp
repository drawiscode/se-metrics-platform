#include "metrics.h"
#include "db/db.h"
#include "common/util.h"
#include <sqlite3.h>
#include <sstream>
#include <iomanip>

static int exec_scalar_int(sqlite3* db, const char* sql, int repo_id) 
{
    sqlite3_stmt* stmt = nullptr;
    int result = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) result = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

static double exec_scalar_double(sqlite3* db, const char* sql, int repo_id) 
{
    sqlite3_stmt* stmt = nullptr;
    double result = 0.0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) result = sqlite3_column_double(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

static std::string exec_scalar_text(sqlite3* db, const char* sql, int repo_id) 
{
    sqlite3_stmt* stmt = nullptr;
    std::string result;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* txt = sqlite3_column_text(stmt, 0);
            if (txt) result = reinterpret_cast<const char*>(txt);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}


RepoMetrics compute_repo_metrics(Db& db, int repo_id) 
{
   RepoMetrics out;
    sqlite3* sdb = db.handle();

    out.commits_last_7d = exec_scalar_int(sdb, "SELECT COUNT(*) FROM commits WHERE repo_id=?1 AND committed_at >= datetime('now','-7 days');", repo_id);
    out.active_contributors_30d = exec_scalar_int(sdb, "SELECT COUNT(DISTINCT author_login) FROM commits WHERE repo_id=?1 AND committed_at >= datetime('now','-30 days');", repo_id);
    out.open_issues = exec_scalar_int(sdb, "SELECT COUNT(*) FROM issues WHERE repo_id=?1 AND state='open';", repo_id);
    out.avg_issue_close_days = exec_scalar_double(sdb,
        "SELECT AVG((julianday(closed_at)-julianday(created_at))) "
        "FROM issues WHERE repo_id=?1 AND state='closed' AND closed_at >= datetime('now','-90 days');",
        repo_id);

    int total = exec_scalar_int(sdb, "SELECT COUNT(*) FROM pull_requests WHERE repo_id=?1 AND created_at >= datetime('now','-30 days');", repo_id);
    int merged = exec_scalar_int(sdb, "SELECT COUNT(*) FROM pull_requests WHERE repo_id=?1 AND merged_at IS NOT NULL AND merged_at >= datetime('now','-30 days');", repo_id);
    out.prs_merged_last_30d = merged;
    out.prs_merge_rate = total > 0 ? static_cast<double>(merged) / total : 0.0;

    out.releases_last_90d = exec_scalar_int(sdb, "SELECT COUNT(*) FROM releases WHERE repo_id=?1 AND published_at >= datetime('now','-90 days');", repo_id);
    out.last_push = exec_scalar_text(sdb, "SELECT pushed_at FROM repo_snapshots WHERE repo_id=?1 AND pushed_at IS NOT NULL ORDER BY id DESC LIMIT 1;", repo_id);

    return out;
}

std::string repo_metrics_to_json(const RepoMetrics& m) {
    std::ostringstream os;
    os << "{\"commits_last_7d\":" << m.commits_last_7d
       << ",\"active_contributors_30d\":" << m.active_contributors_30d
       << ",\"open_issues\":" << m.open_issues
       << ",\"avg_issue_close_days\":" << std::fixed << std::setprecision(2) << m.avg_issue_close_days
       << ",\"prs_merged_last_30d\":" << m.prs_merged_last_30d
       << ",\"prs_merge_rate\":" << m.prs_merge_rate
       << ",\"releases_last_90d\":" << m.releases_last_90d
       << ",\"last_push\":\"" << util::json_escape(m.last_push) << "\""
       << "}";
    return os.str();
}