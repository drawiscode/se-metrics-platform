#include "db.h"
#include <iostream>

#include <stdexcept>
#include <utility>

static bool has_column(sqlite3* db, const char* table, const char* col)
{
    std::string sql = std::string("PRAGMA table_info(") + table + ");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;

    bool ok = false;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char* name = sqlite3_column_text(stmt, 1); // column name
        if (name && std::string((const char*)name) == col) { ok = true; break; }
    }
    sqlite3_finalize(stmt);
    return ok;
}


Db::Db(const std::string& path) : path_(path)
{
    if (sqlite3_open(path_.c_str(), &db_) != SQLITE_OK)
    {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "sqlite3_open failed";
        close();
        throw std::runtime_error("open sqlite failed: " + msg);
    }
}

Db::~Db()
{
    close();
}

Db::Db(Db&& other) noexcept : db_(other.db_), path_(std::move(other.path_))
{
    other.db_ = nullptr;
}

Db& Db::operator=(Db&& other) noexcept
{
    if (this != &other)
    {
        close();
        db_ = other.db_;
        path_ = std::move(other.path_);
        other.db_ = nullptr;
    }
    return *this;
}

void Db::close() noexcept
{
    if (db_)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Db::exec(const std::string& sql)
{
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK)
    {
        std::string msg = err ? err : "unknown sqlite error";
        if (err) sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

void Db::init_schema()
{
    
    const char* schema = R"SQL(
    PRAGMA foreign_keys = ON;

    CREATE TABLE IF NOT EXISTS repos (
        id INTEGER PRIMARY KEY AUTOINCREMENT,--仓库标识，自增主键
        full_name TEXT NOT NULL UNIQUE,--仓库完整名称，如 "owner/repo"
        enabled INTEGER NOT NULL DEFAULT 1,--是否启用同步，1=启用（默认），0=禁用
        created_at TEXT NOT NULL DEFAULT (datetime('now'))--仓库创建时间，这里是插入记录的时间
    );

    -- Sync runs: record each attempt
    CREATE TABLE IF NOT EXISTS repo_sync_runs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,--同步运行标识，自增主键
        repo_id INTEGER NOT NULL,--外键，表示哪个仓库的同步运行
        started_at TEXT NOT NULL DEFAULT (datetime('now')),--同步开始时间，这里是插入记录的时间
        finished_at TEXT,--同步结束时间，初始为NULL，表示尚未完成
        status TEXT NOT NULL, -- "ok" | "error"
        error TEXT,--如果status是"error"，这里可以记录错误信息；如果status是"ok"，这里应该为NULL
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE---仓库删除时自动删除相关同步运行记录
    );

    -- 增量同步游标（每个 repo 一行）
    CREATE TABLE IF NOT EXISTS repo_sync_state (
        repo_id INTEGER PRIMARY KEY,
        issues_updated_cursor TEXT,    -- last issue updated_at (ISO8601)
        pulls_updated_cursor TEXT,     -- last PR updated_at
        commits_since_cursor TEXT,     -- last commit ISO time (或 last seen sha)
        releases_cursor TEXT,          -- last release published_at
        updated_at TEXT NOT NULL DEFAULT (datetime('now')),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );

    -- Repo snapshots: append-only time series
    CREATE TABLE IF NOT EXISTS repo_snapshots (
        id INTEGER PRIMARY KEY AUTOINCREMENT,--快照标识，自增主键
        repo_id INTEGER NOT NULL,--外键，表示哪个仓库的快照
        ts TEXT NOT NULL DEFAULT (datetime('now')),--快照时间戳，这里是插入记录的时间
        full_name TEXT NOT NULL,--冗余存储仓库完整名称，方便查询和历史记录，即使仓库后来被重命名或删除也不受影响
        stars INTEGER NOT NULL,--仓库的星标数量
        forks INTEGER NOT NULL,--仓库的分叉数量
        open_issues INTEGER NOT NULL,--仓库的开放问题数量
        watchers INTEGER NOT NULL,--仓库的观察者数量
        pushed_at TEXT,--仓库的最后推送时间，来自GitHub API
        raw_json TEXT,--仓库的原始JSON数据，方便后续扩展和调试
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE---仓库删除时自动删除相关快照记录
    );
    CREATE INDEX IF NOT EXISTS idx_repo_snapshots_repo_id_ts ON repo_snapshots(repo_id, ts);

    CREATE TABLE IF NOT EXISTS issues 
    (
        id INTEGER PRIMARY KEY AUTOINCREMENT,--issue标识，自增主键
        repo_id INTEGER NOT NULL,--外键，表示哪个仓库的issue
        number INTEGER NOT NULL,--issue编号，GitHub仓库内唯一
        state TEXT NOT NULL,           -- open/closed
        title TEXT NOT NULL,--issue标题
        created_at TEXT NOT NULL,--issue创建时间，来自GitHub API
        updated_at TEXT NOT NULL,--issue更新时间，来自GitHub API
        closed_at TEXT,--issue关闭时间，来自GitHub API，open状态为NULL
        comments INTEGER NOT NULL DEFAULT 0,--issue的评论数量
        author_login TEXT,--issue作者的GitHub登录名
        is_pull_request INTEGER NOT NULL DEFAULT 0,--是否为PR，GitHub的issue可以同时是PR，这里用一个字段区分
        raw_json TEXT NOT NULL,--issue的原始JSON数据，方便后续扩展和调试
        UNIQUE(repo_id, number),--确保同一仓库内issue编号唯一
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE---仓库删除时自动删除相关issue记录
    );
    CREATE INDEX IF NOT EXISTS idx_issues_repo_state ON issues(repo_id, state);

    CREATE TABLE IF NOT EXISTS pull_requests 
    (
        id INTEGER PRIMARY KEY AUTOINCREMENT,--PR标识，自增主键
        repo_id INTEGER NOT NULL,--外键，表示哪个仓库的PR
        number INTEGER NOT NULL,--PR编号，GitHub仓库内唯一
        state TEXT NOT NULL,--PR状态，open/closed
        title TEXT NOT NULL,--PR标题
        created_at TEXT NOT NULL,--PR创建时间，来自GitHub API
        updated_at TEXT NOT NULL,--PR更新时间，来自GitHub API
        closed_at TEXT,--PR关闭时间，来自GitHub API，open状态为NULL
        merged_at TEXT,--PR合并时间，来自GitHub API，未合并为NULL
        author_login TEXT,--PR作者的GitHub登录名
        raw_json TEXT NOT NULL,--PR的原始JSON数据，方便后续扩展和调试
        UNIQUE(repo_id, number),--确保同一仓库内PR编号唯一
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE---仓库删除时自动删除相关PR记录
    );
    CREATE INDEX IF NOT EXISTS idx_prs_repo_merged ON pull_requests(repo_id, merged_at);

    CREATE TABLE IF NOT EXISTS commits 
    (
        id INTEGER PRIMARY KEY AUTOINCREMENT,--commit标识，自增主键
        repo_id INTEGER NOT NULL,--外键，表示哪个仓库的commit
        sha TEXT NOT NULL,--commit的SHA-1哈希，GitHub仓库内唯一
        author_login TEXT,--commit作者的GitHub登录名，可能为NULL（如匿名提交）
        committed_at TEXT NOT NULL,--commit的提交时间，来自GitHub API
        raw_json TEXT NOT NULL,--commit的原始JSON数据，方便后续扩展和调试
        UNIQUE(repo_id, sha),--确保同一仓库内commit SHA唯一
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE---仓库删除时自动删除相关commit记录
    );
    CREATE INDEX IF NOT EXISTS idx_commits_repo_time ON commits(repo_id, committed_at);
    CREATE INDEX IF NOT EXISTS idx_commits_sha ON commits(sha);

    CREATE TABLE IF NOT EXISTS commit_files (
        id INTEGER PRIMARY KEY AUTOINCREMENT,--commit文件标识，自增主键
        commit_id INTEGER NOT NULL,--外键，表示哪个commit的文件（原代码列名与约束不一致，已修正）
        sha TEXT NOT NULL,--冗余存储 commit sha，便于仅凭 sha 查询
        filename TEXT NOT NULL,--文件名，包含路径，如 "src/main.cpp"
        additions INTEGER NOT NULL DEFAULT 0,--文件的新增行数
        deletions INTEGER NOT NULL DEFAULT 0,--文件的删除行数
        changes INTEGER NOT NULL DEFAULT 0,--文件的总变更行数（additions + deletions）
        committed_at TEXT NOT NULL,--文件所属commit的提交时间，冗余存储方便查询和统计
        raw_json TEXT NOT NULL,--文件的原始JSON数据，方便后续扩展和调试
        UNIQUE(commit_id, filename),
        FOREIGN KEY (commit_id) REFERENCES commits(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_commit_files_commit ON commit_files(commit_id);
    CREATE INDEX IF NOT EXISTS idx_commit_files_commit_time ON commit_files(commit_id, committed_at); 
    CREATE INDEX IF NOT EXISTS idx_commit_files_sha ON commit_files(sha);

     -- Releases
    CREATE TABLE IF NOT EXISTS releases (
        id INTEGER PRIMARY KEY AUTOINCREMENT,--release标识，自增主键
        repo_id INTEGER NOT NULL,--外键，表示哪个仓库的release
        tag_name TEXT NOT NULL,--release的标签名称，GitHub仓库内唯一
        name TEXT,--release的名称，可能为NULL
        draft INTEGER NOT NULL DEFAULT 0,--release是否为草稿，GitHub API的draft字段，0=非草稿，1=草稿
        prerelease INTEGER NOT NULL DEFAULT 0,--release是否为预发布，GitHub API的prerelease字段，0=非预发布，1=预发布
        published_at TEXT,--release的发布时间，来自GitHub API，draft状态为NULL
        raw_json TEXT NOT NULL,--release的原始JSON数据，方便后续扩展和调试
        UNIQUE(repo_id, tag_name),--确保同一仓库内release tag_name唯一
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE---仓库删除时自动删除相关release记录
    );
    CREATE INDEX IF NOT EXISTS idx_releases_repo_published ON releases(repo_id, published_at);

    -- =============================================
    -- 2.4 AI 智能分析模块: 知识库 + 对话记录
    -- =============================================

    -- 知识块表: 存储从 issues/PRs/commits/releases 提取的结构化文本片段
    CREATE TABLE IF NOT EXISTS knowledge_chunks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        source_type TEXT NOT NULL,       -- 'issue' / 'pull_request' / 'commit' / 'release'
        source_id TEXT NOT NULL,         -- 原始标识: issue 编号 / commit sha 等
        title TEXT NOT NULL DEFAULT '',
        content TEXT NOT NULL DEFAULT '',
        author TEXT DEFAULT '',
        event_time TEXT DEFAULT '',
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_kchunks_repo ON knowledge_chunks(repo_id);
    CREATE INDEX IF NOT EXISTS idx_kchunks_source ON knowledge_chunks(repo_id, source_type, source_id);

    -- AI 对话记录表: 保存每次问答的问题、回答、引用证据
    CREATE TABLE IF NOT EXISTS ai_conversations (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER,
        question TEXT NOT NULL,
        answer TEXT NOT NULL,
        evidence_json TEXT,
        model TEXT,
        created_at TEXT NOT NULL DEFAULT (datetime('now'))
    );
    CREATE INDEX IF NOT EXISTS idx_ai_conv_repo ON ai_conversations(repo_id);

    -- 2.4(3) 风险预警扫描运行记录
    CREATE TABLE IF NOT EXISTS risk_alert_runs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        started_at TEXT NOT NULL DEFAULT (datetime('now')),
        finished_at TEXT,
        status TEXT NOT NULL DEFAULT 'running', -- running | ok | error
        summary_json TEXT,
        error TEXT,
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_risk_runs_repo_started ON risk_alert_runs(repo_id, started_at DESC);

    -- 2.4(3) 风险预警事件
    CREATE TABLE IF NOT EXISTS risk_alert_events (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        run_id INTEGER,
        repo_id INTEGER NOT NULL,
        alert_type TEXT NOT NULL,      -- code_churn_spike / issue_backlog_spike / pr_merge_latency_spike
        metric_name TEXT NOT NULL,
        window_start TEXT,
        window_end TEXT,
        current_value REAL NOT NULL,
        baseline_value REAL NOT NULL,
        threshold_value REAL NOT NULL,
        severity TEXT NOT NULL,        -- warning | critical
        scope_type TEXT NOT NULL DEFAULT 'repo',
        scope_id TEXT NOT NULL DEFAULT '',
        suggested_action TEXT,
        status TEXT NOT NULL DEFAULT 'open', -- open | acknowledged | resolved
        evidence_json TEXT,
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        FOREIGN KEY (run_id) REFERENCES risk_alert_runs(id) ON DELETE SET NULL,
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_risk_events_repo_created ON risk_alert_events(repo_id, created_at DESC);
    CREATE INDEX IF NOT EXISTS idx_risk_events_repo_status ON risk_alert_events(repo_id, status, severity);

    -- 2.3 CI 健康监控: GitHub Actions 工作流运行数据
    CREATE TABLE IF NOT EXISTS ci_workflow_runs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        run_id INTEGER NOT NULL,
        workflow_id INTEGER NOT NULL DEFAULT 0,
        name TEXT NOT NULL DEFAULT '',
        head_branch TEXT NOT NULL DEFAULT '',
        event TEXT NOT NULL DEFAULT '',
        status TEXT NOT NULL DEFAULT '',
        conclusion TEXT NOT NULL DEFAULT '',
        created_at TEXT NOT NULL DEFAULT '',
        updated_at TEXT NOT NULL DEFAULT '',
        run_started_at TEXT NOT NULL DEFAULT '',
        html_url TEXT NOT NULL DEFAULT '',
        actor_login TEXT NOT NULL DEFAULT '',
        run_attempt INTEGER NOT NULL DEFAULT 0,
        raw_json TEXT NOT NULL DEFAULT '',
        inserted_at TEXT NOT NULL DEFAULT (datetime('now')),
        UNIQUE(repo_id, run_id),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_ci_runs_repo_created ON ci_workflow_runs(repo_id, created_at DESC);
    CREATE INDEX IF NOT EXISTS idx_ci_runs_repo_status ON ci_workflow_runs(repo_id, status, conclusion);
    CREATE INDEX IF NOT EXISTS idx_ci_runs_repo_updated ON ci_workflow_runs(repo_id, updated_at DESC);


    -- =============================================
    -- 4.21 新增 :Tasks: AI 生成的任务清单（闭环）
    -- =============================================
    CREATE TABLE IF NOT EXISTS tasks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        title TEXT NOT NULL,
        priority TEXT NOT NULL DEFAULT 'P1',        -- P0/P1/P2
        status TEXT NOT NULL DEFAULT 'open',        -- open/done
        reason TEXT NOT NULL DEFAULT '',
        actions_json TEXT NOT NULL DEFAULT '[]',    -- JSON array of steps
        expected_benefit TEXT NOT NULL DEFAULT '',
        verify TEXT NOT NULL DEFAULT '',
        source TEXT NOT NULL DEFAULT 'ai',          -- ai/manual
        ai_conversation_id INTEGER,                 -- 可选：关联 ai_conversations.id
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        done_at TEXT,
        updated_at TEXT NOT NULL DEFAULT (datetime('now')),
        UNIQUE(repo_id, title),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_tasks_repo_status ON tasks(repo_id, status, priority);
    CREATE INDEX IF NOT EXISTS idx_tasks_repo_created ON tasks(repo_id, created_at DESC);


    )SQL";

    
    exec(schema);

    // --- lightweight migrations for existing DB files ---
    try {
        exec("ALTER TABLE repos ADD COLUMN intro_text TEXT NOT NULL DEFAULT '';");
    } catch (const std::exception& e) {
    }

    try {
        exec("ALTER TABLE repos ADD COLUMN intro_updated_at TEXT;");
    } catch (const std::exception& e) {
    }
}