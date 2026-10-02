// Sketch solver: maps cf::sketch geometry and constraints onto PlaneGCS.
//
// Every solve builds a fresh GCS::System (sketches are small, this keeps the
// code simple and stateless). Constraint i gets tag i + 1 so that conflicting
// and redundant constraints reported by PlaneGCS map back to indices; temporary
// drag constraints use PlaneGCS' low-priority tag -1.

#include "sketch/Sketch.h"

#include <GCS.h>

#include <algorithm>
#include <cmath>
#include <deque>

namespace cf::sketch {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr int kDragTag = GCS::DefaultTemporaryConstraint;

double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

class Builder {
public:
    explicit Builder(const Sketch& s) : m_sketch(s) {}

    void buildGeometry()
    {
        // External geometry: fixed parameters (not declared as unknowns).
        m_origin.pt = GCS::Point(fixed(0.0), fixed(0.0));
        m_hAxis.line.p1 = GCS::Point(fixed(0.0), fixed(0.0));
        m_hAxis.line.p2 = GCS::Point(fixed(1.0), fixed(0.0));
        m_vAxis.line.p1 = GCS::Point(fixed(0.0), fixed(0.0));
        m_vAxis.line.p2 = GCS::Point(fixed(0.0), fixed(1.0));
        m_origin.type = GeoType::Point;
        m_hAxis.type = m_vAxis.type = GeoType::Line;

        m_geos.resize(m_sketch.geometry.size());
        for (size_t i = 0; i < m_sketch.geometry.size(); ++i) {
            const Geometry& g = m_sketch.geometry[i];
            G& e = m_geos[i];
            e.type = g.type;
            switch (g.type) {
            case GeoType::Point:
                e.pt = point(g.p1);
                break;
            case GeoType::Line:
                e.line.p1 = point(g.p1);
                e.line.p2 = point(g.p2);
                break;
            case GeoType::Circle:
                e.circle.center = point(g.center);
                e.circle.rad = unknown(g.radius);
                break;
            case GeoType::Arc:
                e.arc.center = point(g.center);
                e.arc.rad = unknown(g.radius);
                e.arc.startAngle = unknown(g.startAngle);
                e.arc.endAngle = unknown(g.endAngle);
                e.arc.start = point(g.point(PointPos::Start));
                e.arc.end = point(g.point(PointPos::End));
                m_sys.addConstraintArcRules(e.arc, 0);
                break;
            }
        }
    }

