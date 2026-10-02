#pragma once
// Topological naming support: geometric signatures of faces / edges.
//
// Sub-shape references are stored as 1-based indices, which are only stable
// while the referenced shape's topology does not change. Each reference
// therefore also remembers a signature of the referenced face / edge; when the
// input shape changes, the reference is re-resolved to the sub-shape whose
// signature matches best (same geometry type, nearby, same orientation, similar
// size). See model::Document::recompute.

#include "geom/Shape.h"

#include <vector>

namespace cf::geom {

enum class SubShapeKind { Face = 1, Edge = 2 };

struct SubShapeSignature {
    int geomType = -1;   // OCCT GeomAbs_SurfaceType / GeomAbs_CurveType
    Vec3 center{0.0};    // area / length centroid
    Vec3 dir{0.0};       // plane normal, axis of revolution, line direction (unit) or zero
    double size = 0.0;   // area / length
    double radius = 0.0; // cylinder, cone (reference), sphere, torus (major), circle; else 0
    bool valid() const { return geomType >= 0; }
};

/// Signature of the 1-based `index`-th face / edge of `shape` (invalid if out of range).
SubShapeSignature signature(const Shape& shape, SubShapeKind kind, int index);

/// Dissimilarity of two signatures (0 = identical; infinity = incompatible type).
/// Distances are measured relative to `scale` (e.g. the model diagonal).
double signatureDistance(const SubShapeSignature& a, const SubShapeSignature& b, double scale);

struct SubShapeMatch {
    int index = 0;        // 1-based, 0 = none
    double distance = 0.0;
};

/// Best matching face / edge of `shape` for `sig`.
SubShapeMatch findBestMatch(const Shape& shape, SubShapeKind kind, const SubShapeSignature& sig);

} // namespace cf::geom
