#include "core/Placement.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <cmath>

namespace cf {

Mat4 Placement::matrix() const
{
    const Vec3 r = glm::radians(rotationDeg);
    Mat4 m = glm::eulerAngleXYZ(r.x, r.y, r.z);
    m[3] = glm::dvec4(position, 1.0);
    return m;
}

bool Placement::isIdentity() const
{
    return position == Vec3(0.0) && rotationDeg == Vec3(0.0);
}

Placement Placement::fromMatrix(const Mat4& m)
{
    Placement p;
    p.position = Vec3(m[3]);
    // Remove any scale that numerical drift (or a gizmo) may have introduced.
    Mat4 r(1.0);
    for (int i = 0; i < 3; ++i)
        r[i] = glm::dvec4(glm::normalize(Vec3(m[i])), 0.0);
    double a = 0, b = 0, c = 0;
    glm::extractEulerAngleXYZ(r, a, b, c);
    p.rotationDeg = glm::degrees(Vec3(a, b, c));
    // Snap tiny values to zero to keep property panels clean.
    for (int i = 0; i < 3; ++i) {
        if (std::abs(p.rotationDeg[i]) < 1e-9) p.rotationDeg[i] = 0.0;
        if (std::abs(p.position[i]) < 1e-12) p.position[i] = 0.0;
    }
    return p;
}

} // namespace cf
