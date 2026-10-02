#include "fea/Post.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numeric>
#include <unordered_map>

namespace cf::fea {

const char* fieldName(ResultField f)
{
    switch (f) {
    case ResultField::None: return "None";
    case ResultField::VonMises: return "Von Mises stress";
    case ResultField::DisplacementMagnitude: return "Displacement";
    case ResultField::DisplacementX: return "Displacement X";
    case ResultField::DisplacementY: return "Displacement Y";
    case ResultField::DisplacementZ: return "Displacement Z";
    }
    return "";
}

const char* fieldUnit(ResultField f)
{
    return f == ResultField::VonMises ? "MPa" : f == ResultField::None ? "" : "mm";
}

std::vector<double> fieldValues(const StaticResult& r, ResultField field)
{
    std::vector<double> v;
    switch (field) {
    case ResultField::None: break;
    case ResultField::VonMises: v = r.vonMises; break;
    case ResultField::DisplacementMagnitude:
        for (const auto& u : r.displacement) v.push_back(glm::length(u));
        break;
    case ResultField::DisplacementX:
        for (const auto& u : r.displacement) v.push_back(u.x);
        break;
    case ResultField::DisplacementY:
        for (const auto& u : r.displacement) v.push_back(u.y);
        break;
    case ResultField::DisplacementZ:
        for (const auto& u : r.displacement) v.push_back(u.z);
        break;
    }
    return v;
}

double automaticDeformationScale(const VolumeMesh& mesh, const StaticResult& result)
{
    if (result.maxDisplacement <= 0)
        return 0.0;
    BoundingBox bb;
    for (const auto& p : mesh.nodes)
        bb.add(p);
    return 0.05 * bb.diagonal() / result.maxDisplacement;
}

glm::vec3 colormap(float t)
{
    const glm::vec3 c[6] = {{0.19f, 0.07f, 0.23f}, {0.16f, 0.47f, 0.93f}, {0.11f, 0.81f, 0.62f},
                            {0.64f, 0.98f, 0.24f}, {0.98f, 0.73f, 0.18f}, {0.80f, 0.18f, 0.04f}};
    t = std::clamp(t, 0.0f, 1.0f) * 5.0f;
    const int i = std::min(int(t), 4);
    return glm::mix(c[i], c[i + 1], t - float(i));
}


namespace {

struct ClipVertex {
    Vec3 p;
    Vec3 n;
    float scalar;
};

/// Removes everything with dot(n, p) > d from the triangles and edges of `m`.
void clipSurface(MeshData& m, const Vec3& n, double d)
{
    const bool hasScalars = !m.scalars.empty();
    auto get = [&](std::uint32_t v) {
        ClipVertex c;
        c.p = Vec3(m.positions[3 * v], m.positions[3 * v + 1], m.positions[3 * v + 2]);
        c.n = Vec3(m.normals[3 * v], m.normals[3 * v + 1], m.normals[3 * v + 2]);
        c.scalar = hasScalars ? m.scalars[v] : 0.0f;
        return c;
    };
    auto add = [&](const ClipVertex& c, std::uint32_t face) {
        m.positions.insert(m.positions.end(), {float(c.p.x), float(c.p.y), float(c.p.z)});
        m.normals.insert(m.normals.end(), {float(c.n.x), float(c.n.y), float(c.n.z)});
        m.faceIds.push_back(face);
        if (hasScalars)
            m.scalars.push_back(c.scalar);
        return std::uint32_t(m.vertexCount() - 1);
    };
    auto lerp = [](const ClipVertex& a, const ClipVertex& b, double t) {
        return ClipVertex{a.p + t * (b.p - a.p), glm::normalize(a.n + t * (b.n - a.n) + Vec3(1e-30)),
                          float(a.scalar + t * (b.scalar - a.scalar))};
    };

    const std::vector<std::uint32_t> oldIdx = std::move(m.indices);
    m.indices.clear();
    std::vector<IndexRange> ranges = m.faceRanges;
    auto clipTriangles = [&](std::uint32_t first, std::uint32_t count) {
        for (std::uint32_t t = first; t + 2 < first + count; t += 3) {
            const std::uint32_t v[3] = {oldIdx[t], oldIdx[t + 1], oldIdx[t + 2]};
            ClipVertex c[3];
            double s[3];
            int inside = 0;
            for (int k = 0; k < 3; ++k) {
                c[k] = get(v[k]);
                s[k] = glm::dot(n, c[k].p) - d;
                inside += s[k] <= 0.0;
            }
            if (inside == 3) {
                m.indices.insert(m.indices.end(), {v[0], v[1], v[2]});
                continue;
            }
            if (inside == 0)
                continue;
            // Sutherland-Hodgman against one plane: at most 4 vertices.
            std::uint32_t poly[4];
            int np = 0;
            const std::uint32_t face = m.faceIds[v[0]];
            for (int k = 0; k < 3; ++k) {
                const int j = (k + 1) % 3;
                if (s[k] <= 0.0)
                    poly[np++] = v[k];
                if ((s[k] <= 0.0) != (s[j] <= 0.0))
                    poly[np++] = add(lerp(c[k], c[j], s[k] / (s[k] - s[j])), face);
            }
            for (int k = 1; k + 1 < np; ++k)
                m.indices.insert(m.indices.end(), {poly[0], poly[k], poly[k + 1]});
        }
    };
    // Keep each face's triangles contiguous; triangles outside any range (id 0) go last.
    std::vector<char> covered(oldIdx.size() / 3, 0);
    for (auto& r : ranges) {
        const std::uint32_t first = r.first, count = r.count;
        for (std::uint32_t t = first; t < first + count; t += 3)
            covered[t / 3] = 1;
        r.first = std::uint32_t(m.indices.size());
        clipTriangles(first, count);
        r.count = std::uint32_t(m.indices.size()) - r.first;
    }
    for (std::uint32_t t = 0; t < covered.size(); ++t)
        if (!covered[t])
            clipTriangles(3 * t, 3);
    m.faceRanges = std::move(ranges);

    // Edges: plain segment clipping.
    std::vector<float> ep;
    std::vector<std::uint32_t> eid;
    for (std::size_t i = 0; i + 1 < m.edgeVertexCount(); i += 2) {
        Vec3 a(m.edgePositions[3 * i], m.edgePositions[3 * i + 1], m.edgePositions[3 * i + 2]);
        Vec3 b(m.edgePositions[3 * i + 3], m.edgePositions[3 * i + 4], m.edgePositions[3 * i + 5]);
        const double sa = glm::dot(n, a) - d, sb = glm::dot(n, b) - d;
        if (sa > 0.0 && sb > 0.0)
            continue;
        if (sa > 0.0)
            a = a + (sa / (sa - sb)) * (b - a);
        else if (sb > 0.0)
            b = a + (sa / (sa - sb)) * (b - a);
        ep.insert(ep.end(), {float(a.x), float(a.y), float(a.z), float(b.x), float(b.y), float(b.z)});
        eid.push_back(m.edgeIds[i]);
        eid.push_back(m.edgeIds[i + 1]);
    }
    m.edgePositions = std::move(ep);
    m.edgeIds = std::move(eid);
    m.edgeRanges.clear(); // FEA surfaces have no pickable edges
}

/// Appends the cut of the volume mesh with the plane dot(n, p) = d (cap triangles
/// facing +n, face id 0) and, optionally, the cut element edges.
void appendSectionCap(MeshData& m, const VolumeMesh& mesh, const std::vector<double>& values,
                      const std::function<Vec3(int)>& position, const Vec3& n, double d, bool edges)
{
    const bool hasScalars = !values.empty();
    // Tet10 is split into 8 linear sub-tets through its mid-edge nodes, so the cap
    // matches the (subdivided) boundary display.
    static const int split10[8][4] = {{0, 4, 6, 7}, {4, 1, 5, 8}, {6, 5, 2, 9}, {7, 8, 9, 3},
                                      {4, 5, 6, 8}, {4, 6, 7, 8}, {6, 5, 9, 8}, {6, 9, 7, 8}};
    static const int split4[1][4] = {{0, 1, 2, 3}};
    const int nen = mesh.nodesPerElement;
    const auto subs = nen == 10 ? &split10[0] : &split4[0];
    const int nSub = nen == 10 ? 8 : 1;
    const float nf[3] = {float(n.x), float(n.y), float(n.z)};
    // The cap is moved a hair towards the removed side so it does not z-fight with clipped faces.
    const double eps = 1e-6 * std::max(m.bounds.diagonal(), 1.0);

    auto addVertex = [&](const Vec3& p, float s) {
        const Vec3 q = p + eps * n;
        m.positions.insert(m.positions.end(), {float(q.x), float(q.y), float(q.z)});
        m.normals.insert(m.normals.end(), {nf[0], nf[1], nf[2]});
        m.faceIds.push_back(0);
        if (hasScalars)
            m.scalars.push_back(s);
        return std::uint32_t(m.vertexCount() - 1);
    };
    static const int tetEdges[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
    for (std::size_t e = 0; e < mesh.elementCount(); ++e) {
        const int* conn = &mesh.elements[e * std::size_t(nen)];
        for (int sIdx = 0; sIdx < nSub; ++sIdx) {
            int node[4];
            Vec3 p[4];
            double s[4];
            int above = 0;
            for (int k = 0; k < 4; ++k) {
                node[k] = conn[subs[sIdx][k]];
                p[k] = position(node[k]);
                s[k] = glm::dot(n, p[k]) - d;
                above += s[k] > 0.0;
            }
            if (above == 0 || above == 4)
                continue;
            Vec3 q[4];
            float qs[4];
            int nq = 0;
            for (const auto& ed : tetEdges) {
                const int a = ed[0], b = ed[1];
                if ((s[a] > 0.0) == (s[b] > 0.0) || nq == 4)
                    continue;
                const double t = s[a] / (s[a] - s[b]);
                q[nq] = p[a] + t * (p[b] - p[a]);
                qs[nq] = hasScalars ? float(values[std::size_t(node[a])] +
                                            t * (values[std::size_t(node[b])] - values[std::size_t(node[a])]))
                                    : 0.0f;
                ++nq;
            }
            if (nq < 3)
                continue;
            // Order the polygon around its centroid (3 or 4 points).
            Vec3 c(0.0);
            for (int k = 0; k < nq; ++k)
                c += q[k];
            c /= double(nq);
            const Vec3 u = glm::normalize(q[0] - c + Vec3(1e-30));
            const Vec3 w = glm::cross(n, u);
            int order[4] = {0, 1, 2, 3};
            double ang[4];
            for (int k = 0; k < nq; ++k)
                ang[k] = std::atan2(glm::dot(q[k] - c, w), glm::dot(q[k] - c, u));
            for (int i = 1; i < nq; ++i) // insertion sort (at most 4 entries)
                for (int j = i; j > 0 && ang[order[j]] < ang[order[j - 1]]; --j)
                    std::swap(order[j], order[j - 1]);
            std::uint32_t vi[4];
            for (int k = 0; k < nq; ++k)
                vi[k] = addVertex(q[order[k]], qs[order[k]]);
            for (int k = 1; k + 1 < nq; ++k)
                m.indices.insert(m.indices.end(), {vi[0], vi[k], vi[k + 1]});
            if (edges)
                for (int k = 0; k < nq; ++k) {
                    const Vec3 a = q[order[k]] + eps * 2.0 * n, b = q[order[(k + 1) % nq]] + eps * 2.0 * n;
                    m.edgePositions.insert(m.edgePositions.end(), {float(a.x), float(a.y), float(a.z), float(b.x),
                                                                   float(b.y), float(b.z)});
                    m.edgeIds.push_back(0);
                    m.edgeIds.push_back(0);
                }
        }
    }
}

} // namespace

bool probeSurface(const MeshData& m, const Vec3& origin, const Vec3& dir, ProbeHit& hit)
{
    bool found = false;
    double best = 1e300;
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const std::uint32_t v[3] = {m.indices[t], m.indices[t + 1], m.indices[t + 2]};
        Vec3 p[3];
        for (int k = 0; k < 3; ++k)
            p[k] = Vec3(m.positions[3 * v[k]], m.positions[3 * v[k] + 1], m.positions[3 * v[k] + 2]);
        // Moller-Trumbore
        const Vec3 e1 = p[1] - p[0], e2 = p[2] - p[0];
        const Vec3 h = glm::cross(dir, e2);
        const double a = glm::dot(e1, h);
        if (std::abs(a) < 1e-14)
            continue;
        const double f = 1.0 / a;
        const Vec3 s = origin - p[0];
        const double u = f * glm::dot(s, h);
        if (u < 0.0 || u > 1.0)
            continue;
        const Vec3 q = glm::cross(s, e1);
        const double w = f * glm::dot(dir, q);
        if (w < 0.0 || u + w > 1.0)
            continue;
        const double dist = f * glm::dot(e2, q);
        if (dist <= 0.0 || dist >= best)
            continue;
        best = dist;
        found = true;
        hit.distance = dist;
        hit.point = origin + dist * dir;
        hit.face = m.faceIds.empty() ? 0 : m.faceIds[v[0]];
        hit.value = m.scalars.empty()
                        ? 0.0f
                        : float((1.0 - u - w) * m.scalars[v[0]] + u * m.scalars[v[1]] + w * m.scalars[v[2]]);
    }
    return found;
}

MeshData resultSurface(const VolumeMesh& mesh, const StaticResult* result, const SurfaceOptions& opt)
{
    MeshData out;
    const std::size_t nf = std::size_t(mesh.nodesPerFace);
    const std::size_t nTri = mesh.boundaryFaceCount();
    const bool hasResult = result && result->displacement.size() == mesh.nodeCount();
    const double scale = hasResult ? opt.deformationScale : 0.0;
    const std::vector<double> values = hasResult ? fieldValues(*result, opt.field) : std::vector<double>{};

    auto position = [&](int n) {
        Vec3 p = mesh.nodes[std::size_t(n)];
        if (scale != 0.0)
            p += scale * result->displacement[std::size_t(n)];
        return p;
    };

    // Sub-triangles of a boundary face (Tri6 is split into 4 for display).
    static const int sub6[4][3] = {{0, 3, 5}, {3, 1, 4}, {5, 4, 2}, {3, 4, 5}};
    static const int sub3[1][3] = {{0, 1, 2}};

    // Group boundary triangles by B-Rep face so each face gets a contiguous index range.
    std::vector<std::size_t> order(nTri);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return mesh.boundaryFaceIds[a] < mesh.boundaryFaceIds[b]; });
    int maxFace = 0;
    for (int id : mesh.boundaryFaceIds)
        maxFace = std::max(maxFace, id);
    out.faceRanges.assign(std::size_t(maxFace), IndexRange{});

    // One vertex per (node, face) pair: smooth inside a CAD face, crisp across faces.
    std::unordered_map<std::uint64_t, std::uint32_t> vertexOf;
    std::vector<Vec3> normalAcc;
    auto vertex = [&](int node, int face) -> std::uint32_t {
        const std::uint64_t key = (std::uint64_t(std::uint32_t(node)) << 32) | std::uint32_t(face);
        auto [it, inserted] = vertexOf.try_emplace(key, std::uint32_t(out.vertexCount()));
        if (inserted) {
            const Vec3 p = position(node);
            out.positions.insert(out.positions.end(), {float(p.x), float(p.y), float(p.z)});
            out.faceIds.push_back(std::uint32_t(std::max(face, 0)));
            out.scalars.push_back(values.empty() ? 0.0f : float(values[std::size_t(node)]));
            normalAcc.emplace_back(0.0);
            out.bounds.add(p);
        }
        return it->second;
    };

    int currentFace = -1;
    for (std::size_t oi = 0; oi < nTri; ++oi) {
        const std::size_t t = order[oi];
        const int face = mesh.boundaryFaceIds[t];
        if (face != currentFace) {
            if (currentFace >= 1)
                out.faceRanges[std::size_t(currentFace - 1)].count =
                    std::uint32_t(out.indices.size()) - out.faceRanges[std::size_t(currentFace - 1)].first;
            currentFace = face;
            if (face >= 1)
                out.faceRanges[std::size_t(face - 1)].first = std::uint32_t(out.indices.size());
        }
        const int* tn = &mesh.boundaryFaces[t * nf];
        const auto subs = nf == 6 ? &sub6[0] : &sub3[0];
        const int nSub = nf == 6 ? 4 : 1;
        for (int s = 0; s < nSub; ++s) {
            std::uint32_t v[3];
            for (int k = 0; k < 3; ++k)
                v[k] = vertex(tn[subs[s][k]], face);
            const Vec3 a = position(tn[subs[s][0]]), b = position(tn[subs[s][1]]), c = position(tn[subs[s][2]]);
            const Vec3 n = glm::cross(b - a, c - a);
            for (auto vi : v) {
                normalAcc[vi] += n;
                out.indices.push_back(vi);
            }
        }
    }
    if (currentFace >= 1)
        out.faceRanges[std::size_t(currentFace - 1)].count =
            std::uint32_t(out.indices.size()) - out.faceRanges[std::size_t(currentFace - 1)].first;

    out.normals.reserve(normalAcc.size() * 3);
    for (const auto& n : normalAcc) {
        const double len = glm::length(n);
        const Vec3 u = len > 0 ? n / len : Vec3(0, 0, 1);
        out.normals.insert(out.normals.end(), {float(u.x), float(u.y), float(u.z)});
    }
    if (values.empty())
        out.scalars.clear();

    // Wireframe of the boundary element edges (not pickable: edge id 0).
    if (opt.elementEdges) {
        std::unordered_map<std::uint64_t, char> seen;
        auto addSeg = [&](int a, int b) {
            const Vec3 pa = position(a), pb = position(b);
            out.edgePositions.insert(out.edgePositions.end(),
                                     {float(pa.x), float(pa.y), float(pa.z), float(pb.x), float(pb.y), float(pb.z)});
            out.edgeIds.push_back(0);
            out.edgeIds.push_back(0);
        };
        static const int e3[3][2] = {{0, 1}, {1, 2}, {2, 0}};
        for (std::size_t t = 0; t < nTri; ++t) {
            const int* tn = &mesh.boundaryFaces[t * nf];
            for (int k = 0; k < 3; ++k) {
                int a = tn[e3[k][0]], b = tn[e3[k][1]];
                const std::uint64_t key = (std::uint64_t(std::uint32_t(std::min(a, b))) << 32) | std::uint32_t(std::max(a, b));
                if (!seen.emplace(key, 1).second)
                    continue;
                if (nf == 6) {
                    const int m = tn[3 + k];
                    addSeg(a, m);
                    addSeg(m, b);
                } else {
                    addSeg(a, b);
                }
            }
        }
    }
    if (opt.section) {
        const Vec3 n = glm::normalize(opt.sectionNormal);
        clipSurface(out, n, opt.sectionOffset);
        appendSectionCap(out, mesh, values, position, n, opt.sectionOffset, opt.elementEdges);
        out.bounds = BoundingBox{};
        for (std::uint32_t v : out.indices)
            out.bounds.add(Vec3(out.positions[3 * v], out.positions[3 * v + 1], out.positions[3 * v + 2]));
    }
    return out;
}

} // namespace cf::fea
