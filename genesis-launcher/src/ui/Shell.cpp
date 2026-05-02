#include "genesis/ui/Shell.hpp"
#include "genesis/ui/Theme.hpp"
#include "genesis/ui/UiState.hpp"
#include "genesis/ui/LogStream.hpp"
#include "genesis/ui/ProcessMonitor.hpp"
#include "genesis/ui/AsyncLauncher.hpp"
#include "genesis/ui/Widgets.hpp"
#include "genesis/ui/Views.hpp"
#include "genesis/core/Launcher.hpp"
#include "genesis/core/EventBus.hpp"
#include "genesis/logging/Logger.hpp"
#include "genesis/platform/PlatformUtils.hpp"

#include <imgui.h>
#include <memory>
#include <cmath>
#include <cstdio>

namespace genesis::ui::shell {

using namespace genesis::core;
namespace ust = genesis::ui::state;

static std::unique_ptr<async_ops::AsyncLauncher>  s_async;
static bool                                       s_initialized = false;

async_ops::AsyncLauncher& async_launcher() {
    return *s_async;
}

void initialize(Launcher& launcher) {
    if (s_initialized) return;
    theme::apply();
    log::install_into_root_logger();
    s_async = std::make_unique<async_ops::AsyncLauncher>(launcher);

    // Wire core EventBus → UI state.
    EventBus::global().subscribe<DownloadProgressEvent>([](const DownloadProgressEvent& e) {
        ust::dispatch::update_op("download:current", e.fraction(), e.name);
    });
    EventBus::global().subscribe<AuthStatusEvent>([](const AuthStatusEvent& e) {
        if (e.authenticated)
            ust::dispatch::set_authenticated(true, e.username, "");
        else
            ust::dispatch::set_authenticated(false, "", "");
    });
    EventBus::global().subscribe<ErrorEvent>([](const ErrorEvent& e) {
        ust::dispatch::record_error(
            ust::make_error(e.context, "EVT-" + e.context, e.message, "", ""));
    });
    EventBus::global().subscribe<UpdateAvailableEvent>([](const UpdateAvailableEvent& e) {
        ust::dispatch::set_update_available(true, e.new_version);
    });

    // Sync initial auth state from launcher.
    if (launcher.auth_manager().is_logged_in()) {
        auto u = launcher.auth_manager().username().value_or("");
        ust::dispatch::set_authenticated(true, u, "");
    }

    s_initialized = true;
}

void shutdown() {
    if (s_async) s_async->shutdown();
    s_async.reset();
    s_initialized = false;
}

// ─── Top header bar ──────────────────────────────────────────────────────────
static void draw_header(Launcher& /*launcher*/) {
    auto& s = ust::global();
    float h = 44.0f;
    ImVec2 vp = ImGui::GetMainViewport()->Size;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({vp.x, h});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::PANEL);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14, 8});
    ImGui::Begin("##header", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToDisplayFront | ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushStyleColor(ImGuiCol_Text, theme::ACCENT);
    ImGui::SetWindowFontScale(1.30f);
    ImGui::TextUnformatted("Genesis");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_FAINT);
    ImGui::Text("v" GENESIS_VERSION_STRING "  /  Minecraft Control Plane");
    ImGui::PopStyleColor();

    // Right-aligned account block
    float right_w = 220.0f;
    ImGui::SameLine(vp.x - right_w);

    if (s.auth.authenticated) {
        widgets::live_dot();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::SUCCESS);
        ImGui::Text("%s", s.auth.username.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Sign out")) async_launcher().start_logout();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
        ImGui::TextUnformatted("not signed in");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Sign in")) async_launcher().start_login();
    }

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ─── Workspace dispatch ──────────────────────────────────────────────────────
static void draw_workspace(Launcher& launcher) {
    auto& s = ust::global();

    // Animation: fade between modes for ~160ms
    int64_t age = ust::now_us() - s.animation.mode_change_us;
    float t = (s.animation.mode_change_us == 0)
                ? 1.0f
                : std::min(1.0f, float(age) / float(ust::AnimationState::TRANSITION_US));
    if (t < 1.0f) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    }

    switch (s.selection.mode) {
        case ust::WorkspaceMode::Instances:   views::draw_instance_dashboard(launcher); break;
        case ust::WorkspaceMode::Modpacks:    views::draw_modpack_view(launcher);       break;
        case ust::WorkspaceMode::Versions:    views::draw_version_view(launcher);       break;
        case ust::WorkspaceMode::Logs:        views::draw_log_view(launcher);           break;
        case ust::WorkspaceMode::Diagnostics: views::draw_diagnostics_view(launcher);   break;
        case ust::WorkspaceMode::Settings:    views::draw_settings_tab(launcher);       break;
    }

    if (t < 1.0f) ImGui::PopStyleVar();
}

