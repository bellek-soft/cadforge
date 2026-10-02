#include "app/ui/Ui.h"
#include "app/ui/SketchUi.h"
#include "app/ui/Toolbar.h"

#include <LucideIcons.h>

#include "app/AppContext.h"
#include "core/Placement.h"
#include "model/features/PartFeatures.h"

#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace cf::app::ui {

using render::PickFilter;
using render::PickKind;
using render::StandardView;

namespace {

struct ViewportInput {
    ImVec2 pressPos;
    bool dragging = false;
    ImGuiMouseButton pressButton = -1;
    bool gizmoWasUsing = false;
    bool gizmoShown = false;
};
ViewportInput g_in;

void drawToolbar(AppContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    Toolbar tb(ctx.prefs.toolbarLabels);
    if (ctx.sketchEdit.active()) {
        drawSketchToolbar(ctx, tb);
        ImGui::PopStyleVar(2);
        return;
    }

    for (const auto& t : ctx.doc.registry().types()) {
        if (t.category != "Primitives")
            continue;
        const char* icon = t.type == "Part::Box"        ? ICON_BOX
                           : t.type == "Part::Cylinder" ? ICON_CYLINDER
                           : t.type == "Part::Sphere"   ? ICON_SPHERE
                           : t.type == "Part::Cone"     ? ICON_CONE
                                                        : ICON_TORUS;
        if (tb.button(icon, t.label.c_str(), ("Create a " + t.label).c_str()))
            ctx.createFeature(t.type);
    }
    tb.separator();
    drawSketchCreateToolbar(ctx, tb);
    tb.separator();
    if (tb.button(ICON_UNION, "Union", "Union of the selected objects (U)"))
        ctx.booleanFromSelection(model::BooleanFeature::Union);
    if (tb.button(ICON_CUT, "Cut", "Subtract the other selected objects from the first one (X)"))
        ctx.booleanFromSelection(model::BooleanFeature::Cut);
    if (tb.button(ICON_INTERSECT, "Intersect", "Common volume of the selected objects (N)"))
        ctx.booleanFromSelection(model::BooleanFeature::Intersect);
    tb.separator();
    if (tb.button(ICON_FILLET, "Fillet", "Round the selected edges (Shift+F)"))
        ctx.dressUpFromSelection("Part::Fillet");
    if (tb.button(ICON_CHAMFER, "Chamfer", "Bevel the selected edges (Shift+C)"))
        ctx.dressUpFromSelection("Part::Chamfer");
    tb.separator();
    tb.text("Select");
    if (tb.button(ICON_SELECT, "Obj", "Select whole objects (1)", ctx.pickFilter == PickFilter::Object))
        ctx.pickFilter = PickFilter::Object;
    if (tb.button(ICON_FACE, "Face", "Select faces (2)", ctx.pickFilter == PickFilter::Face))
        ctx.pickFilter = PickFilter::Face;
    if (tb.button(ICON_EDGE, "Edge", "Select edges (3)", ctx.pickFilter == PickFilter::Edge))
        ctx.pickFilter = PickFilter::Edge;
    tb.separator();
    if (tb.button(ICON_MOVE, "Move", "Translate gizmo (W)", ctx.gizmo == GizmoMode::Translate))
        ctx.gizmo = GizmoMode::Translate;
    if (tb.button(ICON_ROTATE, "Rotate", "Rotate gizmo (E)", ctx.gizmo == GizmoMode::Rotate))
        ctx.gizmo = GizmoMode::Rotate;
    if (tb.button(ICON_OFF, "Off", "Hide gizmo (Q)", ctx.gizmo == GizmoMode::None))
        ctx.gizmo = GizmoMode::None;
    tb.separator();
    if (tb.button(ICON_MEASURE, "Measure", "Measure the selection: one item, or the distance / angle between two "
                                           "(Ctrl+click). Key M", ctx.measureMode))
        ctx.measureMode = !ctx.measureMode;
    tb.separator();
    drawAnalysisToolbar(ctx, tb);

    ImGui::PopStyleVar(2);
}

/// View buttons overlaid in the top-right corner of the 3D view.
void drawViewOverlay(AppContext& ctx, const ImVec2& origin, const ImVec2& size)
{
    struct Btn { const char* label; StandardView v; const char* tip; };
    static const Btn buttons[] = {
        {"Iso", StandardView::Isometric, "Isometric (Keypad 0)"},
        {"Front", StandardView::Front, "Front (Keypad 1)"},
        {"Right", StandardView::Right, "Right (Keypad 3)"},
        {"Top", StandardView::Top, "Top (Keypad 7)"},
    };
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 3));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.13f, 0.15f, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.93f, 0.95f, 1.0f)); // on the 3D view in any theme
    float x = origin.x + size.x - 8.0f;
    const float y = origin.y + 8.0f;
    auto place = [&](const char* label) {
        const float w = ImGui::CalcTextSize(label).x + 12.0f;
        x -= w;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        x -= 4.0f;
    };
    place("Fit");
    if (ImGui::Button("Fit"))
        ctx.fitAll();
    place(ctx.camera.orthographic() ? "Ortho" : "Persp");
    if (ImGui::Button(ctx.camera.orthographic() ? "Ortho" : "Persp"))
        ctx.camera.setOrthographic(!ctx.camera.orthographic());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Toggle orthographic / perspective (O)");
    for (int i = int(std::size(buttons)) - 1; i >= 0; --i) {
        place(buttons[i].label);
        if (ImGui::Button(buttons[i].label))
            ctx.camera.setStandardView(buttons[i].v);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", buttons[i].tip);
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

/// XYZ axis triad in the bottom-left corner.
void drawAxisTriad(AppContext& ctx, ImDrawList* dl, const ImVec2& origin, const ImVec2& size)
{
    const ImVec2 c(origin.x + 48.0f, origin.y + size.y - 48.0f);
    const float len = 34.0f;
    const glm::dmat3 rot(ctx.camera.view());
    struct Axis { Vec3 dir; ImU32 col; const char* name; double depth; ImVec2 tip; };
    Axis axes[3] = {
        {{1, 0, 0}, IM_COL32(230, 80, 80, 255), "X", 0, {}},
        {{0, 1, 0}, IM_COL32(100, 200, 100, 255), "Y", 0, {}},
        {{0, 0, 1}, IM_COL32(90, 150, 240, 255), "Z", 0, {}},
    };
    for (auto& a : axes) {
        const Vec3 v = rot * a.dir;
        a.depth = v.z;
        a.tip = ImVec2(c.x + float(v.x) * len, c.y - float(v.y) * len);
    }
    std::sort(std::begin(axes), std::end(axes), [](const Axis& a, const Axis& b) { return a.depth < b.depth; });
    dl->AddCircleFilled(c, len + 12.0f, IM_COL32(20, 22, 26, 110));
    for (const auto& a : axes) {
        dl->AddLine(c, a.tip, a.col, 2.5f);
        dl->AddCircleFilled(a.tip, 8.0f, a.col);
        const ImVec2 ts = ImGui::CalcTextSize(a.name);
        dl->AddText(ImVec2(a.tip.x - ts.x * 0.5f, a.tip.y - ts.y * 0.5f), IM_COL32(15, 15, 15, 255), a.name);
    }
}

/// Transform gizmo for a single selected feature.
void drawGizmo(AppContext& ctx, const ImVec2& origin, const ImVec2& size)
{
    const auto feats = ctx.selection.features();
    const bool usable = ctx.gizmo != GizmoMode::None && feats.size() == 1 && !ctx.shapeEdit.active() &&
                        !ctx.sketchEdit.active() && ctx.pickFilter == PickFilter::Object;
    model::Feature* f = usable ? ctx.doc.find(feats[0]) : nullptr;
    const model::Property* pos = f ? f->props().find(model::Feature::kPosition) : nullptr;
    g_in.gizmoShown = f && f->visible() && f->producesGeometry() && pos && !pos->hidden;
    if (!g_in.gizmoShown) {
        g_in.gizmoWasUsing = false;
        return;
    }

    ImGuizmo::SetOrthographic(ctx.camera.orthographic());
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);

    const glm::mat4 view(ctx.camera.view());
    const glm::mat4 proj(ctx.camera.projection());
    glm::mat4 model(f->placement().matrix());
    const auto op = ctx.gizmo == GizmoMode::Translate ? ImGuizmo::TRANSLATE : ImGuizmo::ROTATE;
    // Move along world axes; rotate around the part's own axes.
    const auto mode = op == ImGuizmo::TRANSLATE ? ImGuizmo::WORLD : ImGuizmo::LOCAL;

    // Hold Ctrl to snap (1 mm / 15 deg).
    const bool snap = ImGui::GetIO().KeyCtrl;
    const float snapValues[3] = {op == ImGuizmo::TRANSLATE ? 1.0f : 15.0f, 1.0f, 1.0f};
    if (snap && op == ImGuizmo::TRANSLATE) {
        // grid-aware snapping step
        const float step = float(ctx.renderer.gridSpacing());
        const float s[3] = {step, step, step};
        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, mode,
                             glm::value_ptr(model), nullptr, s);
    } else {
        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, mode,
                             glm::value_ptr(model), nullptr, snap ? snapValues : nullptr);
    }

    if (ImGuizmo::IsUsing()) {
        if (f->setPlacement(Placement::fromMatrix(Mat4(model))))
            ctx.recompute();
        g_in.gizmoWasUsing = true;
    } else if (g_in.gizmoWasUsing) {
        g_in.gizmoWasUsing = false;
        ctx.commit(op == ImGuizmo::TRANSLATE ? "Move " + f->name() : "Rotate " + f->name());
    }
}

