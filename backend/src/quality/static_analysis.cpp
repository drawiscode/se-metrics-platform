#include "static_analysis.h"
#include "quality_score.h"
#include "common/util.h"
#include "db/db.h"

#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <unordered_set>
#include <set>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iterator>

namespace fs = std::filesystem;

namespace {

struct ToolExecutionResult {
    std::string tool;
    int analyzed_files = 0;
    int lines_analyzed = 0;
    std::vector<QualityIssue> issues;
    std::map<std::string, int> severity_stats;
    std::string output_file;
    std::string error;
};

static std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string quote_path(const std::string& s)
{
    std::string trimmed = util::trim(s);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed;
    }
    return "\"" + trimmed + "\"";
}

static std::vector<std::string> split_list(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == ',' || ch == ';' || ch == '\n' || ch == '\r' || ch == '|') {
            std::string v = util::trim(cur);
            if (!v.empty()) out.push_back(v);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    std::string v = util::trim(cur);
    if (!v.empty()) out.push_back(v);
    return out;
}

static std::string join_list(const std::vector<std::string>& items, const std::string& sep)
{
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += sep;
        out += items[i];
    }
    return out;
}

static std::string get_clone_root()
{
    std::string root = util::get_env("REPO_CLONE_ROOT", "data/repo_cache");
    return util::trim(root);
}

static std::string build_repo_url(const std::string& full_name)
{
    std::string token = util::get_env("GITHUB_TOKEN", "");
    if (!token.empty()) {
        return "https://x-access-token:" + token + "@github.com/" + full_name + ".git";
    }
    return "https://github.com/" + full_name + ".git";
}

static int run_cmd(const std::string& cmd)
{
    return std::system(cmd.c_str());
}

static std::string run_cmd_capture(const std::string& cmd)
{
    std::string out;
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return out;

    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        out += buf;
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return util::trim(out);
}

static bool is_ignored_dir(const fs::path& p)
{
    static const std::unordered_set<std::string> kIgnored = {
        ".git", "node_modules", "dist", "build", "out",
        "vendor", "third_party", ".vscode"
    };
    for (const auto& part : p) {
        std::string name = to_lower(part.string());
        if (kIgnored.count(name)) return true;
    }
    return false;
}

static bool is_hot_path(const std::string& rel)
{
    std::string r = to_lower(rel);
    if (r.rfind("src/", 0) == 0) return true;
    if (r.rfind("include/", 0) == 0) return true;
    if (r.rfind("lib/", 0) == 0) return true;
    if (r.rfind("app/", 0) == 0) return true;
    if (r.rfind("docs/", 0) == 0) return true;
    if (r == "readme.md" || r == "readme.txt") return true;
    return false;
}

static bool is_cpp_source(const fs::path& p)
{
    std::string ext = to_lower(p.extension().string());
    return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx"
        || ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx";
}

static bool is_cpp_translation_unit(const fs::path& p)
{
    std::string ext = to_lower(p.extension().string());
    return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx";
}

static bool clone_or_pull_repo(const fs::path& repo_dir,
                               const std::string& full_name,
                               const std::string& ref,
                               std::string& err)
{
    std::string url = build_repo_url(full_name);
    std::string ref_arg = ref.empty() ? "main" : ref;

    if (!fs::exists(repo_dir)) {
        fs::create_directories(repo_dir.parent_path());
        std::string cmd = "git clone --depth 1 --branch " + ref_arg + " " + url + " " + quote_path(repo_dir.string());
        int rc = run_cmd(cmd);
        if (rc != 0) {
            err = "git clone failed";
            return false;
        }
        return true;
    }

    if (!fs::exists(repo_dir / ".git")) {
        err = "repo_dir exists but is not a git repo";
        return false;
    }

    std::string cmd_fetch = "git -C " + quote_path(repo_dir.string()) + " fetch --depth 1 origin " + ref_arg;
    if (run_cmd(cmd_fetch) != 0) {
        err = "git fetch failed";
        return false;
    }
    std::string cmd_checkout = "git -C " + quote_path(repo_dir.string()) + " checkout --force FETCH_HEAD";
    if (run_cmd(cmd_checkout) != 0) {
        err = "git checkout failed";
        return false;
    }

    return true;
}

static bool ensure_repo_checkout(int repo_id,
                                const std::string& full_name,
                                const std::string& ref,
                                fs::path& repo_dir,
                                std::string& err)
{
    std::string root = get_clone_root();
    repo_dir = fs::path(root) / std::to_string(repo_id);
    return clone_or_pull_repo(repo_dir, full_name, ref, err);
}

