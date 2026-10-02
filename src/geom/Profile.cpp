#include "geom/Profile.h"
#include "geom/OcctShape.h"

#include <BOPAlgo_Tools.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <ShapeFix_Face.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace cf::geom {

namespace {

constexpr double kPi = 3.14159265358979323846;

gp_Pnt pnt(const Vec3& v) { return gp_Pnt(v.x, v.y, v.z); }
gp_Dir dir(const Vec3& v) { return gp_Dir(v.x, v.y, v.z); }

gp_Ax2 axes(const PlaneFrame& f, const Vec3& origin)
{
    return gp_Ax2(pnt(origin), dir(f.normal()), dir(f.xDir));
}

double signedArea(const TopoDS_Face& f)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(f, props);
    return props.Mass();
}

/// Every vertex of a closed, simple wire is used by exactly two edge ends.
bool isSimpleClosed(const TopoDS_Wire& w)
{
    std::vector<std::pair<TopoDS_Vertex, int>> uses;
    for (TopExp_Explorer ex(w, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Vertex v[2];
        TopExp::Vertices(TopoDS::Edge(ex.Current()), v[0], v[1]);
        for (const auto& vx : v) {
            if (vx.IsNull())
                return false;
            auto it = std::find_if(uses.begin(), uses.end(), [&](const auto& u) { return u.first.IsSame(vx); });
            if (it == uses.end())
                uses.emplace_back(vx, 1);
            else
                ++it->second;
        }
    }
    if (uses.empty())
        return false;
    return std::all_of(uses.begin(), uses.end(), [](const auto& u) { return u.second == 2; });
}

struct Loop {
    TopoDS_Wire wire;  // oriented counter-clockwise around the plane normal
    TopoDS_Face face;  // the loop filled
    double area = 0.0;
    int parent = -1;
    int depth = 0;
};

/// True if `inner` lies inside `outer` (tested with points on inner's edges).
bool contains(const Loop& outer, const Loop& inner)
{
    BRepClass_FaceClassifier classifier;
    for (TopExp_Explorer ex(inner.wire, TopAbs_EDGE); ex.More(); ex.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
        for (double t : {0.5, 0.25, 0.75}) {
            const gp_Pnt p = c.Value(c.FirstParameter() + t * (c.LastParameter() - c.FirstParameter()));
            classifier.Perform(outer.face, p, 1e-6);
            const TopAbs_State st = classifier.State();
            if (st == TopAbs_IN)
                return true;
            if (st == TopAbs_OUT)
                return false;
        }
    }
    return false;
}

TopoDS_Shape makeCompoundOf(const std::vector<TopoDS_Shape>& shapes)
{
    BRep_Builder b;
    TopoDS_Compound c;
    b.MakeCompound(c);
    for (const auto& s : shapes)
        b.Add(c, s);
    return c;
}

} // namespace

Shape makePlanarEdges(const PlaneFrame& frame, const std::vector<Curve2d>& curves)
{
    return guarded("Sketch edges", [&] {
        std::vector<TopoDS_Shape> edges;
        for (const auto& c : curves) {
            switch (c.kind) {
            case Curve2d::Kind::Line:
                if (glm::length(c.b - c.a) < 1e-7)
                    continue;
                edges.push_back(BRepBuilderAPI_MakeEdge(pnt(frame.toWorld(c.a)), pnt(frame.toWorld(c.b))).Edge());
                break;
            case Curve2d::Kind::Circle:
                if (c.radius < 1e-7)
                    continue;
                edges.push_back(BRepBuilderAPI_MakeEdge(gp_Circ(axes(frame, frame.toWorld(c.center)), c.radius)).Edge());
                break;
            case Curve2d::Kind::Arc:
                if (c.radius < 1e-7 || c.endAngle - c.startAngle < 1e-9)
                    continue;
                edges.push_back(BRepBuilderAPI_MakeEdge(gp_Circ(axes(frame, frame.toWorld(c.center)), c.radius),
                                                        c.startAngle, c.endAngle)
                                    .Edge());
                break;
            }
        }
        return fromOcct(makeCompoundOf(edges));
    });
}

