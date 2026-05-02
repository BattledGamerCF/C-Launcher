# Apply Genesis compiler flags to a target. Called from CMakeLists.txt
# AFTER add_executable(Genesis ...) so the target exists.
function(genesis_apply_compiler_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
            /wd4100
            /wd4127
            /wd4244
            /wd4267
        )
        # nlohmann/json needs <cmath> definitions
        target_compile_definitions(${target} PRIVATE _USE_MATH_DEFINES NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wno-unused-parameter
            -Wno-unused-function
            -Wno-unused-variable
            -Wno-unused-but-set-variable
            -Wno-sign-compare
            -Wno-missing-field-initializers
        )
        # NOTE: -fno-exceptions removed — MicrosoftAuth and nlohmann::json
        # rely on try/catch + json::exception. Build would fail otherwise.
        if(CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_BUILD_TYPE)
            target_compile_options(${target} PRIVATE -O2)
        endif()
    endif()
endfunction()
