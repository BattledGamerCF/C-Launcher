if(MSVC)
    target_compile_options(Genesis PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /utf-8
        /wd4100
        /wd4127
    )
else()
    target_compile_options(Genesis PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wno-unused-parameter
        -fno-exceptions
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(Genesis PRIVATE -O2)
    endif()
endif()
