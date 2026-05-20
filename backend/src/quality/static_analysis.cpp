#include "static_analysis.h"
#include "quality_score.h"
#include "common/util.h"
#include "db/db.h"

#include <nlohmann/json.hpp>
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
#include <cstdio>

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

struct CommandCaptureResult {
    std::string output;
    int exit_code = 0;
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

static CommandCaptureResult run_cmd_capture_result(const std::string& cmd)
{
    CommandCaptureResult result;
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        result.exit_code = -1;
        return result;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        result.output += buf;
    }
#ifdef _WIN32
    result.exit_code = _pclose(pipe);
#else
    result.exit_code = pclose(pipe);
#endif
    result.output = util::trim(result.output);
    return result;
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

static bool is_python_source(const fs::path& p)
{
    return to_lower(p.extension().string()) == ".py";
}

static bool is_java_source(const fs::path& p)
{
    return to_lower(p.extension().string()) == ".java";
}

static bool is_quality_ignored_path(const std::string& rel_path);

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

static std::vector<fs::path> collect_source_files(const fs::path& repo_dir,
                                                  bool hot_mode,
                                                  int max_files,
                                                  bool (*is_source)(const fs::path&))
{
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(repo_dir); it != fs::recursive_directory_iterator(); ++it) {
        const auto& p = it->path();
        if (is_ignored_dir(p)) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file()) continue;
        if (!is_source(p)) continue;

        std::string rel = fs::relative(p, repo_dir).generic_string();
        if (rel.empty()) continue;
        if (is_quality_ignored_path(rel)) continue;
        if (hot_mode && !is_hot_path(rel)) continue;

        files.push_back(p);
        if (max_files > 0 && static_cast<int>(files.size()) >= max_files) break;
    }
    std::sort(files.begin(), files.end());
    return files;
}

static std::vector<fs::path> collect_cpp_files(const fs::path& repo_dir, bool hot_mode, int max_files)
{
    return collect_source_files(repo_dir, hot_mode, max_files, is_cpp_source);
}

static std::vector<fs::path> collect_python_files(const fs::path& repo_dir, bool hot_mode, int max_files)
{
    return collect_source_files(repo_dir, hot_mode, max_files, is_python_source);
}

static std::vector<fs::path> collect_java_files(const fs::path& repo_dir, bool hot_mode, int max_files)
{
    return collect_source_files(repo_dir, hot_mode, max_files, is_java_source);
}

static bool path_matches_pattern(const std::string& rel, const std::string& raw_pattern)
{
    std::string pattern = util::trim(raw_pattern);
    std::replace(pattern.begin(), pattern.end(), '\\', '/');
    while (pattern.rfind("./", 0) == 0) pattern.erase(0, 2);
    if (pattern.empty()) return false;
    if (rel == pattern) return true;
    if (rel.size() > pattern.size()
        && rel.compare(rel.size() - pattern.size(), pattern.size(), pattern) == 0
        && rel[rel.size() - pattern.size() - 1] == '/') {
        return true;
    }
    if (!pattern.empty() && pattern.back() == '/') {
        return rel.rfind(pattern, 0) == 0;
    }
    return false;
}

