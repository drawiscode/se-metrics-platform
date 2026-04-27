// 2.4(5) 团队洞察: 隐形专家识别 (PageRank)
// 从开发者协作网络中识别关键贡献者，辅助任务分派与知识传承
#pragma once

#include <string>
#include <vector>

class Db;

// 单个开发者的专家评分
struct ExpertScore {
    std::string login;           // 开发者 GitHub 登录名
    double pagerank = 0.0;       // PageRank 得分
    int commit_count = 0;        // 总提交数
    int files_touched = 0;       // 涉及文件数
    std::string last_active;     // 最近一次提交时间
    std::string primary_module;  // 主要负责模块（提交最多的顶层目录）
};

// 模块级专家信息
struct ModuleExpert {
    std::string module_path;     // 模块目录路径
    std::string login;           // 开发者
    int commit_count = 0;        // 在该模块的提交数
    int lines_changed = 0;       // 在该模块的变更行数
};

// 计算全局 PageRank 专家排名
std::vector<ExpertScore> compute_expert_pagerank(Db& db, int repo_id, int top_n = 20);

// 查询某模块目录下的专家排名
std::vector<ModuleExpert> compute_module_experts(Db& db, int repo_id,
                                                 const std::string& dir_prefix, int top_n = 10);

// 将专家数据写入 knowledge_chunks 供 RAG 引用
int build_expert_knowledge(Db& db, int repo_id);

// JSON 序列化
std::string experts_to_json(const std::vector<ExpertScore>& experts);
std::string module_experts_to_json(const std::vector<ModuleExpert>& experts);
