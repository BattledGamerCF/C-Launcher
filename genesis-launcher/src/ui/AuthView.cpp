#include "genesis/ui/AuthView.hpp"
#include "genesis/core/EventBus.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include <imgui.h>

namespace genesis::ui {

void draw_device_code_modal(const auth::DeviceCodeInfo& info, bool& open) {
    if (!open) return;

    ImGui::OpenPopup("Microsoft Sign In");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({440, 260});

    if (ImGui::BeginPopupModal("Microsoft Sign In", &open, ImGuiWindowFlags_NoResize)) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, {0.92f, 0.92f, 0.95f, 1.0f});
        ImGui::TextWrapped("To sign in, visit the following URL and enter the code below:");
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, {0.55f, 0.55f, 0.60f, 1.0f});
        ImGui::TextUnformatted(info.verification_uri.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::SetWindowFontScale(1.8f);
        ImGui::PushStyleColor(ImGuiCol_Text, {0.24f, 0.52f, 0.88f, 1.0f});
        ImGui::Text("  %s", info.user_code.c_str());
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, {0.55f, 0.55f, 0.60f, 1.0f});
        ImGui::Text("Expires in %d seconds.", info.expires_in_seconds);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Open Browser & Copy Code", {200, 28})) {
            platform::open_url_in_browser(info.verification_uri);
            platform::copy_to_clipboard(info.user_code);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {70, 28}))
            open = false;

        ImGui::EndPopup();
    }
}

}