    /// Adds constraint `index`; returns false if its references are unsuitable.
    bool addConstraint(int index)
    {
        const Constraint& c = m_sketch.constraints[size_t(index)];
        const int tag = index + 1;
        if (!m_sketch.validRef(c.a) || (c.b.valid() && !m_sketch.validRef(c.b)) ||
            (c.c.valid() && !m_sketch.validRef(c.c)))
            return false;

        GCS::Point* pa = c.a.isPoint() ? pointOf(c.a) : nullptr;
        GCS::Point* pb = c.b.isPoint() ? pointOf(c.b) : nullptr;
        GCS::Line* la = lineOf(c.a);
        GCS::Line* lb = lineOf(c.b);
        auto& sys = m_sys;

        switch (c.type) {
        case ConstraintType::Coincident:
            if (!pa || !pb)
                return false;
            sys.addConstraintP2PCoincident(*pa, *pb, tag);
            return true;

        case ConstraintType::PointOnObject:
            if (!pa || !c.b.isEdge())
                return false;
            if (lb)
                sys.addConstraintPointOnLine(*pa, *lb, tag);
            else if (GCS::Arc* ab = arcOf(c.b))
                sys.addConstraintPointOnArc(*pa, *ab, tag);
            else if (GCS::Circle* cb = circleOf(c.b))
                sys.addConstraintPointOnCircle(*pa, *cb, tag);
            else
                return false;
            return true;

        case ConstraintType::Horizontal:
        case ConstraintType::Vertical: {
            const bool h = c.type == ConstraintType::Horizontal;
            if (la && !c.b.valid())
                h ? sys.addConstraintHorizontal(*la, tag) : sys.addConstraintVertical(*la, tag);
            else if (pa && pb)
                h ? sys.addConstraintHorizontal(*pa, *pb, tag) : sys.addConstraintVertical(*pa, *pb, tag);
            else
                return false;
            return true;
        }

        case ConstraintType::Parallel:
            if (!la || !lb)
                return false;
            sys.addConstraintParallel(*la, *lb, tag);
            return true;

        case ConstraintType::Perpendicular:
            if (!la || !lb)
                return false;
            sys.addConstraintPerpendicular(*la, *lb, tag);
            return true;

        case ConstraintType::Tangent: {
            if (!c.a.isEdge() || !c.b.isEdge())
                return false;
            GCS::Line* l = la ? la : lb;
            const Ref other = la ? c.b : c.a;
            if (la && lb)
                return false;
            if (l) {
                const Vec2 p1 = value(l->p1), p2 = value(l->p2);
                if (GCS::Arc* a = arcOf(other)) {
                    const bool ccw = cross(p2 - p1, value(a->center) - p1) >= 0.0;
                    sys.addConstraintTangent(*l, *a, ccw, tag);
                } else if (GCS::Circle* ci = circleOf(other)) {
                    const bool ccw = cross(p2 - p1, value(ci->center) - p1) >= 0.0;
                    sys.addConstraintTangent(*l, *ci, ccw, tag);
                } else {
                    return false;
                }
                return true;
            }
            GCS::Arc* aa = arcOf(c.a);
            GCS::Arc* ab = arcOf(c.b);
            GCS::Circle* ca = circleOf(c.a);
            GCS::Circle* cb = circleOf(c.b);
            if (aa && ab)
                sys.addConstraintTangent(*aa, *ab, tag);
            else if (ca && ab)
                sys.addConstraintTangent(*ca, *ab, tag);
            else if (aa && cb)
                sys.addConstraintTangent(*cb, *aa, tag);
            else if (ca && cb)
                sys.addConstraintTangent(*ca, *cb, tag);
            else
                return false;
            return true;
        }

        case ConstraintType::Equal: {
            if (la && lb) {
                sys.addConstraintEqualLength(*la, *lb, tag);
                return true;
            }
            GCS::Arc* aa = arcOf(c.a);
            GCS::Arc* ab = arcOf(c.b);
            GCS::Circle* ca = circleOf(c.a);
            GCS::Circle* cb = circleOf(c.b);
            if (aa && ab)
                sys.addConstraintEqualRadius(*aa, *ab, tag);
            else if (ca && ab)
                sys.addConstraintEqualRadius(*ca, *ab, tag);
            else if (aa && cb)
                sys.addConstraintEqualRadius(*cb, *aa, tag);
            else if (ca && cb)
                sys.addConstraintEqualRadius(*ca, *cb, tag);
            else
                return false;
            return true;
        }

        case ConstraintType::Symmetric: {
            if (!pa || !pb || !c.c.valid())
                return false;
            if (GCS::Line* lc = lineOf(c.c))
                sys.addConstraintP2PSymmetric(*pa, *pb, *lc, tag);
            else if (c.c.isPoint())
                sys.addConstraintP2PSymmetric(*pa, *pb, *pointOf(c.c), tag);
            else
                return false;
            return true;
        }

        case ConstraintType::Midpoint:
            if (!pa || !lb)
                return false;
            sys.addConstraintP2PSymmetric(lb->p1, lb->p2, *pa, tag);
            return true;

        case ConstraintType::Fixed:
            if (!pa)
                return false;
            sys.addConstraintCoordinateX(*pa, fixed(c.value), tag);
            sys.addConstraintCoordinateY(*pa, fixed(c.value2), tag);
            return true;

        case ConstraintType::Distance:
            if (c.value <= 0.0)
                return false;
            if (pa && pb)
                sys.addConstraintP2PDistance(*pa, *pb, fixed(c.value), tag);
            else if (pa && lb) {
                const Vec2 p1 = value(lb->p1), p2 = value(lb->p2);
                const bool ccw = cross(p2 - p1, value(*pa) - p1) >= 0.0;
                sys.addConstraintP2LDistance(*pa, *lb, fixed(c.value), ccw, tag);
            } else if (la && !c.b.valid())
                sys.addConstraintP2PDistance(la->p1, la->p2, fixed(c.value), tag);
            else
                return false;
            return true;

        case ConstraintType::DistanceX:
        case ConstraintType::DistanceY: {
            const bool x = c.type == ConstraintType::DistanceX;
            GCS::Point *p1 = nullptr, *p2 = nullptr;
            if (pa && pb) {
                p1 = pa;
                p2 = pb;
            } else if (la && !c.b.valid()) {
                p1 = &la->p1;
                p2 = &la->p2;
            } else if (pa && !c.b.valid()) {
                x ? sys.addConstraintCoordinateX(*pa, fixed(c.value), tag)
                  : sys.addConstraintCoordinateY(*pa, fixed(c.value), tag);
                return true;
            } else {
                return false;
            }
            if (x)
                sys.addConstraintDifference(p1->x, p2->x, fixed(c.value), tag);
            else
                sys.addConstraintDifference(p1->y, p2->y, fixed(c.value), tag);
            return true;
        }

        case ConstraintType::Radius:
        case ConstraintType::Diameter: {
            if (c.value <= 0.0 || c.b.valid())
                return false;
            const bool dia = c.type == ConstraintType::Diameter;
            if (GCS::Arc* a = arcOf(c.a))
                dia ? sys.addConstraintArcDiameter(*a, fixed(c.value), tag)
                    : sys.addConstraintArcRadius(*a, fixed(c.value), tag);
            else if (GCS::Circle* ci = circleOf(c.a))
                dia ? sys.addConstraintCircleDiameter(*ci, fixed(c.value), tag)
                    : sys.addConstraintCircleRadius(*ci, fixed(c.value), tag);
            else
                return false;
            return true;
        }

        case ConstraintType::Angle: {
            double* angle = fixed(c.value * kPi / 180.0);
            if (la && lb)
                sys.addConstraintL2LAngle(*la, *lb, angle, tag);
            else if (la && !c.b.valid())
                sys.addConstraintL2LAngle(m_hAxis.line, *la, angle, tag);
            else
                return false;
            return true;
        }

        case ConstraintType::Count_:
            break;
        }
        return false;
    }

