#pragma once

#include "genesis/auth/MicrosoftAuth.hpp"
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <optional>
#include <functional>
#include <cstdint>

namespace genesis::ui::state {

// ─── Workspace mode ──────────────────────────────────────────────────────────
enum class WorkspaceMode {
    Instances,
    Modpacks,
    Versions,
    Logs,
    Diagnostics,
    Settings,
};
const char* mode_label(WorkspaceMode m);
const char* mode_glyph(WorkspaceMode m);
constexpr int kModeCount = 6;

// ─── Async op state machine ──────────────────────────────────────────────────
enum class AsyncStatus { Idle, Pending, Success, Error };
const char* status_label(AsyncStatus s);

struct ErrorRecord {
    std::string code;             // "AUTH-001", "NET-022", ...
    std::string subsystem;        // "auth", "version", "jvm", ...
    std::string message;
    std::string detail;
    std::string correlation_id;   // for log linkage
    std::string suggested_action; // optional remediation hint
    int64_t     timestamp_us = 0;
};

struct AsyncOp {
    std::string  id;              // "launch:my-instance"
    std::string  label;
    AsyncStatus  status = AsyncStatus::Idle;
    float        progress = 0.0f;
    std::string  step_label;
    std::optional<ErrorRecord> error;
    int64_t      started_us  = 0;
    int64_t      finished_us = 0;
};

// ─── Per-instance live runtime state ─────────────────────────────────────────
enum class InstanceRuntimeState {
    Stopped,
    Syncing,
    Starting,
    Running,
    Stopping,
    Crashed,
};
const char* runtime_label(InstanceRuntimeState s);

struct InstanceLiveState {
    std::string          instance_id;
    InstanceRuntimeState state = InstanceRuntimeState::Stopped;
    int64_t              pid = 0;
    int64_t              started_us = 0;
    int64_t              ended_us   = 0;
    int32_t              exit_code  = 0;
    std::string          crash_reason;

    // Rolling samples — capped at MAX_SAMPLES
    std::vector<float>   ram_mb;
    std::vector<float>   cpu_pct;
    static constexpr size_t MAX_SAMPLES = 120;
};

// ─── Per-instance modpack state ──────────────────────────────────────────────
struct ModpackState {
    std::string                instance_id;
    std::string                fabric_loader_version;
    std::vector<std::string>   installed;
    std::vector<std::string>   skipped;
    std::vector<std::string>   failed;
    AsyncStatus                install_status   = AsyncStatus::Idle;
    float                      install_progress = 0.0f;
    std::string                current_step;
    std::optional<ErrorRecord> last_error;
};

// ─── Selection / navigation ──────────────────────────────────────────────────
struct UiSelection {
    WorkspaceMode mode = WorkspaceMode::Instances;
    std::string   selected_instance_id;
    std::string   selected_modpack_instance_id;
    std::string   selected_version_id;
    int64_t       selected_log_id = -1;
    std::string   selected_jvm_profile_id;
    std::string   selected_mod_slug;
    std::string   selected_error_code;
};

struct AnimationState {
    WorkspaceMode prev_mode      = WorkspaceMode::Instances;
    int64_t       mode_change_us = 0;
    static constexpr int64_t TRANSITION_US = 160'000;   // ~160ms
};

struct ConsoleState {
    bool   paused              = false;
    bool   scroll_lock         = false;
    int    level_filter        = 0;   // 0=all, 1=info+, 2=warn+, 3=err+
    char   search_buffer[256]  = {};
    bool   show_logger_column  = true;
    float  height_px           = 220.0f;
    bool   collapsed           = false;
};

struct AuthState {
    bool                                  authenticated = false;
    std::string                           username;
    std::string                           uuid;
    AsyncOp                               op;
    std::optional<auth::DeviceCodeInfo>   device_code;
};

struct Toast {
    std::string message;
    bool        is_error  = false;
    int64_t     created_us = 0;
};

// ─── Aggregate state ─────────────────────────────────────────────────────────
struct UiState {
    UiSelection                                          selection;
    AnimationState                                       animation;
    ConsoleState                                         console;
    AuthState                                            auth;

    std::unordered_map<std::string, AsyncOp>             ops;
    std::unordered_map<std::string, InstanceLiveState>   instances;
    std::unordered_map<std::string, ModpackState>        modpacks;

    std::deque<ErrorRecord>                              errors;   // newest at front
    std::deque<Toast>                                    toasts;

    bool                                                 update_available = false;
    std::string                                          update_version;
    uint64_t                                             snapshots_taken  = 0;

    bool                                                 show_new_instance = false;
    bool                                                 show_about        = false;
};

// ─── Access ──────────────────────────────────────────────────────────────────
UiState& global();   // single source of truth, only mutated on main thread

// ─── Dispatch (reducer pipeline) ─────────────────────────────────────────────
namespace dispatch {

void set_mode(WorkspaceMode m);
void select_instance(std::string id);
void select_modpack_instance(std::string id);
void select_version(std::string id);
void select_log(int64_t id);
void select_mod(std::string slug);
void select_jvm_profile(std::string id);
void select_error(std::string code);

void start_op(std::string id, std::string label);
void update_op(std::string id, float progress, std::string step);
void complete_op(std::string id);
void fail_op(std::string id, ErrorRecord err);

void set_instance_state(std::string id, InstanceRuntimeState s);
void set_instance_pid(std::string id, int64_t pid);
void push_instance_sample(std::string id, float ram_mb, float cpu_pct);
void set_instance_exit(std::string id, int32_t exit_code, std::string crash_reason);
void clear_instance_state(std::string id);

void set_modpack(std::string id, ModpackState ms);
void set_modpack_progress(std::string id, float progress, std::string step);
void set_modpack_install_result(std::string id,
                                std::vector<std::string> installed,
                                std::vector<std::string> skipped,
                                std::vector<std::string> failed,
                                std::string fabric_loader_version);
void set_modpack_install_status(std::string id, AsyncStatus s);

void record_error(ErrorRecord err);
void clear_errors();
void push_toast(std::string msg, bool is_error = false);
void prune_old_toasts(int64_t max_age_us);

void set_authenticated(bool yes, std::string username, std::string uuid);
void set_auth_op(AsyncOp op);
void set_device_code(auth::DeviceCodeInfo info);
void clear_device_code();

void set_update_available(bool yes, std::string version);
void increment_snapshots();

void show_new_instance(bool yes);
void show_about(bool yes);

}

// ─── Worker → main thread queue ──────────────────────────────────────────────
void post(std::function<void()> action);   // safe from any thread
void drain();                              // call once per frame on main thread

// Helpers
int64_t      now_us();
std::string  new_correlation_id();
ErrorRecord  make_error(std::string subsystem,
                        std::string code,
                        std::string message,
                        std::string detail = {},
                        std::string suggested = {});

}
