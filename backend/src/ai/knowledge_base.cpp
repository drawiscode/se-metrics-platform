// 2.4(1) 工程知识库 - 实现
#include "knowledge_base.h"
#include "db/db.h"
#include "common/util.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include <httplib.h>

// ============================================================
// 向量检索: URL 解析（复用 ai_assistant.cpp 的模式）
// ============================================================
struct EmbUrlParts {
    std::string host;
    int port = 443;
    bool use_ssl = true;
    std::string path_prefix;
};

static EmbUrlParts parse_emb_url(const std::string& url)
{
    EmbUrlParts p;
    std::string rest = url;
    if (rest.rfind("https://", 0) == 0) {
        rest = rest.substr(8); p.use_ssl = true; p.port = 443;
    } else if (rest.rfind("http://", 0) == 0) {
        rest = rest.substr(7); p.use_ssl = false; p.port = 80;
    }
    auto slash = rest.find('/');
    if (slash != std::string::npos) {
        p.path_prefix = rest.substr(slash);
        rest = rest.substr(0, slash);
    }
    auto colon = rest.find(':');
    if (colon != std::string::npos) {
        p.host = rest.substr(0, colon);
        try { p.port = std::stoi(rest.substr(colon + 1)); } catch (...) {}
    } else {
        p.host = rest;
    }
    while (!p.path_prefix.empty() && p.path_prefix.back() == '/')
        p.path_prefix.pop_back();
    return p;
}

// ============================================================
// 向量检索: Embedding 序列化/反序列化（原始 float32 字节）
// ============================================================
static std::string serialize_embedding(const std::vector<float>& vec)
{
    std::string blob(reinterpret_cast<const char*>(vec.data()), vec.size() * sizeof(float));
    return blob;
}

static std::vector<float> deserialize_embedding(const void* blob, int bytes)
{
    if (!blob || bytes <= 0 || bytes % sizeof(float) != 0) return {};
    int count = bytes / static_cast<int>(sizeof(float));
    std::vector<float> vec(count);
    std::memcpy(vec.data(), blob, bytes);
    return vec;
}

// ============================================================
// 向量检索: UTF-8 清洗（过滤非法字节，避免 JSON 序列化异常）
// ============================================================
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
        out.push_back('?'); ++i;
    }
    return out;
}

// ============================================================
// 向量检索: Cosine Similarity
// ============================================================
double cosine_similarity(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size() || a.empty()) return 0.0;
    double dot = 0.0, norm_a = 0.0, norm_b = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot    += static_cast<double>(a[i]) * b[i];
        norm_a += static_cast<double>(a[i]) * a[i];
        norm_b += static_cast<double>(b[i]) * b[i];
    }
    if (norm_a < 1e-12 || norm_b < 1e-12) return 0.0;
    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