// ─── Main render ─────────────────────────────────────────────────────────────
void render(Launcher& launcher) {
    if (!s_initialized) initialize(launcher);

    // Drain queued reducers and poll background work first.
    ust::drain();
    s_async->poll();
    monitor::ProcessMonitor::global().poll();
    ust::dispatch::prune_old_toasts(4'000'000);

    auto& s = ust::global();
    ImVec2 vp = ImGui::GetMainViewport()->Size;

    // Layout constants
    const float HEADER_H   = 44.0f;
    const float NAV_W      = 200.0f;
    const float INSP_W     = 320.0f;
    const float CONSOLE_H  = s.console.collapsed ? 30.0f : s.console.height_px;

    // ── Header ───────────────────────────────────────────────────────────────
    draw_header(launcher);

    // ── Left rail ────────────────────────────────────────────────────────────
    ImGui::SetNextWindowPos({0, HEADER_H});
    ImGui::SetNextWindowSize({NAV_W, vp.y - HEADER_H - CONSOLE_H});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::PANEL_DEEP);
    ImGui::Begin("##nav", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToDisplayFront | ImGuiWindowFlags_NoSavedSettings);
    views::draw_nav_rail(launcher);
    ImGui::End();
    ImGui::PopStyleColor();

    // ── Center workspace ─────────────────────────────────────────────────────
    float center_x = NAV_W;
    float center_w = vp.x - NAV_W - INSP_W;
    ImGui::SetNextWindowPos({center_x, HEADER_H});
    ImGui::SetNextWindowSize({center_w, vp.y - HEADER_H - CONSOLE_H});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::BG);
    ImGui::Begin("##workspace", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToDisplayFront | ImGuiWindowFlags_NoSavedSettings);
    draw_workspace(launcher);
    ImGui::End();
    ImGui::PopStyleColor();

    // ── Right inspector ──────────────────────────────────────────────────────
    ImGui::SetNextWindowPos({vp.x - INSP_W, HEADER_H});
    ImGui::SetNextWindowSize({INSP_W, vp.y - HEADER_H - CONSOLE_H});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::PANEL);
    ImGui::Begin("##inspector", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToDisplayFront | ImGuiWindowFlags_NoSavedSettings);
    views::draw_inspector(launcher);
    ImGui::End();
    ImGui::PopStyleColor();

    // ── Bottom console ───────────────────────────────────────────────────────
    ImGui::SetNextWindowPos({0, vp.y - CONSOLE_H});
    ImGui::SetNextWindowSize({vp.x, CONSOLE_H});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::PANEL_DEEP);
    ImGui::Begin("##console", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToDisplayFront | ImGuiWindowFlags_NoSavedSettings);
    views::draw_console(launcher);
    ImGui::End();
    ImGui::PopStyleColor();

    // ── Modals & overlays ────────────────────────────────────────────────────
    views::draw_device_code_modal();
    views::draw_new_instance_modal(launcher);
    views::draw_about_modal();
    views::draw_toasts();
}

// ════════════════════════════════════════════════════════════════════════════
//  Region: Nav Rail
// ════════════════════════════════════════════════════════════════════════════
}

