#pragma once

namespace genesis::core { class Launcher; }

// Workspace and shell-region view declarations. Implementations live in
// src/ui/views/*.cpp and src/ui/Shell.cpp.
namespace genesis::ui::views {

// Workspace modes (one rendered at a time)
void draw_instance_dashboard(core::Launcher& launcher);
void draw_modpack_view      (core::Launcher& launcher);
void draw_version_view      (core::Launcher& launcher);
void draw_log_view          (core::Launcher& launcher);
void draw_diagnostics_view  (core::Launcher& launcher);
void draw_settings_tab      (core::Launcher& launcher);

// Shell regions
void draw_nav_rail   (core::Launcher& launcher);
void draw_inspector  (core::Launcher& launcher);
void draw_console    (core::Launcher& launcher);

// Modals
void draw_device_code_modal();
void draw_new_instance_modal(core::Launcher& launcher);
void draw_about_modal();
void draw_toasts();

}