// ============================================================
// 向量检索: 调用 Embedding API（兼容 OpenAI Embeddings 格式）
// 支持批量输入，返回多个向量
// ============================================================
static std::vector<std::vector<float>> call_embedding_api_batch(
    const std::vector<std::string>& texts, std::string& error_out)
{
    std::vector<std::vector<float>> results;
    if (texts.empty()) return results;

    // 读取配置：优先使用 EMBEDDING_ 专用配置，否则复用 LLM_ 配置
    std::string api_base = util::get_env("EMBEDDING_API_BASE", "");
    if (api_base.empty()) api_base = util::get_env("LLM_API_BASE", "");
    std::string api_key = util::get_env("EMBEDDING_API_KEY", "");
    if (api_key.empty()) api_key = util::get_env("LLM_API_KEY", "");
    std::string model = util::get_env("EMBEDDING_MODEL", "text-embedding-v3");

    if (api_base.empty() || api_key.empty()) {
        error_out = "Embedding API 未配置 (EMBEDDING_API_BASE/LLM_API_BASE 或 API_KEY 为空)";
        return results;
    }

    auto url = parse_emb_url(api_base);
    if (url.host.empty()) {
        error_out = "Embedding API URL 无效";
        return results;
    }

    // 构造请求体（清洗 UTF-8 避免 JSON 序列化异常）
    std::vector<std::string> safe_texts;
    safe_texts.reserve(texts.size());
    for (const auto& t : texts) {
        safe_texts.push_back(sanitize_utf8(t));
    }

    nlohmann::json body;
    body["model"] = model;
    body["input"] = safe_texts;
    body["encoding_format"] = "float";
    std::string body_str = body.dump();

    // 智能拼接路径
    std::string path;
    if (url.path_prefix.size() >= 3 &&
        url.path_prefix.substr(url.path_prefix.size() - 3) == "/v1") {
        path = url.path_prefix + "/embeddings";
    } else {
        path = url.path_prefix + "/v1/embeddings";
    }

    httplib::Headers headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Authorization", "Bearer " + api_key);

    std::string response_text;

#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) || defined(HTTPLIB_OPENSSL_SUPPORT)
    if (url.use_ssl) {
        httplib::SSLClient cli(url.host, url.port);
        cli.set_read_timeout(60, 0);
        cli.set_connection_timeout(10, 0);
        auto res = cli.Post(path, headers, body_str, "application/json");
        if (!res) {
            error_out = "Embedding API 请求失败 (SSL): " + httplib::to_string(res.error());
            return results;
        }
        if (res->status != 200) {
            error_out = "Embedding API 返回状态 " + std::to_string(res->status) + ": " + res->body;
            return results;
        }
        response_text = res->body;
    } else
#endif
    {
        httplib::Client cli(url.host, url.port);
        cli.set_read_timeout(60, 0);
        cli.set_connection_timeout(10, 0);
        auto res = cli.Post(path, headers, body_str, "application/json");
        if (!res) {
            error_out = "Embedding API 请求失败";
            return results;
        }
        if (res->status != 200) {
            error_out = "Embedding API 返回状态 " + std::to_string(res->status) + ": " + res->body;
            return results;
        }
        response_text = res->body;
    }

    // 解析响应: {"data": [{"embedding": [...], "index": 0}, ...]}
    try {
        auto j = nlohmann::json::parse(response_text);
        if (!j.contains("data") || !j["data"].is_array()) {
            error_out = "Embedding API 响应格式异常: 缺少 data 字段";
            return results;
        }

        // 按 index 排序确保顺序正确
        results.resize(texts.size());
        for (const auto& item : j["data"]) {
            int idx = item.value("index", -1);
            if (idx < 0 || idx >= static_cast<int>(texts.size())) continue;
            auto& emb_arr = item["embedding"];
            if (!emb_arr.is_array()) continue;
            std::vector<float> vec;
            vec.reserve(emb_arr.size());
            for (const auto& v : emb_arr) {
                vec.push_back(v.get<float>());
            }
            results[idx] = std::move(vec);
        }
    } catch (const std::exception& e) {
        error_out = std::string("Embedding API 响应解析失败: ") + e.what();
        results.clear();
    }
    return results;
}

// 单条文本 embedding 的便捷接口
std::vector<float> call_embedding_api(const std::string& text)
{
    std::string error;
    auto batch = call_embedding_api_batch({text}, error);
    if (!error.empty()) {
        std::cerr << "[embedding] " << error << "\n";
    }
    if (batch.empty() || batch[0].empty()) return {};
    return batch[0];
}

