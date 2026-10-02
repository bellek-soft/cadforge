#include "app/SketchEditor.h"
#include "app/AppContext.h"

#include "model/features/SketchFeatures.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace cf::app {

using sketch::Constraint;
using sketch::ConstraintType;
using sketch::GeoType;
using sketch::Geometry;
using sketch::PointPos;
using sketch::Ref;
using sketch::Sketch;

namespace {

constexpr double kAutoHvTolerance = 0.035; // ~2 degrees

Constraint make(ConstraintType t, Ref a, Ref b = {}, Ref c = {})
{
    Constraint k;
    k.type = t;
    k.a = a;
    k.b = b;
    k.c = c;
    return k;
}

model::SketchFeature* sketchFeature(AppContext& ctx, FeatureId id)
{
    return dynamic_cast<model::SketchFeature*>(ctx.doc.find(id));
}

} // namespace

// ---- session ----------------------------------------------------------------------

bool SketchEditor::begin(AppContext& ctx, FeatureId id)
{
    model::SketchFeature* sk = sketchFeature(ctx, id);
    if (!sk)
        return false;
    if (active())
        finish(ctx);
    ctx.cancelSubShapeEdit();

    m_feature = id;
    m_sketch = sk->sketch();
    m_original = m_sketch;
    m_frame = sk->frame();
    m_undo.clear();
    m_redo.clear();
    m_message.clear();
    m_dragging = false;
    resolve();
    resetTool();
    clearSelection();
    tool = SketchTool::Select;

    // Look at the sketch plane, orthographic, sketch grid instead of the XY grid.
    m_savedCamera = ctx.camera.state();
    m_savedOrtho = ctx.camera.orthographic();
    m_savedGrid = ctx.settings.showGrid;
    ctx.camera.setOrthographic(true);
    ctx.settings.showGrid = false;

    const int plane = sk->props().get<int>(model::SketchFeature::kPlane);
    const render::StandardView view = plane == model::SketchFeature::XY   ? render::StandardView::Top
                                      : plane == model::SketchFeature::XZ ? render::StandardView::Front
                                                                          : render::StandardView::Right;
    Vec3 target = m_frame.toWorld(m_frame.toLocal(m_savedCamera.target));
    double distance = m_savedCamera.distance;
    if (!sk->shape().isNull() && sk->shape().edgeCount() > 0) {
        const BoundingBox bb = sk->shape().bounds();
        target = bb.center();
        distance = std::max(bb.diagonal() * 1.4, 20.0) / (2.0 * std::tan(glm::radians(ctx.camera.fovDeg()) * 0.5));
    }
    ctx.camera.viewFrom(view, target, distance);

    ctx.selection.set({id});
    ctx.status("Editing " + sk->name() + " - draw with the tools above, Close to finish");
    return true;
}

void SketchEditor::finish(AppContext& ctx)
{
    if (!active())
        return;
    const FeatureId id = m_feature;
    m_feature = kNoFeature;
    ctx.camera.setOrthographic(m_savedOrtho);
    ctx.settings.showGrid = m_savedGrid;
    ctx.camera.setState(m_savedCamera);
    if (model::SketchFeature* sk = sketchFeature(ctx, id)) {
        sk->setSketch(m_sketch);
        if (m_sketch == m_original) {
            ctx.recompute();
        } else {
            ctx.commit("Edit " + sk->name());
            ctx.status("Finished editing " + sk->name() + " (" + m_result.message + ")");
        }
    }
}

void SketchEditor::cancel(AppContext& ctx)
{
    if (!active())
        return;
    const FeatureId id = m_feature;
    m_feature = kNoFeature;
    ctx.camera.setOrthographic(m_savedOrtho);
    ctx.settings.showGrid = m_savedGrid;
    ctx.camera.setState(m_savedCamera);
    if (model::SketchFeature* sk = sketchFeature(ctx, id)) {
        sk->setSketch(m_original);
        ctx.recompute();
        ctx.status("Sketch changes discarded");
    }
}

void SketchEditor::pushToDocument(AppContext& ctx)
{
    if (model::SketchFeature* sk = sketchFeature(ctx, m_feature)) {
        sk->setSketch(m_sketch);
        ctx.recompute();
    }
}

void SketchEditor::resolve()
{
    Sketch t = m_sketch;
    m_result = sketch::solve(t);
    if (m_result.ok())
        m_sketch = std::move(t);
}

