#include "code_index.h"
#include "knowledge_base.h"
#include "common/util.h"
#include "db/db.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <set>
#include <algorithm>
#include <cstdlib>

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
    return "\"" + s + "\"";
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

static std::string sanitize_utf8(const std::string& input)
{
    std::string out;
    out.reserve(input.size());
    auto is_cont = [](unsigned char c) { return (c & 0xC0) == 0x80; };
    for (size_t i = 0; i < input.size();) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c <= 0x7F) { out.push_back(static_cast<char>(c)); ++i; continue; }
        if (c >= 0xC2 && c <= 0xDF && i + 1 < input.size() && is_cont(input[i + 1])) {
            out.append(input, i, 2); i += 2; continue;
        }
        if (c >= 0xE0 && c <= 0xEF && i + 2 < input.size() && is_cont(input[i + 1]) && is_cont(input[i + 2])) {
            out.append(input, i, 3); i += 3; continue;
        }
        if (c >= 0xF0 && c <= 0xF4 && i + 3 < input.size() && is_cont(input[i + 1]) && is_cont(input[i + 2]) && is_cont(input[i + 3])) {
            out.append(input, i, 4); i += 4; continue;
        }
        out.push_back('?');
        ++i;
    }
    return out;
}

static bool is_binary_file(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return false;
    char buf[8192];
    in.read(buf, sizeof(buf));
    std::streamsize n = in.gcount();
    for (std::streamsize i = 0; i < n; ++i) {
        if (buf[i] == '\0') return true;
    }
    return false;
}

static bool is_ignored_dir(const fs::path& p)
{
    static const std::unordered_set<std::string> kIgnored = {//常见的二进制文件夹和构建输出目录，这里可以添加新的忽律规则
        ".git", "node_modules", "dist", "build", "out",
        "vendor", "third_party", ".vscode"
    };
    for (const auto& part : p) {
        std::string name = to_lower(part.string());
        if (kIgnored.count(name)) return true;
    }
    return false;
}

static bool is_allowed_extension(const fs::path& p)
{
    static const std::unordered_set<std::string> kExt = {
        ".cpp", ".h", ".c", ".py", ".java", ".js", ".ts", ".vue",
        ".go", ".rs", ".md", ".txt", ".json", ".yml", ".yaml",
        ".toml", ".sh", ".ps1"
    };
    std::string ext = to_lower(p.extension().string());
    return kExt.count(ext) > 0;
}

static std::string detect_lang(const fs::path& p)
{
    std::string ext = to_lower(p.extension().string());
    if (ext == ".cpp" || ext == ".h" || ext == ".c") return "cpp";
    if (ext == ".py") return "python";
    if (ext == ".java") return "java";
    if (ext == ".js") return "javascript";
    if (ext == ".ts") return "typescript";
    if (ext == ".vue") return "vue";
    if (ext == ".go") return "go";
    if (ext == ".rs") return "rust";
    if (ext == ".md") return "markdown";
    if (ext == ".json") return "json";
    if (ext == ".yml" || ext == ".yaml") return "yaml";
    if (ext == ".toml") return "toml";
    if (ext == ".sh") return "shell";
    if (ext == ".ps1") return "powershell";
    return "text";
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

static std::string build_repo_tree_summary(const fs::path& root, int depth)
{
    std::set<std::string> entries;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        const auto& p = it->path();
        if (is_ignored_dir(p)) {//如果是忽略的目录，跳过并且不进入子目录
            it.disable_recursion_pending();
            continue;
        }

        std::string rel = fs::relative(p, root).generic_string();
        if (rel.empty()) continue;//如果相对路径为空，说明是根目录，跳过
        int d = 1;
        for (char c : rel) if (c == '/') d++;
        if (d > depth) {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }

        if (it->is_directory()) {
            entries.insert(rel + "/");
        } else {
            entries.insert(rel);
        }
    }

    std::ostringstream oss;
    for (const auto& e : entries) {//将目录结构拼接成字符串，作为仓库树的摘要信息
        oss << e << "\n";
    }
    return oss.str();
}

static bool clone_or_pull_repo(const fs::path& repo_dir,
                               const std::string& full_name,
                               const std::string& ref,
                               std::string& err)
{
    std::string url = build_repo_url(full_name);
    std::string ref_arg = ref.empty() ? "main" : ref;

    if (!fs::exists(repo_dir)) {//如果仓库不存在
        fs::create_directories(repo_dir.parent_path());
        std::string cmd = "git clone --depth 1 --branch " + ref_arg + " " + url + " " + quote_path(repo_dir.string());
        int rc = run_cmd(cmd);
        if (rc != 0) {
            err = "git clone failed";
            return false;
        }
        return true;
    }

    if (!fs::exists(repo_dir / ".git")) {//如果git目录不存在，说明不是合法的git仓库
        err = "repo_dir exists but is not a git repo";
        return false;
    }

    std::string cmd_fetch = "git -C " + quote_path(repo_dir.string()) + " fetch --depth 1 origin " + ref_arg;
    if (run_cmd(cmd_fetch) != 0) {//如果拉取失败，可能是网络问题或者ref不存在等
        err = "git fetch failed";
        return false;
    }
    std::string cmd_checkout = "git -C " + quote_path(repo_dir.string()) + " checkout --force FETCH_HEAD";
    if (run_cmd(cmd_checkout) != 0) {//如果切换失败，可能是ref不存在等
        err = "git checkout failed";
        return false;
    }

    return true;
}

