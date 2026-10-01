#pragma once

#include <filesystem>
#include <string>

namespace cf::paths {

/// Per-user configuration directory (created on demand):
///   macOS   ~/Library/Application Support/CadForge
///   Windows %APPDATA%\CadForge
///   Linux   $XDG_CONFIG_HOME/CadForge or ~/.config/CadForge
std::filesystem::path configDir();

/// UTF-8 <-> filesystem path conversions (portable, incl. Windows).
std::filesystem::path fromUtf8(const std::string& utf8);
std::string toUtf8(const std::filesystem::path& p);

} // namespace cf::paths
