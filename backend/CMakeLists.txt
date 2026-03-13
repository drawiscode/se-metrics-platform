
#// filepath: /e:/大三下/软件工程综合实验/lab/se-metrics-platform/backend/CMakeLists.txt
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
    src/main.cpp
)

target_link_libraries(devinsight_backend PRIVATE sqlite3)

# cpp-httplib (header-only). Put httplib.h under backend/third_party/httplib/httplib.h
target_include_directories(devinsight_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib)

if (WIN32)
  target_link_libraries(devinsight_backend PRIVATE ws2_32)
endif()

if (MSVC)
  target_compile_options(devinsight_backend PRIVATE /W4)
else()
  target_compile_options(devinsight_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()