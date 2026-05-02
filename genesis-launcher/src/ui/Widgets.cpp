#include "genesis/ui/Widgets.hpp"
#include "genesis/ui/Theme.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdarg>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace genesis::ui::widgets {

// ─── Badge ───────────────────────────────────────────────────────────────────
void badge(const char* text, ImVec4 fg, ImVec4 bg) {
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    ImVec2 sz = ImGui::CalcTextSize(text);
    ImVec2 pad{6, 2};
    ImVec2 box{sz.x + pad.x * 2, sz.y + pad.y * 2};
    ImVec2 p  = ImGui::GetCursorScreenPos();
    auto* dl  = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + box.x, p.y + box.y}, theme::to_u32(bg), 4.0f);
    dl->AddRect      (p, {p.x + box.x, p.y + box.y}, theme::to_u32(theme::with_alpha(fg, 0.30f)), 4.0f);
    ImGui::SetCursorScreenPos({p.x + pad.x, p.y + pad.y});
    ImGui::TextUnformatted(text);
    ImGui::SetCursorScreenPos({p.x + box.x, p.y});
    ImGui::Dummy({0, box.y});
    ImGui::PopStyleColor();
}

void badge_runtime(state::InstanceRuntimeState s) {
    using S = state::InstanceRuntimeState;
    ImVec4 fg, bg;
    switch (s) {
        case S::Stopped:  fg = theme::TEXT_DIM; bg = theme::with_alpha(theme::TEXT_DIM, 0.18f); break;
        case S::Syncing:  fg = theme::INFO;     bg = theme::with_alpha(theme::INFO,     0.18f); break;
        case S::Starting: fg = theme::WARN;     bg = theme::with_alpha(theme::WARN,     0.20f); break;
        case S::Running:  fg = theme::SUCCESS;  bg = theme::with_alpha(theme::SUCCESS,  0.20f); break;
        case S::Stopping: fg = theme::WARN;     bg = theme::with_alpha(theme::WARN,     0.20f); break;
        case S::Crashed:  fg = theme::ERR;      bg = theme::with_alpha(theme::ERR,      0.22f); break;
    }
    badge(state::runtime_label(s), fg, bg);
}

void badge_async(state::AsyncStatus s) {
    using A = state::AsyncStatus;
    ImVec4 fg, bg;
    switch (s) {
        case A::Idle:    fg = theme::TEXT_DIM; bg = theme::with_alpha(theme::TEXT_DIM, 0.15f); break;
        case A::Pending: fg = theme::WARN;     bg = theme::with_alpha(theme::WARN,     0.20f); break;
        case A::Success: fg = theme::SUCCESS;  bg = theme::with_alpha(theme::SUCCESS,  0.20f); break;
        case A::Error:   fg = theme::ERR;      bg = theme::with_alpha(theme::ERR,      0.22f); break;
    }
    badge(state::status_label(s), fg, bg);
}

void badge_log_level(logging::Level lv) {
    ImVec4 fg, bg;
    const char* lbl = "?";
    switch (lv) {
        case logging::Level::Trace:    fg = theme::TRACE_;  lbl = "TRACE"; break;
        case logging::Level::Debug:    fg = theme::TRACE_;  lbl = "DEBUG"; break;
        case logging::Level::Info:     fg = theme::INFO;    lbl = "INFO";  break;
        case logging::Level::Warn:     fg = theme::WARN;    lbl = "WARN";  break;
        case logging::Level::Error:    fg = theme::ERR;     lbl = "ERROR"; break;
        case logging::Level::Critical: fg = theme::ERR;     lbl = "CRIT";  break;
    }
    bg = theme::with_alpha(fg, 0.18f);
    badge(lbl, fg, bg);
}

void status_dot(ImVec4 color, const char* label) {
    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float r = ImGui::GetTextLineHeight() * 0.30f;
    p.y += ImGui::GetTextLineHeight() * 0.5f;
    dl->AddCircleFilled({p.x + r + 2, p.y}, r, theme::to_u32(color));
    ImGui::SetCursorScreenPos({p.x + r * 2 + 8, p.y - ImGui::GetTextLineHeight() * 0.5f});
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

void section_header(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::ACCENT);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    dl->AddLine({p.x, p.y - 2}, {p.x + w, p.y - 2}, theme::to_u32(theme::BORDER), 1.0f);
    ImGui::Dummy({0, 4});
}

void sparkline(const char* id,
               const std::vector<float>& values,
               ImVec4 color,
               ImVec2 size,
               float min_value,
               float max_value) {
    if (values.empty()) {
        ImGui::Dummy(size);
        return;
    }
    if (min_value == 0.0f && max_value == 0.0f) {
        min_value = *std::min_element(values.begin(), values.end());
        max_value = *std::max_element(values.begin(), values.end());
        if (max_value - min_value < 1e-3f) max_value = min_value + 1.0f;
    }
    ImGui::PushStyleColor(ImGuiCol_PlotLines,        color);
    ImGui::PushStyleColor(ImGuiCol_PlotLinesHovered, theme::ACCENT_HOT);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,          theme::PANEL_DEEP);
    ImGui::PlotLines(id, values.data(), static_cast<int>(values.size()),
                     0, nullptr, min_value, max_value, size);
    ImGui::PopStyleColor(3);
}

void progress(float fraction, const char* overlay, ImVec2 size, ImVec4 fg, ImVec4 bg) {
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, fg);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       bg);
    ImGui::ProgressBar(fraction, size, overlay);
    ImGui::PopStyleColor(2);
}

