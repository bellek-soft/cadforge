#pragma once
// Minimal thread-safe logger with an in-memory ring buffer (shown in the UI console).

#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace cf::log {

enum class Level { Info, Warning, Error };

struct Entry {
    Level level;
    std::string text;
};

void write(Level level, std::string text);

/// Copy of the most recent entries (oldest first).
std::vector<Entry> entries();
/// Monotonic counter, increases on every new entry (cheap change detection).
std::uint64_t revision();
void clear();

template <typename... Args>
std::string concat(Args&&... args)
{
    std::ostringstream os;
    (os << ... << std::forward<Args>(args));
    return os.str();
}

template <typename... Args> void info(Args&&... a)  { write(Level::Info,    concat(std::forward<Args>(a)...)); }
template <typename... Args> void warn(Args&&... a)  { write(Level::Warning, concat(std::forward<Args>(a)...)); }
template <typename... Args> void error(Args&&... a) { write(Level::Error,   concat(std::forward<Args>(a)...)); }

} // namespace cf::log
