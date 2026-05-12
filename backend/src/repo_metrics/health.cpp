#include "health.h"

#include <cmath>


HealthScore compute_health_from_metrics(const RepoMetrics& m)
{
    //clamp函数用于将一个值限制在指定的范围内，如果值小于下限则返回下限，如果值大于上限则返回上限，否则返回原值
    auto clamp = [](double v, double lo, double hi)
    {
        return std::max(lo, std::min(hi, v));
    };

    // Activity: 基于最近 7 天提交数和 30 天活跃贡献者数（对数缩放）
    // commits: 20 commits -> 100
    // contributors: 10 contributors -> 100
    double commits_score = clamp(std::log1p(m.commits_last_7d) / std::log(21.0) * 100.0, 0.0, 100.0);
    double contrib_score = clamp(std::log1p(m.active_contributors_30d) / std::log(11.0) * 100.0, 0.0, 100.0);
    double activity = commits_score * 0.6 + contrib_score * 0.4;

    // Responsiveness: 基于平均 issue 关闭天数、打开的 issue 数和 PR 合并率
    // avg_issue_close_days: 越小越好，0 天 -> 100，20 天 -> 0（线性映射）
    double close_days_score = clamp(100.0 - m.avg_issue_close_days * 4.0, 0.0, 100.0);
    // open_issues: 问题越多越差（适度惩罚）
    double open_issues_score = clamp(100.0 - m.open_issues * 0.8, 0.0, 100.0);
    double prs_merge_rate_pct = clamp(m.prs_merge_rate * 100.0, 0.0, 100.0);
    double responsiveness = close_days_score * 0.5 + open_issues_score * 0.2 + prs_merge_rate_pct * 0.3;

    // Quality: 主要看 PR 合并率和最近合并的 PR 数（对数缩放）
    // prs_merged_last_30d: 50 merges -> 100
    double merged_prs_score = clamp(std::log1p(m.prs_merged_last_30d) / std::log(51.0) * 100.0, 0.0, 100.0);
    double quality = clamp(prs_merge_rate_pct * 0.6 + merged_prs_score * 0.4, 0.0, 100.0);

    // Release cadence: 最近 90 天的 release 数（对数缩放）
    // 5 releases -> 100
    double release = clamp(std::log1p(m.releases_last_90d) / std::log(6.0) * 100.0, 0.0, 100.0);

    // 总分：按权重合成（可按需调整权重）
    double score = clamp(activity * 0.25 + responsiveness * 0.35 + quality * 0.25 + release * 0.15, 0.0, 100.0);

    HealthScore hs;
    hs.activity = activity;
    hs.responsiveness = responsiveness;
    hs.quality = quality;
    hs.release = release;
    hs.score = score;
    return hs;
}

std::string repo_health_to_json(const HealthScore& health)
{
    return std::string("{")
        + "\"score\":" + std::to_string(health.score) + ","
        + "\"activity\":" + std::to_string(health.activity) + ","
        + "\"responsiveness\":" + std::to_string(health.responsiveness) + ","
        + "\"quality\":" + std::to_string(health.quality) + ","
        + "\"release\":" + std::to_string(health.release)
        + "}";
}

