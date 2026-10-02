#pragma once
// Linear static solver for 3D solid elasticity (Tet4 / Tet10).

#include "fea/FeaTypes.h"

#include <atomic>
#include <functional>
#include <string>

namespace cf::fea {

struct SolveControl {
    /// Called with a short stage description and progress in [0, 1].
    std::function<void(const std::string& stage, double progress)> progress;
    /// Set to true to abort; solveStatic then throws FeaError("Cancelled").
    const std::atomic<bool>* cancel = nullptr;
    enum class Method {
        Automatic, // direct for small systems, AMG-preconditioned CG otherwise
        Direct,    // sparse LDL^T (Eigen)
        AmgCg,     // CG + smoothed aggregation AMG (ILU0 smoother, rigid body modes; AMGCL)
        IcCg,      // CG + incomplete Cholesky (Eigen)
    };
    Method method = Method::Automatic;
    /// Automatic: use the direct solver up to this many equations.
    int directSolverLimit = 3000;
    double tolerance = 1e-9; // relative residual of the iterative solvers
};

/// Assembles and solves K u = f. Throws FeaError for invalid setups
/// (no supports, under-constrained model, bad material, ...).
StaticResult solveStatic(const VolumeMesh& mesh, const StaticSetup& setup, const SolveControl& control = {});

/// Natural frequencies and mode shapes: K phi = omega^2 M phi (consistent mass,
/// shift-invert Lanczos via Spectra on a sparse LDL^T factorization).
/// Throws FeaError like solveStatic.
ModalResult solveModal(const VolumeMesh& mesh, const ModalSetup& setup, const SolveControl& control = {});

/// Von Mises equivalent stress of a Voigt stress vector (xx, yy, zz, xy, yz, zx).
double vonMises(const std::array<double, 6>& s);

} // namespace cf::fea
