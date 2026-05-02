#include "genesis/ui/Views.hpp"
#include "genesis/ui/UiState.hpp"
#include "genesis/ui/Theme.hpp"
#include "genesis/ui/Widgets.hpp"
#include "genesis/ui/Shell.hpp"
#include "genesis/ui/AsyncLauncher.hpp"
#include "genesis/core/Launcher.hpp"
#include "genesis/version/VersionManager.hpp"
#include "genesis/version/VersionManifest.hpp"
#include "genesis/mods/PerformancePack.hpp"

#include <imgui.h>
#include <vector>
#include <cstdio>

namespace genesis::ui::views {

namespace ust = genesis::ui::state;

static ImVec4 color_for_type(genesis::version::ReleaseType t) {
    using T = genesis::version::ReleaseType;
    switch (t) {
        case T::Release:  return theme::SUCCESS;
        case T::Snapshot: return theme::INFO;
        case T::OldBeta:  return theme::WARN;
        case T::OldAlpha: return theme::ERR;
        case T::Unknown:  break;
    }
    return theme::TEXT_DIM;
}

void draw_version_view(genesis::core::Launcher& launcher) {
    auto& s = ust::global();

    widgets::section_header("VERSIONS");
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);
    if (ImGui::SmallButton("Refresh manifest"))
        launcher.version_manager().fetch_version_list(true);

    auto list_res = launcher.version_manager().fetch_version_list(false);
    if (list_res.is_err()) {
        widgets::error_panel(ust::make_error("version", "VER-001",
            "Could not fetch version manifest",
            list_res.error().full(),
            "Check your network connection then click 'Refresh manifest'."));
        return;
    }
    auto& list = list_res.value();

    widgets::kv_row("Latest release",  list.latest_release.c_str());
    widgets::kv_row("Latest snapshot", list.latest_snapshot.c_str());
    widgets::kv_row_fmt("Total versions", "%zu", list.versions.size());

    ImGui::Spacing();
    widgets::section_header("COMPATIBILITY HEATMAP");
    widgets::faint_text("Color reflects release type. Eligible auto-pack rows show a chevron.");

    // Filter row
    static int filter_type = 0;   // 0=all,1=release,2=snapshot,3=old
    int chosen = widgets::segmented("##vfilter",
        {"all", "release", "snapshot", "old"}, filter_type);
    if (chosen != filter_type) filter_type = chosen;

    static char search_buf[64] = {};
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##vsearch", "search id", search_buf, sizeof(search_buf));

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::PANEL_DEEP);
    ImGui::BeginChild("##vlist", {0, 0}, true);

    ImGuiListClipper clip;
    // Pre-filter
    static std::vector<int> filtered;
    filtered.clear();
    for (size_t i = 0; i < list.versions.size(); ++i) {
        auto& v = list.versions[i];
        bool keep = true;
        switch (filter_type) {
            case 1: keep = (v.type == genesis::version::ReleaseType::Release);  break;
            case 2: keep = (v.type == genesis::version::ReleaseType::Snapshot); break;
            case 3: keep = (v.type == genesis::version::ReleaseType::OldBeta ||
                            v.type == genesis::version::ReleaseType::OldAlpha); break;
            default: break;
        }
        if (keep && search_buf[0] != '\0') {
            keep = (v.id.find(search_buf) != std::string::npos);
        }
        if (keep) filtered.push_back(static_cast<int>(i));
    }

    clip.Begin(static_cast<int>(filtered.size()));
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            auto& v = list.versions[filtered[i]];
            bool selected = (s.selection.selected_version_id == v.id);
            ImVec4 col = color_for_type(v.type);

            // Heatmap dot
            auto* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            float r = 6.0f;
            dl->AddRectFilled({p.x, p.y + 4}, {p.x + 8, p.y + 18},
                              theme::to_u32(theme::with_alpha(col, 0.85f)), 2.0f);
            ImGui::Dummy({14, 0});
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                theme::with_alpha(theme::ACCENT, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                theme::with_alpha(theme::ACCENT, 0.45f));

            char label[256];
            std::snprintf(label, sizeof(label), "%-22s  %-9s  %-22s",
                          v.id.c_str(),
                          genesis::version::release_type_to_string(v.type).c_str(),
                          v.release_time.c_str());
            if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns))
                ust::dispatch::select_version(v.id);
            ImGui::PopStyleColor(2);

            // Eligible chevron
            if (genesis::mods::PerformancePack::qualifies(v.id)) {
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 70);
                widgets::badge("AUTO PACK", theme::SUCCESS,
                               theme::with_alpha(theme::SUCCESS, 0.18f));
            }
            (void)r;
        }
    }
    clip.End();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

}
