// Sketcher (PlaneGCS solver) and sketch-based features (extrude / revolve).

#include "geom/Profile.h"
#include "model/Document.h"
#include "model/features/SketchFeatures.h"
#include "sketch/Sketch.h"

#include "TestHarness.h"

#include <nlohmann/json.hpp>

#include <cmath>

using namespace cf;
using namespace cf::sketch;

namespace {
constexpr double kPi = 3.14159265358979323846;

Ref S(int g) { return {g, PointPos::Start}; }
Ref E(int g) { return {g, PointPos::End}; }
Ref C(int g) { return {g, PointPos::Center}; }
Ref Ed(int g) { return {g, PointPos::Edge}; }

Constraint con(ConstraintType t, Ref a, Ref b = {}, double v = 0.0)
{
    Constraint c;
    c.type = t;
    c.a = a;
    c.b = b;
    c.value = v;
    return c;
}

/// Closed rectangle (4 lines, coincident corners, H/V). Returns the first line index.
int rectangle(Sketch& s, Vec2 a, Vec2 b, bool constrain = true)
{
    const int l0 = s.add(Geometry::line(a, {b.x, a.y}));
    s.add(Geometry::line({b.x, a.y}, b));
    s.add(Geometry::line(b, {a.x, b.y}));
    s.add(Geometry::line({a.x, b.y}, a));
    if (constrain) {
        for (int i = 0; i < 4; ++i)
            s.addConstraint(con(ConstraintType::Coincident, E(l0 + i), S(l0 + (i + 1) % 4)));
        s.addConstraint(con(ConstraintType::Horizontal, Ed(l0)));
        s.addConstraint(con(ConstraintType::Horizontal, Ed(l0 + 2)));
        s.addConstraint(con(ConstraintType::Vertical, Ed(l0 + 1)));
        s.addConstraint(con(ConstraintType::Vertical, Ed(l0 + 3)));
    }
    return l0;
}
} // namespace

TEST(sketch_rectangle_fully_constrained)
{
    Sketch s;
    // Deliberately sloppy input; the solver must square it up.
    const int l = s.add(Geometry::line({0.3, -0.2}, {38, 1}));
    s.add(Geometry::line({38, 1}, {41, 22}));
    s.add(Geometry::line({41, 22}, {-1, 19}));
    s.add(Geometry::line({-1, 19}, {0.3, -0.2}));
    for (int i = 0; i < 4; ++i)
        s.addConstraint(con(ConstraintType::Coincident, E(l + i), S(l + (i + 1) % 4)));
    s.addConstraint(con(ConstraintType::Horizontal, Ed(l)));
    s.addConstraint(con(ConstraintType::Horizontal, Ed(l + 2)));
    s.addConstraint(con(ConstraintType::Vertical, Ed(l + 1)));
    s.addConstraint(con(ConstraintType::Vertical, Ed(l + 3)));

    SolveResult r = solve(s);
    CHECK(r.ok());
    CHECK(r.dof == 4); // position (2) + width + height

    s.addConstraint(con(ConstraintType::Coincident, S(l), {kOrigin, PointPos::Start}));
    s.addConstraint(con(ConstraintType::Distance, Ed(l), {}, 40.0));
    s.addConstraint(con(ConstraintType::DistanceY, Ed(l + 1), {}, 20.0));
    r = solve(s);
    CHECK(r.status == SolveResult::Status::Ok);
    CHECK(r.fullyConstrained());
    CHECK_NEAR(s.geometry[0].p1.x, 0.0, 1e-8);
    CHECK_NEAR(s.geometry[0].p1.y, 0.0, 1e-8);
    CHECK_NEAR(s.geometry[1].p2.x, 40.0, 1e-8);
    CHECK_NEAR(s.geometry[1].p2.y, 20.0, 1e-8);
    CHECK_NEAR(s.geometry[3].p1.x, 0.0, 1e-8);
    CHECK_NEAR(s.geometry[3].p1.y, 20.0, 1e-8);

    // Changing a dimension moves the geometry.
    s.constraints.back().value = 25.0;
    CHECK(solve(s).ok());
    CHECK_NEAR(s.geometry[2].p1.y, 25.0, 1e-8);
}