// ============================================================
// 向量检索: 将 embedding 存入 knowledge_chunks
// ============================================================
static void store_embedding(Db& db, int chunk_id, const std::vector<float>& embedding)
{
    if (embedding.empty()) return;
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE knowledge_chunks SET embedding=?1 WHERE id=?2;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    std::string blob = serialize_embedding(embedding);
    sqlite3_bind_blob(stmt, 1, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, chunk_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ============================================================
// 向量检索: 批量为某仓库的知识块生成 embedding
// ============================================================
static int generate_embeddings_for_repo(Db& db, int repo_id)
{
    // 读取所有尚无 embedding 的知识块
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, title, content FROM knowledge_chunks "
        "WHERE repo_id=?1 AND (embedding IS NULL OR LENGTH(embedding)=0);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, repo_id);

    struct ChunkInfo { int id; std::string text; };
    std::vector<ChunkInfo> pending;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ChunkInfo ci;
        ci.id = sqlite3_column_int(stmt, 0);
        auto t = sqlite3_column_text(stmt, 1);
        auto c = sqlite3_column_text(stmt, 2);
        ci.text = std::string(t ? (const char*)t : "") + " " + std::string(c ? (const char*)c : "");
        pending.push_back(std::move(ci));
    }
    sqlite3_finalize(stmt);

    if (pending.empty()) return 0;

    int total_generated = 0;
    const int batch_size = 6;  // DashScope text-embedding-v3 批量上限 10，留余量取 6

    for (size_t offset = 0; offset < pending.size(); offset += batch_size) {
        size_t end = std::min(offset + batch_size, pending.size());
        std::vector<std::string> texts;
        for (size_t i = offset; i < end; ++i) {
            // 截取前 2000 字符，避免超过 API 限制
            std::string& t = pending[i].text;
            if (t.size() > 2000) t = t.substr(0, 2000);
            texts.push_back(t);
        }

        std::string error;
        auto embeddings = call_embedding_api_batch(texts, error);
        if (!error.empty()) {
            std::cerr << "[embedding] 批量生成失败 (offset=" << offset << "): " << error << "\n";
            continue;  // 跳过这一批，继续下一批
        }

        for (size_t i = 0; i < texts.size() && i < embeddings.size(); ++i) {
            if (!embeddings[i].empty()) {
                store_embedding(db, pending[offset + i].id, embeddings[i]);
                total_generated++;
            }
        }
    }

    std::cerr << "[embedding] repo_id=" << repo_id
              << " 生成向量: " << total_generated << "/" << pending.size() << "\n";
    return total_generated;
}

static std::vector<std::string> split_utf8_chars(const std::string& s)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > s.size()) len = 1;
        out.push_back(s.substr(i, len));
        i += len;
    }
    return out;
}

// ============================================================
// 辅助: 从用户查询中提取关键词
// 规则: 按空格/标点分隔; 中文与 ASCII 交界处分隔; 去重
// ============================================================
static std::vector<std::string> extract_keywords(const std::string& query)
{
    std::vector<std::string> keywords;
    std::string cur_ascii;   // 正在累积的 ASCII 单词
    std::string cur_cjk;     // 正在累积的 CJK 片段

    auto flush_ascii = [&]() {
        if (cur_ascii.empty()) return;
        std::transform(cur_ascii.begin(), cur_ascii.end(), cur_ascii.begin(), ::tolower);
        if (cur_ascii.size() >= 2)          // 过滤单字母（a/I 等无意义）
            keywords.push_back(cur_ascii);
        cur_ascii.clear();
    };
    auto flush_cjk = [&]() {
        if (cur_cjk.empty()) return;

        // 中文短语直接作为候选词，同时补充双字切片提升"贡献者/告警"等问法召回率。
        auto chars = split_utf8_chars(cur_cjk);
        if (chars.size() <= 4) {
            keywords.push_back(cur_cjk);
        }
        if (chars.size() == 1) {
            keywords.push_back(chars[0]);
        } else {
            for (size_t i = 0; i + 1 < chars.size(); ++i) {
                keywords.push_back(chars[i] + chars[i + 1]);
            }
        }
        cur_cjk.clear();
    };

    for (size_t i = 0; i < query.size(); ) {
        unsigned char c = query[i];

        if (c >= 0x80) {                       // 多字节 UTF-8（中文等）
            flush_ascii();
            int len = 1;
            if      ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            if (i + len <= query.size())
                cur_cjk += query.substr(i, len);
            i += len;
        } else if (std::isalnum(c)) {          // ASCII 字母/数字
            flush_cjk();
            cur_ascii += static_cast<char>(c);
            i++;
        } else {                               // 空格/标点 → 分隔符
            flush_ascii();
            flush_cjk();
            i++;
        }
    }
    flush_ascii();
    flush_cjk();

    // 去重
    std::sort(keywords.begin(), keywords.end());
    keywords.erase(std::unique(keywords.begin(), keywords.end()), keywords.end());
    return keywords;
}

// ============================================================
// 辅助: 清除某仓库的旧知识块
// ============================================================
static void clear_repo_chunks(Db& db, int repo_id)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM knowledge_chunks WHERE repo_id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, repo_id);
        sqlite3_step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
}

