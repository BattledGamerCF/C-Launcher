cmake_minimum_required(VERSION 3.20)

project(Genesis
    VERSION 1.0.0
    DESCRIPTION "Genesis Minecraft Launcher"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cmake/CompilerFlags.cmake)
include(cmake/Platform.cmake)
include(cmake/Dependencies.cmake)

set(GENESIS_SOURCES
    src/main.cpp
    src/core/Launcher.cpp
    src/core/State.cpp
    src/core/EventBus.cpp
    src/auth/AuthManager.cpp
    src/auth/MicrosoftAuth.cpp
    src/auth/SecureStorage.cpp
    src/version/VersionManager.cpp
    src/version/VersionManifest.cpp
    src/version/Profile.cpp
    src/assets/AssetManager.cpp
    src/assets/Downloader.cpp
    src/assets/Verifier.cpp
    src/jvm/JvmOrchestrator.cpp
    src/jvm/ProcessHandle.cpp
    src/jvm/JavaFinder.cpp
    src/instance/InstanceManager.cpp
    src/instance/Instance.cpp
    src/update/Updater.cpp
    src/mods/ModrinthClient.cpp
    src/mods/PerformancePack.cpp
    src/logging/Logger.cpp
    src/platform/PlatformUtils.cpp
    src/ui/MainWindow.cpp
    src/ui/AuthView.cpp
    src/ui/InstanceView.cpp
    src/ui/DownloadView.cpp
    src/ui/SettingsView.cpp
    src/ui/Theme.cpp
    src/ui/UiState.cpp
    src/ui/LogStream.cpp
    src/ui/ProcessMonitor.cpp
    src/ui/AsyncLauncher.cpp
    src/ui/Widgets.cpp
    src/ui/Shell.cpp
    src/ui/views/InstanceDashboard.cpp
    src/ui/views/ModpackView.cpp
    src/ui/views/VersionView.cpp
    src/ui/views/LogView.cpp
    src/ui/views/DiagnosticsView.cpp
    src/ui/views/SettingsTab.cpp
)

if(GENESIS_PLATFORM_WINDOWS)
    list(APPEND GENESIS_SOURCES src/platform/windows/SecureStorage_Win.cpp)
    list(APPEND GENESIS_SOURCES src/platform/windows/ProcessSpawn_Win.cpp)
    list(APPEND GENESIS_SOURCES src/platform/windows/JavaFinder_Win.cpp)
elseif(GENESIS_PLATFORM_MACOS)
    list(APPEND GENESIS_SOURCES src/platform/macos/SecureStorage_Mac.cpp)
    list(APPEND GENESIS_SOURCES src/platform/macos/ProcessSpawn_Mac.cpp)
    list(APPEND GENESIS_SOURCES src/platform/macos/JavaFinder_Mac.cpp)
elseif(GENESIS_PLATFORM_LINUX)
    list(APPEND GENESIS_SOURCES src/platform/linux/SecureStorage_Linux.cpp)
    list(APPEND GENESIS_SOURCES src/platform/linux/ProcessSpawn_Linux.cpp)
    list(APPEND GENESIS_SOURCES src/platform/linux/JavaFinder_Linux.cpp)
endif()

add_executable(Genesis ${GENESIS_SOURCES})

target_include_directories(Genesis
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${CURL_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(Genesis
    PRIVATE
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        imgui
        ${GENESIS_PLATFORM_LIBS}
)

target_compile_definitions(Genesis PRIVATE
    GENESIS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    GENESIS_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    GENESIS_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    GENESIS_VERSION_STRING="${PROJECT_VERSION}"
    $<$<BOOL:${GENESIS_PLATFORM_WINDOWS}>:GENESIS_PLATFORM_WINDOWS>
    $<$<BOOL:${GENESIS_PLATFORM_MACOS}>:GENESIS_PLATFORM_MACOS>
    $<$<BOOL:${GENESIS_PLATFORM_LINUX}>:GENESIS_PLATFORM_LINUX>
)

install(TARGETS Genesis
    RUNTIME DESTINATION bin
    BUNDLE  DESTINATION .
)
