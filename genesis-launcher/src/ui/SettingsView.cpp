#include "genesis/ui/SettingsView.hpp"
#include <imgui.h>

namespace genesis::ui {

void draw_jvm_profile_editor(jvm::JvmProfile& profile, bool& changed) {
    changed = false;
    ImGui::Text("Profile: %s", profile.display_name.c_str());
    ImGui::Spacing();

    ImGui::Text("Min Memory (MB)");
    if (ImGui::SliderInt("##min_mem", &profile.memory.min_mb, 256, 4096))
        changed = true;

    ImGui::Text("Max Memory (MB)");
    if (ImGui::SliderInt("##max_mem", &profile.memory.max_mb, 512, 16384))
        changed = true;

    if (profile.memory.max_mb < profile.memory.min_mb) {
        ImGui::PushStyleColor(ImGuiCol_Text, {0.90f, 0.30f, 0.30f, 1.0f});
        ImGui::TextUnformatted("  Max must be >= Min");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Text("Garbage Collector");
    const char* gc_names[] = {"G1GC", "ZGC", "ShenandoahGC", "SerialGC", "ParallelGC", "Default"};
    int gc_idx = static_cast<int>(profile.gc_strategy);
    if (ImGui::Combo("##gc", &gc_idx, gc_names, 6)) {
        profile.gc_strategy = static_cast<jvm::GcStrategy>(gc_idx);
        changed = true;
    }
}

}
