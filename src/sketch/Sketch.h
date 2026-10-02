#pragma once
// 2D constrained sketch: geometry (points, lines, circles, arcs), geometric and
// dimensional constraints, and a solver based on FreeCAD's PlaneGCS.
//
// The sketch lives in its own 2D coordinate system; the SketchFeature maps it
// onto a plane in 3D. This module depends on core only (no OCCT, no UI), so it
// is easy to test and reuse.

#include "core/Types.h"

#include <nlohmann/json_fwd.hpp>

#include <climits>
#include <string>
#include <vector>

namespace cf::sketch {

enum class GeoType { Point = 0, Line = 1, Circle = 2, Arc = 3 };

/// Which part of a geometry a reference addresses.
enum class PointPos { Edge = 0, Start = 1, End = 2, Center = 3 };

/// Geometry indices >= 0 address Sketch::geometry. Negative indices are the
/// fixed external geometry every sketch has: origin point and the two axes.
inline constexpr int kNoGeo = INT_MIN;
inline constexpr int kOrigin = -1;
inline constexpr int kHAxis = -2;
inline constexpr int kVAxis = -3;

struct Geometry {
    GeoType type = GeoType::Line;
    bool construction = false;
    Vec2 p1{0.0};       // Point: position. Line: start.
    Vec2 p2{0.0};       // Line: end.
    Vec2 center{0.0};   // Circle / Arc
    double radius = 0.0;
    double startAngle = 0.0; // Arc, radians; the arc runs counter-clockwise
    double endAngle = 0.0;   // from startAngle to endAngle (endAngle > startAngle)

    static Geometry point(Vec2 p);
    static Geometry line(Vec2 a, Vec2 b);
    static Geometry circle(Vec2 c, double r);
    static Geometry arc(Vec2 c, double r, double a0, double a1);

    /// Whether this geometry has a point at `pos` (Edge is never a point).
    bool hasPoint(PointPos pos) const;
    /// Position of Start / End / Center.
    Vec2 point(PointPos pos) const;
    /// Closest point of the edge (or the point itself) to `p`.
    Vec2 closestPoint(Vec2 p) const;
};

struct Ref {
    int geo = kNoGeo;
    PointPos pos = PointPos::Edge;

    bool valid() const { return geo != kNoGeo; }
    bool isPoint() const { return valid() && pos != PointPos::Edge; }
    bool isEdge() const { return valid() && pos == PointPos::Edge; }
    bool operator==(const Ref&) const = default;
};

enum class ConstraintType {
    Coincident = 0,   // a, b points
    PointOnObject,    // a point, b edge
    Horizontal,       // a line | a, b points
    Vertical,         // a line | a, b points
    Parallel,         // a, b lines
    Perpendicular,    // a, b lines
    Tangent,          // a, b edges (line/circle/arc)
    Equal,            // a, b lines (length) or circles/arcs (radius)
    Symmetric,        // a, b points symmetric about c (line or point)
    Midpoint,         // a point at the midpoint of line b
    Fixed,            // a point locked at (value, value2)
    Distance,         // a, b points | a point, b line | a line (length)
    DistanceX,        // a, b points | a line | a point (x coordinate)
    DistanceY,        // a, b points | a line | a point (y coordinate)
    Radius,           // a circle/arc
    Diameter,         // a circle/arc
    Angle,            // a, b lines (from a to b) | a line (to the horizontal), degrees
    Count_
};

struct Constraint {
    ConstraintType type = ConstraintType::Coincident;
    Ref a, b, c;
    double value = 0.0;  // mm, or degrees for Angle; x for Fixed
    double value2 = 0.0; // y for Fixed

    /// Dimensional constraints carry an editable value (shown as a dimension).
    bool isDimension() const;
    bool references(int geo) const;
};

const char* constraintName(ConstraintType t);

/// Result of a solve.
struct SolveResult {
    enum class Status {
        Ok,          // all constraints satisfied
        Redundant,   // satisfied, but some constraints are redundant
        Conflicting, // over-constrained; geometry left unchanged
        Failed,      // the solver did not converge; geometry left unchanged
        Invalid,     // some constraints reference unsuitable geometry
    };
    Status status = Status::Ok;
    int dof = 0;                   // remaining degrees of freedom
    std::vector<int> conflicting;  // constraint indices
    std::vector<int> redundant;    // constraint indices
    std::vector<int> malformed;    // constraint indices
    std::string message;

    bool ok() const { return status == Status::Ok || status == Status::Redundant; }
    bool fullyConstrained() const { return ok() && dof == 0; }
};

/// Interactive dragging: the dragged entity follows the cursor as far as the
/// constraints allow (temporary, low-priority constraints).
struct Drag {
    Ref ref;              // a point, or an edge
    Vec2 cursor{0.0};     // cursor position (sketch coordinates)
    Vec2 delta{0.0};      // cursor - grab position (moves whole lines)
    Geometry start;       // the dragged geometry at drag start
};

class Sketch {
public:
    std::vector<Geometry> geometry;
    std::vector<Constraint> constraints;

    int add(const Geometry& g);
    int addConstraint(const Constraint& c);

    /// Removes a geometry, every constraint referencing it, and renumbers references.
    void removeGeometry(int index);
    void removeConstraint(int index);

    bool validRef(const Ref& r) const;
    /// Type of the referenced geometry (external axes are lines, origin is a point).
    GeoType typeOf(int geo) const;
    Vec2 pointAt(const Ref& r) const;

    /// Edges used for solids (non-construction lines, circles, arcs).
    std::vector<int> profileGeometry() const;
    /// Construction lines (e.g. revolve axes) in order.
    std::vector<int> constructionLines() const;

    nlohmann::ordered_json toJson() const;
    static Sketch fromJson(const nlohmann::ordered_json& j);

    bool operator==(const Sketch& other) const;
};

/// Solves `sketch` in place. With `drag`, the dragged entity is pulled towards the cursor.
SolveResult solve(Sketch& sketch, const Drag* drag = nullptr);

/// Current value of a dimensional constraint measured on the geometry (used to
/// initialise new dimensions so that adding them does not move anything).
double measure(const Sketch& sketch, const Constraint& c);

} // namespace cf::sketch
