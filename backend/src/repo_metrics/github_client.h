// filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/src/github_client.h
#pragma once

#include <string>
#include <vector>

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
    int comments = 0; // GitHub PR 有 comments 字段(类 issue comments)，先留
    std::string author_login;
    std::string raw_json;
};

void Judge_GitHub_Token(const std::string& token);


bool fetch_repo_snapshot_from_github(const GitHubResponse& gh,
                                            RepoSnapshotData& out,
                                            std::string& error_out,
                                            int& http_status_out) ;
// 新增：解析 issues 列表（gh.body 必须是 JSON array）
bool parse_repo_issues_from_github(const GitHubResponse& gh,
                                  std::vector<RepoIssueData>& out,
                                  std::string& error_out,
                                  int& http_status_out);

// 新增：解析 pulls 列表（gh.body 必须是 JSON array）
bool parse_repo_pulls_from_github(const GitHubResponse& gh,
                                 std::vector<RepoPullRequestData>& out,
                                 std::string& error_out,
                                 int& http_status_out);


// 新增：列出 issues（返回 JSON array 字符串）
GitHubResponse github_list_issues(const std::string& full_name,
                                 const std::string& token,
                                 const std::string& state /* "open"|"closed"|"all" */,
                                 int per_page,
                                 int page,
                                 const std::string& since_iso8601 /*可空*/);

// 新增：列出 pull requests（返回 JSON array 字符串）
GitHubResponse github_list_pulls(const std::string& full_name,
                                const std::string& token,
                                const std::string& state /* "open"|"closed"|"all" */,
                                int per_page,
                                int page,
                                const std::string& since_iso8601 /*可空*/);

GitHubResponse github_get_repo(const std::string& full_name, const std::string& token);
