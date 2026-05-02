#include "genesis/ui/Views.hpp"
#include "genesis/ui/UiState.hpp"
#include "genesis/ui/Theme.hpp"
#include "genesis/ui/Widgets.hpp"
#include "genesis/ui/LogStream.hpp"
#include "genesis/ui/Shell.hpp"
#include "genesis/ui/AsyncLauncher.hpp"
#include "genesis/core/Launcher.hpp"
#include "genesis/platform/PlatformUtils.hpp"

#include <imgui.h>
#include <cstdio>
#include <chrono>

namespace genesis::ui::views {

namespace ust = genesis::ui::state;

static std::vector<float> g_global_ram_history;
static int64_t            g_last_history_us = 0;

static void update_global_history() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    if (now_us - g_last_history_us < 1'000'000) return;   // 1s
    g_last_history_us = now_us;

    float total_ram = 0.0f;
    auto& s = ust::global();
    for (auto& [id, live] : s.instances) {
        if (!live.ram_mb.empty()) total_ram += live.ram_mb.back();
    }
    g_global_ram_history.push_back(total_ram);
    if (g_global_ram_history.size() > 240)
        g_global_ram_history.erase(g_global_ram_history.begin());
}

static void draw_health_banner() {
    auto& s = ust::global();
    int crashed = 0, running = 0;
    for (auto& [id, live] : s.instances) {
        if (live.state == ust::InstanceRuntimeState::Crashed) ++crashed;
        if (live.state == ust::InstanceRuntimeState::Running) ++running;
    }

    ImVec4 bg = (crashed > 0) ? theme::with_alpha(theme::ERR, 0.18f)
              : (running > 0) ? theme::with_alpha(theme::SUCCESS, 0.15f)
              :                 theme::PANEL_HI;
    ImVec4 fg = (crashed > 0) ? theme::ERR
              : (running > 0) ? theme::SUCCESS
              :                 theme::TEXT_DIM;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border,  fg);
    ImGui::BeginChild("##health", {0, 0},
        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border);
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    if (crashed > 0) {
        ImGui::SetWindowFontScale(1.15f);
        ImGui::Text("Degraded — %d crashed instance(s)", crashed);
        ImGui::SetWindowFontScale(1.0f);
    } else if (running > 0) {
        ImGui::SetWindowFontScale(1.15f);
        ImGui::Text("Healthy — %d running, no faults", running);
        ImGui::SetWindowFontScale(1.0f);
    } else {
        ImGui::SetWindowFontScale(1.10f);
        ImGui::Text("Idle — no active instances");
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

static const char* infer_root_cause(const ust::ErrorRecord& e) {
    auto contains = [&](const char* needle) {
        return e.message.find(needle) != std::string::npos
            || e.detail.find(needle)  != std::string::npos;
    };
    if (contains("OutOfMemory") || contains("heap"))     return "JVM heap exhausted — raise max memory in JVM profile.";
    if (contains("Connection") || contains("network"))   return "Network reachability — check connectivity / proxy.";
    if (contains("permission") || contains("access"))    return "Filesystem permissions — check write access.";
    if (contains("hash") || contains("checksum"))        return "Asset integrity — re-download by clearing the cache.";
    if (contains("token") || contains("auth"))           return "Auth state stale — sign in again.";
    return nullptr;
}

void draw_diagnostics_view(genesis::core::Launcher& launcher) {
    update_global_history();
    auto& s = ust::global();

    widgets::section_header("DIAGNOSTICS");
    ImGui::SameLine(ImGui::GetWindowWidth() - 220);
    if (ImGui::Button("Capture snapshot", {200, 26})) {
        auto p = genesis::platform::path_join(
            genesis::platform::default_game_dir(),
            "snapshots/snapshot.json");
        shell::async_launcher().start_take_snapshot(p);
    }
    widgets::faint_text("Snapshots taken: %llu", (unsigned long long)s.snapshots_taken);

    ImGui::Spacing();
    draw_health_banner();

    ImGui::Spacing();
    widgets::section_header("MEMORY PRESSURE TIMELINE");
    widgets::timeline_chart("aggregate RAM (across instances)",
        g_global_ram_history, "MB", theme::ACCENT, {-1, 100});

    ImGui::Spacing();
    widgets::section_header("PER-INSTANCE HEALTH");
    if (s.instances.empty()) {
        widgets::empty_state("·", "No live data",
            "Launch an instance to see per-instance metrics.");
    } else {
        for (auto& [id, live] : s.instances) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::PANEL_HI);
            ImGui::BeginChild(("##diag_" + id).c_str(), {0, 0},
                ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT);
            ImGui::TextUnformatted(id.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 90);
            widgets::badge_runtime(live.state);

            float vw = ImGui::GetContentRegionAvail().x;
            float each = (vw - 8) / 2.0f;
            widgets::timeline_chart("RAM", live.ram_mb, "MB",  theme::ACCENT, {each, 70});
            ImGui::SameLine();
            widgets::timeline_chart("CPU", live.cpu_pct, "%",  theme::WARN,   {each, 70});

            if (!live.crash_reason.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::ERR);
                ImGui::TextWrapped("crash: %s", live.crash_reason.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
    }

    ImGui::Spacing();
    widgets::section_header("ROOT-CAUSE INFERENCE");
    if (s.errors.empty()) {
        widgets::dim_text("No recent errors. System nominal.");
    } else {
        int shown = 0;
        for (auto& e : s.errors) {
            ImGui::PushID(e.correlation_id.c_str());
            widgets::error_chip(e);
            const char* cause = infer_root_cause(e);
            if (cause) {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_Text, theme::WARN);
                ImGui::TextWrapped("→ %s", cause);
                ImGui::PopStyleColor();
                ImGui::Unindent();
            }
            ImGui::PopID();
            if (++shown >= 6) break;
        }
    }
    (void)launcher;
}

}
