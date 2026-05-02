#include "genesis/ui/DownloadView.hpp"
#include <imgui.h>
#include <string>

namespace genesis::ui {

void draw_download_overlay(const std::string& label, float fraction) {
    if (fraction <= 0.0f || fraction >= 1.0f) return;

    ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos({vp.x * 0.25f, vp.y * 0.44f});
    ImGui::SetNextWindowSize({vp.x * 0.50f, 80});
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##dl_overlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove);

    ImGui::PushStyleColor(ImGuiCol_Text, {0.55f, 0.55f, 0.60f, 1.0f});
    ImGui::TextUnformatted(label.c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, {0.24f, 0.52f, 0.88f, 1.0f});
    ImGui::ProgressBar(fraction, {-1, 20});
    ImGui::PopStyleColor();
    ImGui::End();
}

}
