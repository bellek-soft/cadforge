# Layered architecture (each layer only depends on the ones above it):
#
#   core    - plain data types (math, mesh data, logging). No OCCT, no GL.
#   geom    - geometry kernel facade over OpenCASCADE. OCCT never leaks out.
#   model   - parametric document: features, properties, recompute, undo, I/O.
#   render  - OpenGL 4.1 core renderer. Knows meshes, not features.
#   app     - GLFW + Dear ImGui desktop application.
#
# Future modules plug in at the same level:
#   analysis (FEA) - depends on geom (meshing) + model (analysis features).
add_subdirectory(core)
add_subdirectory(geom)
add_subdirectory(model)
add_subdirectory(render)
add_subdirectory(app)
