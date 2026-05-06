#pragma once

#include <string>
#include <map>

class Db;

struct CodeIndexResult {
    int indexed_files = 0;
    int indexed_chunks = 0;
    int embeddings_generated = 0;
    std::string repo_head_sha;
    std::map<std::string, int> skipped_reason;
};

CodeIndexResult build_code_index(Db& db, int repo_id,
                                 const std::string& full_name,
                                 const std::string& ref,
                                 const std::string& mode,
                                 int max_files,
                                 int max_total_kb);
