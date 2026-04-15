// 2.4 AI 智能分析路由
// 提供知识库构建/检索、AI 问答、对话历史查询接口
#include "routes.h"
#include "common/util.h"
#include "ai/knowledge_base.h"
#include "ai/ai_assistant.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

static constexpr const char* kJsonUtf8 = "application/json; charset=utf-8";

static int get_int_param_ai(const httplib::Request& req, const std::string& key, int defv) {
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
}

// ============================================================
// POST /api/repos/{id}/knowledge/build
// 为指定仓库构建（重建）知识索引
// ============================================================
static void post_knowledge_build_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);

    // 检查仓库是否存在
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id FROM repos WHERE id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"数据库错误"})", kJsonUtf8);
        return;
    }
    sqlite3_bind_int(stmt, 1, rid);
    bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);

    if (!exists) {
        res.status = 404;
        res.set_content(R"({"error":"仓库不存在"})", kJsonUtf8);
        return;
    }

    auto result = build_knowledge_index(db, rid);
    res.set_content(
        "{\"ok\":true,\"repo_id\":" + std::to_string(rid)
        + ",\"result\":" + build_result_to_json(result) + "}",
        kJsonUtf8);
}

// ============================================================
// GET /api/repos/{id}/knowledge/search?q=...&top=10
// 在知识库中检索相关内容
// ============================================================
static void get_knowledge_search_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int rid = std::stoi(req.matches[1]);
    std::string query = req.has_param("q") ? req.get_param_value("q") : "";
    int top = std::max(1, std::min(50, get_int_param_ai(req, "top", 10)));

    if (query.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"缺少查询参数 'q'"})", kJsonUtf8);
        return;
    }

    auto chunks = search_knowledge(db, rid, query, top);
    res.set_content("{\"items\":" + knowledge_chunks_to_json(chunks) + "}", kJsonUtf8);
}

// ============================================================
// POST /api/ai/ask
// AI 问答（RAG 流程）
// 请求体: {"repo_id": 1, "question": "..."}
// 也支持 query 参数: ?repo_id=1&question=...
// ============================================================
static void post_ai_ask_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int repo_id = 0;
    std::string question;

    // 优先从 JSON body 解析
    if (!req.body.empty()) {
        try {
            auto body = nlohmann::json::parse(req.body);
            repo_id  = body.value("repo_id", 0);
            question = body.value("question", "");
        } catch (...) {
            // JSON 解析失败，回退到 query 参数
        }
    }

    // 回退: 从 query 参数获取
    if (repo_id <= 0)
        repo_id = get_int_param_ai(req, "repo_id", 0);
    if (question.empty() && req.has_param("question"))
        question = req.get_param_value("question");

    if (question.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"缺少 question"})", kJsonUtf8);
        return;
    }

    auto answer = ask_question(db, repo_id, question);
    res.set_content(ai_answer_to_json(answer), kJsonUtf8);
}