namespace genesis::ui::views {

namespace ust = genesis::ui::state;

void draw_nav_rail(genesis::core::Launcher& /*launcher*/) {
    auto& s = ust::global();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::ACCENT);
    ImGui::TextUnformatted("NAVIGATION");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    auto draw_entry = [&](ust::WorkspaceMode m,
                          const char* label,
                          const char* badge_text,
                          ImVec4 badge_fg) {
        bool active = (s.selection.mode == m);
        ImGui::PushStyleColor(ImGuiCol_Button,
            active ? theme::with_alpha(theme::ACCENT, 0.25f) : ImVec4{0, 0, 0, 0});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            theme::with_alpha(theme::ACCENT, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_Text,
            active ? theme::ACCENT_HOT : theme::TEXT);

        char id[64];
        std::snprintf(id, sizeof(id), "  %s  %s##%s", ust::mode_glyph(m), label, label);

        if (ImGui::Button(id, {-1, 32}))
            ust::dispatch::set_mode(m);

        ImGui::PopStyleColor(3);

        if (badge_text && badge_text[0]) {
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 38);
            widgets::badge(badge_text, badge_fg, theme::with_alpha(badge_fg, 0.18f));
        }
    };

    // Compute live badges
    int running = 0, syncing = 0, errored = 0;
    for (auto& [id, live] : s.instances) {
        if (live.state == ust::InstanceRuntimeState::Running) ++running;
        if (live.state == ust::InstanceRuntimeState::Syncing) ++syncing;
        if (live.state == ust::InstanceRuntimeState::Crashed) ++errored;
    }
    int64_t info, warn, err;
    log::LogStream::global().counts(info, warn, err);

    char inst_badge[16] = {};
    if (running) std::snprintf(inst_badge, sizeof(inst_badge), "%d", running);
    char log_badge[16] = {};
    if (err)  std::snprintf(log_badge, sizeof(log_badge), "%lld", (long long)err);

    draw_entry(ust::WorkspaceMode::Instances,   "Instances",   inst_badge, theme::SUCCESS);
    draw_entry(ust::WorkspaceMode::Modpacks,    "Modpacks",    "",         theme::INFO);
    draw_entry(ust::WorkspaceMode::Versions,    "Versions",    "",         theme::INFO);
    draw_entry(ust::WorkspaceMode::Logs,        "Logs",        log_badge,  theme::ERR);
    draw_entry(ust::WorkspaceMode::Diagnostics, "Diagnostics",
               errored ? "!" : "", theme::WARN);
    draw_entry(ust::WorkspaceMode::Settings,    "Settings",    "",         theme::INFO);

    // Bottom block: live metric summary
    ImGui::Dummy({0, 16});
    widgets::section_header("LIVE");
    widgets::dim_text("Running:  %d", running);
    widgets::dim_text("Syncing:  %d", syncing);
    widgets::dim_text("Errored:  %d", errored);
    widgets::dim_text("Errors:   %lld", (long long)err);
    widgets::dim_text("Warnings: %lld", (long long)warn);

    if (s.update_available) {
        ImGui::Dummy({0, 12});
        ImGui::PushStyleColor(ImGuiCol_Text, theme::WARN);
        ImGui::TextWrapped("Update available: %s", s.update_version.c_str());
        ImGui::PopStyleColor();
        if (ImGui::SmallButton("View")) ust::dispatch::set_mode(ust::WorkspaceMode::Settings);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Region: Console
// ════════════════════════════════════════════════════════════════════════════
static std::vector<log::LogEntry> g_console_buf;
static int64_t                    g_console_last_id = 0;

void draw_console(genesis::core::Launcher& /*launcher*/) {
    auto& cs = ust::global().console;
    auto& stream = log::LogStream::global();

    // Toolbar
    ImGui::PushStyleColor(ImGuiCol_Text, theme::ACCENT);
    ImGui::TextUnformatted("CONSOLE");
    ImGui::PopStyleColor();
    ImGui::SameLine();

    int64_t info, warn, err;
    stream.counts(info, warn, err);
    widgets::faint_text("  %lld info  %lld warn  %lld err",
                        (long long)info, (long long)warn, (long long)err);

    ImGui::SameLine(ImGui::GetWindowWidth() - 480);

    // Filter dropdown
    const char* levels[] = {"all", "info+", "warn+", "error+"};
    ImGui::SetNextItemWidth(80);
    if (ImGui::Combo("##lvl", &cs.level_filter, levels, 4))
        g_console_last_id = 0;   // re-snapshot from start with new filter

    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    if (ImGui::InputTextWithHint("##search", "search", cs.search_buffer, sizeof(cs.search_buffer)))
        g_console_last_id = 0;

    ImGui::SameLine();
    bool paused = stream.paused();
    if (widgets::toggle(paused ? "  resume  " : "  pause  ", &paused))
        stream.set_paused(paused);

    ImGui::SameLine();
    widgets::toggle(cs.scroll_lock ? "scroll: off" : "scroll: auto", &cs.scroll_lock);

    ImGui::SameLine();
    if (ImGui::SmallButton("export")) {
        auto p = platform::path_join(platform::default_game_dir(), "logs/genesis-console.log");
        shell::async_launcher().start_export_logs(p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("collapse")) cs.collapsed = !cs.collapsed;

    if (cs.collapsed) return;

    // Content
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::BG_DEEP);
    ImGui::BeginChild("##console_body", {0, 0}, false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    // Snapshot recent entries based on filter; for simplicity we re-snapshot
    // from g_console_last_id and append. If filters changed, we reset above.
    int64_t until = stream.paused() ? stream.pause_at_id() : stream.latest_id();
    if (g_console_last_id < until) {
        std::vector<log::LogEntry> new_entries;
        if (cs.level_filter == 0 && cs.search_buffer[0] == '\0') {
            stream.snapshot(g_console_last_id, new_entries, 5000);
        } else {
            stream.snapshot_filtered(g_console_last_id, cs.level_filter,
                                     cs.search_buffer, new_entries, 5000);
        }
        for (auto& e : new_entries) {
            if (e.id > until) break;
            g_console_buf.push_back(std::move(e));
        }
        if (!new_entries.empty())
            g_console_last_id = g_console_buf.empty() ? until : g_console_buf.back().id;
        // Trim ring
        if (g_console_buf.size() > 20000)
            g_console_buf.erase(g_console_buf.begin(),
                                g_console_buf.begin() + (g_console_buf.size() - 20000));
    }

    // Virtualized rendering
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(g_console_buf.size()));
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            auto& e = g_console_buf[i];
            ImVec4 col;
            switch (e.level) {
                case logging::Level::Trace:    col = theme::TRACE_;  break;
                case logging::Level::Debug:    col = theme::TRACE_;  break;
                case logging::Level::Info:     col = theme::TEXT;    break;
                case logging::Level::Warn:     col = theme::WARN;    break;
                case logging::Level::Error:
                case logging::Level::Critical: col = theme::ERR;     break;
            }
            // [HH:MM:SS.ms] [logger] message
            int64_t ts_s  = e.timestamp_us / 1'000'000;
            int64_t ts_ms = (e.timestamp_us / 1000) % 1000;
            int hh = (ts_s / 3600) % 24, mm = (ts_s / 60) % 60, ss = ts_s % 60;

            ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_FAINT);
            ImGui::Text("%02d:%02d:%02d.%03lld", hh, mm, ss, (long long)ts_ms);
            ImGui::PopStyleColor();

            if (cs.show_logger_column) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, theme::ACCENT_DIM);
                ImGui::Text("%-12s", e.logger_name.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            char tag[8];
            switch (e.level) {
                case logging::Level::Warn: std::snprintf(tag, sizeof(tag), "WARN");  break;
                case logging::Level::Error:
                case logging::Level::Critical: std::snprintf(tag, sizeof(tag), "ERR"); break;
                case logging::Level::Info: std::snprintf(tag, sizeof(tag), "INF");   break;
                default:                   std::snprintf(tag, sizeof(tag), "DBG");   break;
            }
            ImGui::Text("%s", tag);
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(e.message.c_str());
            ImGui::PopStyleColor();
        }
    }
    clip.End();

    if (!cs.scroll_lock && !stream.paused() &&
        ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ════════════════════════════════════════════════════════════════════════════
//  Region: Inspector  (context-aware right panel)
// ════════════════════════════════════════════════════════════════════════════
static void inspector_for_instance(genesis::core::Launcher& launcher,
                                   const std::string& id) {
    auto inst_opt = launcher.instance_manager().find(id);
    if (!inst_opt) {
        widgets::empty_state("·", "Instance not found",
                             "Pick another from the list.");
        return;
    }
    auto& inst = inst_opt->get();
    auto& cfg  = inst.config();

    widgets::section_header("INSTANCE");
    widgets::kv_row("Name",     cfg.display_name.c_str());
    widgets::kv_row("ID",       cfg.id.c_str());
    widgets::kv_row("Version",  cfg.game_version.c_str());
    widgets::kv_row("Profile",  cfg.jvm_profile_id.empty()
                                 ? "(default)" : cfg.jvm_profile_id.c_str());

    auto& s = ust::global();
    auto it = s.instances.find(id);
    if (it != s.instances.end()) {
        ImGui::Spacing();
        widgets::section_header("RUNTIME");
        widgets::badge_runtime(it->second.state);
        if (it->second.pid > 0)
            widgets::kv_row_fmt("PID", "%lld", (long long)it->second.pid);
        if (it->second.exit_code != 0)
            widgets::kv_row_fmt("Exit", "%d", it->second.exit_code);

        ImGui::Spacing();
        widgets::timeline_chart("RAM", it->second.ram_mb, "MB",
                                theme::ACCENT, {-1, 60});
        widgets::timeline_chart("CPU", it->second.cpu_pct, "%",
                                theme::WARN, {-1, 60});
    }

    ImGui::Spacing();
    widgets::section_header("ACTIONS");
    bool can_launch = s.auth.authenticated &&
        (it == s.instances.end() ||
         it->second.state == ust::InstanceRuntimeState::Stopped ||
         it->second.state == ust::InstanceRuntimeState::Crashed);

    if (!can_launch) ImGui::BeginDisabled();
    if (ImGui::Button("Launch", {-1, 30}))
        shell::async_launcher().start_launch(id);
    if (!can_launch) ImGui::EndDisabled();

    bool can_stop = (it != s.instances.end() &&
        (it->second.state == ust::InstanceRuntimeState::Running ||
         it->second.state == ust::InstanceRuntimeState::Starting));
    if (!can_stop) ImGui::BeginDisabled();
    if (ImGui::Button("Stop", {-1, 26})) shell::async_launcher().start_stop(id);
    if (!can_stop) ImGui::EndDisabled();

    if (ImGui::Button("Open instance folder", {-1, 26}))
        platform::open_in_file_manager(inst.root_dir());
    if (ImGui::Button("Open mods folder", {-1, 26}))
        platform::open_in_file_manager(inst.mods_dir());
    if (ImGui::Button("Reinstall performance pack", {-1, 26}))
        shell::async_launcher().start_install_modpack(id);
}

static void inspector_for_log(int64_t id) {
    auto opt = log::LogStream::global().by_id(id);
    if (!opt) {
        widgets::empty_state("·", "Log entry not in buffer",
                             "It may have been rotated out.");
        return;
    }
    auto& e = *opt;
    widgets::section_header("LOG ENTRY");
    widgets::badge_log_level(e.level);
    ImGui::Spacing();
    widgets::kv_row("Logger",   e.logger_name.c_str());
    widgets::kv_row_fmt("ID",   "%lld", (long long)e.id);
    widgets::kv_row_fmt("Time", "%lld us", (long long)e.timestamp_us);
    if (!e.correlation_id.empty())
        widgets::kv_row("Corr ID", e.correlation_id.c_str());

    ImGui::Spacing();
    widgets::section_header("MESSAGE");
    ImGui::TextWrapped("%s", e.message.c_str());

    if (!e.correlation_id.empty()) {
        ImGui::Spacing();
        if (ImGui::Button("Copy correlation ID", {-1, 26}))
            platform::copy_to_clipboard(e.correlation_id);
    }
}

static void inspector_for_version(genesis::core::Launcher& launcher,
                                  const std::string& vid) {
    widgets::section_header("VERSION");
    widgets::kv_row("ID", vid.c_str());

    auto list_res = launcher.version_manager().fetch_version_list(false);
    if (list_res.is_ok()) {
        auto found = list_res.value().find(vid);
        if (found) {
            widgets::kv_row("Type",
                version::release_type_to_string(found->type).c_str());
            widgets::kv_row("Released", found->release_time.c_str());
        }
    }

    bool qualifies = mods::PerformancePack::qualifies(vid);
    ImGui::Spacing();
    widgets::section_header("PERFORMANCE PACK");
    if (qualifies) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::SUCCESS);
        ImGui::TextWrapped("Eligible — Sodium, Sodium Extra, Lithium and Iris will auto-install on Fabric.");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
        ImGui::TextWrapped("Not eligible. Auto pack requires release 1.21.11+.");
        ImGui::PopStyleColor();
    }
}