void handlePick(AppContext& ctx, const render::PickResult& hit, bool additive)
{
    const FeatureId fid = ctx.scene.featureForPickId(hit.pickId);
    if (!hit || fid == kNoFeature) {
        if (!additive)
            ctx.selection.clear();
        return;
    }
    if (ctx.shapeEdit.active() && fid != ctx.shapeEdit.base)
        return; // only edges of the base can be picked while editing
    SelItem item{fid};
    if (ctx.pickFilter != PickFilter::Object) {
        if (hit.index <= 0)
            return; // e.g. analysis mesh wireframe: not a CAD sub-shape
        item = {fid, hit.kind, hit.index};
    }
    if (additive || ctx.shapeEdit.active())
        ctx.selection.toggle(item);
    else
        ctx.selection.set(item);
}

} // namespace

void drawViewport(AppContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    const bool open = ImGui::Begin(kViewportWindow, nullptr, ImGuiWindowFlags_NoScrollbar |
                                                              ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::End();
        return;
    }

    drawToolbar(ctx);

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 32.0f);
    size.y = std::max(size.y, 32.0f);
    const ImVec2 fbScale = io.DisplayFramebufferScale;
    const int pxW = std::max(1, int(size.x * fbScale.x));
    const int pxH = std::max(1, int(size.y * fbScale.y));
    ctx.camera.setViewport(pxW, pxH);

    // Interaction surface. While the mouse is over the gizmo we must not submit
    // an interactive item there, otherwise ImGuizmo refuses to activate.
    const ImVec2 mouse = io.MousePos;
    const bool mouseInside = mouse.x >= pos.x && mouse.y >= pos.y && mouse.x < pos.x + size.x &&
                             mouse.y < pos.y + size.y;
    const bool gizmoBusy = g_in.gizmoShown && (ImGuizmo::IsUsing() || (mouseInside && ImGuizmo::IsOver())) &&
                           g_in.pressButton != ImGuiMouseButton_Right && g_in.pressButton != ImGuiMouseButton_Middle;
    bool hovered = false, active = false;
    if (gizmoBusy) {
        ImGui::Dummy(size);
        hovered = ImGui::IsWindowHovered() && mouseInside;
    } else {
        ImGui::InvisibleButton("##viewport", size,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        hovered = ImGui::IsItemHovered();
        active = ImGui::IsItemActive();
    }
    const double mx = (mouse.x - pos.x) * fbScale.x;
    const double my = (mouse.y - pos.y) * fbScale.y;

    // --- camera navigation ---
    if (hovered && io.MouseWheel != 0.0f)
        ctx.camera.zoom(io.MouseWheel * ctx.prefs.zoomSpeed * (ctx.prefs.invertZoom ? -1.0f : 1.0f), mx, my);

    if (active) {
        for (ImGuiMouseButton b : {ImGuiMouseButton_Left, ImGuiMouseButton_Right, ImGuiMouseButton_Middle}) {
            if (ImGui::IsMouseClicked(b)) {
                g_in.pressPos = mouse;
                g_in.pressButton = b;
                g_in.dragging = false;
            }
        }
        const ImVec2 d = io.MouseDelta;
        const ImGuiMouseButton orbitButton = ImGuiMouseButton(ctx.prefs.orbitButton);
        const ImGuiMouseButton panButton = ImGuiMouseButton(ctx.prefs.panButton);
        const bool orbitDown = ImGui::IsMouseDown(orbitButton) || (io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left));
        const bool panDown = ImGui::IsMouseDown(panButton);
        if (ImGui::IsMouseDragging(g_in.pressButton, 3.0f))
            g_in.dragging = true;
        const bool leftDragAllowed =
            orbitButton != ImGuiMouseButton_Left || (g_in.dragging && (!ctx.sketchEdit.active() || io.KeyAlt));
        if ((orbitDown || panDown) && leftDragAllowed && (d.x != 0 || d.y != 0)) {
            // Configurable buttons (Preferences); Shift swaps orbit and pan.
            const bool pan = panDown ? !io.KeyShift : io.KeyShift;
            if (pan)
                ctx.camera.pan(d.x * fbScale.x, d.y * fbScale.y);
            else
                ctx.camera.orbit(d.x, d.y);
        }
    }

    // --- render ---
    const auto& items = ctx.scene.build(ctx);
    BoundingBox sceneBox = ctx.scene.bounds();
    ctx.camera.setSceneBounds(sceneBox);

    // Hover picking (only when the mouse is idle over the viewport).
    ctx.hover = {};
    ctx.hoverFeature = kNoFeature;
    const bool sketching = ctx.sketchEdit.active();
    if (hovered && !gizmoBusy && !sketching && !ImGui::IsAnyMouseDown()) {
        const int radius = ctx.pickFilter == PickFilter::Edge ? int(5 * fbScale.x) : 1;
        ctx.renderer.resize(pxW, pxH);
        ctx.hover = ctx.renderer.pick(ctx.camera, items, ctx.pickFilter, int(mx), int(my), radius,
                                      ctx.settings.edgeWidth * fbScale.x);
        ctx.hoverFeature = ctx.scene.featureForPickId(ctx.hover.pickId);
        if (ctx.shapeEdit.active() && ctx.hoverFeature != ctx.shapeEdit.base)
            ctx.hover = {}, ctx.hoverFeature = kNoFeature;
        ctx.scene.build(ctx); // refresh hover highlight
    }
    render::RenderSettings rs = ctx.settings;
    rs.edgeWidth *= fbScale.x; // keep the on-screen width constant on HiDPI displays
    ctx.renderer.render(ctx.camera, ctx.scene.items(), rs, pxW, pxH);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddImage(static_cast<ImTextureID>(static_cast<std::uintptr_t>(ctx.renderer.colorTexture())), pos,
                 ImVec2(pos.x + size.x, pos.y + size.y), ImVec2(0, 1), ImVec2(1, 0));

    // --- probe (analysis results) ---
    const bool leftClick = hovered && !gizmoBusy && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                           !g_in.dragging && g_in.pressButton == ImGuiMouseButton_Left && !io.KeyAlt &&
                           !g_in.gizmoWasUsing;
    const bool probed = !sketching && analysisProbe(ctx, pos, size, (hovered && !ImGui::IsAnyMouseDown()) || leftClick,
                                                    leftClick);

    // --- click selection (left release without drag) ---
    if (leftClick && !sketching && !probed) {
        const int radius = ctx.pickFilter == PickFilter::Edge ? int(5 * fbScale.x) : 1;
        const auto hit = ctx.renderer.pick(ctx.camera, ctx.scene.items(), ctx.pickFilter, int(mx), int(my),
                                           radius, ctx.settings.edgeWidth * fbScale.x);
        handlePick(ctx, hit, io.KeyCtrl || io.KeyShift || io.KeySuper);
    }

    // --- context menu (right click without drag) ---
    const bool rightClick = hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !g_in.dragging &&
                            g_in.pressButton == ImGuiMouseButton_Right;
    if (rightClick && !sketching)
        ImGui::OpenPopup("##viewctx");
    if (ImGui::BeginPopup("##viewctx")) {
        const bool sel = !ctx.selection.empty();
        if (ImGui::MenuItem("Fit All", "F")) ctx.fitAll();
        if (ImGui::MenuItem("Fit Selection", "Shift+V", false, sel)) ctx.fitSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Hide", nullptr, false, sel))
            for (FeatureId id : ctx.selection.features())
                ctx.setVisible(id, false);
        if (ImGui::MenuItem("Delete", "Del", false, sel)) ctx.deleteSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Clear Selection", "Esc", false, sel)) ctx.selection.clear();
        ImGui::EndPopup();
    }

    // --- overlays ---
    if (sketching) {
        SketchViewportInput sin;
        sin.pos = pos;
        sin.size = size;
        sin.hovered = hovered;
        sin.rightClick = rightClick;
        sketchViewport(ctx, sin);
    }
    drawGizmo(ctx, pos, size);
    drawAxisTriad(ctx, dl, pos, size);
    drawAnalysisOverlay(ctx, dl, pos, size);
    drawMeasureOverlay(ctx, dl, pos);
    drawViewOverlay(ctx, pos, size);

    if (ctx.shapeEdit.active()) {
        const char* msg = ctx.shapeEdit.kind == PickKind::Face
                              ? "Face edit: click faces, Enter = apply, Esc = cancel"
                              : "Edge edit: click edges, Enter = apply, Esc = cancel";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        const ImVec2 p(pos.x + (size.x - ts.x) * 0.5f, pos.y + 12.0f);
        dl->AddRectFilled(ImVec2(p.x - 10, p.y - 5), ImVec2(p.x + ts.x + 10, p.y + ts.y + 5),
                          IM_COL32(40, 30, 10, 220), 4.0f);
        dl->AddText(p, IM_COL32(255, 200, 90, 255), msg);
    }
    if (ctx.doc.features().empty() && !sketching) {
        const char* msg = "Create a primitive or a sketch from the toolbar, or File > Load Demo Scene";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + size.y * 0.5f - ts.y),
                    IM_COL32(200, 205, 215, 160), msg);
    }

    // Hover tooltip with the sub-shape under the cursor.
    if (ctx.hoverFeature != kNoFeature && ctx.pickFilter != PickFilter::Object) {
        if (const model::Feature* f = ctx.doc.find(ctx.hoverFeature)) {
            const char* kind = ctx.hover.kind == PickKind::Face ? "Face" : "Edge";
            ImGui::SetTooltip("%s  %s%d", f->name().c_str(), kind, ctx.hover.index);
        }
    }

    if (!ImGui::IsAnyMouseDown())
        g_in.pressButton = -1;
    ImGui::End();
}

} // namespace cf::app::ui
