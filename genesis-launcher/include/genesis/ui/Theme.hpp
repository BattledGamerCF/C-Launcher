#pragma once

#include <imgui.h>

namespace genesis::ui::theme {

extern const ImVec4 BG;
extern const ImVec4 BG_DEEP;
extern const ImVec4 PANEL;
extern const ImVec4 PANEL_HI;
extern const ImVec4 PANEL_DEEP;
extern const ImVec4 ACCENT;
extern const ImVec4 ACCENT_DIM;
extern const ImVec4 ACCENT_HOT;
extern const ImVec4 TEXT;
extern const ImVec4 TEXT_DIM;
extern const ImVec4 TEXT_FAINT;
extern const ImVec4 SUCCESS;
extern const ImVec4 SUCCESS_DIM;
extern const ImVec4 ERR;
extern const ImVec4 ERR_DIM;
extern const ImVec4 WARN;
extern const ImVec4 INFO;
extern const ImVec4 TRACE_;
extern const ImVec4 BORDER;
extern const ImVec4 BORDER_HI;

void apply();

ImU32 to_u32(const ImVec4& c);
ImVec4 with_alpha(const ImVec4& c, float a);
ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t);

}