static void inspector_for_error(const std::string& code) {
    auto& s = ust::global();
    for (auto& e : s.errors) {
        if (e.code == code) {
            widgets::error_panel(e);
            return;
        }
    }
    widgets::empty_state("·", "Error no longer in buffer",
                         "It scrolled out of the rolling window.");
}

void draw_inspector(genesis::core::Launcher& launcher) {
    auto& s = ust::global();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::ACCENT);
    ImGui::TextUnformatted("INSPECTOR");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Routing: prefer the most specific selection per current mode.
    switch (s.selection.mode) {
        case ust::WorkspaceMode::Instances:
        case ust::WorkspaceMode::Modpacks:
        case ust::WorkspaceMode::Diagnostics:
            if (!s.selection.selected_instance_id.empty()) {
                inspector_for_instance(launcher, s.selection.selected_instance_id);
                return;
            }
            break;
        case ust::WorkspaceMode::Versions:
            if (!s.selection.selected_version_id.empty()) {
                inspector_for_version(launcher, s.selection.selected_version_id);
                return;
            }
            break;
        case ust::WorkspaceMode::Logs:
            if (s.selection.selected_log_id > 0) {
                inspector_for_log(s.selection.selected_log_id);
                return;
            }
            break;
        case ust::WorkspaceMode::Settings:
            break;
    }

    if (!s.selection.selected_error_code.empty()) {
        inspector_for_error(s.selection.selected_error_code);
        return;
    }

    // Default empty state
    widgets::empty_state("·", "Nothing selected",
                         "Click an item to see its details here.");
}

