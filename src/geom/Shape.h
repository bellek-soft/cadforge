#pragma once
// Opaque, immutable handle to a B-Rep shape.
//
// This is the ONLY geometry type the rest of the application sees. OpenCASCADE
// headers are confined to src/geom/*.cpp (and OcctShape.h for geom-internal
// use), which keeps compile times sane and lets us swap or extend the kernel.
// Shapes are cheap to copy (shared, immutable implementation) and therefore
// safe to cache and to share between features.

#include "core/Types.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace cf::geom {

/// Thrown by every geometry operation that fails (wraps OCCT exceptions).
class GeomError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Shape {
public:
    struct Impl; // defined in OcctShape.h

    Shape() = default;
    explicit Shape(std::shared_ptr<const Impl> impl) : m_impl(std::move(impl)) {}

    bool isNull() const;
    explicit operator bool() const { return !isNull(); }

    /// Number of B-Rep faces / edges. Sub-shape indices used across the
    /// application are 1-based indices into these (stable for a given shape).
    int faceCount() const;
    int edgeCount() const;

    BoundingBox bounds() const;
    double volume() const;
    double area() const;
    bool isValid() const;

    /// Returns this shape moved by a rigid transform (shares the geometry).
    Shape transformed(const Mat4& rigid) const;

    const Impl* impl() const { return m_impl.get(); }

private:
    std::shared_ptr<const Impl> m_impl;
};

} // namespace cf::geom
