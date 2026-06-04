// 2.4 AI 智能分析路由
// 提供知识库构建/检索、AI 问答、对话历史查询接口
#include "routes.h"
#include "common/util.h"
#include "common/system_log.h"
#include "ai/knowledge_base.h"
#include "ai/ai_assistant.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <chrono>

static constexpr const char* kJsonUtf8 = "application/json; charset=utf-8";

static int get_int_param_ai(const httplib::Request& req, const std::string& key, int defv) {
    if (!req.has_param(key)) return defv;
    try { return std::stoi(req.get_param_value(key)); } catch (...) { return defv; }
}

static std::string get_repo_full_name_ai(Db& db, int repo_id)
{
    if (repo_id <= 0) return "";
    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT full_name FROM repos WHERE id=?1;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_int(stmt, 1, repo_id);
    std::string out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* v = sqlite3_column_text(stmt, 0);
        out = v ? reinterpret_cast<const char*>(v) : "";
    }
    sqlite3_finalize(stmt);
    return out;
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
    res.status = 200;
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
    res.status = 200;
    res.set_content("{\"items\":" + knowledge_chunks_to_json(chunks) + "}", kJsonUtf8);
}

// ============================================================
// POST /api/ai/ask
// AI 问答（RAG 流程 + 多轮对话）
// 请求体: {"repo_id": 1, "question": "...", "thread_id": 0}
// thread_id > 0 时会加载该线程历史拼入 LLM 上下文
// ============================================================
static void post_ai_ask_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    const auto begin = std::chrono::steady_clock::now();
    int repo_id = 0;
    int thread_id = 0;
    std::string question;

    // 优先从 JSON body 解析
    if (!req.body.empty()) {
        try {
            auto body = nlohmann::json::parse(req.body);
            repo_id  = body.value("repo_id", 0);
            thread_id = body.value("thread_id", 0);
            question = body.value("question", "");
        } catch (...) {
            // JSON 解析失败，回退到 query 参数
        }
    }

    // 回退: 从 query 参数获取
    if (repo_id <= 0)
        repo_id = get_int_param_ai(req, "repo_id", 0);
    if (thread_id <= 0)
        thread_id = get_int_param_ai(req, "thread_id", 0);
    if (question.empty() && req.has_param("question"))
        question = req.get_param_value("question");

    if (question.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"missing question"})", kJsonUtf8);
        const auto end = std::chrono::steady_clock::now();
        const int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());
        system_log::write_operation(db, "ai.ask", "/api/ai/ask", "error", duration_ms, req.remote_addr,
                                    R"({"reason":"missing question"})");
        return;
    }

    auto answer = ask_question(db, repo_id, question, thread_id);
    res.status = 200;
    res.set_content(ai_answer_to_json(answer), kJsonUtf8);

    const auto end = std::chrono::steady_clock::now();
    const int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());
    const std::string status = answer.success ? "ok" : "error";
    const std::string repo_full_name = get_repo_full_name_ai(db, repo_id);

    system_log::write_operation(
        db,
        "ai.ask",
        "/api/ai/ask",
        status,
        duration_ms,
        req.remote_addr,
        std::string("{\"repo_id\":") + std::to_string(repo_id) +
            ",\"thread_id\":" + std::to_string(thread_id) +
            ",\"model\":\"" + util::json_escape(answer.model) + "\"}"
    );

    system_log::write_ai_usage(
        db,
        repo_id,
        repo_full_name,
        answer.model,
        answer.prompt_tokens,
        answer.completion_tokens,
        answer.total_tokens,
        answer.cost_usd,
        answer.duration_ms > 0 ? answer.duration_ms : duration_ms,
        req.remote_addr,
        status,
        answer.error
    );
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
        out += ",\"question\":\"" + util::json_escape(q ? reinterpret_cast<const char*>(q) : "") + "\"";
        out += ",\"answer\":\"" + util::json_escape(a ? reinterpret_cast<const char*>(a) : "") + "\"";
        out += ",\"model\":\"" + util::json_escape(m ? reinterpret_cast<const char*>(m) : "") + "\"";
        out += ",\"created_at\":\"" + util::json_escape(t ? reinterpret_cast<const char*>(t) : "") + "\"";
        out += "}";
    }
    out += "]}";
    sqlite3_finalize(stmt);

    res.status = 200;
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
    out += ",\"question\":\"" + util::json_escape(q ? reinterpret_cast<const char*>(q) : "") + "\"";
    out += ",\"answer\":\"" + util::json_escape(a ? reinterpret_cast<const char*>(a) : "") + "\"";
    out += ",\"evidence_json\":" + std::string(ej ? reinterpret_cast<const char*>(ej) : "[]");
    out += ",\"model\":\"" + util::json_escape(m ? reinterpret_cast<const char*>(m) : "") + "\"";
    out += ",\"created_at\":\"" + util::json_escape(t ? reinterpret_cast<const char*>(t) : "") + "\"";
    out += "}";
    sqlite3_finalize(stmt);

    res.status = 200;
    res.set_content(out, kJsonUtf8);
}


