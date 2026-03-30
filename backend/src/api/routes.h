#pragma once

#include <httplib.h>
#include "db/db.h"

class Db;

void register_routes(httplib::Server& app, Db& db);

