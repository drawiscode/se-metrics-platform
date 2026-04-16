// filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/src/github_client.cpp
#include "github_client.h"

#include "common/util.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <algorithm>

struct ProxyConfig {
    std::string host;
    int port = 0;
};

static bool parse_proxy_url(const std::string& raw, ProxyConfig& out)
{
    if (raw.empty()) return false;

    std::string s = raw;
    if (s.rfind("http://", 0) == 0) s = s.substr(7);
    if (s.rfind("https://", 0) == 0) s = s.substr(8);

    // 去掉可能携带的路径
    auto slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(0, slash);

    auto colon = s.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= s.size()) return false;

    out.host = s.substr(0, colon);
    try {
        out.port = std::stoi(s.substr(colon + 1));
    } catch (...) {
        return false;
    }

    return !out.host.empty() && out.port > 0;
}

static bool load_proxy_from_env(ProxyConfig& out)
{
    const std::string candidates[] = {
        util::get_env("HTTPS_PROXY", ""),
        util::get_env("https_proxy", ""),
        util::get_env("HTTP_PROXY", ""),
        util::get_env("http_proxy", "")
    };

    for (const auto& c : candidates) {
        if (parse_proxy_url(c, out)) return true;
    }
    return false;
}

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


static std::string j_get_string_path_or_empty(const nlohmann::json& obj,
                                             const char* k1,
                                             const char* k2,
                                             const char* k3)
{
    const nlohmann::json* cur = &obj;
    if (k1) {
        auto it1 = cur->find(k1);
        if (it1 == cur->end() || it1->is_null() || !it1->is_object()) return "";
        cur = &(*it1);
    }
    if (k2) {
        auto it2 = cur->find(k2);
        if (it2 == cur->end() || it2->is_null() || !it2->is_object()) return "";
        cur = &(*it2);
    }
    if (k3) {
        auto it3 = cur->find(k3);
        if (it3 == cur->end() || it3->is_null()) return "";
        if (it3->is_string()) return it3->get<std::string>();
        return it3->dump();
    }
    return "";
}

