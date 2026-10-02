# glad: OpenGL 4.1 core loader, generated with glad2 (gl:core=4.1). Vendored -> no dependency.
add_library(cadforge_glad STATIC glad/src/gl.c)
target_include_directories(cadforge_glad SYSTEM PUBLIC glad/include)
add_library(cadforge::glad ALIAS cadforge_glad)

# portable-file-dialogs: header-only native file dialogs (WTFPL). Vendored.
add_library(cadforge_pfd INTERFACE)
target_include_directories(cadforge_pfd SYSTEM INTERFACE pfd)
add_library(cadforge::pfd ALIAS cadforge_pfd)

# AMGCL 1.4.9: header-only algebraic multigrid (MIT). Vendored (no vcpkg port);
# used without Boost.
add_library(cadforge_amgcl INTERFACE)
target_include_directories(cadforge_amgcl SYSTEM INTERFACE amgcl)
target_compile_definitions(cadforge_amgcl INTERFACE AMGCL_NO_BOOST)
add_library(cadforge::amgcl ALIAS cadforge_amgcl)

# PlaneGCS: FreeCAD's 2D sketch constraint solver (LGPL-2.1+), vendored with
# Boost/FreeCAD shims (see planegcs/README.md). Needs Eigen.
add_library(cadforge_planegcs STATIC
    planegcs/GCS.cpp
    planegcs/Geo.cpp
    planegcs/Constraints.cpp
    planegcs/SubSystem.cpp
    planegcs/qp_eq.cpp)
target_include_directories(cadforge_planegcs
    SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/planegcs
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/planegcs/compat)
target_link_libraries(cadforge_planegcs PUBLIC Eigen3::Eigen)
target_compile_features(cadforge_planegcs PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(cadforge_planegcs PRIVATE /w /bigobj)
    target_compile_definitions(cadforge_planegcs PRIVATE _USE_MATH_DEFINES NOMINMAX)
else()
    target_compile_options(cadforge_planegcs PRIVATE -w)
endif()
add_library(cadforge::planegcs ALIAS cadforge_planegcs)

# Spectra 1.0.1: header-only sparse eigensolvers (MPL-2.0), vendored. Needs Eigen.
add_library(cadforge_spectra INTERFACE)
target_include_directories(cadforge_spectra SYSTEM INTERFACE spectra/include)
target_link_libraries(cadforge_spectra INTERFACE Eigen3::Eigen)
add_library(cadforge::spectra ALIAS cadforge_spectra)
