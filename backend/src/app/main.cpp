//test_way: Invoke-RestMethod
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "httplib.h"
#include <sqlite3.h>

#include "api/routes.h"
#include "common/util.h"
#include "db/db.h"

static void load_env_best_effort()
{
    namespace fs = std::filesystem;

    // 1) 允许通过环境变量显式指定（支持绝对/相对路径）
    const std::string explicit_path = util::get_env("DEVINSIGHT_ENV_FILE", "");
    if (!explicit_path.empty())
    {
        util::load_env_file(explicit_path);
        return;
    }

    // 2) 自动探测：根据常见启动工作目录尝试相对路径
    const std::vector<fs::path> candidates = {
        // 从项目根启动（E:/Code/se-metrics-platform）
        fs::path("backend/config/config.env"),

        // 从 build/ 启动（E:/Code/se-metrics-platform/build）
        fs::path("../backend/config/config.env"),
        fs::path("../../backend/config/config.env"),

        // 从 backend/ 启动（E:/Code/se-metrics-platform/backend）
        fs::path("config/config.env"),
    };

    for (const auto& p : candidates)
    {
        std::error_code ec;
        if (fs::exists(p, ec) && !ec)
        {
            util::load_env_file(p.string());
            return;
        }
    }

    // 保持 util::load_env_file 的“找不到就跳过”语义，但给出提示避免误解
    std::cout << "[WARN] config.env 未找到，已跳过加载。当前工作目录: "
              << fs::current_path().string() << "\n"
              << "       可设置环境变量 DEVINSIGHT_ENV_FILE 指向文件路径。\n";
}

int main()
{
    try
    {
        load_env_best_effort();

        const std::string db_path = util::get_env("DEVINSIGHT_DB", "data/devinsight.db");
       // std::cout << "[DEBUG] DEVINSIGHT_DB from env or default: " << db_path << "\n";

        const int port = std::stoi(util::get_env("PORT", "8080"));

        Db db(db_path);
        db.init_schema();

        httplib::Server app;
        register_routes(app, db);

        std::cout << "DevInsight backend listening on http://127.0.0.1:" << port << "\n";
        std::cout << "DB: " << db_path << "\n";

        app.listen("0.0.0.0", port);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[FATAL] " << e.what() << "\n";
        return 1;
    }
}