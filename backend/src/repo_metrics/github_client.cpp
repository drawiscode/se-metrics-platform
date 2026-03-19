// filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/src/github_client.cpp
#include "github_client.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

void Judge_GitHub_Token(const std::string &token)
{
    // 调试输出，方便确认是否拿到 token
    if (token.empty())
    {
        std::cerr << "[DEBUG] GITHUB_TOKEN is empty\n";
    }
    else
    {
        std::cerr << "[DEBUG] GITHUB_TOKEN loaded, length = " << token.size() << '\n';
        // 不要直接输出 token，容易泄漏机密，调试时最多输出长度或前几位做确认
        std::cerr << "[DEBUG] GITHUB_TOKEN = " << token << '\n';
    }
}

GitHubResponse github_get_repo(const std::string& full_name, const std::string& token)
{
    GitHubResponse out;

    // full_name: owner/repo
    const auto slash = full_name.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= full_name.size()) {
        out.error = "invalid full_name";
        return out;
    }

    const std::string path = "/repos/" + full_name;

    // HTTPS client
   // httplib::SSLClient cli("api.github.com", 443);
    httplib::Client cli("api.github.com", 443);
    cli.set_follow_location(true);

    httplib::Headers headers;
    headers.emplace("User-Agent", "devinsight-backend");
    headers.emplace("Accept", "application/vnd.github+json");
    if (!token.empty()) {
        headers.emplace("Authorization", "Bearer " + token);
    }

    auto res = cli.Get(path.c_str(), headers);
    if (!res) {
        out.error = "http request failed";
        return out;
    }

    out.status = res->status;
    out.body = res->body;
    return out;
}


bool fetch_repo_snapshot_from_github(const GitHubResponse& gh,
                                        const std::string& full_name,
                                        const std::string& token,
                                        RepoSnapshotData& out,
                                        std::string& error_out,
                                        int& http_status_out) 
{
    http_status_out = gh.status;

    if (!gh.error.empty()) 
    {
        error_out = gh.error;
        return false;
    }
    if (gh.status < 200 || gh.status >= 300) 
    {
        error_out = "github status " + std::to_string(gh.status);
        return false;
    }
    try 
    {
        auto j = nlohmann::json::parse(gh.body);
        out.stars       = j.value("stargazers_count", 0);
        out.forks       = j.value("forks_count", 0);
        out.open_issues = j.value("open_issues_count", 0);
        out.watchers    = j.value("watchers_count", 0);
        out.pushed_at   = j.value("pushed_at", "");
        out.raw_json    = gh.body;
    } 
    catch (const std::exception& e) 
    {
        error_out = std::string("json parse failed: ") + e.what();
        return false;
    }

    return true;
}

bool fetch_repo_issue_from_github(const GitHubResponse& gh,
                                            const std::string& full_name,
                                            const std::string& token,
                                            RepoIssueData& out,
                                            std::string& error_out,
                                            int& http_status_out)
{
    http_status_out = gh.status;

    if (!gh.error.empty()) 
    {
        error_out = gh.error;
        return false;
    }
    if (gh.status < 200 || gh.status >= 300) 
    {
        error_out = "github status " + std::to_string(gh.status);
        return false;
    }
    try 
    {
        auto j = nlohmann::json::parse(gh.body);
        out.number        = j.value("number", 0);
        out.state         = j.value("state", "");
        out.title         = j.value("title", "");
        out.created_at    = j.value("created_at", "");
        out.updated_at    = j.value("updated_at", "");
        out.closed_at     = j.value("closed_at", "");
        out.comments      = j.value("comments", 0);
        if (j.contains("user") && j["user"].is_object()) {
            out.author_login = j["user"].value("login", "");
        } else {
            out.author_login = "";
        }
        out.is_pull_request = j.contains("pull_request");
        out.raw_json      = gh.body;
    } 
    catch (const std::exception& e) 
    {
        error_out = std::string("json parse failed: ") + e.what();
        return false;
    }

    return true;
}



bool fetch_repo_pull_from_github(const GitHubResponse& gh,
                                            const std::string& full_name,
                                            const std::string& token,
                                            RepoPullRequestData& out,
                                            std::string& error_out,
                                            int& http_status_out) 
{
    http_status_out = gh.status;

    if (!gh.error.empty()) 
    {
        error_out = gh.error;
        return false;
    }
    if (gh.status < 200 || gh.status >= 300) 
    {
        error_out = "github status " + std::to_string(gh.status);
        return false;
    }
    try 
    {
        auto j = nlohmann::json::parse(gh.body);
        out.number        = j.value("number", 0);
        out.state         = j.value("state", "");
        out.title         = j.value("title", "");
        out.created_at    = j.value("created_at", "");
        out.updated_at    = j.value("updated_at", "");
        out.closed_at     = j.value("closed_at", "");
        out.merged_at     = j.value("merged_at", "");
        if (j.contains("user") && j["user"].is_object()) {
            out.author_login = j["user"].value("login", "");
        } else {
            out.author_login = "";
        }
        out.raw_json      = gh.body;
    } 
    catch (const std::exception& e) 
    {
        error_out = std::string("json parse failed: ") + e.what();
        return false;
    }

    return true;
}