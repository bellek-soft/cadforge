cmake_minimum_required(VERSION 3.21)

project(CadForge
    VERSION 0.1.0
    DESCRIPTION "Parametric 3D CAD built on OpenCASCADE, OpenGL and Dear ImGui"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CADFORGE_BUILD_TESTS   "Build unit tests" ON)
option(CADFORGE_FETCH_MISSING "Download small dependencies (GLFW, ImGui, ImGuizmo, glm, json) with FetchContent when they are not found" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(third_party)
add_subdirectory(src)

if(CADFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
