# 2D constraint sketcher (data model + PlaneGCS solver). Depends on core only.
add_library(cadforge_sketch STATIC
    Sketch.cpp
    Solver.cpp)
target_link_libraries(cadforge_sketch
    PUBLIC  cadforge::core nlohmann_json::nlohmann_json
    PRIVATE cadforge::planegcs)
cadforge_set_warnings(cadforge_sketch)
add_library(cadforge::sketch ALIAS cadforge_sketch)
