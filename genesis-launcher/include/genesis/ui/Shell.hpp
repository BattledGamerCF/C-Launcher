#pragma once

namespace genesis::core    { class Launcher; }
namespace genesis::ui::async_ops { class AsyncLauncher; }

namespace genesis::ui::shell {

// Initialize the shell — applies theme, installs streaming log sink,
// constructs the AsyncLauncher singleton. Idempotent.
void initialize(core::Launcher& launcher);

// Render the entire UI: header, left rail, center workspace, right inspector,
// bottom console, modals, toasts. Call once per frame.
void render(core::Launcher& launcher);

// Tear down. Must be called before Launcher::shutdown so worker threads stop.
void shutdown();

// Access the shared async launcher (for views that need to dispatch actions).
async_ops::AsyncLauncher& async_launcher();

}
