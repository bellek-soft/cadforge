#include "core/Paths.h"

#include <cstdlib>
#include <system_error>

namespace cf::paths {

namespace {
std::filesystem::path envPath(const char* name)
{
    const char* v = std::getenv(name);
    return (v && *v) ? std::filesystem::path(v) : std::filesystem::path();
}
} // namespace

std::filesystem::path configDir()
{
    std::filesystem::path base;
#if defined(_WIN32)
    base = envPath("APPDATA");
#elif defined(__APPLE__)
    if (auto home = envPath("HOME"); !home.empty())
        base = home / "Library" / "Application Support";
#else
    base = envPath("XDG_CONFIG_HOME");
    if (base.empty())
        if (auto home = envPath("HOME"); !home.empty())
            base = home / ".config";
#endif
    if (base.empty())
        base = std::filesystem::temp_directory_path();

    auto dir = base / "CadForge";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path fromUtf8(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string toUtf8(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

} // namespace cf::paths
