#pragma once
// MeshData uploaded to the GPU (faces VAO + edges VAO), with per-face and
// per-edge ranges so sub-shapes can be drawn individually for highlighting.

#include "core/MeshData.h"

#include <glad/gl.h>

#include <vector>

namespace cf::render {

class GpuMesh {
public:
    GpuMesh() = default;
    ~GpuMesh();
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&& o) noexcept;
    GpuMesh& operator=(GpuMesh&& o) noexcept;

    void upload(const MeshData& data);
    void release();

    void drawFaces() const;
    void drawFace(int faceIndex) const;  // 1-based
    void drawEdges() const;
    void drawEdge(int edgeIndex) const;  // 1-based

    bool hasScalars() const { return m_hasScalars; }
    const BoundingBox& bounds() const { return m_bounds; }
    int faceCount() const { return static_cast<int>(m_faceRanges.size()); }
    int edgeCount() const { return static_cast<int>(m_edgeRanges.size()); }

private:
    void moveFrom(GpuMesh& o);

    GLuint m_faceVao = 0, m_edgeVao = 0;
    GLuint m_buffers[6] = {};  // pos, normal, faceId, scalar, index, edgePos(+id interleaved below)
    GLuint m_edgeIdBuffer = 0;
    GLsizei m_indexCount = 0;
    GLsizei m_edgeVertexCount = 0;
    std::vector<IndexRange> m_faceRanges;
    std::vector<IndexRange> m_edgeRanges;
    BoundingBox m_bounds;
    bool m_hasScalars = false;
};

} // namespace cf::render
