#pragma once
// User preferences, persisted as JSON in the per-user configuration directory
// (paths::configDir()/preferences.json). Unknown / missing keys fall back to
// defaults, so the file can evolve freely.

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace cf::app {

struct Preferences {
    enum class MouseButton { Left = 0, Right = 1, Middle = 2 };
    enum class Theme { Dark = 0, Light = 1 };

    // Navigation
    MouseButton orbitButton = MouseButton::Right;
    MouseButton panButton = MouseButton::Middle;
    bool invertZoom = false;
    float zoomSpeed = 1.0f;

    // Appearance
    Theme theme = Theme::Dark;
    float fontSize = 15.0f;          // pt, applied on restart
    bool toolbarLabels = true;       // icon + text (false: icons only)
    glm::vec3 backgroundTop{0.23f, 0.25f, 0.29f};
    glm::vec3 backgroundBottom{0.11f, 0.12f, 0.14f};
    glm::vec3 selectionColor{1.0f, 0.62f, 0.12f};
    glm::vec3 hoverColor{0.45f, 0.82f, 1.0f};
    float edgeWidth = 1.4f;

    // Numbers
    int decimals = 3;                // shown in property fields, dimensions, measurements

    // Files
    int autosaveMinutes = 5;         // 0 = off
    int maxRecentFiles = 10;
    std::vector<std::string> recentFiles;

    /// Loads from disk (defaults on any problem).
    static Preferences load();
    /// Writes to disk; returns false (and logs) on failure.
    bool save() const;

    void addRecentFile(const std::string& path);
    void removeRecentFile(const std::string& path);

    /// printf format for numbers in the UI, e.g. "%.3f".
    std::string numberFormat() const;
};

} // namespace cf::app
