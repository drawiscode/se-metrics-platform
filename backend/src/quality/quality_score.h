#pragma once

#include <map>
#include <string>

class Db;

struct QualityScore
{
    double score = 0.0;
    double penalty = 0.0;
    int total_issues = 0;
    int files_with_issues = 0;
    int lines_analyzed = 0;
    std::map<std::string, int> severity_counts;
};

QualityScore compute_quality_score(Db& db, int repo_id, const std::string& tool);
QualityScore compute_quality_score_for_run(Db& db, int run_id);
std::string quality_score_to_json(const QualityScore& q);
