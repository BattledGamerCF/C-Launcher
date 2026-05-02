#include "genesis/ui/Views.hpp"
#include "genesis/ui/UiState.hpp"
#include "genesis/ui/Theme.hpp"
#include "genesis/ui/Widgets.hpp"
#include "genesis/ui/Shell.hpp"
#include "genesis/ui/AsyncLauncher.hpp"
#include "genesis/core/Launcher.hpp"
#include "genesis/instance/InstanceManager.hpp"
#include "genesis/platform/PlatformUtils.hpp"

#include <imgui.h>
#include <cstdio>

namespace genesis::ui::views {

namespace ust = genesis::ui::state;

static void draw_instance_card(genesis::core::Launcher& launcher,
                               genesis::instance::Instance& inst,
                               float card_w) {
    auto& s   = ust::global();
    auto& cfg = inst.config();

    bool selected = (s.selection.selected_instance_id == cfg.id);
    auto it       = s.instances.find(cfg.id);
    auto runtime  = (it != s.instances.end()) ? it->second.state
                                              : ust::InstanceRuntimeState::Stopped;

    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        selected ? theme::with_alpha(theme::ACCENT, 0.10f) : theme::PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border,
        selected ? theme::ACCENT : theme::BORDER);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);

    ImGui::BeginChild(("##card_" + cfg.id).c_str(),
                      {card_w, 168}, true);

    // Header line: name + state badge
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT);
    ImGui::SetWindowFontScale(1.10f);
    ImGui::TextUnformatted(cfg.display_name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::SameLine(card_w - 90);
    widgets::badge_runtime(runtime);

    // Subline: version + last played
    widgets::dim_text("v %s", cfg.game_version.c_str());

    uint64_t secs = cfg.total_play_seconds;
    uint32_t hh = static_cast<uint32_t>(secs / 3600);
    uint32_t mm = static_cast<uint32_t>((secs % 3600) / 60);
    widgets::faint_text("Played: %uh %02um", hh, mm);

    // Process identity + heartbeat (truth from ProcessHandle, surfaced
    // through UiState by the AsyncLauncher watcher and ProcessMonitor).
    if (it != s.instances.end() && it->second.pid > 0) {
        const auto& live = it->second;
        const char* hb_label = "no heartbeat";
        char hb_buf[64];
        if (live.last_heartbeat_us > 0) {
            int64_t age_ms = (ust::now_us() - live.last_heartbeat_us) / 1000;
            std::snprintf(hb_buf, sizeof(hb_buf), "heartbeat %lldms ago",
                          (long long)age_ms);
            hb_label = hb_buf;
        }
        widgets::faint_text("PID %lld • %s%s",
                            (long long)live.pid, hb_label,
                            live.has_handle ? "" : " • no handle");
    }

    // Sparklines
    if (it != s.instances.end() && !it->second.ram_mb.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
        ImGui::Text("RAM");
        ImGui::PopStyleColor();
        ImGui::SameLine(38);
        widgets::sparkline(("##ram_" + cfg.id).c_str(),
                           it->second.ram_mb, theme::ACCENT,
                           {card_w - 60, 28});
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
        ImGui::Text("CPU");
        ImGui::PopStyleColor();
        ImGui::SameLine(38);
        widgets::sparkline(("##cpu_" + cfg.id).c_str(),
                           it->second.cpu_pct, theme::WARN,
                           {card_w - 60, 28});
    } else {
        ImGui::Dummy({0, 60});
        widgets::faint_text("  (no live samples)");
    }

    // Action row — buttons are derived strictly from ProcessHandle ownership.
    ImGui::Spacing();
    bool has_handle = (it != s.instances.end()) && it->second.has_handle;
    bool can_launch = s.auth.authenticated && !has_handle &&
        (runtime == ust::InstanceRuntimeState::Stopped ||
         runtime == ust::InstanceRuntimeState::Crashed ||
         runtime == ust::InstanceRuntimeState::Detached);
    auto op_it = s.ops.find("launch:" + cfg.id);
    bool launching = (op_it != s.ops.end() &&
                      op_it->second.status == ust::AsyncStatus::Pending);

    if (!can_launch || launching) ImGui::BeginDisabled();
    if (ImGui::Button(launching ? "Starting..." : "Launch", {90, 28}))
        shell::async_launcher().start_launch(cfg.id);
    if (!can_launch || launching) ImGui::EndDisabled();

    ImGui::SameLine();
    // Stop is enabled only if a real OS handle exists; without one we
    // expose "Detach" instead (clears the placeholder UI state).
    const char* stop_label = has_handle ? "Stop" : "Detach";
    bool can_stop = has_handle ||
        runtime == ust::InstanceRuntimeState::Detached ||
        runtime == ust::InstanceRuntimeState::Zombie;
    if (!can_stop) ImGui::BeginDisabled();
    if (ImGui::Button(stop_label, {64, 28})) shell::async_launcher().start_stop(cfg.id);
    if (!can_stop) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Folder", {66, 28}))
        platform::open_in_file_manager(inst.root_dir());

    // Click anywhere to select
    bool hovered_card = ImGui::IsWindowHovered() &&
                        !ImGui::IsAnyItemHovered() &&
                        ImGui::IsMouseClicked(0);
    if (hovered_card)
        ust::dispatch::select_instance(cfg.id);

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void draw_instance_dashboard(genesis::core::Launcher& launcher) {
    auto& s = ust::global();

    // Header bar
    widgets::section_header("INSTANCES");
    ImGui::SameLine(ImGui::GetWindowWidth() - 180);
    if (ImGui::Button("+ New instance", {160, 26}))
        ust::dispatch::show_new_instance(true);

    auto& list = launcher.instance_manager().all();
    if (list.empty()) {
        widgets::empty_state("[]", "No instances yet",
                             "Click '+ New instance' to start.");
        return;
    }

    // Summary strip
    int running = 0, syncing = 0, crashed = 0;
    for (auto& [id, live] : s.instances) {
        if (live.state == ust::InstanceRuntimeState::Running) ++running;
        if (live.state == ust::InstanceRuntimeState::Syncing) ++syncing;
        if (live.state == ust::InstanceRuntimeState::Crashed) ++crashed;
    }
    widgets::status_dot(theme::SUCCESS,  ("running:  " + std::to_string(running)).c_str());
    ImGui::SameLine();
    widgets::status_dot(theme::INFO,     ("syncing:  " + std::to_string(syncing)).c_str());
    ImGui::SameLine();
    widgets::status_dot(theme::ERR,      ("crashed:  " + std::to_string(crashed)).c_str());
    ImGui::SameLine();
    widgets::status_dot(theme::TEXT_DIM, ("total:    " + std::to_string(list.size())).c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Card grid: 2 columns minimum, expands to 3 on wide screens.
    float pane_w  = ImGui::GetContentRegionAvail().x;
    float spacing = 12.0f;
    int   cols    = (pane_w > 760.0f) ? 3 : 2;
    float card_w  = (pane_w - spacing * (cols - 1)) / float(cols);

    for (size_t i = 0; i < list.size(); ++i) {
        if (i % cols != 0) ImGui::SameLine(0, spacing);
        draw_instance_card(launcher, *list[i], card_w);
    }
}

}
