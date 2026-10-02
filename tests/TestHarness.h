#pragma once
// Tiny dependency-free test harness: TEST(name) { CHECK(...); }

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace cftest {
struct TestCase {
    const char* name;
    std::function<void()> fn;
};
inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}
inline int& failures()
{
    static int f = 0;
    return f;
}
struct Registrar {
    Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};
} // namespace cftest

#define TEST(name)                                            \
    static void name();                                       \
    static cftest::Registrar reg_##name(#name, name);         \
    static void name()

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ++cftest::failures();                                                   \
        }                                                                           \
    } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::abs((a) - (b)) <= (tol))