// ════════════════════════════════════════════════════════════════════════════
//  Modals & toasts
// ════════════════════════════════════════════════════════════════════════════
void draw_device_code_modal() {
    auto& s = ust::global();
    if (!s.auth.device_code.has_value()) return;
    auto& info = *s.auth.device_code;

    ImGui::OpenPopup("Microsoft Sign In##gen");
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({440, 270});

    if (ImGui::BeginPopupModal("Microsoft Sign In##gen", nullptr,
                               ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("Visit the URL below and enter the code:");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
        ImGui::TextUnformatted(info.verification_uri.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::SetWindowFontScale(1.8f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::ACCENT);
        ImGui::Text("  %s", info.user_code.c_str());
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);

        ImGui::Spacing();
        widgets::dim_text("Code expires in %d seconds.", info.expires_in_seconds);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Open browser & copy code", {-1, 28})) {
            platform::open_url_in_browser(info.verification_uri);
            platform::copy_to_clipboard(info.user_code);
        }
        if (ImGui::Button("Cancel", {-1, 24})) {
            ust::dispatch::clear_device_code();
            // Note: this does not actually cancel the polling thread; the
            // worker will time out on its own. We just close the modal.
        }
        ImGui::EndPopup();
    }
}

void draw_about_modal() {
    auto& s = ust::global();
    if (!s.show_about) return;
    ImGui::OpenPopup("About Genesis##gen");
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({420, 220});

    bool open = true;
    if (ImGui::BeginPopupModal("About Genesis##gen", &open,
                               ImGuiWindowFlags_NoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::ACCENT);
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextUnformatted("Genesis");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        widgets::dim_text("Minecraft launcher v" GENESIS_VERSION_STRING);
        ImGui::Spacing();
        ImGui::TextWrapped("A control plane for modded Minecraft environments. "
                           "Streaming logs, async actions, structured errors.");
        ImGui::Spacing();
        widgets::faint_text("Built with C++20, Dear ImGui, libcurl, OpenSSL.");
        ImGui::EndPopup();
    }
    if (!open) ust::dispatch::show_about(false);
}