bool SketchEditor::commit(AppContext& ctx, Sketch s, const std::string& what)
{
    Sketch t = s;
    const sketch::SolveResult r = sketch::solve(t);
    if (!r.ok() && m_result.ok()) {
        m_message = what + ": " + r.message;
        ctx.status(m_message, true);
        return false;
    }
    m_undo.push_back(m_sketch);
    if (m_undo.size() > 200)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_sketch = r.ok() ? std::move(t) : std::move(s);
    m_result = r;
    m_message.clear();
    pushToDocument(ctx);
    return true;
}

bool SketchEditor::tryAdd(Sketch& s, const Constraint& c)
{
    if (!s.validRef(c.a) || (c.b.valid() && !s.validRef(c.b)) || (c.c.valid() && !s.validRef(c.c)))
        return false;
    Sketch base = s;
    const sketch::SolveResult r0 = sketch::solve(base);
    Sketch t = s;
    t.addConstraint(c);
    const sketch::SolveResult r = sketch::solve(t);
    if (!r.ok() || !r.malformed.empty() || r.redundant.size() > r0.redundant.size())
        return false;
    s = std::move(t);
    return true;
}

void SketchEditor::snapConstraint(Sketch& s, const SketchSnap& snap, const Ref& target)
{
    if (!snap.ref.valid())
        return;
    if (snap.ref.isPoint())
        tryAdd(s, make(ConstraintType::Coincident, target, snap.ref));
    else
        tryAdd(s, make(ConstraintType::PointOnObject, target, snap.ref));
}

// ---- tools & selection --------------------------------------------------------------

void SketchEditor::setTool(SketchTool t)
{
    tool = t;
    resetTool();
    if (t != SketchTool::Select)
        clearSelection();
}

void SketchEditor::resetTool()
{
    toolPoints.clear();
    toolSnaps.clear();
    chainLine = -1;
    chainStartLine = -1;
}

bool SketchEditor::isSelected(const Ref& r) const
{
    return std::find(selection.begin(), selection.end(), r) != selection.end();
}

void SketchEditor::toggleSelect(const Ref& r, bool additive)
{
    if (!additive) {
        selection = {r};
        selectedConstraints.clear();
        return;
    }
    auto it = std::find(selection.begin(), selection.end(), r);
    if (it == selection.end())
        selection.push_back(r);
    else
        selection.erase(it);
}

void SketchEditor::toggleSelectConstraint(int index, bool additive)
{
    if (!additive) {
        selection.clear();
        selectedConstraints = {index};
        return;
    }
    auto it = std::find(selectedConstraints.begin(), selectedConstraints.end(), index);
    if (it == selectedConstraints.end())
        selectedConstraints.push_back(index);
    else
        selectedConstraints.erase(it);
}

void SketchEditor::clearSelection()
{
    selection.clear();
    selectedConstraints.clear();
}

// ---- geometry creation -----------------------------------------------------------------

void SketchEditor::addPoint(AppContext& ctx, const SketchSnap& p)
{
    Sketch s = m_sketch;
    Geometry g = Geometry::point(p.pos);
    g.construction = constructionMode;
    const int i = s.add(g);
    snapConstraint(s, p, {i, PointPos::Start});
    commit(ctx, std::move(s), "Point");
}

bool SketchEditor::addChainLine(AppContext& ctx, const SketchSnap& a, const SketchSnap& b)
{
    if (glm::length(b.pos - a.pos) < 1e-9)
        return false;
    Sketch s = m_sketch;
    Geometry g = Geometry::line(a.pos, b.pos);
    g.construction = constructionMode;
    const int l = s.add(g);
    if (chainLine >= 0 && chainLine < l)
        tryAdd(s, make(ConstraintType::Coincident, {l, PointPos::Start}, {chainLine, PointPos::End}));
    else
        snapConstraint(s, a, {l, PointPos::Start});

    const bool closes = chainStartLine >= 0 && b.ref == Ref{chainStartLine, PointPos::Start};
    snapConstraint(s, b, {l, PointPos::End});

    const Vec2 d = b.pos - a.pos;
    if (std::abs(d.y) < kAutoHvTolerance * std::abs(d.x))
        tryAdd(s, make(ConstraintType::Horizontal, {l, PointPos::Edge}));
    else if (std::abs(d.x) < kAutoHvTolerance * std::abs(d.y))
        tryAdd(s, make(ConstraintType::Vertical, {l, PointPos::Edge}));

    if (!commit(ctx, std::move(s), "Line"))
        return false;
    if (chainStartLine < 0)
        chainStartLine = l;
    chainLine = l;
    return closes;
}

