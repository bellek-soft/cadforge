#include "app/Autosave.h"
#include "app/AppContext.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <random>

namespace cf::app {

using json = nlohmann::ordered_json;

namespace {

std::uint64_t fnv1a(const std::string& s)
{
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string localTime()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

} // namespace

std::filesystem::path Autosave::directory()
{
    const auto dir = paths::configDir() / "autosave";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

Autosave::Autosave()
{
    std::random_device rd;
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    m_file = "session-" + std::to_string(static_cast<long long>(now)) + "-" + std::to_string(rd() % 100000) + ".cfp";
}

void Autosave::tick(AppContext& ctx, double now)
{
    const int minutes = ctx.prefs.autosaveMinutes;
    if (!enabled || minutes <= 0)
        return;
    if (m_lastWrite == 0.0)
        m_lastWrite = now;
    if (now - m_lastWrite < minutes * 60.0)
        return;
    m_lastWrite = now;
    writeNow(ctx);
}

bool Autosave::writeNow(AppContext& ctx)
{
    if (!enabled || !ctx.isModified())
        return false;
    json j = ctx.doc.toJson();
    const std::string body = j.dump();
    const std::uint64_t h = fnv1a(body);
    if (h == m_lastHash)
        return true; // nothing new
    j["autosave"] = {{"originalPath", ctx.filePath}, {"time", localTime()}};
    const auto path = directory() / m_file;
    const auto tmp = std::filesystem::path(path).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            log::warn("Autosave: cannot write ", paths::toUtf8(tmp));
            return false;
        }
        out << j.dump(1) << '\n';
        if (!out)
            return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        log::warn("Autosave failed: ", ec.message());
        return false;
    }
    m_lastHash = h;
    return true;
}

void Autosave::clear()
{
    std::error_code ec;
    std::filesystem::remove(directory() / m_file, ec);
    m_lastHash = 0;
}

std::vector<Autosave::Recovery> Autosave::findRecoveries()
{
    std::vector<std::pair<std::filesystem::file_time_type, Recovery>> found;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(directory(), ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".cfp")
            continue;
        try {
            std::ifstream in(e.path(), std::ios::binary);
            json j;
            in >> j;
            Recovery r;
            r.file = e.path();
            if (auto a = j.find("autosave"); a != j.end()) {
                r.originalPath = a->value("originalPath", std::string());
                r.time = a->value("time", std::string());
            }
            if (auto f = j.find("features"); f != j.end() && f->is_array())
                r.features = f->size();
            found.emplace_back(e.last_write_time(ec), r);
        } catch (const std::exception&) {
            // Unreadable leftovers (e.g. a write cut short) are skipped.
        }
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<Recovery> out;
    for (auto& f : found)
        out.push_back(std::move(f.second));
    return out;
}

bool Autosave::restore(AppContext& ctx, const Recovery& r)
{
    if (!ctx.openDocument(paths::toUtf8(r.file), false))
        return false;
    ctx.filePath = r.originalPath;
    ctx.history.markModified();
    ctx.status("Recovered unsaved work from " + r.time + " - save it to keep it");
    discard(r);
    return true;
}

void Autosave::discard(const Recovery& r)
{
    std::error_code ec;
    std::filesystem::remove(r.file, ec);
}

} // namespace cf::app
