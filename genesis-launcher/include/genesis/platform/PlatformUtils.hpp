#pragma once

#include "genesis/core/Result.hpp"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace genesis::platform {

std::string default_game_dir();
std::string user_data_dir(const std::string& app_name);
std::string temp_dir();
std::string executable_path();

bool   path_exists(const std::string& path);
bool   is_directory(const std::string& path);
bool   is_file(const std::string& path);
core::Result<void> create_directories(const std::string& path);
core::Result<void> remove_file(const std::string& path);
core::Result<void> remove_directory_recursive(const std::string& path);
core::Result<void> copy_file(const std::string& src, const std::string& dst, bool overwrite = true);
core::Result<void> move_file(const std::string& src, const std::string& dst);
core::Result<void> atomic_write_file(const std::string& path, const std::string& content);

std::string path_join(const std::string& a, const std::string& b);
std::string path_join(std::initializer_list<std::string> parts);
std::string path_parent(const std::string& path);
std::string path_filename(const std::string& path);
std::string path_extension(const std::string& path);
std::string path_stem(const std::string& path);

core::Result<std::string> read_file(const std::string& path);
core::Result<void>        write_file(const std::string& path, const std::string& content);

core::Result<std::vector<std::string>> list_directory(const std::string& path);

int64_t total_memory_mb();
int32_t cpu_core_count();

std::string os_name();
std::string os_arch();
std::string os_version();

struct ProcessResult {
    int         exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

core::Result<ProcessResult> run_process(const std::string&              executable,
                                         const std::vector<std::string>& args,
                                         const std::string&              working_dir = {},
                                         int                             timeout_ms  = 30000);

void open_url_in_browser(const std::string& url);
void open_in_file_manager(const std::string& path);
void copy_to_clipboard(const std::string& text);

}
