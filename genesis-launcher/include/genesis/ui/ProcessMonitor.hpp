#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace genesis::ui::monitor {

struct ProcessSample {
    int64_t timestamp_us = 0;
    float   ram_mb       = 0.0f;
    float   cpu_pct      = 0.0f;
    bool    valid        = false;
};

// Best-effort cross-platform sampler. On platforms with no implementation,
// `valid` will be false and callers should treat the sample as missing data,
// not as zero usage.
ProcessSample sample_process(int64_t pid);

class ProcessMonitor {
public:
    static ProcessMonitor& global();

    // Begin tracking pid, associated with the given instance id.
    void track(const std::string& instance_id, int64_t pid);

    // Stop tracking the given instance.
    void untrack(const std::string& instance_id);

    // Sample every tracked process and dispatch the result into UiState.
    // Safe to call once per frame on the main thread.
    void poll();

    void shutdown();

private:
    struct Tracked {
        std::string instance_id;
        int64_t     pid;
        int64_t     last_cpu_us       = 0;   // platform-dependent jiffy/ticks
        int64_t     last_total_us     = 0;
        int64_t     last_sample_us    = 0;
    };

    std::mutex            mu_;
    std::vector<Tracked>  tracked_;
    int64_t               last_poll_us_ = 0;
    static constexpr int64_t POLL_INTERVAL_US = 1'000'000;   // 1s
};

}
