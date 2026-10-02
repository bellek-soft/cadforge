#pragma once
// Measurements on shapes, faces and edges (for the measure tool).

#include "geom/Shape.h"

namespace cf::geom {

/// What a measurement refers to: a whole shape or one of its faces / edges.
struct MeasureTarget {
    const Shape* shape = nullptr;
    int kind = 0;  // 0 = whole shape, 1 = face, 2 = edge (same values as render::PickKind)
    int index = 0; // 1-based for faces / edges
};

struct EntityMeasure {
    bool valid = false;
    int kind = 0;
    double length = 0.0;   // edge
    double area = 0.0;     // face / whole shape
    double volume = 0.0;   // whole shape
    Vec3 center{0.0};      // centroid (curve, surface or volume)
    BoundingBox bounds;
    bool hasRadius = false; // circle edge, cylinder / sphere / cone / torus face
    double radius = 0.0;
    bool hasDirection = false; // line direction, plane normal or axis of revolution
    Vec3 direction{0.0};
    const char* typeName = ""; // "Line", "Circle", "Plane", "Cylinder", ...
};

EntityMeasure measureEntity(const MeasureTarget& t);

struct DistanceMeasure {
    bool valid = false;
    double distance = 0.0;   // minimum distance
    Vec3 pointA{0.0}, pointB{0.0};
    bool hasAngle = false;   // both have a direction (lines, planes, axes)
    double angleDeg = 0.0;   // 0..90 for axes / lines, 0..180 between plane normals
    Vec3 delta{0.0};         // pointB - pointA
};

DistanceMeasure measureDistance(const MeasureTarget& a, const MeasureTarget& b);

} // namespace cf::geom
