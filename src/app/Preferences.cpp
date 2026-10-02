#include "app/Preferences.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace cf::app {

using json = nlohmann::json;

namespace {

std::filesystem::path prefsPath() { return paths::configDir() / "preferences.json"; }

json color(const glm::vec3& c) { return json::array({c.r, c.g, c.b}); }

void readColor(const json& j, const char* key, glm::vec3& c)
{
    if (auto it = j.find(key); it != j.end() && it->is_array() && it->size() == 3)
        c = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
}

template <typename E> void readEnum(const json& j, const char* key, E& e, int maxValue)
{
    if (auto it = j.find(key); it != j.end() && it->is_number_integer()) {
        const int v = it->get<int>();
        if (v >= 0 && v <= maxValue)
            e = static_cast<E>(v);
    }
}

} // namespace

Preferences Preferences::load()
{
    Preferences p;
    std::ifstream in(prefsPath(), std::ios::binary);
    if (!in)
        return p;
    try {
        json j;
        in >> j;
        readEnum(j, "orbitButton", p.orbitButton, 2);
        readEnum(j, "panButton", p.panButton, 2);
        p.invertZoom = j.value("invertZoom", p.invertZoom);
        p.zoomSpeed = std::clamp(j.value("zoomSpeed", p.zoomSpeed), 0.1f, 5.0f);
        readEnum(j, "theme", p.theme, 1);
        p.fontSize = std::clamp(j.value("fontSize", p.fontSize), 10.0f, 28.0f);
        p.toolbarLabels = j.value("toolbarLabels", p.toolbarLabels);
        readColor(j, "backgroundTop", p.backgroundTop);
        readColor(j, "backgroundBottom", p.backgroundBottom);
        readColor(j, "selectionColor", p.selectionColor);
        readColor(j, "hoverColor", p.hoverColor);
        p.edgeWidth = std::clamp(j.value("edgeWidth", p.edgeWidth), 0.5f, 4.0f);
        p.decimals = std::clamp(j.value("decimals", p.decimals), 0, 8);
        p.autosaveMinutes = std::clamp(j.value("autosaveMinutes", p.autosaveMinutes), 0, 120);
        p.maxRecentFiles = std::clamp(j.value("maxRecentFiles", p.maxRecentFiles), 1, 30);
        if (auto it = j.find("recentFiles"); it != j.end() && it->is_array())
            for (const auto& f : *it)
                if (f.is_string())
                    p.recentFiles.push_back(f.get<std::string>());
        if (int(p.recentFiles.size()) > p.maxRecentFiles)
            p.recentFiles.resize(std::size_t(p.maxRecentFiles));
    } catch (const std::exception& e) {
        log::warn("Ignoring invalid preferences file: ", e.what());
        return Preferences{};
    }
    return p;
}

bool Preferences::save() const
{
    const json j = {
        {"orbitButton", int(orbitButton)},
        {"panButton", int(panButton)},
        {"invertZoom", invertZoom},
        {"zoomSpeed", zoomSpeed},
        {"theme", int(theme)},
        {"fontSize", fontSize},
        {"toolbarLabels", toolbarLabels},
        {"backgroundTop", color(backgroundTop)},
        {"backgroundBottom", color(backgroundBottom)},
        {"selectionColor", color(selectionColor)},
        {"hoverColor", color(hoverColor)},
        {"edgeWidth", edgeWidth},
        {"decimals", decimals},
        {"autosaveMinutes", autosaveMinutes},
        {"maxRecentFiles", maxRecentFiles},
        {"recentFiles", recentFiles},
    };
    std::ofstream out(prefsPath(), std::ios::binary | std::ios::trunc);
    if (!out) {
        log::warn("Cannot write preferences to ", paths::toUtf8(prefsPath()));
        return false;
    }
    out << j.dump(2) << '\n';
    return bool(out);
}

void Preferences::addRecentFile(const std::string& path)
{
    removeRecentFile(path);
    recentFiles.insert(recentFiles.begin(), path);
    if (int(recentFiles.size()) > maxRecentFiles)
        recentFiles.resize(std::size_t(maxRecentFiles));
}

void Preferences::removeRecentFile(const std::string& path)
{
    recentFiles.erase(std::remove(recentFiles.begin(), recentFiles.end(), path), recentFiles.end());
}

std::string Preferences::numberFormat() const
{
    return "%." + std::to_string(std::clamp(decimals, 0, 8)) + "f";
}

} // namespace cf::app
