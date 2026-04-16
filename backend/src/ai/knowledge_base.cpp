// 2.4(1) 工程知识库 - 实现
#include "knowledge_base.h"
#include "db/db.h"
#include "common/util.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>

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

        // 中文短语直接作为候选词，同时补充双字切片提升“贡献者/告警”等问法召回率。
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

    return result;
}

// ============================================================
// 公共接口: 关键词检索
// 策略: 提取关键词 → 用 LIKE 匹配 → 在 C++ 中按命中次数评分排序
// ============================================================
std::vector<KnowledgeChunk> search_knowledge(Db& db, int repo_id,
                                             const std::string& query, int top_k)
{
    std::vector<KnowledgeChunk> results;
    auto keywords = extract_keywords(query);
    if (keywords.empty()) return results;

    // 构造 SQL: 指定 repo 时按仓库过滤；否则搜索全部仓库。
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
        std::cerr << "[knowledge] search prepare failed: " << sqlite3_errmsg(sdb) << "\n";
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

    // 读取候选结果，在 C++ 侧评分
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

        // 评分: 标题/作者/source 命中优先，提升对“贡献者/规则类型/编号”类问题的召回排序。
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

    // 按得分降序排序，取 Top-K
    std::sort(scored.begin(), scored.end(),
              [](const Scored& a, const Scored& b) { return a.score > b.score; });

    int n = std::min(top_k, static_cast<int>(scored.size()));
    for (int i = 0; i < n; i++)
        results.push_back(std::move(scored[i].chunk));

    // 无命中时兜底：返回最近知识块，避免 AI 直接“无证据可答”。
    if (results.empty()) {
        std::string fallback_sql =
            "SELECT id, repo_id, source_type, source_id, title, content, author, event_time "
            "FROM knowledge_chunks ";
        if (repo_id > 0) {
            fallback_sql += "WHERE repo_id=?1 ";
            fallback_sql += "ORDER BY event_time DESC, id DESC LIMIT ?2;";
        } else {
            fallback_sql += "ORDER BY event_time DESC, id DESC LIMIT ?1;";
        }

        sqlite3_stmt* fallback_stmt = nullptr;
        if (sqlite3_prepare_v2(sdb, fallback_sql.c_str(), -1, &fallback_stmt, nullptr) == SQLITE_OK) {
            if (repo_id > 0) {
                sqlite3_bind_int(fallback_stmt, 1, repo_id);
                sqlite3_bind_int(fallback_stmt, 2, top_k);
            } else {
                sqlite3_bind_int(fallback_stmt, 1, top_k);
            }

            while (sqlite3_step(fallback_stmt) == SQLITE_ROW) {
                KnowledgeChunk c;
                c.id         = sqlite3_column_int(fallback_stmt, 0);
                c.repo_id    = sqlite3_column_int(fallback_stmt, 1);
                auto v2 = sqlite3_column_text(fallback_stmt, 2); c.source_type = v2 ? (const char*)v2 : "";
                auto v3 = sqlite3_column_text(fallback_stmt, 3); c.source_id   = v3 ? (const char*)v3 : "";
                auto v4 = sqlite3_column_text(fallback_stmt, 4); c.title       = v4 ? (const char*)v4 : "";
                auto v5 = sqlite3_column_text(fallback_stmt, 5); c.content     = v5 ? (const char*)v5 : "";
                auto v6 = sqlite3_column_text(fallback_stmt, 6); c.author      = v6 ? (const char*)v6 : "";
                auto v7 = sqlite3_column_text(fallback_stmt, 7); c.event_time  = v7 ? (const char*)v7 : "";
                c.score = 0.1;
                results.push_back(std::move(c));
            }
            sqlite3_finalize(fallback_stmt);
        }
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
