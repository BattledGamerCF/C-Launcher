#include "genesis/ui/MainWindow.hpp"
#include "genesis/core/EventBus.hpp"
#include "genesis/core/State.hpp"
#include "genesis/logging/Logger.hpp"
#include "genesis/platform/PlatformUtils.hpp"

#include <imgui.h>
#include <string>
#include <vector>
#include <optional>

namespace genesis::ui {

using namespace core;
static auto log = logging::get_logger("UI");

// ─── Colour palette ──────────────────────────────────────────────────────────
static ImVec4 COL_BG         = {0.10f, 0.10f, 0.12f, 1.0f};
static ImVec4 COL_PANEL      = {0.14f, 0.14f, 0.17f, 1.0f};
static ImVec4 COL_ACCENT     = {0.24f, 0.52f, 0.88f, 1.0f};
static ImVec4 COL_ACCENT_DIM = {0.18f, 0.40f, 0.68f, 1.0f};
static ImVec4 COL_TEXT       = {0.92f, 0.92f, 0.95f, 1.0f};
static ImVec4 COL_TEXT_DIM   = {0.55f, 0.55f, 0.60f, 1.0f};
static ImVec4 COL_SUCCESS    = {0.30f, 0.76f, 0.40f, 1.0f};
static ImVec4 COL_ERROR      = {0.90f, 0.30f, 0.30f, 1.0f};
static ImVec4 COL_WARN       = {0.96f, 0.76f, 0.22f, 1.0f};

void apply_genesis_theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding   = 8.0f;
    s.FrameRounding    = 5.0f;
    s.GrabRounding     = 5.0f;
    s.TabRounding      = 4.0f;
    s.PopupRounding    = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowBorderSize = 0.0f;
    s.FramePadding     = {10.0f, 6.0f};
    s.ItemSpacing      = {10.0f, 8.0f};
    s.IndentSpacing    = 16.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = COL_BG;
    c[ImGuiCol_ChildBg]           = COL_PANEL;
    c[ImGuiCol_PopupBg]           = {0.12f, 0.12f, 0.15f, 1.0f};
    c[ImGuiCol_Border]            = {0.22f, 0.22f, 0.28f, 1.0f};
    c[ImGuiCol_FrameBg]           = {0.18f, 0.18f, 0.22f, 1.0f};
    c[ImGuiCol_FrameBgHovered]    = {0.22f, 0.22f, 0.28f, 1.0f};
    c[ImGuiCol_FrameBgActive]     = COL_ACCENT_DIM;
    c[ImGuiCol_TitleBg]           = COL_PANEL;
    c[ImGuiCol_TitleBgActive]     = COL_PANEL;
    c[ImGuiCol_MenuBarBg]         = COL_PANEL;
    c[ImGuiCol_ScrollbarBg]       = COL_PANEL;
    c[ImGuiCol_ScrollbarGrab]     = {0.35f, 0.35f, 0.40f, 1.0f};
    c[ImGuiCol_ScrollbarGrabHover]= {0.45f, 0.45f, 0.50f, 1.0f};
    c[ImGuiCol_ScrollbarGrabActive]= COL_ACCENT;
    c[ImGuiCol_CheckMark]         = COL_ACCENT;
    c[ImGuiCol_SliderGrab]        = COL_ACCENT;
    c[ImGuiCol_SliderGrabActive]  = {0.30f, 0.60f, 1.0f, 1.0f};
    c[ImGuiCol_Button]            = COL_ACCENT_DIM;
    c[ImGuiCol_ButtonHovered]     = COL_ACCENT;
    c[ImGuiCol_ButtonActive]      = {0.14f, 0.34f, 0.60f, 1.0f};
    c[ImGuiCol_Header]            = {0.22f, 0.44f, 0.76f, 0.45f};
    c[ImGuiCol_HeaderHovered]     = COL_ACCENT;
    c[ImGuiCol_HeaderActive]      = COL_ACCENT_DIM;
    c[ImGuiCol_Tab]               = {0.14f, 0.14f, 0.18f, 1.0f};
    c[ImGuiCol_TabHovered]        = COL_ACCENT;
    c[ImGuiCol_TabActive]         = COL_ACCENT_DIM;
    c[ImGuiCol_TabUnfocused]      = {0.12f, 0.12f, 0.16f, 1.0f};
    c[ImGuiCol_TabUnfocusedActive]= {0.18f, 0.30f, 0.50f, 1.0f};
    c[ImGuiCol_Text]              = COL_TEXT;
    c[ImGuiCol_TextDisabled]      = COL_TEXT_DIM;
    c[ImGuiCol_Separator]         = {0.22f, 0.22f, 0.28f, 1.0f};
    c[ImGuiCol_SeparatorHovered]  = COL_ACCENT;
    c[ImGuiCol_ResizeGrip]        = {0.22f, 0.22f, 0.28f, 0.5f};
    c[ImGuiCol_ResizeGripHovered] = COL_ACCENT;
    c[ImGuiCol_ResizeGripActive]  = COL_ACCENT;
    c[ImGuiCol_PlotLines]         = COL_ACCENT;
    c[ImGuiCol_PlotHistogram]     = COL_ACCENT;
    c[ImGuiCol_TableBorderStrong] = {0.22f, 0.22f, 0.28f, 1.0f};
    c[ImGuiCol_TableBorderLight]  = {0.18f, 0.18f, 0.22f, 1.0f};
    c[ImGuiCol_TableRowBg]        = {0.0f, 0.0f, 0.0f, 0.0f};
    c[ImGuiCol_TableRowBgAlt]     = {1.0f, 1.0f, 1.0f, 0.025f};
}