    void addDrag(const Drag& d)
    {
        if (!m_sketch.validRef(d.ref) || d.ref.geo < 0)
            return;
        const Geometry& g = m_sketch.geometry[size_t(d.ref.geo)];
        Ref ref = d.ref;
        if (g.type == GeoType::Point)
            ref.pos = PointPos::Start;

        auto pin = [&](GCS::Point& p, Vec2 target) {
            m_sys.addConstraintCoordinateX(p, fixed(target.x), kDragTag);
            m_sys.addConstraintCoordinateY(p, fixed(target.y), kDragTag);
        };
        if (ref.isPoint()) {
            pin(*pointOf(ref), d.cursor);
        } else if (g.type == GeoType::Line) {
            GCS::Line* l = lineOf(ref);
            pin(l->p1, d.start.p1 + d.delta);
            pin(l->p2, d.start.p2 + d.delta);
        } else {
            m_tempPoints.emplace_back(fixed(d.cursor.x), fixed(d.cursor.y));
            if (GCS::Arc* a = arcOf(ref))
                m_sys.addConstraintPointOnCircle(m_tempPoints.back(), *a, kDragTag);
            else if (GCS::Circle* c = circleOf(ref))
                m_sys.addConstraintPointOnCircle(m_tempPoints.back(), *c, kDragTag);
        }
    }

    GCS::System& system() { return m_sys; }
    std::vector<double*>& unknowns() { return m_unknowns; }

    /// Reads the solved parameters back into `out`.
    bool writeBack(Sketch& out) const
    {
        std::vector<Geometry> geos = out.geometry;
        for (size_t i = 0; i < geos.size(); ++i) {
            Geometry& g = geos[i];
            const G& e = m_geos[i];
            switch (g.type) {
            case GeoType::Point:
                g.p1 = value(e.pt);
                break;
            case GeoType::Line:
                g.p1 = value(e.line.p1);
                g.p2 = value(e.line.p2);
                break;
            case GeoType::Circle:
                g.center = value(e.circle.center);
                g.radius = *e.circle.rad;
                if (!(g.radius > 1e-9))
                    return false;
                break;
            case GeoType::Arc: {
                g.center = value(e.arc.center);
                g.radius = *e.arc.rad;
                if (!(g.radius > 1e-9))
                    return false;
                double a0 = std::remainder(*e.arc.startAngle, kTwoPi);
                double sweep = std::fmod(*e.arc.endAngle - *e.arc.startAngle, kTwoPi);
                if (sweep <= 1e-12)
                    sweep += kTwoPi;
                g.startAngle = a0;
                g.endAngle = a0 + sweep;
                break;
            }
            }
            if (!std::isfinite(g.p1.x) || !std::isfinite(g.p1.y) || !std::isfinite(g.p2.x) ||
                !std::isfinite(g.p2.y) || !std::isfinite(g.center.x) || !std::isfinite(g.center.y))
                return false;
        }
        out.geometry = std::move(geos);
        return true;
    }

private:
    struct G {
        GeoType type = GeoType::Point;
        GCS::Point pt;
        GCS::Line line;
        GCS::Circle circle;
        GCS::Arc arc;
    };

