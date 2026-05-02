#pragma once

#include "genesis/core/Result.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace genesis::jvm {

// Authoritative lifecycle state of a spawned JVM process. Mirrored into
// ui::state::InstanceRuntimeState by AsyncLauncher's watcher thread.
enum class ProcessState {
    Running,    // OS reports the process is alive
    Stopped,    // exited cleanly (exit_code == 0)
    Crashed,    // exited with non-zero code or died unexpectedly
    Detached,   // we lost the OS handle (e.g. PID disappeared mid-flight)
    Zombie      // terminate + kill both failed; needs manual cleanup
};

const char* process_state_label(ProcessState s);

// Cross-platform owning handle to a spawned OS process. Construction
// captures the live PID (and an OS-level handle on Windows) and the
// handle is the *single source of truth* for lifecycle state. UI state
// is derived from this handle, never the other way around.
//
// Threading: every public method is safe to call from any thread.
//   - wait()      blocks the calling thread until the process exits
//   - terminate() / kill() can be called concurrently with wait()
//   - is_running() is non-blocking and cheap
class ProcessHandle {
public:
    using TransitionFn = std::function<void(ProcessState prev,
                                            ProcessState next,
                                            int32_t exit_code)>;

    ProcessHandle();
    ~ProcessHandle();

    ProcessHandle(const ProcessHandle&)            = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;

    // Adopt an OS-spawned process. After this call, the handle owns the
    // OS resource (closed in dtor on Windows; PID released on POSIX).
    void adopt(int64_t pid, void* os_handle, std::string instance_id);

    // Lifecycle queries
    [[nodiscard]] int64_t       pid()                const { return pid_; }
    [[nodiscard]] std::string   instance_id()        const;
    [[nodiscard]] int64_t       launch_timestamp_us() const { return launch_us_; }
    [[nodiscard]] ProcessState  state()              const { return state_.load(); }
    [[nodiscard]] int32_t       exit_code()          const { return exit_code_.load(); }
    [[nodiscard]] bool          is_running();

    // Subscribe to OS-confirmed transitions. Replaces any previous channel.
    void on_transition(TransitionFn fn);

    // Block until the process exits. Returns the exit code. Idempotent —
    // subsequent calls return the cached exit code immediately.
    core::Result<int32_t> wait();

    // Polite termination (SIGTERM / TerminateProcess(0)). Returns ok even
    // if the OS reports "process already gone".
    core::Result<void> terminate();

    // Forceful kill (SIGKILL / TerminateProcess(1)). Same liveness rules.
    core::Result<void> kill();

    // Force the handle into Detached state (used when monitor confirms
    // the PID is no longer valid but we never observed an exit).
    void mark_detached(const std::string& reason);

    // Force the handle into Zombie state (used when terminate+kill both
    // fail; UI must surface this).
    void mark_zombie(const std::string& reason);

private:
    // Internal: record the new state, emit channel notification.
    void transition_to_(ProcessState next, int32_t exit_code);

    int64_t                   pid_         = 0;
    void*                     os_handle_   = nullptr;   // HANDLE on Windows
    int64_t                   launch_us_   = 0;
    std::atomic<ProcessState> state_       {ProcessState::Running};
    std::atomic<int32_t>      exit_code_   {-1};
    std::atomic<bool>         waited_      {false};

    // One-time gate so only the first wait() performs the blocking OS
    // call; concurrent waiters block on wait_cv_ until reaped.
    std::atomic<bool>         wait_in_progress_ {false};
    std::mutex                wait_mu_;
    std::condition_variable   wait_cv_;

    mutable std::mutex        mu_;
    std::string               instance_id_;
    TransitionFn              on_transition_;
};

}
