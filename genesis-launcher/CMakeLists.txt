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
    src/jvm/JvmConfig.cpp
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

genesis_apply_compiler_flags(Genesis)

# ─── Install + packaging ────────────────────────────────────────────────────
include(GNUInstallDirs)

if(APPLE)
    set_target_properties(Genesis PROPERTIES
        MACOSX_BUNDLE                       TRUE
        MACOSX_BUNDLE_BUNDLE_NAME           "Genesis"
        MACOSX_BUNDLE_BUNDLE_VERSION        "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING  "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER        "io.genesis.launcher"
        MACOSX_BUNDLE_COPYRIGHT             "© 2026 Genesis Launcher"
    )
endif()

install(TARGETS Genesis
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .
)

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/genesis.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
            OPTIONAL)
    install(FILES packaging/linux/genesis.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            OPTIONAL)
endif()

# ─── CPack ───────────────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME              "Genesis")
set(CPACK_PACKAGE_VENDOR            "Genesis Launcher Project")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform Minecraft launcher")
set(CPACK_PACKAGE_HOMEPAGE_URL      "https://github.com/genesis-launcher/genesis")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Genesis")
set(CPACK_PACKAGE_CONTACT           "support@genesis.invalid")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME       "Genesis")
    set(CPACK_NSIS_PACKAGE_NAME       "Genesis")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\Genesis.exe")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "Genesis.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_DMG_VOLUME_NAME         "Genesis ${PROJECT_VERSION}")
    set(CPACK_DMG_FORMAT              "UDZO")
    set(CPACK_BUNDLE_NAME             "Genesis")
else()
    # Linux: build tarball + DEB; AppImage produced by CI separately.
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Genesis Launcher Project <support@genesis.invalid>")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS    "libc6, libstdc++6, libcurl4, libssl3, libglfw3, libsecret-1-0")
    set(CPACK_DEBIAN_PACKAGE_SECTION    "games")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY   "optional")
endif()

include(CPack)
