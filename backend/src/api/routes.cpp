#include "routes.h"
#include "common/util.h"
#include "db/db.h"
#include "repo_metrics/github_client.h"

#include <cmath>
#include <nlohmann/json.hpp>
#include <string>
//#include <sqlite3.h>

// 声明更细分的注册函数（定义在其他 cpp 里）
void register_get_routes(httplib::Server& app, Db& db);
void register_post_routes(httplib::Server& app, Db& db);
void register_put_routes(httplib::Server& app, Db& db);
void register_delete_routes(httplib::Server& app, Db& db);
void register_ai_routes(httplib::Server& app, Db& db);   // 2.4 AI 智能分析路由
void register_risk_routes(httplib::Server& app, Db& db); // 2.4(3) 异常检测与风险预警路由

void register_tasks_routes(httplib::Server& app, Db& db);
void register_expert_routes(httplib::Server& app, Db& db);  // 2.4(5) 隐形专家识别
void register_report_routes(httplib::Server& app, Db& db);  // 2.4(4) 自动周报生成

void register_routes(httplib::Server& app, Db& db)
{
    // 公共的健康检查路由也可以看成 GET 路由的一部分
    register_get_routes(app, db);
    register_post_routes(app, db);
    register_put_routes(app, db);
    register_delete_routes(app, db);
    register_ai_routes(app, db);     // 2.4 知识库 + AI 问答
    register_risk_routes(app, db);   // 2.4(3) 风险扫描与告警查询
    register_tasks_routes(app, db);
    register_expert_routes(app, db); // 2.4(5) 隐形专家识别
    register_report_routes(app, db); // 2.4(4) 自动周报生成
}


