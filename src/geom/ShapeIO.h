#pragma once
// Exchange formats. Paths are UTF-8.

#include "geom/Shape.h"

#include <string>
#include <vector>

namespace cf::geom {

Shape importStep(const std::string& path);
void exportStep(const std::vector<Shape>& shapes, const std::string& path);

/// Binary STL. deflection <= 0 picks a value relative to the model size.
void exportStl(const std::vector<Shape>& shapes, const std::string& path, double deflection = 0.0);

/// Native OCCT text format; handy for debugging and for embedding geometry.
Shape importBrep(const std::string& path);
void exportBrep(const Shape& shape, const std::string& path);

} // namespace cf::geom