static std::vector<fs::path> collect_cpp_files(const fs::path& repo_dir, bool hot_mode, int max_files)
{
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(repo_dir); it != fs::recursive_directory_iterator(); ++it) {
        const auto& p = it->path();
        if (is_ignored_dir(p)) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file()) continue;
        if (!is_cpp_source(p)) continue;

        std::string rel = fs::relative(p, repo_dir).generic_string();
        if (rel.empty()) continue;
        if (hot_mode && !is_hot_path(rel)) continue;

        files.push_back(p);
        if (max_files > 0 && static_cast<int>(files.size()) >= max_files) break;
    }
    std::sort(files.begin(), files.end());
    return files;
}

static std::string read_text_file(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

static int count_text_lines(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return 0;
    int lines = 0;
    std::string line;
    while (std::getline(in, line)) ++lines;
    return lines;
}

static int count_lines(const std::vector<fs::path>& files)
{
    int total = 0;
    for (const auto& f : files) total += count_text_lines(f);
    return total;
}

static std::string to_relative_or_full(const fs::path& repo_dir, const std::string& path)
{
    std::error_code ec;
    fs::path p(path);
    if (p.is_absolute()) {
        auto rel = fs::relative(p, repo_dir, ec);
        if (!ec && !rel.empty()) return rel.generic_string();
    }
    return path;
}

static std::string normalize_issue_path(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.rfind("./", 0) == 0) path.erase(0, 2);
    return path;
}

static std::string fnv1a_hex(const std::string& s)
{
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << h;
    return oss.str();
}

static std::string issue_key_for(const std::string& tool, const QualityIssue& issue)
{
    std::ostringstream raw;
    raw << to_lower(tool) << "|"
        << to_lower(normalize_issue_path(issue.file_path)) << "|"
        << issue.line << "|"
        << issue.column << "|"
        << to_lower(issue.rule_id) << "|"
        << issue.message;
    return fnv1a_hex(raw.str());
}

static std::string get_attr(const std::string& tag, const std::string& key)
{
    std::string needle = key + "=\"";
    size_t pos = tag.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    size_t end = tag.find('"', pos);
    if (end == std::string::npos) return {};
    std::string value = tag.substr(pos, end - pos);
    struct Entity { const char* from; const char* to; };
    static const Entity kEntities[] = {
        {"&quot;", "\""}, {"&apos;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&amp;", "&"}
    };
    for (const auto& e : kEntities) {
        size_t p = 0;
        while ((p = value.find(e.from, p)) != std::string::npos) {
            value.replace(p, std::strlen(e.from), e.to);
            p += std::strlen(e.to);
        }
    }
    return value;
}

static std::vector<QualityIssue> parse_cppcheck_xml(const std::string& xml, const fs::path& repo_dir)
{
    std::vector<QualityIssue> issues;
    size_t pos = 0;
    while (true) {
        size_t err_start = xml.find("<error ", pos);
        if (err_start == std::string::npos) break;
        size_t err_end = xml.find("</error>", err_start);
        if (err_end == std::string::npos) break;
        size_t tag_end = xml.find('>', err_start);
        if (tag_end == std::string::npos || tag_end > err_end) break;

        std::string err_tag = xml.substr(err_start, tag_end - err_start + 1);
        std::string block = xml.substr(tag_end + 1, err_end - tag_end - 1);

        QualityIssue issue;
        issue.rule_id = get_attr(err_tag, "id");
        issue.severity = get_attr(err_tag, "severity");
        issue.message = get_attr(err_tag, "msg");

        size_t loc_pos = block.find("<location ");
        if (loc_pos != std::string::npos) {
            size_t loc_end = block.find('>', loc_pos);
            if (loc_end != std::string::npos) {
                std::string loc_tag = block.substr(loc_pos, loc_end - loc_pos + 1);
                issue.file_path = get_attr(loc_tag, "file");
                issue.file_path = to_relative_or_full(repo_dir, issue.file_path);
                std::string line = get_attr(loc_tag, "line");
                std::string col = get_attr(loc_tag, "column");
                if (!line.empty()) issue.line = std::stoi(line);
                if (!col.empty()) issue.column = std::stoi(col);
            }
        }

        if (!issue.file_path.empty()) {
            issues.push_back(std::move(issue));
        }
        pos = err_end + 8;
    }
    return issues;
}