void SketchEditor::addRectangle(AppContext& ctx, const SketchSnap& a, const SketchSnap& b)
{
    if (std::abs(b.pos.x - a.pos.x) < 1e-6 || std::abs(b.pos.y - a.pos.y) < 1e-6)
        return;
    Sketch s = m_sketch;
    const Vec2 p0 = a.pos, p1{b.pos.x, a.pos.y}, p2 = b.pos, p3{a.pos.x, b.pos.y};
    const Vec2 pts[4] = {p0, p1, p2, p3};
    int l0 = -1;
    for (int i = 0; i < 4; ++i) {
        Geometry g = Geometry::line(pts[i], pts[(i + 1) % 4]);
        g.construction = constructionMode;
        const int idx = s.add(g);
        if (i == 0)
            l0 = idx;
    }
    for (int i = 0; i < 4; ++i)
        s.addConstraint(make(ConstraintType::Coincident, {l0 + i, PointPos::End}, {l0 + (i + 1) % 4, PointPos::Start}));
    s.addConstraint(make(ConstraintType::Horizontal, {l0, PointPos::Edge}));
    s.addConstraint(make(ConstraintType::Horizontal, {l0 + 2, PointPos::Edge}));
    s.addConstraint(make(ConstraintType::Vertical, {l0 + 1, PointPos::Edge}));
    s.addConstraint(make(ConstraintType::Vertical, {l0 + 3, PointPos::Edge}));
    snapConstraint(s, a, {l0, PointPos::Start});
    snapConstraint(s, b, {l0 + 1, PointPos::End});
    commit(ctx, std::move(s), "Rectangle");
}

void SketchEditor::addCircle(AppContext& ctx, const SketchSnap& center, const SketchSnap& rim)
{
    const double r = glm::length(rim.pos - center.pos);
    if (r < 1e-6)
        return;
    Sketch s = m_sketch;
    Geometry g = Geometry::circle(center.pos, r);
    g.construction = constructionMode;
    const int c = s.add(g);
    snapConstraint(s, center, {c, PointPos::Center});
    if (rim.ref.isPoint())
        tryAdd(s, make(ConstraintType::PointOnObject, rim.ref, {c, PointPos::Edge}));
    commit(ctx, std::move(s), "Circle");
}

void SketchEditor::addArc(AppContext& ctx, const SketchSnap& center, const SketchSnap& start, const SketchSnap& end)
{
    const double r = glm::length(start.pos - center.pos);
    if (r < 1e-6 || glm::length(end.pos - center.pos) < 1e-9)
        return;
    const double a0 = std::atan2(start.pos.y - center.pos.y, start.pos.x - center.pos.x);
    const double a1 = std::atan2(end.pos.y - center.pos.y, end.pos.x - center.pos.x);
    Sketch s = m_sketch;
    Geometry g = Geometry::arc(center.pos, r, a0, a1);
    g.construction = constructionMode;
    const int i = s.add(g);
    snapConstraint(s, center, {i, PointPos::Center});
    snapConstraint(s, start, {i, PointPos::Start});
    snapConstraint(s, end, {i, PointPos::End});
    commit(ctx, std::move(s), "Arc");
}

// ---- constraints --------------------------------------------------------------------------

