#include "core/Log.h"

#include <cstdio>
#include <deque>

namespace cf::log {

namespace {
std::mutex g_mutex;
std::deque<Entry> g_entries;
std::uint64_t g_revision = 0;
constexpr std::size_t kMaxEntries = 1000;
} // namespace

void write(Level level, std::string text)
{
    const char* tag = level == Level::Info ? "info" : level == Level::Warning ? "warn" : "error";
    FILE* stream = level == Level::Info ? stdout : stderr;
    std::fprintf(stream, "[%s] %s\n", tag, text.c_str());
    std::fflush(stream);

    std::lock_guard lock(g_mutex);
    g_entries.push_back({level, std::move(text)});
    if (g_entries.size() > kMaxEntries)
        g_entries.pop_front();
    ++g_revision;
}

std::vector<Entry> entries()
{
    std::lock_guard lock(g_mutex);
    return {g_entries.begin(), g_entries.end()};
}

std::uint64_t revision()
{
    std::lock_guard lock(g_mutex);
    return g_revision;
}

void clear()
{
    std::lock_guard lock(g_mutex);
    g_entries.clear();
    ++g_revision;
}

} // namespace cf::log
