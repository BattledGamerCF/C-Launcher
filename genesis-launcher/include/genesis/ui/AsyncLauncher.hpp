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

    // Per-instance OS-level handle ownership. 1:1 with UiState.instances.
    mutable std::mutex                                                       handles_mu_;
    std::unordered_map<std::string, std::shared_ptr<jvm::ProcessHandle>>     handles_;
    // Watcher threads that block on handle->wait() and translate exit
    // events back into UiState. Joined at shutdown.
    std::vector<std::unique_ptr<std::thread>>                                watchers_;
};

}
