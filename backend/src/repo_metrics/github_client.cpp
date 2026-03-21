// filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/src/github_client.cpp
#include "github_client.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <algorithm>

static std::string j_get_string_or_empty(const nlohmann::json& obj, const char* key)
{
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return "";
    if (it->is_string()) return it->get<std::string>();
    // 兜底：若不是 string（极少见），转成 dump，避免抛异常
    return it->dump();
}

static int j_get_int_or(const nlohmann::json& obj, const char* key, int defv)
{
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return defv;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_number()) return static_cast<int>(it->get<double>());
    return defv;
}



// 简易 URL 编码：只对非保留字符以外的字符做 %HH 转义
static std::string simple_encode_url(const std::string& src)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(src.size() * 3);

    for (unsigned char c : src)
    {
        // RFC 3986 unreserved characters: ALPHA / DIGIT / "-" / "." / "_" / "~"
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0x0F]);
            out.push_back(hex[c & 0x0F]);
        }
    }

    return out;
}


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

static bool validate_full_name(const std::string& full_name)
{
    const auto slash = full_name.find('/');
    return !(slash == std::string::npos || slash == 0 || slash + 1 >= full_name.size());
}

static GitHubResponse github_get_path(const std::string& path, const std::string& token)
{
    GitHubResponse out;

#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) || defined(HTTPLIB_OPENSSL_SUPPORT)
    httplib::SSLClient cli("api.github.com", 443);
    cli.set_follow_location(true);

    // 开发期：避免证书链问题导致握手失败（生产环境不要关）
   // cli.enable_server_certificate_verification(false);

    httplib::Headers headers;
    headers.emplace("User-Agent", "devinsight-backend");
    headers.emplace("Accept", "application/vnd.github+json");
    if (!token.empty()) headers.emplace("Authorization", "Bearer " + token);

    auto res = cli.Get(path.c_str(), headers);
    if (!res) {
        out.error = "http request failed";
        return out;
    }

    out.status = res->status;
    out.body = res->body;
    return out;
#else
    out.error = "httplib built without OpenSSL support";
    (void)path; (void)token;
    return out;
#endif
}

static std::string clamp_state(const std::string& state)
{
    if (state == "open" || state == "closed" || state == "all") return state;
    return "all";
}


GitHubResponse github_get_repo(const std::string& full_name, const std::string& token)
{
    GitHubResponse out;
    if (!validate_full_name(full_name)) 
    {
        out.error = "invalid full_name";
        return out;
    }
    const std::string path = "/repos/" + full_name;
    return github_get_path(path, token);
}


bool fetch_repo_snapshot_from_github(const GitHubResponse& gh,
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

// 新增：解析 issues 列表（gh.body 必须是 JSON array）
bool parse_repo_issues_from_github(const GitHubResponse& gh,
                                  std::vector<RepoIssueData>& out,
                                  std::string& error_out,
                                  int& http_status_out)
{
    http_status_out = gh.status;
    out.clear();

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
        auto arr = nlohmann::json::parse(gh.body);
        if (!arr.is_array())
        {
            error_out = "issues json is not array";
            return false;
        }

        out.reserve(arr.size());
        for (const auto& it : arr)
        {
            RepoIssueData d;
            d.number = it.value("number", 0);
            if (d.number <= 0) continue;

            d.state      = j_get_string_or_empty(it, "state");
            d.title      = j_get_string_or_empty(it, "title");
            d.created_at = j_get_string_or_empty(it, "created_at");
            d.updated_at = j_get_string_or_empty(it, "updated_at");
            d.closed_at  = j_get_string_or_empty(it, "closed_at");   // 可能为 null
            d.comments   = j_get_int_or(it, "comments", 0);

            if (it.contains("user") && it["user"].is_object())
                d.author_login = j_get_string_or_empty(it["user"], "login");

            d.is_pull_request = it.contains("pull_request") ? true : false;
            d.raw_json = it.dump();

            out.push_back(std::move(d));
        }
    }
    catch (const std::exception& e)
    {
        error_out = std::string("issues json parse failed: ") + e.what();
        return false;
    }

    return true;
}

