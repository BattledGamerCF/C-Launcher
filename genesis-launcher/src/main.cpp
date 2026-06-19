#include <GLFW/glfw3.h>
#include "genesis/core/Launcher.hpp"
#include "genesis/ui/MainWindow.hpp"
#include "genesis/ui/Shell.hpp"
#include "genesis/platform/PlatformUtils.hpp"
#include "genesis/logging/Logger.hpp"

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#ifdef GENESIS_PLATFORM_WINDOWS
#   include <imgui_impl_win32.h>
#   include <windows.h>
#   include <GL/gl.h>
#elif defined(GENESIS_PLATFORM_MACOS)
#   include <imgui_impl_osx.h>
#   include <OpenGL/gl3.h>
#else
#   include <imgui_impl_glfw.h>
#   include <GLFW/glfw3.h>
#endif

#include <cstdlib>
#include <iostream>

// ─── GLFW entry (Linux / macOS backup) ───────────────────────────────────────
#if !defined(GENESIS_PLATFORM_WINDOWS)

static GLFWwindow* g_window = nullptr;

static bool platform_init(int width, int height) {
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef GENESIS_PLATFORM_MACOS
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    g_window = glfwCreateWindow(width, height, "Genesis Launcher", nullptr, nullptr);
    if (!g_window) { glfwTerminate(); return false; }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io     = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr;

    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    return true;
}

static bool platform_new_frame() {
    return !glfwWindowShouldClose(g_window);
}

static void platform_begin_frame() {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

static void platform_end_frame() {
    ImGui::Render();
    int w, h;
    glfwGetFramebufferSize(g_window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(g_window);
}

static void platform_shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(g_window);
    glfwTerminate();
}

#endif // !GENESIS_PLATFORM_WINDOWS

// ─── main ────────────────────────────────────────────────────────────────────

#ifdef GENESIS_PLATFORM_WINDOWS
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
#else
int main(int, char**)
#endif
{
    genesis::core::Launcher launcher;

    auto config_path = genesis::platform::path_join(
        genesis::platform::user_data_dir("Genesis"),
        "config.json");

    auto init_res = launcher.initialize(config_path);
    if (init_res.is_err()) {
        std::cerr << "Launcher init failed: " << init_res.error().full() << "\n";
        return 1;
    }

#ifndef GENESIS_PLATFORM_WINDOWS
    if (!platform_init(1024, 680)) return 1;

    while (platform_new_frame()) {
        platform_begin_frame();
        genesis::ui::render_main_window(launcher);
        platform_end_frame();
    }

    platform_shutdown();
#endif

    genesis::ui::shell::shutdown();
    launcher.shutdown();
    return 0;
}