    double* fixed(double v)
    {
        m_store.push_back(v);
        return &m_store.back();
    }
    double* unknown(double v)
    {
        double* p = fixed(v);
        m_unknowns.push_back(p);
        return p;
    }
    GCS::Point point(Vec2 v)
    {
        double* x = unknown(v.x);
        double* y = unknown(v.y);
        return GCS::Point(x, y);
    }
    static Vec2 value(const GCS::Point& p) { return {*p.x, *p.y}; }

    G* geo(int index)
    {
        if (index == kOrigin)
            return &m_origin;
        if (index == kHAxis)
            return &m_hAxis;
        if (index == kVAxis)
            return &m_vAxis;
        if (index >= 0 && index < int(m_geos.size()))
            return &m_geos[size_t(index)];
        return nullptr;
    }

    GCS::Point* pointOf(const Ref& r)
    {
        G* g = geo(r.geo);
        if (!g || !r.isPoint())
            return nullptr;
        switch (g->type) {
        case GeoType::Point: return &g->pt;
        case GeoType::Line: return r.pos == PointPos::End ? &g->line.p2 : &g->line.p1;
        case GeoType::Circle: return &g->circle.center;
        case GeoType::Arc:
            if (r.pos == PointPos::Start)
                return &g->arc.start;
            if (r.pos == PointPos::End)
                return &g->arc.end;
            return &g->arc.center;
        }
        return nullptr;
    }
    GCS::Line* lineOf(const Ref& r)
    {
        G* g = r.isEdge() ? geo(r.geo) : nullptr;
        return g && g->type == GeoType::Line ? &g->line : nullptr;
    }
    GCS::Circle* circleOf(const Ref& r)
    {
        G* g = r.isEdge() ? geo(r.geo) : nullptr;
        return g && g->type == GeoType::Circle ? &g->circle : nullptr;
    }
    GCS::Arc* arcOf(const Ref& r)
    {
        G* g = r.isEdge() ? geo(r.geo) : nullptr;
        return g && g->type == GeoType::Arc ? &g->arc : nullptr;
    }

