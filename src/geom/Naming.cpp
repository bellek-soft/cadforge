#include "geom/Naming.h"
#include "geom/OcctShape.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

#include <cmath>
#include <limits>

namespace cf::geom {

namespace {

Vec3 vec(const gp_XYZ& v) { return {v.X(), v.Y(), v.Z()}; }
Vec3 vec(const gp_Dir& d) { return {d.X(), d.Y(), d.Z()}; }

SubShapeSignature faceSignature(const TopoDS_Face& f)
{
    SubShapeSignature s;
    BRepAdaptor_Surface surf(f, false);
    s.geomType = int(surf.GetType());
    GProp_GProps props;
    BRepGProp::SurfaceProperties(f, props);
    s.size = props.Mass();
    s.center = vec(props.CentreOfMass().XYZ());
    switch (surf.GetType()) {
    case GeomAbs_Plane: {
        gp_Dir n = surf.Plane().Axis().Direction();
        if (f.Orientation() == TopAbs_REVERSED)
            n.Reverse();
        s.dir = vec(n);
        break;
    }
    case GeomAbs_Cylinder:
        s.dir = vec(surf.Cylinder().Axis().Direction());
        s.radius = surf.Cylinder().Radius();
        break;
    case GeomAbs_Cone:
        s.dir = vec(surf.Cone().Axis().Direction());
        s.radius = surf.Cone().RefRadius();
        break;
    case GeomAbs_Sphere:
        s.radius = surf.Sphere().Radius();
        break;
    case GeomAbs_Torus:
        s.dir = vec(surf.Torus().Axis().Direction());
        s.radius = surf.Torus().MajorRadius();
        break;
    default:
        break;
    }
    return s;
}

SubShapeSignature edgeSignature(const TopoDS_Edge& e)
{
    SubShapeSignature s;
    BRepAdaptor_Curve curve(e);
    s.geomType = int(curve.GetType());
    GProp_GProps props;
    BRepGProp::LinearProperties(e, props);
    s.size = props.Mass();
    s.center = vec(props.CentreOfMass().XYZ());
    switch (curve.GetType()) {
    case GeomAbs_Line:
        s.dir = vec(curve.Line().Direction());
        break;
    case GeomAbs_Circle:
        s.dir = vec(curve.Circle().Axis().Direction());
        s.radius = curve.Circle().Radius();
        break;
    default:
        break;
    }
    return s;
}

const NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>& mapOf(const Shape& shape, SubShapeKind kind)
{
    return kind == SubShapeKind::Face ? shape.impl()->faces : shape.impl()->edges;
}

} // namespace

SubShapeSignature signature(const Shape& shape, SubShapeKind kind, int index)
{
    if (shape.isNull())
        return {};
    const auto& map = mapOf(shape, kind);
    if (index < 1 || index > map.Extent())
        return {};
    try {
        return kind == SubShapeKind::Face ? faceSignature(TopoDS::Face(map(index)))
                                          : edgeSignature(TopoDS::Edge(map(index)));
    } catch (const Standard_Failure&) {
        return {};
    }
}

double signatureDistance(const SubShapeSignature& a, const SubShapeSignature& b, double scale)
{
    if (!a.valid() || !b.valid() || a.geomType != b.geomType)
        return std::numeric_limits<double>::infinity();
    scale = std::max(scale, 1e-9);
    double d = glm::length(a.center - b.center) / scale;
    const double la = glm::length(a.dir), lb = glm::length(b.dir);
    if (la > 0.5 && lb > 0.5)
        d += 1.0 - std::abs(glm::dot(a.dir, b.dir)); // axis direction (sign-free)
    d += std::abs(a.radius - b.radius) / scale;
    if (a.size > 0.0 && b.size > 0.0)
        d += 0.1 * std::abs(std::log(a.size / b.size));
    return d;
}

SubShapeMatch findBestMatch(const Shape& shape, SubShapeKind kind, const SubShapeSignature& sig)
{
    SubShapeMatch best;
    best.distance = std::numeric_limits<double>::infinity();
    if (shape.isNull() || !sig.valid())
        return best;
    const double scale = std::max(shape.bounds().diagonal(), 1e-6);
    const int n = mapOf(shape, kind).Extent();
    for (int i = 1; i <= n; ++i) {
        const double d = signatureDistance(sig, signature(shape, kind, i), scale);
        if (d < best.distance) {
            best.distance = d;
            best.index = i;
        }
    }
    return best;
}

} // namespace cf::geom
