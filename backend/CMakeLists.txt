
cmake_minimum_required(VERSION 3.20)
project(devinsight_backend LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(devinsight_backend
    src/app/main.cpp
    src/api/routes.cpp
    src/common/util.cpp
    src/db/db.cpp
    src/repo_metrics/github_client.cpp
)


# 让项目内 include "api/..." 之类能找到
target_include_directories(devinsight_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    C:/vcpkg/installed/x64-windows/include 
)

# vcpkg dependencies
find_package(SQLite3 REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

find_package(OpenSSL REQUIRED)

target_link_libraries(devinsight_backend PRIVATE
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)

# enable https support in cpp-httplib
#target_compile_definitions(devinsight_backend PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

if (WIN32)
  target_link_libraries(devinsight_backend PRIVATE ws2_32 crypt32)
endif()

if (MSVC)
  target_compile_options(devinsight_backend PRIVATE /W4)
else()
  target_compile_options(devinsight_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (MSVC)
  target_compile_definitions(devinsight_backend PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()