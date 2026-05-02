#include "genesis/jvm/ProcessHandle.hpp"
#include "genesis/logging/Logger.hpp"
#include <chrono>

#ifdef GENESIS_PLATFORM_WINDOWS
#   include <windows.h>
#else
#   include <signal.h>
#   include <sys/types.h>
#   include <sys/wait.h>
#   include <errno.h>
#endif

namespace genesis::jvm {

using Error = core::Error;
template<typename T> using Result = core::Result<T>;

static auto log = logging::get_logger("ProcessHandle");

const char* process_state_label(ProcessState s) {
    switch (s) {
        case ProcessState::Running:  return "running";
        case ProcessState::Stopped:  return "stopped";
        case ProcessState::Crashed:  return "crashed";
        case ProcessState::Detached: return "detached";
        case ProcessState::Zombie:   return "zombie";
    }
    return "?";
}

static int64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

ProcessHandle::ProcessHandle() = default;

ProcessHandle::~ProcessHandle() {
#ifdef GENESIS_PLATFORM_WINDOWS
    if (os_handle_) {
        ::CloseHandle(reinterpret_cast<HANDLE>(os_handle_));
        os_handle_ = nullptr;
    }
#endif
}

void ProcessHandle::adopt(int64_t pid, void* os_handle, std::string instance_id) {
    std::lock_guard<std::mutex> l(mu_);
    pid_         = pid;
    os_handle_   = os_handle;
    instance_id_ = std::move(instance_id);
    launch_us_   = now_us();
    state_.store(ProcessState::Running);
    exit_code_.store(-1);
    waited_.store(false);
}

std::string ProcessHandle::instance_id() const {
    std::lock_guard<std::mutex> l(mu_);
    return instance_id_;
}

void ProcessHandle::on_transition(TransitionFn fn) {
    std::lock_guard<std::mutex> l(mu_);
    on_transition_ = std::move(fn);
}

void ProcessHandle::transition_to_(ProcessState next, int32_t exit_code) {
    ProcessState prev = state_.exchange(next);
    if (exit_code >= 0) exit_code_.store(exit_code);
    if (prev == next) return;
    TransitionFn fn;
    {
        std::lock_guard<std::mutex> l(mu_);
        fn = on_transition_;
    }
    if (fn) {
        try { fn(prev, next, exit_code_.load()); }
        catch (...) { /* never let watcher callbacks crash the worker */ }
    }
    log->info(std::string("[handle pid=") + std::to_string(pid_) +
              " inst=" + instance_id() + "] " +
              process_state_label(prev) + " -> " +
              process_state_label(next) +
              " (exit=" + std::to_string(exit_code_.load()) + ")");
}

bool ProcessHandle::is_running() {
    if (pid_ == 0) return false;
    auto cur = state_.load();
    if (cur != ProcessState::Running) return false;
#ifdef GENESIS_PLATFORM_WINDOWS
    if (!os_handle_) return false;
    DWORD code = 0;
    if (!::GetExitCodeProcess(reinterpret_cast<HANDLE>(os_handle_), &code))
        return false;
    return code == STILL_ACTIVE;
#else
    int r = ::kill(static_cast<pid_t>(pid_), 0);
    if (r == 0) return true;
    return errno != ESRCH;   // EPERM still implies the PID is taken
#endif
}

Result<int32_t> ProcessHandle::wait() {
    if (waited_.load()) return Result<int32_t>::ok(exit_code_.load());
    if (pid_ == 0)
        return Result<int32_t>::err(Error::make(Error::Code::ProcessError,
                                                 "wait() on null handle"));

    // One-time gate: exactly one thread enters the OS wait; everyone else
    // blocks on wait_cv_ until that thread reaps and notifies. This makes
    // wait() safe to call concurrently from the watcher thread and the
    // stop worker without risking a duplicate waitpid()/ECHILD sequence
    // that would corrupt the final transition.
    bool expected = false;
    if (!wait_in_progress_.compare_exchange_strong(expected, true)) {
        std::unique_lock<std::mutex> lk(wait_mu_);
        wait_cv_.wait(lk, [this]{ return waited_.load(); });
        return Result<int32_t>::ok(exit_code_.load());
    }

#ifdef GENESIS_PLATFORM_WINDOWS
    if (!os_handle_)
        return Result<int32_t>::err(Error::make(Error::Code::ProcessError,
                                                 "wait() with no OS handle"));
    HANDLE h = reinterpret_cast<HANDLE>(os_handle_);
    DWORD wr = ::WaitForSingleObject(h, INFINITE);
    if (wr == WAIT_FAILED) {
        waited_.store(true);
        { std::lock_guard<std::mutex> lk(wait_mu_); }
        wait_cv_.notify_all();
        transition_to_(ProcessState::Detached, -1);
        return Result<int32_t>::err(Error::make(Error::Code::ProcessError,
                                                 "WaitForSingleObject failed"));
    }
    DWORD code = 0;
    ::GetExitCodeProcess(h, &code);
    int32_t exit = static_cast<int32_t>(code);
    waited_.store(true);
    { std::lock_guard<std::mutex> lk(wait_mu_); }
    wait_cv_.notify_all();
    transition_to_(exit == 0 ? ProcessState::Stopped : ProcessState::Crashed, exit);
    return Result<int32_t>::ok(exit);
#else
    int status = 0;
    pid_t r = 0;
    do { r = ::waitpid(static_cast<pid_t>(pid_), &status, 0); }
    while (r == -1 && errno == EINTR);
    if (r == -1) {
        // ECHILD ⇒ already reaped or never our child. Treat as detached
        // rather than as a hard error so the UI can recover.
        waited_.store(true);
        { std::lock_guard<std::mutex> lk(wait_mu_); }
        wait_cv_.notify_all();
        transition_to_(ProcessState::Detached, -1);
        return Result<int32_t>::err(Error::make(Error::Code::ProcessError,
                                                 "waitpid() failed",
                                                 std::to_string(errno)));
    }
    int32_t exit = -1;
    if (WIFEXITED(status))         exit = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))  exit = 128 + WTERMSIG(status);
    waited_.store(true);
    { std::lock_guard<std::mutex> lk(wait_mu_); }
    wait_cv_.notify_all();
    transition_to_(exit == 0 ? ProcessState::Stopped : ProcessState::Crashed, exit);
    return Result<int32_t>::ok(exit);
