# Fetches and bootstraps a project-local vcpkg (external\vcpkg).
# Afterwards:  cmake --preset release ; cmake --build --preset release
$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Vcpkg = Join-Path $Root "external\vcpkg"

if (-not (Test-Path (Join-Path $Vcpkg ".git"))) {
    Write-Host "Cloning vcpkg into $Vcpkg ..."
    git clone https://github.com/microsoft/vcpkg $Vcpkg
} else {
    Write-Host "Updating vcpkg in $Vcpkg ..."
    git -C $Vcpkg pull --ff-only
}

& (Join-Path $Vcpkg "bootstrap-vcpkg.bat") -disableMetrics

Write-Host ""
Write-Host "vcpkg is ready. Next steps (from a 'x64 Native Tools' or normal PowerShell):"
Write-Host "  cmake --preset release          # first run builds OpenCASCADE etc. (20-60 min, cached afterwards)"
Write-Host "  cmake --build --preset release"
Write-Host "  ctest --preset release"
