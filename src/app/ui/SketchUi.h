#pragma once
// Sketch edit mode UI (see SketchUi.cpp).

#include <imgui.h>

namespace cf::app {
class AppContext;
}

namespace cf::app::ui {

class Toolbar;

struct SketchViewportInput {
    ImVec2 pos, size;        // viewport rectangle (screen coordinates)
    bool hovered = false;    // mouse over the viewport
    bool rightClick = false; // right button released without dragging
};

/// Toolbar while editing a sketch (tools, constraints, close).
void drawSketchToolbar(AppContext& ctx, Toolbar& tb);
/// "Sketch / Extrude / Revolve" buttons of the normal toolbar.
void drawSketchCreateToolbar(AppContext& ctx, Toolbar& tb);
/// Picking, dragging, tools and drawing of the sketch over the 3D view.
void sketchViewport(AppContext& ctx, const SketchViewportInput& in);
/// Property-panel content while editing.
void drawSketchPanel(AppContext& ctx);
/// Keyboard handling in sketch mode. Returns true if sketch mode is active
/// (the normal shortcuts are then skipped).
bool handleSketchShortcuts(AppContext& ctx);

} // namespace cf::app::ui