static bool is_quality_ignored_path(const std::string& rel_path)
{
    std::string rel = util::trim(rel_path);
    std::replace(rel.begin(), rel.end(), '\\', '/');
    while (rel.rfind("./", 0) == 0) rel.erase(0, 2);
    const std::string configured = util::trim(util::get_env(
        "QUALITY_IGNORE_PATHS",
        "backend/src/httplib.h,src/httplib.h"));
    for (const auto& pattern : split_list(configured)) {
        if (path_matches_pattern(rel, pattern)) return true;
    }
    return false;
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

static std::string canonical_tool_name(std::string tool)
{
    tool = to_lower(util::trim(tool));
    if (tool == "clang_tidy") return "clang-tidy";
    if (tool == "cpp-lint") return "cpplint";
    if (tool == "flaw-finder" || tool == "flaw_finder") return "flawfinder";
    if (tool == "python" || tool == "py-lint" || tool == "py_lint") return "pylint";
    if (tool == "java" || tool == "java-checkstyle" || tool == "java_checkstyle") return "checkstyle";
    return tool;
}

static std::vector<std::string> expand_requested_tools(const std::vector<std::string>& requested)
{
    std::vector<std::string> tools;
    std::set<std::string> seen;

    for (const auto& raw : requested) {
        std::string tool = canonical_tool_name(raw);
        if (tool == "all") {
            std::string configured = util::trim(util::get_env(
                "QUALITY_ALL_TOOLS",
                "cppcheck,clang-tidy"));
            for (const auto& nested : split_list(configured)) {
                std::string nested_tool = canonical_tool_name(nested);
                if (!nested_tool.empty() && nested_tool != "all" && seen.insert(nested_tool).second) {
                    tools.push_back(nested_tool);
                }
            }
            continue;
        }
        if (!tool.empty() && seen.insert(tool).second) tools.push_back(tool);
    }

    return tools;
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

static bool parse_file_line_prefix(const std::string& text,
                                   std::string& file_path,
                                   int& line,
                                   std::string& rest)
{
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != ':') continue;
        size_t j = i + 1;
        if (j >= text.size() || !std::isdigit(static_cast<unsigned char>(text[j]))) continue;
        while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) ++j;
        if (j >= text.size() || text[j] != ':') continue;

        file_path = text.substr(0, i);
        line = std::atoi(text.substr(i + 1, j - i - 1).c_str());
        rest = util::trim(text.substr(j + 1));
        return !file_path.empty() && line > 0;
    }
    return false;
}

static bool parse_cpplint_line(const std::string& line, QualityIssue& out)
{
    size_t conf_open = line.rfind(" [");
    if (conf_open == std::string::npos || line.empty() || line.back() != ']') return false;
    std::string confidence_s = line.substr(conf_open + 2, line.size() - conf_open - 3);
    if (confidence_s.empty()
        || !std::all_of(confidence_s.begin(), confidence_s.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return false;
    }

    size_t rule_close = conf_open;
    while (rule_close > 0 && std::isspace(static_cast<unsigned char>(line[rule_close - 1]))) --rule_close;
    if (rule_close == 0 || line[rule_close - 1] != ']') return false;
    size_t rule_open = line.rfind(" [", rule_close - 1);
    if (rule_open == std::string::npos) return false;

    std::string prefix = line.substr(0, rule_open);
    std::string rest;
    if (!parse_file_line_prefix(prefix, out.file_path, out.line, rest)) return false;

    int confidence = std::atoi(confidence_s.c_str());
    out.column = 0;
    out.rule_id = line.substr(rule_open + 2, rule_close - rule_open - 3);
    out.message = rest;
    out.severity = confidence >= 5 ? "warning" : (confidence >= 3 ? "style" : "information");
    return true;
}

static std::vector<QualityIssue> parse_cpplint_output(const std::string& output, const fs::path& repo_dir)
{
    std::vector<QualityIssue> issues;
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        QualityIssue issue;
        if (parse_cpplint_line(line, issue)) {
            issue.file_path = to_relative_or_full(repo_dir, util::trim(issue.file_path));
            issues.push_back(std::move(issue));
        }
    }
    return issues;
}