// 新增：解析 pulls 列表（gh.body 必须是 JSON array）
bool parse_repo_pulls_from_github(const GitHubResponse& gh,
                                 std::vector<RepoPullRequestData>& out,
                                 std::string& error_out,
                                 int& http_status_out)
{
    http_status_out = gh.status;
    out.clear();

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
        auto arr = nlohmann::json::parse(gh.body);
        if (!arr.is_array())
        {
            error_out = "pulls json is not array";
            return false;
        }

        out.reserve(arr.size());
        for (const auto& it : arr)
        {
            RepoPullRequestData d;
            d.number = it.value("number", 0);
            if (d.number <= 0) continue;

            d.state      = j_get_string_or_empty(it, "state");
            d.title      = j_get_string_or_empty(it, "title");
            d.created_at = j_get_string_or_empty(it, "created_at");
            d.updated_at = j_get_string_or_empty(it, "updated_at");
            d.closed_at  = j_get_string_or_empty(it, "closed_at");   // 可能为 null
            d.merged_at  = j_get_string_or_empty(it, "merged_at");   // 可能为 null
            d.comments   = j_get_int_or(it, "comments", 0);

            if (it.contains("user") && it["user"].is_object())
                d.author_login = j_get_string_or_empty(it["user"], "login");

            d.raw_json = it.dump();
            out.push_back(std::move(d));
        }
    }
    catch (const std::exception& e)
    {
        error_out = std::string("pulls json parse failed: ") + e.what();
        return false;
    }

    return true;
}

// 新增：列出 issues（返回 JSON array 字符串）
GitHubResponse github_list_issues(const std::string& full_name,
                                 const std::string& token,
                                 const std::string& state /* "open"|"closed"|"all" */,
                                 int per_page,
                                 int page,
                                 const std::string& since_iso8601 /*可空*/)      
{
    GitHubResponse out;
    // 校验仓库名格式必须类似 "owner/repo"
    if (!validate_full_name(full_name)) {
        out.error = "invalid full_name";
        return out;
    }

    per_page = std::max(1, std::min(100, per_page));
    page = std::max(1, page);

    const std::string st = clamp_state(state);

    std::string path = "/repos/" + full_name + "/issues?state=" + st +
                       "&per_page=" + std::to_string(per_page) +
                       "&page=" + std::to_string(page);

    // GitHub issues list 支持 since（返回 updated_at > since 的 issues）
    if (!since_iso8601.empty()) {
        path += "&since=" + simple_encode_url(since_iso8601);
    }

    return github_get_path(path, token);
}

// 新增：列出 pull requests（返回 JSON array 字符串）
GitHubResponse github_list_pulls(const std::string& full_name,
                                const std::string& token,
                                const std::string& state /* "open"|"closed"|"all" */,
                                int per_page,
                                int page,
                                const std::string& since_iso8601 /*可空*/)
{
    GitHubResponse out;
    if (!validate_full_name(full_name)) {
        out.error = "invalid full_name";
        return out;
    }

    per_page = std::max(1, std::min(100, per_page));
    page = std::max(1, page);

    const std::string st = clamp_state(state);

    std::string path = "/repos/" + full_name + "/pulls?state=" + st +
                       "&per_page=" + std::to_string(per_page) +
                       "&page=" + std::to_string(page);

    // pulls list 本身不支持 since 参数（issues 支持）。想增量一般用：
    // - 拉 closed 的，按 updated 排序 + page
    // - 或用 /pulls?state=all&sort=updated&direction=desc 然后客户端截断
    // 这里先保留 since 参数但不拼接，避免误导
    (void)since_iso8601;

    return github_get_path(path, token);
}