static int j_get_bool01_or(const nlohmann::json& obj, const char* key, int defv)
{
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return defv;
    if (it->is_boolean()) return it->get<bool>() ? 1 : 0;
    if (it->is_number_integer()) return it->get<int>() ? 1 : 0;
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
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(60, 0);
    cli.set_write_timeout(60, 0);

    ProxyConfig proxy;
    if (load_proxy_from_env(proxy)) {
        cli.set_proxy(proxy.host, proxy.port);
    }

    // 开发期：避免证书链问题导致握手失败（生产环境不要关）
   // cli.enable_server_certificate_verification(false);

    httplib::Headers headers;
    headers.emplace("User-Agent", "devinsight-backend");
    headers.emplace("Accept", "application/vnd.github+json");
    if (!token.empty()) headers.emplace("Authorization", "Bearer " + token);

    auto res = cli.Get(path.c_str(), headers);
    if (!res) {
        out.error = "http request failed: " + httplib::to_string(res.error());
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

GitHubResponse github_search_issues_prs(const std::string& token,
                                       const std::string& query,
                                       int per_page,
                                       int page,
                                       const std::string& sort,
                                       const std::string& order)
{
    GitHubResponse out;
    per_page = std::max(1, std::min(100, per_page));
    page = std::max(1, page);

    const std::string s = sort.empty() ? "updated" : sort;
    const std::string o = (order == "asc" || order == "desc") ? order : "asc";

    // 关键：Search 的 q 必须 URL 编码（包含 :, >=, 空格 等）
    std::string path = "/search/issues?q=" + simple_encode_url(query) +
                       "&sort=" + simple_encode_url(s) +
                       "&order=" + simple_encode_url(o) +
                       "&per_page=" + std::to_string(per_page) +
                       "&page=" + std::to_string(page);

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


bool parse_repo_releases_from_github(const GitHubResponse& gh,
                                    std::vector<RepoReleaseData>& out,
                                    std::string& error_out,
                                    int& http_status_out)
{
    http_status_out = gh.status;
    out.clear();

    if (!gh.error.empty()) { error_out = gh.error; return false; }
    if (gh.status < 200 || gh.status >= 300) {
        error_out = "github status " + std::to_string(gh.status);
        return false;
    }

    try {
        auto arr = nlohmann::json::parse(gh.body);
        if (!arr.is_array()) { error_out = "releases json is not array"; return false; }

        out.reserve(arr.size());
        for (const auto& it : arr)
        {
            if (!it.is_object()) continue;

            RepoReleaseData d;
            d.tag_name = j_get_string_or_empty(it, "tag_name");
            if (d.tag_name.empty()) continue;

            d.name        = j_get_string_or_empty(it, "name");
            d.draft       = j_get_bool01_or(it, "draft", 0);
            d.prerelease  = j_get_bool01_or(it, "prerelease", 0); // 0=非预发布, 1=预发布

            // published_at 可能为 null（draft）
            d.published_at = j_get_string_or_empty(it, "published_at");
            d.raw_json     = it.dump();

            out.push_back(std::move(d));
        }
    } catch (const std::exception& e) {
        error_out = std::string("releases json parse failed: ") + e.what();
        return false;
    }

    return true;
}


bool parse_repo_commits_from_github(const GitHubResponse& gh,
                                   std::vector<RepoCommitData>& out,
                                   std::string& error_out,
                                   int& http_status_out)
{
    http_status_out = gh.status;
    out.clear();

    if (!gh.error.empty()) { error_out = gh.error; return false; }
    if (gh.status < 200 || gh.status >= 300) {
        error_out = "github status " + std::to_string(gh.status);
        return false;
    }

    try {
        auto arr = nlohmann::json::parse(gh.body);
        if (!arr.is_array()) { error_out = "commits json is not array"; return false; }

        out.reserve(arr.size());
        for (const auto& it : arr)
        {
            if (!it.is_object()) continue;

            RepoCommitData d;
            d.sha = j_get_string_or_empty(it, "sha");
            if (d.sha.empty()) continue;

            // author.login 可能为 null（匿名/找不到 github 用户），需要兜底
            if (it.contains("author") && it["author"].is_object())
                d.author_login = j_get_string_or_empty(it["author"], "login");
            else
                d.author_login.clear();

            // committed_at: commit.committer.date（也可能用 commit.author.date，看你口径）
            d.committed_at = j_get_string_path_or_empty(it, "commit", "committer", "date");
            if (d.committed_at.empty()) {
                // fallback
                d.committed_at = j_get_string_path_or_empty(it, "commit", "author", "date");
            }

            d.raw_json = it.dump();
            out.push_back(std::move(d));
        }
    } catch (const std::exception& e) {
        error_out = std::string("commits json parse failed: ") + e.what();
        return false;
    }

    return true;
}


GitHubResponse github_get_commit_with_file(const std::string& full_name,
                                const std::string& token,
                                const std::string& sha_or_ref)
{
    GitHubResponse out;

    if (!validate_full_name(full_name)) {
        out.error = "invalid full_name";
        return out;
    }

    if (sha_or_ref.empty()) {
        out.error = "empty sha_or_ref";
        return out;
    }

    // GET /repos/{owner}/{repo}/commits/{ref}
    std::string path = "/repos/" + full_name + "/commits/" + simple_encode_url(sha_or_ref);
    return github_get_path(path, token);
}


bool parse_commit_files_from_github(const GitHubResponse& gh,
                                    std::vector<CommitFileData>& out,
                                    std::string& error_out,
                                    int& http_status_out)
{
    http_status_out = gh.status;
    out.clear();

    if (!gh.error.empty()) { error_out = gh.error; return false; }
    if (gh.status < 200 || gh.status >= 300) {
        error_out = "github status " + std::to_string(gh.status);
        return false;
    }

    try {
        auto obj = nlohmann::json::parse(gh.body);
        if (!obj.is_object()) { error_out = "commit json is not object"; return false; }

        // top-level sha
        std::string sha = j_get_string_or_empty(obj, "sha");
        if (sha.empty()) { error_out = "commit sha missing"; return false; }

        // committed_at: commit.committer.date, fallback to commit.author.date
        std::string committed_at = j_get_string_path_or_empty(obj, "commit", "committer", "date");
        if (committed_at.empty()) {
            committed_at = j_get_string_path_or_empty(obj, "commit", "author", "date");
        }

        if (!obj.contains("files") || !obj["files"].is_array()) {
            // 没有 files：可能是一个浅响应或其他错误，视需求决定是否算错误
            error_out = "commit json has no files array";
            return false;
        }

        const auto& files = obj["files"];
        out.reserve(files.size());
        for (const auto& f : files) {
            if (!f.is_object()) continue;

            CommitFileData d;
            d.sha = sha;
            d.filename = j_get_string_or_empty(f, "filename");

            if (f.contains("additions") && f["additions"].is_number()) d.additions = f["additions"].get<int>();
            else d.additions = 0;

            if (f.contains("deletions") && f["deletions"].is_number()) d.deletions = f["deletions"].get<int>();
            else d.deletions = 0;

            if (f.contains("changes") && f["changes"].is_number()) d.changes = f["changes"].get<int>();
            else d.changes = d.additions + d.deletions; // fallback：某些接口可能没有 changes

            d.committed_at = committed_at;
            d.raw_json = f.dump();

            out.push_back(std::move(d));
        }
    } catch (const std::exception& e) {
        error_out = std::string("commit files json parse failed: ") + e.what();
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


GitHubResponse github_list_commits(const std::string& full_name,
                                  const std::string& token,
                                  int per_page,
                                  int page,
                                  const std::string& since_iso8601,
                                  const std::string& until_iso8601)
{
    GitHubResponse out;

    if (!validate_full_name(full_name)) {
        out.error = "invalid full_name";
        return out;
    }

    per_page = std::max(1, std::min(100, per_page));
    page = std::max(1, page);

    std::string path = "/repos/" + full_name + "/commits?per_page=" +
                       std::to_string(per_page) + "&page=" + std::to_string(page);

    // commits list 支持 since/until（ISO8601）
    if (!since_iso8601.empty()) {
        path += "&since=" + simple_encode_url(since_iso8601);
    }
    if (!until_iso8601.empty()) {
        path += "&until=" + simple_encode_url(until_iso8601);
    }

    return github_get_path(path, token);
}

GitHubResponse github_list_releases(const std::string& full_name,
                                   const std::string& token,
                                   int per_page,
                                   int page)
{
    GitHubResponse out;

    if (!validate_full_name(full_name)) {
        out.error = "invalid full_name";
        return out;
    }

    per_page = std::max(1, std::min(100, per_page));
    page = std::max(1, page);

    const std::string path = "/repos/" + full_name + "/releases?per_page=" +
                             std::to_string(per_page) + "&page=" + std::to_string(page);

    return github_get_path(path, token);
}


GitHubResponse github_get_pull(const std::string& full_name,
                              const std::string& token,
                              int number)
{
    GitHubResponse out;
    if (!validate_full_name(full_name)) {
        out.error = "invalid full_name";
        return out;
    }
    if (number <= 0) {
        out.error = "invalid pull number";
        return out;
    }

    std::string path = "/repos/" + full_name + "/pulls/" + std::to_string(number);
    return github_get_path(path, token);
}



bool parse_pull_from_github_json(const GitHubResponse& gh,
                                       RepoPullRequestData& out_pr,
                                       std::string& err,
                                       int& http_status)
{
    http_status = gh.status;
    if (!gh.error.empty())
    {
        err = gh.error;
        return false;
    }
    if (gh.status < 200 || gh.status >= 300)
    {
        err = "github http " + std::to_string(gh.status);
        return false;
    }

    try
    {
        auto j = nlohmann::json::parse(gh.body);

        out_pr.number = j.value("number", 0);
        out_pr.state = j.value("state", "");
        out_pr.title = j.value("title", "");

        out_pr.created_at = j_get_string_or_empty(j, "created_at");
        out_pr.updated_at = j_get_string_or_empty(j, "updated_at");
        out_pr.closed_at  = j_get_string_or_empty(j, "closed_at");   // 可能为 null
        out_pr.merged_at  = j_get_string_or_empty(j, "merged_at");   // 可能为 null

        if (j.contains("user") && j["user"].is_object())
        {
            out_pr.author_login = j_get_string_or_empty(j["user"], "login"); // user.login 也可能为 null
        }
        else
        {
            out_pr.author_login.clear();
        }
        
        out_pr.raw_json = gh.body;

        if (out_pr.number <= 0 || out_pr.updated_at.empty())
        {
            err = "invalid pull payload (missing number/updated_at)";
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        err = std::string("parse pull json failed: ") + e.what();
        return false;
    }
}

bool parse_workflow_runs_from_github(const GitHubResponse& gh,
                                     std::vector<WorkflowRunData>& out,
                                     std::string& error_out,
                                     int& http_status_out)
{
    http_status_out = gh.status;
    out.clear();

    if (!gh.error.empty()) {
        error_out = gh.error;
        return false;
    }
    if (gh.status < 200 || gh.status >= 300) {
        error_out = "github status " + std::to_string(gh.status);
        return false;
    }

    try {
        auto obj = nlohmann::json::parse(gh.body);
        if (!obj.is_object()) {
            error_out = "workflow runs json is not object";
            return false;
        }
        if (!obj.contains("workflow_runs") || !obj["workflow_runs"].is_array()) {
            error_out = "workflow runs missing workflow_runs array";
            return false;
        }

        const auto& arr = obj["workflow_runs"];
        out.reserve(arr.size());

        for (const auto& it : arr) {
            if (!it.is_object()) continue;

            WorkflowRunData d;
            if (it.contains("id") && it["id"].is_number_integer()) {
                d.run_id = it["id"].get<long long>();
            }
            if (d.run_id <= 0) continue;

            if (it.contains("workflow_id") && it["workflow_id"].is_number_integer()) {
                d.workflow_id = it["workflow_id"].get<long long>();
            }

            d.name = j_get_string_or_empty(it, "name");
            d.head_branch = j_get_string_or_empty(it, "head_branch");
            d.event = j_get_string_or_empty(it, "event");
            d.status = j_get_string_or_empty(it, "status");
            d.conclusion = j_get_string_or_empty(it, "conclusion");
            d.created_at = j_get_string_or_empty(it, "created_at");
            d.updated_at = j_get_string_or_empty(it, "updated_at");
            d.run_started_at = j_get_string_or_empty(it, "run_started_at");
            d.html_url = j_get_string_or_empty(it, "html_url");
            if (it.contains("actor") && it["actor"].is_object()) {
                d.actor_login = j_get_string_or_empty(it["actor"], "login");
            }
            d.run_attempt = j_get_int_or(it, "run_attempt", 0);
            d.raw_json = it.dump();

            out.push_back(std::move(d));
        }
    } catch (const std::exception& e) {
        error_out = std::string("workflow runs json parse failed: ") + e.what();
        return false;
    }

    return true;
}

GitHubResponse github_list_workflow_runs(const std::string& full_name,
                                         const std::string& token,
                                         int per_page,
                                         int page,
                                         const std::string& status,
                                         const std::string& event,
                                         const std::string& branch)
{
    GitHubResponse out;
    if (!validate_full_name(full_name)) {
        out.error = "invalid full_name";
        return out;
    }

    per_page = std::max(1, std::min(100, per_page));
    page = std::max(1, page);

    std::string path = "/repos/" + full_name + "/actions/runs?per_page=" +
                       std::to_string(per_page) + "&page=" + std::to_string(page);

    if (!status.empty()) path += "&status=" + simple_encode_url(status);
    if (!event.empty()) path += "&event=" + simple_encode_url(event);
    if (!branch.empty()) path += "&branch=" + simple_encode_url(branch);

    return github_get_path(path, token);
}



bool github_get_commit_with_retry(const std::string& full_name,
                                        const std::string& token,
                                        const std::string& sha,
                                        GitHubResponse& out,
                                        int max_retries)
{
    for (int attempt = 0; attempt <= max_retries; ++attempt)
    {
        out = github_get_commit_with_file(full_name, token, sha);
        // 成功
        if (out.error.empty() && out.status >= 200 && out.status < 300) return true;

        // 不重试的情况（典型：404/401/403/422）
        const bool retryable_status =
            (out.status == 429) || (out.status == 500) || (out.status == 502) || (out.status == 503) || (out.status == 504);

        if (!retryable_status || attempt == max_retries) return false;

        // 指数退避：1s,2s,4s,8s,16s...
        const int backoff_ms = 1000 * (1 << attempt);
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
    }
    return false;
}