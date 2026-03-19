#pragma once

#include <httplib.h>


class Db;

void register_routes(httplib::Server& app, Db& db);

