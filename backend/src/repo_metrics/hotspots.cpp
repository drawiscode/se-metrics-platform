#include "db/db.h"
#include "common/util.h"
#include "github_client.h"
#include "hotspots.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>
#include <map>
#include <algorithm>


using nlohmann::json;
namespace fs = std::filesystem;

// helper: dirname with depth (1 -> top-level dir, 2 -> parent/child etc.)
static std::string dirname_depth(const std::string& path, int depth) {
    if (path.empty() || depth <= 0) return "";
    fs::path p(path);
    std::vector<std::string> parts;
    for (auto it = p.begin(); it != p.end(); ++it) parts.emplace_back(it->string());
    if (parts.empty()) return "";
    int take = std::min((int)parts.size(), depth);
    std::ostringstream os;
    for (int i = 0; i < take; ++i) {
        if (i) os << "/";
        os << parts[i];
    }
    return os.str();
}

// 聚合 top N 文件（按 churn 降序），days_window 可用于时间过滤
std::vector<HotFile> compute_hot_files(Db& db, httplib::Response& res, int repo_id, int days_window, int top_n)
{
    std::vector<HotFile> out;
    sqlite3* sdb = db.handle();
    if (!sdb) {
        res.status = 500;
        res.set_content(R"({"error":"db handle is null"})", "application/json");
        return out;
    }

   // 修改：将 commit_files 与 commits 关联，按 commit_id 聚合，并从 commits 表读取 committed_at / repo_id
    const char* sql_all =
        "SELECT cf.filename, COUNT(DISTINCT cf.commit_id) AS commits, SUM(cf.additions) AS adds, SUM(cf.deletions) AS dels "
        "FROM commit_files cf JOIN commits c ON cf.commit_id = c.id "
        "WHERE c.repo_id = ?1 GROUP BY cf.filename ORDER BY (adds + dels) DESC LIMIT ?2;";

    const char* sql_days =
        "SELECT cf.filename, COUNT(DISTINCT cf.commit_id) AS commits, SUM(cf.additions) AS adds, SUM(cf.deletions) AS dels "
        "FROM commit_files cf JOIN commits c ON cf.commit_id = c.id "
        "WHERE c.repo_id = ?1 AND c.committed_at >= datetime('now', ?2) "
        "GROUP BY cf.filename ORDER BY (adds + dels) DESC LIMIT ?3;";

    sqlite3_stmt* stmt = nullptr;
    const char* used = (days_window > 0) ? sql_days : sql_all;
    if (sqlite3_prepare_v2(sdb, used, -1, &stmt, nullptr) != SQLITE_OK) 
    {
        const char* msg = sqlite3_errmsg(sdb);
        res.status = 500;
        std::string err = std::string("{\"error\":\"db prepare failed\",\"detail\":\"") + (msg ? msg : "unknown") + "\"}";
        res.set_content(err, "application/json");
        return out;
    }
    sqlite3_bind_int(stmt, 1, repo_id);
    int bi = 2;
    if (days_window > 0) 
    {
        std::string delta = std::string("-") + std::to_string(days_window) + " days";
        sqlite3_bind_text(stmt, bi++, delta.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bi++, top_n);

    while (sqlite3_step(stmt) == SQLITE_ROW) 
    {
        HotFile hf;
        const unsigned char* fn = sqlite3_column_text(stmt, 0);
        hf.filename = fn ? reinterpret_cast<const char*>(fn) : "";
        hf.commits = sqlite3_column_int(stmt, 1);
        hf.additions = sqlite3_column_int(stmt, 2);
        hf.deletions = sqlite3_column_int(stmt, 3);
        out.push_back(std::move(hf));
    }
    sqlite3_finalize(stmt);
    return out;
}

// 按目录聚合 top N（dir_depth 指定层级）
std::vector<HotDir> compute_hot_dirs(Db& db, httplib::Response& res, int repo_id, int days_window, int top_n, int dir_depth)
{
    std::vector<HotDir> out;
    sqlite3* sdb = db.handle();
    if (!sdb) 
    {
        res.status = 500;
        res.set_content(R"({"error":"db handle is null"})", "application/json");
        return out;
    }

    const char* sql_days =
        "SELECT cf.filename, COUNT(DISTINCT cf.commit_id) AS commits, SUM(cf.additions) AS adds, SUM(cf.deletions) AS dels "
        "FROM commit_files cf JOIN commits c ON cf.commit_id = c.id "
        "WHERE c.repo_id=?1 AND c.committed_at >= datetime('now', ?2) GROUP BY cf.filename;";
    const char* sql_all =
        "SELECT cf.filename, COUNT(DISTINCT cf.commit_id) AS commits, SUM(cf.additions) AS adds, SUM(cf.deletions) AS dels "
        "FROM commit_files cf JOIN commits c ON cf.commit_id = c.id "
        "WHERE c.repo_id=?1 GROUP BY cf.filename;";

    sqlite3_stmt* stmt = nullptr;
    const char* used = (days_window > 0) ? sql_days : sql_all;
    if (sqlite3_prepare_v2(sdb, used, -1, &stmt, nullptr) != SQLITE_OK) return out;

    sqlite3_bind_int(stmt, 1, repo_id);
    if (days_window > 0) 
    {
        std::string delta = std::string("-") + std::to_string(days_window) + " days";
        sqlite3_bind_text(stmt, 2, delta.c_str(), -1, SQLITE_TRANSIENT);
    }

    std::map<std::string, HotDir> mapdirs;
    while (sqlite3_step(stmt) == SQLITE_ROW) 
    {
        const unsigned char* fn = sqlite3_column_text(stmt, 0);
        std::string filename = fn ? reinterpret_cast<const char*>(fn) : "";
        int commits = sqlite3_column_int(stmt, 1);
        int adds = sqlite3_column_int(stmt, 2);
        int dels = sqlite3_column_int(stmt, 3);

        std::string dir = dirname_depth(filename, dir_depth);
        if (dir.empty()) dir = ".";
        auto &entry = mapdirs[dir];
        entry.dir = dir;
        entry.commits += commits;
        entry.additions += adds;
        entry.deletions += dels;
    }
    sqlite3_finalize(stmt);

    for (auto &kv : mapdirs) out.push_back(kv.second);
    std::sort(out.begin(), out.end(), [](const HotDir& a, const HotDir& b){
        return (a.churn()) > (b.churn());
    });
    if ((int)out.size() > top_n) out.resize(top_n);
    return out;
}
