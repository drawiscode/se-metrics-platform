// filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/src/github_client.h
#pragma once

#include <string>

struct GitHubResponse {
    int status = 0;
    std::string body;
    std::string error;
};

struct RepoSnapshotData {
    int stars = 0;
    int forks = 0;
    int open_issues = 0;
    int watchers = 0;
    std::string pushed_at;
    std::string raw_json;
};

struct RepoIssueData {
    int number = 0;
    std::string state;
    std::string title;
    std::string created_at;
    std::string updated_at;
    std::string closed_at;
    int comments = 0;
    std::string author_login;
    bool is_pull_request = false;
    std::string raw_json;
};

struct RepoPullRequestData {
    int number = 0;
    std::string state;
    std::string title;
    std::string created_at;
    std::string updated_at;
    std::string closed_at;
    std::string merged_at;
    std::string author_login;
    std::string raw_json;
};

void Judge_GitHub_Token(const std::string& token);


bool fetch_repo_snapshot_from_github(const GitHubResponse& gh,
                                            const std::string& full_name,
                                            const std::string& token,
                                            RepoSnapshotData& out,
                                            std::string& error_out,
                                            int& http_status_out) ;

bool fetch_repo_issue_from_github(const GitHubResponse& gh,
                                            const std::string& full_name,
                                            const std::string& token,
                                            RepoIssueData& out,
                                            std::string& error_out,
                                            int& http_status_out) ;

bool fetch_repo_pull_from_github(const GitHubResponse& gh,
                                            const std::string& full_name,
                                            const std::string& token,
                                            RepoPullRequestData& out,
                                            std::string& error_out,
                                            int& http_status_out) ;

GitHubResponse github_get_repo(const std::string& full_name, const std::string& token);
