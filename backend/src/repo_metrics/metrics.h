#pragma once
#include <string>
#include <map>

class Db;

struct RepoMetrics//Metrics:度量工具，度量指标
{
    int commits_last_7d = 0;//最近7天的提交数
    int active_contributors_30d = 0;//最近30天的活跃贡献者数
    int open_issues = 0;//当前打开的issue数
    double avg_issue_close_days = 0.0;//平均每个issue关闭需要的天数
    int prs_merged_last_30d = 0;//最近30天合并的PR数
    double prs_merge_rate = 0.0; //最近30天合并的PR数占总PR数的比例
    int releases_last_90d = 0;//最近90天的release数
    std::string last_push;//最近一次push的时间
    //按需扩展:

};

RepoMetrics compute_repo_metrics(Db& db, int repo_id);
std::string repo_metrics_to_json(const RepoMetrics& metrics);// 返回json字符串