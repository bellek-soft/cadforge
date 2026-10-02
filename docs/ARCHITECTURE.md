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
            +-------------+-------------v---------+            |
  geom      | Shape (opaque)  Primitives  Ops     |            |
            | Profile  Tessellator  ShapeIO [OCCT]|            |
            +---------------------------+---------+            |
  sketch    | Sketch (geometry, constraints)  solve() [PlaneGCS]|  (core only)
            +---------------------------+---------+            |
  fea       | Mesher [Netgen]  Solver [Eigen, AMGCL]  Post     |  (core only)
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
* Data that does not fit into properties (sketch geometry and constraints) is stored by the feature itself
  through `Feature::saveData` / `loadData` under `"data"` in the file and the undo snapshots, and is part of
  the cache key via `cacheSalt()`.
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

### Topological naming (implemented in v0.4)

Faces and edges are referenced by `IndexList` properties (fillet / chamfer edges, faces of supports and loads).
Indices alone break as soon as an upstream change alters the topology (e.g. another hole renumbers every edge).
Each reference therefore also stores a **geometric signature** of what it pointed to (`geom::SubShapeSignature`:
surface / curve type, centroid, normal or axis, radius, area / length), taken in the local frame of the
referenced feature so that moving it changes nothing.

Before a feature is evaluated, `Document::resolveSubShapeRefs` checks its references against the current
result of `Feature::subShapeTarget()`:

1. unchanged target and known indices -> nothing to do (cheap);
2. the stored index still has the same signature -> keep it;
3. otherwise the best match is searched (same geometry type required; distance = centroid shift relative to the
   model size + axis deviation + radius change + size ratio). Matches below a threshold replace the index (logged),
   and the signature is refreshed so gradual edits are followed;
4. nothing similar any more -> the feature goes into the error state with "re-pick" advice. The old signature
   is kept, so the reference recovers if the geometry comes back (e.g. undo of the upstream change).

New or re-picked indices simply capture fresh signatures. References are stored in the file under `"refs"`.
Limitations: a heuristic, not history-based; symmetric look-alike faces that move far in one step may be
confused. A future history layer (`BRepTools_History` of booleans / fillets) can plug into the same place.

### Sketcher (implemented in v0.3)

```
 sketch (core only)                 model                              app
 ------------------------------     ---------------------------------  ---------------------------------
 Sketch: Geometry (point, line,     SketchFeature: plane + offset,     SketchEditor: working copy, local
   circle, arc; construction)         sketch data (saveData/loadData)    undo, tools, auto-constraints,
 Constraint: refs (geo, pos)          execute(): solve -> 3D edges       constraint-from-selection, drag
   + value; external origin/axes    Extrude / Revolve (ProfileFeature) ui/SketchUi: overlay drawing,
 solve(): PlaneGCS (DogLeg -> LM      profile faces (geom::Profile)      screen-space picking, dimension
   -> BFGS), DOF, conflicting /       + boolean with a target body       labels, panel, shortcuts
   redundant / malformed, drag
```

* **Solver:** every solve builds a fresh `GCS::System`; constraint *i* gets tag *i+1* so PlaneGCS diagnostics map
  back to constraint indices. Redundant constraints make the Jacobian singular, so the sketch is re-solved without
  them and they are reported. Dragging adds temporary low-priority constraints (tag -1). Conflicting or failed
  solves leave the geometry untouched.
* **Profiles:** `geom::makePlanarFaces` connects the sketch edges into wires (`BOPAlgo_Tools::EdgesToWires`),
  rejects open / self-intersecting loops, sorts loops by area and nests them by point classification: even
  depth = outer boundary, odd depth = hole (islands inside holes become solids again).
* **Editing in place:** the camera turns orthographically to the sketch plane; the sketch is drawn as an ImGui
  overlay (no GPU round trip), picking is done in screen space (points before edges, labels first). Each edit is
  solved immediately; edits that would conflict or add redundancy are refused. The document gets a single undo step
  when the sketch is closed; the sketch editor has its own undo stack meanwhile.

### CSG (beyond v0.1)

* Sweep / Loft features -> `geom` wrappers around `BRepOffsetAPI_*`; sketches on faces of solids.
* Mirror / linear and polar patterns, shell, draft.
* **Topological naming (implemented in v0.4):** see the section above.
* Asynchronous recompute (worker thread + cancellation) for heavy models.

### FEA (implemented in v0.2 — linear static)

```
 model (data)                         app (runtime)                     fea (engine, core only)
 ---------------------------------    -------------------------------   -------------------------------
 StaticAnalysisFeature  target,       FeaController                     generateVolumeMesh(MeshData)
   material, mesh size/order,   --->    builds StaticSetup + surface --->   weld -> Netgen STL -> tets
   gravity                              runs mesh/solve on a thread          classify boundary tris
 FixedSupport / Force / Pressure        keeps results in memory              to CAD faces
   (nested under the analysis,          keyed by content hashes         solveStatic(VolumeMesh, setup)
    faces = CAD face indices)           -> "outdated" detection           Tet4/Tet10, D, loads,
 buildStaticSetup(doc, analysis)      SceneView shows mesh / contours     direct LDL^T or AMG+CG
                                      AnalysisUi: panel, glyphs, legend  resultSurface(): MeshData
```

* **Meshing:** the solid is tessellated finely, welded into a watertight surface and given to Netgen as
  STL geometry; every CAD face boundary is passed as a feature edge, so each boundary triangle of the
  tetrahedral mesh lies on exactly one CAD face. Triangles are mapped back to their CAD face (nearest
  original triangle), which is how supports and loads defined on faces reach the mesh. Element
  connectivity is canonicalized (positive tets, outward boundary triangles, Abaqus-style Tet10 order).
* **Solver:** isotropic linear elasticity, Tet4 (1-point) and Tet10 (4-point Gauss) elements, consistent
  surface and body loads. Only the lower triangle of the free-DOF stiffness matrix is stored. Small systems
  use Eigen's sparse LDL^T; larger ones CG preconditioned by AMGCL smoothed-aggregation AMG with the six
  rigid body modes as near-null space (≈20-30 iterations independent of size). A rank check of the rigid
  body modes on the fixed DOFs rejects under-constrained models before solving.
* **Post-processing:** nodal stresses are averaged from element nodes; von Mises, displacement magnitude and
  components are shown as contours on the (optionally deformed) boundary surface; reactions are computed from
  the internal forces and shown next to the applied load as an equilibrium check.
* **Validation (unit tests):** uniform tension (exact for Tet4/Tet10), Tet10 cantilever vs. beam theory
  (0.1903 vs 0.1905 mm), gravity + pressure equilibrium, under-constrained detection for both solver paths.

Next steps for FEA:

* Results export (VTK / CSV), clipping plane and probe tool.
* Remote / bonded contacts between bodies, assemblies.
* Modal analysis (eigenfrequencies) re-using the assembly code (needs a sparse eigen solver, e.g. Spectra).
* Local mesh refinement on selected faces; mesh quality report.
* Multithreaded assembly and stress recovery.