std::vector<Constraint> SketchEditor::constraintsFromSelection(ConstraintType type) const
{
    const Sketch& s = m_sketch;
    std::vector<Ref> pts, lines, curves, edges;
    for (const Ref& r : selection) {
        if (!s.validRef(r))
            continue;
        if (r.isPoint()) {
            pts.push_back(r);
            continue;
        }
        const GeoType t = s.typeOf(r.geo);
        if (t == GeoType::Point) {
            pts.push_back({r.geo, r.geo == sketch::kOrigin ? PointPos::Start : PointPos::Start});
            continue;
        }
        edges.push_back(r);
        (t == GeoType::Line ? lines : curves).push_back(r);
    }
    const bool onlyPoints = edges.empty();
    std::vector<Constraint> out;
    auto dim = [&](Constraint c) {
        c.value = sketch::measure(s, c);
        out.push_back(c);
    };

    switch (type) {
    case ConstraintType::Coincident:
        if (pts.size() >= 2 && onlyPoints)
            for (size_t i = 1; i < pts.size(); ++i)
                out.push_back(make(type, pts[0], pts[i]));
        break;
    case ConstraintType::PointOnObject:
        if (pts.size() == 1 && edges.size() == 1)
            out.push_back(make(type, pts[0], edges[0]));
        break;
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
        if (pts.empty() && !lines.empty() && curves.empty()) {
            for (const Ref& l : lines)
                if (l.geo >= 0)
                    out.push_back(make(type, l));
        } else if (pts.size() == 2 && onlyPoints) {
            out.push_back(make(type, pts[0], pts[1]));
        }
        break;
    case ConstraintType::Parallel:
        if (pts.empty() && curves.empty() && lines.size() >= 2)
            for (size_t i = 1; i < lines.size(); ++i)
                out.push_back(make(type, lines[0], lines[i]));
        break;
    case ConstraintType::Perpendicular:
        if (pts.empty() && curves.empty() && lines.size() == 2)
            out.push_back(make(type, lines[0], lines[1]));
        break;
    case ConstraintType::Tangent:
        if (pts.empty() && edges.size() == 2 && !curves.empty())
            out.push_back(make(type, edges[0], edges[1]));
        break;
    case ConstraintType::Equal:
        if (pts.empty() && lines.size() >= 2 && curves.empty())
            for (size_t i = 1; i < lines.size(); ++i)
                out.push_back(make(type, lines[0], lines[i]));
        else if (pts.empty() && curves.size() >= 2 && lines.empty())
            for (size_t i = 1; i < curves.size(); ++i)
                out.push_back(make(type, curves[0], curves[i]));
        break;
    case ConstraintType::Symmetric:
        if (pts.size() == 3 && onlyPoints)
            out.push_back(make(type, pts[0], pts[1], pts[2]));
        else if (pts.size() == 2 && lines.size() == 1 && curves.empty())
            out.push_back(make(type, pts[0], pts[1], lines[0]));
        break;
    case ConstraintType::Midpoint:
        if (pts.size() == 1 && lines.size() == 1 && curves.empty())
            out.push_back(make(type, pts[0], lines[0]));
        break;
    case ConstraintType::Fixed: {
        std::vector<Ref> targets;
        for (const Ref& p : pts)
            if (p.geo >= 0)
                targets.push_back(p);
        for (const Ref& e : edges) {
            if (e.geo < 0)
                continue;
            const GeoType t = s.typeOf(e.geo);
            if (t == GeoType::Line) {
                targets.push_back({e.geo, PointPos::Start});
                targets.push_back({e.geo, PointPos::End});
            } else {
                targets.push_back({e.geo, PointPos::Center});
            }
        }
        for (const Ref& p : targets) {
            Constraint c = make(type, p);
            const Vec2 v = s.pointAt(p);
            c.value = v.x;
            c.value2 = v.y;
            out.push_back(c);
        }
        break;
    }
    case ConstraintType::Distance:
        if (pts.size() == 2 && onlyPoints)
            dim(make(type, pts[0], pts[1]));
        else if (pts.size() == 1 && lines.size() == 1 && curves.empty())
            dim(make(type, pts[0], lines[0]));
        else if (pts.empty() && curves.empty() && !lines.empty())
            for (const Ref& l : lines)
                if (l.geo >= 0)
                    dim(make(type, l));
        if (!out.empty() && std::any_of(out.begin(), out.end(), [](const Constraint& c) { return c.value <= 1e-9; }))
            out.clear();
        break;
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY:
        if (pts.size() == 2 && onlyPoints)
            dim(make(type, pts[0], pts[1]));
        else if (pts.size() == 1 && onlyPoints && pts[0].geo >= 0)
            dim(make(type, pts[0]));
        else if (pts.empty() && curves.empty() && lines.size() == 1 && lines[0].geo >= 0)
            dim(make(type, lines[0]));
        break;
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        if (pts.empty() && lines.empty())
            for (const Ref& c : curves)
                dim(make(type, c));
        break;
    case ConstraintType::Angle:
        if (pts.empty() && curves.empty() && lines.size() == 2)
            dim(make(type, lines[0], lines[1]));
        else if (pts.empty() && curves.empty() && lines.size() == 1 && lines[0].geo >= 0)
            dim(make(type, lines[0]));
        break;
    case ConstraintType::Count_:
        break;
    }
    return out;
}

