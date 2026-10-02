#pragma once
// Planar profiles (from sketches) and the solids swept from them.

#include "geom/Shape.h"

#include <vector>

namespace cf::geom {

/// A 2D curve in a plane frame (circle parameters: radians, counter-clockwise
/// around the frame normal starting at the frame x axis).
struct Curve2d {
    enum class Kind { Line, Circle, Arc };
    Kind kind = Kind::Line;
    Vec2 a{0.0}, b{0.0};    // Line
    Vec2 center{0.0};       // Circle / Arc
    double radius = 0.0;
    double startAngle = 0.0; // Arc
    double endAngle = 0.0;
};

/// Compound of 3D edges (in `frame`) for the given curves. Degenerate curves are skipped.
Shape makePlanarEdges(const PlaneFrame& frame, const std::vector<Curve2d>& curves);

/// Builds planar faces from the closed loops formed by `edges` (lying in `frame`).
/// Nested loops become holes (even-odd rule). Throws GeomError if a loop is open
/// or loops intersect.
Shape makePlanarFaces(const PlaneFrame& frame, const Shape& edges);

/// Sweeps `profile` (faces) along unit direction `dir` from -`backward` to +`forward`.
Shape extrude(const Shape& profile, const Vec3& dir, double forward, double backward = 0.0);

/// Revolves `profile` around the axis (origin, unit dir) by `angleDeg` (360 = full).
Shape revolve(const Shape& profile, const Vec3& axisOrigin, const Vec3& axisDir, double angleDeg);

} // namespace cf::geom