#endif
}

Result<void> ProcessHandle::terminate() {
    if (pid_ == 0) return Result<void>::ok();
    if (!is_running()) return Result<void>::ok();
#ifdef GENESIS_PLATFORM_WINDOWS
    HANDLE h = reinterpret_cast<HANDLE>(os_handle_);
    if (!h) return Result<void>::err(Error::make(Error::Code::ProcessError,
                                                  "terminate() with no OS handle"));
    if (!::TerminateProcess(h, 0)) {
        DWORD e = ::GetLastError();
        if (e == ERROR_ACCESS_DENIED || e == ERROR_INVALID_HANDLE)
            return Result<void>::err(Error::make(Error::Code::ProcessError,
                "TerminateProcess failed", std::to_string(e)));
        // Otherwise the process was likely already gone.
    }
    return Result<void>::ok();
#else
    // Signal the entire process group (we made the child a session leader
    // at spawn time, so PGID == PID). This takes down any JVM-spawned
    // subprocesses, preventing orphans. Falls back to per-PID signal if
    // the group doesn't exist for any reason.
    pid_t pg = static_cast<pid_t>(pid_);
    if (::killpg(pg, SIGTERM) == 0) return Result<void>::ok();
    if (errno == ESRCH) {
        if (::kill(pg, SIGTERM) == 0) return Result<void>::ok();
        if (errno == ESRCH) return Result<void>::ok();
    }
    return Result<void>::err(Error::make(Error::Code::ProcessError,
        "killpg(SIGTERM) failed", std::to_string(errno)));
#endif
}

Result<void> ProcessHandle::kill() {
    if (pid_ == 0) return Result<void>::ok();
    if (!is_running()) return Result<void>::ok();
#ifdef GENESIS_PLATFORM_WINDOWS
    HANDLE h = reinterpret_cast<HANDLE>(os_handle_);
    if (!h) return Result<void>::err(Error::make(Error::Code::ProcessError,
                                                  "kill() with no OS handle"));
    if (!::TerminateProcess(h, 1)) {
        DWORD e = ::GetLastError();
        return Result<void>::err(Error::make(Error::Code::ProcessError,
            "TerminateProcess(force) failed", std::to_string(e)));
    }
    return Result<void>::ok();
#else
    pid_t pg = static_cast<pid_t>(pid_);
    if (::killpg(pg, SIGKILL) == 0) return Result<void>::ok();
    if (errno == ESRCH) {
        if (::kill(pg, SIGKILL) == 0) return Result<void>::ok();
        if (errno == ESRCH) return Result<void>::ok();
    }
    return Result<void>::err(Error::make(Error::Code::ProcessError,
        "killpg(SIGKILL) failed", std::to_string(errno)));
#endif
}

void ProcessHandle::mark_detached(const std::string& reason) {
    log->warn("Process detached: " + reason + " (pid=" + std::to_string(pid_) + ")");
    transition_to_(ProcessState::Detached, -1);
}

void ProcessHandle::mark_zombie(const std::string& reason) {
    log->error("Process zombie: " + reason + " (pid=" + std::to_string(pid_) + ")");
    transition_to_(ProcessState::Zombie, -1);
}

}
