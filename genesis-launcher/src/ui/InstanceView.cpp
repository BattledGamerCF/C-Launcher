#include "genesis/ui/InstanceView.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include <imgui.h>

namespace genesis::ui {

void draw_instance_detail(const instance::Instance& inst) {
    auto& cfg = inst.config();

    ImGui::SeparatorText(cfg.display_name.c_str());
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, {0.55f, 0.55f, 0.60f, 1.0f});
    ImGui::Text("Version:   %s", cfg.game_version.c_str());
    ImGui::Text("Directory: %s", inst.root_dir().c_str());

    uint64_t h = cfg.total_play_seconds / 3600;
    uint64_t m = (cfg.total_play_seconds % 3600) / 60;
    ImGui::Text("Play Time: %lluh %02llum", (unsigned long long)h, (unsigned long long)m);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    if (ImGui::Button("Open Folder")) {
        platform::open_in_file_manager(inst.root_dir());
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Mods Folder")) {
        platform::open_in_file_manager(inst.mods_dir());
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Saves Folder")) {
        platform::open_in_file_manager(inst.saves_dir());
    }
}

}