static std::string get_head_sha(const fs::path& repo_dir)
{
    std::string cmd = "git -C " + quote_path(repo_dir.string()) + " rev-parse HEAD";
    return run_cmd_capture(cmd);
}

} // namespace

CodeIndexResult build_code_index(Db& db, int repo_id,
                                 const std::string& full_name,
                                 const std::string& ref,
                                 const std::string& mode,
                                 int max_files,
                                 int max_total_kb)
{
    CodeIndexResult result;
    std::string root = get_clone_root();
    fs::path repo_dir = fs::path(root) / std::to_string(repo_id);

    std::string err;
    if (!clone_or_pull_repo(repo_dir, full_name, ref, err)) {//更新代码到本地
        result.skipped_reason["git_error"]++;
        return result;
    }

    result.repo_head_sha = get_head_sha(repo_dir);

    clear_repo_chunks_by_types(db, repo_id, {"repo_tree", "code"});//先清理掉之前的索引数据

    std::string tree = build_repo_tree_summary(repo_dir, 5);//最大递归遍历5层目录
    if (!tree.empty()) {//将仓库树的摘要信息作为一个特殊的知识块插入到知识库中，方便后续查询和分析
        std::string content = "REPO_TREE\n" + tree;
        insert_knowledge_chunk(db, repo_id, "repo_tree",
                               "tree@" + result.repo_head_sha,
                               "repo_tree", content, "", "");
    }

    const bool hot_mode = (to_lower(mode) == "hot");
    int processed_files = 0;
    long long total_kb = 0;

    for (auto it = fs::recursive_directory_iterator(repo_dir); it != fs::recursive_directory_iterator(); ++it) {//递归遍历仓库目录，收集代码文件并生成知识块
        const auto& p = it->path();
        if (is_ignored_dir(p)) {
            it.disable_recursion_pending();
            continue;
        }

        if (!it->is_regular_file()) continue;

        std::string rel = fs::relative(p, repo_dir).generic_string();
        if (rel.empty()) continue;

        if (hot_mode && !is_hot_path(rel)) {
            result.skipped_reason["not_in_hot_scope"]++;
            continue;
        }

        if (!is_allowed_extension(p)) {
            result.skipped_reason["extension_not_allowed"]++;
            continue;
        }

        std::error_code ec;
        auto size_bytes = fs::file_size(p, ec);
        if (ec) {
            result.skipped_reason["stat_error"]++;
            continue;
        }

        int size_kb = static_cast<int>((size_bytes + 1023) / 1024);
        if (size_kb > 256) {
            result.skipped_reason["too_large"]++;
            continue;
        }

        if (max_total_kb > 0 && (total_kb + size_kb) > max_total_kb) {
            result.skipped_reason["max_total_kb"]++;
            break;
        }

        if (is_binary_file(p)) {
            result.skipped_reason["binary"]++;
            continue;
        }

        if (max_files > 0 && processed_files >= max_files) {
            result.skipped_reason["max_files"]++;
            break;
        }

        std::ifstream in(p);
        if (!in.is_open()) {
            result.skipped_reason["read_error"]++;
            continue;
        }

        processed_files++;
        total_kb += size_kb;
        result.indexed_files++;

        const std::string lang = detect_lang(p);
        const int chunk_lines = 250;

        std::string line;
        int line_no = 0;
        int chunk_start = 1;
        std::ostringstream chunk_buf;

        auto flush_chunk = [&](int chunk_end) {
            std::string code = sanitize_utf8(chunk_buf.str());
            if (code.empty()) return;
            std::string title = rel + " (L" + std::to_string(chunk_start) + "-L" + std::to_string(chunk_end) + ")";
            std::string source_id = rel + "@" + result.repo_head_sha + "#L" + std::to_string(chunk_start) + "-L" + std::to_string(chunk_end);
            std::string header = "FILE: " + rel + "\nRANGE: " + std::to_string(chunk_start) + "-" + std::to_string(chunk_end) + "\nLANG: " + lang + "\n\n";
            std::string content = header + code;

            if (insert_knowledge_chunk(db, repo_id, "code", source_id, title, content, "", "")) {
                result.indexed_chunks++;
            }
        };

        while (std::getline(in, line)) {
            line_no++;
            chunk_buf << line << "\n";
            if ((line_no - chunk_start + 1) >= chunk_lines) {
                flush_chunk(line_no);
                chunk_buf.str("");
                chunk_buf.clear();
                chunk_start = line_no + 1;
            }
        }

        if (chunk_buf.tellp() > 0) {
            flush_chunk(line_no);
        }
    }

    result.embeddings_generated = generate_embeddings_for_repo(db, repo_id);
    return result;
}
