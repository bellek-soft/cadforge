// CadForge shim for FreeCAD's Base/Tools.h (only TimeElapsed is referenced, in profiling code).
#pragma once
#include <chrono>
namespace Base {
struct TimeElapsed {
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    static double diffTimeF(const TimeElapsed& a, const TimeElapsed& b)
    {
        return std::chrono::duration<double>(b.t - a.t).count();
    }
};
} // namespace Base
