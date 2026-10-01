#!/usr/bin/env bash
# Fetches and bootstraps a project-local vcpkg (external/vcpkg).
# Afterwards:  cmake --preset release && cmake --build --preset release
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VCPKG_DIR="$ROOT/external/vcpkg"

if [ ! -d "$VCPKG_DIR/.git" ]; then
    echo "Cloning vcpkg into $VCPKG_DIR ..."
    git clone https://github.com/microsoft/vcpkg "$VCPKG_DIR"
else
    echo "Updating vcpkg in $VCPKG_DIR ..."
    git -C "$VCPKG_DIR" pull --ff-only || true
fi

"$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics

echo
echo "vcpkg is ready. Next steps:"
echo "  cmake --preset release          # first run builds OpenCASCADE etc. (20-60 min, cached afterwards)"
echo "  cmake --build --preset release"
echo "  ctest --preset release"
