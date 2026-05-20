#pragma once

#include <string>

class Db;

namespace system_log {

void write_operation(Db& db,
                     const std::string& operation_type,
                     const std::string& target,
                     const std::string& status,
                     int duration_ms,
                     const std::string& ip,
                     const std::string& detail_json = "{}");

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
                    const std::string& error = "");

} // namespace system_log
