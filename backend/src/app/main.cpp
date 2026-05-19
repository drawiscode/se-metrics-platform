//test_way: Invoke-RestMethod
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <fstream>

#include "httplib.h"
#include <sqlite3.h>

#include "api/routes.h"
#include "common/util.h"
#include "db/db.h"

static void load_env_best_effort()
{
    namespace fs = std::filesystem;

    // 允许通过环境变量显式指定（支持绝对/相对路径）
    const std::string explicit_path = util::get_env("DEVINSIGHT_ENV_FILE", "");
    if (!explicit_path.empty())
    {
        util::load_env_file(explicit_path);
        return;
    }

    // 自动探测：根据常见启动工作目录尝试相对路径
    const std::vector<fs::path> candidates = {
        // 发布版：exe 在项目根目录，config/ 在根目录下
        fs::path("config/config.env"),

        // 从项目根启动（开发环境）
        fs::path("backend/config/config.env"),

        // 从 build/ 启动
        fs::path("../backend/config/config.env"),
        fs::path("../../backend/config/config.env"),

        fs::path("../se-metrics-platform/backend/config/config.env"),//适用于lwy的目录结构
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

        const int port = std::stoi(util::get_env("PORT", "8080"));

        Db db(db_path);
        db.init_schema();

        httplib::Server app;

        // 1. 先注册 API 路由（httplib handler 优先级高于 mount）
        register_routes(app, db);

        // 2. 托管前端静态文件（发布版 frontend-dist 目录）
        {
            namespace fs = std::filesystem;
            // 探测 frontend-dist 目录的路径
            std::vector<fs::path> frontend_candidates = {
                "frontend-dist",                // 发布版：exe 同级
                "backend/frontend-dist",        // 开发环境
                "../frontend-dist",             // build/ 目录启动
            };
            std::string frontend_root;
            for (const auto& p : frontend_candidates)
            {
                std::error_code ec;
                if (fs::exists(p / "index.html", ec) && !ec)
                {
                    frontend_root = p.string();
                    break;
                }
            }

            if (!frontend_root.empty())
            {
                // 挂载静态文件目录
                app.set_mount_point("/", frontend_root);

                // SPA fallback: 非 /api/ 路径回退到 index.html
                // httplib 的 handler 优先级高于 mount，
                // 此 catch-all 只在没有匹配的静态文件时触发
                auto fallback_handler = [frontend_root](const httplib::Request& req, httplib::Response& res) {
                    // API 路径不拦截，让它们 404
                    if (req.path.find("/api/") == 0)
                        return false;
                    // 已经是静态资源后缀，不处理
                    std::string path = req.path;
                    if (path.find('.') != std::string::npos)
                        return false;
                    // 返回 index.html（SPA 路由）
                    std::ifstream ifs(frontend_root + "/index.html");
                    if (ifs)
                    {
                        std::string content((std::istreambuf_iterator<char>(ifs)),
                                             std::istreambuf_iterator<char>());
                        res.set_content(content, "text/html");
                    }
                    return true;
                };
                // 用 Get handler 兜底（mount 未命中时触发）
                app.Get(".*", fallback_handler);

                std::cout << "[INFO] 前端静态文件已托管: " << frontend_root << "\n";
            }
            else
            {
                std::cout << "[INFO] 未找到前端静态文件目录 (frontend-dist)，仅提供 API 服务\n";
            }
        }

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