// ============================================================
// 2.6 多轮对话: POST /api/ai/threads — 创建新对话线程
// ============================================================
static void post_thread_create_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int repo_id = 0;
    std::string title;
    if (!req.body.empty()) {
        try {
            auto body = nlohmann::json::parse(req.body);
            repo_id = body.value("repo_id", 0);
            title = body.value("title", "");
        } catch (...) {}
    }
    if (repo_id <= 0)
        repo_id = get_int_param_ai(req, "repo_id", 0);

    int tid = create_conversation_thread(db, repo_id, title);
    res.status = 200;
    res.set_content(
        "{\"ok\":true,\"thread_id\":" + std::to_string(tid)
        + ",\"repo_id\":" + std::to_string(repo_id) + "}",
        kJsonUtf8);
}

// ============================================================
// 2.6 多轮对话: GET /api/ai/threads?repo_id=X — 列出线程
// ============================================================
static void get_threads_list_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int repo_id = get_int_param_ai(req, "repo_id", 0);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    std::string sql;
    if (repo_id > 0) {
        sql =
            "SELECT DISTINCT thread_id, repo_id, MIN(created_at) as started_at, COUNT(*) as msg_count "
            "FROM ai_conversations WHERE thread_id IS NOT NULL AND repo_id=?1 "
            "GROUP BY thread_id ORDER BY started_at DESC LIMIT 50;";
    } else {
        sql =
            "SELECT DISTINCT thread_id, repo_id, MIN(created_at) as started_at, COUNT(*) as msg_count "
            "FROM ai_conversations WHERE thread_id IS NOT NULL "
            "GROUP BY thread_id ORDER BY started_at DESC LIMIT 50;";
    }

    if (sqlite3_prepare_v2(sdb, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db error"})", kJsonUtf8);
        return;
    }
    if (repo_id > 0) sqlite3_bind_int(stmt, 1, repo_id);

    std::string out = R"({"items":[)";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) out += ",";
        first = false;
        int tid = sqlite3_column_int(stmt, 0);
        int rid = sqlite3_column_int(stmt, 1);
        auto started = sqlite3_column_text(stmt, 2);
        int msg_count = sqlite3_column_int(stmt, 3);

        std::string question = "";
        // get the first question as title
        sqlite3_stmt* stmt2 = nullptr;
        const char* sq2 = "SELECT question FROM ai_conversations WHERE thread_id=?1 ORDER BY id ASC LIMIT 1;";
        if (sqlite3_prepare_v2(sdb, sq2, -1, &stmt2, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt2, 1, tid);
            if (sqlite3_step(stmt2) == SQLITE_ROW) {
                auto q = sqlite3_column_text(stmt2, 0);
                question = q ? reinterpret_cast<const char*>(q) : "";
            }
            sqlite3_finalize(stmt2);
        }

        out += "{\"thread_id\":" + std::to_string(tid);
        out += ",\"repo_id\":" + std::to_string(rid);
        out += ",\"title\":\"" + util::json_escape(question) + "\"";
        out += ",\"msg_count\":" + std::to_string(msg_count);
        out += ",\"started_at\":\"" + util::json_escape(started ? reinterpret_cast<const char*>(started) : "") + "\"";
        out += "}";
    }
    out += "]}";
    sqlite3_finalize(stmt);
    res.status = 200;
    res.set_content(out, kJsonUtf8);
}

