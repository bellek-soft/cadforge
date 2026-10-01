#pragma once
// Rigid placement (translation + rotation) of a feature.
// Rotation is stored as XYZ Euler angles in degrees: M = T * Rx * Ry * Rz.

#include "core/Types.h"

namespace cf {

struct Placement {
    Vec3 position{0.0};
    Vec3 rotationDeg{0.0};

    Mat4 matrix() const;
    bool isIdentity() const;
    static Placement fromMatrix(const Mat4& m);

    bool operator==(const Placement&) const = default;
};

} // namespace cf