static bool parse_clang_tidy_line(const std::string& line, QualityIssue& out)
{
    static const std::vector<std::string> severities = {
        " error: ", " warning: ", " note: ", " remark: "
    };

    size_t sev_pos = std::string::npos;
    std::string sev;
    for (const auto& s : severities) {
        size_t p = line.find(s);
        if (p != std::string::npos) {
            sev_pos = p;
            sev = s.substr(1, s.size() - 3);
            break;
        }
    }
    if (sev_pos == std::string::npos) return false;

    std::string left = line.substr(0, sev_pos);
    std::string right = line.substr(sev_pos + 1); // starts with severity

    size_t col_pos = left.rfind(':');
    if (col_pos == std::string::npos) return false;
    size_t line_pos = left.rfind(':', col_pos - 1);
    if (line_pos == std::string::npos) return false;

    out.file_path = left.substr(0, line_pos);
    out.line = std::atoi(left.substr(line_pos + 1, col_pos - line_pos - 1).c_str());
    out.column = std::atoi(left.substr(col_pos + 1).c_str());
    out.severity = util::trim(sev);

    std::string msg_part = right.substr(sev.size() + 1); // after "severity: "
    size_t rule_pos = msg_part.rfind('[');
    if (!msg_part.empty() && rule_pos != std::string::npos && msg_part.back() == ']') {
        out.rule_id = msg_part.substr(rule_pos + 1, msg_part.size() - rule_pos - 2);
        out.message = util::trim(msg_part.substr(0, rule_pos));
    } else {
        out.message = util::trim(msg_part);
    }

    return true;
}

static std::vector<QualityIssue> parse_clang_tidy_output(const std::string& output, const fs::path& repo_dir)
{
    std::vector<QualityIssue> issues;
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        QualityIssue issue;
        if (parse_clang_tidy_line(line, issue)) {
            issue.file_path = to_relative_or_full(repo_dir, util::trim(issue.file_path));
            if (!issue.file_path.empty()) {
                issues.push_back(std::move(issue));
            }
        }
    }
    return issues;
}

static bool insert_quality_issue(Db& db, int repo_id, const std::string& tool, const QualityIssue& issue)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO quality_issues(repo_id, tool, file_path, line, column, rule_id, severity, message) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, issue.file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, issue.line);
    sqlite3_bind_int(stmt, 5, issue.column);
    sqlite3_bind_text(stmt, 6, issue.rule_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, issue.severity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, issue.message.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

static void clear_quality_issues(Db& db, int repo_id, const std::string& tool)//清除某仓库某工具的旧质量问题
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM quality_issues WHERE repo_id=?1 AND tool=?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static std::string severity_stats_to_json(const std::map<std::string, int>& stats)
{
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& kv : stats) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << util::json_escape(kv.first) << "\":" << kv.second;
    }
    oss << "}";
    return oss.str();
}