    const Sketch& m_sketch;
    GCS::System m_sys;
    std::deque<double> m_store; // stable addresses for PlaneGCS parameter pointers
    std::vector<double*> m_unknowns;
    std::vector<G> m_geos;
    std::deque<GCS::Point> m_tempPoints;
    G m_origin, m_hAxis, m_vAxis;
};

std::vector<int> tagsToIndices(const std::vector<int>& tags, int count)
{
    std::vector<int> out;
    for (int t : tags)
        if (t >= 1 && t <= count)
            out.push_back(t - 1);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace

namespace {

/// One solver run; constraints listed in `skip` are left out.
SolveResult run(Sketch& sketch, const Drag* drag, const std::vector<int>& skip)
{
    SolveResult res;
    Builder b(sketch);
    b.buildGeometry();
    for (int i = 0; i < int(sketch.constraints.size()); ++i) {
        if (std::find(skip.begin(), skip.end(), i) != skip.end())
            continue;
        if (!b.addConstraint(i))
            res.malformed.push_back(i);
    }
    if (drag)
        b.addDrag(*drag);

    GCS::System& sys = b.system();
    sys.declareUnknowns(b.unknowns());
    sys.initSolution(GCS::DogLeg);

    const int count = int(sketch.constraints.size());
    GCS::VEC_I tags;
    sys.getConflicting(tags);
    res.conflicting = tagsToIndices(tags, count);
    sys.getRedundant(tags);
    res.redundant = tagsToIndices(tags, count);
    res.dof = std::max(0, sys.dofsNumber());

    if (!res.conflicting.empty()) {
        res.status = SolveResult::Status::Conflicting;
        res.message = "Over-constrained: conflicting constraints";
        return res;
    }
    if (!res.redundant.empty())
        return res; // the caller re-runs without them

    GCS::SolveStatus st = sys.solve(GCS::DogLeg);
    if (st != GCS::SolveStatus::Success) {
        st = sys.solve(GCS::LevenbergMarquardt);
        if (st != GCS::SolveStatus::Success)
            st = sys.solve(GCS::BFGS);
    }
    const bool accepted = st == GCS::SolveStatus::Success ||
                          (drag && st == GCS::SolveStatus::Converged);
    if (!accepted) {
        res.status = SolveResult::Status::Failed;
        res.message = "Solver did not converge";
        return res;
    }
    sys.applySolution();
    if (!b.writeBack(sketch)) {
        res.status = SolveResult::Status::Failed;
        res.message = "Solution would make geometry degenerate";
        return res;
    }
    if (!res.malformed.empty()) {
        res.status = SolveResult::Status::Invalid;
        res.message = "Some constraints do not fit their geometry";
    } else {
        res.status = SolveResult::Status::Ok;
        res.message = res.dof == 0 ? "Fully constrained" : std::to_string(res.dof) + " degrees of freedom";
    }
    return res;
}

} // namespace

SolveResult solve(Sketch& sketch, const Drag* drag)
{
    SolveResult res = run(sketch, drag, {});
    if (res.status == SolveResult::Status::Ok && !res.redundant.empty()) {
        // Redundant constraints make the system singular: solve without them
        // (they are satisfied anyway) and report them.
        const std::vector<int> redundant = res.redundant;
        res = run(sketch, drag, redundant);
        res.redundant = redundant;
        if (res.status == SolveResult::Status::Ok) {
            res.status = SolveResult::Status::Redundant;
            res.message = "Redundant constraints (" + std::to_string(redundant.size()) + ")";
        }
    }
    return res;
}

double measure(const Sketch& s, const Constraint& c)
{
    auto lineDir = [&](const Ref& r) {
        if (r.geo == kHAxis)
            return Vec2(1.0, 0.0);
        if (r.geo == kVAxis)
            return Vec2(0.0, 1.0);
        const Geometry& g = s.geometry.at(size_t(r.geo));
        return g.p2 - g.p1;
    };
    auto isLine = [&](const Ref& r) { return r.isEdge() && s.validRef(r) && s.typeOf(r.geo) == GeoType::Line; };
    auto lineEnds = [&](const Ref& r, Vec2& p1, Vec2& p2) {
        if (r.geo < 0) {
            p1 = Vec2(0.0);
            p2 = lineDir(r);
        } else {
            p1 = s.geometry[size_t(r.geo)].p1;
            p2 = s.geometry[size_t(r.geo)].p2;
        }
    };

    switch (c.type) {
    case ConstraintType::Distance:
        if (c.a.isPoint() && c.b.isPoint())
            return glm::length(s.pointAt(c.b) - s.pointAt(c.a));
        if (c.a.isPoint() && isLine(c.b)) {
            Vec2 p1, p2;
            lineEnds(c.b, p1, p2);
            const double l = glm::length(p2 - p1);
            return l > 0.0 ? std::abs(cross(p2 - p1, s.pointAt(c.a) - p1)) / l : 0.0;
        }
        if (isLine(c.a)) {
            Vec2 p1, p2;
            lineEnds(c.a, p1, p2);
            return glm::length(p2 - p1);
        }
        return 0.0;
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY: {
        const int k = c.type == ConstraintType::DistanceX ? 0 : 1;
        if (c.a.isPoint() && c.b.isPoint())
            return s.pointAt(c.b)[k] - s.pointAt(c.a)[k];
        if (isLine(c.a)) {
            Vec2 p1, p2;
            lineEnds(c.a, p1, p2);
            return p2[k] - p1[k];
        }
        if (c.a.isPoint())
            return s.pointAt(c.a)[k];
        return 0.0;
    }
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        if (c.a.geo >= 0 && c.a.geo < int(s.geometry.size()))
            return s.geometry[size_t(c.a.geo)].radius * (c.type == ConstraintType::Diameter ? 2.0 : 1.0);
        return 0.0;
    case ConstraintType::Angle: {
        const Vec2 d1 = isLine(c.b) ? lineDir(c.a) : Vec2(1.0, 0.0);
        const Vec2 d2 = isLine(c.b) ? lineDir(c.b) : lineDir(c.a);
        return std::atan2(cross(d1, d2), glm::dot(d1, d2)) * 180.0 / kPi;
    }
    case ConstraintType::Fixed:
        return s.pointAt(c.a).x;
    default:
        return 0.0;
    }
}

} // namespace cf::sketch
