#include "geom/Primitives.h"
#include "geom/OcctShape.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>

#include <cmath>

namespace cf::geom {

namespace {
constexpr double kMinSize = 1e-6;

void requirePositive(double v, const char* name)
{
    if (!(v > kMinSize))
        throw GeomError(std::string(name) + " must be greater than zero");
}
} // namespace

Shape makeBox(double dx, double dy, double dz, bool centered)
{
    requirePositive(dx, "Length");
    requirePositive(dy, "Width");
    requirePositive(dz, "Height");
    return guarded("Box", [&] {
        gp_Pnt origin = centered ? gp_Pnt(-dx / 2, -dy / 2, -dz / 2) : gp_Pnt(0, 0, 0);
        return fromOcct(BRepPrimAPI_MakeBox(origin, dx, dy, dz).Shape());
    });
}

Shape makeCylinder(double radius, double height, double angleDeg)
{
    requirePositive(radius, "Radius");
    requirePositive(height, "Height");
    if (!(angleDeg > 0.0 && angleDeg <= 360.0))
        throw GeomError("Angle must be in (0, 360]");
    return guarded("Cylinder", [&] {
        const double a = angleDeg * 3.14159265358979323846 / 180.0;
        return fromOcct(BRepPrimAPI_MakeCylinder(radius, height, a).Shape());
    });
}

Shape makeSphere(double radius)
{
    requirePositive(radius, "Radius");
    return guarded("Sphere", [&] { return fromOcct(BRepPrimAPI_MakeSphere(radius).Shape()); });
}

Shape makeCone(double r1, double r2, double height)
{
    requirePositive(height, "Height");
    if (r1 < 0 || r2 < 0)
        throw GeomError("Radii must not be negative");
    if (std::abs(r1 - r2) < kMinSize)
        throw GeomError("Radius1 and Radius2 must differ (use a cylinder instead)");
    return guarded("Cone", [&] { return fromOcct(BRepPrimAPI_MakeCone(r1, r2, height).Shape()); });
}

Shape makeTorus(double majorRadius, double minorRadius)
{
    requirePositive(majorRadius, "Major radius");
    requirePositive(minorRadius, "Minor radius");
    if (minorRadius >= majorRadius)
        throw GeomError("Minor radius must be smaller than major radius");
    return guarded("Torus", [&] {
        return fromOcct(BRepPrimAPI_MakeTorus(majorRadius, minorRadius).Shape());
    });
}

} // namespace cf::geom
