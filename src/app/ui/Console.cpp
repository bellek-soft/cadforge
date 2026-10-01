#include "app/ui/Ui.h"

#include "app/AppContext.h"
#include "core/Log.h"

namespace cf::app::ui {

void drawConsole(AppContext& ctx)
{
    if (!ctx.showConsole)
        return;
    if (!ImGui::Begin(kConsoleWindow, &ctx.showConsole)) {
        ImGui::End();
        return;
    }
    static std::uint64_t lastRevision = 0;
    static std::vector<log::Entry> entries;
    const std::uint64_t rev = log::revision();
    const bool changed = rev != lastRevision;
    if (changed) {
        entries = log::entries();
        lastRevision = rev;
    }

    if (ImGui::SmallButton("Clear"))
        log::clear();
    ImGui::Separator();

    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
    ImGuiListClipper clipper;
    clipper.Begin(int(entries.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& e = entries[size_t(i)];
            switch (e.level) {
            case log::Level::Info: ImGui::TextUnformatted(e.text.c_str()); break;
            case log::Level::Warning: ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "%s", e.text.c_str()); break;
            case log::Level::Error: ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", e.text.c_str()); break;
            }
        }
    }
    if (changed && atBottom)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

} // namespace cf::app::ui
