#include "genesis/ui/Views.hpp"
#include "genesis/ui/UiState.hpp"
#include "genesis/ui/Theme.hpp"
#include "genesis/ui/Widgets.hpp"
#include "genesis/ui/Shell.hpp"
#include "genesis/ui/AsyncLauncher.hpp"
#include "genesis/ui/SettingsView.hpp"   // existing draw_jvm_profile_editor
#include "genesis/core/Launcher.hpp"
#include "genesis/jvm/JvmOrchestrator.hpp"
#include "genesis/platform/PlatformUtils.hpp"

#include <imgui.h>
#include <cstdio>

namespace genesis::ui::views {

namespace ust = genesis::ui::state;

static int g_section = 0;   // 0=Account, 1=Runtime, 2=Updates, 3=About

static void section_account(genesis::core::Launcher& launcher) {
    widgets::section_header("ACCOUNT");
    auto& s = ust::global();
    if (s.auth.authenticated) {
        widgets::kv_row("Status",   "Signed in");
        widgets::kv_row("Username", s.auth.username.c_str());
        widgets::kv_row("UUID",     s.auth.uuid.empty() ? "(unknown)" : s.auth.uuid.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Sign out", {140, 28}))
            shell::async_launcher().start_logout();
    } else {
        widgets::dim_text("You are not signed in.");
        ImGui::Spacing();
        if (ImGui::Button("Sign in with Microsoft", {220, 30}))
            shell::async_launcher().start_login();
    }

    if (s.auth.op.status == ust::AsyncStatus::Pending) {
        ImGui::Spacing();
        widgets::badge_async(s.auth.op.status);
        ImGui::SameLine();
        widgets::dim_text("%s", s.auth.op.label.c_str());
    } else if (s.auth.op.status == ust::AsyncStatus::Error && s.auth.op.error.has_value()) {
        ImGui::Spacing();
        widgets::error_panel(*s.auth.op.error);
    }
    (void)launcher;
}

static void section_runtime(genesis::core::Launcher& launcher) {
    widgets::section_header("JVM PROFILES");
    auto profiles = launcher.jvm_orchestrator().list_profiles();
    if (profiles.empty()) {
        widgets::dim_text("No JVM profiles configured.");
        if (ImGui::Button("Create default", {160, 26})) {
            auto p = launcher.jvm_orchestrator().get_or_create_default();
            launcher.jvm_orchestrator().save_profile(p);
        }
        return;
    }

    auto& s = ust::global();
    if (s.selection.selected_jvm_profile_id.empty())
        s.selection.selected_jvm_profile_id = profiles.front().id;

    // Two-column layout: list | editor
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##jvm_list", {w * 0.30f, 0}, true);
    for (auto& p : profiles) {
        bool sel = (s.selection.selected_jvm_profile_id == p.id);
        if (ImGui::Selectable(p.display_name.c_str(), sel))
            ust::dispatch::select_jvm_profile(p.id);
        if (p.is_default) {
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 60);
            widgets::badge("default", theme::SUCCESS,
                           theme::with_alpha(theme::SUCCESS, 0.18f));
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##jvm_edit", {0, 0}, true);
    for (auto& p : profiles) {
        if (p.id != s.selection.selected_jvm_profile_id) continue;
        // Reuse the existing editor helper from SettingsView.cpp
        bool changed = false;
        auto copy = p;
        ui::draw_jvm_profile_editor(copy, changed);
        if (changed) launcher.jvm_orchestrator().save_profile(copy);
        break;
    }
    ImGui::EndChild();
}

static void section_updates(genesis::core::Launcher& launcher) {
    widgets::section_header("UPDATES");
    auto& s = ust::global();

    widgets::kv_row("Current", GENESIS_VERSION_STRING);
    if (s.update_available) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::WARN);
        ImGui::TextWrapped("Update available: %s", s.update_version.c_str());
        ImGui::PopStyleColor();
    } else {
        widgets::dim_text("No update available.");
    }

    auto op_it = s.ops.find("update:check");
    bool checking = (op_it != s.ops.end() &&
                     op_it->second.status == ust::AsyncStatus::Pending);

    ImGui::Spacing();
    if (checking) ImGui::BeginDisabled();
    if (ImGui::Button(checking ? "Checking..." : "Check now", {140, 28}))
        shell::async_launcher().start_check_update();
    if (checking) ImGui::EndDisabled();

    if (op_it != s.ops.end() &&
        op_it->second.status == ust::AsyncStatus::Error &&
        op_it->second.error.has_value()) {
        ImGui::Spacing();
        widgets::error_panel(*op_it->second.error);
    }
    (void)launcher;
}

static void section_about(genesis::core::Launcher& /*launcher*/) {
    widgets::section_header("ABOUT");
    ImGui::PushStyleColor(ImGuiCol_Text, theme::ACCENT);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("Genesis Launcher");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    widgets::dim_text("v" GENESIS_VERSION_STRING);
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Genesis is a production-grade Minecraft control plane. It uses a "
        "centralized state model, async worker tasks, streaming logs, live "
        "process telemetry and structured error reporting.");
    ImGui::Spacing();

    widgets::section_header("PATHS");
    widgets::kv_row("Game dir",  genesis::platform::default_game_dir().c_str());
    widgets::kv_row("OS",        genesis::platform::os_name().c_str());
    widgets::kv_row("Arch",      genesis::platform::os_arch().c_str());
    widgets::kv_row_fmt("RAM (system)", "%lld MB",
                        (long long)genesis::platform::total_memory_mb());
    widgets::kv_row_fmt("CPU cores", "%d",
                        genesis::platform::cpu_core_count());
}

void draw_settings_tab(genesis::core::Launcher& launcher) {
    widgets::section_header("SETTINGS");
    int chosen = widgets::segmented("##settings_seg",
        {"Account", "Runtime", "Updates", "About"}, g_section);
    if (chosen != g_section) g_section = chosen;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    switch (g_section) {
        case 0: section_account(launcher);  break;
        case 1: section_runtime(launcher);  break;
        case 2: section_updates(launcher);  break;
        case 3: section_about(launcher);    break;
    }
}

}
