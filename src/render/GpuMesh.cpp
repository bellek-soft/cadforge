#include "render/GpuMesh.h"

#include <utility>

namespace cf::render {

namespace {
enum Buffer { Pos = 0, Normal, FaceId, Scalar, Index, EdgePos };

template <typename T>
void bufferData(GLenum target, GLuint buf, const std::vector<T>& v)
{
    glBindBuffer(target, buf);
    glBufferData(target, static_cast<GLsizeiptr>(v.size() * sizeof(T)), v.empty() ? nullptr : v.data(),
                 GL_STATIC_DRAW);
}
} // namespace

GpuMesh::~GpuMesh() { release(); }

GpuMesh::GpuMesh(GpuMesh&& o) noexcept { moveFrom(o); }

GpuMesh& GpuMesh::operator=(GpuMesh&& o) noexcept
{
    if (this != &o) {
        release();
        moveFrom(o);
    }
    return *this;
}

void GpuMesh::moveFrom(GpuMesh& o)
{
    m_faceVao = std::exchange(o.m_faceVao, 0);
    m_edgeVao = std::exchange(o.m_edgeVao, 0);
    for (int i = 0; i < 6; ++i)
        m_buffers[i] = std::exchange(o.m_buffers[i], 0);
    m_edgeIdBuffer = std::exchange(o.m_edgeIdBuffer, 0);
    m_indexCount = o.m_indexCount;
    m_edgeVertexCount = o.m_edgeVertexCount;
    m_faceRanges = std::move(o.m_faceRanges);
    m_edgeRanges = std::move(o.m_edgeRanges);
    m_bounds = o.m_bounds;
    m_hasScalars = o.m_hasScalars;
}

void GpuMesh::release()
{
    if (m_faceVao) glDeleteVertexArrays(1, &m_faceVao);
    if (m_edgeVao) glDeleteVertexArrays(1, &m_edgeVao);
    if (m_buffers[0]) glDeleteBuffers(6, m_buffers);
    if (m_edgeIdBuffer) glDeleteBuffers(1, &m_edgeIdBuffer);
    m_faceVao = m_edgeVao = m_edgeIdBuffer = 0;
    for (auto& b : m_buffers) b = 0;
    m_indexCount = m_edgeVertexCount = 0;
    m_faceRanges.clear();
    m_edgeRanges.clear();
}

void GpuMesh::upload(const MeshData& d)
{
    release();
    glGenVertexArrays(1, &m_faceVao);
    glGenVertexArrays(1, &m_edgeVao);
    glGenBuffers(6, m_buffers);
    glGenBuffers(1, &m_edgeIdBuffer);

    // ---- faces ----
    glBindVertexArray(m_faceVao);
    bufferData(GL_ARRAY_BUFFER, m_buffers[Pos], d.positions);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    bufferData(GL_ARRAY_BUFFER, m_buffers[Normal], d.normals);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    bufferData(GL_ARRAY_BUFFER, m_buffers[FaceId], d.faceIds);
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, 0, nullptr);

    m_hasScalars = !d.scalars.empty() && d.scalars.size() == d.vertexCount();
    if (m_hasScalars) {
        bufferData(GL_ARRAY_BUFFER, m_buffers[Scalar], d.scalars);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    } else {
        glDisableVertexAttribArray(3);
        glVertexAttrib1f(3, 0.0f);
    }

    bufferData(GL_ELEMENT_ARRAY_BUFFER, m_buffers[Index], d.indices);
    m_indexCount = static_cast<GLsizei>(d.indices.size());

    // ---- edges ----
    glBindVertexArray(m_edgeVao);
    bufferData(GL_ARRAY_BUFFER, m_buffers[EdgePos], d.edgePositions);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    bufferData(GL_ARRAY_BUFFER, m_edgeIdBuffer, d.edgeIds);
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, 0, nullptr);
    m_edgeVertexCount = static_cast<GLsizei>(d.edgeVertexCount());

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_faceRanges = d.faceRanges;
    m_edgeRanges = d.edgeRanges;
    m_bounds = d.bounds;
}

void GpuMesh::drawFaces() const
{
    if (!m_indexCount)
        return;
    glBindVertexArray(m_faceVao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
}

void GpuMesh::drawFace(int faceIndex) const
{
    if (faceIndex < 1 || faceIndex > faceCount())
        return;
    const IndexRange& r = m_faceRanges[static_cast<std::size_t>(faceIndex - 1)];
    if (!r.count)
        return;
    glBindVertexArray(m_faceVao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(r.count), GL_UNSIGNED_INT,
                   reinterpret_cast<const void*>(static_cast<std::uintptr_t>(r.first) * sizeof(std::uint32_t)));
}

void GpuMesh::drawEdges() const
{
    if (!m_edgeVertexCount)
        return;
    glBindVertexArray(m_edgeVao);
    glDrawArrays(GL_LINES, 0, m_edgeVertexCount);
}

void GpuMesh::drawEdge(int edgeIndex) const
{
    if (edgeIndex < 1 || edgeIndex > edgeCount())
        return;
    const IndexRange& r = m_edgeRanges[static_cast<std::size_t>(edgeIndex - 1)];
    if (!r.count)
        return;
    glBindVertexArray(m_edgeVao);
    glDrawArrays(GL_LINES, static_cast<GLint>(r.first), static_cast<GLsizei>(r.count));
}

} // namespace cf::render