// ============================================================
// 辅助: 插入一条知识块
// ============================================================
static bool insert_chunk(Db& db, int repo_id,
                         const std::string& source_type, const std::string& source_id,
                         const std::string& title, const std::string& content,
                         const std::string& author, const std::string& event_time)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO knowledge_chunks(repo_id, source_type, source_id, title, content, author, event_time) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7);";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int (stmt, 1, repo_id);
    sqlite3_bind_text(stmt, 2, source_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, source_id.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, title.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, content.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, author.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, event_time.c_str(),   -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// ============================================================
// 索引 Issues
// ============================================================
static int index_issues(Db& db, int repo_id)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT number, state, title, created_at, closed_at, comments, author_login, raw_json "
        "FROM issues WHERE repo_id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, repo_id);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int    number   = sqlite3_column_int(stmt, 0);
        auto   state    = (const char*)sqlite3_column_text(stmt, 1);
        auto   title    = (const char*)sqlite3_column_text(stmt, 2);
        auto   created  = (const char*)sqlite3_column_text(stmt, 3);
        auto   closed   = (const char*)sqlite3_column_text(stmt, 4);
        int    comments = sqlite3_column_int(stmt, 5);
        auto   author   = (const char*)sqlite3_column_text(stmt, 6);
        auto   raw      = (const char*)sqlite3_column_text(stmt, 7);

        // 标题
        std::string s_title = "Issue #" + std::to_string(number) + ": " + (title ? title : "");

        // 从 raw_json 提取 body 正文（截取前 500 字符）
        std::string body;
        if (raw) {
            try {
                auto j = nlohmann::json::parse(raw);
                body = j.value("body", "");
                if (body.size() > 500) body = body.substr(0, 500) + "...";
            } catch (...) {}
        }

        // 内容
        std::string s_content;
        s_content += std::string("State: ") + (state ? state : "unknown");
        s_content += ", Author: " + std::string(author ? author : "unknown");
        s_content += ", Created: " + std::string(created ? created : "");
        if (closed && closed[0])
            s_content += ", Closed: " + std::string(closed);
        s_content += ", Comments: " + std::to_string(comments);
        if (!body.empty())
            s_content += "\n" + body;

        if (insert_chunk(db, repo_id, "issue", std::to_string(number),
                         s_title, s_content, author ? author : "", created ? created : ""))
            count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

// ============================================================
// 索引 Pull Requests
// ============================================================
static int index_pulls(Db& db, int repo_id)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT number, state, title, created_at, closed_at, merged_at, author_login, raw_json "
        "FROM pull_requests WHERE repo_id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, repo_id);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int  number  = sqlite3_column_int(stmt, 0);
        auto state   = (const char*)sqlite3_column_text(stmt, 1);
        auto title   = (const char*)sqlite3_column_text(stmt, 2);
        auto created = (const char*)sqlite3_column_text(stmt, 3);
        auto closed  = (const char*)sqlite3_column_text(stmt, 4);
        auto merged  = (const char*)sqlite3_column_text(stmt, 5);
        auto author  = (const char*)sqlite3_column_text(stmt, 6);
        auto raw     = (const char*)sqlite3_column_text(stmt, 7);

        std::string s_title = "PR #" + std::to_string(number) + ": " + (title ? title : "");

        std::string body;
        if (raw) {
            try {
                auto j = nlohmann::json::parse(raw);
                body = j.value("body", "");
                if (body.size() > 500) body = body.substr(0, 500) + "...";
            } catch (...) {}
        }

        std::string s_content;
        s_content += std::string("State: ") + (state ? state : "unknown");
        s_content += ", Author: " + std::string(author ? author : "unknown");
        s_content += ", Created: " + std::string(created ? created : "");
        if (merged && merged[0])
            s_content += ", Merged: " + std::string(merged);
        else if (closed && closed[0])
            s_content += ", Closed: " + std::string(closed);
        if (!body.empty())
            s_content += "\n" + body;

        if (insert_chunk(db, repo_id, "pull_request", std::to_string(number),
                         s_title, s_content, author ? author : "", created ? created : ""))
            count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

// ============================================================
// 索引 Commits（含关联的 commit_files 信息）
// ============================================================
static int index_commits(Db& db, int repo_id)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    // 子查询: 拼接该 commit 关联的文件列表
    const char* sql =
        "SELECT c.sha, c.author_login, c.committed_at, c.raw_json, "
        "  (SELECT GROUP_CONCAT(cf.filename || '(+' || cf.additions || '/-' || cf.deletions || ')', ', ') "
        "   FROM commit_files cf WHERE cf.sha = c.sha LIMIT 1) AS files_summary "
        "FROM commits c WHERE c.repo_id=?1;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, repo_id);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto sha          = (const char*)sqlite3_column_text(stmt, 0);
        auto author       = (const char*)sqlite3_column_text(stmt, 1);
        auto committed_at = (const char*)sqlite3_column_text(stmt, 2);
        auto raw          = (const char*)sqlite3_column_text(stmt, 3);
        auto files        = (const char*)sqlite3_column_text(stmt, 4);

        if (!sha) continue;
        std::string sha_short = std::string(sha).substr(0, 7);

        // 从 raw_json 提取 commit message
        std::string message;
        if (raw) {
            try {
                auto j = nlohmann::json::parse(raw);
                if (j.contains("commit") && j["commit"].is_object())
                    message = j["commit"].value("message", "");
                if (message.size() > 300) message = message.substr(0, 300) + "...";
            } catch (...) {}
        }

        std::string s_title = "Commit " + sha_short + " by " + (author ? author : "unknown");

        std::string s_content;
        s_content += "SHA: " + std::string(sha);
        s_content += ", Author: " + std::string(author ? author : "unknown");
        s_content += ", Date: " + std::string(committed_at ? committed_at : "");
        if (!message.empty())
            s_content += "\nMessage: " + message;
        if (files && files[0]) {
            std::string fs = files;
            if (fs.size() > 500) fs = fs.substr(0, 500) + "...";
            s_content += "\nFiles: " + fs;
        }

        if (insert_chunk(db, repo_id, "commit", sha_short,
                         s_title, s_content, author ? author : "", committed_at ? committed_at : ""))
            count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

// ============================================================
// 索引 Releases
// ============================================================
static int index_releases(Db& db, int repo_id)
{
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT tag_name, name, draft, prerelease, published_at, raw_json "
        "FROM releases WHERE repo_id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, repo_id);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto tag        = (const char*)sqlite3_column_text(stmt, 0);
        auto name       = (const char*)sqlite3_column_text(stmt, 1);
        int  draft      = sqlite3_column_int(stmt, 2);
        int  prerelease = sqlite3_column_int(stmt, 3);
        auto published  = (const char*)sqlite3_column_text(stmt, 4);
        auto raw        = (const char*)sqlite3_column_text(stmt, 5);

        std::string s_title = "Release " + std::string(tag ? tag : "");
        if (name && name[0]) s_title += ": " + std::string(name);

        std::string body;
        if (raw) {
            try {
                auto j = nlohmann::json::parse(raw);
                body = j.value("body", "");
                if (body.size() > 500) body = body.substr(0, 500) + "...";
            } catch (...) {}
        }

        std::string s_content;
        s_content += "Tag: " + std::string(tag ? tag : "");
        s_content += ", Published: " + std::string(published ? published : "unpublished");
        if (draft)      s_content += " [Draft]";
        if (prerelease) s_content += " [Pre-release]";
        if (!body.empty()) s_content += "\n" + body;

        if (insert_chunk(db, repo_id, "release", tag ? tag : "",
                         s_title, s_content, "", published ? published : ""))
            count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

// ============================================================
// 公共接口: 构建知识索引
// ============================================================
BuildIndexResult build_knowledge_index(Db& db, int repo_id)
{
    BuildIndexResult result;

    // 先清除该仓库的旧知识块
    clear_repo_chunks(db, repo_id);

    result.issues_indexed   = index_issues(db, repo_id);
    result.pulls_indexed    = index_pulls(db, repo_id);
    result.commits_indexed  = index_commits(db, repo_id);
    result.releases_indexed = index_releases(db, repo_id);

    std::cerr << "[knowledge] repo_id=" << repo_id
              << " 索引完成: issues=" << result.issues_indexed
              << ", pulls=" << result.pulls_indexed
              << ", commits=" << result.commits_indexed
              << ", releases=" << result.releases_indexed << "\n";

    // 为知识块生成向量嵌入（Embedding API 不可用时自动跳过）
    result.embeddings_generated = generate_embeddings_for_repo(db, repo_id);

    return result;
}

// ============================================================
// 内部: 兜底 — 无命中时返回最近知识块
// ============================================================
static std::vector<KnowledgeChunk> fallback_recent_chunks(Db& db, int repo_id, int top_k)
{
    std::vector<KnowledgeChunk> results;
    sqlite3* sdb = db.handle();
    std::string sql =
        "SELECT id, repo_id, source_type, source_id, title, content, author, event_time "
        "FROM knowledge_chunks ";
    if (repo_id > 0) {
        sql += "WHERE repo_id=?1 ";
        sql += "ORDER BY event_time DESC, id DESC LIMIT ?2;";
    } else {
        sql += "ORDER BY event_time DESC, id DESC LIMIT ?1;";
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (repo_id > 0) {
            sqlite3_bind_int(stmt, 1, repo_id);
            sqlite3_bind_int(stmt, 2, top_k);
        } else {
            sqlite3_bind_int(stmt, 1, top_k);
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            KnowledgeChunk c;
            c.id         = sqlite3_column_int(stmt, 0);
            c.repo_id    = sqlite3_column_int(stmt, 1);
            auto v2 = sqlite3_column_text(stmt, 2); c.source_type = v2 ? (const char*)v2 : "";
            auto v3 = sqlite3_column_text(stmt, 3); c.source_id   = v3 ? (const char*)v3 : "";
            auto v4 = sqlite3_column_text(stmt, 4); c.title       = v4 ? (const char*)v4 : "";
            auto v5 = sqlite3_column_text(stmt, 5); c.content     = v5 ? (const char*)v5 : "";
            auto v6 = sqlite3_column_text(stmt, 6); c.author      = v6 ? (const char*)v6 : "";
            auto v7 = sqlite3_column_text(stmt, 7); c.event_time  = v7 ? (const char*)v7 : "";
            c.score = 0.1;
            results.push_back(std::move(c));
        }
        sqlite3_finalize(stmt);
    }
    return results;
}

// ============================================================
// 内部: 关键词检索路径（原有逻辑）
// ============================================================
static std::vector<KnowledgeChunk> keyword_search(Db& db, int repo_id,
                                                  const std::vector<std::string>& keywords,
                                                  int top_k)
{
    std::vector<KnowledgeChunk> results;
    if (keywords.empty()) return results;

    std::string or_clauses;
    for (size_t i = 0; i < keywords.size(); i++) {
        if (i > 0) or_clauses += " OR ";
        or_clauses += "LOWER(title||' '||content||' '||author||' '||source_type||' '||source_id) LIKE ?"
                   + std::to_string(repo_id > 0 ? i + 2 : i + 1);
    }

    const int candidate_limit = std::min(2000, std::max(200, top_k * 50));

    std::string sql =
        "SELECT id, repo_id, source_type, source_id, title, content, author, event_time "
        "FROM knowledge_chunks ";
    if (repo_id > 0) {
        sql += "WHERE repo_id=?1 AND (" + or_clauses + ") ";
    } else {
        sql += "WHERE (" + or_clauses + ") ";
    }
    sql += "LIMIT " + std::to_string(candidate_limit) + ";";

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[knowledge] keyword search prepare failed: " << sqlite3_errmsg(sdb) << "\n";
        return results;
    }

    int bind_index = 1;
    if (repo_id > 0) {
        sqlite3_bind_int(stmt, bind_index++, repo_id);
    }
    for (size_t i = 0; i < keywords.size(); i++) {
        std::string pattern = "%" + keywords[i] + "%";
        sqlite3_bind_text(stmt, bind_index++, pattern.c_str(), -1, SQLITE_TRANSIENT);
    }

    struct Scored { KnowledgeChunk chunk; double score; };
    std::vector<Scored> scored;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        KnowledgeChunk c;
        c.id         = sqlite3_column_int(stmt, 0);
        c.repo_id    = sqlite3_column_int(stmt, 1);
        auto v2 = sqlite3_column_text(stmt, 2); c.source_type = v2 ? (const char*)v2 : "";
        auto v3 = sqlite3_column_text(stmt, 3); c.source_id   = v3 ? (const char*)v3 : "";
        auto v4 = sqlite3_column_text(stmt, 4); c.title       = v4 ? (const char*)v4 : "";
        auto v5 = sqlite3_column_text(stmt, 5); c.content     = v5 ? (const char*)v5 : "";
        auto v6 = sqlite3_column_text(stmt, 6); c.author      = v6 ? (const char*)v6 : "";
        auto v7 = sqlite3_column_text(stmt, 7); c.event_time  = v7 ? (const char*)v7 : "";

        double score = 0.0;
        std::string tl = c.title, cl = c.content, al = c.author, sl = c.source_type, il = c.source_id;
        std::transform(tl.begin(), tl.end(), tl.begin(), ::tolower);
        std::transform(cl.begin(), cl.end(), cl.begin(), ::tolower);
        std::transform(al.begin(), al.end(), al.begin(), ::tolower);
        std::transform(sl.begin(), sl.end(), sl.begin(), ::tolower);
        std::transform(il.begin(), il.end(), il.begin(), ::tolower);

        for (const auto& kw : keywords) {
            if (tl.find(kw) != std::string::npos) score += 2.0;
            if (cl.find(kw) != std::string::npos) score += 1.0;
            if (al.find(kw) != std::string::npos) score += 2.5;
            if (sl.find(kw) != std::string::npos) score += 1.2;
            if (il.find(kw) != std::string::npos) score += 1.6;
            if (!al.empty() && al == kw) score += 3.0;
        }
        c.score = score;
        scored.push_back({c, score});
    }
    sqlite3_finalize(stmt);

    std::sort(scored.begin(), scored.end(),
              [](const Scored& a, const Scored& b) { return a.score > b.score; });

    int n = std::min(top_k, static_cast<int>(scored.size()));
    for (int i = 0; i < n; i++)
        results.push_back(std::move(scored[i].chunk));

    return results;
}