static bool parse_flawfinder_line(const std::string& line, QualityIssue& out)
{
    std::string rest;
    if (!parse_file_line_prefix(line, out.file_path, out.line, rest)) return false;

    size_t level_open = rest.find('[');
    size_t level_close = rest.find(']', level_open == std::string::npos ? 0 : level_open + 1);
    if (level_open == std::string::npos || level_close == std::string::npos) return false;
    std::string level_s = rest.substr(level_open + 1, level_close - level_open - 1);
    if (level_s.empty()
        || !std::all_of(level_s.begin(), level_s.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return false;
    }

    int level = std::atoi(level_s.c_str());
    size_t rule_open = rest.find('(', level_close + 1);
    size_t rule_close = rest.find(')', rule_open == std::string::npos ? 0 : rule_open + 1);
    if (rule_open != std::string::npos && rule_close != std::string::npos) {
        out.rule_id = rest.substr(rule_open + 1, rule_close - rule_open - 1);
        out.message = util::trim(rest.substr(rule_close + 1));
    } else {
        out.rule_id = "flawfinder-level-" + std::to_string(level);
        out.message = util::trim(rest.substr(level_close + 1));
    }
    out.column = 0;
    out.severity = level >= 4 ? "error" : (level >= 2 ? "warning" : "information");
    return true;
}

static std::vector<QualityIssue> parse_flawfinder_output(const std::string& output, const fs::path& repo_dir)
{
    std::vector<QualityIssue> issues;
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        QualityIssue issue;
        if (parse_flawfinder_line(line, issue)) {
            issue.file_path = to_relative_or_full(repo_dir, util::trim(issue.file_path));
            issues.push_back(std::move(issue));
        }
    }
    return issues;
}

static std::string pylint_severity(const std::string& type)
{
    const std::string t = to_lower(util::trim(type));
    if (t == "fatal" || t == "error") return "error";
    if (t == "warning") return "warning";
    if (t == "convention" || t == "refactor") return "style";
    if (t == "information" || t == "info") return "information";
    return t.empty() ? "warning" : t;
}

static std::vector<QualityIssue> parse_pylint_json(const std::string& output, const fs::path& repo_dir)
{
    std::vector<QualityIssue> issues;
    try {
        auto json = nlohmann::json::parse(output);
        if (!json.is_array()) return issues;

        for (const auto& item : json) {
            if (!item.is_object()) continue;

            QualityIssue issue;
            issue.file_path = item.value("path", "");
            if (issue.file_path.empty()) issue.file_path = item.value("abspath", "");
            issue.file_path = to_relative_or_full(repo_dir, util::trim(issue.file_path));
            issue.line = item.value("line", 0);
            issue.column = item.value("column", 0);
            if (issue.column > 0) ++issue.column;
            std::string msg_id = item.value("message-id", "");
            std::string symbol = item.value("symbol", "");
            issue.rule_id = msg_id.empty() ? symbol : (symbol.empty() ? msg_id : msg_id + "/" + symbol);
            issue.severity = pylint_severity(item.value("type", ""));
            issue.message = item.value("message", "");

            if (!issue.file_path.empty()) issues.push_back(std::move(issue));
        }
    } catch (const std::exception&) {
        return {};
    }
    return issues;
}

static std::string checkstyle_rule_id(std::string source)
{
    source = util::trim(source);
    if (source.empty()) return "checkstyle";
    const size_t pos = source.find_last_of('.');
    return pos == std::string::npos ? source : source.substr(pos + 1);
}

