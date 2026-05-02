#pragma once

#include "genesis/core/Result.hpp"
#include "genesis/logging/Logger.hpp"
#include <spdlog/sinks/base_sink.h>
#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <mutex>
#include <cstdint>

namespace genesis::ui::log {

struct LogEntry {
    int64_t          id;
    int64_t          timestamp_us;
    logging::Level   level;
    std::string      logger_name;
    std::string      message;
    std::string      correlation_id;   // optional, parsed from message tags
};

class LogStream {
public:
    static LogStream& global();

    void   push(LogEntry e);
    void   clear();

    // Snapshot all entries with id > since_id, up to `max`.
    void   snapshot(int64_t since_id,
                    std::vector<LogEntry>& out,
                    size_t max = 10000) const;

    // Snapshot, filtered by minimum level (0=all, see ConsoleState) and case-
    // insensitive substring search. Empty search returns all matching levels.
    void   snapshot_filtered(int64_t since_id,
                             int min_level,
                             std::string_view search,
                             std::vector<LogEntry>& out,
                             size_t max = 10000) const;

    [[nodiscard]] int64_t latest_id() const;
    [[nodiscard]] size_t  size()      const;

    void  counts(int64_t& info, int64_t& warn, int64_t& err) const;

    core::Result<void> export_to(const std::string& path) const;

    // Pause/resume only affects the auto-scroll snapshot frontier; the buffer
    // continues to accept entries so we never lose data.
    void               set_paused(bool paused);
    [[nodiscard]] bool paused() const;
    [[nodiscard]] int64_t pause_at_id() const;

    [[nodiscard]] std::optional<LogEntry> by_id(int64_t id) const;

private:
    LogStream() = default;

    static constexpr size_t MAX_ENTRIES = 100'000;

    mutable std::mutex   mu_;
    std::deque<LogEntry> buf_;
    int64_t              next_id_      = 1;
    int64_t              info_count_   = 0;
    int64_t              warn_count_   = 0;
    int64_t              err_count_    = 0;
    bool                 paused_       = false;
    int64_t              pause_at_id_  = 0;
};

// spdlog sink that pushes every record into LogStream::global().
class GenesisStreamSink : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override {}
};

// Install the streaming sink on the spdlog default logger (idempotent).
void install_into_root_logger();

}
