#include "db.h"
#include <iostream>

#include <stdexcept>
#include <utility>
#include <vector>

static bool has_column(sqlite3* db, const char* table, const char* col)
{
    std::string sql = std::string("PRAGMA table_info(") + table + ");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;

    bool ok = false;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char* name = sqlite3_column_text(stmt, 1); // column name
        if (name && std::string(reinterpret_cast<const char*>(name)) == col) { ok = true; break; }
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
    // MSVC has string literal size limit (~16KB), so we split into parts
    
    exec(R"SQL(
    PRAGMA foreign_keys = ON;

    CREATE TABLE IF NOT EXISTS repos (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        full_name TEXT NOT NULL UNIQUE,
        enabled INTEGER NOT NULL DEFAULT 1,
        created_at TEXT NOT NULL DEFAULT (datetime('now'))
    );

    CREATE TABLE IF NOT EXISTS repo_sync_runs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        started_at TEXT NOT NULL DEFAULT (datetime('now')),
        finished_at TEXT,
        status TEXT NOT NULL,
        error TEXT,
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );

    CREATE TABLE IF NOT EXISTS repo_sync_state (
        repo_id INTEGER PRIMARY KEY,
        issues_updated_cursor TEXT,
        pulls_updated_cursor TEXT,
        commits_since_cursor TEXT,
        releases_cursor TEXT,
        updated_at TEXT NOT NULL DEFAULT (datetime('now')),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );

    CREATE TABLE IF NOT EXISTS repo_snapshots (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        ts TEXT NOT NULL DEFAULT (datetime('now')),
        full_name TEXT NOT NULL,
        stars INTEGER NOT NULL,
        forks INTEGER NOT NULL,
        open_issues INTEGER NOT NULL,
        watchers INTEGER NOT NULL,
        pushed_at TEXT,
        raw_json TEXT,
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_repo_snapshots_repo_id_ts ON repo_snapshots(repo_id, ts);
    )SQL");

    exec(R"SQL(
    CREATE TABLE IF NOT EXISTS issues (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        number INTEGER NOT NULL,
        state TEXT NOT NULL,
        title TEXT NOT NULL,
        created_at TEXT NOT NULL,
        updated_at TEXT NOT NULL,
        closed_at TEXT,
        comments INTEGER NOT NULL DEFAULT 0,
        author_login TEXT,
        is_pull_request INTEGER NOT NULL DEFAULT 0,
        raw_json TEXT NOT NULL,
        UNIQUE(repo_id, number),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_issues_repo_state ON issues(repo_id, state);

    CREATE TABLE IF NOT EXISTS pull_requests (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        number INTEGER NOT NULL,
        state TEXT NOT NULL,
        title TEXT NOT NULL,
        created_at TEXT NOT NULL,
        updated_at TEXT NOT NULL,
        closed_at TEXT,
        merged_at TEXT,
        author_login TEXT,
        raw_json TEXT NOT NULL,
        UNIQUE(repo_id, number),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_prs_repo_merged ON pull_requests(repo_id, merged_at);

    CREATE TABLE IF NOT EXISTS commits (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        sha TEXT NOT NULL,
        author_login TEXT,
        committed_at TEXT NOT NULL,
        raw_json TEXT NOT NULL,
        UNIQUE(repo_id, sha),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_commits_repo_time ON commits(repo_id, committed_at);
    CREATE INDEX IF NOT EXISTS idx_commits_sha ON commits(sha);

    CREATE TABLE IF NOT EXISTS commit_files (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        commit_id INTEGER NOT NULL,
        sha TEXT NOT NULL,
        filename TEXT NOT NULL,
        additions INTEGER NOT NULL DEFAULT 0,
        deletions INTEGER NOT NULL DEFAULT 0,
        changes INTEGER NOT NULL DEFAULT 0,
        committed_at TEXT NOT NULL,
        raw_json TEXT NOT NULL,
        UNIQUE(commit_id, filename),
        FOREIGN KEY (commit_id) REFERENCES commits(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_commit_files_commit ON commit_files(commit_id);
    CREATE INDEX IF NOT EXISTS idx_commit_files_commit_time ON commit_files(commit_id, committed_at);
    CREATE INDEX IF NOT EXISTS idx_commit_files_sha ON commit_files(sha);

    CREATE TABLE IF NOT EXISTS releases (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        tag_name TEXT NOT NULL,
        name TEXT,
        draft INTEGER NOT NULL DEFAULT 0,
        prerelease INTEGER NOT NULL DEFAULT 0,
        published_at TEXT,
        raw_json TEXT NOT NULL,
        UNIQUE(repo_id, tag_name),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_releases_repo_published ON releases(repo_id, published_at);
    )SQL");

    exec(R"SQL(
    CREATE TABLE IF NOT EXISTS knowledge_chunks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        source_type TEXT NOT NULL,
        source_id TEXT NOT NULL,
        title TEXT NOT NULL DEFAULT '',
        content TEXT NOT NULL DEFAULT '',
        author TEXT DEFAULT '',
        event_time TEXT DEFAULT '',
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_kchunks_repo ON knowledge_chunks(repo_id);
    CREATE INDEX IF NOT EXISTS idx_kchunks_source ON knowledge_chunks(repo_id, source_type, source_id);

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

    CREATE TABLE IF NOT EXISTS system_operation_logs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        operation_type TEXT NOT NULL DEFAULT '',
        target TEXT NOT NULL DEFAULT '',
        status TEXT NOT NULL DEFAULT 'ok',
        duration_ms INTEGER NOT NULL DEFAULT 0,
        ip TEXT NOT NULL DEFAULT '',
        detail_json TEXT NOT NULL DEFAULT '{}'
    );
    CREATE INDEX IF NOT EXISTS idx_sys_ops_created ON system_operation_logs(created_at DESC);
    CREATE INDEX IF NOT EXISTS idx_sys_ops_type_status ON system_operation_logs(operation_type, status);

    CREATE TABLE IF NOT EXISTS system_ai_usage_logs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        repo_id INTEGER,
        repo_full_name TEXT NOT NULL DEFAULT '',
        model TEXT NOT NULL DEFAULT '',
        prompt_tokens INTEGER NOT NULL DEFAULT 0,
        completion_tokens INTEGER NOT NULL DEFAULT 0,
        total_tokens INTEGER NOT NULL DEFAULT 0,
        cost_usd REAL NOT NULL DEFAULT 0,
        duration_ms INTEGER NOT NULL DEFAULT 0,
        ip TEXT NOT NULL DEFAULT '',
        status TEXT NOT NULL DEFAULT 'ok',
        error TEXT NOT NULL DEFAULT '',
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE SET NULL
    );
    CREATE INDEX IF NOT EXISTS idx_sys_ai_created ON system_ai_usage_logs(created_at DESC);
    CREATE INDEX IF NOT EXISTS idx_sys_ai_repo_created ON system_ai_usage_logs(repo_id, created_at DESC);

    CREATE TABLE IF NOT EXISTS risk_alert_runs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        started_at TEXT NOT NULL DEFAULT (datetime('now')),
        finished_at TEXT,
        status TEXT NOT NULL DEFAULT 'running',
        summary_json TEXT,
        error TEXT,
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_risk_runs_repo_started ON risk_alert_runs(repo_id, started_at DESC);

    CREATE TABLE IF NOT EXISTS risk_alert_events (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        run_id INTEGER,
        repo_id INTEGER NOT NULL,
        alert_type TEXT NOT NULL,
        metric_name TEXT NOT NULL,
        window_start TEXT,
        window_end TEXT,
        current_value REAL NOT NULL,
        baseline_value REAL NOT NULL,
        threshold_value REAL NOT NULL,
        severity TEXT NOT NULL,
        scope_type TEXT NOT NULL DEFAULT 'repo',
        scope_id TEXT NOT NULL DEFAULT '',
        suggested_action TEXT,
        status TEXT NOT NULL DEFAULT 'open',
        evidence_json TEXT,
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        FOREIGN KEY (run_id) REFERENCES risk_alert_runs(id) ON DELETE SET NULL,
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_risk_events_repo_created ON risk_alert_events(repo_id, created_at DESC);
    CREATE INDEX IF NOT EXISTS idx_risk_events_repo_status ON risk_alert_events(repo_id, status, severity);

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
    )SQL");

    exec(R"SQL(
    CREATE TABLE IF NOT EXISTS quality_issues (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        tool TEXT NOT NULL,
        issue_key TEXT NOT NULL DEFAULT '',
        file_path TEXT NOT NULL,
        line INTEGER NOT NULL DEFAULT 0,
        column INTEGER NOT NULL DEFAULT 0,
        rule_id TEXT NOT NULL DEFAULT '',
        severity TEXT NOT NULL DEFAULT '',
        message TEXT NOT NULL DEFAULT '',
        status TEXT NOT NULL DEFAULT 'active',
        first_seen_at TEXT NOT NULL DEFAULT (datetime('now')),
        first_seen_run_id INTEGER NOT NULL DEFAULT 0,
        last_seen_run_id INTEGER NOT NULL DEFAULT 0,
        last_seen_at TEXT,
        fixed_at TEXT,
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_quality_issues_repo_tool ON quality_issues(repo_id, tool);
    CREATE INDEX IF NOT EXISTS idx_quality_issues_repo_file ON quality_issues(repo_id, file_path);


    CREATE TABLE IF NOT EXISTS quality_analysis_tasks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        branch TEXT NOT NULL DEFAULT 'main',
        tools TEXT NOT NULL DEFAULT 'cppcheck',
        mode TEXT NOT NULL DEFAULT 'full',
        max_files INTEGER NOT NULL DEFAULT 2000,
        config_json TEXT NOT NULL DEFAULT '{}',
        schedule TEXT NOT NULL DEFAULT 'manual',
        status TEXT NOT NULL DEFAULT 'Pending',
        last_run_id INTEGER,
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        updated_at TEXT NOT NULL DEFAULT (datetime('now')),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_quality_tasks_repo ON quality_analysis_tasks(repo_id, created_at DESC);
    CREATE INDEX IF NOT EXISTS idx_quality_tasks_status ON quality_analysis_tasks(repo_id, status);

    CREATE TABLE IF NOT EXISTS quality_analysis_runs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        task_id INTEGER,
        repo_id INTEGER NOT NULL,
        branch TEXT NOT NULL DEFAULT 'main',
        tools TEXT NOT NULL DEFAULT '',
        mode TEXT NOT NULL DEFAULT 'full',
        max_files INTEGER NOT NULL DEFAULT 0,
        status TEXT NOT NULL DEFAULT 'Running',
        config_json TEXT NOT NULL DEFAULT '{}',
        started_at TEXT NOT NULL DEFAULT (datetime('now')),
        finished_at TEXT,
        analyzed_files INTEGER NOT NULL DEFAULT 0,
        lines_analyzed INTEGER NOT NULL DEFAULT 0,
        issues_total INTEGER NOT NULL DEFAULT 0,
        issues_new INTEGER NOT NULL DEFAULT 0,
        issues_fixed INTEGER NOT NULL DEFAULT 0,
        issues_by_severity_json TEXT NOT NULL DEFAULT '{}',
        score REAL NOT NULL DEFAULT 0,
        baseline_score REAL,
        degraded INTEGER NOT NULL DEFAULT 0,
        output_json TEXT NOT NULL DEFAULT '{}',
        error TEXT NOT NULL DEFAULT '',
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE,
        FOREIGN KEY (task_id) REFERENCES quality_analysis_tasks(id) ON DELETE SET NULL
    );
    CREATE INDEX IF NOT EXISTS idx_quality_runs_repo_started ON quality_analysis_runs(repo_id, started_at DESC);
    CREATE INDEX IF NOT EXISTS idx_quality_runs_task ON quality_analysis_runs(task_id, started_at DESC);

    CREATE TABLE IF NOT EXISTS quality_run_issues (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        run_id INTEGER NOT NULL,
        repo_id INTEGER NOT NULL,
        tool TEXT NOT NULL,
        issue_key TEXT NOT NULL DEFAULT '',
        file_path TEXT NOT NULL,
        line INTEGER NOT NULL DEFAULT 0,
        column INTEGER NOT NULL DEFAULT 0,
        rule_id TEXT NOT NULL DEFAULT '',
        severity TEXT NOT NULL DEFAULT '',
        message TEXT NOT NULL DEFAULT '',
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        FOREIGN KEY (run_id) REFERENCES quality_analysis_runs(id) ON DELETE CASCADE,
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_quality_run_issues_run ON quality_run_issues(run_id);
    CREATE INDEX IF NOT EXISTS idx_quality_run_issues_repo_tool ON quality_run_issues(repo_id, tool);

    CREATE TABLE IF NOT EXISTS quality_baselines (
        repo_id INTEGER PRIMARY KEY,
        min_score REAL NOT NULL DEFAULT 80.0,
        max_new_issues INTEGER NOT NULL DEFAULT 0,
        max_error_issues INTEGER NOT NULL DEFAULT 0,
        updated_at TEXT NOT NULL DEFAULT (datetime('now')),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );

    CREATE TABLE IF NOT EXISTS tasks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        title TEXT NOT NULL,
        priority TEXT NOT NULL DEFAULT 'P1',
        status TEXT NOT NULL DEFAULT 'open',
        reason TEXT NOT NULL DEFAULT '',
        actions_json TEXT NOT NULL DEFAULT '[]',
        expected_benefit TEXT NOT NULL DEFAULT '',
        verify TEXT NOT NULL DEFAULT '',
        source TEXT NOT NULL DEFAULT 'ai',
        ai_conversation_id INTEGER,
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        done_at TEXT,
        updated_at TEXT NOT NULL DEFAULT (datetime('now')),
        UNIQUE(repo_id, title),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_tasks_repo_status ON tasks(repo_id, status, priority);
    CREATE INDEX IF NOT EXISTS idx_tasks_repo_created ON tasks(repo_id, created_at DESC);

    CREATE TABLE IF NOT EXISTS weekly_reports (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        repo_id INTEGER NOT NULL,
        week_start TEXT NOT NULL,
        week_end TEXT NOT NULL,
        report_text TEXT NOT NULL,
        metrics_json TEXT,
        model TEXT,
        created_at TEXT NOT NULL DEFAULT (datetime('now')),
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE
    );
    CREATE INDEX IF NOT EXISTS idx_weekly_reports_repo ON weekly_reports(repo_id, created_at DESC);
    )SQL");

    // --- lightweight migrations for existing DB files ---
    try {
        exec("ALTER TABLE repos ADD COLUMN intro_text TEXT NOT NULL DEFAULT '';");
    } catch (const std::exception& e) {
    }

    try {
        exec("ALTER TABLE repos ADD COLUMN intro_updated_at TEXT;");
    } catch (const std::exception& e) {
    }

    // 2.4 向量检索: 为 knowledge_chunks 添加 embedding 列
    if (!has_column(db_, "knowledge_chunks", "embedding")) {
        try {
            exec("ALTER TABLE knowledge_chunks ADD COLUMN embedding BLOB;");
        } catch (const std::exception& e) {
            std::cerr << "[db] ALTER TABLE add embedding column failed: " << e.what() << "\n";
        }
    }

    const std::vector<std::pair<const char*, const char*>> quality_issue_columns = {
        {"issue_key", "ALTER TABLE quality_issues ADD COLUMN issue_key TEXT NOT NULL DEFAULT '';"},
        {"status", "ALTER TABLE quality_issues ADD COLUMN status TEXT NOT NULL DEFAULT 'active';"},
        {"first_seen_run_id", "ALTER TABLE quality_issues ADD COLUMN first_seen_run_id INTEGER NOT NULL DEFAULT 0;"},
        {"last_seen_run_id", "ALTER TABLE quality_issues ADD COLUMN last_seen_run_id INTEGER NOT NULL DEFAULT 0;"},
        {"last_seen_at", "ALTER TABLE quality_issues ADD COLUMN last_seen_at TEXT;"},
        {"fixed_at", "ALTER TABLE quality_issues ADD COLUMN fixed_at TEXT;"}
    };
    for (const auto& col : quality_issue_columns) {
        if (!has_column(db_, "quality_issues", col.first)) {
            try {
                exec(col.second);
            } catch (const std::exception& e) {
                std::cerr << "[db] quality_issues migration failed for "
                          << col.first << ": " << e.what() << "\n";
            }
        }
    }

    try {
        exec("UPDATE quality_issues SET status='active' WHERE status IS NULL OR status='';");
        exec("UPDATE quality_issues SET issue_key='legacy-' || id WHERE issue_key IS NULL OR issue_key='';");
        exec("CREATE INDEX IF NOT EXISTS idx_quality_issues_repo_status ON quality_issues(repo_id, status, tool);");
        exec("CREATE INDEX IF NOT EXISTS idx_quality_issues_key ON quality_issues(repo_id, tool, issue_key);");
    } catch (const std::exception& e) {
        std::cerr << "[db] quality_issues index/backfill failed: " << e.what() << "\n";
    }
}
