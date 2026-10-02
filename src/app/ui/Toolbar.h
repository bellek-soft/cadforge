#pragma once

#include <imgui.h>

namespace cf::app::ui {

/// Horizontal toolbar that wraps onto the next line when the window is narrow.
class Toolbar {
public:
    Toolbar() : m_right(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) {}

    bool button(const char* label, const char* tooltip, bool active = false)
    {
        place(ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        const bool clicked = ImGui::Button(label);
        if (active)
            ImGui::PopStyleColor();
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    }
    void text(const char* t)
    {
        place(ImGui::CalcTextSize(t).x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", t);
    }
    void separator() { text("|"); }

private:
    void place(float width)
    {
        if (!m_first) {
            const float next = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width;
            if (next <= m_right)
                ImGui::SameLine();
        }
        m_first = false;
    }
    float m_right;
    bool m_first = true;
};

} // namespace cf::app::ui
