#pragma once

#include <string>
#include <memory>
#include <functional>

namespace genesis::logging {

enum class Level { Trace, Debug, Info, Warn, Error, Critical };

struct LogRecord {
    Level       level;
    std::string logger_name;
    std::string message;
    int64_t     timestamp_us;
    std::string file;
    int         line;
};

class Logger {
public:
    explicit Logger(std::string name);

    void trace(const std::string& msg);
    void debug(const std::string& msg);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);
    void critical(const std::string& msg);

    template <typename... Args>
    void infof(const char* fmt, Args&&... args);

    template <typename... Args>
    void errorf(const char* fmt, Args&&... args);

    void set_level(Level min_level);

    [[nodiscard]] const std::string& name() const { return name_; }

private:
    std::string name_;
    void*       spdlog_ptr_ = nullptr;
};

void init_logging(const std::string& log_dir, bool also_stdout = true);
void shutdown_logging();

std::shared_ptr<Logger> get_logger(const std::string& name);
std::shared_ptr<Logger> root_logger();

}

#define GENESIS_LOG_INFO(msg)  genesis::logging::root_logger()->info(msg)
#define GENESIS_LOG_WARN(msg)  genesis::logging::root_logger()->warn(msg)
#define GENESIS_LOG_ERROR(msg) genesis::logging::root_logger()->error(msg)
#define GENESIS_LOG_DEBUG(msg) genesis::logging::root_logger()->debug(msg)