void draw_new_instance_modal(genesis::core::Launcher& /*launcher*/) {
    auto& s = ust::global();
    if (!s.show_new_instance) return;

    ImGui::OpenPopup("New Instance##gen");
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({440, 280});

    static char  name_buf[128] = {};
    static char  ver_buf[32]   = "1.21.11";

    bool open = true;
    if (ImGui::BeginPopupModal("New Instance##gen", &open,
                               ImGuiWindowFlags_NoResize)) {
        widgets::section_header("BASICS");
        ImGui::Text("Name");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##name", name_buf, sizeof(name_buf));
        ImGui::Spacing();
        ImGui::Text("Minecraft version");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ver", ver_buf, sizeof(ver_buf));

        ImGui::Spacing();
        bool qualifies = mods::PerformancePack::qualifies(ver_buf);
        if (qualifies) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::SUCCESS);
            ImGui::TextWrapped("Eligible: Sodium + Sodium Extra + Lithium + Iris will install automatically.");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
            ImGui::TextWrapped("This version is not in the performance pack window (1.21.11+).");
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        bool valid = (name_buf[0] != '\0' && ver_buf[0] != '\0');
        if (!valid) ImGui::BeginDisabled();
        if (ImGui::Button("Create", {120, 30})) {
            instance::InstanceConfig cfg;
            cfg.id           = name_buf;
            cfg.display_name = name_buf;
            cfg.game_version = ver_buf;
            shell::async_launcher().start_create_instance(std::move(cfg));
            name_buf[0] = '\0';
            ust::dispatch::show_new_instance(false);
        }
        if (!valid) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {80, 30}))
            ust::dispatch::show_new_instance(false);

        ImGui::EndPopup();
    }
    if (!open) ust::dispatch::show_new_instance(false);
}

void draw_toasts() {
    auto& s = ust::global();
    if (s.toasts.empty()) return;

    ImVec2 vp = ImGui::GetMainViewport()->Size;
    float w = 320.0f;
    float y = 56.0f;
    int idx = 0;
    for (auto& t : s.toasts) {
        char id[32];
        std::snprintf(id, sizeof(id), "##toast_%d", idx++);
        ImGui::SetNextWindowPos({vp.x - w - 16, y});
        ImGui::SetNextWindowSize({w, 0});
        ImGui::SetNextWindowBgAlpha(0.95f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
            t.is_error ? theme::with_alpha(theme::ERR, 0.20f) : theme::PANEL_HI);
        ImGui::PushStyleColor(ImGuiCol_Border,
            t.is_error ? theme::ERR : theme::ACCENT_DIM);
        ImGui::Begin(id, nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PushStyleColor(ImGuiCol_Text,
            t.is_error ? theme::ERR : theme::TEXT);
        ImGui::TextWrapped("%s", t.message.c_str());
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleColor(2);
        y += 56.0f;
    }
}

}