struct MainWindowState {
    int   active_tab         = 0;
    bool  show_settings      = false;
    bool  show_about         = false;
    float download_fraction  = 0.0f;
    std::string download_label;
    std::string status_message;
    bool  status_is_error    = false;
    bool  is_authenticated   = false;
    std::string username;
    std::vector<std::string>  instance_ids;
    std::vector<std::string>  instance_names;
    int   selected_instance  = -1;
    std::vector<std::string>  version_list;
    int   selected_version   = 0;
    bool  is_launching       = false;
    bool  update_available   = false;
    std::string update_version;
    char  new_instance_name[128] = {};
    char  new_instance_version[32] = "1.21.4";
    bool  show_new_instance_dialog = false;
};

static MainWindowState g_state;

static void setup_event_listeners() {
    EventBus::global().subscribe<DownloadProgressEvent>([](const DownloadProgressEvent& e) {
        g_state.download_fraction = e.fraction();
        g_state.download_label    = e.name;
    });
    EventBus::global().subscribe<AuthStatusEvent>([](const AuthStatusEvent& e) {
        g_state.is_authenticated = e.authenticated;
        g_state.username         = e.username;
    });
    EventBus::global().subscribe<ErrorEvent>([](const ErrorEvent& e) {
        g_state.status_message  = "[" + e.context + "] " + e.message;
        g_state.status_is_error = true;
    });
    EventBus::global().subscribe<UpdateAvailableEvent>([](const UpdateAvailableEvent& e) {
        g_state.update_available = true;
        g_state.update_version   = e.new_version;
    });
}