// ============================================================
// 2.6 多轮对话: GET /api/ai/threads/{id}/messages — 获取线程消息
// ============================================================
static void get_thread_messages_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int thread_id = std::stoi(req.matches[1]);

    sqlite3* sdb = db.handle();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, repo_id, question, answer, evidence_json, model, created_at "
        "FROM ai_conversations WHERE thread_id=?1 ORDER BY id ASC;";
    if (sqlite3_prepare_v2(sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        res.status = 500;
        res.set_content(R"({"error":"db error"})", kJsonUtf8);
        return;
    }
    sqlite3_bind_int(stmt, 1, thread_id);

    std::string out = R"({"thread_id":)" + std::to_string(thread_id) + R"(,"messages":[)";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) out += ",";
        first = false;
        int id = sqlite3_column_int(stmt, 0);
        int rid = sqlite3_column_int(stmt, 1);
        auto q = sqlite3_column_text(stmt, 2);
        auto a = sqlite3_column_text(stmt, 3);
        auto ej = sqlite3_column_text(stmt, 4);
        auto m = sqlite3_column_text(stmt, 5);
        auto t = sqlite3_column_text(stmt, 6);

        out += "{\"id\":" + std::to_string(id);
        out += ",\"repo_id\":" + std::to_string(rid);
        out += ",\"question\":\"" + util::json_escape(q ? reinterpret_cast<const char*>(q) : "") + "\"";
        out += ",\"answer\":\"" + util::json_escape(a ? reinterpret_cast<const char*>(a) : "") + "\"";
        out += ",\"evidence_json\":" + std::string(ej ? reinterpret_cast<const char*>(ej) : "[]");
        out += ",\"model\":\"" + util::json_escape(m ? reinterpret_cast<const char*>(m) : "") + "\"";
        out += ",\"created_at\":\"" + util::json_escape(t ? reinterpret_cast<const char*>(t) : "") + "\"";
        out += "}";
    }
    out += "]}";
    sqlite3_finalize(stmt);
    res.status = 200;
    res.set_content(out, kJsonUtf8);
}

// ============================================================
// 2.6 多轮对话: DELETE /api/ai/threads/{id} — 删除线程
// ============================================================
static void delete_thread_handler(Db& db, const httplib::Request& req, httplib::Response& res)
{
    int thread_id = std::stoi(req.matches[1]);
    bool ok = delete_conversation_thread(db, thread_id);
    res.status = ok ? 200 : 404;
    res.set_content(
        ok ? "{\"ok\":true}" : "{\"error\":\"thread not found\"}",
        kJsonUtf8);
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

    // 2.6 多轮对话: 线程管理
    app.Post("/api/ai/threads",
             [&db](const httplib::Request& req, httplib::Response& res) {
                 try { post_thread_create_handler(db, req, res); }
                 catch (const std::exception& e) {
                     res.status = 500;
                     res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJsonUtf8);
                 }
             });

    app.Get("/api/ai/threads",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_threads_list_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJsonUtf8);
                }
            });

    app.Get(R"(/api/ai/threads/(\d+)/messages)",
            [&db](const httplib::Request& req, httplib::Response& res) {
                try { get_thread_messages_handler(db, req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("{\"error\":\"") + util::json_escape(e.what()) + "\"}", kJsonUtf8);
                }
            });

    app.Delete(R"(/api/ai/threads/(\d+))",
               [&db](const httplib::Request& req, httplib::Response& res) {
                   try { delete_thread_handler(db, req, res); }
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
