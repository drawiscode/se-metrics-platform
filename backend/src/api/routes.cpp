
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


void register_routes(httplib::Server& app, Db& db)
{
    // 公共的健康检查路由也可以看成 GET 路由的一部分
    register_get_routes(app, db);
    register_post_routes(app, db);
    register_put_routes(app, db);
    register_delete_routes(app, db);
}


