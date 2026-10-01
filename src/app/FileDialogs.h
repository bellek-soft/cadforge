#pragma once
// Native file dialogs (portable-file-dialogs). Isolated in one translation
// unit because it pulls in platform headers (windows.h, ...).

#include <optional>
#include <string>
#include <vector>

namespace cf::app::dialogs {

/// False when no native dialog backend exists (e.g. Linux without zenity/kdialog).
bool available();

std::optional<std::string> openFile(const std::string& title, const std::vector<std::string>& filters);
std::optional<std::string> saveFile(const std::string& title, const std::string& defaultPath,
                                    const std::vector<std::string>& filters);

} // namespace cf::app::dialogs
