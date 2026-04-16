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

struct RepoCommitData {
    std::string sha;
    std::string author_login;   // 可能为空
    std::string committed_at;   // commit.committer.date
    std::string raw_json;
};

struct CommitFileData {
    std::string sha;
    std::string filename;
    int additions = 0;
    int deletions = 0;
    int changes = 0;
    std::string committed_at; // ISO time
    std::string raw_json;
};


struct RepoReleaseData {
    std::string tag_name;       // 必填
    std::string name;           // 可空
    int draft = 0;              // 0/1
    int prerelease = 0;         // 0=非预发布, 1=预发布, 对应 GitHub API 的 prerelease 字段

    std::string published_at;   // 可空（draft 可能为空）
    std::string raw_json;
};

struct WorkflowRunData {
    long long run_id = 0;
    long long workflow_id = 0;
    std::string name;
    std::string head_branch;
    std::string event;
    std::string status;
    std::string conclusion;
    std::string created_at;
    std::string updated_at;
    std::string run_started_at;
    std::string html_url;
    std::string actor_login;
    int run_attempt = 0;
    std::string raw_json;
};

void Judge_GitHub_Token(const std::string& token);


bool fetch_repo_snapshot_from_github(const GitHubResponse& gh,
                                            RepoSnapshotData& out,
                                            std::string& error_out,
                                            int& http_status_out) ;

// 解析 issues 列表（gh.body 必须是 JSON array）
bool parse_repo_issues_from_github(const GitHubResponse& gh,
                                  std::vector<RepoIssueData>& out,
                                  std::string& error_out,
                                  int& http_status_out);

// 解析 pulls 列表（gh.body 必须是 JSON array）
bool parse_repo_pulls_from_github(const GitHubResponse& gh,
                                 std::vector<RepoPullRequestData>& out,
                                 std::string& error_out,
                                 int& http_status_out);
// 解析 commits 列表（gh.body 必须是 JSON array）
bool parse_repo_commits_from_github(const GitHubResponse& gh,
                                   std::vector<RepoCommitData>& out,
                                   std::string& error_out,
                                   int& http_status_out);

bool parse_commit_files_from_github(const GitHubResponse& gh,
                                    std::vector<CommitFileData>& out,
                                    std::string& error_out,
                                    int& http_status_out);


// 解析 releases 列表（gh.body 必须是 JSON array）
bool parse_repo_releases_from_github(const GitHubResponse& gh,
                                    std::vector<RepoReleaseData>& out,
                                    std::string& error_out,
                                    int& http_status_out);

bool parse_pull_from_github_json(const GitHubResponse& gh,
                                       RepoPullRequestData& out_pr,
                                       std::string& err,
                                       int& http_status);

bool parse_workflow_runs_from_github(const GitHubResponse& gh,
                                     std::vector<WorkflowRunData>& out,
                                     std::string& error_out,
                                     int& http_status_out);

GitHubResponse github_list_workflow_runs(const std::string& full_name,
                                         const std::string& token,
                                         int per_page,
                                         int page,
                                         const std::string& status /*可空*/,
                                         const std::string& event /*可空*/,
                                         const std::string& branch /*可空*/);

// 列出 issues（返回 JSON array 字符串）
GitHubResponse github_list_issues(const std::string& full_name,
                                 const std::string& token,
                                 const std::string& state /* "open"|"closed"|"all" */,
                                 int per_page,
                                 int page,
                                 const std::string& since_iso8601 /*可空*/);

// 列出 pull requests（返回 JSON array 字符串）
GitHubResponse github_list_pulls(const std::string& full_name,
                                const std::string& token,
                                const std::string& state /* "open"|"closed"|"all" */,
                                int per_page,
                                int page,
                                const std::string& since_iso8601 /*可空*/);
                                
// 列出 commits（返回 JSON array 字符串）
GitHubResponse github_list_commits(const std::string& full_name,
                                  const std::string& token,
                                  int per_page,
                                  int page,
                                  const std::string& since_iso8601 /*可空*/,
                                  const std::string& until_iso8601 /*可空*/);

GitHubResponse github_get_commit_with_file(const std::string& full_name,
                                const std::string& token,
                                const std::string& sha_or_ref);

// 列出 releases（返回 JSON array 字符串）
GitHubResponse github_list_releases(const std::string& full_name,
                                   const std::string& token,
                                   int per_page,
                                   int page);

GitHubResponse github_get_repo(const std::string& full_name, const std::string& token);


// Search issues/prs（用于增量拉 PR：is:pr + updated:>=cursor）
GitHubResponse github_search_issues_prs(const std::string& token,
                                       const std::string& query,
                                       int per_page,
                                       int page,
                                       const std::string& sort /*updated*/,
                                       const std::string& order /*asc|desc*/);

// 获取单个 PR 详情，用于补 merged_at 等字段
GitHubResponse github_get_pull(const std::string& full_name,
                              const std::string& token,
                              int number);

                              
bool github_get_commit_with_retry(const std::string& full_name,
                                        const std::string& token,
                                        const std::string& sha,
                                        GitHubResponse& out,
                                        int max_retries = 5);