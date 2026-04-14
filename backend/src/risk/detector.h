#pragma once

#include <string>
#include <vector>

class Db;

// 持久化后的风险告警记录（供查询接口返回）
struct RiskAlertEvent {
    int id = 0;
    int repo_id = 0;
    std::string alert_type;      // 例如 code_churn_spike
    std::string metric_name;     // 例如 daily_churn
    std::string window_start;    // 检测窗口开始日期
    std::string window_end;      // 检测窗口结束日期
    double current_value = 0.0;
    double baseline_value = 0.0;
    double threshold_value = 0.0;
    std::string severity;        // 告警级别：warning / critical
    std::string scope_type;      // 影响范围类型：repo / file / module
    std::string scope_id;
    std::string suggested_action;
    std::string status;          // 处理状态：open / acknowledged / resolved
    std::string evidence_json;
    std::string created_at;
};

// 一次风险扫描的返回结果
struct RiskScanResult {
    bool success = false;
    int repo_id = 0;
    int run_id = 0;
    int alerts_created = 0;
    std::string error;
    std::vector<RiskAlertEvent> alerts;
};

// 对单仓库执行风险检测，并将告警落库
RiskScanResult scan_repo_risks(Db& db, int repo_id, int lookback_days = 30);

// 查询已落库告警，支持 status/severity 过滤
std::vector<RiskAlertEvent> list_risk_alerts(Db& db,
                                             int repo_id,
                                             const std::string& status,
                                             const std::string& severity,
                                             int limit,
                                             int offset);

// 接口返回用的序列化辅助函数
std::string risk_scan_result_to_json(const RiskScanResult& result);
std::string risk_alerts_to_json(const std::vector<RiskAlertEvent>& alerts);
std::string risk_alerts_summary_to_json(Db& db, int repo_id, int days);
