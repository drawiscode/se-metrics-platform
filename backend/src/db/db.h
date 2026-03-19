// filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/src/db.h
#pragma once

#include <string>
#include <sqlite3.h>

class Db {
public:
    explicit Db(const std::string& path);
    ~Db();

    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    Db(Db&& other) noexcept;
    Db& operator=(Db&& other) noexcept;

    sqlite3* handle() const noexcept { return db_; }
    const std::string& path() const noexcept { return path_; }

    void exec(const std::string& sql);
    void init_schema();

private:
    void close() noexcept;

    sqlite3* db_{nullptr};
    std::string path_;
};