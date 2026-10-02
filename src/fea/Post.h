#pragma once
// Post-processing: turns a volume mesh (+ optional results) into a renderable
// surface (core::MeshData) with a per-vertex scalar field.

#include "core/MeshData.h"
#include "fea/FeaTypes.h"

#include <glm/glm.hpp>

namespace cf::fea {

enum class ResultField { None = 0, VonMises, DisplacementMagnitude, DisplacementX, DisplacementY, DisplacementZ };

const char* fieldName(ResultField f);
const char* fieldUnit(ResultField f);

struct SurfaceOptions {
    ResultField field = ResultField::None;
    double deformationScale = 0.0; // 0 = undeformed
    bool elementEdges = true;      // draw the mesh wireframe as edges
};

/// Boundary surface of the mesh. Faces keep their B-Rep face id (pickable),
/// `scalars` holds the chosen field, edges are the boundary element edges.
MeshData resultSurface(const VolumeMesh& mesh, const StaticResult* result, const SurfaceOptions& options);

/// Per-node values of a field (empty for ResultField::None).
std::vector<double> fieldValues(const StaticResult& result, ResultField field);

/// Deformation scale so that the largest displacement looks like ~5% of the model size.
double automaticDeformationScale(const VolumeMesh& mesh, const StaticResult& result);

/// Same colormap as the mesh shader (t in [0, 1]).
glm::vec3 colormap(float t);

} // namespace cf::fea
