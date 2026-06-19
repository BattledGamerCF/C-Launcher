include(FetchContent)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)


FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(nlohmann_json)


FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(spdlog)


FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(imgui)


set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp

    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)


# GLFW backend is used on all desktop platforms
list(APPEND IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
)


# Windows native backend
if(WIN32)
    list(APPEND IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    )
endif()


# macOS native backend
if(APPLE)
    list(APPEND IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/backends/imgui_impl_osx.mm
    )
endif()


add_library(imgui STATIC ${IMGUI_SOURCES})


target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)


if(APPLE)
    enable_language(OBJCXX)

    set_source_files_properties(
        ${imgui_SOURCE_DIR}/backends/imgui_impl_osx.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
endif()


target_link_libraries(imgui PUBLIC
    glfw
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
