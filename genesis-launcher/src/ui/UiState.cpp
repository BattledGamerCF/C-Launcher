#include "genesis/ui/UiState.hpp"
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <vector>

namespace genesis::ui::state {

// ─── Labels ──────────────────────────────────────────────────────────────────
const char* mode_label(WorkspaceMode m) {
    switch (m) {
        case WorkspaceMode::Instances:   return "Instances";
        case WorkspaceMode::Modpacks:    return "Modpacks";
        case WorkspaceMode::Versions:    return "Versions";
        case WorkspaceMode::Logs:        return "Logs";
        case WorkspaceMode::Diagnostics: return "Diagnostics";
        case WorkspaceMode::Settings:    return "Settings";
    }
    return "?";
}

const char* mode_glyph(WorkspaceMode m) {
    switch (m) {
        case WorkspaceMode::Instances:   return "[I]";
        case WorkspaceMode::Modpacks:    return "[M]";
        case WorkspaceMode::Versions:    return "[V]";
        case WorkspaceMode::Logs:        return "[L]";
        case WorkspaceMode::Diagnostics: return "[D]";
        case WorkspaceMode::Settings:    return "[S]";
    }
    return "[?]";
}

const char* status_label(AsyncStatus s) {
    switch (s) {
        case AsyncStatus::Idle:    return "idle";
        case AsyncStatus::Pending: return "pending";
        case AsyncStatus::Success: return "success";
        case AsyncStatus::Error:   return "error";
    }
    return "?";
}

const char* runtime_label(InstanceRuntimeState s) {
    switch (s) {
        case InstanceRuntimeState::Stopped:  return "stopped";
        case InstanceRuntimeState::Syncing:  return "syncing";
        case InstanceRuntimeState::Starting: return "starting";
        case InstanceRuntimeState::Running:  return "running";
        case InstanceRuntimeState::Stopping: return "stopping";
        case InstanceRuntimeState::Crashed:  return "crashed";
    }
    return "?";
}

// ─── Singleton ───────────────────────────────────────────────────────────────
UiState& global() {
    static UiState s;
    return s;
}

// ─── Dispatch queue ──────────────────────────────────────────────────────────
namespace {
std::mutex                          q_mu;
std::vector<std::function<void()>>  q_pending;
}

void post(std::function<void()> action) {
    std::lock_guard<std::mutex> l(q_mu);
    q_pending.push_back(std::move(action));
}

void drain() {
    std::vector<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> l(q_mu);
        work.swap(q_pending);
    }
    for (auto& f : work) {
        try { f(); } catch (...) { /* swallow — UI must not crash on bad updates */ }
    }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
int64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

std::string new_correlation_id() {
    static std::mt19937_64 rng{std::random_device{}()};
    uint64_t v = rng();
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << v;
    return oss.str();
}

ErrorRecord make_error(std::string subsystem,
                       std::string code,
                       std::string message,
                       std::string detail,
                       std::string suggested) {
    ErrorRecord e;
    e.subsystem        = std::move(subsystem);
    e.code             = std::move(code);
    e.message          = std::move(message);
    e.detail           = std::move(detail);
    e.suggested_action = std::move(suggested);
    e.correlation_id   = new_correlation_id();
    e.timestamp_us     = now_us();
    return e;
}

// ─── Dispatch implementations ────────────────────────────────────────────────
namespace dispatch {

#define REDUCER(BODY)  post([=]() mutable { auto& s = global(); BODY })

void set_mode(WorkspaceMode m) {
    REDUCER({
        if (s.selection.mode != m) {
            s.animation.prev_mode      = s.selection.mode;
            s.animation.mode_change_us = now_us();
            s.selection.mode           = m;
        }
    });
}

void select_instance(std::string id)          { REDUCER({ s.selection.selected_instance_id = id; s.selection.selected_modpack_instance_id = id; }); }
void select_modpack_instance(std::string id)  { REDUCER({ s.selection.selected_modpack_instance_id = id; }); }
void select_version(std::string id)           { REDUCER({ s.selection.selected_version_id = id; }); }
void select_log(int64_t id)                   { REDUCER({ s.selection.selected_log_id = id; }); }
void select_mod(std::string slug)             { REDUCER({ s.selection.selected_mod_slug = slug; }); }
void select_jvm_profile(std::string id)       { REDUCER({ s.selection.selected_jvm_profile_id = id; }); }
void select_error(std::string code)           { REDUCER({ s.selection.selected_error_code = code; }); }

void start_op(std::string id, std::string label) {
    REDUCER({
        AsyncOp op;
        op.id          = id;
        op.label       = label;
        op.status      = AsyncStatus::Pending;
        op.progress    = 0.0f;
        op.started_us  = now_us();
        s.ops[id]      = std::move(op);
    });
}

void update_op(std::string id, float progress, std::string step) {
    REDUCER({
        auto it = s.ops.find(id);
        if (it == s.ops.end()) return;
        it->second.progress   = progress;
        it->second.step_label = step;
    });
}

void complete_op(std::string id) {
    REDUCER({
        auto it = s.ops.find(id);
        if (it == s.ops.end()) return;
        it->second.status      = AsyncStatus::Success;
        it->second.progress    = 1.0f;
        it->second.finished_us = now_us();
    });
}

void fail_op(std::string id, ErrorRecord err) {
    REDUCER({
        auto it = s.ops.find(id);
        if (it == s.ops.end()) {
            AsyncOp op;
            op.id = id;
            op.label = id;
            op.status = AsyncStatus::Error;
            op.started_us  = now_us();
            op.finished_us = now_us();
            op.error = err;
            s.ops[id] = std::move(op);
        } else {
            it->second.status      = AsyncStatus::Error;
            it->second.finished_us = now_us();
            it->second.error       = err;
        }
        s.errors.push_front(err);
        if (s.errors.size() > 100) s.errors.pop_back();
    });
}

void set_instance_state(std::string id, InstanceRuntimeState st) {
    REDUCER({
        auto& live = s.instances[id];
        live.instance_id = id;
        live.state       = st;
        if (st == InstanceRuntimeState::Starting || st == InstanceRuntimeState::Syncing) {
            live.started_us = now_us();
        } else if (st == InstanceRuntimeState::Stopped || st == InstanceRuntimeState::Crashed) {
            live.ended_us = now_us();
        }
    });
}

void set_instance_pid(std::string id, int64_t pid) {
    REDUCER({
        auto& live = s.instances[id];
        live.instance_id = id;
        live.pid         = pid;
    });
}

void push_instance_sample(std::string id, float ram_mb, float cpu_pct) {
    REDUCER({
        auto& live = s.instances[id];
        live.instance_id = id;
        live.ram_mb.push_back(ram_mb);
        live.cpu_pct.push_back(cpu_pct);
        if (live.ram_mb.size() > InstanceLiveState::MAX_SAMPLES)
            live.ram_mb.erase(live.ram_mb.begin());
        if (live.cpu_pct.size() > InstanceLiveState::MAX_SAMPLES)
            live.cpu_pct.erase(live.cpu_pct.begin());
    });
}

void set_instance_exit(std::string id, int32_t exit_code, std::string crash_reason) {
    REDUCER({
        auto& live = s.instances[id];
        live.instance_id  = id;
        live.exit_code    = exit_code;
        live.crash_reason = crash_reason;
        live.ended_us     = now_us();
        live.state = (exit_code == 0 && crash_reason.empty())
                       ? InstanceRuntimeState::Stopped
                       : InstanceRuntimeState::Crashed;
    });
}

void clear_instance_state(std::string id) {
    REDUCER({ s.instances.erase(id); });
}

void set_modpack(std::string id, ModpackState ms) {
    REDUCER({ s.modpacks[id] = std::move(ms); });
}

void set_modpack_progress(std::string id, float progress, std::string step) {
    REDUCER({
        auto& m = s.modpacks[id];
        m.instance_id      = id;
        m.install_progress = progress;
        m.current_step     = step;
        m.install_status   = AsyncStatus::Pending;
    });
}

void set_modpack_install_result(std::string id,
                                std::vector<std::string> installed,
                                std::vector<std::string> skipped,
                                std::vector<std::string> failed,
                                std::string fabric_loader_version) {
    REDUCER({
        auto& m = s.modpacks[id];
        m.instance_id            = id;
        m.installed              = installed;
        m.skipped                = skipped;
        m.failed                 = failed;
        m.fabric_loader_version  = fabric_loader_version;
        m.install_progress       = 1.0f;
        m.install_status         = failed.empty() ? AsyncStatus::Success : AsyncStatus::Error;
    });
}

void set_modpack_install_status(std::string id, AsyncStatus st) {
    REDUCER({
        auto& m = s.modpacks[id];
        m.instance_id    = id;
        m.install_status = st;
    });
}

void record_error(ErrorRecord err) {
    REDUCER({
        s.errors.push_front(err);
        if (s.errors.size() > 100) s.errors.pop_back();
    });
}

void clear_errors() { REDUCER({ s.errors.clear(); }); }

void push_toast(std::string msg, bool is_error) {
    REDUCER({
        Toast t;
        t.message     = msg;
        t.is_error    = is_error;
        t.created_us  = now_us();
        s.toasts.push_back(std::move(t));
        if (s.toasts.size() > 8) s.toasts.pop_front();
    });
}

void prune_old_toasts(int64_t max_age_us) {
    REDUCER({
        int64_t cutoff = now_us() - max_age_us;
        while (!s.toasts.empty() && s.toasts.front().created_us < cutoff)
            s.toasts.pop_front();
    });
}

void set_authenticated(bool yes, std::string username, std::string uuid) {
    REDUCER({
        s.auth.authenticated = yes;
        s.auth.username      = username;
        s.auth.uuid          = uuid;
        if (!yes) {
            s.auth.device_code.reset();
        }
    });
}

void set_auth_op(AsyncOp op)                  { REDUCER({ s.auth.op = op; }); }
void set_device_code(auth::DeviceCodeInfo i)  { REDUCER({ s.auth.device_code = i; }); }
void clear_device_code()                      { REDUCER({ s.auth.device_code.reset(); }); }

void set_update_available(bool yes, std::string v) {
    REDUCER({ s.update_available = yes; s.update_version = v; });
}

void increment_snapshots() { REDUCER({ s.snapshots_taken++; }); }

void show_new_instance(bool yes) { REDUCER({ s.show_new_instance = yes; }); }
void show_about(bool yes)        { REDUCER({ s.show_about = yes; }); }

#undef REDUCER

}

}
