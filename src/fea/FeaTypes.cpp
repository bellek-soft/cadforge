#include "fea/FeaTypes.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace cf::fea {

double VolumeMesh::volume() const
{
    double v = 0.0;
    const std::size_t n = std::size_t(nodesPerElement);
    for (std::size_t e = 0; e + n <= elements.size(); e += n) {
        const Vec3& a = nodes[std::size_t(elements[e + 0])];
        const Vec3& b = nodes[std::size_t(elements[e + 1])];
        const Vec3& c = nodes[std::size_t(elements[e + 2])];
        const Vec3& d = nodes[std::size_t(elements[e + 3])];
        v += std::abs(glm::dot(b - a, glm::cross(c - a, d - a))) / 6.0;
    }
    return v;
}

std::vector<int> VolumeMesh::nodesOnFaces(const std::vector<int>& faces) const
{
    std::unordered_set<int> want(faces.begin(), faces.end());
    std::vector<int> out;
    const std::size_t n = std::size_t(nodesPerFace);
    for (std::size_t t = 0; t < boundaryFaceIds.size(); ++t)
        if (want.count(boundaryFaceIds[t]))
            for (std::size_t k = 0; k < n; ++k)
                out.push_back(boundaryFaces[t * n + k]);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

const std::vector<Material>& materialLibrary()
{
    // Nominal room-temperature values; always verify against your supplier data.
    static const std::vector<Material> lib = {
        {"Steel (structural)", 210000.0, 0.30, 7.85e-9, 250.0},
        {"Stainless steel 304", 193000.0, 0.29, 8.00e-9, 215.0},
        {"Aluminium 6061-T6", 68900.0, 0.33, 2.70e-9, 276.0},
        {"Titanium Ti-6Al-4V", 113800.0, 0.342, 4.43e-9, 880.0},
        {"Cast iron (grey)", 110000.0, 0.26, 7.20e-9, 150.0},
        {"Brass", 100000.0, 0.34, 8.50e-9, 200.0},
        {"ABS", 2200.0, 0.35, 1.04e-9, 40.0},
        {"PLA", 3500.0, 0.36, 1.24e-9, 50.0},
    };
    return lib;
}

} // namespace cf::fea