void dim_text(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
}

void faint_text(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_FAINT);
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
}

void error_panel(const state::ErrorRecord& err, bool* clicked) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::with_alpha(theme::ERR, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_Border,  theme::with_alpha(theme::ERR, 0.55f));
    ImGui::BeginChild((std::string("##err_") + err.correlation_id).c_str(),
                      {0, 0}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border);

    ImGui::PushStyleColor(ImGuiCol_Text, theme::ERR);
    ImGui::Text("[%s] %s",
                err.code.empty() ? "GEN-000" : err.code.c_str(),
                err.subsystem.empty() ? "system" : err.subsystem.c_str());
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT);
    ImGui::TextWrapped("%s", err.message.c_str());
    ImGui::PopStyleColor();

    if (!err.detail.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
        ImGui::TextWrapped("%s", err.detail.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_FAINT);
    if (!err.correlation_id.empty())
        ImGui::Text("correlation: %s", err.correlation_id.c_str());
    ImGui::PopStyleColor();

    if (!err.suggested_action.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::WARN);
        ImGui::TextWrapped("Suggestion: %s", err.suggested_action.c_str());
        ImGui::PopStyleColor();
    }

    if (clicked && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0))
        *clicked = true;

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void error_chip(const state::ErrorRecord& err) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::ERR);
    ImGui::Text("%s", err.code.empty() ? "GEN-000" : err.code.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
    ImGui::Text("[%s]", err.subsystem.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(err.message.c_str());
}

ImVec4 pulse(const ImVec4& base, float speed) {
    using namespace std::chrono;
    double t = duration<double>(steady_clock::now().time_since_epoch()).count();
    float k = 0.6f + 0.4f * static_cast<float>(0.5 + 0.5 * std::sin(t * speed));
    return {base.x * k, base.y * k, base.z * k, base.w};
}

void kv_row(const char* key, const char* value) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
    ImGui::TextUnformatted(key);
    ImGui::PopStyleColor();
    ImGui::SameLine(120.0f);
    ImGui::TextUnformatted(value);
}

void kv_row_fmt(const char* key, const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kv_row(key, buf);
}

bool icon_button(const char* glyph, const char* tooltip, ImVec2 size) {
    bool pressed = ImGui::Button(glyph, size);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

bool toggle(const char* label, bool* value) {
    ImGui::PushStyleColor(ImGuiCol_Button,
        *value ? theme::ACCENT_DIM : theme::PANEL_HI);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        *value ? theme::ACCENT : theme::with_alpha(theme::ACCENT, 0.30f));
    bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(2);
    if (clicked) *value = !*value;
    return clicked;
}

int segmented(const char* id, const std::vector<const char*>& options, int current) {
    int chosen = current;
    ImGui::PushID(id);
    for (size_t i = 0; i < options.size(); ++i) {
        if (i > 0) ImGui::SameLine(0, 2);
        bool active = (current == static_cast<int>(i));
        ImGui::PushStyleColor(ImGuiCol_Button,
            active ? theme::ACCENT_DIM : theme::PANEL_HI);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            active ? theme::ACCENT : theme::with_alpha(theme::ACCENT, 0.30f));
        if (ImGui::Button(options[i])) chosen = static_cast<int>(i);
        ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
    return chosen;
}

void timeline_chart(const char* title,
                    const std::vector<float>& values,
                    const char* unit,
                    ImVec4 color,
                    ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (values.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_FAINT);
        ImGui::TextUnformatted("no data");
        ImGui::PopStyleColor();
        ImGui::Dummy(size);
        return;
    }
    float vmin = *std::min_element(values.begin(), values.end());
    float vmax = *std::max_element(values.begin(), values.end());
    if (vmax - vmin < 1e-3f) vmax = vmin + 1.0f;

    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%.1f %s (peak %.1f)",
                  values.back(), unit, vmax);
    ImGui::PushStyleColor(ImGuiCol_PlotLines,        color);
    ImGui::PushStyleColor(ImGuiCol_PlotLinesHovered, theme::ACCENT_HOT);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,          theme::PANEL_DEEP);
    ImGui::PlotLines((std::string("##") + title).c_str(),
                     values.data(), static_cast<int>(values.size()),
                     0, overlay, vmin * 0.9f, vmax * 1.1f, size);
    ImGui::PopStyleColor(3);
}

void live_dot() {
    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float r = ImGui::GetTextLineHeight() * 0.32f;
    p.x += r;
    p.y += ImGui::GetTextLineHeight() * 0.5f;
    ImVec4 col = pulse(theme::SUCCESS, 3.0f);
    dl->AddCircleFilled({p.x, p.y}, r, theme::to_u32(col));
    ImGui::Dummy({r * 2 + 4, ImGui::GetTextLineHeight()});
}

void empty_state(const char* glyph, const char* title, const char* hint) {
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetContentRegionAvail().y;
    ImGui::Dummy({0, h * 0.30f});
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_FAINT);
    float gw = ImGui::CalcTextSize(glyph).x;
    ImGui::SetCursorPosX((w - gw) * 0.5f);
    ImGui::SetWindowFontScale(2.4f);
    ImGui::TextUnformatted(glyph);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_DIM);
    float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((w - tw) * 0.5f);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (hint && hint[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_FAINT);
        float hw = ImGui::CalcTextSize(hint).x;
        ImGui::SetCursorPosX((w - hw) * 0.5f);
        ImGui::TextUnformatted(hint);
        ImGui::PopStyleColor();
    }
}

}
