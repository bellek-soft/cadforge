// Sketch edit mode UI: toolbar, in-viewport drawing / picking / dragging,
// dimension labels and the sketch panel shown in the property window.

#include "app/ui/SketchUi.h"
#include "app/ui/Toolbar.h"
#include "app/ui/Ui.h"

#include <LucideIcons.h>

#include "app/AppContext.h"
#include "model/features/SketchFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace cf::app::ui {

using sketch::Constraint;
using sketch::ConstraintType;
using sketch::GeoType;
using sketch::Geometry;
using sketch::PointPos;
using sketch::Ref;
using sketch::Sketch;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kPointPickPx = 8.0f;
constexpr float kEdgePickPx = 6.0f;

// Colors
constexpr ImU32 kColGeo = IM_COL32(235, 238, 245, 255);
constexpr ImU32 kColGeoFull = IM_COL32(120, 225, 130, 255);
constexpr ImU32 kColConstruction = IM_COL32(110, 160, 255, 255);
constexpr ImU32 kColSelected = IM_COL32(255, 160, 40, 255);
constexpr ImU32 kColHover = IM_COL32(120, 215, 255, 255);
constexpr ImU32 kColAxisH = IM_COL32(220, 90, 90, 170);
constexpr ImU32 kColAxisV = IM_COL32(100, 200, 100, 170);
constexpr ImU32 kColPreview = IM_COL32(255, 225, 120, 255);
constexpr ImU32 kColDim = IM_COL32(255, 205, 120, 255);
constexpr ImU32 kColGlyph = IM_COL32(175, 200, 255, 255);
constexpr ImU32 kColConflict = IM_COL32(255, 90, 80, 255);
constexpr ImU32 kColRedundant = IM_COL32(255, 220, 60, 255);

/// Maps between sketch coordinates and viewport screen coordinates.
struct View {
    ImVec2 pos, size, fb;
    const render::Camera* cam = nullptr;
    PlaneFrame frame;

