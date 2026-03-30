#pragma once
#include <string>
#include <vector>
#include <httplib.h>
struct HotFile {
    std::string filename;
    int commits = 0;
    int additions = 0;
    int deletions = 0;
    int churn() const { return additions + deletions; }
};

struct HotDir {
    std::string dir;
    int commits = 0;
    int additions = 0;
    int deletions = 0;
    int churn() const { return additions + deletions; }
};

struct TimePoint {
    std::string day; // "YYYY-MM-DD"
    int churn = 0;
};

struct HotModule {
    std::string module;      // directory or module id
    int churn = 0;
    double complexity = 0.0; // from static analysis
    double score = 0.0;      // combined score for ranking
};

static std::string dirname_depth(const std::string& path, int depth);
std::vector<HotFile> compute_hot_files(Db& db, httplib::Response& res, int repo_id, int days_window, int top_n);
std::vector<HotDir> compute_hot_dirs(Db& db, int repo_id, int days_window, int top_n, int dir_depth);