static int insert_quality_run(Db& db,
                              int repo_id,
                              int task_id,
                              const std::string& ref,
                              const std::string& tools,
                              const std::string& mode,
                              int max_files,
                              const std::string& config_json)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO quality_analysis_runs("
        "task_id, repo_id, branch, tools, mode, max_files, status, config_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, 'Running', ?7);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

    if (task_id > 0) sqlite3_bind_int(stmt, 1, task_id);
    else sqlite3_bind_null(stmt, 1);
    sqlite3_bind_int(stmt, 2, repo_id);
    sqlite3_bind_text(stmt, 3, ref.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, tools.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, max_files);
    sqlite3_bind_text(stmt, 7, config_json.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return 0;
    return static_cast<int>(sqlite3_last_insert_rowid(sdb));
}

static void finish_quality_run(Db& db,
                               int run_id,
                               const QualityAnalysisResult& result,
                               double score,
                               double baseline_score,
                               bool degraded)
{
    if (run_id <= 0) return;

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE quality_analysis_runs SET "
        "finished_at=datetime('now'), status=?1, analyzed_files=?2, lines_analyzed=?3, "
        "issues_total=?4, issues_new=?5, issues_fixed=?6, issues_by_severity_json=?7, "
        "score=?8, baseline_score=?9, degraded=?10, output_json=?11, error=?12 "
        "WHERE id=?13;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    const std::string status = result.error.empty() ? "Finished" : "Failed";
    const std::string sev_json = severity_stats_to_json(result.severity_stats);

    std::ostringstream output_json;
    output_json << "{\"tools\":{";
    bool first = true;
    for (const auto& kv : result.output_files) {
        if (!first) output_json << ",";
        first = false;
        output_json << "\"" << util::json_escape(kv.first) << "\":\""
                    << util::json_escape(kv.second) << "\"";
    }
    output_json << "}}";

    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, result.analyzed_files);
    sqlite3_bind_int(stmt, 3, result.lines_analyzed);
    sqlite3_bind_int(stmt, 4, result.issues_inserted);
    sqlite3_bind_int(stmt, 5, result.issues_new);
    sqlite3_bind_int(stmt, 6, result.issues_fixed);
    sqlite3_bind_text(stmt, 7, sev_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 8, score);
    if (baseline_score >= 0.0) sqlite3_bind_double(stmt, 9, baseline_score);
    else sqlite3_bind_null(stmt, 9);
    sqlite3_bind_int(stmt, 10, degraded ? 1 : 0);
    const std::string out = output_json.str();
    sqlite3_bind_text(stmt, 11, out.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, result.error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 13, run_id);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void update_quality_task_after_run(Db& db, int task_id, int run_id, const std::string& status)
{
    if (task_id <= 0) return;

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE quality_analysis_tasks SET status=?1, last_run_id=?2, updated_at=datetime('now') "
        "WHERE id=?3;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, run_id);
    sqlite3_bind_int(stmt, 3, task_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static double read_quality_baseline(Db& db, int repo_id)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT min_score FROM quality_baselines WHERE repo_id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1.0;
    sqlite3_bind_int(stmt, 1, repo_id);
    double baseline = -1.0;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        baseline = sqlite3_column_double(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return baseline;
}

static bool find_current_issue(Db& db,
                               int repo_id,
                               const std::string& tool,
                               const std::string& issue_key,
                               int& issue_id,
                               std::string& status)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, status FROM quality_issues "
        "WHERE repo_id=?1 AND tool=?2 AND issue_key=?3 "
        "ORDER BY id ASC LIMIT 1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, issue_key.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        issue_id = sqlite3_column_int(stmt, 0);
        const unsigned char* s = sqlite3_column_text(stmt, 1);
        status = s ? (const char*)s : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

static bool insert_run_issue(Db& db,
                             int run_id,
                             int repo_id,
                             const std::string& tool,
                             const std::string& issue_key,
                             const QualityIssue& issue)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO quality_run_issues("
        "run_id, repo_id, tool, issue_key, file_path, line, column, rule_id, severity, message) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, run_id);
    sqlite3_bind_int(stmt, 2, repo_id);
    sqlite3_bind_text(stmt, 3, tool.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, issue_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, issue.file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, issue.line);
    sqlite3_bind_int(stmt, 7, issue.column);
    sqlite3_bind_text(stmt, 8, issue.rule_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, issue.severity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, issue.message.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

static bool upsert_current_issue(Db& db,
                                 int repo_id,
                                 int run_id,
                                 const std::string& tool,
                                 const std::string& issue_key,
                                 const QualityIssue& issue,
                                 bool& is_new)
{
    int existing_id = 0;
    std::string existing_status;
    is_new = !find_current_issue(db, repo_id, tool, issue_key, existing_id, existing_status)
        || existing_status != "active";

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    if (existing_id > 0) {
        const char* sql =
            "UPDATE quality_issues SET "
            "file_path=?1, line=?2, column=?3, rule_id=?4, severity=?5, message=?6, "
            "status='active', last_seen_run_id=?7, last_seen_at=datetime('now'), fixed_at=NULL "
            "WHERE id=?8;";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, issue.file_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, issue.line);
        sqlite3_bind_int(stmt, 3, issue.column);
        sqlite3_bind_text(stmt, 4, issue.rule_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, issue.severity.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, issue.message.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 7, run_id);
        sqlite3_bind_int(stmt, 8, existing_id);
    } else {
        const char* sql =
            "INSERT INTO quality_issues("
            "repo_id, tool, issue_key, file_path, line, column, rule_id, severity, message, "
            "status, first_seen_run_id, last_seen_run_id, last_seen_at) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, 'active', ?10, ?10, datetime('now'));";
        if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int(stmt, 1, repo_id);
        sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, issue_key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, issue.file_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, issue.line);
        sqlite3_bind_int(stmt, 6, issue.column);
        sqlite3_bind_text(stmt, 7, issue.rule_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, issue.severity.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, issue.message.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 10, run_id);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return false;
    return insert_run_issue(db, run_id, repo_id, tool, issue_key, issue);
}

static int mark_fixed_issues(Db& db, int repo_id, int run_id, const std::string& tool)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE quality_issues SET status='fixed', fixed_at=datetime('now') "
        "WHERE repo_id=?1 AND tool=?2 AND status='active' AND last_seen_run_id<>?3;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, tool.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, run_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? sqlite3_changes(sdb) : 0;
}

static std::string ensure_output_dir()
{
    std::string root = util::get_env("QUALITY_OUTPUT_DIR", "data/quality");
    fs::create_directories(root);
    return root;
}

static std::string make_timestamp()
{
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    return std::string(buf);
}

static bool env_truthy(const std::string& name, bool defv = false)
{
    std::string v = to_lower(util::trim(util::get_env(name.c_str(), defv ? "1" : "0")));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

static std::set<std::string> ignored_rule_ids()
{
    std::set<std::string> rules;
    std::string configured = util::trim(util::get_env(
        "QUALITY_IGNORE_RULES",
        "missingInclude,missingIncludeSystem"));
    for (const auto& r : split_list(configured)) {
        rules.insert(to_lower(r));
    }
    return rules;
}

static void normalize_and_filter_issues(std::vector<QualityIssue>& issues)
{
    const auto ignored = ignored_rule_ids();
    std::vector<QualityIssue> filtered;
    filtered.reserve(issues.size());

    for (auto& issue : issues) {
        issue.file_path = normalize_issue_path(util::trim(issue.file_path));
        issue.rule_id = util::trim(issue.rule_id);
        issue.severity = util::trim(issue.severity);
        issue.message = util::trim(issue.message);
        if (ignored.count(to_lower(issue.rule_id))) continue;
        filtered.push_back(std::move(issue));
    }
    issues.swap(filtered);
}

static std::vector<fs::path> tool_source_files(const std::vector<fs::path>& files, bool include_headers)
{
    if (include_headers) return files;

    std::vector<fs::path> out;
    for (const auto& f : files) {
        if (is_cpp_translation_unit(f)) out.push_back(f);
    }
    return out.empty() ? files : out;
}

static std::vector<fs::path> discover_include_dirs(const fs::path& repo_dir)
{
    std::vector<fs::path> dirs;
    const std::vector<fs::path> candidates = {
        repo_dir,
        repo_dir / "include",
        repo_dir / "src",
        repo_dir / "lib",
        repo_dir / "app",
        repo_dir / "backend" / "include",
        repo_dir / "backend" / "src"
    };

    for (const auto& d : candidates) {
        std::error_code ec;
        if (fs::exists(d, ec) && fs::is_directory(d, ec)) dirs.push_back(d);
    }

    for (const auto& raw : split_list(util::get_env("CPPCHECK_INCLUDE_DIRS", ""))) {
        fs::path d(raw);
        std::error_code ec;
        if (fs::exists(d, ec) && fs::is_directory(d, ec)) dirs.push_back(d);
    }

    std::sort(dirs.begin(), dirs.end());
    dirs.erase(std::unique(dirs.begin(), dirs.end()), dirs.end());
    return dirs;
}

static bool write_file_list(const fs::path& file_list, const std::vector<fs::path>& files)
{
    std::ofstream out(file_list, std::ios::binary);
    if (!out.is_open()) return false;
    for (const auto& f : files) {
        out << f.generic_string() << "\n";
    }
    return true;
}

static ToolExecutionResult execute_cppcheck(const fs::path& repo_dir,
                                            const std::vector<fs::path>& files,
                                            int repo_id)
{
    ToolExecutionResult result;
    result.tool = "cppcheck";

    const bool analyze_headers = env_truthy("CPPCHECK_ANALYZE_HEADERS", false);
    std::vector<fs::path> tool_files = tool_source_files(files, analyze_headers);
    result.analyzed_files = static_cast<int>(tool_files.size());
    result.lines_analyzed = count_lines(tool_files);

    if (tool_files.empty()) {
        result.error = "no source files to analyze";
        return result;
    }

    std::string bin = util::trim(util::get_env("CPPCHECK_BIN", "cppcheck"));
    std::string out_dir = ensure_output_dir();
    std::string stamp = make_timestamp();
    fs::path out_file = fs::path(out_dir) / ("cppcheck_" + std::to_string(repo_id) + "_" + stamp + ".xml");
    fs::path file_list = fs::path(out_dir) / ("cppcheck_" + std::to_string(repo_id) + "_" + stamp + ".files.txt");
    result.output_file = out_file.string();

    if (!write_file_list(file_list, tool_files)) {
        result.error = "failed to write cppcheck file list";
        return result;
    }

    std::ostringstream cmd;
    cmd << quote_path(bin)
        << " --xml --xml-version=2 --enable=all --inconclusive --quiet --force"
        << " --language=c++ --std=c++17";

    for (const auto& rule : ignored_rule_ids()) {
        if (!rule.empty()) cmd << " --suppress=" << rule;
    }

    for (const auto& dir : discover_include_dirs(repo_dir)) {
        cmd << " -I" << quote_path(dir.string());
    }

    std::string extra = util::trim(util::get_env("CPPCHECK_ARGS", ""));
    if (!extra.empty()) cmd << " " << extra;
    cmd << " --file-list=" << quote_path(file_list.string());
    cmd << " 2> " << quote_path(out_file.string());

    std::string cmd_str = cmd.str();
#ifdef _WIN32
    cmd_str = "cmd /c \"" + cmd_str + "\"";
#endif
    int rc = run_cmd(cmd_str);
    std::string xml = read_text_file(out_file);
    if (xml.empty()) {
        result.error = rc == 0 ? "cppcheck produced empty XML output" : "cppcheck command failed";
        return result;
    }

    result.issues = parse_cppcheck_xml(xml, repo_dir);
    normalize_and_filter_issues(result.issues);
    for (const auto& issue : result.issues) {
        result.severity_stats[issue.severity]++;
    }
    return result;
}

static fs::path resolve_compile_commands_dir(const fs::path& repo_dir);

static ToolExecutionResult execute_clang_tidy(const fs::path& repo_dir,
                                              const std::vector<fs::path>& files)
{
    ToolExecutionResult result;
    result.tool = "clang-tidy";

    std::vector<fs::path> tool_files = tool_source_files(files, false);
    result.analyzed_files = static_cast<int>(tool_files.size());
    result.lines_analyzed = count_lines(tool_files);

    if (tool_files.empty()) {
        result.error = "no source files to analyze";
        return result;
    }

    fs::path cc_dir = resolve_compile_commands_dir(repo_dir);
    if (cc_dir.empty()) {
        result.error = "compile_commands.json not found (set CLANG_TIDY_COMPILE_COMMANDS)";
        return result;
    }

    std::string bin = util::trim(util::get_env("CLANG_TIDY_BIN", "clang-tidy"));
    std::string extra = util::trim(util::get_env("CLANG_TIDY_ARGS", ""));

    for (const auto& f : tool_files) {
        std::ostringstream cmd;
        cmd << quote_path(bin)
            << " -p " << quote_path(cc_dir.string())
            << " " << quote_path(f.string())
            << " --quiet";
        if (!extra.empty()) cmd << " " << extra;
        cmd << " 2>&1";

        std::string cmd_str = cmd.str();
#ifdef _WIN32
        cmd_str = "cmd /c \"" + cmd_str + "\"";
#endif
        std::string output = run_cmd_capture(cmd_str);
        auto parsed = parse_clang_tidy_output(output, repo_dir);
        result.issues.insert(result.issues.end(),
                             std::make_move_iterator(parsed.begin()),
                             std::make_move_iterator(parsed.end()));
    }

    normalize_and_filter_issues(result.issues);
    for (const auto& issue : result.issues) {
        result.severity_stats[issue.severity]++;
    }
    return result;
}

static void merge_tool_result(QualityAnalysisResult& aggregate,
                              Db& db,
                              int repo_id,
                              int run_id,
                              const ToolExecutionResult& tool_result)
{
    aggregate.analyzed_files += tool_result.analyzed_files;
    aggregate.lines_analyzed += tool_result.lines_analyzed;
    if (!tool_result.output_file.empty()) {
        aggregate.output_files[tool_result.tool] = tool_result.output_file;
    }

    if (!tool_result.error.empty()) {
        if (!aggregate.error.empty()) aggregate.error += "; ";
        aggregate.error += tool_result.tool + ": " + tool_result.error;
        return;
    }

    int persisted = 0;
    int new_count = 0;
    for (const auto& issue : tool_result.issues) {
        const std::string key = issue_key_for(tool_result.tool, issue);
        bool is_new = false;
        if (upsert_current_issue(db, repo_id, run_id, tool_result.tool, key, issue, is_new)) {
            persisted++;
            if (is_new) new_count++;
            aggregate.severity_stats[issue.severity]++;
        }
    }

    aggregate.issues_inserted += persisted;
    aggregate.issues_new += new_count;
    aggregate.issues_fixed += mark_fixed_issues(db, repo_id, run_id, tool_result.tool);
}

static QualityAnalysisResult run_cppcheck(Db& db, int repo_id, const fs::path& repo_dir,
                                          const std::vector<fs::path>& files)
{
    QualityAnalysisResult result;
    result.tool = "cppcheck";
    result.analyzed_files = static_cast<int>(files.size());

    if (files.empty()) {
        result.error = "no source files to analyze";
        return result;
    }

    std::string bin = util::trim(util::get_env("CPPCHECK_BIN", "cppcheck"));
    std::string out_dir = ensure_output_dir();
    std::string out_file = (fs::path(out_dir) / ("cppcheck_" + std::to_string(repo_id) + "_" + make_timestamp() + ".xml")).string();

    std::ostringstream cmd;
    cmd << quote_path(bin)
        << " --xml --xml-version=2 --enable=all --inconclusive --quiet --force"
        << " --suppress=missingIncludeSystem"
        << " ";

    for (const auto& f : files) {
        cmd << quote_path(f.string()) << " ";
    }

#ifdef _WIN32
    cmd << " 2> " << quote_path(out_file);
#else
    cmd << " 2> " << quote_path(out_file);
#endif

    std::string cmd_str = cmd.str();
#ifdef _WIN32
    cmd_str = "cmd /c \"" + cmd_str + "\"";
#endif
    if (run_cmd(cmd_str) != 0) {
        // cppcheck 在发现问题时也可能返回非 0，这里不直接视为失败
    }

    std::string xml = read_text_file(out_file);
    auto issues = parse_cppcheck_xml(xml, repo_dir);

    for (const auto& issue : issues) {
        if (insert_quality_issue(db, repo_id, result.tool, issue)) {
            result.issues_inserted++;
            result.severity_stats[issue.severity]++;
        }
    }

    return result;
}

static fs::path resolve_compile_commands_dir(const fs::path& repo_dir)
{
    std::string env_cc = util::get_env("CLANG_TIDY_COMPILE_COMMANDS", "");
    if (!env_cc.empty()) {
        fs::path p(env_cc);
        if (fs::exists(p / "compile_commands.json")) return p;
        if (fs::exists(p)) return p.parent_path();
    }

    if (fs::exists(repo_dir / "compile_commands.json")) return repo_dir;
    if (fs::exists(repo_dir / "build" / "compile_commands.json")) return repo_dir / "build";
    return {};
}

static QualityAnalysisResult run_clang_tidy(Db& db, int repo_id, const fs::path& repo_dir,
                                            const std::vector<fs::path>& files)
{
    QualityAnalysisResult result;
    result.tool = "clang-tidy";
    result.analyzed_files = static_cast<int>(files.size());

    if (files.empty()) {
        result.error = "no source files to analyze";
        return result;
    }

    fs::path cc_dir = resolve_compile_commands_dir(repo_dir);
    if (cc_dir.empty()) {
        result.error = "compile_commands.json not found (set CLANG_TIDY_COMPILE_COMMANDS)";
        return result;
    }

    std::string bin = util::trim(util::get_env("CLANG_TIDY_BIN", "clang-tidy"));
    std::string extra = util::trim(util::get_env("CLANG_TIDY_ARGS", ""));

    for (const auto& f : files) {
        std::ostringstream cmd;
        cmd << quote_path(bin)
            << " -p " << quote_path(cc_dir.string())
            << " " << quote_path(f.string())
            << " --quiet";
        if (!extra.empty()) {
            cmd << " " << extra;
        }
        cmd << " 2>&1";

        std::string cmd_str = cmd.str();
    #ifdef _WIN32
        cmd_str = "cmd /c \"" + cmd_str + "\"";
    #endif
        std::string output = run_cmd_capture(cmd_str);
        auto issues = parse_clang_tidy_output(output, repo_dir);
        for (const auto& issue : issues) {
            if (insert_quality_issue(db, repo_id, result.tool, issue)) {
                result.issues_inserted++;
                result.severity_stats[issue.severity]++;
            }
        }
    }

    return result;
}

} // namespace

QualityAnalysisResult run_static_analysis(Db& db,
                                          int repo_id,
                                          const std::string& full_name,
                                          const std::string& ref,
                                          const std::string& tool,
                                          const std::string& mode,
                                          int max_files)
{
    return run_static_analysis_task(db, repo_id, 0, full_name, ref, tool, mode, max_files, "{}");
    QualityAnalysisResult result;

    fs::path repo_dir;
    std::string err;
    if (!ensure_repo_checkout(repo_id, full_name, ref, repo_dir, err)) {//确保代码已经拉取到本地
        result.tool = tool;
        result.error = err;
        return result;
    }

    bool hot_mode = (to_lower(mode) == "hot");
    auto files = collect_cpp_files(repo_dir, hot_mode, max_files);

    std::string tool_lower = to_lower(tool);
    if (tool_lower == "cppcheck") {
        clear_quality_issues(db, repo_id, "cppcheck");
        return run_cppcheck(db, repo_id, repo_dir, files);
    }
    if (tool_lower == "clang-tidy" || tool_lower == "clang_tidy") {
        clear_quality_issues(db, repo_id, "clang-tidy");
        return run_clang_tidy(db, repo_id, repo_dir, files);
    }

    result.tool = tool;
    result.error = "unsupported tool";
    return result;
}

QualityAnalysisResult run_static_analysis_task(Db& db,
                                               int repo_id,
                                               int task_id,
                                               const std::string& full_name,
                                               const std::string& ref,
                                               const std::string& tools,
                                               const std::string& mode,
                                               int max_files,
                                               const std::string& config_json)
{
    QualityAnalysisResult result;
    result.task_id = task_id;
    result.tool = tools;
    result.status = "Running";

    const std::vector<std::string> tool_names = split_list(tools.empty() ? "cppcheck" : tools);
    if (tool_names.empty()) {
        result.status = "Failed";
        result.error = "no analysis tools selected";
        return result;
    }

    fs::path repo_dir;
    std::string err;
    if (!ensure_repo_checkout(repo_id, full_name, ref, repo_dir, err)) {
        result.status = "Failed";
        result.error = err;
        return result;
    }

    const std::string normalized_tools = join_list(tool_names, ",");
    const int run_id = insert_quality_run(db, repo_id, task_id, ref, normalized_tools, mode, max_files, config_json);
    result.run_id = run_id;
    if (run_id <= 0) {
        result.status = "Failed";
        result.error = "failed to create quality analysis run";
        return result;
    }

    const bool hot_mode = (to_lower(mode) == "hot");
    auto files = collect_cpp_files(repo_dir, hot_mode, max_files);

    for (const auto& raw_tool : tool_names) {
        std::string tool_lower = to_lower(util::trim(raw_tool));
        if (tool_lower == "all") {
            ToolExecutionResult cppcheck = execute_cppcheck(repo_dir, files, repo_id);
            merge_tool_result(result, db, repo_id, run_id, cppcheck);
            ToolExecutionResult clang_tidy = execute_clang_tidy(repo_dir, files);
            merge_tool_result(result, db, repo_id, run_id, clang_tidy);
            continue;
        }
        if (tool_lower == "cppcheck") {
            ToolExecutionResult cppcheck = execute_cppcheck(repo_dir, files, repo_id);
            merge_tool_result(result, db, repo_id, run_id, cppcheck);
            continue;
        }
        if (tool_lower == "clang-tidy" || tool_lower == "clang_tidy") {
            ToolExecutionResult clang_tidy = execute_clang_tidy(repo_dir, files);
            merge_tool_result(result, db, repo_id, run_id, clang_tidy);
            continue;
        }

        if (!result.error.empty()) result.error += "; ";
        result.error += "unsupported tool: " + raw_tool;
    }

    result.status = result.error.empty() ? "Finished" : "Failed";
    QualityScore score = compute_quality_score(db, repo_id, "");
    const double baseline = read_quality_baseline(db, repo_id);
    const bool degraded = baseline >= 0.0 && score.score < baseline;
    finish_quality_run(db, run_id, result, score.score, baseline, degraded);
    update_quality_task_after_run(db, task_id, run_id, result.status);
    return result;
}

std::string quality_result_to_json(const QualityAnalysisResult& r)
{
    std::ostringstream oss;
    oss << "{\"ok\":" << (r.error.empty() ? "true" : "false")
        << ",\"tool\":\"" << util::json_escape(r.tool) << "\""
        << ",\"status\":\"" << util::json_escape(r.status) << "\""
        << ",\"run_id\":" << r.run_id
        << ",\"task_id\":" << r.task_id
        << ",\"analyzed_files\":" << r.analyzed_files
        << ",\"lines_analyzed\":" << r.lines_analyzed
        << ",\"issues_inserted\":" << r.issues_inserted
        << ",\"issues_new\":" << r.issues_new
        << ",\"issues_fixed\":" << r.issues_fixed
        << ",\"severity_stats\":{";

    bool first = true;
    for (const auto& kv : r.severity_stats) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << util::json_escape(kv.first) << "\":" << kv.second;
    }
    oss << "}";

    oss << ",\"output_files\":{";
    first = true;
    for (const auto& kv : r.output_files) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << util::json_escape(kv.first) << "\":\""
            << util::json_escape(kv.second) << "\"";
    }
    oss << "}";

    if (!r.error.empty()) {
        oss << ",\"error\":\"" << util::json_escape(r.error) << "\"";
    }
    oss << "}";
    return oss.str();
}
