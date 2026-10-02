#include "fea/Post.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
    return out;
}

} // namespace cf::fea