TEST(sketch_conflicts_and_redundancy)
{
    Sketch s;
    const int l = s.add(Geometry::line({0, 0}, {10, 1}));
    s.addConstraint(con(ConstraintType::Horizontal, Ed(l)));
    s.addConstraint(con(ConstraintType::Vertical, Ed(l)));
    s.addConstraint(con(ConstraintType::Distance, Ed(l), {}, 10.0));
    const Sketch before = s;
    SolveResult r = solve(s);
    CHECK(r.status == SolveResult::Status::Conflicting);
    CHECK(!r.conflicting.empty());
    CHECK(s == before); // geometry untouched

    Sketch t;
    const int m = t.add(Geometry::line({0, 0}, {10, 1}));
    t.addConstraint(con(ConstraintType::Horizontal, Ed(m)));
    t.addConstraint(con(ConstraintType::Horizontal, Ed(m)));
    r = solve(t);
    CHECK(r.status == SolveResult::Status::Redundant);
    CHECK(r.ok());
    CHECK(!r.redundant.empty());

    // A constraint referencing unsuitable geometry is reported, not crashed on.
    Sketch u;
    const int c = u.add(Geometry::circle({0, 0}, 5));
    u.addConstraint(con(ConstraintType::Horizontal, Ed(c)));
    r = solve(u);
    CHECK(r.status == SolveResult::Status::Invalid);
    CHECK(r.malformed.size() == 1);
}

TEST(sketch_tangent_arc_circle_constraints)
{
    Sketch s;
    const int c = s.add(Geometry::circle({1, 2}, 4));
    const int l = s.add(Geometry::line({-10, 9}, {10, 8}));
    s.addConstraint(con(ConstraintType::Coincident, C(c), {kOrigin, PointPos::Start}));
    s.addConstraint(con(ConstraintType::Radius, Ed(c), {}, 5.0));
    s.addConstraint(con(ConstraintType::Horizontal, Ed(l)));
    s.addConstraint(con(ConstraintType::Tangent, Ed(l), Ed(c)));
    CHECK(solve(s).ok());
    CHECK_NEAR(s.geometry[size_t(l)].p1.y, 5.0, 1e-7); // stays on the side it started on
    CHECK_NEAR(s.geometry[size_t(c)].radius, 5.0, 1e-9);

    // Arc: end points follow the angles; diameter and point-on-object.
    Sketch a;
    const int arc = a.add(Geometry::arc({0, 0}, 3, 0.0, kPi / 2));
    const int p = a.add(Geometry::point({7, 7}));
    a.addConstraint(con(ConstraintType::Diameter, Ed(arc), {}, 10.0));
    a.addConstraint(con(ConstraintType::Coincident, C(arc), {kOrigin, PointPos::Start}));
    a.addConstraint(con(ConstraintType::PointOnObject, S(p), Ed(arc)));
    CHECK(solve(a).ok());
    CHECK_NEAR(a.geometry[size_t(arc)].radius, 5.0, 1e-8);
    CHECK_NEAR(glm::length(a.geometry[size_t(p)].p1), 5.0, 1e-7);
    const Vec2 st = a.geometry[size_t(arc)].point(PointPos::Start);
    CHECK_NEAR(glm::length(st), 5.0, 1e-8);
    CHECK(a.geometry[size_t(arc)].endAngle > a.geometry[size_t(arc)].startAngle);

    // Angle between lines, measured value matches the solved geometry.
    Sketch g;
    const int l1 = g.add(Geometry::line({0, 0}, {10, 0}));
    const int l2 = g.add(Geometry::line({0, 0}, {10, 3}));
    g.addConstraint(con(ConstraintType::Coincident, S(l1), S(l2)));
    g.addConstraint(con(ConstraintType::Angle, Ed(l1), Ed(l2), 30.0));
    CHECK(solve(g).ok());
    CHECK_NEAR(measure(g, g.constraints[1]), 30.0, 1e-6);
}

TEST(sketch_drag_respects_constraints)
{
    Sketch s;
    const int l = s.add(Geometry::line({0, 0}, {10, 0}));
    s.addConstraint(con(ConstraintType::Coincident, S(l), {kOrigin, PointPos::Start}));
    s.addConstraint(con(ConstraintType::Distance, Ed(l), {}, 10.0));
    Drag d;
    d.ref = E(l);
    d.cursor = {0, 20};
    d.start = s.geometry[size_t(l)];
    CHECK(solve(s, &d).ok());
    // The end point swings around the origin as far as the length allows.
    CHECK_NEAR(s.geometry[size_t(l)].p2.x, 0.0, 1e-5);
    CHECK_NEAR(s.geometry[size_t(l)].p2.y, 10.0, 1e-5);
    CHECK_NEAR(s.geometry[size_t(l)].p1.x, 0.0, 1e-9);

    // Dragging a free circle's edge changes its radius.
    Sketch c;
    const int ci = c.add(Geometry::circle({0, 0}, 2));
    c.addConstraint(con(ConstraintType::Coincident, C(ci), {kOrigin, PointPos::Start}));
    Drag dc;
    dc.ref = Ed(ci);
    dc.cursor = {6, 0};
    dc.start = c.geometry[0];
    CHECK(solve(c, &dc).ok());
    CHECK_NEAR(c.geometry[0].radius, 6.0, 1e-6);
}

