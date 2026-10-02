Spectra 1.0.1 (https://github.com/yixuan/spectra), header-only sparse eigenvalue
solvers built on Eigen. MPL-2.0 (see LICENSE). Only `include/` is vendored;
the only change is an explicit `#include <cassert>` in LinAlg/Orthogonalization.h (needed
with Apple's libc++). Used by the modal analysis (fea::solveModal).
