#include "fea/MeshUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_map>

namespace cf::fea::detail {

namespace {

std::uint64_t cellKey(std::int64_t x, std::int64_t y, std::int64_t z)
{
    // 21 bits per axis is plenty for the grids used here.
    const auto m = [](std::int64_t v) { return std::uint64_t(v + (1 << 20)) & 0x1FFFFF; };
    return (m(x) << 42) | (m(y) << 21) | m(z);
}

std::uint64_t edgeKey(int a, int b)
{
    if (a > b)
        std::swap(a, b);
    return (std::uint64_t(std::uint32_t(a)) << 32) | std::uint32_t(b);
}

} // namespace

// ---- welding -----------------------------------------------------------------

WeldedSurface weld(const MeshData& surface)
{
    WeldedSurface out;
    out.bounds = surface.bounds;
    const double tol = std::max(surface.bounds.diagonal() * 1e-7, 1e-9);
    const double cell = tol * 4.0;

    std::unordered_map<std::uint64_t, std::vector<int>> grid;
    std::vector<int> remap(surface.vertexCount(), -1);

    for (std::size_t i = 0; i < surface.vertexCount(); ++i) {
        const Vec3 p(surface.positions[3 * i], surface.positions[3 * i + 1], surface.positions[3 * i + 2]);
        const auto cx = std::int64_t(std::floor(p.x / cell));
        const auto cy = std::int64_t(std::floor(p.y / cell));
        const auto cz = std::int64_t(std::floor(p.z / cell));
        int found = -1;
        for (int dx = -1; dx <= 1 && found < 0; ++dx)
            for (int dy = -1; dy <= 1 && found < 0; ++dy)
                for (int dz = -1; dz <= 1 && found < 0; ++dz) {
                    auto it = grid.find(cellKey(cx + dx, cy + dy, cz + dz));
                    if (it == grid.end())
                        continue;
                    for (int j : it->second)
                        if (glm::distance(out.points[std::size_t(j)], p) <= tol) {
                            found = j;
                            break;
                        }
                }
        if (found < 0) {
            found = int(out.points.size());
            out.points.push_back(p);
            grid[cellKey(cx, cy, cz)].push_back(found);
        }
        remap[i] = found;
    }

    for (std::size_t t = 0; t + 2 < surface.indices.size(); t += 3) {
        const int a = remap[surface.indices[t]], b = remap[surface.indices[t + 1]], c = remap[surface.indices[t + 2]];
        if (a == b || b == c || a == c)
            continue; // degenerate after welding
        const Vec3& pa = out.points[std::size_t(a)];
        const Vec3& pb = out.points[std::size_t(b)];
        const Vec3& pc = out.points[std::size_t(c)];
        if (glm::length(glm::cross(pb - pa, pc - pa)) < tol * tol)
            continue; // zero area
        out.triangles.push_back({a, b, c});
        out.triangleFace.push_back(int(surface.faceIds[surface.indices[t]]));
    }
    return out;
}

std::vector<std::array<int, 2>> featureEdges(const WeldedSurface& s)
{
    struct Info {
        int a, b;
        int count = 0;
        int face = 0;
        bool sharp = false;
    };
    std::unordered_map<std::uint64_t, Info> edges;
    for (std::size_t t = 0; t < s.triangles.size(); ++t) {
        const auto& tri = s.triangles[t];
        for (int k = 0; k < 3; ++k) {
            const int a = tri[std::size_t(k)], b = tri[std::size_t((k + 1) % 3)];
            Info& e = edges.try_emplace(edgeKey(a, b), Info{a, b}).first->second;
            if (e.count > 0 && e.face != s.triangleFace[t])
                e.sharp = true;
            e.face = s.triangleFace[t];
            ++e.count;
        }
    }
    std::vector<std::array<int, 2>> out;
    for (const auto& [key, e] : edges)
        if (e.sharp || e.count != 2)
            out.push_back({e.a, e.b});
    std::sort(out.begin(), out.end());
    return out;
}

int openEdgeCount(const WeldedSurface& s)
{
    std::unordered_map<std::uint64_t, int> count;
    for (const auto& tri : s.triangles)
        for (int k = 0; k < 3; ++k)
            ++count[edgeKey(tri[std::size_t(k)], tri[std::size_t((k + 1) % 3)])];
    int open = 0;
    for (const auto& [k, c] : count)
        open += (c == 1);
    return open;
}

// ---- classification ------------------------------------------------------------

double pointTriangleDistanceSq(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
{
    // Ericson, "Real-Time Collision Detection", 5.1.5
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    Vec3 q;
    if (d1 <= 0 && d2 <= 0) {
        q = a;
    } else {
        const Vec3 bp = p - b;
        const double d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
        const Vec3 cp = p - c;
        const double d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
        const double vc = d1 * d4 - d3 * d2;
        const double vb = d5 * d2 - d1 * d6;
        const double va = d3 * d6 - d5 * d4;
        if (d3 >= 0 && d4 <= d3)
            q = b;
        else if (vc <= 0 && d1 >= 0 && d3 <= 0)
            q = a + ab * (d1 / (d1 - d3));
        else if (d6 >= 0 && d5 <= d6)
            q = c;
        else if (vb <= 0 && d2 >= 0 && d6 <= 0)
            q = a + ac * (d2 / (d2 - d6));
        else if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
            q = b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        else {
            const double denom = 1.0 / (va + vb + vc);
            q = a + ab * (vb * denom) + ac * (vc * denom);
        }
    }
    const Vec3 d = p - q;
    return glm::dot(d, d);
}

struct FaceClassifier::Impl {
    const WeldedSurface& s;
    double cell = 1.0;
    Vec3 origin{0.0};
    std::array<std::int64_t, 3> dims{1, 1, 1};
    std::unordered_map<std::uint64_t, std::vector<int>> grid;

    explicit Impl(const WeldedSurface& surf) : s(surf)
    {
        const BoundingBox& bb = s.bounds;
        const Vec3 size = bb.valid() ? bb.size() : Vec3(1.0);
        // ~ 40 cells along the largest dimension.
        cell = std::max({size.x, size.y, size.z}) / 40.0;
        if (!(cell > 0))
            cell = 1.0;
        origin = bb.valid() ? bb.min : Vec3(0.0);
        for (int i = 0; i < 3; ++i)
            dims[std::size_t(i)] = std::max<std::int64_t>(1, std::int64_t(std::ceil(size[i] / cell)) + 1);

        for (std::size_t t = 0; t < s.triangles.size(); ++t) {
            Vec3 lo(1e300), hi(-1e300);
            for (int k : s.triangles[t]) {
                lo = glm::min(lo, s.points[std::size_t(k)]);
                hi = glm::max(hi, s.points[std::size_t(k)]);
            }
            const auto c0 = toCell(lo), c1 = toCell(hi);
            for (auto x = c0[0]; x <= c1[0]; ++x)
                for (auto y = c0[1]; y <= c1[1]; ++y)
                    for (auto z = c0[2]; z <= c1[2]; ++z)
                        grid[cellKey(x, y, z)].push_back(int(t));
        }
    }

    std::array<std::int64_t, 3> toCell(const Vec3& p) const
    {
        std::array<std::int64_t, 3> c{};
        for (int i = 0; i < 3; ++i)
            c[std::size_t(i)] = std::clamp<std::int64_t>(std::int64_t(std::floor((p[i] - origin[i]) / cell)), 0,
                                                        dims[std::size_t(i)] - 1);
        return c;
    }

    int classify(const Vec3& p) const
    {
        const auto c = toCell(p);
        double best = std::numeric_limits<double>::max();
        int bestFace = 0;
        const std::int64_t maxRing = std::max({dims[0], dims[1], dims[2]});
        for (std::int64_t r = 0; r <= maxRing; ++r) {
            // Cells at Chebyshev distance r cannot contain anything closer than (r-1)*cell.
            if (bestFace != 0 && double(r - 1) * cell > std::sqrt(best))
                break;
            for (std::int64_t x = c[0] - r; x <= c[0] + r; ++x)
                for (std::int64_t y = c[1] - r; y <= c[1] + r; ++y)
                    for (std::int64_t z = c[2] - r; z <= c[2] + r; ++z) {
                        if (std::max({std::abs(x - c[0]), std::abs(y - c[1]), std::abs(z - c[2])}) != r)
                            continue;
                        auto it = grid.find(cellKey(x, y, z));
                        if (it == grid.end())
                            continue;
                        for (int t : it->second) {
                            const auto& tri = s.triangles[std::size_t(t)];
                            const double d = pointTriangleDistanceSq(p, s.points[std::size_t(tri[0])],
                                                                     s.points[std::size_t(tri[1])],
                                                                     s.points[std::size_t(tri[2])]);
                            if (d < best) {
                                best = d;
                                bestFace = s.triangleFace[std::size_t(t)];
                            }
                        }
                    }
        }
        return bestFace;
    }
};

FaceClassifier::FaceClassifier(const WeldedSurface& s) : m(std::make_unique<Impl>(s)) {}
FaceClassifier::~FaceClassifier() = default;
int FaceClassifier::classify(const Vec3& p) const { return m->classify(p); }

// ---- canonical ordering ---------------------------------------------------------------

namespace {

/// Assigns mid-edge nodes to the canonical edges by proximity to the edge midpoints.
template <std::size_t NE>
void matchMidNodes(const std::vector<Vec3>& nodes, const int* corners,
                   const std::array<std::array<int, 2>, NE>& edges, int* mids)
{
    std::array<int, NE> in{};
    std::copy(mids, mids + NE, in.begin());
    std::array<bool, NE> used{};
    for (std::size_t e = 0; e < NE; ++e) {
        const Vec3 m = 0.5 * (nodes[std::size_t(corners[edges[e][0]])] + nodes[std::size_t(corners[edges[e][1]])]);
        double best = std::numeric_limits<double>::max();
        std::size_t bi = 0;
        for (std::size_t k = 0; k < NE; ++k) {
            if (used[k])
                continue;
            const Vec3 d = nodes[std::size_t(in[k])] - m;
            const double dd = glm::dot(d, d);
            if (dd < best) {
                best = dd;
                bi = k;
            }
        }
        used[bi] = true;
        mids[e] = in[bi];
    }
}

constexpr std::array<std::array<int, 2>, 6> kTetEdges = {{{0, 1}, {1, 2}, {2, 0}, {0, 3}, {1, 3}, {2, 3}}};
constexpr std::array<std::array<int, 2>, 3> kTriEdges = {{{0, 1}, {1, 2}, {2, 0}}};

using FaceKey = std::array<int, 3>;
struct FaceKeyHash {
    std::size_t operator()(const FaceKey& k) const
    {
        std::uint64_t h = 1469598103934665603ull;
        for (int v : k)
            h = (h ^ std::uint64_t(std::uint32_t(v))) * 1099511628211ull;
        return std::size_t(h);
    }
};

FaceKey faceKey(int a, int b, int c)
{
    FaceKey v{a, b, c};
    std::sort(v.begin(), v.end());
    return v;
}

} // namespace

void canonicalize(VolumeMesh& mesh)
{
    const std::size_t ne = std::size_t(mesh.nodesPerElement);
    const bool quadTet = ne == 10;

    // Tetrahedra: positive orientation, canonical mid-edge nodes.
    std::unordered_map<FaceKey, int, FaceKeyHash> faceOpposite; // face -> opposite corner node
    for (std::size_t e = 0; e + ne <= mesh.elements.size(); e += ne) {
        int* el = &mesh.elements[e];
        const Vec3 &a = mesh.nodes[std::size_t(el[0])], &b = mesh.nodes[std::size_t(el[1])],
                   &c = mesh.nodes[std::size_t(el[2])], &d = mesh.nodes[std::size_t(el[3])];
        if (glm::dot(b - a, glm::cross(c - a, d - a)) < 0)
            std::swap(el[1], el[2]);
        if (quadTet)
            matchMidNodes(mesh.nodes, el, kTetEdges, el + 4);
        faceOpposite[faceKey(el[0], el[1], el[2])] = el[3];
        faceOpposite[faceKey(el[0], el[1], el[3])] = el[2];
        faceOpposite[faceKey(el[0], el[2], el[3])] = el[1];
        faceOpposite[faceKey(el[1], el[2], el[3])] = el[0];
    }

    // Boundary triangles: outward normal, canonical mid-edge nodes.
    const std::size_t nf = std::size_t(mesh.nodesPerFace);
    for (std::size_t f = 0; f + nf <= mesh.boundaryFaces.size(); f += nf) {
        int* t = &mesh.boundaryFaces[f];
        auto it = faceOpposite.find(faceKey(t[0], t[1], t[2]));
        if (it != faceOpposite.end()) {
            const Vec3 &a = mesh.nodes[std::size_t(t[0])], &b = mesh.nodes[std::size_t(t[1])],
                       &c = mesh.nodes[std::size_t(t[2])];
            const Vec3 n = glm::cross(b - a, c - a);
            if (glm::dot(n, mesh.nodes[std::size_t(it->second)] - a) > 0)
                std::swap(t[1], t[2]);
        }
        if (nf == 6)
            matchMidNodes(mesh.nodes, t, kTriEdges, t + 3);
    }
}

} // namespace cf::fea::detail
