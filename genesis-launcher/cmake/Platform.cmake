if(WIN32)
    set(GENESIS_PLATFORM_WINDOWS TRUE)
    set(GENESIS_PLATFORM_LIBS
        crypt32
        wintrust
        secur32
        winhttp
        ncrypt
        advapi32
        shell32
        user32
    )
    message(STATUS "Genesis: targeting Windows")
elseif(APPLE)
    set(GENESIS_PLATFORM_MACOS TRUE)
    find_library(SECURITY_FRAMEWORK Security)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(APPKIT_FRAMEWORK AppKit)
    set(GENESIS_PLATFORM_LIBS
        ${SECURITY_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
    )
    message(STATUS "Genesis: targeting macOS")
elseif(UNIX)
    set(GENESIS_PLATFORM_LINUX TRUE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET libsecret-1)
    if(LIBSECRET_FOUND)
        set(GENESIS_PLATFORM_LIBS ${LIBSECRET_LIBRARIES})
        include_directories(${LIBSECRET_INCLUDE_DIRS})
        add_definitions(-DGENESIS_HAS_LIBSECRET)
    endif()
    find_package(OpenGL REQUIRED)
    set(GENESIS_PLATFORM_LIBS ${GENESIS_PLATFORM_LIBS} pthread OpenGL::GL ${CMAKE_DL_LIBS})
    message(STATUS "Genesis: targeting Linux")
endif()
