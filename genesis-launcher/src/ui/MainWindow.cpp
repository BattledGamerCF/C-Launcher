#include "genesis/ui/MainWindow.hpp"
#include "genesis/ui/Shell.hpp"
#include "genesis/ui/Theme.hpp"

namespace genesis::ui {

void apply_genesis_theme() {
    theme::apply();
}

void render_main_window(core::Launcher& launcher) {
    static bool initialized = false;
    if (!initialized) {
        shell::initialize(launcher);
        initialized = true;
    }
    shell::render(launcher);
}

}