void SketchEditor::constrainSelection(AppContext& ctx, ConstraintType type)
{
    const auto cons = constraintsFromSelection(type);
    const std::string name = sketch::constraintName(type);
    if (cons.empty()) {
        m_message = name + ": the selection does not fit this constraint";
        ctx.status(m_message, true);
        return;
    }
    Sketch s = m_sketch;
    int added = 0, last = -1;
    for (const Constraint& c : cons)
        if (tryAdd(s, c)) {
            ++added;
            last = int(s.constraints.size()) - 1;
        }
    if (added == 0) {
        m_message = name + ": would over-constrain the sketch (conflicting or redundant)";
        ctx.status(m_message, true);
        return;
    }
    if (commit(ctx, std::move(s), name)) {
        if (added == 1 && m_sketch.constraints[size_t(last)].isDimension())
            editDimension = last;
        if (added < int(cons.size()))
            ctx.status(name + ": " + std::to_string(int(cons.size()) - added) + " skipped (would over-constrain)");
        clearSelection();
    }
}

bool SketchEditor::setDimension(AppContext& ctx, int index, double value)
{
    if (index < 0 || index >= int(m_sketch.constraints.size()))
        return false;
    Sketch s = m_sketch;
    Constraint& c = s.constraints[size_t(index)];
    if (c.type != ConstraintType::Angle && c.type != ConstraintType::DistanceX &&
        c.type != ConstraintType::DistanceY && value <= 0.0) {
        m_message = "The value must be positive";
        ctx.status(m_message, true);
        return false;
    }
    if (c.value == value)
        return true;
    c.value = value;
    return commit(ctx, std::move(s), "Dimension");
}

void SketchEditor::deleteConstraint(AppContext& ctx, int index)
{
    if (index < 0 || index >= int(m_sketch.constraints.size()))
        return;
    Sketch s = m_sketch;
    s.removeConstraint(index);
    commit(ctx, std::move(s), "Delete constraint");
    clearSelection();
}

void SketchEditor::deleteSelection(AppContext& ctx)
{
    if (selection.empty() && selectedConstraints.empty())
        return;
    Sketch s = m_sketch;
    std::vector<int> cons = selectedConstraints;
    std::sort(cons.rbegin(), cons.rend());
    for (int c : cons)
        s.removeConstraint(c);
    std::set<int, std::greater<>> geos;
    for (const Ref& r : selection)
        if (r.geo >= 0)
            geos.insert(r.geo);
    for (int g : geos)
        s.removeGeometry(g);
    commit(ctx, std::move(s), "Delete");
    clearSelection();
    resetTool();
}

void SketchEditor::toggleConstruction(AppContext& ctx)
{
    std::set<int> geos;
    for (const Ref& r : selection)
        if (r.geo >= 0)
            geos.insert(r.geo);
    if (geos.empty()) {
        constructionMode = !constructionMode;
        return;
    }
    Sketch s = m_sketch;
    for (int g : geos)
        s.geometry[size_t(g)].construction = !s.geometry[size_t(g)].construction;
    commit(ctx, std::move(s), "Construction");
}

// ---- dragging -------------------------------------------------------------------------------

void SketchEditor::beginDrag(const Ref& ref, Vec2 grab)
{
    if (!m_sketch.validRef(ref) || ref.geo < 0)
        return;
    m_dragStart = m_sketch;
    m_drag = {};
    m_drag.ref = ref;
    if (m_sketch.typeOf(ref.geo) == GeoType::Point)
        m_drag.ref.pos = PointPos::Start;
    m_drag.start = m_sketch.geometry[size_t(ref.geo)];
    m_grab = grab;
    m_dragging = true;
}

void SketchEditor::dragTo(Vec2 cursor)
{
    if (!m_dragging)
        return;
    sketch::Drag d = m_drag;
    d.cursor = cursor;
    d.delta = cursor - m_grab;
    Sketch s = m_sketch;
    if (sketch::solve(s, &d).ok())
        m_sketch = std::move(s);
}

void SketchEditor::endDrag(AppContext& ctx)
{
    if (!m_dragging)
        return;
    m_dragging = false;
    resolve();
    if (!(m_sketch == m_dragStart)) {
        m_undo.push_back(m_dragStart);
        m_redo.clear();
        pushToDocument(ctx);
    }
}

// ---- undo ------------------------------------------------------------------------------------

void SketchEditor::undo(AppContext& ctx)
{
    if (m_undo.empty())
        return;
    m_redo.push_back(m_sketch);
    m_sketch = m_undo.back();
    m_undo.pop_back();
    resolve();
    resetTool();
    clearSelection();
    pushToDocument(ctx);
}

void SketchEditor::redo(AppContext& ctx)
{
    if (m_redo.empty())
        return;
    m_undo.push_back(m_sketch);
    m_sketch = m_redo.back();
    m_redo.pop_back();
    resolve();
    resetTool();
    clearSelection();
    pushToDocument(ctx);
}

} // namespace cf::app
