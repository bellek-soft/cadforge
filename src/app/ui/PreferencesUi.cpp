// Preferences window and the crash-recovery dialog.

#include "app/ui/Ui.h"

#include "app/AppContext.h"
#include "core/Paths.h"

#include <LucideIcons.h>

#include <algorithm>
#include <string>

namespace cf::app::ui {

namespace {

const char* const kButtons[] = {"Left", "Right", "Middle"};

bool colorEdit(const char* label, glm::vec3& c)
{
    float v[3] = {c.r, c.g, c.b};
    if (ImGui::ColorEdit3(label, v, ImGuiColorEditFlags_NoInputs)) {
        c = {v[0], v[1], v[2]};
        return true;
    }
    return false;
}

void drawRecovery(AppContext& ctx)
{
    if (ctx.recoveries.empty())
        return;
    const char* title = ICON_WARNING " Recover unsaved work";
    if (!ImGui::IsPopupOpen(title))
        ImGui::OpenPopup(title);
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextWrapped("CadForge did not shut down cleanly last time. These autosaved documents were found:");
    ImGui::Spacing();
    int remove = -1;
    for (int i = 0; i < int(ctx.recoveries.size()); ++i) {
        const auto& r = ctx.recoveries[std::size_t(i)];
        ImGui::PushID(i);
        const std::string name = r.originalPath.empty() ? "Untitled document"
                                                        : paths::toUtf8(paths::fromUtf8(r.originalPath).filename());
        ImGui::BulletText("%s  -  %s, %zu features", name.c_str(), r.time.c_str(), r.features);
        if (!r.originalPath.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", r.originalPath.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Recover")) {
            Autosave::restore(ctx, r);
            remove = i;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Discard")) {
            Autosave::discard(r);
            remove = i;
        }
        ImGui::PopID();
    }
    if (remove >= 0)
        ctx.recoveries.erase(ctx.recoveries.begin() + remove);
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Discard all")) {
        for (const auto& r : ctx.recoveries)
            Autosave::discard(r);
        ctx.recoveries.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Decide later"))
        ctx.recoveries.clear(); // files stay on disk and are offered again next time
    if (ctx.recoveries.empty())
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace

void drawPreferences(AppContext& ctx)
{
    drawRecovery(ctx);
    if (!ctx.showPreferences)
        return;
    Preferences& p = ctx.prefs;
    ImGui::SetNextWindowSize(ImVec2(470, 520), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin(ICON_SETTINGS " Preferences", &open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        if (!open) {
            ctx.showPreferences = false;
            ctx.savePreferences();
        }
        return;
    }
    bool changed = false;

    if (ImGui::CollapsingHeader(ICON_MOUSE " Mouse", ImGuiTreeNodeFlags_DefaultOpen)) {
        int orbit = int(p.orbitButton), pan = int(p.panButton);
        ImGui::SetNextItemWidth(140);
        if (ImGui::Combo("Orbit button", &orbit, kButtons, 3)) {
            p.orbitButton = Preferences::MouseButton(orbit);
            if (p.panButton == p.orbitButton)
                p.panButton = p.orbitButton == Preferences::MouseButton::Middle ? Preferences::MouseButton::Right
                                                                                : Preferences::MouseButton::Middle;
            changed = true;
        }
        ImGui::SetNextItemWidth(140);
        if (ImGui::Combo("Pan button", &pan, kButtons, 3)) {
            p.panButton = Preferences::MouseButton(pan);
            if (p.panButton == p.orbitButton)
                p.orbitButton = p.panButton == Preferences::MouseButton::Right ? Preferences::MouseButton::Middle
                                                                               : Preferences::MouseButton::Right;
            changed = true;
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Left button stays for selection (Alt+Left orbits, Shift swaps orbit / pan).");
        ImGui::PopTextWrapPos();
        changed |= ImGui::Checkbox("Invert wheel zoom", &p.invertZoom);
        ImGui::SetNextItemWidth(140);
        changed |= ImGui::SliderFloat("Zoom speed", &p.zoomSpeed, 0.2f, 3.0f, "%.2fx");
    }

    if (ImGui::CollapsingHeader(ICON_PALETTE " Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        int theme = int(p.theme);
        ImGui::SetNextItemWidth(140);
        if (ImGui::Combo("Theme", &theme, "Dark\0Light\0")) {
            p.theme = Preferences::Theme(theme);
            applyTheme(p.theme == Preferences::Theme::Light);
            // Matching viewport background (unless the user picked custom colors).
            const glm::vec3 darkTop{0.23f, 0.25f, 0.29f}, darkBottom{0.11f, 0.12f, 0.14f};
            const glm::vec3 lightTop{0.93f, 0.94f, 0.96f}, lightBottom{0.70f, 0.73f, 0.78f};
            const bool light = p.theme == Preferences::Theme::Light;
            if (p.backgroundTop == (light ? darkTop : lightTop) && p.backgroundBottom == (light ? darkBottom : lightBottom)) {
                p.backgroundTop = light ? lightTop : darkTop;
                p.backgroundBottom = light ? lightBottom : darkBottom;
                ctx.applyPreferences();
            }
            changed = true;
        }
        ImGui::SetNextItemWidth(140);
        changed |= ImGui::SliderFloat("Font size", &p.fontSize, 11.0f, 24.0f, "%.0f px");
        ImGui::SameLine();
        ImGui::TextDisabled("(restart)");
        changed |= ImGui::Checkbox("Toolbar labels (off: icons only)", &p.toolbarLabels);
        bool colors = false;
        colors |= colorEdit("Background top", p.backgroundTop);
        ImGui::SameLine(230);
        colors |= colorEdit("Background bottom", p.backgroundBottom);
        colors |= colorEdit("Selection", p.selectionColor);
        ImGui::SameLine(230);
        colors |= colorEdit("Highlight", p.hoverColor);
        ImGui::SetNextItemWidth(140);
        colors |= ImGui::SliderFloat("Edge width", &p.edgeWidth, 0.5f, 4.0f, "%.1f px");
        if (ImGui::Button("Reset colors")) {
            const Preferences d;
            p.backgroundTop = d.backgroundTop;
            p.backgroundBottom = d.backgroundBottom;
            p.selectionColor = d.selectionColor;
            p.hoverColor = d.hoverColor;
            p.edgeWidth = d.edgeWidth;
            colors = true;
        }
        if (colors)
            ctx.applyPreferences();
        changed |= colors;
    }

    if (ImGui::CollapsingHeader(ICON_KEYBOARD " Units & precision", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(140);
        changed |= ImGui::SliderInt("Decimals", &p.decimals, 0, 6);
        ImGui::TextDisabled("Units: mm, N, MPa, t/mm3 (fixed). Example: %s mm",
                            [&] {
                                static char buf[32];
                                std::snprintf(buf, sizeof(buf), p.numberFormat().c_str(), 12.3456789);
                                return buf;
                            }());
    }

    if (ImGui::CollapsingHeader(ICON_SAVE " Files", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(140);
        changed |= ImGui::SliderInt("Autosave every", &p.autosaveMinutes, 0, 30,
                                    p.autosaveMinutes == 0 ? "off" : "%d min");
        ImGui::SetNextItemWidth(140);
        if (ImGui::SliderInt("Recent files", &p.maxRecentFiles, 1, 30)) {
            if (int(p.recentFiles.size()) > p.maxRecentFiles)
                p.recentFiles.resize(std::size_t(p.maxRecentFiles));
            changed = true;
        }
        if (ImGui::Button("Clear recent files")) {
            p.recentFiles.clear();
            changed = true;
        }
        ImGui::TextDisabled("Settings folder: %s", paths::toUtf8(paths::configDir()).c_str());
    }

    if (changed && !ImGui::IsAnyItemActive())
        ctx.savePreferences();
    ImGui::End();
    if (!open) {
        ctx.showPreferences = false;
        ctx.savePreferences();
    }
}

} // namespace cf::app::ui