static std::vector<QualityIssue> parse_checkstyle_xml(const std::string& xml, const fs::path& repo_dir)
{
    std::vector<QualityIssue> issues;
    size_t pos = 0;
    while (true) {
        size_t file_start = xml.find("<file ", pos);
        if (file_start == std::string::npos) break;
        size_t file_tag_end = xml.find('>', file_start);
        if (file_tag_end == std::string::npos) break;
        size_t file_end = xml.find("</file>", file_tag_end);
        if (file_end == std::string::npos) break;

        std::string file_tag = xml.substr(file_start, file_tag_end - file_start + 1);
        std::string file_path = to_relative_or_full(repo_dir, get_attr(file_tag, "name"));
        std::string block = xml.substr(file_tag_end + 1, file_end - file_tag_end - 1);

        size_t err_pos = 0;
        while (true) {
            size_t err_start = block.find("<error ", err_pos);
            if (err_start == std::string::npos) break;
            size_t err_tag_end = block.find('>', err_start);
            if (err_tag_end == std::string::npos) break;

            std::string err_tag = block.substr(err_start, err_tag_end - err_start + 1);
            QualityIssue issue;
            issue.file_path = file_path;
            const std::string line = get_attr(err_tag, "line");
            const std::string col = get_attr(err_tag, "column");
            if (!line.empty()) issue.line = std::atoi(line.c_str());
            if (!col.empty()) issue.column = std::atoi(col.c_str());
            issue.rule_id = checkstyle_rule_id(get_attr(err_tag, "source"));
            issue.severity = get_attr(err_tag, "severity");
            if (to_lower(issue.severity) == "info") issue.severity = "information";
            issue.message = get_attr(err_tag, "message");

            if (!issue.file_path.empty()) issues.push_back(std::move(issue));
            err_pos = err_tag_end + 1;
        }

        pos = file_end + 7;
    }
    return issues;
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
        status = s ? reinterpret_cast<const char*>(s) : "";
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
    std::tm tm_value{};
#ifdef _WIN32
    localtime_s(&tm_value, &t);
#else
    localtime_r(&t, &tm_value);
#endif
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_value);
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
        if (to_lower(issue.severity) == "note") continue;
        if (issue.file_path.rfind("../", 0) == 0 || issue.file_path.find(":/") != std::string::npos) continue;
        if (is_quality_ignored_path(issue.file_path)) continue;
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
        repo_dir / "backend" / "src",
        repo_dir / "vcpkg" / "installed" / "x64-windows" / "include",
        repo_dir / "backend" / "vcpkg" / "installed" / "x64-windows" / "include"
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

static bool write_text_file(const fs::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    out << content;
    return true;
}

static void append_text_file(const fs::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (out.is_open()) out << content;
}

static std::string first_non_empty_line(const std::string& s)
{
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        line = util::trim(line);
        if (!line.empty()) return line;
    }
    return {};
}

static bool output_looks_like_missing_tool(const std::string& output)
{
    const std::string o = to_lower(output);
    return o.find("not recognized") != std::string::npos
        || o.find("not found") != std::string::npos
        || o.find("no such file") != std::string::npos
        || o.find("is not installed") != std::string::npos;
}

static void deduplicate_issues(const std::string& tool, std::vector<QualityIssue>& issues)
{
    std::set<std::string> seen;
    std::vector<QualityIssue> unique;
    unique.reserve(issues.size());
    for (auto& issue : issues) {
        std::string key = issue_key_for(tool, issue);
        if (seen.insert(key).second) unique.push_back(std::move(issue));
    }
    issues.swap(unique);
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
    deduplicate_issues(result.tool, result.issues);
    for (const auto& issue : result.issues) {
        result.severity_stats[issue.severity]++;
    }
    return result;
}

static fs::path resolve_existing_compile_commands_dir(const fs::path& repo_dir);
static fs::path prepare_fallback_compile_commands(const fs::path& repo_dir,
                                                  const std::vector<fs::path>& files,
                                                  int repo_id,
                                                  const std::string& stamp,
                                                  std::string& error);

static ToolExecutionResult execute_clang_tidy(const fs::path& repo_dir,
                                              const std::vector<fs::path>& files,
                                              int repo_id)
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

    std::string out_dir = ensure_output_dir();
    std::string stamp = make_timestamp();
    fs::path out_file = fs::path(out_dir) / ("clang_tidy_" + std::to_string(repo_id) + "_" + stamp + ".log");
    result.output_file = out_file.string();

    fs::path cc_dir = resolve_existing_compile_commands_dir(repo_dir);
    if (cc_dir.empty()) {
        std::string cc_error;
        cc_dir = prepare_fallback_compile_commands(repo_dir, tool_files, repo_id, stamp, cc_error);
        if (cc_dir.empty()) {
            result.error = cc_error.empty()
                ? "compile_commands.json not found (set CLANG_TIDY_COMPILE_COMMANDS)"
                : cc_error;
            return result;
        }
    }

    std::string bin = util::trim(util::get_env("CLANG_TIDY_BIN", "clang-tidy"));
    std::string extra = util::trim(util::get_env("CLANG_TIDY_ARGS", ""));
    std::vector<std::string> failed_samples;

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
        CommandCaptureResult captured = run_cmd_capture_result(cmd_str);
        append_text_file(out_file, "### " + f.generic_string() + "\n" + captured.output + "\n\n");
        auto parsed = parse_clang_tidy_output(captured.output, repo_dir);
        if (captured.exit_code != 0 && parsed.empty()) {
            if (output_looks_like_missing_tool(captured.output)) {
                result.error = "clang-tidy command failed: " + first_non_empty_line(captured.output);
                return result;
            }
            if (failed_samples.size() < 3) failed_samples.push_back(f.generic_string());
        }
        result.issues.insert(result.issues.end(),
                             std::make_move_iterator(parsed.begin()),
                             std::make_move_iterator(parsed.end()));
    }

    normalize_and_filter_issues(result.issues);
    deduplicate_issues(result.tool, result.issues);
    if (result.issues.empty() && !failed_samples.empty()) {
        result.error = "clang-tidy failed for " + std::to_string(failed_samples.size())
            + " file(s); see " + out_file.string();
        return result;
    }
    for (const auto& issue : result.issues) {
        result.severity_stats[issue.severity]++;
    }
    return result;
}

