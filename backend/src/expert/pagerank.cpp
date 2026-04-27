// 2.4(5) 团队洞察: 隐形专家识别 - PageRank 实现
#include "pagerank.h"
#include "db/db.h"
#include "common/util.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <sstream>

namespace {

// ============================================================
// 辅助: 提取文件路径的顶层目录（模块）
// ============================================================
std::string extract_module(const std::string& filepath, int depth = 1)
{
    if (filepath.empty()) return "";
    std::string result;
    int slashes = 0;
    for (size_t i = 0; i < filepath.size(); ++i) {
        if (filepath[i] == '/') {
            slashes++;
            if (slashes >= depth) {
                result = filepath.substr(0, i);
                return result;
            }
        }
    }
    // 没有足够的 '/'，返回整个目录名或文件名
    auto last_slash = filepath.rfind('/');
    if (last_slash != std::string::npos)
        return filepath.substr(0, last_slash);
    return filepath;
}

// ============================================================
// 辅助: 从 commits + commit_files 构建开发者协作图
// 返回: 邻接表 graph[A][B] = 权重 (A 和 B 共同修改过的模块数)
// ============================================================
struct DevInfo {
    int commit_count = 0;
    int files_touched = 0;
    std::string last_active;
    std::unordered_map<std::string, int> module_commits; // module -> commit count
};

using Graph = std::unordered_map<std::string, std::unordered_map<std::string, double>>;

void build_collaboration_graph(Db& db, int repo_id,
                               Graph& graph,
                               std::unordered_map<std::string, DevInfo>& dev_info)
{
    sqlite3* sdb = db.handle();

    // 1. 收集每个开发者的基本信息
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT author_login, COUNT(*) AS cnt, MAX(committed_at) AS last_active "
            "FROM commits WHERE repo_id=?1 AND author_login IS NOT NULL "
            "AND TRIM(author_login) <> '' "
            "GROUP BY author_login;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, repo_id);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                auto login_raw = sqlite3_column_text(stmt, 0);
                if (!login_raw) continue;
                std::string login = (const char*)login_raw;
                DevInfo& di = dev_info[login];
                di.commit_count = sqlite3_column_int(stmt, 1);
                auto la = sqlite3_column_text(stmt, 2);
                di.last_active = la ? (const char*)la : "";
            }
            sqlite3_finalize(stmt);
        }
    }

    // 2. 收集每个开发者在各模块的提交信息
    //    module -> set of developers
    std::unordered_map<std::string, std::unordered_set<std::string>> module_devs;
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT c.author_login, cf.filename, cf.additions + cf.deletions AS changes "
            "FROM commits c "
            "JOIN commit_files cf ON cf.commit_id = c.id "
            "WHERE c.repo_id=?1 AND c.author_login IS NOT NULL "
            "AND TRIM(c.author_login) <> '';";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, repo_id);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                auto login_raw = sqlite3_column_text(stmt, 0);
                auto file_raw = sqlite3_column_text(stmt, 1);
                if (!login_raw || !file_raw) continue;

                std::string login = (const char*)login_raw;
                std::string filepath = (const char*)file_raw;
                std::string mod = extract_module(filepath);

                dev_info[login].files_touched++;
                dev_info[login].module_commits[mod]++;
                module_devs[mod].insert(login);
            }
            sqlite3_finalize(stmt);
        }
    }

    // 3. 从共同模块构建协作边
    //    如果 A 和 B 都修改过同一个模块，建立 A<->B 边
    for (const auto& [mod, devs] : module_devs) {
        std::vector<std::string> dev_list(devs.begin(), devs.end());
        for (size_t i = 0; i < dev_list.size(); ++i) {
            for (size_t j = i + 1; j < dev_list.size(); ++j) {
                graph[dev_list[i]][dev_list[j]] += 1.0;
                graph[dev_list[j]][dev_list[i]] += 1.0;
            }
        }
    }

    // 4. 从 PR raw_json 提取 review 关系作为额外边
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT author_login, raw_json FROM pull_requests "
            "WHERE repo_id=?1 AND author_login IS NOT NULL;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, repo_id);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                auto author_raw = sqlite3_column_text(stmt, 0);
                auto json_raw = sqlite3_column_text(stmt, 1);
                if (!author_raw || !json_raw) continue;

                std::string pr_author = (const char*)author_raw;
                try {
                    auto j = nlohmann::json::parse((const char*)json_raw);
                    // 解析 requested_reviewers
                    if (j.contains("requested_reviewers") && j["requested_reviewers"].is_array()) {
                        for (const auto& reviewer : j["requested_reviewers"]) {
                            std::string reviewer_login = reviewer.value("login", "");
                            if (!reviewer_login.empty() && reviewer_login != pr_author) {
                                // reviewer -> author: review 关系，权重 2.0（比共同修改更强）
                                graph[reviewer_login][pr_author] += 2.0;
                                graph[pr_author][reviewer_login] += 2.0;
                                // 确保 reviewer 也出现在 dev_info 中
                                if (dev_info.find(reviewer_login) == dev_info.end()) {
                                    dev_info[reviewer_login] = {};
                                }
                            }
                        }
                    }
                } catch (...) {
                    // JSON 解析失败，跳过
                }
            }
            sqlite3_finalize(stmt);
        }
    }
}

