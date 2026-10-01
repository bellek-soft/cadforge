# glad: OpenGL 4.1 core loader, generated with glad2 (gl:core=4.1). Vendored -> no dependency.
add_library(cadforge_glad STATIC glad/src/gl.c)
target_include_directories(cadforge_glad SYSTEM PUBLIC glad/include)
add_library(cadforge::glad ALIAS cadforge_glad)

# portable-file-dialogs: header-only native file dialogs (WTFPL). Vendored.
add_library(cadforge_pfd INTERFACE)
target_include_directories(cadforge_pfd SYSTEM INTERFACE pfd)
add_library(cadforge::pfd ALIAS cadforge_pfd)