static ToolExecutionResult execute_cpplint(const fs::path& repo_dir,
                                           const std::vector<fs::path>& files,
                                           int repo_id)
{
    ToolExecutionResult result;
    result.tool = "cpplint";

    std::vector<fs::path> tool_files = tool_source_files(files, true);
    result.analyzed_files = static_cast<int>(tool_files.size());
    result.lines_analyzed = count_lines(tool_files);

    if (tool_files.empty()) {
        result.error = "no source files to analyze";
        return result;
    }

    std::string out_dir = ensure_output_dir();
    std::string stamp = make_timestamp();
    fs::path out_file = fs::path(out_dir) / ("cpplint_" + std::to_string(repo_id) + "_" + stamp + ".log");
    result.output_file = out_file.string();

    std::string bin = util::trim(util::get_env("CPPLINT_BIN", "cpplint"));
    std::string extra = util::trim(util::get_env("CPPLINT_ARGS", ""));
    std::vector<std::string> failed_samples;

    for (const auto& f : tool_files) {
        std::ostringstream cmd;
        cmd << quote_path(bin);
        if (!extra.empty()) cmd << " " << extra;
        cmd << " " << quote_path(f.string()) << " 2>&1";

        std::string cmd_str = cmd.str();
#ifdef _WIN32
        cmd_str = "cmd /c \"" + cmd_str + "\"";
#endif
        CommandCaptureResult captured = run_cmd_capture_result(cmd_str);
        append_text_file(out_file, "### " + f.generic_string() + "\n" + captured.output + "\n\n");
        auto parsed = parse_cpplint_output(captured.output, repo_dir);
        if (captured.exit_code != 0 && parsed.empty()) {
            if (output_looks_like_missing_tool(captured.output)) {
                result.error = "cpplint command failed: " + first_non_empty_line(captured.output);
                return result;
            }
            if (failed_samples.size() < 3) failed_samples.push_back(f.generic_string());
        }
        result.issues.insert(result.issues.end(),
                             std::make_move_iterator(parsed.begin()),
                             std::make_move_iterator(parsed.end()));
    }

    normalize_and_filter_issues(result.issues);
    deduplicate_issues(result.tool, result.issues);
    if (result.issues.empty() && !failed_samples.empty()) {
        result.error = "cpplint failed for " + std::to_string(failed_samples.size())
            + " file(s); see " + out_file.string();
        return result;
    }
    for (const auto& issue : result.issues) {
        result.severity_stats[issue.severity]++;
    }
    return result;
}

