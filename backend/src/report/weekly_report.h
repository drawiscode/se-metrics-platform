// 2.4(4) 自动周报生成模块
// 聚合最近一周的开发数据，通过 LLM 生成结构化项目周报
#pragma once

#include <string>
#include <vector>

class Db;

// 单份周报记录
struct WeeklyReport {
    int id = 0;
    int repo_id = 0;
    std::string week_start;      // 周期起始日 (ISO date)
    std::string week_end;        // 周期结束日 (ISO date)
    std::string report_text;     // LLM 生成的 Markdown 周报
    std::string metrics_json;    // 原始指标快照 JSON
    std::string model;           // 使用的 LLM 模型
    std::string created_at;
};

// 生成周报（聚合数据 + 调用 LLM）
WeeklyReport generate_weekly_report(Db& db, int repo_id);

// 获取最新周报
WeeklyReport get_latest_report(Db& db, int repo_id);

// 查询周报历史
std::vector<WeeklyReport> list_weekly_reports(Db& db, int repo_id, int limit = 10);

// 获取指定 id 的周报（全文）
WeeklyReport get_weekly_report_by_id(Db& db, int repo_id, int report_id);

// JSON 序列化
std::string report_to_json(const WeeklyReport& r);
std::string reports_to_json(const std::vector<WeeklyReport>& reports);
