#include "fea/Solver.h"

#include <Eigen/Dense>
#include <Eigen/IterativeLinearSolvers>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>

#include <Spectra/MatOp/SparseSymMatProd.h>
#include <Spectra/SymGEigsShiftSolver.h>

#include <amgcl/adapter/crs_tuple.hpp>
#include <amgcl/amg.hpp>
#include <amgcl/backend/builtin.hpp>
#include <amgcl/coarsening/rigid_body_modes.hpp>
#include <amgcl/coarsening/smoothed_aggregation.hpp>
#include <amgcl/make_solver.hpp>
#include <amgcl/relaxation/ilu0.hpp>
#include <amgcl/solver/cg.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <tuple>

namespace cf::fea {

namespace {

using Clock = std::chrono::steady_clock;
using Mat3 = Eigen::Matrix3d;
using Mat6 = Eigen::Matrix<double, 6, 6>;
using SpMat = Eigen::SparseMatrix<double>; // column major, lower triangle stored

double seconds(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

// ---- element formulation --------------------------------------------------------

/// Natural coordinates (r, s, t) of the element nodes (corners, then canonical mid-edges).
const std::array<Eigen::Vector3d, 10>& nodeNaturalCoords()
{
    static const std::array<Eigen::Vector3d, 10> c = [] {
        std::array<Eigen::Vector3d, 10> v;
        v[0] = {0, 0, 0};
        v[1] = {1, 0, 0};
        v[2] = {0, 1, 0};
        v[3] = {0, 0, 1};
        const int e[6][2] = {{0, 1}, {1, 2}, {2, 0}, {0, 3}, {1, 3}, {2, 3}};
        for (int i = 0; i < 6; ++i)
            v[std::size_t(4 + i)] = 0.5 * (v[std::size_t(e[i][0])] + v[std::size_t(e[i][1])]);
        return v;
    }();
    return c;
}

/// Shape functions N and derivatives dN/d(r,s,t) at a natural point.
void shape(int nen, const Eigen::Vector3d& p, double* N, Eigen::Matrix<double, 10, 3>& dN)
{
    const double L[4] = {1.0 - p.x() - p.y() - p.z(), p.x(), p.y(), p.z()};
    // dL_k / d(r,s,t)
    static const double dL[4][3] = {{-1, -1, -1}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    if (nen == 4) {
        for (int i = 0; i < 4; ++i) {
            N[i] = L[i];
            for (int j = 0; j < 3; ++j)
                dN(i, j) = dL[i][j];
        }
        return;
    }
    for (int i = 0; i < 4; ++i) {
        N[i] = L[i] * (2.0 * L[i] - 1.0);
        for (int j = 0; j < 3; ++j)
            dN(i, j) = (4.0 * L[i] - 1.0) * dL[i][j];
    }
    static const int e[6][2] = {{0, 1}, {1, 2}, {2, 0}, {0, 3}, {1, 3}, {2, 3}};
    for (int m = 0; m < 6; ++m) {
        const int a = e[m][0], b = e[m][1];
        N[4 + m] = 4.0 * L[a] * L[b];
        for (int j = 0; j < 3; ++j)
            dN(4 + m, j) = 4.0 * (L[b] * dL[a][j] + L[a] * dL[b][j]);
    }
}

struct GaussPoint {
    Eigen::Vector3d p;
    double w;
};

const std::vector<GaussPoint>& gaussRule(int nen)
{
    static const std::vector<GaussPoint> one = {{{0.25, 0.25, 0.25}, 1.0 / 6.0}};
    static const std::vector<GaussPoint> four = [] {
        const double a = 0.5854101966249685, b = 0.1381966011250105;
        return std::vector<GaussPoint>{{{b, b, b}, 1.0 / 24.0},
                                       {{a, b, b}, 1.0 / 24.0},
                                       {{b, a, b}, 1.0 / 24.0},
                                       {{b, b, a}, 1.0 / 24.0}};
    }();
    return nen == 4 ? one : four;
}

Mat6 elasticity(const Material& m)
{
    const double E = m.youngsModulus, nu = m.poissonRatio;
    const double lambda = E * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
    const double mu = E / (2.0 * (1.0 + nu));
    Mat6 D = Mat6::Zero();
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            D(i, j) = lambda;
        D(i, i) = lambda + 2.0 * mu;
        D(3 + i, 3 + i) = mu;
    }
    return D;
}

/// Strain-displacement matrix B (6 x 3nen) at a natural point; returns det J.
double bMatrix(const VolumeMesh& mesh, const int* conn, int nen, const Eigen::Vector3d& p,
               Eigen::Matrix<double, 6, 30>& B, double* N)
{
    Eigen::Matrix<double, 10, 3> dN;
    shape(nen, p, N, dN);
    Mat3 J = Mat3::Zero();
    for (int a = 0; a < nen; ++a) {
        const Vec3& x = mesh.nodes[std::size_t(conn[a])];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                J(i, j) += x[i] * dN(a, j);
    }
    const double det = J.determinant();
    if (std::abs(det) < 1e-300)
        throw FeaError("Degenerate element in the mesh");
    const Mat3 Jinv = J.inverse();
    B.setZero();
    for (int a = 0; a < nen; ++a) {
        // dN/dx_i = sum_j dN/dxi_j * dxi_j/dx_i
        const Eigen::RowVector3d g = dN.row(a) * Jinv;
        const int c = 3 * a;
        B(0, c + 0) = g(0);
        B(1, c + 1) = g(1);
        B(2, c + 2) = g(2);
        B(3, c + 0) = g(1);
        B(3, c + 1) = g(0);
        B(4, c + 1) = g(2);
        B(4, c + 2) = g(1);
        B(5, c + 0) = g(2);
        B(5, c + 2) = g(0);
    }
    return det;
}

void elementStiffness(const VolumeMesh& mesh, const int* conn, int nen, const Mat6& D,
                      Eigen::Matrix<double, 30, 30>& Ke)
{
    Ke.setZero();
    Eigen::Matrix<double, 6, 30> B;
    double N[10];
    const int nd = 3 * nen;
    for (const auto& gp : gaussRule(nen)) {
        const double det = bMatrix(mesh, conn, nen, gp.p, B, N);
        const auto Bn = B.leftCols(nd);
        Ke.topLeftCorner(nd, nd).noalias() += (Bn.transpose() * D * Bn) * (std::abs(det) * gp.w);
    }
}

// ---- surface loads ---------------------------------------------------------------

/// Adds a uniform traction (force per area) on a boundary triangle to f.
void addTraction(const VolumeMesh& mesh, std::size_t tri, const Vec3& tractionTimesArea, Eigen::VectorXd& f)
{
    const int* t = &mesh.boundaryFaces[tri * std::size_t(mesh.nodesPerFace)];
    // Consistent nodal loads on a flat triangle: Tri3 -> 1/3 per corner;
    // Tri6 -> 0 at corners, 1/3 at each mid-edge node.
    const int first = mesh.nodesPerFace == 6 ? 3 : 0;
    for (int k = first; k < first + 3; ++k)
        for (int c = 0; c < 3; ++c)
            f(3 * t[k] + c) += tractionTimesArea[c] / 3.0;
}

Vec3 triangleAreaNormal(const VolumeMesh& mesh, std::size_t tri)
{
    const int* t = &mesh.boundaryFaces[tri * std::size_t(mesh.nodesPerFace)];
    const Vec3& a = mesh.nodes[std::size_t(t[0])];
    const Vec3& b = mesh.nodes[std::size_t(t[1])];
    const Vec3& c = mesh.nodes[std::size_t(t[2])];
    return 0.5 * glm::cross(b - a, c - a); // outward normal * area
}


struct Equations {
    std::vector<char> fixed; // per DOF
    std::vector<int> eq;     // DOF -> equation (-1 = fixed)
    int count = 0;
};

/// Fixes the supported DOFs, checks that all rigid body motions are prevented
/// and numbers the free DOFs.
Equations numberEquations(const VolumeMesh& mesh, const std::vector<FixedSupport>& supports)
{
    const std::size_t nNodes = mesh.nodeCount();
    const std::size_t nDof = 3 * nNodes;
    Equations out;
    std::vector<char>& fixed = out.fixed;
    fixed.assign(nDof, 0);
    for (const auto& s : supports) {
        const auto nodes = mesh.nodesOnFaces(s.faces);
        if (nodes.empty())
            throw FeaError("A fixed support references faces that are not part of the mesh");
        for (int n : nodes) {
            if (s.fixX) fixed[std::size_t(3 * n + 0)] = 1;
            if (s.fixY) fixed[std::size_t(3 * n + 1)] = 1;
            if (s.fixZ) fixed[std::size_t(3 * n + 2)] = 1;
        }
    }
    // Nodes not used by any element (should not happen) are fixed to keep K regular.
    {
        std::vector<char> used(nNodes, 0);
        for (int n : mesh.elements)
            used[std::size_t(n)] = 1;
        for (std::size_t n = 0; n < nNodes; ++n)
            if (!used[n])
                fixed[3 * n] = fixed[3 * n + 1] = fixed[3 * n + 2] = 1;
    }
    // The supports must remove all six rigid body motions. Check the rank of the
    // rigid body modes restricted to the fixed DOFs (cheap and solver independent).
    {
        Vec3 center(0.0);
        double radius = 0.0;
        for (const auto& p : mesh.nodes)
            center += p;
        center /= double(nNodes);
        for (const auto& p : mesh.nodes)
            radius = std::max(radius, glm::length(p - center));
        radius = std::max(radius, 1e-9);
        Eigen::Matrix<double, 6, 6> G = Eigen::Matrix<double, 6, 6>::Zero();
        for (std::size_t n = 0; n < nNodes; ++n) {
            const Vec3 r = (mesh.nodes[n] - center) / radius;
            // Rows of the rigid body mode matrix for this node's x, y, z DOFs.
            const double rows[3][6] = {{1, 0, 0, 0, r.z, -r.y}, {0, 1, 0, -r.z, 0, r.x}, {0, 0, 1, r.y, -r.x, 0}};
            for (int c = 0; c < 3; ++c) {
                if (!fixed[3 * n + std::size_t(c)])
                    continue;
                for (int i = 0; i < 6; ++i)
                    for (int j = 0; j < 6; ++j)
                        G(i, j) += rows[c][i] * rows[c][j];
            }
        }
        const Eigen::SelfAdjointEigenSolver<Eigen::Matrix<double, 6, 6>> es(G);
        const auto& ev = es.eigenvalues();
        if (ev.maxCoeff() <= 0 || ev.minCoeff() <= ev.maxCoeff() * 1e-10)
            throw FeaError("The supports do not prevent all rigid body motions (the part could slide or "
                           "rotate): fix more faces or more directions");
    }

    out.eq.assign(nDof, -1);
    for (std::size_t d = 0; d < nDof; ++d)
        if (!fixed[d])
            out.eq[d] = out.count++;
    if (out.count == 0)
        throw FeaError("Every node is fixed: nothing to solve");
    return out;
}


/// Assembles the lower triangle of a global matrix over the free equations.
/// `element(conn, Me)` fills the (3 nen x 3 nen) element matrix.
template <typename ElementFn, typename ProgressFn>
SpMat assembleLower(const VolumeMesh& mesh, const std::vector<int>& eq, int nEq, ElementFn&& element,
                    ProgressFn&& progress)
{
    const int nen = mesh.nodesPerElement;
    const std::size_t nNodes = mesh.nodeCount();
    const std::size_t nElem = mesh.elementCount();
    std::vector<std::vector<int>> adjacency(nNodes);
    for (std::size_t e = 0; e < nElem; ++e) {
        const int* conn = &mesh.elements[e * std::size_t(nen)];
        for (int a = 0; a < nen; ++a)
            for (int b = 0; b < nen; ++b)
                adjacency[std::size_t(conn[a])].push_back(conn[b]);
    }
    for (auto& adj : adjacency) {
        std::sort(adj.begin(), adj.end());
        adj.erase(std::unique(adj.begin(), adj.end()), adj.end());
    }
    progress(0.0);

    SpMat K(nEq, nEq);
    {
        Eigen::VectorXi colNnz = Eigen::VectorXi::Zero(nEq);
        for (std::size_t n = 0; n < nNodes; ++n)
            for (int c = 0; c < 3; ++c) {
                const int col = eq[3 * n + std::size_t(c)];
                if (col < 0)
                    continue;
                int cnt = 0;
                for (int m : adjacency[n])
                    for (int r = 0; r < 3; ++r)
                        cnt += (eq[std::size_t(3 * m + r)] >= col);
                colNnz(col) = cnt;
            }
        K.reserve(colNnz);
        // Insert the pattern in sorted order (cheap), values are added below.
        for (std::size_t n = 0; n < nNodes; ++n)
            for (int c = 0; c < 3; ++c) {
                const int col = eq[3 * n + std::size_t(c)];
                if (col < 0)
                    continue;
                for (int m : adjacency[n])
                    for (int r = 0; r < 3; ++r) {
                        const int row = eq[std::size_t(3 * m + r)];
                        if (row >= col)
                            K.insert(row, col) = 0.0;
                    }
            }
    }
    adjacency.clear();
    adjacency.shrink_to_fit();

    Eigen::Matrix<double, 30, 30> Me;
    std::vector<int> dofEq(std::size_t(3 * nen));
    for (std::size_t e = 0; e < nElem; ++e) {
        const int* conn = &mesh.elements[e * std::size_t(nen)];
        element(conn, Me);
        for (int a = 0; a < nen; ++a)
            for (int c = 0; c < 3; ++c)
                dofEq[std::size_t(3 * a + c)] = eq[std::size_t(3 * conn[a] + c)];
        for (int j = 0; j < 3 * nen; ++j) {
            const int col = dofEq[std::size_t(j)];
            if (col < 0)
                continue;
            for (int i = 0; i < 3 * nen; ++i) {
                const int row = dofEq[std::size_t(i)];
                if (row >= col)
                    K.coeffRef(row, col) += Me(i, j);
            }
        }
        if ((e & 4095) == 0)
            progress(double(e) / double(std::max<std::size_t>(nElem, 1)));
    }
    K.makeCompressed();
    return K;
}

/// Consistent mass matrix of a (straight-sided) Tet4 / Tet10 element.
void elementMass(const VolumeMesh& mesh, const int* conn, int nen, double density,
                 Eigen::Matrix<double, 30, 30>& Me)
{
    Me.setZero();
    const Vec3& p0 = mesh.nodes[std::size_t(conn[0])];
    const double V = std::abs(glm::dot(mesh.nodes[std::size_t(conn[1])] - p0,
                                       glm::cross(mesh.nodes[std::size_t(conn[2])] - p0,
                                                  mesh.nodes[std::size_t(conn[3])] - p0))) / 6.0;
    double m[10][10];
    if (nen == 4) {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                m[i][j] = (i == j ? 2.0 : 1.0) / 20.0;
    } else {
        // Exact integrals of N_i N_j for the quadratic tetrahedron (times 1/V), see
        // Zienkiewicz & Taylor: corner/corner 6|1, corner/mid-edge -4 (edge touches
        // the corner) | -6, mid/mid 32 | 16 (edges share a corner) | 8 (opposite).
        static const int e[6][2] = {{0, 1}, {1, 2}, {2, 0}, {0, 3}, {1, 3}, {2, 3}};
        auto touches = [](int edge, int corner) { return e[edge][0] == corner || e[edge][1] == corner; };
        for (int i = 0; i < 10; ++i)
            for (int j = 0; j < 10; ++j) {
                double v;
                if (i < 4 && j < 4)
                    v = i == j ? 6.0 : 1.0;
                else if (i < 4 || j < 4) {
                    const int c = i < 4 ? i : j, ed = (i < 4 ? j : i) - 4;
                    v = touches(ed, c) ? -4.0 : -6.0;
                } else if (i == j) {
                    v = 32.0;
                } else {
                    const int a = i - 4, b = j - 4;
                    const bool share = touches(a, e[b][0]) || touches(a, e[b][1]);
                    v = share ? 16.0 : 8.0;
                }
                m[i][j] = v / 420.0;
            }
    }
    for (int a = 0; a < nen; ++a)
        for (int b = 0; b < nen; ++b)
            for (int c = 0; c < 3; ++c)
                Me(3 * a + c, 3 * b + c) = density * V * m[a][b];
}

} // namespace

double vonMises(const std::array<double, 6>& s)
{
    const double a = s[0] - s[1], b = s[1] - s[2], c = s[2] - s[0];
    return std::sqrt(0.5 * (a * a + b * b + c * c) + 3.0 * (s[3] * s[3] + s[4] * s[4] + s[5] * s[5]));
}

StaticResult solveStatic(const VolumeMesh& mesh, const StaticSetup& setup, const SolveControl& ctl)
{
    const auto report = [&](const std::string& stage, double p) {
        if (ctl.progress)
            ctl.progress(stage, p);
    };
    const auto checkCancel = [&] {
        if (ctl.cancel && ctl.cancel->load())
            throw FeaError("Cancelled");
    };

    const Material& mat = setup.material;
    if (!(mat.youngsModulus > 0))
        throw FeaError("Young's modulus must be positive");
    if (!(mat.poissonRatio > -1.0 && mat.poissonRatio < 0.5))
        throw FeaError("Poisson's ratio must be in (-1, 0.5)");
    if (mesh.empty())
        throw FeaError("The mesh is empty");
    if (setup.supports.empty())
        throw FeaError("Add at least one fixed support: the model can move freely");

    const int nen = mesh.nodesPerElement;
    const std::size_t nNodes = mesh.nodeCount();
    const std::size_t nElem = mesh.elementCount();
    const std::size_t nDof = 3 * nNodes;
    const Mat6 D = elasticity(mat);
    StaticResult res;

    // ---- constraints -> equation numbering ----
    const Equations eqs = numberEquations(mesh, setup.supports);
    const std::vector<char>& fixed = eqs.fixed;
    const std::vector<int>& eq = eqs.eq;
    const int nEq = eqs.count;
    res.equations = nEq;

    // ---- load vector ----
    report("Applying loads", 0.0);
    Eigen::VectorXd f = Eigen::VectorXd::Zero(Eigen::Index(nDof));
    for (const auto& load : setup.forces) {
        double area = 0.0;
        std::vector<std::size_t> tris;
        for (std::size_t t = 0; t < mesh.boundaryFaceIds.size(); ++t)
            if (std::find(load.faces.begin(), load.faces.end(), mesh.boundaryFaceIds[t]) != load.faces.end()) {
                tris.push_back(t);
                area += glm::length(triangleAreaNormal(mesh, t));
            }
        if (tris.empty() || area <= 0)
            throw FeaError("A force references faces that are not part of the mesh");
        const Vec3 traction = load.force / area;
        for (std::size_t t : tris)
            addTraction(mesh, t, traction * glm::length(triangleAreaNormal(mesh, t)), f);
    }
    for (const auto& load : setup.pressures) {
        bool any = false;
        for (std::size_t t = 0; t < mesh.boundaryFaceIds.size(); ++t)
            if (std::find(load.faces.begin(), load.faces.end(), mesh.boundaryFaceIds[t]) != load.faces.end()) {
                addTraction(mesh, t, -load.pressure * triangleAreaNormal(mesh, t), f);
                any = true;
            }
        if (!any)
            throw FeaError("A pressure references faces that are not part of the mesh");
    }
    if (setup.gravity) {
        const Vec3 b = mat.density * setup.gravityAcceleration; // N/mm^3
        Eigen::Matrix<double, 6, 30> B;
        double N[10];
        for (std::size_t e = 0; e < nElem; ++e) {
            const int* conn = &mesh.elements[e * std::size_t(nen)];
            for (const auto& gp : gaussRule(nen)) {
                const double w = std::abs(bMatrix(mesh, conn, nen, gp.p, B, N)) * gp.w;
                for (int a = 0; a < nen; ++a)
                    for (int c = 0; c < 3; ++c)
                        f(3 * conn[a] + c) += N[a] * b[c] * w;
            }
        }
    }
    for (std::size_t n = 0; n < nNodes; ++n)
        res.appliedLoad += Vec3(f(Eigen::Index(3 * n)), f(Eigen::Index(3 * n + 1)), f(Eigen::Index(3 * n + 2)));

    // ---- stiffness matrix (lower triangle of the free equations) ----
    const auto t0 = Clock::now();
    report("Assembling stiffness matrix", 0.05);
    SpMat K = assembleLower(mesh, eq, nEq,
                            [&](const int* conn, Eigen::Matrix<double, 30, 30>& Ke) {
                                elementStiffness(mesh, conn, nen, D, Ke);
                            },
                            [&](double t) {
                                checkCancel();
                                report("Assembling stiffness matrix", 0.05 + 0.25 * t);
                            });
    Eigen::VectorXd fFree(nEq);
    for (std::size_t d = 0; d < nDof; ++d)
        if (eq[d] >= 0)
            fFree(eq[d]) = f(Eigen::Index(d));
    res.assemblySeconds = seconds(t0);

    // ---- solve ----
    const auto t1 = Clock::now();
    Eigen::VectorXd uFree;
    using Method = SolveControl::Method;
    Method method = ctl.method;
    if (method == Method::Automatic)
        method = nEq <= ctl.directSolverLimit ? Method::Direct : Method::AmgCg;

    if (method == Method::Direct) {
        report("Factorizing (direct solver)", 0.35);
        Eigen::SimplicialLDLT<SpMat, Eigen::Lower> ldlt;
        ldlt.compute(K);
        if (ldlt.info() != Eigen::Success)
            throw FeaError("The stiffness matrix is singular: the model is not sufficiently supported");
        const auto& d = ldlt.vectorD();
        const double dmax = d.cwiseAbs().maxCoeff();
        if (d.minCoeff() <= dmax * 1e-13)
            throw FeaError("The model is not sufficiently supported (rigid body motion possible)");
        checkCancel();
        report("Solving", 0.75);
        uFree = ldlt.solve(fFree);
        res.solver = "Direct (sparse LDL^T)";
    } else if (method == Method::AmgCg) {
        report("Building AMG preconditioner", 0.35);
        // AMGCL wants the full (both triangles) matrix in CSR form.
        Eigen::SparseMatrix<double, Eigen::RowMajor> A = K.selfadjointView<Eigen::Lower>();
        A.makeCompressed();
        const std::ptrdiff_t n = A.rows();
        const auto rows = amgcl::make_iterator_range(A.outerIndexPtr(), A.outerIndexPtr() + n + 1);
        const auto cols = amgcl::make_iterator_range(A.innerIndexPtr(), A.innerIndexPtr() + A.nonZeros());
        const auto vals = amgcl::make_iterator_range(A.valuePtr(), A.valuePtr() + A.nonZeros());

        using Backend = amgcl::backend::builtin<double>;
        using AmgSolver = amgcl::make_solver<
            amgcl::amg<Backend, amgcl::coarsening::smoothed_aggregation, amgcl::relaxation::ilu0>,
            amgcl::solver::cg<Backend>>;
        AmgSolver::params prm;
        prm.solver.tol = ctl.tolerance;
        prm.solver.maxiter = 5000;
        // With rigid body modes as near-null space, aggregating without a strength
        // threshold works far better for (quadratic) elasticity elements.
        prm.precond.coarsening.aggr.eps_strong = 0.0;
        // Rigid body modes of the free equations as near-null space: the key to
        // fast convergence for elasticity.
        {
            std::vector<double> coords(3 * nNodes);
            for (std::size_t i = 0; i < nNodes; ++i)
                for (int c = 0; c < 3; ++c)
                    coords[3 * i + std::size_t(c)] = mesh.nodes[i][c];
            std::vector<double> Bfull;
            const int modes = amgcl::coarsening::rigid_body_modes(3, coords, Bfull);
            std::vector<double>& B = prm.precond.coarsening.nullspace.B;
            B.resize(std::size_t(nEq) * std::size_t(modes));
            for (std::size_t d = 0; d < nDof; ++d)
                if (eq[d] >= 0)
                    for (int m = 0; m < modes; ++m)
                        B[std::size_t(eq[d]) * std::size_t(modes) + std::size_t(m)] =
                            Bfull[d * std::size_t(modes) + std::size_t(m)];
            prm.precond.coarsening.nullspace.cols = modes;
        }
        checkCancel();
        AmgSolver solver(std::tie(n, rows, cols, vals), prm);
        checkCancel();
        report("Solving (AMG + CG)", 0.6);
        std::vector<double> rhs(fFree.data(), fFree.data() + nEq), x(std::size_t(nEq), 0.0);
        const auto [iters, error] = solver(rhs, x);
        if (!(error <= ctl.tolerance * 10.0) || !std::isfinite(error))
            throw FeaError("The iterative solver did not converge (is the model sufficiently supported?)");
        uFree = Eigen::Map<Eigen::VectorXd>(x.data(), nEq);
        res.solver = "AMG + CG (" + std::to_string(iters) + " iterations)";
    } else {
        report("Preconditioning (iterative solver)", 0.35);
        Eigen::ConjugateGradient<SpMat, Eigen::Lower, Eigen::IncompleteCholesky<double, Eigen::Lower>> cg;
        cg.setTolerance(ctl.tolerance);
        cg.compute(K);
        if (cg.info() != Eigen::Success)
            throw FeaError("Preconditioner setup failed: the model may not be sufficiently supported");
        uFree = Eigen::VectorXd::Zero(nEq);
        const int chunk = 250, maxIter = std::max(2000, nEq / 5);
        cg.setMaxIterations(chunk);
        int total = 0;
        for (;;) {
            checkCancel();
            uFree = cg.solveWithGuess(fFree, uFree);
            total += int(cg.iterations());
            if (cg.info() == Eigen::Success)
                break;
            if (total >= maxIter)
                throw FeaError("Iterative solver did not converge (is the model sufficiently supported?)");
            const double err = std::max(cg.error(), 1e-12);
            report("Solving (iteration " + std::to_string(total) + ")",
                   0.40 + 0.45 * std::clamp(std::log10(err) / -9.0, 0.0, 1.0));
        }
        res.solver = "CG + incomplete Cholesky (" + std::to_string(total) + " iterations)";
    }
    res.solveSeconds = seconds(t1);

    // ---- displacements ----
    res.displacement.assign(nNodes, Vec3(0.0));
    for (std::size_t n = 0; n < nNodes; ++n)
        for (int c = 0; c < 3; ++c) {
            const int q = eq[3 * n + std::size_t(c)];
            if (q >= 0)
                res.displacement[n][c] = uFree(q);
        }
    for (const auto& u : res.displacement)
        res.maxDisplacement = std::max(res.maxDisplacement, glm::length(u));
    {
        BoundingBox bb;
        for (const auto& p : mesh.nodes)
            bb.add(p);
        if (!std::isfinite(res.maxDisplacement) || res.maxDisplacement > 1e3 * std::max(bb.diagonal(), 1.0))
            throw FeaError("Unrealistically large displacements: the model is not sufficiently supported");
    }

    // ---- stresses (nodal averages) and reactions ----
    report("Computing stresses", 0.9);
    res.stress.assign(nNodes, {0, 0, 0, 0, 0, 0});
    std::vector<int> count(nNodes, 0);
    Eigen::VectorXd fInt = Eigen::VectorXd::Zero(Eigen::Index(nDof));
    Eigen::Matrix<double, 6, 30> B;
    Eigen::Matrix<double, 30, 1> ue;
    Eigen::Matrix<double, 30, 30> Ke;
    double N[10];
    const auto& natural = nodeNaturalCoords();
    for (std::size_t e = 0; e < nElem; ++e) {
        const int* conn = &mesh.elements[e * std::size_t(nen)];
        ue.setZero();
        for (int a = 0; a < nen; ++a)
            for (int c = 0; c < 3; ++c)
                ue(3 * a + c) = res.displacement[std::size_t(conn[a])][c];
        for (int a = 0; a < nen; ++a) {
            bMatrix(mesh, conn, nen, natural[std::size_t(a)], B, N);
            const Eigen::Matrix<double, 6, 1> sig = D * (B.leftCols(3 * nen) * ue.head(3 * nen));
            auto& acc = res.stress[std::size_t(conn[a])];
            for (int k = 0; k < 6; ++k)
                acc[std::size_t(k)] += sig(k);
            ++count[std::size_t(conn[a])];
        }
        elementStiffness(mesh, conn, nen, D, Ke);
        const Eigen::VectorXd fe = Ke.topLeftCorner(3 * nen, 3 * nen) * ue.head(3 * nen);
        for (int a = 0; a < nen; ++a)
            for (int c = 0; c < 3; ++c)
                fInt(3 * conn[a] + c) += fe(3 * a + c);
        if ((e & 4095) == 0)
            checkCancel();
    }
    res.vonMises.assign(nNodes, 0.0);
    for (std::size_t n = 0; n < nNodes; ++n) {
        if (count[n] > 0)
            for (auto& v : res.stress[n])
                v /= count[n];
        res.vonMises[n] = vonMises(res.stress[n]);
        res.maxVonMises = std::max(res.maxVonMises, res.vonMises[n]);
    }
    for (std::size_t d = 0; d < nDof; ++d)
        if (fixed[d])
            res.reaction[int(d % 3)] += fInt(Eigen::Index(d)) - f(Eigen::Index(d));

    report("Done", 1.0);
    return res;
}

namespace {

/// Spectra operator y = (K - sigma M)^-1 x using a sparse LDL^T factorization.
class ShiftInvertLdlt {
public:
    using Scalar = double;
    ShiftInvertLdlt(const SpMat& K, const SpMat& M) : m_K(K), m_M(M) {}
    Eigen::Index rows() const { return m_K.rows(); }
    Eigen::Index cols() const { return m_K.cols(); }
    void set_shift(double sigma)
    {
        SpMat A = m_K;
        if (sigma != 0.0)
            A -= sigma * m_M;
        m_ldlt.compute(A);
        if (m_ldlt.info() != Eigen::Success)
            throw FeaError("The stiffness matrix is singular: the model is not sufficiently supported");
    }
    void perform_op(const double* x, double* y) const
    {
        Eigen::Map<Eigen::VectorXd>(y, rows()) = m_ldlt.solve(Eigen::Map<const Eigen::VectorXd>(x, rows()));
    }

private:
    const SpMat& m_K;
    const SpMat& m_M;
    Eigen::SimplicialLDLT<SpMat, Eigen::Lower> m_ldlt;
};

} // namespace

ModalResult solveModal(const VolumeMesh& mesh, const ModalSetup& setup, const SolveControl& ctl)
{
    const auto report = [&](const std::string& stage, double p) {
        if (ctl.progress)
            ctl.progress(stage, p);
    };
    const auto checkCancel = [&] {
        if (ctl.cancel && ctl.cancel->load())
            throw FeaError("Cancelled");
    };
    const Material& mat = setup.material;
    if (!(mat.youngsModulus > 0))
        throw FeaError("Young's modulus must be positive");
    if (!(mat.poissonRatio > -1.0 && mat.poissonRatio < 0.5))
        throw FeaError("Poisson's ratio must be in (-1, 0.5)");
    if (!(mat.density > 0))
        throw FeaError("A modal analysis needs a positive density");
    if (mesh.empty())
        throw FeaError("The mesh is empty");
    if (setup.supports.empty())
        throw FeaError("Add at least one fixed support (free-free modal analysis is not supported yet)");

    const int nen = mesh.nodesPerElement;
    const std::size_t nNodes = mesh.nodeCount();
    const Mat6 D = elasticity(mat);
    ModalResult res;

    const Equations eqs = numberEquations(mesh, setup.supports);
    const int nEq = eqs.count;
    res.equations = nEq;

    const auto t0 = Clock::now();
    report("Assembling stiffness matrix", 0.02);
    const SpMat K = assembleLower(
        mesh, eqs.eq, nEq, [&](const int* conn, Eigen::Matrix<double, 30, 30>& Ke) { elementStiffness(mesh, conn, nen, D, Ke); },
        [&](double t) {
            checkCancel();
            report("Assembling stiffness matrix", 0.02 + 0.18 * t);
        });
    report("Assembling mass matrix", 0.2);
    const SpMat M = assembleLower(
        mesh, eqs.eq, nEq,
        [&](const int* conn, Eigen::Matrix<double, 30, 30>& Me) { elementMass(mesh, conn, nen, mat.density, Me); },
        [&](double t) {
            checkCancel();
            report("Assembling mass matrix", 0.2 + 0.1 * t);
        });
    res.assemblySeconds = seconds(t0);

    const int nev = std::clamp(setup.modes, 1, std::max(1, nEq - 2));
    if (nEq < 3)
        throw FeaError("The model has too few free degrees of freedom for a modal analysis");
    const int ncv = std::min(nEq, std::max(2 * nev + 1, 20));

    const auto t1 = Clock::now();
    report("Factorizing (shift-invert)", 0.35);
    using BOp = Spectra::SparseSymMatProd<double, Eigen::Lower>;
    ShiftInvertLdlt op(K, M);
    BOp bop(M);
    // Shift 0: the factorization of K itself; the lowest frequencies converge first.
    Spectra::SymGEigsShiftSolver<ShiftInvertLdlt, BOp, Spectra::GEigsMode::ShiftInvert> solver(op, bop, nev, ncv, 0.0);
    checkCancel();
    report("Computing eigenvalues", 0.7);
    solver.init();
    const Eigen::Index nconv = solver.compute(Spectra::SortRule::LargestMagn, 1000, 1e-10, Spectra::SortRule::SmallestAlge);
    if (solver.info() != Spectra::CompInfo::Successful || nconv < 1)
        throw FeaError("The eigenvalue solver did not converge");
    const Eigen::VectorXd lambda = solver.eigenvalues();
    const Eigen::MatrixXd phi = solver.eigenvectors(); // M-orthonormal
    res.solveSeconds = seconds(t1);
    res.solver = "Shift-invert Lanczos (Spectra) + sparse LDL^T";

    // Rigid body translation vectors of the free DOFs -> effective modal masses.
    report("Mode shapes", 0.95);
    const auto Mfull = M.selfadjointView<Eigen::Lower>();
    Eigen::MatrixXd R = Eigen::MatrixXd::Zero(nEq, 3);
    for (std::size_t d = 0; d < 3 * nNodes; ++d)
        if (eqs.eq[d] >= 0)
            R(eqs.eq[d], Eigen::Index(d % 3)) = 1.0;
    const Eigen::MatrixXd MR = Mfull * R;
    const Eigen::Vector3d total(R.col(0).dot(MR.col(0)), R.col(1).dot(MR.col(1)), R.col(2).dot(MR.col(2)));
    res.totalMass = total.maxCoeff();

    // Spectra returns the requested order (SmallestAlge: ascending).
    for (Eigen::Index k = 0; k < lambda.size(); ++k) {
        const double w2 = std::max(lambda(k), 0.0);
        res.frequencies.push_back(std::sqrt(w2) / (2.0 * 3.14159265358979323846));
        std::vector<Vec3> shape(nNodes, Vec3(0.0));
        double umax = 0.0;
        for (std::size_t n = 0; n < nNodes; ++n) {
            for (int c = 0; c < 3; ++c) {
                const int q = eqs.eq[3 * n + std::size_t(c)];
                if (q >= 0)
                    shape[n][c] = phi(q, k);
            }
            umax = std::max(umax, glm::length(shape[n]));
        }
        if (umax > 0.0)
            for (auto& u : shape)
                u /= umax;
        res.shapes.push_back(std::move(shape));
        const Eigen::Vector3d gamma = MR.transpose() * phi.col(k); // participation factors
        Vec3 ratio(0.0);
        for (int c = 0; c < 3; ++c)
            ratio[c] = total(c) > 0.0 ? gamma(c) * gamma(c) / total(c) : 0.0;
        res.effectiveMassRatio.push_back(ratio);
    }
    report("Done", 1.0);
    return res;
}

} // namespace cf::fea
