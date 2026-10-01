#pragma once
// GPU-agnostic triangle/line mesh produced by the geometry kernel and consumed
// by the renderer. Topology ids (1-based face / edge indices of the B-Rep) are
// kept per vertex so that the renderer can pick and highlight sub-shapes.

#include "core/Types.h"

#include <cstdint>
#include <vector>

namespace cf {

struct IndexRange {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

struct MeshData {
    // --- Faces (triangles) ---
    std::vector<float> positions;          // xyz per vertex
    std::vector<float> normals;            // xyz per vertex
    std::vector<std::uint32_t> faceIds;    // per vertex, 1-based B-Rep face index
    std::vector<std::uint32_t> indices;    // triangle list
    std::vector<IndexRange> faceRanges;    // [faceIndex-1] -> range in `indices`

    // --- Edges (line segments, GL_LINES layout) ---
    std::vector<float> edgePositions;      // xyz per line vertex
    std::vector<std::uint32_t> edgeIds;    // per line vertex, 1-based B-Rep edge index
    std::vector<IndexRange> edgeRanges;    // [edgeIndex-1] -> vertex range in `edgePositions`

    // --- Optional per-vertex scalar field (e.g. FEA stress, displacement) ---
    // Empty when unused; otherwise one value per face vertex.
    std::vector<float> scalars;

    BoundingBox bounds;

    std::size_t vertexCount() const { return positions.size() / 3; }
    std::size_t triangleCount() const { return indices.size() / 3; }
    std::size_t edgeVertexCount() const { return edgePositions.size() / 3; }
    bool empty() const { return indices.empty() && edgePositions.empty(); }
};

} // namespace cf
