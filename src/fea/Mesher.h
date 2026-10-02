#pragma once
// Tetrahedral volume meshing (Netgen / nglib backend).

#include "core/MeshData.h"
#include "fea/FeaTypes.h"

#include <string>
#include <vector>

namespace cf::fea {

/// Smaller elements on some B-Rep faces.
struct LocalMeshSize {
    std::vector<int> faces; // 1-based B-Rep face ids (MeshData::faceIds)
    double size = 1.0;      // mm
};

struct MeshSettings {
    double maxSize = 0.0;   // target element size in mm (<= 0: automatic, diagonal / 20)
    double minSize = 0.0;   // lower bound for local refinement (0 = none)
    double grading = 0.3;   // 0 = uniform ... 1 = aggressive local refinement
    ElementOrder order = ElementOrder::Quadratic;
    std::vector<LocalMeshSize> localSizes;
};

/// Builds a tetrahedral mesh of the closed solid described by `surface`
/// (a fine tessellation with B-Rep face ids, see geom::tessellate).
/// Boundary triangles of the result carry the B-Rep face id, so loads and
/// supports defined on CAD faces can be applied to the mesh.
/// Throws FeaError on failure. Thread-safe (calls are serialized internally).
VolumeMesh generateVolumeMesh(const MeshData& surface, const MeshSettings& settings);

/// Automatic element size used when MeshSettings::maxSize <= 0.
double automaticElementSize(const MeshData& surface);

/// Name/version of the meshing backend (for the UI).
std::string mesherName();

} // namespace cf::fea
