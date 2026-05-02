#include "genesis/ui/LogStream.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/sink.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cctype>
#include <regex>

namespace genesis::ui::log {

// ─── LogStream ───────────────────────────────────────────────────────────────
LogStream& LogStream::global() {
    static LogStream s;
    return s;
}

void LogStream::push(LogEntry e) {
    std::lock_guard<std::mutex> l(mu_);
    e.id = next_id_++;
    if (buf_.size() >= MAX_ENTRIES) buf_.pop_front();
    switch (e.level) {
        case logging::Level::Info:     ++info_count_; break;
        case logging::Level::Warn:     ++warn_count_; break;
        case logging::Level::Error:
        case logging::Level::Critical: ++err_count_;  break;
        default: break;
    }
    buf_.push_back(std::move(e));
}

void LogStream::clear() {
    std::lock_guard<std::mutex> l(mu_);
    buf_.clear();
    info_count_ = warn_count_ = err_count_ = 0;
    pause_at_id_ = 0;
}

void LogStream::snapshot(int64_t since_id,
                         std::vector<LogEntry>& out,
                         size_t max) const {
    std::lock_guard<std::mutex> l(mu_);
    out.clear();
    out.reserve(std::min(buf_.size(), max));
    for (auto it = buf_.begin(); it != buf_.end(); ++it) {
        if (it->id <= since_id) continue;
        out.push_back(*it);
        if (out.size() >= max) break;
    }
}

static bool ic_contains(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    auto eq = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a))
            == std::tolower(static_cast<unsigned char>(b));
    };
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (!eq(hay[i + j], needle[j])) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

void LogStream::snapshot_filtered(int64_t since_id,
                                  int min_level,
                                  std::string_view search,
                                  std::vector<LogEntry>& out,
                                  size_t max) const {
    std::lock_guard<std::mutex> l(mu_);
    out.clear();

    auto level_ok = [min_level](logging::Level lv) {
        switch (min_level) {
            case 0: return true;
            case 1: return lv >= logging::Level::Info;
            case 2: return lv >= logging::Level::Warn;
            case 3: return lv >= logging::Level::Error;
            default: return true;
        }
    };

    for (auto it = buf_.begin(); it != buf_.end(); ++it) {
        if (it->id <= since_id) continue;
        if (!level_ok(it->level)) continue;
        if (!search.empty() &&
            !ic_contains(it->message, search) &&
            !ic_contains(it->logger_name, search))
            continue;
        out.push_back(*it);
        if (out.size() >= max) break;
    }
}

int64_t LogStream::latest_id() const {
    std::lock_guard<std::mutex> l(mu_);
    return next_id_ - 1;
}

size_t LogStream::size() const {
    std::lock_guard<std::mutex> l(mu_);
    return buf_.size();
}

void LogStream::counts(int64_t& info, int64_t& warn, int64_t& err) const {
    std::lock_guard<std::mutex> l(mu_);
    info = info_count_;
    warn = warn_count_;
    err  = err_count_;
}

core::Result<void> LogStream::export_to(const std::string& path) const {
    std::vector<LogEntry> snap;
    {
        std::lock_guard<std::mutex> l(mu_);
        snap.assign(buf_.begin(), buf_.end());
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        return core::Result<void>::err(core::Error::make(
            core::Error::Code::IoError,
            "Could not open log export file",
            path));
    }
    auto level_str = [](logging::Level lv) {
        switch (lv) {
            case logging::Level::Trace:    return "TRACE";
            case logging::Level::Debug:    return "DEBUG";
            case logging::Level::Info:     return "INFO ";
            case logging::Level::Warn:     return "WARN ";
            case logging::Level::Error:    return "ERROR";
            case logging::Level::Critical: return "CRIT ";
        }
        return "?    ";
    };
    for (auto& e : snap) {
        f << e.timestamp_us << '\t'
          << level_str(e.level) << '\t'
          << e.logger_name << '\t';
        if (!e.correlation_id.empty()) f << '[' << e.correlation_id << "] ";
        f << e.message << '\n';
    }
    return core::Result<void>::ok();
}

void LogStream::set_paused(bool p) {
    std::lock_guard<std::mutex> l(mu_);
    paused_ = p;
    if (p) pause_at_id_ = next_id_ - 1;
    else   pause_at_id_ = 0;
}

bool LogStream::paused() const {
    std::lock_guard<std::mutex> l(mu_);
    return paused_;
}

int64_t LogStream::pause_at_id() const {
    std::lock_guard<std::mutex> l(mu_);
    return pause_at_id_;
}

std::optional<LogEntry> LogStream::by_id(int64_t id) const {
    std::lock_guard<std::mutex> l(mu_);
    // Linear scan from the back — recent entries are usually queried.
    for (auto it = buf_.rbegin(); it != buf_.rend(); ++it) {
        if (it->id == id) return *it;
        if (it->id < id)  break;
    }
    return std::nullopt;
}

// ─── Sink ────────────────────────────────────────────────────────────────────
static logging::Level from_spdlog(spdlog::level::level_enum lv) {
    switch (lv) {
        case spdlog::level::trace:    return logging::Level::Trace;
        case spdlog::level::debug:    return logging::Level::Debug;
        case spdlog::level::info:     return logging::Level::Info;
        case spdlog::level::warn:     return logging::Level::Warn;
        case spdlog::level::err:      return logging::Level::Error;
        case spdlog::level::critical: return logging::Level::Critical;
        default:                      return logging::Level::Info;
    }
}

// Heuristic correlation-id parser: looks for "[cid:abcd1234]" prefix.
static std::string extract_correlation(const std::string& msg, std::string& cleaned) {
    static const std::regex re(R"(\[cid:([0-9a-fA-F]{4,32})\]\s*)");
    std::smatch m;
    if (std::regex_search(msg, m, re) && m.position(0) == 0) {
        cleaned = msg.substr(m.length(0));
        return m[1].str();
    }
    cleaned = msg;
    return {};
}

void GenesisStreamSink::sink_it_(const spdlog::details::log_msg& msg) {
    LogEntry e;
    e.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        msg.time.time_since_epoch()).count();
    e.level        = from_spdlog(msg.level);
    e.logger_name.assign(msg.logger_name.data(), msg.logger_name.size());
    std::string raw(msg.payload.data(), msg.payload.size());
    e.correlation_id = extract_correlation(raw, e.message);
    LogStream::global().push(std::move(e));
}

void install_into_root_logger() {
    auto root = spdlog::default_logger();
    if (!root) return;
    static std::shared_ptr<GenesisStreamSink> installed;
    if (installed) return;
    installed = std::make_shared<GenesisStreamSink>();
    installed->set_level(spdlog::level::trace);
    root->sinks().push_back(installed);
}

}
