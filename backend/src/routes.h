#pragma once

#include "httplib.h"
#include <sqlite3.h>


/*static void health_handler(const httplib::Request& req, httplib::Response& res);

static void post_projects_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);
static void post_repos_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);
static void post_project_repos_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);
static void put_repo_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);

static void get_repos_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);
static void get_repo_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);
static void get_projects_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);
static void get_project_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);
static void get_project_repos_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);

static void put_project_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);

static void delete_project_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);
static void delete_project_repo_link_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);
static void delete_repo_handler(sqlite3* db, const httplib::Request& req, httplib::Response& res);
*/

void register_routes(httplib::Server& app, sqlite3* db);

