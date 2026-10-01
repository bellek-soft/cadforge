#include "app/FileDialogs.h"

#include <portable-file-dialogs.h>

namespace cf::app::dialogs {

bool available()
{
    static const bool ok = pfd::settings::available();
    return ok;
}

std::optional<std::string> openFile(const std::string& title, const std::vector<std::string>& filters)
{
    auto result = pfd::open_file(title, "", filters, pfd::opt::none).result();
    if (result.empty())
        return std::nullopt;
    return result.front();
}

std::optional<std::string> saveFile(const std::string& title, const std::string& defaultPath,
                                    const std::vector<std::string>& filters)
{
    auto result = pfd::save_file(title, defaultPath, filters, pfd::opt::none).result();
    if (result.empty())
        return std::nullopt;
    return result;
}

} // namespace cf::app::dialogs
