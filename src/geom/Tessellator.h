#pragma once

#include "core/MeshData.h"
#include "geom/Shape.h"

namespace cf::geom {

struct TessellationParams {
    /// Linear deflection relative to the bounding-box diagonal (used when
    /// absoluteDeflection <= 0).
    double relativeDeflection = 0.0008;
    double absoluteDeflection = 0.0;
    double angularDeflectionDeg = 12.0;
};

/// Triangulates faces and discretizes edges, keeping B-Rep face/edge ids.
MeshData tessellate(const Shape& shape, const TessellationParams& params = {});

} // namespace cf::geom
