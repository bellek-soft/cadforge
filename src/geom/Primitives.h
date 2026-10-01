#pragma once
// Solid primitives. All dimensions in model units (mm). Primitives are built
// in their local frame; features apply their placement afterwards.

#include "geom/Shape.h"

namespace cf::geom {

/// Axis-aligned box from (0,0,0) to (dx,dy,dz), or centered on the origin.
Shape makeBox(double dx, double dy, double dz, bool centered = false);
/// Cylinder along +Z, base centered on the origin. angleDeg in (0, 360].
Shape makeCylinder(double radius, double height, double angleDeg = 360.0);
Shape makeSphere(double radius);
/// Cone/frustum along +Z. One of the radii may be zero.
Shape makeCone(double radius1, double radius2, double height);
/// Torus in the XY plane, centered on the origin.
Shape makeTorus(double majorRadius, double minorRadius);

} // namespace cf::geom
