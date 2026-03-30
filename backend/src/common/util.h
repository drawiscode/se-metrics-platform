// filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/src/util.h
#pragma once

#include <string>
struct sqlite3;

namespace util {

// 读取环境变量，若不存在/为空则返回 def
std::string get_env(const char* name, const std::string& def);

// 最小 JSON 字符串转义（用于拼 JSON 响应）
std::string json_escape(const std::string& s);

// 执行一段 sql，失败抛异常（runtime_error）
void exec_sql(sqlite3* db, const std::string& sql);

// 读取 KEY=VALUE 的 env 文件并写入进程环境（忽略空行/#注释）
void load_env_file(const std::string& path);

} // namespace util