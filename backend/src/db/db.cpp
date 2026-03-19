// filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/src/db.cpp
#include "db.h"

#include <stdexcept>
#include <utility>

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

    CREATE TABLE IF NOT EXISTS projects (
        id INTEGER PRIMARY KEY AUTOINCREMENT,--项目标识，自增主键
        name TEXT NOT NULL UNIQUE,--项目名称
        created_at TEXT NOT NULL DEFAULT (datetime('now'))--项目创建时间，这里是插入记录的时间
    );

    CREATE TABLE IF NOT EXISTS repos (
        id INTEGER PRIMARY KEY AUTOINCREMENT,--仓库标识，自增主键
        full_name TEXT NOT NULL UNIQUE,--仓库完整名称，如 "owner/repo"
        enabled INTEGER NOT NULL DEFAULT 1,--是否启用同步，1=启用（默认），0=禁用
        created_at TEXT NOT NULL DEFAULT (datetime('now'))--仓库创建时间，这里是插入记录的时间
    );

    CREATE TABLE IF NOT EXISTS project_repos (
        project_id INTEGER NOT NULL,--外键，表示属于哪个项目
        repo_id INTEGER NOT NULL,--表示关联的仓库
        created_at TEXT NOT NULL DEFAULT (datetime('now')),--这条关系建立的时间
        PRIMARY KEY (project_id, repo_id),--联合主键，确保同一项目和仓库的关系唯一
        FOREIGN KEY (project_id) REFERENCES projects(id) ON DELETE CASCADE,--项目删除时自动删除相关关系
        FOREIGN KEY (repo_id) REFERENCES repos(id) ON DELETE CASCADE---仓库删除时自动删除相关关系
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

     -- Releases
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

    )SQL";

    exec(schema);
}