// ============================================================
// 内部: 向量检索路径
// 从 DB 加载所有有 embedding 的 chunk，计算 cosine similarity 排序
// ============================================================
static std::vector<KnowledgeChunk> vector_search(Db& db, int repo_id,
                                                 const std::vector<float>& query_embedding,
                                                 int top_k)
{
    std::vector<KnowledgeChunk> results;
    if (query_embedding.empty()) return results;

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    std::string sql =
        "SELECT id, repo_id, source_type, source_id, title, content, author, event_time, embedding "
        "FROM knowledge_chunks WHERE embedding IS NOT NULL AND LENGTH(embedding) > 0 ";
    if (repo_id > 0) {
        sql += "AND repo_id=?1";
    }
    sql += ";";

    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[knowledge] vector search prepare failed: " << sqlite3_errmsg(sdb) << "\n";
        return results;
    }
    if (repo_id > 0) {
        sqlite3_bind_int(stmt, 1, repo_id);
    }

    struct Scored { KnowledgeChunk chunk; double score; };
    std::vector<Scored> scored;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        KnowledgeChunk c;
        c.id         = sqlite3_column_int(stmt, 0);
        c.repo_id    = sqlite3_column_int(stmt, 1);
        auto v2 = sqlite3_column_text(stmt, 2); c.source_type = v2 ? (const char*)v2 : "";
        auto v3 = sqlite3_column_text(stmt, 3); c.source_id   = v3 ? (const char*)v3 : "";
        auto v4 = sqlite3_column_text(stmt, 4); c.title       = v4 ? (const char*)v4 : "";
        auto v5 = sqlite3_column_text(stmt, 5); c.content     = v5 ? (const char*)v5 : "";
        auto v6 = sqlite3_column_text(stmt, 6); c.author      = v6 ? (const char*)v6 : "";
        auto v7 = sqlite3_column_text(stmt, 7); c.event_time  = v7 ? (const char*)v7 : "";

        const void* blob = sqlite3_column_blob(stmt, 8);
        int blob_bytes = sqlite3_column_bytes(stmt, 8);
        auto emb = deserialize_embedding(blob, blob_bytes);

        double sim = cosine_similarity(query_embedding, emb);
        c.score = sim;
        scored.push_back({std::move(c), sim});
    }
    sqlite3_finalize(stmt);

    std::sort(scored.begin(), scored.end(),
              [](const Scored& a, const Scored& b) { return a.score > b.score; });

    int n = std::min(top_k, static_cast<int>(scored.size()));
    for (int i = 0; i < n; i++)
        results.push_back(std::move(scored[i].chunk));

    return results;
}

