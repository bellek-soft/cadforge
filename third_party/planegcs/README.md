# PlaneGCS (vendored)

2D geometric constraint solver from FreeCAD's Sketcher workbench
(`src/Mod/Sketcher/App/planegcs`), LGPL-2.1-or-later.

* Upstream: https://github.com/FreeCAD/FreeCAD, commit dc876b473c7a066d75a8e50eab1c9ba47122f506
* Local changes (kept minimal so updates are easy):
  * `#include "../../SketcherGlobal.h"` -> `#include "SketcherGlobal.h"` (local shim, no export macro)
  * Boost removed: `boost/math/constants` (unused) and `boost/graph/graph_concepts` includes dropped,
    Boost.Graph replaced by `CompatGraph.h` (union-find connected components)
  * `#include <cmath>` added to `Geo.h` (was pulled in through Boost)
  * C++20 instead of C++23: `std::unreachable()` -> `CADFORGE_UNREACHABLE()` (defined in `SketcherGlobal.h`),
    `#include <cassert>` added to `Constraints.cpp`, `GCS.cpp` and `SubSystem.cpp` (libc++ does not pull it in)
  * `compat/` provides empty/no-op shims for `FCConfig.h`, `Base/Console.h`, `Base/Tools.h`
