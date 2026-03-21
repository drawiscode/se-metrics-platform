//test_way: Invoke-RestMethod
#include <iostream>
#include <string>
#include <cstdlib>

#include "httplib.h"
#include <sqlite3.h>

#include "api/routes.h"
#include "common/util.h"
#include "db/db.h"

int main()
{
    try
    {
        util::load_env_file("E:/study/SoftwareLab/lab/se-metrics-platform/backend/config/config.env");//这里请换成你的实际路径

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