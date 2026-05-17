#include "system_log.h"

#include "db/db.h"

#include <sqlite3.h>

namespace system_log {

void write_operation(Db& db,
                     const std::string& operation_type,
                     const std::string& target,
                     const std::string& status,
                     int duration_ms,
                     const std::string& ip,
                     const std::string& detail_json)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO system_operation_logs(operation_type, target, status, duration_ms, ip, detail_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6);";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(stmt, 1, operation_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, target.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, duration_ms > 0 ? duration_ms : 0);
    sqlite3_bind_text(stmt, 5, ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, detail_json.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void write_ai_usage(Db& db,
                    int repo_id,
                    const std::string& repo_full_name,
                    const std::string& model,
                    int prompt_tokens,
                    int completion_tokens,
                    int total_tokens,
                    double cost_usd,
                    int duration_ms,
                    const std::string& ip,
                    const std::string& status,
                    const std::string& error)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO system_ai_usage_logs(" 
        "repo_id, repo_full_name, model, prompt_tokens, completion_tokens, total_tokens, "
        "cost_usd, duration_ms, ip, status, error) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }

    if (repo_id > 0) sqlite3_bind_int(stmt, 1, repo_id);
    else sqlite3_bind_null(stmt, 1);
    sqlite3_bind_text(stmt, 2, repo_full_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, prompt_tokens > 0 ? prompt_tokens : 0);
    sqlite3_bind_int(stmt, 5, completion_tokens > 0 ? completion_tokens : 0);
    sqlite3_bind_int(stmt, 6, total_tokens > 0 ? total_tokens : 0);
    sqlite3_bind_double(stmt, 7, cost_usd > 0 ? cost_usd : 0.0);
    sqlite3_bind_int(stmt, 8, duration_ms > 0 ? duration_ms : 0);
    sqlite3_bind_text(stmt, 9, ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, error.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

} // namespace system_log
