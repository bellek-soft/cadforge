add_library(cadforge_core STATIC
    Log.cpp
    Placement.cpp
    Paths.cpp)
target_include_directories(cadforge_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cadforge_core PUBLIC glm::glm)
target_compile_definitions(cadforge_core PUBLIC GLM_ENABLE_EXPERIMENTAL GLM_FORCE_SILENT_WARNINGS)
cadforge_set_warnings(cadforge_core)
add_library(cadforge::core ALIAS cadforge_core)