static ToolExecutionResult execute_flawfinder(const fs::path& repo_dir,
                                              const std::vector<fs::path>& files,
                                              int repo_id)
{
    ToolExecutionResult result;
    result.tool = "flawfinder";

    std::vector<fs::path> tool_files = tool_source_files(files, true);
    result.analyzed_files = static_cast<int>(tool_files.size());
    result.lines_analyzed = count_lines(tool_files);

    if (tool_files.empty()) {
        result.error = "no source files to analyze";
        return result;
    }

    std::string out_dir = ensure_output_dir();
    std::string stamp = make_timestamp();
    fs::path out_file = fs::path(out_dir) / ("flawfinder_" + std::to_string(repo_id) + "_" + stamp + ".log");
    result.output_file = out_file.string();

    std::string bin = util::trim(util::get_env("FLAWFINDER_BIN", "flawfinder"));
    std::string extra = util::trim(util::get_env("FLAWFINDER_ARGS", "--quiet"));
    std::vector<std::string> failed_samples;

    for (const auto& f : tool_files) {
        std::ostringstream cmd;
        cmd << quote_path(bin);
        if (!extra.empty()) cmd << " " << extra;
        cmd << " " << quote_path(f.string()) << " 2>&1";

        std::string cmd_str = cmd.str();
#ifdef _WIN32
        cmd_str = "cmd /c \"" + cmd_str + "\"";
#endif
        CommandCaptureResult captured = run_cmd_capture_result(cmd_str);
        append_text_file(out_file, "### " + f.generic_string() + "\n" + captured.output + "\n\n");
        auto parsed = parse_flawfinder_output(captured.output, repo_dir);
        if (captured.exit_code != 0 && parsed.empty()) {
            if (output_looks_like_missing_tool(captured.output)) {
                result.error = "flawfinder command failed: " + first_non_empty_line(captured.output);
                return result;
            }
            if (failed_samples.size() < 3) failed_samples.push_back(f.generic_string());
        }
        result.issues.insert(result.issues.end(),
                             std::make_move_iterator(parsed.begin()),
                             std::make_move_iterator(parsed.end()));
    }

    normalize_and_filter_issues(result.issues);
    deduplicate_issues(result.tool, result.issues);
    if (result.issues.empty() && !failed_samples.empty()) {
        result.error = "flawfinder failed for " + std::to_string(failed_samples.size())
            + " file(s); see " + out_file.string();
        return result;
    }
    for (const auto& issue : result.issues) {
        result.severity_stats[issue.severity]++;
    }
    return result;
}

static ToolExecutionResult execute_pylint(const fs::path& repo_dir,
                                          const std::vector<fs::path>& files,
                                          int repo_id)
{
    ToolExecutionResult result;
    result.tool = "pylint";
    result.analyzed_files = static_cast<int>(files.size());
    result.lines_analyzed = count_lines(files);

    if (files.empty()) {
        result.error = "no Python source files to analyze";
        return result;
    }

    std::string out_dir = ensure_output_dir();
    std::string stamp = make_timestamp();
    fs::path out_file = fs::path(out_dir) / ("pylint_" + std::to_string(repo_id) + "_" + stamp + ".log");
    result.output_file = out_file.string();

    std::string bin = util::trim(util::get_env("PYLINT_BIN", "pylint"));
    std::string extra = util::trim(util::get_env("PYLINT_ARGS", "--score=n"));
    std::vector<std::string> failed_samples;

    for (const auto& f : files) {
        std::ostringstream cmd;
        cmd << quote_path(bin)
            << " --output-format=json";
        if (!extra.empty()) cmd << " " << extra;
        cmd << " " << quote_path(f.string()) << " 2>&1";

        std::string cmd_str = cmd.str();
#ifdef _WIN32
        cmd_str = "cmd /c \"" + cmd_str + "\"";
#endif
        CommandCaptureResult captured = run_cmd_capture_result(cmd_str);
        append_text_file(out_file, "### " + f.generic_string() + "\n" + captured.output + "\n\n");
        auto parsed = parse_pylint_json(captured.output, repo_dir);
        if (captured.exit_code != 0 && parsed.empty()) {
            if (output_looks_like_missing_tool(captured.output)) {
                result.error = "pylint command failed: " + first_non_empty_line(captured.output);
                return result;
            }
            if (failed_samples.size() < 3) failed_samples.push_back(f.generic_string());
        }
        result.issues.insert(result.issues.end(),
                             std::make_move_iterator(parsed.begin()),
                             std::make_move_iterator(parsed.end()));
    }

    normalize_and_filter_issues(result.issues);
    deduplicate_issues(result.tool, result.issues);
    if (result.issues.empty() && !failed_samples.empty()) {
        result.error = "pylint failed for " + std::to_string(failed_samples.size())
            + " file(s); see " + out_file.string();
        return result;
    }
    for (const auto& issue : result.issues) {
        result.severity_stats[issue.severity]++;
    }
    return result;
}

