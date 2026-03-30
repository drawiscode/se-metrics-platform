#pragma once
#include "metrics.h"

struct HealthScore
{
    double score = 0.0;//综合得分，0-100
    double activity = 0.0;//活跃度，基于提交数和活跃贡献者数的得分
    double responsiveness = 0.0;//响应速度，基于平均 issue 关闭天数、打开的 issue 数和 PR 合并率的得分
    double quality = 0.0;//协作质量，基于 PR 合并率和最近合并的 PR 数的得分
    double release = 0.0;//发布节奏，基于最近 90 天的 release 数的得分
};

HealthScore compute_health_from_metrics(const RepoMetrics& m);
std::string repo_health_to_json(const HealthScore& health);
