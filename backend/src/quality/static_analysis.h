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
    int run_id = 0;
    int task_id = 0;
    std::string status;
    int analyzed_files = 0;
    int lines_analyzed = 0;
    int issues_inserted = 0;
    int issues_new = 0;
    int issues_fixed = 0;
    std::map<std::string, int> severity_stats;
    std::map<std::string, std::string> output_files;
    std::string error;
};

QualityAnalysisResult run_static_analysis(Db& db,
                                          int repo_id,
                                          const std::string& full_name,
                                          const std::string& ref,
                                          const std::string& tool,
                                          const std::string& mode,
                                          int max_files);

QualityAnalysisResult run_static_analysis_task(Db& db,
                                               int repo_id,
                                               int task_id,
                                               const std::string& full_name,
                                               const std::string& ref,
                                               const std::string& tools,
                                               const std::string& mode,
                                               int max_files,
                                               const std::string& config_json);

std::string quality_result_to_json(const QualityAnalysisResult& r);
