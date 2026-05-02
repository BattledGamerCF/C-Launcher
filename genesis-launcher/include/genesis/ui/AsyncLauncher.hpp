#pragma once

#include "genesis/core/Result.hpp"
#include "genesis/instance/Instance.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>

namespace genesis::core { class Launcher; }

namespace genesis::ui::async_ops {

// AsyncLauncher wraps Launcher's blocking calls in worker threads, and
// reports state through ui::state::dispatch::*. It satisfies the spec
// requirement that the UI never blocks: callers fire-and-forget; results
// land back in the central UI state.
class AsyncLauncher {
public:
    explicit AsyncLauncher(core::Launcher& launcher);
    ~AsyncLauncher();

    // High-level user actions
    void start_login();
    void start_logout();
    void start_launch(std::string instance_id);
    void start_stop(std::string instance_id);
    void start_create_instance(instance::InstanceConfig cfg);
    void start_install_modpack(std::string instance_id);
    void start_check_update();
    void start_export_logs(std::string dest_path);
    void start_take_snapshot(std::string dest_path);

    // Called once per frame on the main thread to reap finished workers.
    void poll();

    // Cancel & join all outstanding workers (used at shutdown).
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
    void run_create_worker(instance::InstanceConfig cfg);
    void run_install_modpack_worker(std::string instance_id);
    void run_check_update_worker();
    void run_export_logs_worker(std::string dest_path);
    void run_snapshot_worker(std::string dest_path);

    core::Launcher&                       launcher_;
    std::mutex                            mu_;
    std::vector<std::unique_ptr<Task>>    tasks_;
    std::atomic<bool>                     shutting_down_{false};
};

}
