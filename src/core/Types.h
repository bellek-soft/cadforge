#pragma once
// Basic value types shared by every layer.

#include <cstdint>
#include <glm/glm.hpp>

namespace cf {

using Vec2 = glm::dvec2;
using Vec3 = glm::dvec3;
using Mat4 = glm::dmat4;

/// Stable identifier of a feature inside a document. 0 means "none".
using FeatureId = std::uint64_t;
inline constexpr FeatureId kNoFeature = 0;

struct Color {
    float r = 0.7f, g = 0.7f, b = 0.7f, a = 1.0f;
    bool operator==(const Color&) const = default;
};

struct BoundingBox {
    Vec3 min{ 1e300};
    Vec3 max{-1e300};

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    void add(const Vec3& p) { min = glm::min(min, p); max = glm::max(max, p); }
    void add(const BoundingBox& b) { if (b.valid()) { add(b.min); add(b.max); } }
    Vec3 center() const { return (min + max) * 0.5; }
    Vec3 size() const { return max - min; }
    double diagonal() const { return valid() ? glm::length(max - min) : 0.0; }
};

/// A right-handed 2D coordinate frame in 3D space (sketch planes).
/// normal = xDir x yDir.
struct PlaneFrame {
    Vec3 origin{0.0};
    Vec3 xDir{1.0, 0.0, 0.0};
    Vec3 yDir{0.0, 1.0, 0.0};

    Vec3 normal() const { return glm::cross(xDir, yDir); }
    Vec3 toWorld(const Vec2& p) const { return origin + xDir * p.x + yDir * p.y; }
    Vec2 toLocal(const Vec3& w) const
    {
        const Vec3 d = w - origin;
        return {glm::dot(d, xDir), glm::dot(d, yDir)};
    }
};

} // namespace cf
