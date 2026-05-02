#pragma once

#include "genesis/core/Result.hpp"
#include "genesis/instance/Instance.hpp"
#include "genesis/jvm/ProcessHandle.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace genesis::core { class Launcher; }

namespace genesis::ui::async_ops {

// AsyncLauncher wraps Launcher's blocking calls in worker threads, owns
// the live jvm::ProcessHandle for every running instance, and reports
// state through ui::state::dispatch::*. The UI never blocks: callers
// fire-and-forget; results land in central UI state. Lifecycle truth
// flows handle → UiState only.
class AsyncLauncher {
public:
    explicit AsyncLauncher(core::Launcher& launcher);
    ~AsyncLauncher();

    void start_login();
    void start_logout();
    void start_launch(std::string instance_id);
    void start_stop(std::string instance_id);
    void start_create_instance(instance::InstanceConfig cfg);
    void start_install_modpack(std::string instance_id);
    void start_check_update();
    void start_export_logs(std::string dest_path);
    void start_take_snapshot(std::string dest_path);

    // Handle introspection (read-only).
    [[nodiscard]] std::shared_ptr<jvm::ProcessHandle>
        handle_for(const std::string& instance_id) const;
    [[nodiscard]] bool has_handle(const std::string& instance_id) const;

    void poll();
    void shutdown();

private:
    struct Task {
        std::string                op_id;
        std::unique_ptr<std::thread> thread;
        std::atomic<bool>          done{false};
    };

    void spawn(std::string op_id, std::string label, std::function<void()> body);
    void run_login_worker();
    void run_launch_worker(std::string instance_id);
    void run_stop_worker(std::string instance_id);
    void run_create_worker(instance::InstanceConfig cfg);
    void run_install_modpack_worker(std::string instance_id);
    void run_check_update_worker();
    void run_export_logs_worker(std::string dest_path);
    void run_snapshot_worker(std::string dest_path);

    void register_handle_(std::string instance_id,
                          std::shared_ptr<jvm::ProcessHandle> h);
    void unregister_handle_(const std::string& instance_id);

    core::Launcher&                       launcher_;
    std::mutex                            mu_;
    std::vector<std::unique_ptr<Task>>    tasks_;
    std::atomic<bool>                     shutting_down_{false};

    // ─────────────────────────────────────────────────────────────────────
    // Ownership invariants (do not weaken without re-deriving the proof):
    //   • At most ONE shared_ptr<jvm::ProcessHandle> exists per instance_id
    //     in handles_ at any moment. start_launch() bails out if one is
    //     present; the watcher thread is the sole code path that erases.
    //   • Exactly ONE watcher thread is spawned per launch and it is the
    //     SOLE caller of handle->wait() in normal flow. Stop workers may
    //     call wait() too, but ProcessHandle::wait() is gated so only the
    //     first OS reap actually runs.
    //   • UiState lifecycle is downstream of the handle's transition
    //     channel — no other code path may publish lifecycle states after
    //     register_handle_().
    //   • watchers_ entries are reaped in poll() once their `done` atomic
    //     flips, so the vector cannot grow unboundedly.
    // ─────────────────────────────────────────────────────────────────────
    struct Watcher {
        std::unique_ptr<std::thread> thread;
        std::atomic<bool>            done{false};
    };

    // Per-instance OS-level handle ownership. 1:1 with UiState.instances.
    mutable std::mutex                                                       handles_mu_;
    std::unordered_map<std::string, std::shared_ptr<jvm::ProcessHandle>>     handles_;
    std::vector<std::unique_ptr<Watcher>>                                    watchers_;
    // Synchronous in-flight set for stop ops. UiState.ops is eventually
    // consistent (queued reducer), so a rapid second click can race past
    // an ops-map check. This set is checked + mutated atomically under
    // handles_mu_ on the UI thread, giving us strict double-click safety.
    std::unordered_set<std::string>                                          inflight_stops_;
};

}
