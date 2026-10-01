# -----------------------------------------------------------------------------
# Dependency resolution
#
# Preferred path: vcpkg manifest mode (see vcpkg.json / CMakePresets.json).
# Every package is looked up with find_package() first. Small, header-heavy
# libraries fall back to FetchContent (pinned versions) so the project also
# builds against a system OpenCASCADE without vcpkg.
#
# Exposed targets used by the rest of the build:
#   cadforge::occt       OpenCASCADE toolkits we need (modeling + STEP/STL)
#   glfw                 windowing
#   glm::glm             math
#   nlohmann_json::nlohmann_json
#   imgui::imgui         Dear ImGui incl. GLFW + OpenGL3 backends, docking
#   imguizmo::imguizmo   3D transform gizmo
# -----------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# ---- OpenCASCADE (required) -------------------------------------------------
find_package(OpenCASCADE CONFIG REQUIRED)
message(STATUS "CadForge: OpenCASCADE ${OpenCASCADE_VERSION} (${OpenCASCADE_DIR})")

# OCCT 7.8+ merged the STEP/STL toolkits into TKDE*; older versions use TKSTEP*.
set(_cf_occt_wanted
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
    TKPrim TKBO TKBool TKShHealing TKFillet TKOffset TKMesh TKXSBase
    TKDE TKDESTEP TKDESTL                                   # OCCT >= 7.8
    TKSTEPBase TKSTEPAttr TKSTEP209 TKSTEP TKSTL)           # OCCT <  7.8
set(_cf_occt_libs "")
foreach(_tk IN LISTS _cf_occt_wanted)
    if(TARGET ${_tk})
        list(APPEND _cf_occt_libs ${_tk})
    endif()
endforeach()
if(TARGET TKDESTEP)
    list(REMOVE_ITEM _cf_occt_libs TKSTEPBase TKSTEPAttr TKSTEP209 TKSTEP TKSTL)
endif()
add_library(cadforge_occt INTERFACE)
add_library(cadforge::occt ALIAS cadforge_occt)
target_link_libraries(cadforge_occt INTERFACE ${_cf_occt_libs})
target_include_directories(cadforge_occt SYSTEM INTERFACE ${OpenCASCADE_INCLUDE_DIR})

# ---- glm ---------------------------------------------------------------------
find_package(glm CONFIG QUIET)
if(NOT TARGET glm::glm)
    if(NOT CADFORGE_FETCH_MISSING)
        message(FATAL_ERROR "glm not found")
    endif()
    message(STATUS "CadForge: fetching glm")
    FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glm)
endif()

# ---- nlohmann json -----------------------------------------------------------
find_package(nlohmann_json 3.10 CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    if(NOT CADFORGE_FETCH_MISSING)
        message(FATAL_ERROR "nlohmann_json not found")
    endif()
    message(STATUS "CadForge: fetching nlohmann_json")
    FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(json)
endif()

# ---- GLFW --------------------------------------------------------------------
find_package(glfw3 3.3 CONFIG QUIET)
if(NOT TARGET glfw)
    if(NOT CADFORGE_FETCH_MISSING)
        message(FATAL_ERROR "glfw3 not found")
    endif()
    message(STATUS "CadForge: fetching GLFW")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glfw)
endif()

# ---- Dear ImGui (docking) ----------------------------------------------------
find_package(imgui CONFIG QUIET)
if(NOT TARGET imgui::imgui)
    if(NOT CADFORGE_FETCH_MISSING)
        message(FATAL_ERROR "imgui not found")
    endif()
    message(STATUS "CadForge: fetching Dear ImGui (docking)")
    FetchContent_Declare(imgui_src GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.9b-docking GIT_SHALLOW TRUE
        SOURCE_SUBDIR _cadforge_no_cmake)
    FetchContent_MakeAvailable(imgui_src)
    add_library(cadforge_imgui STATIC
        ${imgui_src_SOURCE_DIR}/imgui.cpp
        ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_src_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_src_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_src_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(cadforge_imgui SYSTEM PUBLIC ${imgui_src_SOURCE_DIR} ${imgui_src_SOURCE_DIR}/backends)
    target_link_libraries(cadforge_imgui PUBLIC glfw)
    add_library(imgui::imgui ALIAS cadforge_imgui)
endif()

# ---- ImGuizmo ----------------------------------------------------------------
find_package(imguizmo CONFIG QUIET)
if(NOT TARGET imguizmo::imguizmo)
    if(NOT CADFORGE_FETCH_MISSING)
        message(FATAL_ERROR "imguizmo not found")
    endif()
    message(STATUS "CadForge: fetching ImGuizmo")
    FetchContent_Declare(imguizmo_src GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git GIT_TAG 1.10 GIT_SHALLOW TRUE
        SOURCE_SUBDIR _cadforge_no_cmake) # we build it ourselves below
    FetchContent_MakeAvailable(imguizmo_src)
    if(EXISTS ${imguizmo_src_SOURCE_DIR}/src/ImGuizmo.cpp)
        set(_gz_dir ${imguizmo_src_SOURCE_DIR}/src)
    else()
        set(_gz_dir ${imguizmo_src_SOURCE_DIR})
    endif()
    add_library(cadforge_imguizmo STATIC ${_gz_dir}/ImGuizmo.cpp)
    target_include_directories(cadforge_imguizmo SYSTEM PUBLIC ${_gz_dir})
    target_link_libraries(cadforge_imguizmo PUBLIC imgui::imgui)
    add_library(imguizmo::imguizmo ALIAS cadforge_imguizmo)
endif()

find_package(OpenGL REQUIRED)
