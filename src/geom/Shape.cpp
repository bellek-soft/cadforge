#include "geom/OcctShape.h"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Version.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Trsf.hxx>

namespace cf::geom {

namespace {
const TopoDS_Shape& nullShape()
{
    static const TopoDS_Shape s;
    return s;
}
} // namespace

Shape fromOcct(const TopoDS_Shape& s)
{
    auto impl = std::make_shared<Shape::Impl>();
    impl->shape = s;
    if (!s.IsNull()) {
        TopExp::MapShapes(s, TopAbs_FACE, impl->faces);
        TopExp::MapShapes(s, TopAbs_EDGE, impl->edges);
    }
    return Shape(std::move(impl));
}

const TopoDS_Shape& toOcct(const Shape& s)
{
    return s.impl() ? s.impl()->shape : nullShape();
}

std::string describe(const Standard_Failure& e)
{
#if OCC_VERSION_MAJOR >= 8
    // OCCT 8: Standard_Failure is a std::exception.
    const char* msg = e.what();
    return (msg && *msg) ? msg : "OpenCASCADE exception";
#else
    const char* msg = e.GetMessageString();
    std::string out = (msg && *msg) ? msg : "";
    std::string type = e.DynamicType()->Name();
    return out.empty() ? type : type + " (" + out + ")";
#endif
}

bool Shape::isNull() const
{
    return !m_impl || m_impl->shape.IsNull();
}

int Shape::faceCount() const { return m_impl ? m_impl->faces.Extent() : 0; }
int Shape::edgeCount() const { return m_impl ? m_impl->edges.Extent() : 0; }

BoundingBox Shape::bounds() const
{
    BoundingBox bb;
    if (isNull())
        return bb;
    try {
        Bnd_Box box;
        BRepBndLib::AddOptimal(m_impl->shape, box, false, false);
        if (box.IsVoid())
            return bb;
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        bb.min = {x0, y0, z0};
        bb.max = {x1, y1, z1};
    } catch (const Standard_Failure&) {
    }
    return bb;
}

double Shape::volume() const
{
    if (isNull())
        return 0.0;
    return guarded("volume", [&] {
        GProp_GProps props;
        BRepGProp::VolumeProperties(m_impl->shape, props);
        return props.Mass();
    });
}

double Shape::area() const
{
    if (isNull())
        return 0.0;
    return guarded("area", [&] {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(m_impl->shape, props);
        return props.Mass();
    });
}

bool Shape::isValid() const
{
    if (isNull())
        return false;
    try {
        BRepCheck_Analyzer analyzer(m_impl->shape);
        return analyzer.IsValid();
    } catch (const Standard_Failure&) {
        return false;
    }
}

Shape Shape::transformed(const Mat4& m) const
{
    if (isNull())
        return *this;
    return guarded("transform", [&] {
        gp_Trsf t;
        // glm is column-major: m[col][row]
        t.SetValues(m[0][0], m[1][0], m[2][0], m[3][0],
                    m[0][1], m[1][1], m[2][1], m[3][1],
                    m[0][2], m[1][2], m[2][2], m[3][2]);
        if (t.Form() == gp_Identity)
            return *this;
        // Moved() shares the underlying TShape (and its triangulation).
        return fromOcct(m_impl->shape.Moved(TopLoc_Location(t)));
    });
}

} // namespace cf::geom