    ImVec2 toScreen(Vec2 p) const
    {
        const Vec3 s = cam->project(frame.toWorld(p));
        return {pos.x + float(s.x) / fb.x, pos.y + float(s.y) / fb.y};
    }
    bool toSketch(ImVec2 m, Vec2& out) const
    {
        const render::Ray r = cam->rayThrough((m.x - pos.x) * fb.x, (m.y - pos.y) * fb.y);
        const Vec3 n = frame.normal();
        const double den = glm::dot(r.dir, n);
        if (std::abs(den) < 1e-12)
            return false;
        const double t = glm::dot(frame.origin - r.origin, n) / den;
        out = frame.toLocal(r.origin + r.dir * t);
        return true;
    }
    /// Sketch units per screen pixel.
    double unitsPerPixel() const
    {
        const ImVec2 c(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        Vec2 a, b;
        if (!toSketch(c, a) || !toSketch(ImVec2(c.x + 1.0f, c.y), b))
            return 1.0;
        return std::max(glm::length(b - a), 1e-9);
    }
};

struct Hit {
    Ref ref;
    int constraint = -1;
    bool any() const { return ref.valid() || constraint >= 0; }
};

struct UiState {
    // label rectangles of the last frame for picking: (min, max, constraint)
    struct Label { ImVec2 min, max; int constraint; };
    std::vector<Label> labels;
    Hit hover;
    bool pressed = false;      // left button pressed in the viewport (select tool)
    ImVec2 pressPos;
    Hit pressHit;
    Vec2 pressSketch{0.0};
    Vec2 cursor{0.0};
    bool cursorValid = false;
    SketchSnap snap;
    int popupConstraint = -1;
    double popupValue = 0.0;
    bool popupFocus = false;
};
UiState g_ui;

float dist2(ImVec2 a, ImVec2 b)
{
    const float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float segmentDistance(ImVec2 p, ImVec2 a, ImVec2 b)
{
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float l2 = vx * vx + vy * vy;
    float t = l2 > 0.0f ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / l2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return std::sqrt(dist2(p, ImVec2(a.x + t * vx, a.y + t * vy)));
}

/// Screen polyline of an edge.
std::vector<ImVec2> polyline(const Geometry& g, const View& v)
{
    std::vector<ImVec2> out;
    switch (g.type) {
    case GeoType::Point:
        out.push_back(v.toScreen(g.p1));
        break;
    case GeoType::Line:
        out = {v.toScreen(g.p1), v.toScreen(g.p2)};
        break;
    case GeoType::Circle:
    case GeoType::Arc: {
        const double a0 = g.type == GeoType::Arc ? g.startAngle : 0.0;
        const double a1 = g.type == GeoType::Arc ? g.endAngle : 2.0 * kPi;
        const int n = std::max(8, int(std::ceil((a1 - a0) / (2.0 * kPi) * 96.0)));
        for (int i = 0; i <= n; ++i) {
            const double a = a0 + (a1 - a0) * i / n;
            out.push_back(v.toScreen(g.center + g.radius * Vec2(std::cos(a), std::sin(a))));
        }
        break;
    }
    }
    return out;
}

std::vector<PointPos> pointsOf(GeoType t)
{
    switch (t) {
    case GeoType::Point: return {PointPos::Start};
    case GeoType::Line: return {PointPos::Start, PointPos::End};
    case GeoType::Circle: return {PointPos::Center};
    case GeoType::Arc: return {PointPos::Start, PointPos::End, PointPos::Center};
    }
    return {};
}

/// Screen distance from `m` to the infinite projection of an axis.
float axisDistance(const View& v, ImVec2 m, Vec2 dir)
{
    const ImVec2 a = v.toScreen(Vec2(0.0)), b = v.toScreen(dir * 1000.0);
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float l = std::sqrt(vx * vx + vy * vy);
    if (l < 1e-6f)
        return 1e9f;
    return std::abs((m.x - a.x) * vy - (m.y - a.y) * vx) / l;
}

/// What is under the mouse: dimension/constraint labels, then points, then edges.
Hit hitTest(const Sketch& s, const View& v, ImVec2 m, bool withLabels)
{
    Hit hit;
    if (withLabels)
        for (const auto& l : g_ui.labels)
            if (m.x >= l.min.x && m.x <= l.max.x && m.y >= l.min.y && m.y <= l.max.y) {
                hit.constraint = l.constraint;
                return hit;
            }

    float best = kPointPickPx * kPointPickPx;
    for (int i = 0; i < int(s.geometry.size()); ++i)
        for (PointPos p : pointsOf(s.geometry[size_t(i)].type)) {
            const float d = dist2(m, v.toScreen(s.geometry[size_t(i)].point(p)));
            if (d < best) {
                best = d;
                hit.ref = {i, p};
            }
        }
    if (const float d = dist2(m, v.toScreen(Vec2(0.0))); d < best) {
        best = d;
        hit.ref = {sketch::kOrigin, PointPos::Start};
    }
    if (hit.ref.valid())
        return hit;

    float bestEdge = kEdgePickPx;
    for (int i = 0; i < int(s.geometry.size()); ++i) {
        const Geometry& g = s.geometry[size_t(i)];
        if (g.type == GeoType::Point)
            continue;
        const auto pl = polyline(g, v);
        for (size_t k = 0; k + 1 < pl.size(); ++k) {
            const float d = segmentDistance(m, pl[k], pl[k + 1]);
            if (d < bestEdge) {
                bestEdge = d;
                hit.ref = {i, PointPos::Edge};
            }
        }
    }
    if (hit.ref.valid())
        return hit;
    if (axisDistance(v, m, {1, 0}) < kEdgePickPx * 0.8f)
        hit.ref = {sketch::kHAxis, PointPos::Edge};
    else if (axisDistance(v, m, {0, 1}) < kEdgePickPx * 0.8f)
        hit.ref = {sketch::kVAxis, PointPos::Edge};
    return hit;
}

double gridStep(AppContext& ctx, const View& v)
{
    // ~ 40 px between grid lines, rounded to 1/2/5 * 10^n.
    const double target = v.unitsPerPixel() * 40.0;
    const double p = std::pow(10.0, std::floor(std::log10(target)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (p * m >= target)
            return p * m;
    (void)ctx;
    return p * 10.0;
}

SketchSnap snapAt(AppContext& ctx, const View& v, ImVec2 m, Vec2 cursor)
{
    const Sketch& s = ctx.sketchEdit.sketch();
    SketchSnap snap;
    snap.pos = cursor;
    const Hit h = hitTest(s, v, m, false);
    if (h.ref.isPoint()) {
        snap.ref = h.ref;
        snap.pos = s.pointAt(h.ref);
    } else if (h.ref.isEdge()) {
        snap.ref = h.ref;
        if (h.ref.geo == sketch::kHAxis)
            snap.pos = {cursor.x, 0.0};
        else if (h.ref.geo == sketch::kVAxis)
            snap.pos = {0.0, cursor.y};
        else if (s.typeOf(h.ref.geo) == GeoType::Point)
            snap.ref = {}; // (not reachable: points are hit as points)
        else
            snap.pos = s.geometry[size_t(h.ref.geo)].closestPoint(cursor);
    } else if (ctx.sketchEdit.gridSnap) {
        const double step = gridStep(ctx, v) / 2.0;
        snap.pos = {std::round(cursor.x / step) * step, std::round(cursor.y / step) * step};
    }
    return snap;
}

std::string fmtValue(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}

std::string dimensionText(const Constraint& c)
{
    switch (c.type) {
    case ConstraintType::Radius: return "R" + fmtValue(c.value);
    case ConstraintType::Diameter: return "\xC3\x98" + fmtValue(c.value); // Ø
    case ConstraintType::Angle: return fmtValue(c.value) + "\xC2\xB0";     // °
    case ConstraintType::DistanceX: return "x " + fmtValue(c.value);
    case ConstraintType::DistanceY: return "y " + fmtValue(c.value);
    default: return fmtValue(c.value);
    }
}

const char* glyphText(ConstraintType t)
{
    switch (t) {
    case ConstraintType::Horizontal: return "H";
    case ConstraintType::Vertical: return "V";
    case ConstraintType::Parallel: return "//";
    case ConstraintType::Perpendicular: return "_|_";
    case ConstraintType::Tangent: return "T";
    case ConstraintType::Equal: return "=";
    case ConstraintType::Symmetric: return "Sym";
    case ConstraintType::Midpoint: return "Mid";
    case ConstraintType::Fixed: return "Fix";
    case ConstraintType::PointOnObject: return "On";
    default: return nullptr;
    }
}

Vec2 edgeAnchor(const Sketch& s, const Ref& r)
{
    if (r.geo == sketch::kHAxis || r.geo == sketch::kVAxis || r.geo < 0)
        return Vec2(0.0);
    const Geometry& g = s.geometry[size_t(r.geo)];
    switch (g.type) {
    case GeoType::Line: return (g.p1 + g.p2) * 0.5;
    case GeoType::Circle: return g.center + g.radius * Vec2(std::cos(kPi / 4), std::sin(kPi / 4));
    case GeoType::Arc: {
        const double a = 0.5 * (g.startAngle + g.endAngle);
        return g.center + g.radius * Vec2(std::cos(a), std::sin(a));
    }
    case GeoType::Point: return g.p1;
    }
    return Vec2(0.0);
}

Vec2 refAnchor(const Sketch& s, const Ref& r)
{
    return r.isPoint() || (r.geo >= 0 && s.typeOf(r.geo) == GeoType::Point) ? s.pointAt(r.isPoint() ? r : Ref{r.geo, PointPos::Start})
                                                                           : edgeAnchor(s, r);
}

bool contains(const std::vector<int>& v, int i)
{
    return std::find(v.begin(), v.end(), i) != v.end();
}

void drawDashed(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 col, float w)
{
    const float dash = 6.0f, gap = 4.0f;
    float acc = 0.0f;
    bool on = true;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        ImVec2 a = pts[i];
        const ImVec2 b = pts[i + 1];
        float len = std::sqrt(dist2(a, b));
        while (len > 0.0f) {
            const float left = (on ? dash : gap) - acc;
            const float step = std::min(left, len);
            const float t = step / len;
            const ImVec2 c(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
            if (on)
                dl->AddLine(a, c, col, w);
            acc += step;
            len -= step;
            a = c;
            if (acc >= (on ? dash : gap) - 1e-4f) {
                acc = 0.0f;
                on = !on;
            }
        }
    }
}

/// A label box (dimension or constraint glyph) that can be picked next frame.
void label(ImDrawList* dl, ImVec2 center, const std::string& text, ImU32 textCol, int constraint, bool selected,
           bool hovered, std::vector<UiState::Label>& out)
{
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    const ImVec2 mn(center.x - ts.x * 0.5f - 4.0f, center.y - ts.y * 0.5f - 2.0f);
    const ImVec2 mx(center.x + ts.x * 0.5f + 4.0f, center.y + ts.y * 0.5f + 2.0f);
    dl->AddRectFilled(mn, mx, selected ? IM_COL32(90, 55, 10, 230) : IM_COL32(25, 28, 34, 210), 3.0f);
    if (hovered || selected)
        dl->AddRect(mn, mx, selected ? kColSelected : kColHover, 3.0f);
    dl->AddText(ImVec2(mn.x + 4.0f, mn.y + 2.0f), textCol, text.c_str());
    out.push_back({mn, mx, constraint});
}

void drawGrid(AppContext& ctx, ImDrawList* dl, const View& v)
{
    Vec2 c[4];
    const ImVec2 corners[4] = {v.pos, ImVec2(v.pos.x + v.size.x, v.pos.y), ImVec2(v.pos.x, v.pos.y + v.size.y),
                               ImVec2(v.pos.x + v.size.x, v.pos.y + v.size.y)};
    for (int i = 0; i < 4; ++i)
        if (!v.toSketch(corners[i], c[i]))
            return;
    Vec2 lo = c[0], hi = c[0];
    for (const auto& p : c) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    const double step = gridStep(ctx, v);
    const long ix0 = long(std::floor(lo.x / step)), ix1 = long(std::ceil(hi.x / step));
    const long iy0 = long(std::floor(lo.y / step)), iy1 = long(std::ceil(hi.y / step));
    if (ix1 - ix0 > 400 || iy1 - iy0 > 400)
        return;
    for (long i = ix0; i <= ix1; ++i) {
        const ImU32 col = i % 5 == 0 ? IM_COL32(255, 255, 255, 40) : IM_COL32(255, 255, 255, 20);
        dl->AddLine(v.toScreen({i * step, lo.y}), v.toScreen({i * step, hi.y}), col);
    }
    for (long i = iy0; i <= iy1; ++i) {
        const ImU32 col = i % 5 == 0 ? IM_COL32(255, 255, 255, 40) : IM_COL32(255, 255, 255, 20);
        dl->AddLine(v.toScreen({lo.x, i * step}), v.toScreen({hi.x, i * step}), col);
    }
    // Axes through the sketch origin.
    const double big = std::max(glm::length(hi - lo), 1.0) * 4.0;
    const bool hovH = g_ui.hover.ref == Ref{sketch::kHAxis, PointPos::Edge};
    const bool hovV = g_ui.hover.ref == Ref{sketch::kVAxis, PointPos::Edge};
    const bool selH = ctx.sketchEdit.isSelected({sketch::kHAxis, PointPos::Edge});
    const bool selV = ctx.sketchEdit.isSelected({sketch::kVAxis, PointPos::Edge});
    dl->AddLine(v.toScreen({-big, 0}), v.toScreen({big, 0}), selH ? kColSelected : hovH ? kColHover : kColAxisH,
                selH || hovH ? 2.0f : 1.2f);
    dl->AddLine(v.toScreen({0, -big}), v.toScreen({0, big}), selV ? kColSelected : hovV ? kColHover : kColAxisV,
                selV || hovV ? 2.0f : 1.2f);
}

void drawSketch(AppContext& ctx, ImDrawList* dl, const View& v)
{
    SketchEditor& ed = ctx.sketchEdit;
    const Sketch& s = ed.sketch();
    const auto& res = ed.result();
    const bool full = res.fullyConstrained();

    // Edges
    for (int i = 0; i < int(s.geometry.size()); ++i) {
        const Geometry& g = s.geometry[size_t(i)];
        if (g.type == GeoType::Point)
            continue;
        const Ref r{i, PointPos::Edge};
        const bool sel = ed.isSelected(r);
        const bool hov = g_ui.hover.ref == r;
        ImU32 col = g.construction ? kColConstruction : full ? kColGeoFull : kColGeo;
        if (hov)
            col = kColHover;
        if (sel)
            col = kColSelected;
        const float w = sel || hov ? 2.6f : 1.8f;
        const auto pl = polyline(g, v);
        if (g.construction)
            drawDashed(dl, pl, col, w);
        else
            dl->AddPolyline(pl.data(), int(pl.size()), col, ImDrawFlags_None, w);
    }
    // Points
    auto drawPoint = [&](ImVec2 p, const Ref& r, bool important) {
        const bool sel = ed.isSelected(r);
        const bool hov = g_ui.hover.ref == r;
        const float rad = sel || hov ? 4.5f : important ? 3.5f : 2.5f;
        const ImU32 col = sel ? kColSelected : hov ? kColHover : full ? kColGeoFull : kColGeo;
        dl->AddRectFilled(ImVec2(p.x - rad, p.y - rad), ImVec2(p.x + rad, p.y + rad), col);
    };
    drawPoint(v.toScreen(Vec2(0.0)), {sketch::kOrigin, PointPos::Start}, true);
    for (int i = 0; i < int(s.geometry.size()); ++i) {
        const Geometry& g = s.geometry[size_t(i)];
        for (PointPos p : pointsOf(g.type))
            drawPoint(v.toScreen(g.point(p)), {i, p}, g.type == GeoType::Point);
    }

    // Constraints: dimension labels and glyphs.
    std::vector<UiState::Label> labels;
    std::map<std::pair<int, int>, int> stack; // glyphs stacked per anchor cell
    for (int ci = 0; ci < int(s.constraints.size()); ++ci) {
        const Constraint& c = s.constraints[size_t(ci)];
        const bool sel = contains(ed.selectedConstraints, ci);
        const bool hov = g_ui.hover.constraint == ci;
        ImU32 col = c.isDimension() ? kColDim : kColGlyph;
        if (contains(res.conflicting, ci) || contains(res.malformed, ci))
            col = kColConflict;
        else if (contains(res.redundant, ci))
            col = kColRedundant;

        if (c.isDimension()) {
            ImVec2 at;
            if (c.type == ConstraintType::Radius || c.type == ConstraintType::Diameter) {
                const Geometry& g = s.geometry[size_t(c.a.geo)];
                const Vec2 rim = edgeAnchor(s, c.a);
                const ImVec2 a = v.toScreen(c.type == ConstraintType::Diameter ? 2.0 * g.center - rim : g.center);
                const ImVec2 b = v.toScreen(rim);
                dl->AddLine(a, b, (col & 0x00FFFFFF) | 0x90000000, 1.0f);
                at = ImVec2(b.x + 18.0f, b.y - 12.0f);
            } else if (c.type == ConstraintType::Angle) {
                Vec2 p = refAnchor(s, c.a);
                if (c.a.geo >= 0 && s.typeOf(c.a.geo) == GeoType::Line)
                    p = s.geometry[size_t(c.a.geo)].p1;
                at = v.toScreen(p);
                at.x += 26.0f;
                at.y -= 16.0f;
            } else {
                // Distance-like: between the two points / along the line.
                Vec2 p1, p2;
                if (c.a.isPoint() && c.b.isPoint()) {
                    p1 = s.pointAt(c.a);
                    p2 = s.pointAt(c.b);
                } else if (c.a.isPoint() && c.b.isEdge()) {
                    p1 = s.pointAt(c.a);
                    p2 = c.b.geo >= 0 ? s.geometry[size_t(c.b.geo)].closestPoint(p1) : Vec2(p1.x, 0.0);
                    if (c.b.geo == sketch::kVAxis)
                        p2 = {0.0, p1.y};
                } else if (c.a.isEdge() && c.a.geo >= 0) {
                    p1 = s.geometry[size_t(c.a.geo)].p1;
                    p2 = s.geometry[size_t(c.a.geo)].p2;
                } else {
                    p1 = Vec2(0.0);
                    p2 = s.pointAt(c.a);
                }
                if (c.type == ConstraintType::DistanceX)
                    p2.y = p1.y;
                else if (c.type == ConstraintType::DistanceY)
                    p2.x = p1.x;
                const ImVec2 a = v.toScreen(p1), b = v.toScreen(p2);
                float nx = -(b.y - a.y), ny = b.x - a.x;
                const float l = std::sqrt(nx * nx + ny * ny);
                if (l > 1e-3f) {
                    nx /= l;
                    ny /= l;
                } else {
                    nx = 0.0f;
                    ny = -1.0f;
                }
                const float off = 18.0f;
                const ImVec2 a2(a.x + nx * off, a.y + ny * off), b2(b.x + nx * off, b.y + ny * off);
                const ImU32 lc = (col & 0x00FFFFFF) | 0x90000000;
                dl->AddLine(a, a2, lc, 1.0f);
                dl->AddLine(b, b2, lc, 1.0f);
                dl->AddLine(a2, b2, lc, 1.0f);
                at = ImVec2((a2.x + b2.x) * 0.5f, (a2.y + b2.y) * 0.5f);
            }
            label(dl, at, dimensionText(c), col, ci, sel, hov, labels);
            continue;
        }

        const char* text = glyphText(c.type);
        if (!text) {
            // Coincident: a ring around the shared point.
            if (c.type == ConstraintType::Coincident) {
                const ImVec2 p = v.toScreen(s.pointAt(c.a));
                dl->AddCircle(p, 6.0f, sel ? kColSelected : (col == kColGlyph ? IM_COL32(175, 200, 255, 140) : col),
                              12, 1.2f);
            }
            continue;
        }
        std::vector<Vec2> anchors;
        switch (c.type) {
        case ConstraintType::Parallel:
        case ConstraintType::Perpendicular:
        case ConstraintType::Equal:
        case ConstraintType::Tangent:
            anchors = {refAnchor(s, c.a), refAnchor(s, c.b)};
            break;
        case ConstraintType::Horizontal:
        case ConstraintType::Vertical:
            anchors = {c.b.valid() ? (s.pointAt(c.a) + s.pointAt(c.b)) * 0.5 : refAnchor(s, c.a)};
            break;
        case ConstraintType::Symmetric:
            anchors = {c.c.isPoint() ? s.pointAt(c.c) : (s.pointAt(c.a) + s.pointAt(c.b)) * 0.5};
            break;
        default:
            anchors = {refAnchor(s, c.a)};
            break;
        }
        for (const Vec2& an : anchors) {
            ImVec2 p = v.toScreen(an);
            const auto key = std::make_pair(int(std::floor(p.x / 12.0f)), int(std::floor(p.y / 12.0f)));
            const int n = stack[key]++;
            p.x += 12.0f + 22.0f * float(n);
            p.y -= 13.0f;
            label(dl, p, text, col, ci, sel, hov, labels);
        }
    }
    g_ui.labels = std::move(labels);
}

void drawPreview(AppContext& ctx, ImDrawList* dl, const View& v)
{
    SketchEditor& ed = ctx.sketchEdit;
    if (!g_ui.cursorValid || ed.tool == SketchTool::Select)
        return;
    const Vec2 cur = g_ui.snap.pos;
    const ImVec2 cs = v.toScreen(cur);
    const ImU32 col = ed.constructionMode ? kColConstruction : kColPreview;
    // snap marker
    if (g_ui.snap.ref.valid())
        dl->AddCircle(cs, 7.0f, kColHover, 16, 1.5f);
    dl->AddLine(ImVec2(cs.x - 5, cs.y), ImVec2(cs.x + 5, cs.y), col);
    dl->AddLine(ImVec2(cs.x, cs.y - 5), ImVec2(cs.x, cs.y + 5), col);

    const auto& pts = ed.toolPoints;
    if (pts.empty())
        return;
    auto circle = [&](Vec2 c, double r, double a0, double a1) {
        Geometry g = Geometry::arc(c, r, a0, a1);
        if (a1 - a0 >= 2.0 * kPi - 1e-9)
            g = Geometry::circle(c, r);
        const auto pl = polyline(g, v);
        dl->AddPolyline(pl.data(), int(pl.size()), col, ImDrawFlags_None, 1.5f);
    };
    switch (ed.tool) {
    case SketchTool::Line:
        dl->AddLine(v.toScreen(pts[0]), cs, col, 1.5f);
        break;
    case SketchTool::Rectangle: {
        const ImVec2 a = v.toScreen(pts[0]), b = v.toScreen({cur.x, pts[0].y}), c = cs, d = v.toScreen({pts[0].x, cur.y});
        const ImVec2 q[5] = {a, b, c, d, a};
        dl->AddPolyline(q, 5, col, ImDrawFlags_None, 1.5f);
        break;
    }
    case SketchTool::Circle:
        circle(pts[0], glm::length(cur - pts[0]), 0.0, 2.0 * kPi);
        dl->AddLine(v.toScreen(pts[0]), cs, (col & 0x00FFFFFF) | 0x80000000, 1.0f);
        break;
    case SketchTool::Arc:
        if (pts.size() == 1) {
            dl->AddLine(v.toScreen(pts[0]), cs, (col & 0x00FFFFFF) | 0x80000000, 1.0f);
            circle(pts[0], glm::length(cur - pts[0]), 0.0, 2.0 * kPi);
        } else {
            const Vec2 c = pts[0];
            const double r = glm::length(pts[1] - c);
            const double a0 = std::atan2(pts[1].y - c.y, pts[1].x - c.x);
            const double a1 = std::atan2(cur.y - c.y, cur.x - c.x);
            Geometry g = Geometry::arc(c, r, a0, a1);
            circle(c, r, g.startAngle, g.endAngle);
        }
        break;
    default:
        break;
    }
}

/// Mouse handling of the active tool. `click` = left click (pressed this frame).
void handleTool(AppContext& ctx, bool click, bool rightClick)
{
    SketchEditor& ed = ctx.sketchEdit;
    if (rightClick) {
        if (!ed.toolPoints.empty() || ed.chainLine >= 0)
            ed.resetTool();
        else
            ed.setTool(SketchTool::Select);
        return;
    }
    if (!click || !g_ui.cursorValid)
        return;
    const SketchSnap snap = g_ui.snap;
    switch (ed.tool) {
    case SketchTool::Point:
        ed.addPoint(ctx, snap);
        break;
    case SketchTool::Line:
        if (ed.toolPoints.empty()) {
            ed.toolPoints = {snap.pos};
            ed.toolSnaps = {snap};
        } else {
            const bool closed = ed.addChainLine(ctx, ed.toolSnaps[0], snap);
            if (closed) {
                ed.resetTool();
            } else if (ed.chainLine >= 0) {
                // Continue the polyline from the new end point.
                const Vec2 end = ed.sketch().geometry[size_t(ed.chainLine)].p2;
                ed.toolPoints = {end};
                ed.toolSnaps = {SketchSnap{end, {ed.chainLine, PointPos::End}}};
            }
        }
        break;
    case SketchTool::Rectangle:
        if (ed.toolPoints.empty()) {
            ed.toolPoints = {snap.pos};
            ed.toolSnaps = {snap};
        } else {
            ed.addRectangle(ctx, ed.toolSnaps[0], snap);
            ed.resetTool();
        }
        break;
    case SketchTool::Circle:
        if (ed.toolPoints.empty()) {
            ed.toolPoints = {snap.pos};
            ed.toolSnaps = {snap};
        } else {
            ed.addCircle(ctx, ed.toolSnaps[0], snap);
            ed.resetTool();
        }
        break;
    case SketchTool::Arc:
        if (ed.toolPoints.size() < 2) {
            ed.toolPoints.push_back(snap.pos);
            ed.toolSnaps.push_back(snap);
        } else {
            ed.addArc(ctx, ed.toolSnaps[0], ed.toolSnaps[1], snap);
            ed.resetTool();
        }
        break;
    case SketchTool::Select:
        break;
    }
}

void dimensionPopup(AppContext& ctx)
{
    SketchEditor& ed = ctx.sketchEdit;
    if (ed.editDimension >= 0 && ed.editDimension < int(ed.sketch().constraints.size())) {
        g_ui.popupConstraint = ed.editDimension;
        g_ui.popupValue = ed.sketch().constraints[size_t(ed.editDimension)].value;
        g_ui.popupFocus = true;
        ed.editDimension = -1;
        ImGui::OpenPopup("##sketchdim");
    }
    if (ImGui::BeginPopup("##sketchdim")) {
        const int ci = g_ui.popupConstraint;
        if (ci < 0 || ci >= int(ed.sketch().constraints.size())) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        const Constraint& c = ed.sketch().constraints[size_t(ci)];
        ImGui::Text("%s", sketch::constraintName(c.type));
        if (g_ui.popupFocus) {
            ImGui::SetKeyboardFocusHere();
            g_ui.popupFocus = false;
        }
        ImGui::SetNextItemWidth(140);
        const bool enter = ImGui::InputDouble(c.type == ConstraintType::Angle ? "deg" : "mm", &g_ui.popupValue, 0.0,
                                              0.0, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::Button("OK")) {
            ed.setDimension(ctx, ci, g_ui.popupValue);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

struct ConstraintButton {
    ConstraintType type;
    const char* label;
    const char* tip;
};
const ConstraintButton kConstraintButtons[] = {
    {ConstraintType::Coincident, "Coinc", "Coincident: two or more points"},
    {ConstraintType::PointOnObject, "On", "Point on object: a point and an edge"},
    {ConstraintType::Horizontal, "H", "Horizontal: lines, or two points (H)"},
    {ConstraintType::Vertical, "V", "Vertical: lines, or two points (V)"},
    {ConstraintType::Parallel, "//", "Parallel: two or more lines"},
    {ConstraintType::Perpendicular, "Perp", "Perpendicular: two lines"},
    {ConstraintType::Tangent, "Tan", "Tangent: a line and a circle/arc, or two circles/arcs (T)"},
    {ConstraintType::Equal, "Eq", "Equal: lines (length) or circles/arcs (radius) (E)"},
    {ConstraintType::Symmetric, "Sym", "Symmetric: two points about a line or a point"},
    {ConstraintType::Midpoint, "Mid", "Midpoint: a point at the middle of a line"},
    {ConstraintType::Fixed, "Fix", "Lock points in place"},
};
const ConstraintButton kDimensionButtons[] = {
    {ConstraintType::Distance, "Dist", "Distance: line length, two points, or point to line (D)"},
    {ConstraintType::DistanceX, "DX", "Horizontal distance: a line, two points, or a point from the origin"},
    {ConstraintType::DistanceY, "DY", "Vertical distance: a line, two points, or a point from the origin"},
    {ConstraintType::Radius, "Rad", "Radius of circles / arcs (R with selection)"},
    {ConstraintType::Diameter, "Dia", "Diameter of circles / arcs"},
    {ConstraintType::Angle, "Ang", "Angle between two lines, or of a line to the horizontal"},
};

} // namespace

// ---- public -------------------------------------------------------------------------------

void drawSketchToolbar(AppContext& ctx, Toolbar& tb)
{
    SketchEditor& ed = ctx.sketchEdit;
    struct T { SketchTool t; const char* icon; const char* label; const char* tip; };
    static const T tools[] = {
        {SketchTool::Select, ICON_SELECT, "Select", "Select / drag geometry (Esc)"},
        {SketchTool::Line, ICON_LINE, "Line", "Polyline: click points, right click or Esc ends (L)"},
        {SketchTool::Rectangle, ICON_RECT, "Rect", "Rectangle: two corners (R)"},
        {SketchTool::Circle, ICON_CIRCLE, "Circle", "Circle: center, then a point on the circle (C)"},
        {SketchTool::Arc, ICON_ARC, "Arc", "Arc: center, start, end - counter-clockwise (A)"},
        {SketchTool::Point, ICON_POINT, "Point", "Point (P)"},
    };
    for (const auto& t : tools)
        if (tb.button(t.icon, t.label, t.tip, ed.tool == t.t))
            ed.setTool(t.t);
    if (tb.button(ICON_CONSTRUCTION, "Constr", "Toggle construction geometry for the selection, or for new geometry (G)",
                  ed.constructionMode))
        ed.toggleConstruction(ctx);
    if (tb.button(ICON_SNAP, "Snap", "Snap free points to the grid", ed.gridSnap))
        ed.gridSnap = !ed.gridSnap;
    tb.separator();
    for (const auto& b : kConstraintButtons) {
        ImGui::BeginDisabled(!ed.canConstrain(b.type));
        if (tb.button(b.label, b.tip))
            ed.constrainSelection(ctx, b.type);
        ImGui::EndDisabled();
    }
    tb.separator();
    for (const auto& b : kDimensionButtons) {
        ImGui::BeginDisabled(!ed.canConstrain(b.type));
        if (tb.button(b.label, b.tip))
            ed.constrainSelection(ctx, b.type);
        ImGui::EndDisabled();
    }
    tb.separator();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.25f, 1.0f));
    if (tb.button(ICON_CHECK, "Close", "Finish editing the sketch (Enter)"))
        ed.finish(ctx);
    ImGui::PopStyleColor();
    if (tb.button(ICON_OFF, "Cancel", "Discard the changes made in this session"))
        ed.cancel(ctx);
}

void drawSketchCreateToolbar(AppContext& ctx, Toolbar& tb)
{
    if (tb.button(ICON_SKETCH, "Sketch", "New sketch on a base plane"))
        ImGui::OpenPopup("##newsketch");
    if (ImGui::BeginPopup("##newsketch")) {
        ImGui::TextDisabled("Sketch plane");
        if (ImGui::MenuItem("XY (top)")) ctx.createSketch(model::SketchFeature::XY);
        if (ImGui::MenuItem("XZ (front)")) ctx.createSketch(model::SketchFeature::XZ);
        if (ImGui::MenuItem("YZ (right)")) ctx.createSketch(model::SketchFeature::YZ);
        ImGui::EndPopup();
    }
    if (tb.button(ICON_EXTRUDE, "Extrude", "Extrude the selected sketch (select a body too to join it)"))
        ctx.profileFeatureFromSelection("Part::Extrude");
    if (tb.button(ICON_REVOLVE, "Revolve", "Revolve the selected sketch around an axis"))
        ctx.profileFeatureFromSelection("Part::Revolve");
}

void sketchViewport(AppContext& ctx, const SketchViewportInput& in)
{
    SketchEditor& ed = ctx.sketchEdit;
    if (!ed.active())
        return;
    if (!dynamic_cast<model::SketchFeature*>(ctx.doc.find(ed.feature()))) {
        ed.cancel(ctx); // the sketch disappeared (e.g. undo in another way)
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    View v;
    v.pos = in.pos;
    v.size = in.size;
    v.fb = io.DisplayFramebufferScale;
    v.cam = &ctx.camera;
    v.frame = ed.frame();

    const ImVec2 m = io.MousePos;
    g_ui.cursorValid = in.hovered && v.toSketch(m, g_ui.cursor);
    const bool navigating = ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                            io.KeyAlt;

    // Hover
    g_ui.hover = {};
    if (in.hovered && !ed.dragging() && !navigating) {
        if (ed.tool == SketchTool::Select)
            g_ui.hover = hitTest(ed.sketch(), v, m, true);
        else if (g_ui.cursorValid)
            g_ui.snap = snapAt(ctx, v, m, g_ui.cursor);
        if (ed.tool != SketchTool::Select)
            g_ui.hover.ref = g_ui.snap.ref;
    }

    // Input
    const bool additive = io.KeyCtrl || io.KeyShift || io.KeySuper;
    if (ed.tool == SketchTool::Select) {
        if (in.hovered && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            g_ui.pressed = true;
            g_ui.pressPos = m;
            g_ui.pressHit = g_ui.hover;
            g_ui.pressSketch = g_ui.cursor;
        }
        if (g_ui.pressed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!ed.dragging() && g_ui.pressHit.ref.valid() && g_ui.pressHit.ref.geo >= 0 &&
                dist2(m, g_ui.pressPos) > 9.0f)
                ed.beginDrag(g_ui.pressHit.ref, g_ui.pressSketch);
            if (ed.dragging() && g_ui.cursorValid)
                ed.dragTo(g_ui.cursor);
        }
        if (g_ui.pressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (ed.dragging()) {
                ed.endDrag(ctx);
            } else if (g_ui.pressHit.constraint >= 0) {
                ed.toggleSelectConstraint(g_ui.pressHit.constraint, additive);
            } else if (g_ui.pressHit.ref.valid()) {
                ed.toggleSelect(g_ui.pressHit.ref, additive);
            } else if (!additive) {
                ed.clearSelection();
            }
            g_ui.pressed = false;
        }
        if (in.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && g_ui.hover.constraint >= 0 &&
            ed.sketch().constraints[size_t(g_ui.hover.constraint)].isDimension())
            ed.editDimension = g_ui.hover.constraint;
    } else {
        g_ui.pressed = false;
        const bool click = in.hovered && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool right = in.hovered && in.rightClick;
        handleTool(ctx, click, right);
    }

    // Draw
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(in.pos, ImVec2(in.pos.x + in.size.x, in.pos.y + in.size.y), true);
    drawGrid(ctx, dl, v);
    drawSketch(ctx, dl, v);
    drawPreview(ctx, dl, v);

    // Status banner
    const auto& res = ed.result();
    char buf[256];
    const model::Feature* f = ctx.doc.find(ed.feature());
    std::snprintf(buf, sizeof(buf), "Sketch %s  |  %s", f ? f->name().c_str() : "", res.message.c_str());
    const ImU32 col = !res.ok() ? kColConflict
                      : res.status == sketch::SolveResult::Status::Redundant ? kColRedundant
                      : res.fullyConstrained() ? kColGeoFull
                                               : IM_COL32(230, 230, 235, 255);
    const ImVec2 ts = ImGui::CalcTextSize(buf);
    const ImVec2 p(in.pos.x + (in.size.x - ts.x) * 0.5f, in.pos.y + 12.0f);
    dl->AddRectFilled(ImVec2(p.x - 10, p.y - 5), ImVec2(p.x + ts.x + 10, p.y + ts.y + 5), IM_COL32(20, 24, 30, 220),
                      4.0f);
    dl->AddText(p, col, buf);
    if (g_ui.cursorValid) {
        std::snprintf(buf, sizeof(buf), "%.3f, %.3f", g_ui.cursor.x, g_ui.cursor.y);
        dl->AddText(ImVec2(in.pos.x + 90.0f, in.pos.y + in.size.y - 24.0f), IM_COL32(200, 205, 215, 200), buf);
    }
    dl->PopClipRect();

    dimensionPopup(ctx);
}

void drawSketchPanel(AppContext& ctx)
{
    SketchEditor& ed = ctx.sketchEdit;
    const model::Feature* f = ctx.doc.find(ed.feature());
    if (!f)
        return;
    const auto& res = ed.result();
    ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "Editing %s", f->name().c_str());
    ImVec4 col = !res.ok() ? ImVec4(1.0f, 0.4f, 0.35f, 1.0f)
                 : res.status == sketch::SolveResult::Status::Redundant ? ImVec4(1.0f, 0.85f, 0.25f, 1.0f)
                 : res.fullyConstrained() ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f)
                                          : ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
    ImGui::TextColored(col, "%s", res.message.c_str());
    if (!ed.message().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", ed.message().c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::Button("Close sketch"))
        ed.finish(ctx);
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ed.cancel(ctx);
    ImGui::SameLine();
    ImGui::BeginDisabled(!ed.canUndo());
    if (ImGui::Button("Undo"))
        ed.undo(ctx);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!ed.canRedo());
    if (ImGui::Button("Redo"))
        ed.redo(ctx);
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextDisabled("Draw with the tools in the viewport toolbar. Select geometry (Ctrl/Shift adds), then "
                        "apply a constraint. Drag points/edges to move them; double-click a dimension to edit it.");
    ImGui::Text("%d geometries, %d constraints, %d selected", int(ed.sketch().geometry.size()),
                int(ed.sketch().constraints.size()), int(ed.selection.size() + ed.selectedConstraints.size()));

    if (!ImGui::CollapsingHeader("Constraints", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    const Sketch& s = ed.sketch();
    int remove = -1;
    if (ImGui::BeginTable("##cons", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        for (int i = 0; i < int(s.constraints.size()); ++i) {
            const Constraint& c = s.constraints[size_t(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImVec4 tc = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            if (contains(res.conflicting, i) || contains(res.malformed, i))
                tc = ImVec4(1.0f, 0.4f, 0.35f, 1.0f);
            else if (contains(res.redundant, i))
                tc = ImVec4(1.0f, 0.85f, 0.25f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, tc);
            char name[64];
            std::snprintf(name, sizeof(name), "%d  %s", i + 1, sketch::constraintName(c.type));
            if (ImGui::Selectable(name, contains(ed.selectedConstraints, i), ImGuiSelectableFlags_AllowOverlap))
                ed.toggleSelectConstraint(i, ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift);
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(1);
            if (c.isDimension()) {
                double value = c.value;
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputDouble("##v", &value, 0.0, 0.0, "%.4g");
                if (ImGui::IsItemDeactivatedAfterEdit())
                    ed.setDimension(ctx, i, value);
            }
            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("x"))
                remove = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove >= 0)
        ed.deleteConstraint(ctx, remove);
}

bool handleSketchShortcuts(AppContext& ctx)
{
    SketchEditor& ed = ctx.sketchEdit;
    if (!ed.active())
        return false;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
        return true;
    const auto pressed = [&](ImGuiKeyChord chord) {
        const ImGuiKey key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
        const int mods = chord & ImGuiMod_Mask_;
        return ImGui::IsKeyPressed(key, false) && (io.KeyMods & ImGuiMod_Mask_) == mods;
    };
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        ed.undo(ctx);
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) || pressed(ImGuiMod_Ctrl | ImGuiKey_Y))
        ed.redo(ctx);
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return true;

    if (pressed(ImGuiKey_Escape)) {
        if (!ed.toolPoints.empty() || ed.chainLine >= 0)
            ed.resetTool();
        else if (ed.tool != SketchTool::Select)
            ed.setTool(SketchTool::Select);
        else if (!ed.selection.empty() || !ed.selectedConstraints.empty())
            ed.clearSelection();
        else
            ed.finish(ctx);
        return true;
    }
    if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) {
        ed.finish(ctx);
        return true;
    }
    if (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace))
        ed.deleteSelection(ctx);
    if (pressed(ImGuiKey_L)) ed.setTool(SketchTool::Line);
    if (pressed(ImGuiKey_C)) ed.setTool(SketchTool::Circle);
    if (pressed(ImGuiKey_A)) ed.setTool(SketchTool::Arc);
    if (pressed(ImGuiKey_P)) ed.setTool(SketchTool::Point);
    if (pressed(ImGuiKey_G)) ed.toggleConstruction(ctx);
    if (pressed(ImGuiKey_R)) {
        if (ed.canConstrain(ConstraintType::Radius))
            ed.constrainSelection(ctx, ConstraintType::Radius);
        else
            ed.setTool(SketchTool::Rectangle);
    }
    if (pressed(ImGuiKey_H)) ed.constrainSelection(ctx, ConstraintType::Horizontal);
    if (pressed(ImGuiKey_V)) ed.constrainSelection(ctx, ConstraintType::Vertical);
    if (pressed(ImGuiKey_D)) ed.constrainSelection(ctx, ConstraintType::Distance);
    if (pressed(ImGuiKey_E)) ed.constrainSelection(ctx, ConstraintType::Equal);
    if (pressed(ImGuiKey_T)) ed.constrainSelection(ctx, ConstraintType::Tangent);
    if (pressed(ImGuiKey_F)) ctx.fitAll();
    return true;
}

} // namespace cf::app::ui
