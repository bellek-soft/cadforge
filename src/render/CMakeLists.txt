# OpenGL 4.1 core renderer (the maximum macOS supports, so it runs everywhere).
add_library(cadforge_render STATIC
    Camera.cpp
    GlUtil.cpp
    GpuMesh.cpp
    Renderer.cpp)
target_link_libraries(cadforge_render
    PUBLIC cadforge::core cadforge::glad OpenGL::GL)
cadforge_set_warnings(cadforge_render)
add_library(cadforge::render ALIAS cadforge_render)
