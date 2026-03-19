cmake_minimum_required(VERSION 3.20)
project(devinsight_backend LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# sqlite library
add_library(sqlite3
    third_party/sqlite/sqlite3.c
)

target_include_directories(sqlite3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
)

add_executable(devinsight_backend
    src/app/main.cpp
    src/api/routes.cpp
    src/common/util.cpp
    src/db/db.cpp
    src/repo_metrics/github_client.cpp
)

target_link_libraries(devinsight_backend PRIVATE sqlite3)

# cpp-httplib (header-only). Put httplib.h under backend/third_party/httplib/httplib.h
target_include_directories(devinsight_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib)

target_include_directories(devinsight_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

target_include_directories(devinsight_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# 1. 手动指定 OpenSSL 头文件所在目录
target_include_directories(devinsight_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl/include
)

# enable https support in cpp-httplib
target_compile_definitions(devinsight_backend PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)


find_package(OpenSSL REQUIRED)
target_link_libraries(devinsight_backend PRIVATE OpenSSL::SSL OpenSSL::Crypto)

if (WIN32)
  target_link_libraries(devinsight_backend PRIVATE ws2_32 crypt32)
endif()


if (MSVC)
  target_compile_options(devinsight_backend PRIVATE /W4)
else()
  target_compile_options(devinsight_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()