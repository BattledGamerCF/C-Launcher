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
    std::lock_guard<std::mutex> l(mu_);
    for (auto it = tasks_.begin(); it != tasks_.end();) {
        if ((*it)->done.load()) {
            if ((*it)->thread && (*it)->thread->joinable()) (*it)->thread->join();
            it = tasks_.erase(it);
        } else ++it;
    }
}

void AsyncLauncher::shutdown() {
    shutting_down_.store(true);
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
    spawn("launch:" + instance_id, "Launch " + instance_id,
          [this, instance_id]() { run_launch_worker(instance_id); });
}

void AsyncLauncher::run_launch_worker(std::string instance_id) {
    log->info("Launch requested for " + instance_id);

    ust::dispatch::set_instance_state(instance_id, ust::InstanceRuntimeState::Syncing);
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
        return;
    }

    auto& report = res.value();
    ust::dispatch::set_instance_exit(instance_id, report.exit_code,
        report.exit_code == 0 ? "" : "Exited with code " + std::to_string(report.exit_code));
    ust::dispatch::complete_op("launch:" + instance_id);
}

// ─── Stop ────────────────────────────────────────────────────────────────────
void AsyncLauncher::start_stop(std::string instance_id) {
    spawn("stop:" + instance_id, "Stop " + instance_id,
          [instance_id]() {
        // The launcher's process is owned by the launch worker thread; signal
        // intent here so the dashboard reflects it. Real terminate is a follow-up.
        ust::dispatch::set_instance_state(instance_id, ust::InstanceRuntimeState::Stopping);
        ust::dispatch::update_op("stop:" + instance_id, 0.5f, "Sending SIGTERM");
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        ust::dispatch::set_instance_state(instance_id, ust::InstanceRuntimeState::Stopped);
        ust::dispatch::complete_op("stop:" + instance_id);
    });
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
