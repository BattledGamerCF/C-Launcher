#include "genesis/ui/Views.hpp"
#include "genesis/ui/UiState.hpp"
#include "genesis/ui/Theme.hpp"
#include "genesis/ui/Widgets.hpp"
#include "genesis/ui/LogStream.hpp"
#include "genesis/ui/Shell.hpp"
#include "genesis/ui/AsyncLauncher.hpp"
#include "genesis/core/Launcher.hpp"
#include "genesis/platform/PlatformUtils.hpp"

#include <imgui.h>
#include <vector>
#include <cstdio>

namespace genesis::ui::views {

namespace ust = genesis::ui::state;

static int   g_view_mode    = 0;     // 0=structured, 1=raw, 2=timeline
static int   g_level_filter = 0;
static char  g_search[256]  = {};
static bool  g_group_by_corr = true;

static std::vector<log::LogEntry> g_buf;
static int64_t                    g_last_id = 0;

static const char* level_short(genesis::logging::Level lv) {
    switch (lv) {
        case genesis::logging::Level::Trace:    return "TRC";
        case genesis::logging::Level::Debug:    return "DBG";
        case genesis::logging::Level::Info:     return "INF";
        case genesis::logging::Level::Warn:     return "WRN";
        case genesis::logging::Level::Error:    return "ERR";
        case genesis::logging::Level::Critical: return "CRT";
    }
    return "?";
}

static ImVec4 color_for_level(genesis::logging::Level lv) {
    switch (lv) {
        case genesis::logging::Level::Trace:    return theme::TRACE_;
        case genesis::logging::Level::Debug:    return theme::TRACE_;
        case genesis::logging::Level::Info:     return theme::TEXT;
        case genesis::logging::Level::Warn:     return theme::WARN;
        case genesis::logging::Level::Error:
        case genesis::logging::Level::Critical: return theme::ERR;
    }
    return theme::TEXT;
}

static void refresh_buffer() {
    auto& stream = log::LogStream::global();
    int64_t until = stream.paused() ? stream.pause_at_id() : stream.latest_id();
    if (g_last_id >= until) return;

    std::vector<log::LogEntry> page;
    if (g_level_filter == 0 && g_search[0] == '\0')
        stream.snapshot(g_last_id, page, 5000);
    else
        stream.snapshot_filtered(g_last_id, g_level_filter, g_search, page, 5000);

    for (auto& e : page) {
        if (e.id > until) break;
        g_buf.push_back(std::move(e));
    }
    if (!page.empty()) g_last_id = g_buf.empty() ? until : g_buf.back().id;

    if (g_buf.size() > 30000)
        g_buf.erase(g_buf.begin(), g_buf.begin() + (g_buf.size() - 30000));
}

static void draw_toolbar(genesis::core::Launcher& launcher) {
    auto& stream = log::LogStream::global();

    int chosen = widgets::segmented("##logmode",
        {"structured", "raw", "timeline"}, g_view_mode);
    if (chosen != g_view_mode) { g_view_mode = chosen; }

    ImGui::SameLine();
    const char* levels[] = {"all", "info+", "warn+", "error+"};
    ImGui::SetNextItemWidth(90);
    if (ImGui::Combo("##loglevel", &g_level_filter, levels, 4)) {
        g_buf.clear(); g_last_id = 0;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    if (ImGui::InputTextWithHint("##logsearch", "search messages",
                                 g_search, sizeof(g_search))) {
        g_buf.clear(); g_last_id = 0;
    }

    ImGui::SameLine();
    bool paused = stream.paused();
    if (widgets::toggle(paused ? "  resume  " : "  pause  ", &paused))
        stream.set_paused(paused);

    ImGui::SameLine();
    widgets::toggle("group", &g_group_by_corr);

    ImGui::SameLine();
    if (ImGui::SmallButton("clear")) { stream.clear(); g_buf.clear(); g_last_id = 0; }

    ImGui::SameLine();
    if (ImGui::SmallButton("export")) {
        auto p = genesis::platform::path_join(
            genesis::platform::default_game_dir(), "logs/genesis-export.log");
        shell::async_launcher().start_export_logs(p);
    }
    (void)launcher;
}

static void draw_structured() {
    auto& s = ust::global();
    refresh_buffer();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::BG_DEEP);
    ImGui::BeginChild("##log_struct", {0, 0}, true,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(g_buf.size()));
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            auto& e = g_buf[i];

            // Group separator: insert a faint line between consecutive
            // entries with different correlation_id when grouping is on.
            if (g_group_by_corr && i > 0 && !e.correlation_id.empty() &&
                g_buf[i - 1].correlation_id != e.correlation_id) {
                ImGui::Dummy({0, 2});
            }

            int64_t ts_s  = e.timestamp_us / 1'000'000;
            int64_t ts_ms = (e.timestamp_us / 1000) % 1000;
            int hh = (ts_s / 3600) % 24, mm = (ts_s / 60) % 60, ss = ts_s % 60;

            ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_FAINT);
            ImGui::Text("%02d:%02d:%02d.%03lld", hh, mm, ss, (long long)ts_ms);
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, color_for_level(e.level));
            ImGui::Text("%s", level_short(e.level));
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, theme::ACCENT_DIM);
            ImGui::Text("%-12s", e.logger_name.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();

            char id[32];
            std::snprintf(id, sizeof(id), "##le_%lld", (long long)e.id);
            bool selected = (s.selection.selected_log_id == e.id);
            ImGui::PushStyleColor(ImGuiCol_Text, color_for_level(e.level));
            if (ImGui::Selectable((e.message + id).c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns))
                ust::dispatch::select_log(e.id);
            ImGui::PopStyleColor();

            if (!e.correlation_id.empty()) {
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 110);
                ImGui::PushStyleColor(ImGuiCol_Text, theme::TEXT_FAINT);
                ImGui::Text("[%.10s]", e.correlation_id.c_str());
                ImGui::PopStyleColor();
            }
        }
    }
    clip.End();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void draw_raw() {
    refresh_buffer();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::BG_DEEP);
    ImGui::BeginChild("##log_raw", {0, 0}, true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(g_buf.size()));
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            auto& e = g_buf[i];
            ImGui::PushStyleColor(ImGuiCol_Text, color_for_level(e.level));
            ImGui::Text("[%lld] [%s] [%s] %s",
                (long long)e.timestamp_us, level_short(e.level),
                e.logger_name.c_str(), e.message.c_str());
            ImGui::PopStyleColor();
        }
    }
    clip.End();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void draw_timeline() {
    refresh_buffer();
    if (g_buf.empty()) {
        widgets::empty_state("·", "No log entries", "Activity will appear here.");
        return;
    }
    // Bin entries into 60 buckets
    const int N = 60;
    std::vector<float> info_v(N, 0), warn_v(N, 0), err_v(N, 0);
    int64_t t0 = g_buf.front().timestamp_us;
    int64_t t1 = g_buf.back().timestamp_us;
    if (t1 == t0) t1 = t0 + 1;
    for (auto& e : g_buf) {
        float frac = float(e.timestamp_us - t0) / float(t1 - t0);
        int idx = std::min(N - 1, std::max(0, int(frac * N)));
        if (e.level == genesis::logging::Level::Info)  info_v[idx] += 1.0f;
        if (e.level == genesis::logging::Level::Warn)  warn_v[idx] += 1.0f;
        if (e.level >= genesis::logging::Level::Error) err_v[idx]  += 1.0f;
    }
    widgets::timeline_chart("info / bucket",  info_v, "events", theme::INFO,    {-1, 80});
    widgets::timeline_chart("warn / bucket",  warn_v, "events", theme::WARN,    {-1, 80});
    widgets::timeline_chart("error / bucket", err_v,  "events", theme::ERR,     {-1, 80});

    ImGui::Spacing();
    widgets::section_header("RECENT ERRORS");
    auto& s = ust::global();
    int shown = 0;
    for (auto& e : s.errors) {
        widgets::error_chip(e);
        if (++shown >= 8) break;
    }
}

void draw_log_view(genesis::core::Launcher& launcher) {
    widgets::section_header("LOG STREAM");
    int64_t info, warn, err;
    log::LogStream::global().counts(info, warn, err);
    widgets::faint_text("buffer: %zu entries  /  %lld info  %lld warn  %lld err",
        log::LogStream::global().size(),
        (long long)info, (long long)warn, (long long)err);

    ImGui::Spacing();
    draw_toolbar(launcher);
    ImGui::Spacing();
    ImGui::Separator();

    switch (g_view_mode) {
        case 0: draw_structured(); break;
        case 1: draw_raw();        break;
        case 2: draw_timeline();   break;
    }
}

}
