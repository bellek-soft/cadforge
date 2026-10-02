// CadForge shim for FreeCAD's Base/Console.h: PlaneGCS only logs debug/diagnostic text.
#pragma once
namespace Base {
struct ConsoleSingleton {
    template <typename... A> void log(A&&...) {}
    template <typename... A> void message(A&&...) {}
    template <typename... A> void warning(A&&...) {}
    template <typename... A> void error(A&&...) {}
};
inline ConsoleSingleton& Console()
{
    static ConsoleSingleton c;
    return c;
}
} // namespace Base
