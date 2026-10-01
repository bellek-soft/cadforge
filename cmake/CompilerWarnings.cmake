# Applies a sensible warning set to our own targets only (never to third-party code).
function(cadforge_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8
            /wd4251   # dll-interface warnings from OCCT headers
            /wd4127)  # conditional expression is constant
        target_compile_definitions(${target} PRIVATE _USE_MATH_DEFINES NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()
