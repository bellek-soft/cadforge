# CadForge architecture

## 1. Layers

```
            +-----------------------------------------------------------+
  app       |  Application (GLFW, main loop)  AppContext (state+commands) |
            |  ui/: ModelTree, Properties, Viewport, Console, Dialogs     |
            +---------------------------+-------------------------------+
                                        | uses
            +---------------------------v---------+   +-----------------+
  model     | Document  Feature  PropertySet      |   | render          |
            | FeatureRegistry  History (undo)     |   | Renderer Camera |
            | features/PartFeatures               |   | GpuMesh Shaders |
            +---------------------------+---------+   +--------+--------+
                                        |                      |
            +---------------------------v---------+            |
  geom      | Shape (opaque)  Primitives  Ops     |            |
            | Tessellator  ShapeIO   [OCCT here]  |            |
            +---------------------------+---------+            |
                                        |                      |
            +---------------------------v----------------------v--------+
  core      | Types (Vec3, BoundingBox, Color)  MeshData  Placement  Log |
            +-----------------------------------------------------------+
```

Rules that keep the code base healthy as it grows:

* **OpenCASCADE never leaks.** Only `src/geom/*.cpp` include OCCT headers. Everyone else uses the
  opaque, immutable, cheaply copyable `geom::Shape`. (Kernel-level modules such as a future FEA mesher may
  include `geom/OcctShape.h`.)
* **The model knows nothing about rendering or UI.** It can be driven by tests, a batch tool or a future
  Python binding.
* **The renderer knows nothing about features.** It draws `DrawItem`s that reference `GpuMesh`es.
  `app/SceneView` is the bridge (tessellation cache + draw-list building).
* **Panels are thin.** They read `AppContext` and call its commands; all editing logic lives in
  `AppContext` / `Commands`.

## 2. The parametric model

* A `Feature` owns typed `Property`s (double, int, bool, vec3, enum, string, file path, feature reference,
  feature reference list, index list). Generic code works from these descriptions:
  * the property editor UI is generated,
  * JSON (de)serialization is automatic,
  * dependencies are discovered from reference properties,
  * cache keys are computed from property values.
* Every feature has a `Placement` (position + XYZ Euler rotation) applied after `execute()`.
* `Document::recompute()` topologically sorts features, detects cycles, and for each feature computes a
  content hash of *type + parameters + input result keys*. Unchanged features are skipped; known results
  come from a bounded shape cache. Placement-only edits do not re-execute the feature at all
  (`Shape::transformed` shares geometry and triangulation), which keeps gizmo dragging interactive.
* Errors (`geom::GeomError`, wrapping OCCT's `Standard_Failure`) put a feature into the `Error` state with a
  message; dependents fail gracefully; nothing crashes.
* `History` stores JSON snapshots (parameters only). Undo = reload the snapshot; thanks to the cache,
  unchanged geometry is not recomputed.

### File format (`.cfp`)

```json
{
  "app": "CadForge", "formatVersion": 1, "units": "mm", "nextId": 12,
  "features": [
    { "id": 1, "type": "Part::Box", "name": "Plate1", "visible": false, "color": [0.6, 0.68, 0.78, 1.0],
      "props": { "position": [0,0,0], "rotation": [0,0,0], "length": 80.0, "width": 50.0, "height": 10.0, "centered": false } },
    { "id": 6, "type": "Part::Boolean", "name": "Bracket1",
      "props": { "operation": 1, "base": 5, "tools": [3, 4], ... } }
  ]
}
```

Geometry is never stored; it is regenerated from parameters. Unknown property keys are ignored, missing ones
get defaults, so the format can evolve. Bump `formatVersion` for breaking changes.

## 3. Rendering

* OpenGL **4.1 core** everywhere (maximum on macOS). Loader: vendored glad.
* Scene is rendered into a 4x MSAA FBO, resolved into a texture that the ImGui "Viewport" window shows.
* Edges are drawn as screen-space quads by a geometry shader (core profile has no wide lines).
* Picking: an `RG32UI` id buffer stores *(object pick id, kind << 28 | face/edge index)*. A small window
  around the cursor is read back, so thin edges are easy to hit.
* Sub-shape highlighting draws per-face index ranges / per-edge vertex ranges again in the highlight color.
* `MeshData::scalars` + the colormap in the mesh shader already support per-vertex result fields
  (needed for FEA results).

## 4. Adding a new feature type

1. Derive from `model::Feature`, declare properties in the constructor, implement `type()`, `typeLabel()`
   and `execute()` using `geom` functions (add new ones to `geom` if needed).
2. Register it in a `registerXxxFeatures(FeatureRegistry&)` function and call that from
   `builtinRegistry()`.
3. That's it: UI, files, undo, caching and dependency handling work automatically. Add a toolbar/menu
   entry if it needs a special creation command (like booleans created from the selection).

## 5. Roadmap

### CSG (beyond v0.1)

* Sketcher (2D constraints) + Extrude / Revolve / Sweep / Loft features -> `geom` wrappers around
  `BRepPrimAPI_MakePrism`, `BRepPrimAPI_MakeRevol`, `BRepOffsetAPI_*`.
* Mirror / linear and polar patterns, shell, draft.
* **Topological naming:** fillet edges are stored as 1-based edge indices of the input shape. They stay
  valid while the input's topology does not change. A robust solution (history-based naming through
  `BRepTools_History` / `BRepAlgoAPI_*::Modified/Generated`, or geometric signatures) is planned and is
  isolated in `EdgeFeature`.
* Asynchronous recompute (worker thread + cancellation) for heavy models.

### FEA

Planned as a new `src/analysis` layer between `geom` and `model`:

1. **Meshing:** tetrahedral volume mesh from a `geom::Shape`. Candidates: Netgen (LGPL, works directly on
   OCCT shapes), Gmsh (GPL - license impact), TetGen (AGPL - license impact). Recommendation: Netgen.
2. **Solver:** linear static elasticity with 4/10-node tetrahedra, sparse assembly with Eigen (MPL-2.0,
   available in vcpkg), CG/Cholesky solvers; later modal analysis.
3. **Model:** `AnalysisFeature` (references a solid), `Material`, boundary conditions (fixed faces, forces,
   pressure) referencing face indices - the same topological-naming mechanism as fillets.
4. **Results:** displacement / von Mises stress per node -> `MeshData::scalars` -> colormap rendering
   (already implemented in the shader), deformed-shape scaling, legend.
5. Long computations run in a background thread with progress reporting in the UI.

Open design question for FEA: whether analysis results live in the document (saved in the project) or
in a separate results file next to it.
