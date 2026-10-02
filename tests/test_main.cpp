#include "TestHarness.h"

#include <cstring>
#include <exception>

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr; // optional substring filter
    int run = 0;
    for (auto& t : cftest::registry()) {
        if (filter && !std::strstr(t.name, filter))
            continue;
        ++run;
        const int before = cftest::failures();
        std::printf("[ RUN  ] %s\n", t.name);
        std::fflush(stdout);
        try {
            t.fn();
        } catch (const std::exception& e) {
            std::printf("    EXCEPTION: %s\n", e.what());
            ++cftest::failures();
        }
        std::printf("[ %s ] %s\n", cftest::failures() == before ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d tests, %d failed checks\n", run, cftest::failures());
    return cftest::failures() == 0 ? 0 : 1;
}