// ============================================================
// PageRank 迭代算法
// ============================================================
std::unordered_map<std::string, double> run_pagerank(
    const Graph& graph,
    const std::unordered_map<std::string, DevInfo>& dev_info,
    double damping = 0.85,
    int iterations = 20)
{
    // 收集所有节点
    std::unordered_set<std::string> all_nodes;
    for (const auto& [node, _] : dev_info) all_nodes.insert(node);
    for (const auto& [node, edges] : graph) {
        all_nodes.insert(node);
        for (const auto& [neighbor, _] : edges) all_nodes.insert(neighbor);
    }

    int n = static_cast<int>(all_nodes.size());
    if (n == 0) return {};

    // 初始化分数
    std::unordered_map<std::string, double> scores;
    double init_score = 1.0 / n;
    for (const auto& node : all_nodes) {
        scores[node] = init_score;
    }

    // 预计算每个节点的出边权重总和
    std::unordered_map<std::string, double> out_weight;
    for (const auto& [node, edges] : graph) {
        double total = 0.0;
        for (const auto& [_, w] : edges) total += w;
        out_weight[node] = total;
    }

    // 迭代
    double base = (1.0 - damping) / n;
    for (int iter = 0; iter < iterations; ++iter) {
        std::unordered_map<std::string, double> new_scores;
        for (const auto& node : all_nodes) {
            new_scores[node] = base;
        }

        for (const auto& [node, edges] : graph) {
            double node_score = scores[node];
            double total_out = out_weight[node];
            if (total_out < 1e-12) continue;

            for (const auto& [neighbor, weight] : edges) {
                new_scores[neighbor] += damping * node_score * (weight / total_out);
            }
        }

        scores = std::move(new_scores);
    }

    return scores;
}

} // anonymous namespace

// ============================================================
// 公共接口: 计算全局 PageRank 专家排名
// ============================================================
std::vector<ExpertScore> compute_expert_pagerank(Db& db, int repo_id, int top_n)
{
    Graph graph;
    std::unordered_map<std::string, DevInfo> dev_info;
    build_collaboration_graph(db, repo_id, graph, dev_info);

    auto scores = run_pagerank(graph, dev_info);

    // 组装结果
    std::vector<ExpertScore> results;
    for (const auto& [login, pr_score] : scores) {
        ExpertScore es;
        es.login = login;
        es.pagerank = pr_score;

        auto it = dev_info.find(login);
        if (it != dev_info.end()) {
            es.commit_count = it->second.commit_count;
            es.files_touched = it->second.files_touched;
            es.last_active = it->second.last_active;

            // 找主要负责模块
            int max_mod_commits = 0;
            for (const auto& [mod, cnt] : it->second.module_commits) {
                if (cnt > max_mod_commits) {
                    max_mod_commits = cnt;
                    es.primary_module = mod;
                }
            }
        }

        results.push_back(std::move(es));
    }

    // 按 PageRank 得分降序
    std::sort(results.begin(), results.end(),
              [](const ExpertScore& a, const ExpertScore& b) {
                  return a.pagerank > b.pagerank;
              });

    if (static_cast<int>(results.size()) > top_n)
        results.resize(top_n);

    return results;
}

