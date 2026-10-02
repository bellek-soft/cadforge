# Finite element analysis engine: meshing (Netgen) + linear static solver (Eigen).
# Depends only on `core`; geometry arrives as a tessellated surface (MeshData).
add_library(cadforge_fea STATIC
    FeaTypes.cpp
    MeshUtils.cpp
    Mesher.cpp
    Solver.cpp
    Post.cpp
    Export.cpp)
target_link_libraries(cadforge_fea
    PUBLIC  cadforge::core
    PRIVATE cadforge::netgen cadforge::amgcl cadforge::spectra Eigen3::Eigen)
cadforge_set_warnings(cadforge_fea)
add_library(cadforge::fea ALIAS cadforge_fea)
