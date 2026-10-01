# Geometry kernel facade. The only library that links/includes OpenCASCADE.
add_library(cadforge_geom STATIC
    Shape.cpp
    Primitives.cpp
    Operations.cpp
    Tessellator.cpp
    ShapeIO.cpp)
target_link_libraries(cadforge_geom
    PUBLIC  cadforge::core
    PRIVATE cadforge::occt)
cadforge_set_warnings(cadforge_geom)
add_library(cadforge::geom ALIAS cadforge_geom)
