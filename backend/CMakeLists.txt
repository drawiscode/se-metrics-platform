# ============================================================
# DevInsight Backend - CMakeLists.txt
# ============================================================
# 支持两种构建方式:
#   方式 A (Windows/vcpkg):  设置 CMAKE_TOOLCHAIN_FILE 指向 vcpkg.cmake
#   方式 B (Linux/系统包):    apt install libsqlite3-dev libssl-dev nlohmann-json3-dev
#                             或使用 conda: conda install sqlite openssl nlohmann_json
# 详见 README.md 环境配置章节
# ============================================================


set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

cmake_minimum_required(VERSION 3.20)
project(devinsight_backend LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(devinsight_backend
    src/app/main.cpp
    src/api/routes.cpp
    src/api/routes_delete.cpp
    src/api/routes_get.cpp
    src/api/routes_post.cpp
    src/api/routes_put.cpp
    src/api/routes_ai.cpp
    src/api/routes_risk.cpp
    src/common/util.cpp
    src/db/db.cpp
    src/repo_metrics/github_client.cpp
    src/repo_metrics/metrics.cpp
    src/repo_metrics/health.cpp
    src/repo_metrics/hotspots.cpp
    src/risk/detector.cpp
    src/ai/knowledge_base.cpp
    src/ai/ai_assistant.cpp
)

# 项目内 include 路径
target_include_directories(devinsight_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---- 依赖查找 ----
# 尝试用 find_package 查找（vcpkg / 系统包均可），找不到则回退到手动路径

# SQLite3
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    target_link_libraries(devinsight_backend PRIVATE SQLite::SQLite3)
else()
    find_library(SQLITE3_LIB sqlite3 REQUIRED)
    target_link_libraries(devinsight_backend PRIVATE ${SQLITE3_LIB})
endif()

# nlohmann_json (header-only)
find_package(nlohmann_json CONFIG QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(devinsight_backend PRIVATE nlohmann_json::nlohmann_json)
endif()
# 若未找到 nlohmann_json 包，只要 include 路径中有 nlohmann/json.hpp 即可编译

# OpenSSL
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_link_libraries(devinsight_backend PRIVATE OpenSSL::SSL OpenSSL::Crypto)
else()
    find_library(SSL_LIB ssl REQUIRED)
    find_library(CRYPTO_LIB crypto REQUIRED)
    target_link_libraries(devinsight_backend PRIVATE ${SSL_LIB} ${CRYPTO_LIB})
endif()

# cpp-httplib: header-only，已放在 src/httplib.h，无需额外链接
# 启用 HTTPS 支持
target_compile_definitions(devinsight_backend PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

# ---- 平台相关 ----
if(WIN32)
    target_link_libraries(devinsight_backend PRIVATE ws2_32 crypt32)
else()
    target_link_libraries(devinsight_backend PRIVATE pthread)
endif()

# ---- 编译选项 ----
if(MSVC)
    target_compile_options(devinsight_backend PRIVATE /W4)
    target_compile_definitions(devinsight_backend PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(devinsight_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- 可选: 额外 include/lib 路径 ----
# 若依赖装在非标准路径（如 conda、vcpkg 手动目录），取消注释并修改:
# target_include_directories(devinsight_backend PRIVATE /path/to/include)
# target_link_directories(devinsight_backend PRIVATE /path/to/lib)
