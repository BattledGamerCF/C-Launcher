#include "genesis/ui/Views.hpp"
#include "genesis/ui/UiState.hpp"
#include "genesis/ui/Theme.hpp"
#include "genesis/ui/Widgets.hpp"
#include "genesis/ui/Shell.hpp"
#include "genesis/ui/AsyncLauncher.hpp"
#include "genesis/core/Launcher.hpp"
#include "genesis/mods/PerformancePack.hpp"
#include "genesis/instance/InstanceManager.hpp"

#include <imgui.h>
#include <cstdio>

namespace genesis::ui::views {

namespace ust = genesis::ui::state;

static void draw_dependency_node(const char* label,
                                 const char* state_label,
                                 ImVec4 state_color) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::PANEL_HI);
    ImGui::BeginChild(label, {0, 0},
        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 90);
    widgets::badge(state_label, state_color, theme::with_alpha(state_color, 0.18f));
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void draw_no_instance_picker(genesis::core::Launcher& launcher) {
    widgets::section_header("MODPACKS");
    widgets::dim_text("Select an instance to view its modpack details.");
    ImGui::Spacing();

    auto& list = launcher.instance_manager().all();
    if (list.empty()) {
        widgets::empty_state("[]", "No instances yet",
                             "Create one from the Instances tab.");
        return;
    }
    for (auto& up : list) {
        auto& cfg = up->config();
        if (ImGui::Selectable(cfg.display_name.c_str()))
            ust::dispatch::select_modpack_instance(cfg.id);
        ImGui::SameLine(ImGui::GetWindowWidth() * 0.7f);
        widgets::dim_text("%s", cfg.game_version.c_str());
    }
}

void draw_modpack_view(genesis::core::Launcher& launcher) {
    auto& s = ust::global();
    auto& sel_id = s.selection.selected_modpack_instance_id;

    if (sel_id.empty()) {
        draw_no_instance_picker(launcher);
        return;
    }

    auto inst_opt = launcher.instance_manager().find(sel_id);
    if (!inst_opt) {
        widgets::section_header("MODPACKS");
        widgets::empty_state("·", "Selected instance no longer exists",
                             "Pick another from the Instances view.");
        return;
    }
    auto& inst = inst_opt->get();
    auto& cfg  = inst.config();

    widgets::section_header("MODPACK");
    ImGui::SameLine(ImGui::GetWindowWidth() - 230);
    if (ImGui::Button("Reinstall pack", {200, 24}))
        shell::async_launcher().start_install_modpack(sel_id);

    widgets::kv_row("Instance",        cfg.display_name.c_str());
    widgets::kv_row("Game version",    cfg.game_version.c_str());

    bool eligible = mods::PerformancePack::qualifies(cfg.game_version);
    ImGui::Spacing();
    if (eligible) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::SUCCESS);
        ImGui::TextWrapped("Compatible with the Genesis performance pack.");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::WARN);
        ImGui::TextWrapped("Not in the supported window. Pack auto-install requires release 1.21.11+.");
        ImGui::PopStyleColor();
    }

    auto mp_it = s.modpacks.find(sel_id);
    if (mp_it != s.modpacks.end()) {
        auto& mp = mp_it->second;

        ImGui::Spacing();
        widgets::section_header("INSTALL STATUS");
        widgets::badge_async(mp.install_status);
        if (mp.install_status == ust::AsyncStatus::Pending) {
            ImGui::Spacing();
            widgets::progress(mp.install_progress, mp.current_step.c_str(),
                              {-1, 18}, theme::ACCENT, theme::PANEL_DEEP);
        }

        if (!mp.fabric_loader_version.empty()) {
            widgets::kv_row("Fabric loader", mp.fabric_loader_version.c_str());
        }

        if (mp.last_error.has_value()) {
            ImGui::Spacing();
            widgets::error_panel(*mp.last_error);
        }
    }

    ImGui::Spacing();
    widgets::section_header("DEPENDENCY GRAPH");
    widgets::faint_text("Genesis ships these mods on-Fabric for eligible versions:");

    static const struct { const char* slug; const char* name; } core[] = {
        {"sodium",        "Sodium"        },
        {"sodium-extra",  "Sodium Extra"  },
        {"lithium",       "Lithium"       },
        {"iris",          "Iris Shaders"  },
    };

    auto status_for = [&](const char* name) -> std::pair<const char*, ImVec4> {
        if (mp_it == s.modpacks.end()) return {"queued", theme::TEXT_DIM};
        auto& mp = mp_it->second;
        for (auto& n : mp.installed) if (n == name) return {"installed", theme::SUCCESS};
        for (auto& n : mp.failed)    if (n == name) return {"failed",    theme::ERR};
        for (auto& n : mp.skipped)   if (n == name) return {"skipped",   theme::WARN};
        return {mp.install_status == ust::AsyncStatus::Pending ? "syncing" : "queued",
                theme::TEXT_DIM};
    };

    for (auto& m : core) {
        auto [lbl, col] = status_for(m.name);
        draw_dependency_node(m.name, lbl, col);
        ImGui::Indent();
        widgets::faint_text("requires: fabric-loader · target: %s", cfg.game_version.c_str());
        ImGui::Unindent();
        ImGui::Dummy({0, 4});
    }

    if (mp_it != s.modpacks.end()) {
        auto& mp = mp_it->second;
        if (!mp.failed.empty()) {
            ImGui::Spacing();
            widgets::section_header("FAILED MODS");
            for (auto& f : mp.failed) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::ERR);
                ImGui::Bullet();
                ImGui::TextUnformatted(f.c_str());
                ImGui::PopStyleColor();
            }
        }
    }
}

}
