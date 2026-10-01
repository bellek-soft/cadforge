#pragma once
// geom-internal: the OCCT side of cf::geom::Shape. Only include from src/geom
// (and, later, from kernel-level modules such as FEA meshing).

#include "geom/Shape.h"

#include <Standard_Failure.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Shape.hxx>

#include <string>
#include <utility>

namespace cf::geom {

struct Shape::Impl {
    TopoDS_Shape shape;
    using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
    ShapeMap faces;
    ShapeMap edges;
};

/// Wraps a TopoDS_Shape and builds its face/edge index maps.
Shape fromOcct(const TopoDS_Shape& s);

/// Access the underlying OCCT shape (null shape for an empty handle).
const TopoDS_Shape& toOcct(const Shape& s);

std::string describe(const Standard_Failure& e);

/// Runs `f`, converting OCCT exceptions into GeomError with context.
template <typename F>
auto guarded(const char* what, F&& f) -> decltype(f())
{
    try {
        return f();
    } catch (const GeomError&) {
        throw;
    } catch (const Standard_Failure& e) {
        throw GeomError(std::string(what) + ": " + describe(e));
    } catch (const std::exception& e) {
        throw GeomError(std::string(what) + ": " + e.what());
    }
}

} // namespace cf::geom
