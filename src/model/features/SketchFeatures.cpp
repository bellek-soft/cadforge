#include "model/features/SketchFeatures.h"
#include "model/Document.h"
#include "model/FeatureRegistry.h"

#include "geom/Operations.h"
#include "geom/Profile.h"

#include <nlohmann/json.hpp>

namespace cf::model {

namespace {
constexpr double kMaxLen = 1e6;

template <typename T>
FeatureTypeInfo info(const char* type, const char* label, const char* category)
{
    return {type, label, category, [] { return std::make_unique<T>(); }};
}

geom::Curve2d toCurve(const sketch::Geometry& g)
{
    geom::Curve2d c;
    switch (g.type) {
    case sketch::GeoType::Line:
        c.kind = geom::Curve2d::Kind::Line;
        c.a = g.p1;
        c.b = g.p2;
        break;
    case sketch::GeoType::Circle:
        c.kind = geom::Curve2d::Kind::Circle;
        c.center = g.center;
        c.radius = g.radius;
        break;
    case sketch::GeoType::Arc:
        c.kind = geom::Curve2d::Kind::Arc;
        c.center = g.center;
        c.radius = g.radius;
        c.startAngle = g.startAngle;
        c.endAngle = g.endAngle;
        break;
    case sketch::GeoType::Point:
        break;
    }
    return c;
}
} // namespace

void registerSketchFeatures(FeatureRegistry& r)
{
    r.add(info<SketchFeature>("Sketch::Sketch", "Sketch", "Sketch"));
    r.add(info<ExtrudeFeature>("Part::Extrude", "Extrude", "Sketch"));
    r.add(info<RevolveFeature>("Part::Revolve", "Revolve", "Sketch"));
}

// ---- Sketch -----------------------------------------------------------------------

SketchFeature::SketchFeature()
{
    props().add(makeEnum(kPlane, "Plane", XY, {"XY", "XZ", "YZ"}, "Sketch"));
    Property& off = props().add(makeDouble(kOffset, "Offset", 0.0, -kMaxLen, kMaxLen, "mm", "Sketch"));
    off.tooltip = "Distance of the sketch plane from the origin along its normal";
    // Sketches live on their base plane; the generic placement is not used.
    props().find(kPosition)->hidden = true;
    props().find(kRotation)->hidden = true;
    setColor({0.95f, 0.95f, 0.98f, 1.0f});
}

PlaneFrame SketchFeature::planeFrame(Plane plane, double offset)
{
    PlaneFrame f;
    switch (plane) {
    case XY:
        f.xDir = {1, 0, 0};
        f.yDir = {0, 1, 0};
        break;
    case XZ:
        f.xDir = {1, 0, 0};
        f.yDir = {0, 0, 1};
        break;
    case YZ:
        f.xDir = {0, 1, 0};
        f.yDir = {0, 0, 1};
        break;
    }
    f.origin = f.normal() * offset;
    return f;
}

PlaneFrame SketchFeature::frame() const
{
    return planeFrame(static_cast<Plane>(props().get<int>(kPlane)), props().get<double>(kOffset));
}

geom::Shape SketchFeature::execute(const ExecContext&)
{
    sketch::Sketch solved = m_sketch;
    m_lastSolve = sketch::solve(solved);
    if (!m_lastSolve.ok())
        throw geom::GeomError(m_lastSolve.message);
    std::vector<geom::Curve2d> curves;
    for (int i : solved.profileGeometry())
        curves.push_back(toCurve(solved.geometry[size_t(i)]));
    return geom::makePlanarEdges(frame(), curves);
}

std::string SketchFeature::cacheSalt() const
{
    return m_sketch.toJson().dump();
}

void SketchFeature::saveData(nlohmann::ordered_json& out) const
{
    out = m_sketch.toJson();
}

void SketchFeature::loadData(const nlohmann::ordered_json& in)
{
    m_sketch = sketch::Sketch::fromJson(in);
}

// ---- Profile features -------------------------------------------------------------

ProfileFeature::ProfileFeature()
{
    Property& s = props().add(makeRef(kSketch, "Sketch", "Inputs"));
    s.tooltip = "Sketch whose closed loops form the profile";
    props().add(makeEnum(kOperation, "Operation", NewBody, {"New body", "Join", "Cut", "Intersect"}, "Operation"));
    Property& t = props().add(makeRef(kTarget, "Target body", "Operation"));
    t.tooltip = "Body to join with / cut from / intersect with (not used for 'New body')";
}

const SketchFeature& ProfileFeature::profileSketch(const ExecContext& ctx) const
{
    const FeatureId id = props().get<FeatureId>(kSketch);
    const auto* sk = dynamic_cast<const SketchFeature*>(ctx.document().find(id));
    if (!sk)
        throw geom::GeomError("Select a sketch as the profile");
    return *sk;
}

geom::Shape ProfileFeature::profileFaces(const ExecContext& ctx) const
{
    const SketchFeature& sk = profileSketch(ctx);
    return geom::makePlanarFaces(sk.frame(), ctx.input(sk.id()));
}

geom::Shape ProfileFeature::combine(const ExecContext& ctx, const geom::Shape& tool) const
{
    const int op = props().get<int>(kOperation);
    if (op == NewBody)
        return tool;
    const FeatureId target = props().get<FeatureId>(kTarget);
    if (target == kNoFeature)
        throw geom::GeomError("Choose a target body for the operation (or use 'New body')");
    const geom::BooleanOp bop = op == Join ? geom::BooleanOp::Union
                                : op == Cut ? geom::BooleanOp::Cut
                                            : geom::BooleanOp::Intersect;
    return geom::booleanOp(bop, ctx.input(target), {tool});
}

ExtrudeFeature::ExtrudeFeature()
{
    props().add(makeDouble("length", "Length", 10.0, 1e-3, kMaxLen, "mm", "Extrude"));
    Property& sym = props().add(makeBool("symmetric", "Symmetric", false, "Extrude"));
    sym.tooltip = "Extrude half the length to each side of the sketch plane";
    props().add(makeBool("reversed", "Reversed", false, "Extrude"));
}

geom::Shape ExtrudeFeature::execute(const ExecContext& ctx)
{
    const SketchFeature& sk = profileSketch(ctx);
    const geom::Shape faces = profileFaces(ctx);
    const double length = props().get<double>("length");
    Vec3 dir = sk.frame().normal();
    if (props().get<bool>("reversed"))
        dir = -dir;
    const bool sym = props().get<bool>("symmetric");
    const geom::Shape tool = geom::extrude(faces, dir, sym ? length * 0.5 : length, sym ? length * 0.5 : 0.0);
    return combine(ctx, tool);
}

RevolveFeature::RevolveFeature()
{
    props().add(makeEnum("axis", "Axis", SketchV, {"Sketch H axis", "Sketch V axis", "Construction line"},
                         "Revolve"));
    Property& line = props().add(makeInt("axisLine", "Construction line #", 1, 1, 1000, "Revolve"));
    line.tooltip = "Which construction line of the sketch is the axis (when Axis = Construction line)";
    Property& a = props().add(makeDouble("angle", "Angle", 360.0, 0.1, 360.0, "deg", "Revolve"));
    a.speed = 1.0;
    props().add(makeBool("reversed", "Reversed", false, "Revolve"));
}

geom::Shape RevolveFeature::execute(const ExecContext& ctx)
{
    const SketchFeature& sk = profileSketch(ctx);
    const PlaneFrame fr = sk.frame();
    Vec3 origin = fr.origin, dir = fr.xDir;
    switch (props().get<int>("axis")) {
    case SketchH: break;
    case SketchV: dir = fr.yDir; break;
    default: {
        const auto lines = sk.sketch().constructionLines();
        const int n = props().get<int>("axisLine");
        if (n < 1 || n > int(lines.size()))
            throw geom::GeomError("The sketch has no construction line #" + std::to_string(n));
        const auto& g = sk.sketch().geometry[size_t(lines[size_t(n - 1)])];
        origin = fr.toWorld(g.p1);
        const Vec3 d = fr.toWorld(g.p2) - origin;
        if (glm::length(d) < 1e-9)
            throw geom::GeomError("The axis line has zero length");
        dir = glm::normalize(d);
        break;
    }
    }
    if (props().get<bool>("reversed"))
        dir = -dir;
    const geom::Shape tool = geom::revolve(profileFaces(ctx), origin, dir, props().get<double>("angle"));
    return combine(ctx, tool);
}

} // namespace cf::model