Shape makePlanarFaces(const PlaneFrame& frame, const Shape& edges)
{
    if (edges.isNull() || edges.edgeCount() == 0)
        throw GeomError("The sketch has no profile geometry");

    return guarded("Profile", [&] {
        TopoDS_Shape wires;
        if (BOPAlgo_Tools::EdgesToWires(toOcct(edges), wires, false) != 0)
            throw GeomError("could not connect the sketch edges into loops");

        const gp_Pln plane(gp_Ax3(pnt(frame.origin), dir(frame.normal()), dir(frame.xDir)));
        std::vector<Loop> loops;
        for (TopExp_Explorer ex(wires, TopAbs_WIRE); ex.More(); ex.Next()) {
            TopoDS_Wire w = TopoDS::Wire(ex.Current());
            if (!isSimpleClosed(w))
                throw GeomError("the sketch contains open or intersecting curves; profiles must be closed loops");
            Loop l;
            l.wire = w;
            BRepBuilderAPI_MakeFace mf(plane, w, true);
            if (!mf.IsDone())
                throw GeomError("could not fill a sketch loop");
            l.face = mf.Face();
            l.area = signedArea(l.face);
            if (l.area < 0.0) {
                l.wire = TopoDS::Wire(w.Reversed());
                BRepBuilderAPI_MakeFace mf2(plane, l.wire, true);
                l.face = mf2.Face();
                l.area = signedArea(l.face);
            }
            if (std::abs(l.area) < 1e-12)
                continue;
            loops.push_back(std::move(l));
        }
        if (loops.empty())
            throw GeomError("the sketch has no closed loop");

        std::sort(loops.begin(), loops.end(), [](const Loop& a, const Loop& b) { return a.area > b.area; });
        for (size_t i = 0; i < loops.size(); ++i)
            for (size_t j = i; j-- > 0;)
                if (contains(loops[j], loops[i])) {
                    loops[i].parent = int(j);
                    loops[i].depth = loops[j].depth + 1;
                    break;
                }

        std::vector<TopoDS_Shape> faces;
        for (size_t i = 0; i < loops.size(); ++i) {
            if (loops[i].depth % 2 != 0)
                continue;
            BRepBuilderAPI_MakeFace mf(loops[i].face);
            for (size_t h = 0; h < loops.size(); ++h)
                if (loops[h].parent == int(i) && loops[h].depth % 2 == 1)
                    mf.Add(TopoDS::Wire(loops[h].wire.Reversed()));
            ShapeFix_Face fix(mf.Face());
            fix.Perform();
            faces.push_back(fix.Face());
        }
        return fromOcct(faces.size() == 1 ? faces.front() : makeCompoundOf(faces));
    });
}

Shape extrude(const Shape& profile, const Vec3& direction, double forward, double backward)
{
    if (profile.isNull() || profile.faceCount() == 0)
        throw GeomError("Extrude: nothing to extrude");
    const double total = forward + backward;
    if (!(std::abs(total) > 1e-7))
        throw GeomError("Extrude: the length must not be zero");

    return guarded("Extrude", [&] {
        TopoDS_Shape base = toOcct(profile);
        if (backward != 0.0) {
            gp_Trsf t;
            t.SetTranslation(gp_Vec(-direction.x * backward, -direction.y * backward, -direction.z * backward));
            base = BRepBuilderAPI_Transform(base, t, true).Shape();
        }
        const Vec3 v = direction * total;
        BRepPrimAPI_MakePrism prism(base, gp_Vec(v.x, v.y, v.z), true);
        if (!prism.IsDone())
            throw GeomError("the sweep failed");
        return fromOcct(prism.Shape());
    });
}

Shape revolve(const Shape& profile, const Vec3& axisOrigin, const Vec3& axisDir, double angleDeg)
{
    if (profile.isNull() || profile.faceCount() == 0)
        throw GeomError("Revolve: nothing to revolve");
    if (!(std::abs(angleDeg) > 1e-6))
        throw GeomError("Revolve: the angle must not be zero");

    return guarded("Revolve", [&] {
        const gp_Ax1 axis(pnt(axisOrigin), dir(axisDir));
        const bool full = std::abs(angleDeg) >= 360.0 - 1e-9;
        std::unique_ptr<BRepPrimAPI_MakeRevol> revol =
            full ? std::make_unique<BRepPrimAPI_MakeRevol>(toOcct(profile), axis, true)
                 : std::make_unique<BRepPrimAPI_MakeRevol>(toOcct(profile), axis, angleDeg * kPi / 180.0,
                                                           true);
        if (!revol->IsDone())
            throw GeomError("the revolution failed");
        const TopoDS_Shape s = revol->Shape();
        if (!BRepCheck_Analyzer(s).IsValid())
            throw GeomError("the result is invalid - does the axis cross the profile?");
        return fromOcct(s);
    });
}

} // namespace cf::geom
