#pragma once
// Shape-level modeling operations (CSG, dress-up features).

#include "geom/Shape.h"

#include <vector>

namespace cf::geom {

enum class BooleanOp { Union = 0, Cut = 1, Intersect = 2 };

/// base (op) tools[0] (op) tools[1] ... ; the result is cleaned up
/// (coplanar faces merged) with ShapeUpgrade_UnifySameDomain.
Shape booleanOp(BooleanOp op, const Shape& base, const std::vector<Shape>& tools);

/// Constant-radius fillet on the given 1-based edge indices of `shape`.
Shape fillet(const Shape& shape, const std::vector<int>& edgeIndices, double radius);

/// Symmetric chamfer on the given 1-based edge indices of `shape`.
Shape chamfer(const Shape& shape, const std::vector<int>& edgeIndices, double distance);

/// Groups several shapes into one compound (used for export).
Shape makeCompound(const std::vector<Shape>& shapes);

} // namespace cf::geom