// ============================================================
// 公共接口: 混合检索（关键词 + 向量）
// 策略: 两路各取 Top-K → 归一化 → 加权合并 → 去重 → 最终 Top-K
// 向量检索不可用时自动退回纯关键词检索
// ============================================================
std::vector<KnowledgeChunk> search_knowledge(Db& db, int repo_id,
                                             const std::string& query, int top_k)
{
    std::vector<KnowledgeChunk> results;
    auto keywords = extract_keywords(query);

    // ---- 关键词检索路径 ----
    auto kw_results = keyword_search(db, repo_id, keywords, top_k * 2);

    // ---- 向量检索路径（Embedding API 不可用时自动跳过）----
    std::vector<float> query_embedding = call_embedding_api(query);
    auto vec_results = vector_search(db, repo_id, query_embedding, top_k * 2);

    // 如果向量检索无结果，直接返回关键词检索结果
    if (vec_results.empty()) {
        // 截取 top_k
        if (static_cast<int>(kw_results.size()) > top_k)
            kw_results.resize(top_k);

        // 关键词也没有命中时走兜底逻辑
        if (kw_results.empty()) {
            return fallback_recent_chunks(db, repo_id, top_k);
        }
        return kw_results;
    }

    // ---- 混合合并 ----
    // 归一化关键词分数到 [0, 1]
    double max_kw_score = 0.0;
    for (const auto& c : kw_results) {
        if (c.score > max_kw_score) max_kw_score = c.score;
    }

    // 用 chunk id 做去重，合并两路得分
    struct MergedScore {
        KnowledgeChunk chunk;
        double kw_norm = 0.0;   // 关键词归一化分数
        double vec_sim = 0.0;   // 向量相似度
    };
    std::unordered_map<int, MergedScore> merged;

    for (auto& c : kw_results) {
        double norm = (max_kw_score > 0.0) ? (c.score / max_kw_score) : 0.0;
        merged[c.id] = {c, norm, 0.0};
    }
    for (auto& c : vec_results) {
        auto it = merged.find(c.id);
        if (it != merged.end()) {
            it->second.vec_sim = c.score;
        } else {
            merged[c.id] = {c, 0.0, c.score};
        }
    }

    // 加权混合: 0.4 * 关键词 + 0.6 * 向量
    struct FinalScored { KnowledgeChunk chunk; double score; };
    std::vector<FinalScored> final_scored;
    for (auto& [id, ms] : merged) {
        double hybrid = 0.4 * ms.kw_norm + 0.6 * ms.vec_sim;
        ms.chunk.score = hybrid;
        final_scored.push_back({std::move(ms.chunk), hybrid});
    }

    std::sort(final_scored.begin(), final_scored.end(),
              [](const FinalScored& a, const FinalScored& b) { return a.score > b.score; });

    int n = std::min(top_k, static_cast<int>(final_scored.size()));
    for (int i = 0; i < n; i++)
        results.push_back(std::move(final_scored[i].chunk));

    // 兜底
    if (results.empty()) {
        return fallback_recent_chunks(db, repo_id, top_k);
    }

    return results;
}

