#include "genesis/logging/Logger.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/ui/LogStream.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <unordered_map>
#include <mutex>

namespace genesis::logging {

namespace {
    std::shared_ptr<spdlog::logger> s_root_logger;
    std::unordered_map<std::string, std::shared_ptr<Logger>> s_loggers;
    std::mutex s_mutex;
}

static spdlog::level::level_enum to_spdlog(Level l) {
    switch (l) {
        case Level::Trace:    return spdlog::level::trace;
        case Level::Debug:    return spdlog::level::debug;
        case Level::Info:     return spdlog::level::info;
        case Level::Warn:     return spdlog::level::warn;
        case Level::Error:    return spdlog::level::err;
        case Level::Critical: return spdlog::level::critical;
    }
    return spdlog::level::info;
}

void init_logging(const std::string& log_dir, bool also_stdout) {
    platform::create_directories(log_dir);
    std::string log_path = platform::path_join(log_dir, "genesis.log");

    std::vector<spdlog::sink_ptr> sinks;
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_path, 5 * 1024 * 1024, 3, false);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
    sinks.push_back(file_sink);

    if (also_stdout) {
        auto con_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        con_sink->set_pattern("%^[%H:%M:%S] [%n] [%l]%$ %v");
        sinks.push_back(con_sink);
    }

    s_root_logger = std::make_shared<spdlog::logger>("genesis", sinks.begin(), sinks.end());
    s_root_logger->set_level(spdlog::level::info);
    s_root_logger->flush_on(spdlog::level::warn);
    spdlog::register_logger(s_root_logger);
    spdlog::set_default_logger(s_root_logger);

    // Install the UI streaming sink so log records are mirrored into the
    // in-memory LogStream consumed by the console region.
    ui::log::install_into_root_logger();
}

void shutdown_logging() {
    spdlog::shutdown();
}

std::shared_ptr<Logger> get_logger(const std::string& name) {
    std::lock_guard lock(s_mutex);
    auto it = s_loggers.find(name);
    if (it != s_loggers.end()) return it->second;
    auto l = std::make_shared<Logger>(name);
    s_loggers[name] = l;
    return l;
}

std::shared_ptr<Logger> root_logger() {
    static auto l = std::make_shared<Logger>("genesis");
    return l;
}

Logger::Logger(std::string name) : name_(std::move(name)) {}

void Logger::trace(const std::string& msg) {
    auto l = spdlog::get(name_);
    if (!l) l = spdlog::default_logger();
    if (l) l->trace(msg);
}

void Logger::debug(const std::string& msg) {
    auto l = spdlog::get(name_);
    if (!l) l = spdlog::default_logger();
    if (l) l->debug(msg);
}

void Logger::info(const std::string& msg) {
    auto l = spdlog::get(name_);
    if (!l) l = spdlog::default_logger();
    if (l) l->info(msg);
}

void Logger::warn(const std::string& msg) {
    auto l = spdlog::get(name_);
    if (!l) l = spdlog::default_logger();
    if (l) l->warn(msg);
}

void Logger::error(const std::string& msg) {
    auto l = spdlog::get(name_);
    if (!l) l = spdlog::default_logger();
    if (l) l->error(msg);
}

void Logger::critical(const std::string& msg) {
    auto l = spdlog::get(name_);
    if (!l) l = spdlog::default_logger();
    if (l) l->critical(msg);
}

void Logger::set_level(Level min_level) {
    auto l = spdlog::get(name_);
    if (l) l->set_level(to_spdlog(min_level));
}

}
