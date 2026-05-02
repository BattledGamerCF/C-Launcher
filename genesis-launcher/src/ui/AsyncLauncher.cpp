#include "genesis/ui/AsyncLauncher.hpp"
#include "genesis/ui/UiState.hpp"
#include "genesis/ui/LogStream.hpp"
#include "genesis/ui/ProcessMonitor.hpp"
#include "genesis/core/Launcher.hpp"
#include "genesis/logging/Logger.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include <fstream>
#include <sstream>
#include <chrono>

namespace genesis::ui::async_ops {

using namespace genesis::core;
namespace ust = genesis::ui::state;

static auto log = logging::get_logger("AsyncLauncher");

// Map JVM ProcessState → UI runtime state.
static ust::InstanceRuntimeState to_ui(jvm::ProcessState s) {
    switch (s) {
        case jvm::ProcessState::Running:  return ust::InstanceRuntimeState::Running;
        case jvm::ProcessState::Stopped:  return ust::InstanceRuntimeState::Stopped;
        case jvm::ProcessState::Crashed:  return ust::InstanceRuntimeState::Crashed;
        case jvm::ProcessState::Detached: return ust::InstanceRuntimeState::Detached;
        case jvm::ProcessState::Zombie:   return ust::InstanceRuntimeState::Zombie;
    }
    return ust::InstanceRuntimeState::Stopped;
}

AsyncLauncher::AsyncLauncher(Launcher& l) : launcher_(l) {}
AsyncLauncher::~AsyncLauncher() { shutdown(); }

void AsyncLauncher::spawn(std::string op_id,
                          std::string label,
                          std::function<void()> body) {
    if (shutting_down_.load()) return;
    ust::dispatch::start_op(op_id, label);

    auto task = std::make_unique<Task>();
    task->op_id = op_id;
    Task* raw = task.get();

    task->thread = std::make_unique<std::thread>([this, raw, body = std::move(body)]() {
        try { body(); }
        catch (const std::exception& e) {
            ust::dispatch::fail_op(raw->op_id,
                ust::make_error("worker", "WORKER-001",
                    "Worker crashed", e.what(),
                    "Restart the launcher; capture logs from the Logs tab."));
        } catch (...) {
            ust::dispatch::fail_op(raw->op_id,
                ust::make_error("worker", "WORKER-002",
                    "Worker crashed", "unknown exception",
                    "Restart the launcher."));
        }
        raw->done.store(true);
    });

    {
        std::lock_guard<std::mutex> l(mu_);
        tasks_.push_back(std::move(task));
    }
}

void AsyncLauncher::poll() {
    {
        std::lock_guard<std::mutex> l(mu_);
        for (auto it = tasks_.begin(); it != tasks_.end();) {
            if ((*it)->done.load()) {
                if ((*it)->thread && (*it)->thread->joinable()) (*it)->thread->join();
                it = tasks_.erase(it);
            } else ++it;
        }
    }
    // Reap completed watcher threads so watchers_ does not grow
    // monotonically across launches.
    {
        std::lock_guard<std::mutex> l(handles_mu_);
        for (auto it = watchers_.begin(); it != watchers_.end();) {
            if ((*it)->done.load()) {
                if ((*it)->thread && (*it)->thread->joinable()) (*it)->thread->join();
                it = watchers_.erase(it);
            } else ++it;
        }
    }
}

void AsyncLauncher::shutdown() {
    shutting_down_.store(true);

    // Terminate every live handle so watcher threads can return.
    {
        std::lock_guard<std::mutex> l(handles_mu_);
        for (auto& [id, h] : handles_) {
            if (h && h->is_running()) {
                (void)h->terminate();
            }
        }
    }
    std::vector<std::unique_ptr<Watcher>> w_drain;
    {
        std::lock_guard<std::mutex> l(handles_mu_);
        w_drain.swap(watchers_);
    }
    for (auto& w : w_drain) {
        if (w && w->thread && w->thread->joinable()) w->thread->join();
    }

    std::vector<std::unique_ptr<Task>> drain;
    {
        std::lock_guard<std::mutex> l(mu_);
        drain.swap(tasks_);
    }
    for (auto& t : drain) {
        if (t->thread && t->thread->joinable()) t->thread->join();
    }
    monitor::ProcessMonitor::global().shutdown();
}

// ─── Handle registry (instance_id ↔ ProcessHandle, 1:1) ─────────────────────
std::shared_ptr<jvm::ProcessHandle>
AsyncLauncher::handle_for(const std::string& instance_id) const {
    std::lock_guard<std::mutex> l(handles_mu_);
    auto it = handles_.find(instance_id);
    return it == handles_.end() ? nullptr : it->second;
}

bool AsyncLauncher::has_handle(const std::string& instance_id) const {
    std::lock_guard<std::mutex> l(handles_mu_);
    return handles_.find(instance_id) != handles_.end();
}

void AsyncLauncher::register_handle_(std::string instance_id,
                                     std::shared_ptr<jvm::ProcessHandle> h) {
    std::lock_guard<std::mutex> l(handles_mu_);
    handles_[instance_id] = h;
}

void AsyncLauncher::unregister_handle_(const std::string& instance_id) {
    std::lock_guard<std::mutex> l(handles_mu_);
    handles_.erase(instance_id);
}

// ─── Login ───────────────────────────────────────────────────────────────────
void AsyncLauncher::start_login() {
    spawn("auth:login", "Sign in with Microsoft",
          [this]() { run_login_worker(); });
}

void AsyncLauncher::run_login_worker() {
    log->info("Starting Microsoft device-code login flow");
    ust::dispatch::set_auth_op([](){
        ust::AsyncOp op;
        op.id = "auth:login";
        op.label = "Sign in with Microsoft";
        op.status = ust::AsyncStatus::Pending;
        op.started_us = ust::now_us();
        return op;
    }());

    auto res = launcher_.auth_manager().login(
        [](const auth::DeviceCodeInfo& info) {
            ust::dispatch::set_device_code(info);
            platform::open_url_in_browser(info.verification_uri);
            platform::copy_to_clipboard(info.user_code);
        });

    ust::dispatch::clear_device_code();

    if (res.is_err()) {
        auto err = ust::make_error("auth", "AUTH-001",
            "Microsoft sign-in failed",
            res.error().full(),
            "Check your network, then click 'Sign in' again.");
        ust::dispatch::fail_op("auth:login", err);
        ust::dispatch::push_toast("Sign-in failed: " + res.error().message, true);
        return;
    }

    auto& cred = res.value();
    ust::dispatch::set_authenticated(true,
        cred.profile.username, cred.profile.uuid);
    ust::dispatch::complete_op("auth:login");
    ust::dispatch::push_toast("Signed in as " + cred.profile.username, false);
}

void AsyncLauncher::start_logout() {
    spawn("auth:logout", "Sign out", [this]() {
        auto res = launcher_.auth_manager().logout();
        if (res.is_err()) {
            ust::dispatch::fail_op("auth:logout",
                ust::make_error("auth", "AUTH-002",
                    "Sign-out failed", res.error().full(),
                    "Try again or restart the launcher."));
            return;
        }
        ust::dispatch::set_authenticated(false, "", "");
        ust::dispatch::complete_op("auth:logout");
        ust::dispatch::push_toast("Signed out", false);
    });
}

// ─── Launch ──────────────────────────────────────────────────────────────────
void AsyncLauncher::start_launch(std::string instance_id) {
    if (has_handle(instance_id)) {
        ust::dispatch::push_toast("Already running: " + instance_id, true);
        return;
    }
    spawn("launch:" + instance_id, "Launch " + instance_id,
          [this, instance_id]() { run_launch_worker(instance_id); });
}

void AsyncLauncher::run_launch_worker(std::string instance_id) {
    const std::string corr = ust::new_correlation_id();
    log->info("[corr=" + corr + "] Launch requested for " + instance_id);

    ust::dispatch::set_instance_state(instance_id, ust::InstanceRuntimeState::Syncing);
    ust::dispatch::record_instance_lifecycle(
        instance_id, ust::InstanceRuntimeState::Stopped,
        ust::InstanceRuntimeState::Syncing, 0, corr, "launch requested");
    ust::dispatch::update_op("launch:" + instance_id, 0.05f, "Resolving instance");

    auto inst_opt = launcher_.instance_manager().find(instance_id);
    if (!inst_opt) {
        auto err = ust::make_error("instance", "INST-404",
            "Instance not found", instance_id,
            "Refresh the instance list or recreate the instance.");
        ust::dispatch::fail_op("launch:" + instance_id, err);
        ust::dispatch::set_instance_state(instance_id, ust::InstanceRuntimeState::Stopped);
        return;
    }
    auto& inst = inst_opt->get();

    ust::dispatch::update_op("launch:" + instance_id, 0.20f, "Preparing launch");
    ust::dispatch::set_instance_state(instance_id, ust::InstanceRuntimeState::Starting);
    ust::dispatch::record_instance_lifecycle(
        instance_id, ust::InstanceRuntimeState::Syncing,
        ust::InstanceRuntimeState::Starting, 0, corr, "preparing");

    LaunchRequest req;
    req.instance_id = instance_id;
    req.version_id  = inst.config().game_version;

    auto res = launcher_.launch(std::move(req));

    if (res.is_err()) {
        auto err = ust::make_error("jvm", "JVM-001",
            "Launch failed", res.error().full(),
            "Open the Logs tab to inspect the JVM stderr.");
        ust::dispatch::fail_op("launch:" + instance_id, err);
        ust::dispatch::set_instance_state(instance_id, ust::InstanceRuntimeState::Crashed);
        ust::dispatch::record_instance_lifecycle(
            instance_id, ust::InstanceRuntimeState::Starting,
            ust::InstanceRuntimeState::Crashed, 0, corr, "spawn failed");
        return;
    }

    auto handle = std::move(res.value());
    const int64_t pid = handle->pid();

    // Subscribe to OS-confirmed transitions BEFORE we publish state.
    // The handle's transition channel is the single authoritative
    // bridge into UiState — no other code path may set instance state.
    handle->on_transition([instance_id, corr, pid](jvm::ProcessState prev,
                                                    jvm::ProcessState next,
                                                    int32_t exit_code) {
        ust::dispatch::set_instance_state(instance_id, to_ui(next));
        ust::dispatch::record_instance_lifecycle(
            instance_id, to_ui(prev), to_ui(next),
            pid, corr,
            std::string("OS: ") + jvm::process_state_label(prev) + "→" +
                jvm::process_state_label(next) +
                " (exit=" + std::to_string(exit_code) + ")");
    });

    register_handle_(instance_id, handle);

    // Publish authoritative running state and PID into UiState atomically.
    ust::dispatch::set_instance_pid(instance_id, pid);
    ust::dispatch::set_instance_has_handle(instance_id, true);
    ust::dispatch::set_instance_state(instance_id, ust::InstanceRuntimeState::Running);
    ust::dispatch::record_instance_lifecycle(
        instance_id, ust::InstanceRuntimeState::Starting,
        ust::InstanceRuntimeState::Running, pid, corr,
        "spawned pid=" + std::to_string(pid));
    ust::dispatch::update_op("launch:" + instance_id, 0.95f, "Running");

    // Track via handle so the monitor can OS-verify liveness before
    // declaring detachment (defends against stub samplers, e.g. macOS).
    monitor::ProcessMonitor::global().track(instance_id, handle);

    // Watcher thread: blocks on wait(), then publishes exit + cleans up.
    // ProcessHandle::wait()'s one-time gate guarantees this is the only
    // path that performs the OS reap, even if the stop worker also calls
    // wait() concurrently.
    auto watcher = std::make_unique<Watcher>();
    Watcher* raw_w = watcher.get();
    watcher->thread = std::make_unique<std::thread>(
        [this, raw_w, handle, instance_id, corr, pid]() {
        auto wait_res = handle->wait();
        int32_t exit_code = wait_res.is_ok() ? wait_res.value()
                                              : handle->exit_code();
        log->info("[corr=" + corr + "] Watcher exit pid=" + std::to_string(pid) +
                  " code=" + std::to_string(exit_code));

        // Untrack BEFORE unregister so the monitor cannot query a
        // freed handle on a (potentially reused) PID.
        monitor::ProcessMonitor::global().untrack(instance_id);

        std::string crash_reason;
        if (exit_code != 0) crash_reason = "Exited with code " + std::to_string(exit_code);
        ust::dispatch::set_instance_exit(instance_id, exit_code, crash_reason);
        ust::dispatch::set_instance_has_handle(instance_id, false);
        ust::dispatch::complete_op("launch:" + instance_id);

        unregister_handle_(instance_id);
        raw_w->done.store(true);   // poll() will join + erase
    });

    {
        std::lock_guard<std::mutex> l(handles_mu_);
        watchers_.push_back(std::move(watcher));
    }
}

// ─── Stop ────────────────────────────────────────────────────────────────────
void AsyncLauncher::start_stop(std::string instance_id) {
    if (!has_handle(instance_id)) {
        // No real process — treat Stop as Detach (clear UI placeholder).
        ust::dispatch::set_instance_state(instance_id, ust::InstanceRuntimeState::Stopped);
        ust::dispatch::set_instance_has_handle(instance_id, false);
        ust::dispatch::push_toast("No live process; cleared placeholder", false);
        return;
    }
    // Strict double-click guard. UiState.ops is eventually consistent
    // (reducer queue), so checking it would leave a race window between
    // the click and the queued op materializing. Use a synchronous set
    // protected by handles_mu_ instead — claim or bail in one atomic step.
    {
        std::lock_guard<std::mutex> l(handles_mu_);
        if (!inflight_stops_.insert(instance_id).second) {
            ust::dispatch::push_toast("Stop already in progress", false);
            return;
        }
    }
    spawn("stop:" + instance_id, "Stop " + instance_id,
          [this, instance_id]() { run_stop_worker(instance_id); });
}

void AsyncLauncher::run_stop_worker(std::string instance_id) {
    // Always release the in-flight claim regardless of how this exits
    // (success, error, or exception), so the user can retry Stop after
    // a transient failure. The shared_ptr-with-deleter trick gives us
    // RAII cleanup from within a member function (a local struct here
    // would not have access to private members).
    auto release_inflight = std::shared_ptr<void>(
        reinterpret_cast<void*>(0x1),
        [this, instance_id](void*) {
            std::lock_guard<std::mutex> l(handles_mu_);
            inflight_stops_.erase(instance_id);
        });

    auto handle = handle_for(instance_id);
    if (!handle) {
        ust::dispatch::complete_op("stop:" + instance_id);
        return;
    }
    const std::string corr = ust::new_correlation_id();
    const int64_t pid = handle->pid();

    ust::dispatch::set_instance_state(instance_id, ust::InstanceRuntimeState::Stopping);
    ust::dispatch::record_instance_lifecycle(
        instance_id, ust::InstanceRuntimeState::Running,
        ust::InstanceRuntimeState::Stopping, pid, corr, "stop requested");
    ust::dispatch::update_op("stop:" + instance_id, 0.25f, "Sending SIGTERM");

    auto term_res = handle->terminate();
    if (term_res.is_err()) {
        log->warn("[corr=" + corr + "] terminate() failed pid=" + std::to_string(pid) +
                  ": " + term_res.error().full() + "; retrying with kill()");
    }

    // Give the JVM a brief grace window to exit cleanly.
    for (int i = 0; i < 30; ++i) {
        if (!handle->is_running()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (handle->is_running()) {
        ust::dispatch::update_op("stop:" + instance_id, 0.65f, "Force kill");
        auto kill_res = handle->kill();
        if (kill_res.is_err()) {
            log->error("[corr=" + corr + "] kill() failed pid=" + std::to_string(pid) +
                       ": " + kill_res.error().full());
            handle->mark_zombie("terminate+kill both failed");
            ust::dispatch::set_instance_state(instance_id, ust::InstanceRuntimeState::Zombie);
            ust::dispatch::record_instance_lifecycle(
                instance_id, ust::InstanceRuntimeState::Stopping,
                ust::InstanceRuntimeState::Zombie, pid, corr,
                "terminate+kill failed: " + kill_res.error().message);
            ust::dispatch::fail_op("stop:" + instance_id,
                ust::make_error("jvm", "JVM-ZOMBIE",
                    "Could not stop instance",
                    kill_res.error().full(),
                    "Open Task Manager / 'kill -9' the PID manually."));
            return;
        }
        // Wait briefly for kill to take effect.
        for (int i = 0; i < 20; ++i) {
            if (!handle->is_running()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // Watcher thread will publish the actual exit + clean handle. We only
    // confirm the operation here.
    ust::dispatch::update_op("stop:" + instance_id, 1.0f, "Stopped");
    ust::dispatch::complete_op("stop:" + instance_id);
}

// ─── Create instance ─────────────────────────────────────────────────────────
void AsyncLauncher::start_create_instance(instance::InstanceConfig cfg) {
    spawn("create:" + cfg.id, "Create " + cfg.id,
          [this, cfg]() mutable { run_create_worker(std::move(cfg)); });
}

void AsyncLauncher::run_create_worker(instance::InstanceConfig cfg) {
    std::string id = cfg.id;
    log->info("Create instance: " + id + " (" + cfg.game_version + ")");

    auto res = launcher_.create_instance_with_performance_pack(
        std::move(cfg),
        [id](const std::string& mod_name, float frac) {
            ust::dispatch::set_modpack_progress(id, frac, "Installing " + mod_name);
            ust::dispatch::update_op("create:" + id, 0.3f + frac * 0.6f,
                                     "Installing " + mod_name);
        });

    if (res.is_err()) {
        auto err = ust::make_error("instance", "INST-CREATE",
            "Could not create instance", res.error().full(),
            "Pick a different name or check disk space.");
        ust::dispatch::fail_op("create:" + id, err);
        return;
    }

    ust::dispatch::complete_op("create:" + id);
    ust::dispatch::push_toast("Instance '" + id + "' created", false);
}

// ─── Modpack reinstall ───────────────────────────────────────────────────────
void AsyncLauncher::start_install_modpack(std::string instance_id) {
    spawn("modpack:" + instance_id, "Install modpack for " + instance_id,
          [this, instance_id]() { run_install_modpack_worker(instance_id); });
}

void AsyncLauncher::run_install_modpack_worker(std::string instance_id) {
    auto inst_opt = launcher_.instance_manager().find(instance_id);
    if (!inst_opt) {
        ust::dispatch::fail_op("modpack:" + instance_id,
            ust::make_error("instance", "INST-404",
                "Instance not found", instance_id, ""));
        return;
    }
    auto& inst = inst_opt->get();
    std::string ver = inst.config().game_version;

    ust::dispatch::set_modpack_install_status(instance_id, ust::AsyncStatus::Pending);

    auto res = launcher_.performance_pack().install_for(inst, ver,
        [instance_id](const std::string& mod_name, float frac) {
            ust::dispatch::set_modpack_progress(instance_id, frac, "Installing " + mod_name);
            ust::dispatch::update_op("modpack:" + instance_id, frac, "Installing " + mod_name);
        });

    if (res.is_err()) {
        auto err = ust::make_error("modpack", "MOD-001",
            "Modpack install failed", res.error().full(),
            "Check Modrinth availability for " + ver + ".");
        ust::dispatch::fail_op("modpack:" + instance_id, err);
        ust::dispatch::set_modpack_install_status(instance_id, ust::AsyncStatus::Error);
        return;
    }

    auto& sum = res.value();
    ust::dispatch::set_modpack_install_result(instance_id,
        sum.installed, sum.skipped, sum.failed, sum.fabric_loader_version);
    ust::dispatch::complete_op("modpack:" + instance_id);
    ust::dispatch::push_toast(
        "Modpack ready: " + std::to_string(sum.installed.size()) + " installed", false);
}

// ─── Update check ────────────────────────────────────────────────────────────
void AsyncLauncher::start_check_update() {
    spawn("update:check", "Check for updates", [this]() { run_check_update_worker(); });
}

void AsyncLauncher::run_check_update_worker() {
    auto res = launcher_.updater().check_for_update();
    if (res.is_err()) {
        ust::dispatch::fail_op("update:check",
            ust::make_error("update", "UPD-001",
                "Update check failed", res.error().full(),
                "Verify your network connection."));
        return;
    }
    if (res.value().has_value()) {
        ust::dispatch::set_update_available(true, res.value()->version);
        ust::dispatch::push_toast("Update available: " + res.value()->version, false);
    } else {
        ust::dispatch::set_update_available(false, "");
    }
    ust::dispatch::complete_op("update:check");
}

// ─── Logs export ─────────────────────────────────────────────────────────────
void AsyncLauncher::start_export_logs(std::string dest_path) {
    spawn("logs:export", "Export logs to " + dest_path,
          [this, dest_path]() { run_export_logs_worker(dest_path); });
}

void AsyncLauncher::run_export_logs_worker(std::string dest_path) {
    auto res = log::LogStream::global().export_to(dest_path);
    if (res.is_err()) {
        ust::dispatch::fail_op("logs:export",
            ust::make_error("logs", "LOG-001",
                "Could not export logs", res.error().full(), ""));
        return;
    }
    ust::dispatch::complete_op("logs:export");
    ust::dispatch::push_toast("Logs exported to " + dest_path, false);
}

// ─── Diagnostics snapshot ────────────────────────────────────────────────────
void AsyncLauncher::start_take_snapshot(std::string dest_path) {
    spawn("snapshot:take", "Capture diagnostics snapshot",
          [this, dest_path]() { run_snapshot_worker(dest_path); });
}

void AsyncLauncher::run_snapshot_worker(std::string dest_path) {
    std::ostringstream oss;
    oss << "{\n  \"snapshot_taken_us\": " << ust::now_us() << ",\n";

    oss << "  \"instances\": [\n";
    auto& s = ust::global();
    bool first = true;
    for (auto& [id, live] : s.instances) {
        if (!first) oss << ",\n";
        first = false;
        oss << "    { \"id\": \"" << id
            << "\", \"state\": \"" << ust::runtime_label(live.state)
            << "\", \"pid\": " << live.pid
            << ", \"has_handle\": " << (live.has_handle ? "true" : "false")
            << ", \"last_heartbeat_us\": " << live.last_heartbeat_us
            << ", \"exit_code\": " << live.exit_code
            << ", \"samples\": " << live.ram_mb.size()
            << " }";
    }
    oss << "\n  ],\n";

    oss << "  \"recent_errors\": [\n";
    first = true;
    int count = 0;
    for (auto& e : s.errors) {
        if (count++ >= 20) break;
        if (!first) oss << ",\n";
        first = false;
        oss << "    { \"code\": \"" << e.code
            << "\", \"subsystem\": \"" << e.subsystem
            << "\", \"correlation_id\": \"" << e.correlation_id
            << "\", \"message\": \"" << e.message << "\" }";
    }
    oss << "\n  ]\n}\n";

    auto write_res = platform::atomic_write_file(dest_path, oss.str());
    if (write_res.is_err()) {
        ust::dispatch::fail_op("snapshot:take",
            ust::make_error("diag", "DIAG-001",
                "Snapshot write failed", write_res.error().full(),
                "Check permissions on the snapshot path."));
        return;
    }
    ust::dispatch::increment_snapshots();
    ust::dispatch::complete_op("snapshot:take");
    ust::dispatch::push_toast("Snapshot saved: " + dest_path, false);
}

}