// ============================================================
// JSON 序列化
// ============================================================
std::string build_result_to_json(const BuildIndexResult& r)
{
    return std::string("{\"issues_indexed\":") + std::to_string(r.issues_indexed)
        + ",\"pulls_indexed\":" + std::to_string(r.pulls_indexed)
        + ",\"commits_indexed\":" + std::to_string(r.commits_indexed)
        + ",\"releases_indexed\":" + std::to_string(r.releases_indexed)
        + ",\"embeddings_generated\":" + std::to_string(r.embeddings_generated)
        + ",\"total\":" + std::to_string(r.total())
        + "}";
}

std::string knowledge_chunks_to_json(const std::vector<KnowledgeChunk>& chunks)
{
    std::string out = "[";
    for (size_t i = 0; i < chunks.size(); i++) {
        if (i > 0) out += ",";
        const auto& c = chunks[i];
        out += "{\"id\":" + std::to_string(c.id)
            + ",\"source_type\":\"" + util::json_escape(c.source_type) + "\""
            + ",\"source_id\":\"" + util::json_escape(c.source_id) + "\""
            + ",\"title\":\"" + util::json_escape(c.title) + "\""
            + ",\"content\":\"" + util::json_escape(c.content) + "\""
            + ",\"author\":\"" + util::json_escape(c.author) + "\""
            + ",\"event_time\":\"" + util::json_escape(c.event_time) + "\""
            + ",\"score\":" + std::to_string(c.score)
            + "}";
    }
    out += "]";
    return out;
}
