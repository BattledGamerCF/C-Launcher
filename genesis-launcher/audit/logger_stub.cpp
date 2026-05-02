#include "genesis/logging/Logger.hpp"
#include <iostream>
#include <mutex>
namespace genesis::logging {
static std::mutex g_log_mu;
static bool       g_quiet = true;
Logger::Logger(std::string n) : name_(std::move(n)) {}
static void emit(const char* lvl, const std::string& n, const std::string& m) {
    if (g_quiet) return;
    std::lock_guard<std::mutex> l(g_log_mu);
    std::cerr << "[" << lvl << " " << n << "] " << m << "\n";
}
void Logger::trace(const std::string& m){ emit("T", name_, m); }
void Logger::debug(const std::string& m){ emit("D", name_, m); }
void Logger::info (const std::string& m){ emit("I", name_, m); }
void Logger::warn (const std::string& m){ emit("W", name_, m); }
void Logger::error(const std::string& m){ emit("E", name_, m); }
void Logger::critical(const std::string& m){ emit("C", name_, m); }
void Logger::set_level(Level){}
void init_logging(const std::string&, bool){}
void shutdown_logging(){}
std::shared_ptr<Logger> get_logger(const std::string& n){ return std::make_shared<Logger>(n); }
std::shared_ptr<Logger> root_logger(){ static auto r = std::make_shared<Logger>("root"); return r; }
}
