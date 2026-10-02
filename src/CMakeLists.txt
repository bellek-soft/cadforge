# Layered architecture (each layer only depends on the ones above it):
#
#   core    - plain data types (math, mesh data, logging). No OCCT, no GL.
#   geom    - geometry kernel facade over OpenCASCADE. OCCT never leaks out.
#   model   - parametric document: features, properties, recompute, undo, I/O.
#   render  - OpenGL 4.1 core renderer. Knows meshes, not features.
#   app     - GLFW + Dear ImGui desktop application.
#
#   sketch  - 2D constraint sketcher (PlaneGCS). Depends on core only.
#   fea     - finite element engine (Netgen mesher, Eigen solver). Depends on core only;
#             model holds the analysis *features*, app runs the jobs.
add_subdirectory(core)
add_subdirectory(geom)
add_subdirectory(sketch)
add_subdirectory(fea)
add_subdirectory(model)
add_subdirectory(render)
add_subdirectory(app)
