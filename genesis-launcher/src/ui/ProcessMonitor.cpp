#include "genesis/ui/ProcessMonitor.hpp"
#include "genesis/ui/UiState.hpp"
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdio>

#if defined(__linux__)
#   include <unistd.h>
#elif defined(__APPLE__)
#   include <unistd.h>
#   include <mach/mach.h>
#elif defined(_WIN32)
#   include <windows.h>
#   include <psapi.h>
#endif

namespace genesis::ui::monitor {

static int64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

#if defined(__linux__)

// Linux /proc/<pid>/stat fields we care about (utime=14, stime=15, 1-indexed).
// Returns CPU time in clock ticks combined for the process.
static bool read_proc_stat_cpu(int64_t pid, int64_t& utime_ticks, int64_t& stime_ticks) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%lld/stat", (long long)pid);
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    if (!std::getline(f, line)) return false;
    // The 2nd field is "(comm)" which may contain spaces — strip it.
    auto rp = line.find_last_of(')');
    if (rp == std::string::npos) return false;
    std::istringstream iss(line.substr(rp + 1));
    std::string tok;
    int field = 2; // we start past 'pid (comm)' which is fields 1 and 2
    int64_t utime = 0, stime = 0;
    while (iss >> tok) {
        ++field;
        if (field == 14) { utime = std::stoll(tok); }
        if (field == 15) { stime = std::stoll(tok); break; }
    }
    utime_ticks = utime;
    stime_ticks = stime;
    return true;
}

static bool read_proc_status_rss_kb(int64_t pid, int64_t& rss_kb) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%lld/status", (long long)pid);
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            std::istringstream iss(line.substr(6));
            iss >> rss_kb;
            return true;
        }
    }
    return false;
}

ProcessSample sample_process(int64_t pid) {
    ProcessSample s;
    s.timestamp_us = now_us();
    int64_t rss_kb = 0;
    if (!read_proc_status_rss_kb(pid, rss_kb)) return s;
    int64_t utime = 0, stime = 0;
    if (!read_proc_stat_cpu(pid, utime, stime)) return s;
    s.ram_mb = rss_kb / 1024.0f;
    // CPU% requires deltas — caller handles. We pack total CPU time as
    // microseconds-equivalent in cpu_pct so the monitor can diff.
    long ticks_per_sec = sysconf(_SC_CLK_TCK);
    if (ticks_per_sec <= 0) ticks_per_sec = 100;
    double total_sec = double(utime + stime) / double(ticks_per_sec);
    s.cpu_pct = static_cast<float>(total_sec * 1'000'000.0); // store as us
    s.valid = true;
    return s;
}

#elif defined(__APPLE__)

ProcessSample sample_process(int64_t pid) {
    ProcessSample s;
    s.timestamp_us = now_us();
    // Sampling other processes on macOS requires privileges via task_for_pid;
    // we leave this as a stub that returns "no data" rather than fake numbers.
    (void)pid;
    return s;
}

#elif defined(_WIN32)

ProcessSample sample_process(int64_t pid) {
    ProcessSample s;
    s.timestamp_us = now_us();
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                           FALSE, static_cast<DWORD>(pid));
    if (!h) return s;
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
        s.ram_mb = static_cast<float>(pmc.WorkingSetSize / (1024.0 * 1024.0));
    }
    FILETIME create_ft, exit_ft, kernel_ft, user_ft;
    if (GetProcessTimes(h, &create_ft, &exit_ft, &kernel_ft, &user_ft)) {
        ULARGE_INTEGER k{}, u{};
        k.LowPart  = kernel_ft.dwLowDateTime;
        k.HighPart = kernel_ft.dwHighDateTime;
        u.LowPart  = user_ft.dwLowDateTime;
        u.HighPart = user_ft.dwHighDateTime;
        // 100-ns intervals → microseconds
        double total_us = double(k.QuadPart + u.QuadPart) / 10.0;
        s.cpu_pct = static_cast<float>(total_us);
    }
    CloseHandle(h);
    s.valid = true;
    return s;
}

#else

ProcessSample sample_process(int64_t pid) {
    ProcessSample s;
    s.timestamp_us = now_us();
    (void)pid;
    return s;
}

#endif

// ─── Monitor ─────────────────────────────────────────────────────────────────
ProcessMonitor& ProcessMonitor::global() {
    static ProcessMonitor m;
    return m;
}

void ProcessMonitor::track(const std::string& instance_id, int64_t pid) {
    std::lock_guard<std::mutex> l(mu_);
    for (auto& t : tracked_) {
        if (t.instance_id == instance_id) { t.pid = pid; return; }
    }
    Tracked t;
    t.instance_id = instance_id;
    t.pid         = pid;
    tracked_.push_back(t);
}

void ProcessMonitor::untrack(const std::string& instance_id) {
    std::lock_guard<std::mutex> l(mu_);
    for (auto it = tracked_.begin(); it != tracked_.end(); ++it) {
        if (it->instance_id == instance_id) { tracked_.erase(it); return; }
    }
}

void ProcessMonitor::poll() {
    int64_t t = now_us();
    if (t - last_poll_us_ < POLL_INTERVAL_US) return;
    last_poll_us_ = t;

    std::vector<Tracked> snap;
    {
        std::lock_guard<std::mutex> l(mu_);
        snap = tracked_;
    }
    for (auto& tr : snap) {
        ProcessSample s = sample_process(tr.pid);
        if (!s.valid) continue;
        // Compute CPU% from delta of stored cpu_us total vs prior sample.
        float cpu_pct = 0.0f;
        if (tr.last_sample_us > 0) {
            double wall_us = double(s.timestamp_us - tr.last_sample_us);
            double cpu_us  = double(s.cpu_pct) - double(tr.last_cpu_us);
            if (wall_us > 0.0)
                cpu_pct = static_cast<float>(100.0 * cpu_us / wall_us);
        }
        // Update tracker bookkeeping
        {
            std::lock_guard<std::mutex> l(mu_);
            for (auto& real : tracked_) {
                if (real.instance_id == tr.instance_id) {
                    real.last_cpu_us    = static_cast<int64_t>(s.cpu_pct);
                    real.last_sample_us = s.timestamp_us;
                    break;
                }
            }
        }
        if (cpu_pct < 0.0f)   cpu_pct = 0.0f;
        if (cpu_pct > 800.0f) cpu_pct = 800.0f;

        state::dispatch::push_instance_sample(tr.instance_id, s.ram_mb, cpu_pct);
    }
}

void ProcessMonitor::shutdown() {
    std::lock_guard<std::mutex> l(mu_);
    tracked_.clear();
}

}
