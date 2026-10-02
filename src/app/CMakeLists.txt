# Desktop application (GLFW + Dear ImGui).
add_executable(CadForge
    main.cpp
    Application.cpp
    AppContext.cpp
    Commands.cpp
    SceneView.cpp
    SketchEditor.cpp
    Preferences.cpp
    Autosave.cpp
    FeaController.cpp
    FileDialogs.cpp
    ui/Theme.cpp
    ui/MainMenu.cpp
    ui/ModelTree.cpp
    ui/Properties.cpp
    ui/Viewport.cpp
    ui/Console.cpp
    ui/Dialogs.cpp
    ui/AnalysisUi.cpp
    ui/SketchUi.cpp
    ui/MeasureUi.cpp
    ui/PreferencesUi.cpp)

target_include_directories(CadForge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(CadForge PRIVATE
    cadforge::model
    cadforge::fea
    cadforge::render
    cadforge::pfd
    cadforge::lucide
    imgui::imgui
    imguizmo::imguizmo
    glfw)

if(WIN32)
    target_link_libraries(CadForge PRIVATE ole32 shell32 comdlg32 uuid userenv)
    set_target_properties(CadForge PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    if(MSVC)
        # Keep a normal main() entry point even as a GUI subsystem app.
        target_link_options(CadForge PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
elseif(APPLE)
    set_target_properties(CadForge PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "CadForge"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.cadforge.CadForge"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    # OpenGL is deprecated (not removed) on macOS; silence the noise.
    target_compile_definitions(CadForge PRIVATE GL_SILENCE_DEPRECATION)
endif()

cadforge_set_warnings(CadForge)
