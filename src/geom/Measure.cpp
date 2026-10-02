#include "geom/Measure.h"
#include "geom/OcctShape.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

#include <algorithm>
#include <cmath>
#include <string>

namespace cf::geom {

namespace {

Vec3 vec(const gp_XYZ& v) { return {v.X(), v.Y(), v.Z()}; }
Vec3 vec(const gp_Dir& d) { return {d.X(), d.Y(), d.Z()}; }

TopoDS_Shape subShape(const MeasureTarget& t)
{
    if (!t.shape || t.shape->isNull())
        return {};
    const auto* impl = t.shape->impl();
    if (t.kind == 1)
        return t.index >= 1 && t.index <= impl->faces.Extent() ? impl->faces(t.index) : TopoDS_Shape();
    if (t.kind == 2)
        return t.index >= 1 && t.index <= impl->edges.Extent() ? impl->edges(t.index) : TopoDS_Shape();
    return impl->shape;
}

} // namespace

EntityMeasure measureEntity(const MeasureTarget& t)
{
    EntityMeasure m;
    const TopoDS_Shape s = subShape(t);
    if (s.IsNull())
        return m;
    try {
        m.kind = t.kind;
        Bnd_Box box;
        BRepBndLib::Add(s, box);
        if (!box.IsVoid()) {
            double x0, y0, z0, x1, y1, z1;
            box.Get(x0, y0, z0, x1, y1, z1);
            m.bounds.add(Vec3(x0, y0, z0));
            m.bounds.add(Vec3(x1, y1, z1));
        }
        GProp_GProps props;
        if (t.kind == 2) {
            const TopoDS_Edge& e = TopoDS::Edge(s);
            BRepGProp::LinearProperties(e, props);
            m.length = props.Mass();
            m.center = vec(props.CentreOfMass().XYZ());
            BRepAdaptor_Curve c(e);
            switch (c.GetType()) {
            case GeomAbs_Line:
                m.typeName = "Line";
                m.hasDirection = true;
                m.direction = vec(c.Line().Direction());
                break;
            case GeomAbs_Circle:
                m.typeName = "Circle / arc";
                m.hasRadius = true;
                m.radius = c.Circle().Radius();
                m.hasDirection = true;
                m.direction = vec(c.Circle().Axis().Direction());
                m.center = vec(c.Circle().Location().XYZ());
                break;
            case GeomAbs_Ellipse: m.typeName = "Ellipse"; break;
            case GeomAbs_BSplineCurve: m.typeName = "B-spline"; break;
            default: m.typeName = "Curve"; break;
            }
        } else if (t.kind == 1) {
            const TopoDS_Face& f = TopoDS::Face(s);
            BRepGProp::SurfaceProperties(f, props);
            m.area = props.Mass();
            m.center = vec(props.CentreOfMass().XYZ());
            BRepAdaptor_Surface sf(f);
            switch (sf.GetType()) {
            case GeomAbs_Plane: {
                m.typeName = "Plane";
                gp_Dir n = sf.Plane().Axis().Direction();
                if (f.Orientation() == TopAbs_REVERSED)
                    n.Reverse();
                m.hasDirection = true;
                m.direction = vec(n);
                break;
            }
            case GeomAbs_Cylinder:
                m.typeName = "Cylinder";
                m.hasRadius = true;
                m.radius = sf.Cylinder().Radius();
                m.hasDirection = true;
                m.direction = vec(sf.Cylinder().Axis().Direction());
                break;
            case GeomAbs_Cone:
                m.typeName = "Cone";
                m.hasDirection = true;
                m.direction = vec(sf.Cone().Axis().Direction());
                break;
            case GeomAbs_Sphere:
                m.typeName = "Sphere";
                m.hasRadius = true;
                m.radius = sf.Sphere().Radius();
                break;
            case GeomAbs_Torus:
                m.typeName = "Torus";
                m.hasRadius = true;
                m.radius = sf.Torus().MinorRadius();
                m.hasDirection = true;
                m.direction = vec(sf.Torus().Axis().Direction());
                break;
            default: m.typeName = "Surface"; break;
            }
        } else {
            m.typeName = "Solid";
            BRepGProp::VolumeProperties(s, props);
            m.volume = props.Mass();
            m.center = vec(props.CentreOfMass().XYZ());
            GProp_GProps sp;
            BRepGProp::SurfaceProperties(s, sp);
            m.area = sp.Mass();
            if (std::abs(m.volume) < 1e-12) {
                m.typeName = "Shape";
                m.center = m.bounds.valid() ? m.bounds.center() : Vec3(0.0);
            }
        }
        m.valid = true;
    } catch (const Standard_Failure&) {
        m.valid = false;
    }
    return m;
}

DistanceMeasure measureDistance(const MeasureTarget& a, const MeasureTarget& b)
{
    DistanceMeasure d;
    const TopoDS_Shape sa = subShape(a), sb = subShape(b);
    if (sa.IsNull() || sb.IsNull())
        return d;
    try {
        BRepExtrema_DistShapeShape ext(sa, sb);
        if (!ext.IsDone() || ext.NbSolution() < 1)
            return d;
        d.distance = ext.Value();
        d.pointA = vec(ext.PointOnShape1(1).XYZ());
        d.pointB = vec(ext.PointOnShape2(1).XYZ());
        d.delta = d.pointB - d.pointA;
        d.valid = true;
        const EntityMeasure ea = measureEntity(a), eb = measureEntity(b);
        if (ea.hasDirection && eb.hasDirection) {
            d.hasAngle = true;
            double c = std::clamp(glm::dot(ea.direction, eb.direction), -1.0, 1.0);
            const bool bothPlanes = std::string(ea.typeName) == "Plane" && std::string(eb.typeName) == "Plane";
            if (!bothPlanes)
                c = std::abs(c); // axes / lines have no orientation
            d.angleDeg = std::acos(c) * 180.0 / 3.14159265358979323846;
            // Line vs plane: angle to the plane, not to its normal.
            const bool linePlane = (std::string(ea.typeName) == "Line" && std::string(eb.typeName) == "Plane") ||
                                   (std::string(eb.typeName) == "Line" && std::string(ea.typeName) == "Plane");
            if (linePlane)
                d.angleDeg = 90.0 - d.angleDeg;
        }
    } catch (const Standard_Failure&) {
        d.valid = false;
    }
    return d;
}

} // namespace cf::geom
