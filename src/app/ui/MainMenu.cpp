#include "app/ui/SketchUi.h"
#include "app/ui/Ui.h"

#include <LucideIcons.h>

#include "app/AppContext.h"
#include "app/Commands.h"
#include "core/Paths.h"
#include "model/features/PartFeatures.h"
#include "model/features/SketchFeatures.h"

#include <imgui_internal.h>

#include <chrono>
#include <filesystem>
#include <cstdio>

namespace cf::app::ui {

using render::PickFilter;
using render::StandardView;

namespace {

#if defined(__APPLE__)
constexpr const char* kMod = "Cmd";
#else
constexpr const char* kMod = "Ctrl";
#endif

std::string sc(const char* keys)
{
    return std::string(kMod) + "+" + keys;
}

void createMenu(AppContext& ctx)
{
    std::string lastCategory;
    for (const auto& t : ctx.doc.registry().types()) {
        if (t.category != "Primitives")
            continue;
        if (ImGui::MenuItem(t.label.c_str()))
            ctx.createFeature(t.type);
    }
}

} // namespace

void drawMainMenu(AppContext& ctx, bool& quitRequested)
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem(ICON_NEW " New", sc("N").c_str())) cmd::newFile(ctx);
        if (ImGui::MenuItem(ICON_OPEN " Open...", sc("O").c_str())) cmd::open(ctx);
        if (ImGui::BeginMenu(ICON_HISTORY " Open Recent", !ctx.prefs.recentFiles.empty())) {
            std::string chosen;
            for (const auto& f : ctx.prefs.recentFiles) {
                const std::string name = paths::toUtf8(paths::fromUtf8(f).filename());
                if (ImGui::MenuItem((name + "##" + f).c_str()))
                    chosen = f;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", f.c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear List")) {
                ctx.prefs.recentFiles.clear();
                ctx.savePreferences();
            }
            ImGui::EndMenu();
            if (!chosen.empty()) {
                if (std::filesystem::exists(paths::fromUtf8(chosen))) {
                    ctx.guardUnsaved([&ctx, chosen] { ctx.openDocument(chosen); });
                } else {
                    ctx.status("File not found: " + chosen, true);
                    ctx.prefs.removeRecentFile(chosen);
                    ctx.savePreferences();
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_SAVE " Save", sc("S").c_str())) cmd::save(ctx);
        if (ImGui::MenuItem("Save As...", sc("Shift+S").c_str())) cmd::saveAs(ctx);
        ImGui::Separator();
        if (ImGui::MenuItem("Import STEP...", sc("I").c_str())) cmd::importStep(ctx);
        if (ImGui::BeginMenu("Export")) {
            const bool sel = !ctx.selection.empty();
            ImGui::TextDisabled(sel ? "Exports the selection" : "Exports all visible objects");
            if (ImGui::MenuItem("STEP...")) cmd::exportStep(ctx);
            if (ImGui::MenuItem("STL...")) cmd::exportStl(ctx);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Load Demo Scene")) cmd::loadDemo(ctx);
        if (ImGui::MenuItem("Load Analysis Demo")) cmd::loadAnalysisDemo(ctx);
        if (ImGui::MenuItem("Load Sketch Demo")) cmd::loadSketchDemo(ctx);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", sc("Q").c_str())) quitRequested = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        const std::string undo = "Undo " + ctx.history.undoLabel();
        const std::string redo = "Redo " + ctx.history.redoLabel();
        if (ImGui::MenuItem(undo.c_str(), sc("Z").c_str(), false, ctx.history.canUndo())) ctx.undo();
        if (ImGui::MenuItem(redo.c_str(), sc("Shift+Z").c_str(), false, ctx.history.canRedo())) ctx.redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del", false, !ctx.selection.empty())) ctx.deleteSelection();
        if (ImGui::MenuItem("Select All", sc("A").c_str())) ctx.selectAll();
        if (ImGui::MenuItem("Clear Selection", "Esc")) ctx.selection.clear();
        ImGui::Separator();
        if (ImGui::MenuItem("Select Objects", "1", ctx.pickFilter == PickFilter::Object)) ctx.pickFilter = PickFilter::Object;
        if (ImGui::MenuItem("Select Faces", "2", ctx.pickFilter == PickFilter::Face)) ctx.pickFilter = PickFilter::Face;
        if (ImGui::MenuItem("Select Edges", "3", ctx.pickFilter == PickFilter::Edge)) ctx.pickFilter = PickFilter::Edge;
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_SETTINGS " Preferences...", sc(",").c_str())) ctx.showPreferences = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Create")) {
        createMenu(ctx);
        ImGui::Separator();
        if (ImGui::BeginMenu("Sketch")) {
            if (ImGui::MenuItem("On XY plane")) ctx.createSketch(model::SketchFeature::XY);
            if (ImGui::MenuItem("On XZ plane")) ctx.createSketch(model::SketchFeature::XZ);
            if (ImGui::MenuItem("On YZ plane")) ctx.createSketch(model::SketchFeature::YZ);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Extrude")) ctx.profileFeatureFromSelection("Part::Extrude");
        if (ImGui::MenuItem("Revolve")) ctx.profileFeatureFromSelection("Part::Revolve");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Modify")) {
        const bool two = ctx.selection.features().size() >= 2;
        if (ImGui::MenuItem("Union", "U", false, two)) ctx.booleanFromSelection(model::BooleanFeature::Union);
        if (ImGui::MenuItem("Cut", "X", false, two)) ctx.booleanFromSelection(model::BooleanFeature::Cut);
        if (ImGui::MenuItem("Intersect", "N", false, two)) ctx.booleanFromSelection(model::BooleanFeature::Intersect);
        ImGui::Separator();
        if (ImGui::MenuItem("Fillet", "Shift+F")) ctx.dressUpFromSelection("Part::Fillet");
        if (ImGui::MenuItem("Chamfer", "Shift+C")) ctx.dressUpFromSelection("Part::Chamfer");
        ImGui::Separator();
        if (ImGui::MenuItem("Move Gizmo", "W", ctx.gizmo == GizmoMode::Translate)) ctx.gizmo = GizmoMode::Translate;
        if (ImGui::MenuItem("Rotate Gizmo", "E", ctx.gizmo == GizmoMode::Rotate)) ctx.gizmo = GizmoMode::Rotate;
        if (ImGui::MenuItem("No Gizmo", "Q", ctx.gizmo == GizmoMode::None)) ctx.gizmo = GizmoMode::None;
        ImGui::EndMenu();
    }

    drawAnalysisMenu(ctx);

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fit All", "F")) ctx.fitAll();
        if (ImGui::MenuItem("Fit Selection", "Shift+V")) ctx.fitSelection();
        ImGui::MenuItem(ICON_MEASURE " Measure", "M", &ctx.measureMode);
        ImGui::Separator();
        if (ImGui::MenuItem("Isometric", "Keypad 0")) ctx.camera.setStandardView(StandardView::Isometric);
        if (ImGui::MenuItem("Front", "Keypad 1")) ctx.camera.setStandardView(StandardView::Front);
        if (ImGui::MenuItem("Back", "Ctrl+Keypad 1")) ctx.camera.setStandardView(StandardView::Back);
        if (ImGui::MenuItem("Right", "Keypad 3")) ctx.camera.setStandardView(StandardView::Right);
        if (ImGui::MenuItem("Left", "Ctrl+Keypad 3")) ctx.camera.setStandardView(StandardView::Left);
        if (ImGui::MenuItem("Top", "Keypad 7")) ctx.camera.setStandardView(StandardView::Top);
        if (ImGui::MenuItem("Bottom", "Ctrl+Keypad 7")) ctx.camera.setStandardView(StandardView::Bottom);
        ImGui::Separator();
        bool ortho = ctx.camera.orthographic();
        if (ImGui::MenuItem("Orthographic", "Keypad 5 / O", &ortho)) ctx.camera.setOrthographic(ortho);
        ImGui::MenuItem("Shaded Faces", nullptr, &ctx.settings.showFaces);
        ImGui::MenuItem("Edges", nullptr, &ctx.settings.showEdges);
        ImGui::MenuItem("Grid", "G", &ctx.settings.showGrid);
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderFloat("Edge width", &ctx.prefs.edgeWidth, 0.5f, 4.0f, "%.1f px"))
            ctx.applyPreferences();
        ImGui::Separator();
        ImGui::MenuItem("Console", nullptr, &ctx.showConsole);
        ImGui::MenuItem("ImGui Demo", nullptr, &ctx.showImGuiDemo);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Mouse & Keyboard")) ctx.showControls = true;
        if (ImGui::MenuItem("About CadForge")) ctx.showAbout = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void drawStatusBar(AppContext& ctx)
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_MenuBar;
    const float height = ImGui::GetFrameHeight();
    if (ImGui::BeginViewportSideBar("##StatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, height, flags)) {
        if (ImGui::BeginMenuBar()) {
            const double age = std::chrono::duration<double>(
                                   std::chrono::steady_clock::now().time_since_epoch()).count() - ctx.statusTime();
            if (ctx.fea.busy()) {
                ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "%s  %.0f%%", ctx.fea.progressText().c_str(),
                                   ctx.fea.progress() * 100.0);
            } else if (!ctx.statusText().empty() && (age < 12.0 || ctx.statusIsError())) {
                if (ctx.statusIsError())
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", ctx.statusText().c_str());
                else
                    ImGui::TextUnformatted(ctx.statusText().c_str());
            } else {
                ImGui::TextDisabled("Ready");
            }

            char right[256];
            const char* filter = ctx.pickFilter == PickFilter::Object ? "Objects"
                                 : ctx.pickFilter == PickFilter::Face ? "Faces"
                                                                      : "Edges";
            std::snprintf(right, sizeof(right), "Select: %s  |  Grid %g mm  |  %s  |  %d features  |  recompute %.1f ms",
                          filter, ctx.renderer.gridSpacing(), ctx.camera.orthographic() ? "Ortho" : "Persp",
                          int(ctx.doc.features().size()), ctx.lastRecompute.milliseconds);
            const float w = ImGui::CalcTextSize(right).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - w - ImGui::GetStyle().WindowPadding.x * 2);
            ImGui::TextDisabled("%s", right);
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

void handleShortcuts(AppContext& ctx, bool& quitRequested)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
        return;
    // Application-wide shortcuts: exact modifier match, independent of window
    // focus (ImGuiMod_Ctrl is Cmd on macOS). Text fields keep their keys (see above).
    const auto pressed = [&](ImGuiKeyChord chord) {
        const ImGuiKey key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
        const int mods = chord & ImGuiMod_Mask_;
        return ImGui::IsKeyPressed(key, false) && (io.KeyMods & ImGuiMod_Mask_) == mods;
    };

    if (pressed(ImGuiMod_Ctrl | ImGuiKey_S)) cmd::save(ctx);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) ctx.showPreferences = !ctx.showPreferences;
    if (handleSketchShortcuts(ctx))
        return; // sketch edit mode has its own keys

    // File / edit (ImGuiMod_Ctrl maps to Cmd on macOS).
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_N)) cmd::newFile(ctx);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_O)) cmd::open(ctx);
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) cmd::saveAs(ctx);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_I)) cmd::importStep(ctx);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Q)) quitRequested = true;
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Z)) ctx.undo();
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) || pressed(ImGuiMod_Ctrl | ImGuiKey_Y)) ctx.redo();
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_A)) ctx.selectAll();

    // Everything below only when no popup/modal is open.
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return;

    if (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace)) ctx.deleteSelection();
    if (pressed(ImGuiKey_Escape)) {
        if (ctx.shapeEdit.active())
            ctx.cancelSubShapeEdit();
        else
            ctx.selection.clear();
    }
    if (ctx.shapeEdit.active() && (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)))
        ctx.applySubShapeEdit();

    if (pressed(ImGuiKey_1)) ctx.pickFilter = PickFilter::Object;
    if (pressed(ImGuiKey_2)) ctx.pickFilter = PickFilter::Face;
    if (pressed(ImGuiKey_3)) ctx.pickFilter = PickFilter::Edge;
    if (pressed(ImGuiKey_W)) ctx.gizmo = GizmoMode::Translate;
    if (pressed(ImGuiKey_E)) ctx.gizmo = GizmoMode::Rotate;
    if (pressed(ImGuiKey_Q)) ctx.gizmo = GizmoMode::None;
    if (pressed(ImGuiKey_F)) ctx.fitAll();
    if (pressed(ImGuiKey_M)) ctx.measureMode = !ctx.measureMode;
    if (pressed(ImGuiMod_Shift | ImGuiKey_V)) ctx.fitSelection();
    if (pressed(ImGuiKey_G)) ctx.settings.showGrid = !ctx.settings.showGrid;
    if (pressed(ImGuiKey_O) || pressed(ImGuiKey_Keypad5)) ctx.camera.setOrthographic(!ctx.camera.orthographic());
    if (pressed(ImGuiKey_U)) ctx.booleanFromSelection(model::BooleanFeature::Union);
    if (pressed(ImGuiKey_X)) ctx.booleanFromSelection(model::BooleanFeature::Cut);
    if (pressed(ImGuiKey_N)) ctx.booleanFromSelection(model::BooleanFeature::Intersect);
    if (pressed(ImGuiMod_Shift | ImGuiKey_F)) ctx.dressUpFromSelection("Part::Fillet");
    if (pressed(ImGuiMod_Shift | ImGuiKey_C)) ctx.dressUpFromSelection("Part::Chamfer");

    if (pressed(ImGuiKey_F5) && !ctx.fea.busy()) {
        if (FeatureId a = ctx.contextAnalysis(); a != kNoFeature)
            ctx.solveAnalysis(a);
        else
            ctx.status("Solve: select the analysis to solve", true);
    }

    if (pressed(ImGuiKey_Keypad0)) ctx.camera.setStandardView(StandardView::Isometric);
    if (pressed(ImGuiKey_Keypad1)) ctx.camera.setStandardView(StandardView::Front);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Keypad1)) ctx.camera.setStandardView(StandardView::Back);
    if (pressed(ImGuiKey_Keypad3)) ctx.camera.setStandardView(StandardView::Right);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Keypad3)) ctx.camera.setStandardView(StandardView::Left);
    if (pressed(ImGuiKey_Keypad7)) ctx.camera.setStandardView(StandardView::Top);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Keypad7)) ctx.camera.setStandardView(StandardView::Bottom);
}

} // namespace cf::app::ui
