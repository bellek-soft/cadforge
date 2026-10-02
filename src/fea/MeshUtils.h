#pragma once
// Mesher-independent helpers: surface welding, feature-edge extraction,
// classification of boundary triangles back to B-Rep faces.

#include "core/MeshData.h"
#include "fea/FeaTypes.h"

#include <array>
#include <memory>
#include <vector>

namespace cf::fea::detail {

/// Watertight version of a per-face tessellation (shared vertices welded).
struct WeldedSurface {
    std::vector<Vec3> points;
    std::vector<std::array<int, 3>> triangles;
    std::vector<int> triangleFace; // 1-based B-Rep face id per triangle
    BoundingBox bounds;
};

WeldedSurface weld(const MeshData& surface);

/// Edges where two different B-Rep faces meet (or open boundary edges),
/// as pairs of point indices into WeldedSurface::points.
std::vector<std::array<int, 2>> featureEdges(const WeldedSurface& s);

/// Number of edges used by exactly one triangle (0 for a closed surface).
int openEdgeCount(const WeldedSurface& s);

/// Finds the B-Rep face closest to a point (uniform grid over the triangles).
class FaceClassifier {
public:
    explicit FaceClassifier(const WeldedSurface& s);
    ~FaceClassifier();
    int classify(const Vec3& p) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

/// Squared distance from p to triangle (a, b, c).
double pointTriangleDistanceSq(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c);

/// Puts element/boundary connectivity into the canonical order documented in
/// VolumeMesh: positive tetrahedra, outward boundary triangles, mid-edge nodes
/// matched geometrically (independent of the mesher's own convention).
void canonicalize(VolumeMesh& mesh);

} // namespace cf::fea::detail
