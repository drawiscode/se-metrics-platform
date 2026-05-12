#include "static_analysis.h"
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

namespace fs = std::filesystem;

namespace {

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
    for (auto it = fs::recursive_directory_iterator(repo_dir); it != fs::recursive_directory_iterator(); ++it) {//递归遍历仓库目录，收集代码文件
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

static std::string get_attr(const std::string& tag, const std::string& key)
{
    std::string needle = key + "=\"";
    size_t pos = tag.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    size_t end = tag.find('"', pos);
    if (end == std::string::npos) return {};
    return tag.substr(pos, end - pos);
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
    if (rule_pos != std::string::npos && msg_part.back() == ']') {
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

std::string quality_result_to_json(const QualityAnalysisResult& r)
{
    std::ostringstream oss;
    oss << "{\"ok\":" << (r.error.empty() ? "true" : "false")
        << ",\"tool\":\"" << util::json_escape(r.tool) << "\""
        << ",\"analyzed_files\":" << r.analyzed_files
        << ",\"issues_inserted\":" << r.issues_inserted
        << ",\"severity_stats\":{";

    bool first = true;
    for (const auto& kv : r.severity_stats) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << util::json_escape(kv.first) << "\":" << kv.second;
    }
    oss << "}";

    if (!r.error.empty()) {
        oss << ",\"error\":\"" << util::json_escape(r.error) << "\"";
    }
    oss << "}";
    return oss.str();
}