static ToolExecutionResult execute_checkstyle(const fs::path& repo_dir,
                                              const std::vector<fs::path>& files,
                                              int repo_id)
{
    ToolExecutionResult result;
    result.tool = "checkstyle";
    result.analyzed_files = static_cast<int>(files.size());
    result.lines_analyzed = count_lines(files);

    if (files.empty()) {
        result.error = "no Java source files to analyze";
        return result;
    }

    std::string out_dir = ensure_output_dir();
    std::string stamp = make_timestamp();
    fs::path out_file = fs::path(out_dir) / ("checkstyle_" + std::to_string(repo_id) + "_" + stamp + ".xml");
    result.output_file = out_file.string();

    std::string bin = util::trim(util::get_env("CHECKSTYLE_BIN", "checkstyle"));
    std::string config = util::trim(util::get_env("CHECKSTYLE_CONFIG", "/google_checks.xml"));
    std::string extra = util::trim(util::get_env("CHECKSTYLE_ARGS", ""));
    std::vector<std::string> failed_samples;

    for (const auto& f : files) {
        std::ostringstream cmd;
        cmd << quote_path(bin)
            << " -f xml -c " << quote_path(config);
        if (!extra.empty()) cmd << " " << extra;
        cmd << " " << quote_path(f.string()) << " 2>&1";

        std::string cmd_str = cmd.str();
#ifdef _WIN32
        cmd_str = "cmd /c \"" + cmd_str + "\"";
#endif
        CommandCaptureResult captured = run_cmd_capture_result(cmd_str);
        append_text_file(out_file, "### " + f.generic_string() + "\n" + captured.output + "\n\n");
        auto parsed = parse_checkstyle_xml(captured.output, repo_dir);
        if (captured.exit_code != 0 && parsed.empty()) {
            if (output_looks_like_missing_tool(captured.output)) {
                result.error = "checkstyle command failed: " + first_non_empty_line(captured.output);
                return result;
            }
            if (failed_samples.size() < 3) failed_samples.push_back(f.generic_string());
        }
        result.issues.insert(result.issues.end(),
                             std::make_move_iterator(parsed.begin()),
                             std::make_move_iterator(parsed.end()));
    }

    normalize_and_filter_issues(result.issues);
    deduplicate_issues(result.tool, result.issues);
    if (result.issues.empty() && !failed_samples.empty()) {
        result.error = "checkstyle failed for " + std::to_string(failed_samples.size())
            + " file(s); see " + out_file.string();
        return result;
    }
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

static fs::path resolve_existing_compile_commands_dir(const fs::path& repo_dir)
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

static fs::path prepare_fallback_compile_commands(const fs::path& repo_dir,
                                                  const std::vector<fs::path>& files,
                                                  int repo_id,
                                                  const std::string& stamp,
                                                  std::string& error)
{
    if (!env_truthy("CLANG_TIDY_FALLBACK_COMPILE_COMMANDS", true)) {
        error = "compile_commands.json not found (set CLANG_TIDY_COMPILE_COMMANDS)";
        return {};
    }

    fs::path out_root = fs::path(ensure_output_dir())
        / ("clang_tidy_compile_commands_" + std::to_string(repo_id) + "_" + stamp);
    std::error_code ec;
    fs::create_directories(out_root, ec);
    if (ec) {
        error = "failed to create fallback compile_commands directory";
        return {};
    }

    const std::string compiler = util::trim(util::get_env("CLANG_TIDY_FALLBACK_COMPILER", "clang++"));
    const std::string fallback_args = util::trim(util::get_env("CLANG_TIDY_FALLBACK_ARGS", "-std=c++17"));
    std::vector<fs::path> include_dirs = discover_include_dirs(repo_dir);

    std::ostringstream json;
    json << "[\n";
    for (size_t i = 0; i < files.size(); ++i) {
        std::ostringstream command;
        command << quote_path(compiler);
        if (!fallback_args.empty()) command << " " << fallback_args;
        for (const auto& dir : include_dirs) {
            command << " -I" << quote_path(dir.string());
        }
        command << " -c " << quote_path(files[i].string());

        if (i) json << ",\n";
        json << "  {"
             << "\"directory\":\"" << util::json_escape(repo_dir.string()) << "\","
             << "\"command\":\"" << util::json_escape(command.str()) << "\","
             << "\"file\":\"" << util::json_escape(files[i].string()) << "\""
             << "}";
    }
    json << "\n]\n";

    if (!write_text_file(out_root / "compile_commands.json", json.str())) {
        error = "failed to write fallback compile_commands.json";
        return {};
    }
    return out_root;
}

} // namespace


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

    const std::vector<std::string> requested_tools = split_list(tools.empty() ? "cppcheck" : tools);
    const std::vector<std::string> tool_names = expand_requested_tools(requested_tools);
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
    result.tool = normalized_tools;
    const int run_id = insert_quality_run(db, repo_id, task_id, ref, normalized_tools, mode, max_files, config_json);
    result.run_id = run_id;
    if (run_id <= 0) {
        result.status = "Failed";
        result.error = "failed to create quality analysis run";
        return result;
    }

    const bool hot_mode = (to_lower(mode) == "hot");

    for (const auto& raw_tool : tool_names) {
        std::string tool_lower = canonical_tool_name(raw_tool);
        if (tool_lower == "cppcheck") {
            auto files = collect_cpp_files(repo_dir, hot_mode, max_files);
            ToolExecutionResult cppcheck = execute_cppcheck(repo_dir, files, repo_id);
            merge_tool_result(result, db, repo_id, run_id, cppcheck);
            continue;
        }
        if (tool_lower == "clang-tidy") {
            auto files = collect_cpp_files(repo_dir, hot_mode, max_files);
            ToolExecutionResult clang_tidy = execute_clang_tidy(repo_dir, files, repo_id);
            merge_tool_result(result, db, repo_id, run_id, clang_tidy);
            continue;
        }
        if (tool_lower == "cpplint") {
            auto files = collect_cpp_files(repo_dir, hot_mode, max_files);
            ToolExecutionResult cpplint = execute_cpplint(repo_dir, files, repo_id);
            merge_tool_result(result, db, repo_id, run_id, cpplint);
            continue;
        }
        if (tool_lower == "flawfinder") {
            auto files = collect_cpp_files(repo_dir, hot_mode, max_files);
            ToolExecutionResult flawfinder = execute_flawfinder(repo_dir, files, repo_id);
            merge_tool_result(result, db, repo_id, run_id, flawfinder);
            continue;
        }
        if (tool_lower == "pylint") {
            auto files = collect_python_files(repo_dir, hot_mode, max_files);
            ToolExecutionResult pylint = execute_pylint(repo_dir, files, repo_id);
            merge_tool_result(result, db, repo_id, run_id, pylint);
            continue;
        }
        if (tool_lower == "checkstyle") {
            auto files = collect_java_files(repo_dir, hot_mode, max_files);
            ToolExecutionResult checkstyle = execute_checkstyle(repo_dir, files, repo_id);
            merge_tool_result(result, db, repo_id, run_id, checkstyle);
            continue;
        }

        if (!result.error.empty()) result.error += "; ";
        result.error += "unsupported tool: " + raw_tool;
    }

    result.status = result.error.empty() ? "Finished" : "Failed";
    QualityScore score = compute_quality_score_for_run(db, run_id);
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