TEST(sketch_json_and_removal)
{
    Sketch s;
    rectangle(s, {0, 0}, {10, 5});
    const int c = s.add(Geometry::circle({5, 2.5}, 1));
    s.geometry[size_t(c)].construction = true;
    s.addConstraint(con(ConstraintType::Radius, Ed(c), {}, 1.0));
    s.addConstraint(con(ConstraintType::Distance, Ed(0), {}, 10.0));

    const Sketch back = Sketch::fromJson(s.toJson());
    CHECK(back == s);
    CHECK(back.geometry[size_t(c)].construction);

    // Removing line 1 drops its constraints and renumbers the others.
    Sketch r = s;
    const size_t before = r.constraints.size();
    r.removeGeometry(1);
    CHECK(r.geometry.size() == 4);
    CHECK(r.constraints.size() == before - 3); // two coincidences + vertical
    for (const auto& k : r.constraints)
        CHECK(r.validRef(k.a));
    CHECK(r.constraints[r.constraints.size() - 2].a.geo == 3); // the circle moved down
}

TEST(profile_faces_with_holes)
{
    PlaneFrame f;
    std::vector<geom::Curve2d> curves;
    auto line = [&](Vec2 a, Vec2 b) {
        geom::Curve2d c;
        c.a = a;
        c.b = b;
        curves.push_back(c);
    };
    line({0, 0}, {40, 0});
    line({40, 0}, {40, 20});
    line({40, 20}, {0, 20});
    line({0, 20}, {0, 0});
    geom::Curve2d hole;
    hole.kind = geom::Curve2d::Kind::Circle;
    hole.center = {10, 10};
    hole.radius = 5;
    curves.push_back(hole);
    // An island inside a second hole: depth 2 is solid again.
    geom::Curve2d hole2 = hole;
    hole2.center = {28, 10};
    hole2.radius = 8;
    curves.push_back(hole2);
    geom::Curve2d island = hole;
    island.center = {28, 10};
    island.radius = 3;
    curves.push_back(island);

    const geom::Shape edges = geom::makePlanarEdges(f, curves);
    CHECK(edges.edgeCount() == 7);
    const geom::Shape faces = geom::makePlanarFaces(f, edges);
    CHECK(faces.faceCount() == 2);
    CHECK_NEAR(faces.area(), 800.0 - kPi * 25 - kPi * 64 + kPi * 9, 1e-6);

    const geom::Shape solid = geom::extrude(faces, {0, 0, 1}, 10.0);
    CHECK(solid.isValid());
    CHECK_NEAR(solid.volume(), 10.0 * (800.0 - kPi * 25 - kPi * 64 + kPi * 9), 1e-5);

    // Open loops are rejected.
    curves.erase(curves.begin());
    bool threw = false;
    try {
        geom::makePlanarFaces(f, geom::makePlanarEdges(f, curves));
    } catch (const geom::GeomError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(document_extrude_revolve)
{
    model::Document doc;
    auto* sk = static_cast<model::SketchFeature*>(doc.create("Sketch::Sketch"));
    {
        Sketch s;
        rectangle(s, {10, 0}, {20, 5});
        sk->setSketch(s);
    }
    auto* ex = doc.create("Part::Extrude");
    ex->props().set(model::ProfileFeature::kSketch, sk->id());
    ex->props().set("length", 8.0);
    doc.recompute();
    CHECK(sk->state() == model::FeatureState::Ok);
    CHECK(sk->lastSolve().dof == 4);
    CHECK(ex->state() == model::FeatureState::Ok);
    CHECK_NEAR(ex->shape().volume(), 50.0 * 8.0, 1e-6);
    CHECK_NEAR(ex->shape().bounds().max.z, 8.0, 1e-6);
    CHECK(doc.isConsumed(sk->id()));

    // Symmetric + reversed on the XZ plane (normal -Y).
    sk->props().set(model::SketchFeature::kPlane, int(model::SketchFeature::XZ));
    ex->props().set("symmetric", true);
    doc.recompute();
    CHECK_NEAR(ex->shape().bounds().min.y, -4.0, 1e-6);
    CHECK_NEAR(ex->shape().bounds().max.y, 4.0, 1e-6);
    CHECK_NEAR(ex->shape().bounds().max.z, 5.0, 1e-6);

    // Revolve around the sketch V axis: a ring with rectangular section.
    sk->props().set(model::SketchFeature::kPlane, int(model::SketchFeature::XY));
    auto* sk2 = static_cast<model::SketchFeature*>(doc.create("Sketch::Sketch"));
    {
        Sketch s;
        rectangle(s, {10, 0}, {20, 5});
        sk2->setSketch(s);
    }
    auto* rv = doc.create("Part::Revolve");
    rv->props().set(model::ProfileFeature::kSketch, sk2->id());
    doc.recompute();
    CHECK(rv->state() == model::FeatureState::Ok);
    CHECK_NEAR(rv->shape().volume(), kPi * (400.0 - 100.0) * 5.0, 1e-4);

    // Half revolution about a construction line x = 30 (parallel to V).
    {
        Sketch s = sk2->sketch();
        const int axis = s.add(Geometry::line({30, -5}, {30, 5}));
        s.geometry[size_t(axis)].construction = true;
        sk2->setSketch(s);
    }
    rv->props().set("axis", int(model::RevolveFeature::ConstructionLine));
    rv->props().set("angle", 180.0);
    doc.recompute();
    CHECK(rv->state() == model::FeatureState::Ok);
    CHECK_NEAR(rv->shape().volume(), 0.5 * kPi * (400.0 - 100.0) * 5.0, 1e-4);

    // An axis through the profile is reported, not crashed on.
    rv->props().set("axis", int(model::RevolveFeature::SketchH));
    doc.recompute();
    CHECK(rv->state() == model::FeatureState::Ok || !rv->error().empty());
}

TEST(document_extrude_operations_and_io)
{
    model::Document doc;
    auto* box = doc.create("Part::Box");
    box->props().set("length", 40.0);
    box->props().set("width", 20.0);
    box->props().set("height", 10.0);

    auto* sk = static_cast<model::SketchFeature*>(doc.create("Sketch::Sketch"));
    sk->props().set(model::SketchFeature::kOffset, 10.0);
    {
        Sketch s;
        const int c = s.add(Geometry::circle({20, 10}, 4));
        s.addConstraint(con(ConstraintType::Radius, Ed(c), {}, 5.0));
        sk->setSketch(s);
    }
    auto* cut = doc.create("Part::Extrude");
    cut->props().set(model::ProfileFeature::kSketch, sk->id());
    cut->props().set(model::ProfileFeature::kOperation, int(model::ProfileFeature::Cut));
    cut->props().set(model::ProfileFeature::kTarget, box->id());
    cut->props().set("reversed", true);
    cut->props().set("length", 4.0);
    doc.recompute();
    CHECK(cut->state() == model::FeatureState::Ok);
    const double expected = 8000.0 - kPi * 25.0 * 4.0; // the solver applied r = 5
    CHECK_NEAR(cut->shape().volume(), expected, 1e-4);
    CHECK(doc.isConsumed(box->id()));

    // Missing target is a clear error.
    cut->props().set(model::ProfileFeature::kTarget, kNoFeature);
    doc.recompute();
    CHECK(cut->state() == model::FeatureState::Error);
    cut->props().set(model::ProfileFeature::kTarget, box->id());
    doc.recompute();

    // Sketch data survives the file format and drives the cache key.
    const auto j = doc.toJson();
    CHECK(j["features"][1].contains("data"));
    model::Document doc2;
    doc2.loadJson(j);
    doc2.recompute();
    const auto* cut2 = doc2.find(cut->id());
    CHECK(cut2 && cut2->state() == model::FeatureState::Ok);
    CHECK_NEAR(cut2->shape().volume(), expected, 1e-4);

    auto* sk2 = static_cast<model::SketchFeature*>(doc2.find(sk->id()));
    Sketch s = sk2->sketch();
    s.constraints[0].value = 2.0;
    sk2->setSketch(s);
    doc2.recompute();
    CHECK_NEAR(cut2->shape().volume(), 8000.0 - kPi * 4.0 * 4.0, 1e-4);

    // Over-constrained sketch -> error state, dependents fail gracefully.
    s.addConstraint(con(ConstraintType::Radius, Ed(0), {}, 3.0));
    sk2->setSketch(s);
    doc2.recompute();
    CHECK(sk2->state() == model::FeatureState::Error);
    CHECK(cut2->state() == model::FeatureState::Error);
}
