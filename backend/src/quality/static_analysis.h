#pragma once

#include <string>
#include <map>
#include <vector>

class Db;

struct QualityIssue {
    std::string file_path;
    int line = 0;
    int column = 0;
    std::string rule_id;
    std::string severity;
    std::string message;
};

struct QualityAnalysisResult {
    std::string tool;
    int analyzed_files = 0;
    int issues_inserted = 0;
    std::map<std::string, int> severity_stats;
    std::string error;
};

QualityAnalysisResult run_static_analysis(Db& db,
                                          int repo_id,
                                          const std::string& full_name,
                                          const std::string& ref,
                                          const std::string& tool,
                                          const std::string& mode,
                                          int max_files);

std::string quality_result_to_json(const QualityAnalysisResult& r);
