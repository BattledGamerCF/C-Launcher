#include "genesis/ui/Theme.hpp"

namespace genesis::ui::theme {

const ImVec4 BG          = {0.07f, 0.08f, 0.10f, 1.0f};
const ImVec4 BG_DEEP     = {0.05f, 0.05f, 0.07f, 1.0f};
const ImVec4 PANEL       = {0.10f, 0.11f, 0.14f, 1.0f};
const ImVec4 PANEL_HI    = {0.13f, 0.14f, 0.18f, 1.0f};
const ImVec4 PANEL_DEEP  = {0.08f, 0.09f, 0.11f, 1.0f};
const ImVec4 ACCENT      = {0.30f, 0.66f, 0.96f, 1.0f};
const ImVec4 ACCENT_DIM  = {0.20f, 0.46f, 0.74f, 1.0f};
const ImVec4 ACCENT_HOT  = {0.42f, 0.80f, 1.00f, 1.0f};
const ImVec4 TEXT        = {0.92f, 0.93f, 0.96f, 1.0f};
const ImVec4 TEXT_DIM    = {0.62f, 0.64f, 0.70f, 1.0f};
const ImVec4 TEXT_FAINT  = {0.40f, 0.42f, 0.48f, 1.0f};
const ImVec4 SUCCESS     = {0.40f, 0.82f, 0.50f, 1.0f};
const ImVec4 SUCCESS_DIM = {0.20f, 0.50f, 0.30f, 1.0f};
const ImVec4 ERR         = {0.94f, 0.36f, 0.36f, 1.0f};
const ImVec4 ERR_DIM     = {0.64f, 0.20f, 0.20f, 1.0f};
const ImVec4 WARN        = {0.98f, 0.78f, 0.28f, 1.0f};
const ImVec4 INFO        = {0.55f, 0.78f, 0.95f, 1.0f};
const ImVec4 TRACE_      = {0.55f, 0.55f, 0.65f, 1.0f};
const ImVec4 BORDER      = {0.18f, 0.19f, 0.24f, 1.0f};
const ImVec4 BORDER_HI   = {0.28f, 0.30f, 0.36f, 1.0f};

ImU32 to_u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

ImVec4 with_alpha(const ImVec4& c, float a) { return {c.x, c.y, c.z, a}; }

ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t,
    };
}

void apply() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding     = 6.0f;
    s.ChildRounding      = 5.0f;
    s.FrameRounding      = 4.0f;
    s.GrabRounding       = 4.0f;
    s.TabRounding        = 4.0f;
    s.PopupRounding      = 6.0f;
    s.ScrollbarRounding  = 6.0f;
    s.WindowBorderSize   = 0.0f;
    s.ChildBorderSize    = 1.0f;
    s.FrameBorderSize    = 0.0f;
    s.PopupBorderSize    = 1.0f;
    s.FramePadding       = {10.0f, 5.0f};
    s.ItemSpacing        = {8.0f, 6.0f};
    s.ItemInnerSpacing   = {6.0f, 4.0f};
    s.IndentSpacing      = 14.0f;
    s.ScrollbarSize      = 12.0f;
    s.GrabMinSize        = 10.0f;
    s.WindowPadding      = {12.0f, 10.0f};
    s.CellPadding        = {8.0f, 4.0f};

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]            = BG;
    c[ImGuiCol_ChildBg]             = PANEL;
    c[ImGuiCol_PopupBg]             = PANEL_HI;
    c[ImGuiCol_Border]              = BORDER;
    c[ImGuiCol_BorderShadow]        = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg]             = PANEL_HI;
    c[ImGuiCol_FrameBgHovered]      = with_alpha(ACCENT, 0.20f);
    c[ImGuiCol_FrameBgActive]       = with_alpha(ACCENT, 0.35f);
    c[ImGuiCol_TitleBg]             = PANEL;
    c[ImGuiCol_TitleBgActive]       = PANEL;
    c[ImGuiCol_TitleBgCollapsed]    = PANEL_DEEP;
    c[ImGuiCol_MenuBarBg]           = PANEL;
    c[ImGuiCol_ScrollbarBg]         = PANEL_DEEP;
    c[ImGuiCol_ScrollbarGrab]       = {0.30f, 0.32f, 0.38f, 1.0f};
    c[ImGuiCol_ScrollbarGrabHovered]= {0.42f, 0.44f, 0.50f, 1.0f};
    c[ImGuiCol_ScrollbarGrabActive] = ACCENT;
    c[ImGuiCol_CheckMark]           = ACCENT;
    c[ImGuiCol_SliderGrab]          = ACCENT;
    c[ImGuiCol_SliderGrabActive]    = ACCENT_HOT;
    c[ImGuiCol_Button]              = with_alpha(ACCENT, 0.18f);
    c[ImGuiCol_ButtonHovered]       = with_alpha(ACCENT, 0.40f);
    c[ImGuiCol_ButtonActive]        = with_alpha(ACCENT, 0.65f);
    c[ImGuiCol_Header]              = with_alpha(ACCENT, 0.20f);
    c[ImGuiCol_HeaderHovered]       = with_alpha(ACCENT, 0.40f);
    c[ImGuiCol_HeaderActive]        = with_alpha(ACCENT, 0.55f);
    c[ImGuiCol_Tab]                 = PANEL;
    c[ImGuiCol_TabHovered]          = with_alpha(ACCENT, 0.40f);
    c[ImGuiCol_TabActive]           = ACCENT_DIM;
    c[ImGuiCol_TabUnfocused]        = PANEL_DEEP;
    c[ImGuiCol_TabUnfocusedActive]  = with_alpha(ACCENT_DIM, 0.6f);
    c[ImGuiCol_Text]                = TEXT;
    c[ImGuiCol_TextDisabled]        = TEXT_FAINT;
    c[ImGuiCol_TextSelectedBg]      = with_alpha(ACCENT, 0.45f);
    c[ImGuiCol_Separator]           = BORDER;
    c[ImGuiCol_SeparatorHovered]    = ACCENT;
    c[ImGuiCol_SeparatorActive]     = ACCENT_HOT;
    c[ImGuiCol_ResizeGrip]          = with_alpha(ACCENT, 0.20f);
    c[ImGuiCol_ResizeGripHovered]   = with_alpha(ACCENT, 0.50f);
    c[ImGuiCol_ResizeGripActive]    = ACCENT;
    c[ImGuiCol_PlotLines]           = ACCENT;
    c[ImGuiCol_PlotLinesHovered]    = ACCENT_HOT;
    c[ImGuiCol_PlotHistogram]       = ACCENT;
    c[ImGuiCol_PlotHistogramHovered]= ACCENT_HOT;
    c[ImGuiCol_TableBorderStrong]   = BORDER;
    c[ImGuiCol_TableBorderLight]    = {0.13f, 0.14f, 0.18f, 1.0f};
    c[ImGuiCol_TableRowBg]          = {0, 0, 0, 0};
    c[ImGuiCol_TableRowBgAlt]       = {1.0f, 1.0f, 1.0f, 0.025f};
    c[ImGuiCol_TableHeaderBg]       = PANEL_HI;
    c[ImGuiCol_NavHighlight]        = ACCENT;
    c[ImGuiCol_DragDropTarget]      = ACCENT_HOT;
    c[ImGuiCol_ModalWindowDimBg]    = {0.0f, 0.0f, 0.0f, 0.55f};
}

}
