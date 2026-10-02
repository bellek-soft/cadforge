#pragma once
// Finite element data types. Units are consistent "mm-N-MPa":
//   length mm, force N, stress / pressure / Young's modulus MPa (N/mm^2),
//   density t/mm^3 (steel = 7.85e-9), acceleration mm/s^2 (g = 9810).
//
// This module depends only on core (no OCCT, no model, no GL): the mesher
// consumes a tessellated surface (MeshData) and everything downstream is
// plain arrays, which keeps it testable and reusable.

#include "core/Types.h"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace cf::fea {

class FeaError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class ElementOrder { Linear = 1, Quadratic = 2 };

/// Tetrahedral volume mesh with its boundary triangles.
///
/// Node ordering (0-based):
///  * Tet4 : corners 0..3
///  * Tet10: corners 0..3, then mid-edge nodes of edges
///           (0,1) (1,2) (2,0) (0,3) (1,3) (2,3)            [Abaqus C3D10 order]
///  * Tri3 : corners 0..2
///  * Tri6 : corners 0..2, then mid-edge nodes (0,1) (1,2) (2,0)
/// Boundary triangles are oriented with outward normals.
struct VolumeMesh {
    int nodesPerElement = 4; // 4 or 10
    int nodesPerFace = 3;    // 3 or 6
    std::vector<Vec3> nodes;
    std::vector<int> elements;        // nodesPerElement entries per tetrahedron
    std::vector<int> boundaryFaces;   // nodesPerFace entries per boundary triangle
    std::vector<int> boundaryFaceIds; // per boundary triangle: 1-based B-Rep face (0 = unknown)

    ElementOrder order() const { return nodesPerElement == 10 ? ElementOrder::Quadratic : ElementOrder::Linear; }
    std::size_t nodeCount() const { return nodes.size(); }
    std::size_t elementCount() const { return nodesPerElement ? elements.size() / std::size_t(nodesPerElement) : 0; }
    std::size_t boundaryFaceCount() const { return nodesPerFace ? boundaryFaces.size() / std::size_t(nodesPerFace) : 0; }
    bool empty() const { return elements.empty(); }

    /// Total volume (sum of corner-tetrahedron volumes).
    double volume() const;
    /// Nodes lying on the given B-Rep faces (sorted, unique).
    std::vector<int> nodesOnFaces(const std::vector<int>& faces) const;
};

struct Material {
    std::string name = "Steel (structural)";
    double youngsModulus = 210000.0; // MPa
    double poissonRatio = 0.30;
    double density = 7.85e-9;        // t/mm^3
    double yieldStrength = 250.0;    // MPa
};

/// A few typical engineering materials (nominal values, for preliminary analysis).
const std::vector<Material>& materialLibrary();

// ---- boundary conditions & loads --------------------------------------------

struct FixedSupport {
    std::vector<int> faces; // 1-based B-Rep faces
    bool fixX = true, fixY = true, fixZ = true;
};

/// Total force (N) distributed over the faces proportionally to area.
struct ForceLoad {
    std::vector<int> faces;
    Vec3 force{0.0};
};

/// Uniform pressure (MPa) acting along the inward surface normal (positive pushes on the surface).
struct PressureLoad {
    std::vector<int> faces;
    double pressure = 0.0;
};

struct StaticSetup {
    Material material;
    std::vector<FixedSupport> supports;
    std::vector<ForceLoad> forces;
    std::vector<PressureLoad> pressures;
    bool gravity = false;
    Vec3 gravityAcceleration{0.0, 0.0, -9810.0}; // mm/s^2
};

struct StaticResult {
    std::vector<Vec3> displacement;            // per node, mm
    std::vector<std::array<double, 6>> stress; // per node (xx, yy, zz, xy, yz, zx), MPa, nodal average
    std::vector<double> vonMises;              // per node, MPa

    double maxDisplacement = 0.0;
    double maxVonMises = 0.0;
    Vec3 appliedLoad{0.0};  // sum of external forces, N
    Vec3 reaction{0.0};     // sum of support reactions, N (should balance appliedLoad)
    int equations = 0;      // free degrees of freedom
    std::string solver;     // which linear solver was used
    double assemblySeconds = 0.0;
    double solveSeconds = 0.0;
};

} // namespace cf::fea