static void draw_status_bar() {
    float bar_h = 26.0f;
    ImVec2 vp   = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos({0, vp.y - bar_h});
    ImGui::SetNextWindowSize({vp.x, bar_h});
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoInputs
                           | ImGuiWindowFlags_NoNav
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoBringToDisplayFront;
    ImGui::Begin("##statusbar", nullptr, flags);

    if (!g_state.status_message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            g_state.status_is_error ? COL_ERROR : COL_TEXT_DIM);
        ImGui::TextUnformatted(g_state.status_message.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::Text("Genesis Launcher v" GENESIS_VERSION_STRING);
        ImGui::PopStyleColor();
    }

    if (g_state.download_fraction > 0.0f && g_state.download_fraction < 1.0f) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(vp.x * 0.4f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, COL_ACCENT);
        ImGui::ProgressBar(g_state.download_fraction,
                           {vp.x * 0.5f, 14.0f},
                           g_state.download_label.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

static void draw_auth_panel() {
    ImGui::BeginChild("##auth_panel", {0, 0}, true);
    ImGui::Dummy({0, 40});

    float center_x = ImGui::GetContentRegionAvail().x * 0.5f;
    ImGui::SetCursorPosX(center_x - 80);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::TextUnformatted("Genesis");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::SetCursorPosX(center_x - 160);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImGui::TextUnformatted("Sign in with your Microsoft account to get started.");
    ImGui::PopStyleColor();

    ImGui::Dummy({0, 30});
    ImGui::SetCursorPosX(center_x - 90);
    ImGui::PushStyleColor(ImGuiCol_Button,        COL_ACCENT_DIM);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT);
    if (ImGui::Button("Sign in with Microsoft", {180, 36})) {
        g_state.status_message  = "Opening browser for authentication...";
        g_state.status_is_error = false;
    }
    ImGui::PopStyleColor(2);

    ImGui::EndChild();
}

static void draw_instance_list(core::Launcher& launcher) {
    ImGuiTableFlags tf = ImGuiTableFlags_Borders
                       | ImGuiTableFlags_RowBg
                       | ImGuiTableFlags_ScrollY
                       | ImGuiTableFlags_SizingStretchProp;

    float table_h = ImGui::GetContentRegionAvail().y - 52;
    if (ImGui::BeginTable("##instances", 4, tf, {0, table_h})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name",         ImGuiTableColumnFlags_WidthStretch, 2.5f);
        ImGui::TableSetupColumn("Version",      ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Last Played",  ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Play Time",    ImGuiTableColumnFlags_WidthFixed,   90.0f);
        ImGui::TableHeadersRow();

        auto& instances = launcher.instance_manager().all();
        for (int i = 0; i < static_cast<int>(instances.size()); ++i) {
            auto& inst = *instances[i];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            bool selected = (g_state.selected_instance == i);
            if (ImGui::Selectable(inst.display_name().c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                g_state.selected_instance = i;
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
            ImGui::TextUnformatted(inst.config().game_version.c_str());
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(2);
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
            auto last = std::chrono::duration_cast<std::chrono::seconds>(
                inst.config().last_played.time_since_epoch()).count();
            ImGui::Text(last > 0 ? "Recently" : "Never");
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(3);
            uint64_t secs = inst.config().total_play_seconds;
            uint32_t h    = static_cast<uint32_t>(secs / 3600);
            uint32_t m    = static_cast<uint32_t>((secs % 3600) / 60);
            ImGui::Text("%uh %02um", h, m);
        }
        ImGui::EndTable();
    }

    ImGui::Dummy({0, 4});
    ImGui::PushStyleColor(ImGuiCol_Button, COL_ACCENT_DIM);
    if (ImGui::Button("+ New Instance", {130, 30}))
        g_state.show_new_instance_dialog = true;
    ImGui::PopStyleColor();

    ImGui::SameLine();
    bool can_launch = (g_state.selected_instance >= 0 &&
                       g_state.is_authenticated &&
                       !g_state.is_launching);
    if (!can_launch) ImGui::BeginDisabled();
    if (ImGui::Button("Launch", {80, 30})) {
        auto& inst = *launcher.instance_manager().all()[g_state.selected_instance];
        g_state.is_launching   = true;
        g_state.status_message = "Launching " + inst.display_name() + "...";
    }
    if (!can_launch) ImGui::EndDisabled();

    ImGui::SameLine();
    bool can_delete = (g_state.selected_instance >= 0);
    if (!can_delete) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.6f, 0.15f, 0.15f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.8f, 0.20f, 0.20f, 1.0f});
    if (ImGui::Button("Delete", {70, 30})) {
        auto& inst = *launcher.instance_manager().all()[g_state.selected_instance];
        launcher.instance_manager().remove(inst.id());
        g_state.selected_instance = -1;
    }
    ImGui::PopStyleColor(2);
    if (!can_delete) ImGui::EndDisabled();
}

static void draw_new_instance_dialog(core::Launcher& launcher) {
    if (!g_state.show_new_instance_dialog) return;
    ImGui::OpenPopup("New Instance");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({420, 260});

    if (ImGui::BeginPopupModal("New Instance", &g_state.show_new_instance_dialog,
                               ImGuiWindowFlags_NoResize)) {
        ImGui::Spacing();
        ImGui::Text("Instance Name");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##inst_name", g_state.new_instance_name,
                         sizeof(g_state.new_instance_name));

        ImGui::Spacing();
        ImGui::Text("Minecraft Version");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##inst_ver", g_state.new_instance_version,
                         sizeof(g_state.new_instance_version));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool valid = (g_state.new_instance_name[0] != '\0' &&
                      g_state.new_instance_version[0] != '\0');

        if (!valid) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button, COL_ACCENT_DIM);
        if (ImGui::Button("Create", {100, 30})) {
            instance::InstanceConfig cfg;
            cfg.id           = std::string(g_state.new_instance_name);
            cfg.display_name = cfg.id;
            cfg.game_version = std::string(g_state.new_instance_version);
            auto res = launcher.instance_manager().create(std::move(cfg));
            if (res.is_err()) {
                g_state.status_message  = "Create failed: " + res.error().full();
                g_state.status_is_error = true;
            } else {
                g_state.status_message  = "Instance created.";
                g_state.status_is_error = false;
            }
            g_state.show_new_instance_dialog = false;
            memset(g_state.new_instance_name, 0, sizeof(g_state.new_instance_name));
        }
        ImGui::PopStyleColor();
        if (!valid) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", {80, 30}))
            g_state.show_new_instance_dialog = false;

        ImGui::EndPopup();
    }
}