// ============================================================
// GET /api/ai/conversations?repo_id=...&limit=20
// 查询 AI 对话历史
// ============================================================
static void get_ai_conversations_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int repo_id = get_int_param_ai(req, "repo_id", 0);
    int limit = std::max(1, std::min(100, get_int_param_ai(req, "limit", 20)));

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    // repo_id > 0 时按仓库过滤，否则返回全部
    std::string sql;
    if (repo_id > 0) {
        sql = "SELECT id, repo_id, question, answer, model, created_at "
              "FROM ai_conversations WHERE repo_id=?1 ORDER BY id DESC LIMIT ?2;";
    } else {
        sql = "SELECT id, repo_id, question, answer, model, created_at "
              "FROM ai_conversations ORDER BY id DESC LIMIT ?1;";
    }

    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"数据库错误"})", kJsonUtf8);
        return;
    }

    if (repo_id > 0) {
        sqlite3_bind_int(stmt, 1, repo_id);
        sqlite3_bind_int(stmt, 2, limit);
    } else {
        sqlite3_bind_int(stmt, 1, limit);
    }

    std::string out = R"({"items":[)";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) out += ",";
        first = false;

        int id   = sqlite3_column_int(stmt, 0);
        int rid  = sqlite3_column_int(stmt, 1);
        auto q   = sqlite3_column_text(stmt, 2);
        auto a   = sqlite3_column_text(stmt, 3);
        auto m   = sqlite3_column_text(stmt, 4);
        auto t   = sqlite3_column_text(stmt, 5);

        out += "{\"id\":" + std::to_string(id);
        out += ",\"repo_id\":" + std::to_string(rid);
        out += ",\"question\":\"" + util::json_escape(q ? (const char*)q : "") + "\"";
        out += ",\"answer\":\"" + util::json_escape(a ? (const char*)a : "") + "\"";
        out += ",\"model\":\"" + util::json_escape(m ? (const char*)m : "") + "\"";
        out += ",\"created_at\":\"" + util::json_escape(t ? (const char*)t : "") + "\"";
        out += "}";
    }
    out += "]}";
    sqlite3_finalize(stmt);

    res.set_content(out, kJsonUtf8);
}


// ============================================================
// GET /api/ai/conversations/{id}
// 查询单条 AI 对话详情
// ============================================================
static void get_ai_conversation_detail_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int id = std::stoi(req.matches[1]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "SELECT id, repo_id, question, answer, evidence_json, model, created_at "
        "FROM ai_conversations WHERE id=?1;";

    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"数据库错误"})", kJsonUtf8);
        return;
    }
    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        res.status = 404;
        res.set_content(R"({"error":"对话不存在"})", kJsonUtf8);
        return;
    }

    int rid = sqlite3_column_int(stmt, 1);
    auto q  = sqlite3_column_text(stmt, 2);
    auto a  = sqlite3_column_text(stmt, 3);
    auto ej = sqlite3_column_text(stmt, 4);
    auto m  = sqlite3_column_text(stmt, 5);
    auto t  = sqlite3_column_text(stmt, 6);

    std::string out = "{";
    out += "\"id\":" + std::to_string(id);
    out += ",\"repo_id\":" + std::to_string(rid);
    out += ",\"question\":\"" + util::json_escape(q ? (const char*)q : "") + "\"";
    out += ",\"answer\":\"" + util::json_escape(a ? (const char*)a : "") + "\"";
    out += ",\"evidence_json\":" + std::string(ej ? (const char*)ej : "[]");
    out += ",\"model\":\"" + util::json_escape(m ? (const char*)m : "") + "\"";
    out += ",\"created_at\":\"" + util::json_escape(t ? (const char*)t : "") + "\"";
    out += "}";
    sqlite3_finalize(stmt);

    res.set_content(out, kJsonUtf8);
}


// ============================================================
// 注册所有 AI 相关路由
// ============================================================
void register_ai_routes(httplib::Server& app, Db& db)
{
    // 知识库: 构建索引
    app.Post(R"(/api/repos/(\d+)/knowledge/build)",
             [&db](const httplib::Request& req, httplib::Response& res) {
                 try { post_knowledge_build_handler(db, req, res); }
                 catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJsonUtf8);
                 }
             });

    // 知识库: 检索
    app.Get(R"(/api/repos/(\d+)/knowledge/search)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_knowledge_search_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJsonUtf8);
                }
            });

    // 对话详情(新增,添加了证据应用的返回)
    app.Get(R"(/api/ai/conversations/(\d+))",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_ai_conversation_detail_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJsonUtf8);
                }
            });

    // AI 问答
    app.Post("/api/ai/ask",
             [&db](const httplib::Request& req, httplib::Response& res) {
                 try { post_ai_ask_handler(db, req, res); }
                 catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJsonUtf8);
                 }
             });

    // 对话历史
    app.Get("/api/ai/conversations",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_ai_conversations_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJsonUtf8);
                }
            });
}