// ============================================================
// 公共接口: 按模块获取专家
// ============================================================
std::vector<ModuleExpert> compute_module_experts(Db& db, int repo_id,
                                                 const std::string& dir_prefix, int top_n)
{
    std::vector<ModuleExpert> results;
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "SELECT c.author_login, "
        "  COUNT(DISTINCT cf.filename) AS file_count, "
        "  SUM(cf.additions + cf.deletions) AS lines_changed "
        "FROM commits c "
        "JOIN commit_files cf ON cf.commit_id = c.id "
        "WHERE c.repo_id=?1 AND c.author_login IS NOT NULL "
        "  AND TRIM(c.author_login) <> '' "
        "  AND cf.filename LIKE ?2 "
        "GROUP BY c.author_login "
        "ORDER BY lines_changed DESC "
        "LIMIT ?3;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;
    sqlite3_bind_int(stmt, 1, repo_id);
    std::string pattern = dir_prefix + "%";
    sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, top_n);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ModuleExpert me;
        me.module_path = dir_prefix;
        auto login = sqlite3_column_text(stmt, 0);
        me.login = login ? (const char*)login : "";
        me.commit_count = sqlite3_column_int(stmt, 1);
        me.lines_changed = sqlite3_column_int(stmt, 2);
        results.push_back(std::move(me));
    }
    sqlite3_finalize(stmt);
    return results;
}

// ============================================================
// 公共接口: 将专家数据写入 knowledge_chunks
// ============================================================
int build_expert_knowledge(Db& db, int repo_id)
{
    // 先删除旧的 expert 类型知识块
    sqlite3* sdb = db.handle();
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "DELETE FROM knowledge_chunks WHERE repo_id=?1 AND source_type='expert';";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, repo_id);
            sqlite3_step(stmt);
        }
        if (stmt) sqlite3_finalize(stmt);
    }

    auto experts = compute_expert_pagerank(db, repo_id, 30);
    if (experts.empty()) return 0;

    int count = 0;
    const char* insert_sql =
        "INSERT INTO knowledge_chunks(repo_id, source_type, source_id, title, content, author, event_time) "
        "VALUES (?1, 'expert', ?2, ?3, ?4, ?5, ?6);";

    // 写入汇总知识块
    {
        std::ostringstream summary;
        summary << "Repository expert ranking (PageRank algorithm):\n";
        for (size_t i = 0; i < experts.size(); ++i) {
            const auto& e = experts[i];
            summary << (i + 1) << ". " << e.login
                    << " (PageRank=" << std::fixed << std::setprecision(4) << e.pagerank
                    << ", commits=" << e.commit_count
                    << ", files=" << e.files_touched;
            if (!e.primary_module.empty())
                summary << ", primary_module=" << e.primary_module;
            summary << ")\n";
        }

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(sdb, insert_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, repo_id);
            sqlite3_bind_text(stmt, 2, "summary", -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, "Expert Ranking Summary (PageRank)", -1, SQLITE_STATIC);
            std::string content = summary.str();
            sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, "", -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 6, "", -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_DONE) count++;
            sqlite3_finalize(stmt);
        }
    }

    // 为每个专家写入单独知识块
    for (const auto& e : experts) {
        std::ostringstream content;
        content << "Developer: " << e.login << "\n"
                << "PageRank score: " << std::fixed << std::setprecision(4) << e.pagerank << "\n"
                << "Total commits: " << e.commit_count << "\n"
                << "Files touched: " << e.files_touched << "\n";
        if (!e.primary_module.empty())
            content << "Primary module: " << e.primary_module << "\n";
        if (!e.last_active.empty())
            content << "Last active: " << e.last_active << "\n";

        std::string title = "Expert: " + e.login;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(sdb, insert_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, repo_id);
            sqlite3_bind_text(stmt, 2, e.login.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
            std::string c = content.str();
            sqlite3_bind_text(stmt, 4, c.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, e.login.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, e.last_active.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_DONE) count++;
            sqlite3_finalize(stmt);
        }
    }

    std::cerr << "[expert] repo_id=" << repo_id << " wrote " << count << " knowledge chunks\n";
    return count;
}

// ============================================================
// JSON 序列化
// ============================================================
std::string experts_to_json(const std::vector<ExpertScore>& experts)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : experts) {
        arr.push_back({
            {"login", e.login},
            {"pagerank", e.pagerank},
            {"commit_count", e.commit_count},
            {"files_touched", e.files_touched},
            {"last_active", e.last_active},
            {"primary_module", e.primary_module}
        });
    }
    return arr.dump();
}

std::string module_experts_to_json(const std::vector<ModuleExpert>& experts)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : experts) {
        arr.push_back({
            {"module_path", e.module_path},
            {"login", e.login},
            {"commit_count", e.commit_count},
            {"lines_changed", e.lines_changed}
        });
    }
    return arr.dump();
}