static void draw_versions_tab(core::Launcher& launcher) {
    ImGui::BeginChild("##ver_panel", {0, 0}, true);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImGui::TextUnformatted("Version list is loaded from Mojang's metadata on first open.");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    if (ImGui::Button("Refresh", {90, 28})) {
        g_state.status_message = "Refreshing version list...";
        launcher.version_manager().fetch_version_list(true);
        g_state.status_message = "Version list refreshed.";
    }

    auto list_res = launcher.version_manager().fetch_version_list(false);
    if (list_res.is_ok()) {
        ImGui::Spacing();
        ImGuiTableFlags tf = ImGuiTableFlags_Borders
                           | ImGuiTableFlags_RowBg
                           | ImGuiTableFlags_ScrollY
                           | ImGuiTableFlags_SizingStretchProp;
        float h = ImGui::GetContentRegionAvail().y - 10;
        if (ImGui::BeginTable("##versions", 3, tf, {0, h})) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("ID",           ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Type",         ImGuiTableColumnFlags_WidthFixed,   90.0f);
            ImGui::TableSetupColumn("Release Time", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableHeadersRow();

            for (auto& v : list_res.value().versions) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(v.id.c_str());
                ImGui::TableSetColumnIndex(1);
                auto type_str = version::release_type_to_string(v.type);
                ImVec4 col = (v.type == version::ReleaseType::Release) ? COL_SUCCESS
                           : (v.type == version::ReleaseType::Snapshot) ? COL_WARN
                           : COL_TEXT_DIM;
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(type_str.c_str());
                ImGui::PopStyleColor();
                ImGui::TableSetColumnIndex(2);
                ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
                ImGui::TextUnformatted(v.release_time.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
    }

    ImGui::EndChild();
}

static void draw_settings_tab(core::Launcher& launcher) {
    ImGui::BeginChild("##settings_panel", {0, 0}, true);

    ImGui::SeparatorText("Account");
    if (g_state.is_authenticated) {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_SUCCESS);
        ImGui::Text("Signed in as: %s", g_state.username.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.5f, 0.15f, 0.15f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.7f, 0.20f, 0.20f, 1.0f});
        if (ImGui::Button("Sign Out", {80, 24})) {
            launcher.auth_manager().logout();
            g_state.is_authenticated = false;
            g_state.username.clear();
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextUnformatted("Not signed in.");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Launcher");

    static char game_dir[512] = {};
    if (game_dir[0] == '\0') {
        auto dir = platform::default_game_dir();
        strncpy(game_dir, dir.c_str(), sizeof(game_dir) - 1);
    }
    ImGui::Text("Game Directory");
    ImGui::SetNextItemWidth(-80);
    ImGui::InputText("##game_dir", game_dir, sizeof(game_dir));
    ImGui::SameLine();
    if (ImGui::Button("Browse", {72, 24}))
        platform::open_in_file_manager(game_dir);

    ImGui::Spacing();
    ImGui::SeparatorText("Updates");

    if (g_state.update_available) {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_WARN);
        ImGui::Text("Update available: %s", g_state.update_version.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, COL_ACCENT_DIM);
        if (ImGui::Button("Install Update", {120, 24})) {
            g_state.status_message = "Downloading update...";
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextUnformatted("Launcher is up to date.");
        ImGui::PopStyleColor();
        if (ImGui::Button("Check Now", {90, 24})) {
            auto res = launcher.updater().check_for_update();
            if (res.is_ok() && res.value().has_value()) {
                g_state.update_available = true;
                g_state.update_version   = res.value()->version;
            } else {
                g_state.status_message = "No updates found.";
            }
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Diagnostics");
    if (ImGui::Button("Open Logs Folder", {140, 24})) {
        platform::open_in_file_manager(
            platform::path_join(platform::default_game_dir(), "logs"));
    }

    ImGui::EndChild();
}

void render_main_window(core::Launcher& launcher) {
    static bool first_frame = true;
    if (first_frame) {
        apply_genesis_theme();
        setup_event_listeners();
        first_frame = false;
    }

    ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({vp.x, vp.y - 26.0f});
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoBringToDisplayFront;

    ImGui::Begin("##main", nullptr, flags);

    // ── Header ───────────────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
    ImGui::SetWindowFontScale(1.25f);
    ImGui::Text("  Genesis");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
    ImGui::Text("| Minecraft Launcher  v" GENESIS_VERSION_STRING);
    ImGui::PopStyleColor();

    if (g_state.is_authenticated) {
        ImGui::SameLine(vp.x - 200);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_SUCCESS);
        ImGui::Text("  %s", g_state.username.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // ── Tabs ─────────────────────────────────────────────────────────────────
    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("Instances")) {
            ImGui::Spacing();
            if (!g_state.is_authenticated) {
                draw_auth_panel();
            } else {
                draw_instance_list(launcher);
                draw_new_instance_dialog(launcher);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Versions")) {
            ImGui::Spacing();
            draw_versions_tab(launcher);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            ImGui::Spacing();
            draw_settings_tab(launcher);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
    draw_status_bar();
}

}
