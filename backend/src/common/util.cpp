// filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/src/util.cpp
#include "util.h"

#include <cstdlib>
#include <stdexcept>
#include <sqlite3.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace util {

std::string get_env(const char* name, const std::string& def)
{
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) == 0 && value) {
        std::string out(value);
        free(value);
        return !out.empty() ? out : def;
    }
    return def;
#else
    const char* v = getenv(name);
    return (v && *v) ? std::string(v) : def;
#endif
}

std::string json_escape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '\"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default: o += c; break;
        }
    }
    return o;
    
}



static inline void trim_inplace(std::string& s)
{
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
}

// ✅ 新增：对外的 trim
std::string trim(const std::string& s)
{
    std::string out = s;
    trim_inplace(out);
    return out;
}


static void set_env_kv(const std::string& k, const std::string& v)
{
#ifdef _WIN32
    _putenv_s(k.c_str(), v.c_str());
#else
    setenv(k.c_str(), v.c_str(), 1);
#endif
}

void load_env_file(const std::string& path)
{
    std::ifstream in(path);
    if (!in.is_open())
        return; // 没有文件就跳过，保持兼容

    std::string line;
    while (std::getline(in, line))
    {
        // 去掉 \r（Windows 换行）
        if (!line.empty() && line.back() == '\r') line.pop_back();

        trim_inplace(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        trim_inplace(key);
        trim_inplace(val);

        // 支持简单的 "..." 包裹
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\'')))
            val = val.substr(1, val.size() - 2);

        if (!key.empty())
            set_env_kv(key, val);
    }
}

} // namespace util