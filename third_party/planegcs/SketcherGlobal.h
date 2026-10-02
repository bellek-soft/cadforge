// CadForge shim: PlaneGCS is built as a static library, no export macros needed.
#pragma once
#define SketcherExport

// FreeCAD builds with C++23 (std::unreachable); CadForge uses C++20.
#if defined(_MSC_VER) && !defined(__clang__)
#define CADFORGE_UNREACHABLE() __assume(false)
#else
#define CADFORGE_UNREACHABLE() __builtin_unreachable()
#endif
