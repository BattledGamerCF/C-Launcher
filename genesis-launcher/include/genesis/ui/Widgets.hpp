#pragma once

#include "genesis/ui/UiState.hpp"
#include "genesis/logging/Logger.hpp"
#include <imgui.h>
#include <string>
#include <vector>

namespace genesis::ui::widgets {

// Small rounded label used for runtime state (running/stopped/syncing/...).
void badge(const char* text, ImVec4 fg, ImVec4 bg);

// Convenience wrappers
void badge_runtime(state::InstanceRuntimeState s);
void badge_async(state::AsyncStatus s);
void badge_log_level(logging::Level l);

// Filled circle status indicator + label
void status_dot(ImVec4 color, const char* label);

// Section heading with thin underline
void section_header(const char* label);

// Sparkline plot: tiny chart suitable for a card.
void sparkline(const char* id,
               const std::vector<float>& values,
               ImVec4 color,
               ImVec2 size,
               float min_value = 0.0f,
               float max_value = 0.0f);   // 0,0 = autoscale

// Linear progress bar with custom colors.
void progress(float fraction,
              const char* overlay,
              ImVec2 size,
              ImVec4 fg,
              ImVec4 bg);

// Wrapped text in a dim color
void dim_text(const char* fmt, ...);
void faint_text(const char* fmt, ...);

// Structured error panel — code, subsystem, correlation ID, suggested action.
// The spec forbids "something went wrong" messages; this widget enforces
// structure.
void error_panel(const state::ErrorRecord& err, bool* clicked = nullptr);

// Inline error chip (single-line, used in lists).
void error_chip(const state::ErrorRecord& err);

// Pulsing color: smoothly modulates `base` between 60% and 100% intensity.
ImVec4 pulse(const ImVec4& base, float speed = 2.0f);

// A right-aligned key/value row for inspector panels.
void kv_row(const char* key, const char* value);
void kv_row_fmt(const char* key, const char* fmt, ...);

// Tiny clickable icon button with tooltip.
bool icon_button(const char* glyph, const char* tooltip, ImVec2 size = {28, 24});

// Toggle button — visually distinct on/off state.
bool toggle(const char* label, bool* value);

// Segmented control (tab-like row)
int segmented(const char* id, const std::vector<const char*>& options, int current);

// Mini chart panel — large variant with labels.
void timeline_chart(const char* title,
                    const std::vector<float>& values,
                    const char* unit,
                    ImVec4 color,
                    ImVec2 size);

// A pulsing "live" dot — indicates streaming data.
void live_dot();

// Empty-state placeholder for panels with no data.
void empty_state(const char* glyph, const char* title, const char* hint);

}
