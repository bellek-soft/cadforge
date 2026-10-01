#include "app/ui/Ui.h"

#include "core/Log.h"

#include <imgui_internal.h>

#include <filesystem>

namespace cf::app::ui {

namespace {

void loadFont()
{
    ImGuiIO& io = ImGui::GetIO();
    // Use a good-looking system UI font when available; fall back to ImGui's
    // built-in font otherwise. No font files are shipped -> no dependency.
    const char* candidates[] = {
#if defined(_WIN32)
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
#else
        "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
#endif
    };
    for (const char* path : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            if (io.Fonts->AddFontFromFileTTF(path, 15.0f)) {
                log::info("UI font: ", path);
                return;
            }
        }
    }
    io.Fonts->AddFontDefault();
}

} // namespace

void setupStyle(float dpiScale)
{
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4.0f;
    s.ChildRounding = 4.0f;
    s.FrameRounding = 4.0f;
    s.PopupRounding = 4.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 4.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(8, 8);
    s.FramePadding = ImVec2(7, 4);
    s.ItemSpacing = ImVec2(8, 5);
    s.ItemInnerSpacing = ImVec2(5, 4);
    s.IndentSpacing = 16.0f;
    s.ScrollbarSize = 13.0f;
    s.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* c = s.Colors;
    const ImVec4 bg0(0.105f, 0.115f, 0.13f, 1.0f);
    const ImVec4 bg1(0.14f, 0.15f, 0.17f, 1.0f);
    const ImVec4 bg2(0.19f, 0.205f, 0.23f, 1.0f);
    const ImVec4 bg3(0.25f, 0.27f, 0.30f, 1.0f);
    const ImVec4 accent(0.29f, 0.56f, 0.89f, 1.0f);
    const ImVec4 accentDim(0.29f, 0.56f, 0.89f, 0.55f);

    c[ImGuiCol_Text] = ImVec4(0.90f, 0.91f, 0.93f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.53f, 0.57f, 1.0f);
    c[ImGuiCol_WindowBg] = bg1;
    c[ImGuiCol_ChildBg] = bg1;
    c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.13f, 0.15f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.07f, 0.075f, 0.085f, 1.0f);
    c[ImGuiCol_FrameBg] = bg0;
    c[ImGuiCol_FrameBgHovered] = bg2;
    c[ImGuiCol_FrameBgActive] = bg3;
    c[ImGuiCol_TitleBg] = bg0;
    c[ImGuiCol_TitleBgActive] = bg0;
    c[ImGuiCol_MenuBarBg] = bg0;
    c[ImGuiCol_ScrollbarBg] = bg1;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Button] = bg2;
    c[ImGuiCol_ButtonHovered] = bg3;
    c[ImGuiCol_ButtonActive] = accentDim;
    c[ImGuiCol_Header] = ImVec4(0.29f, 0.56f, 0.89f, 0.30f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.29f, 0.56f, 0.89f, 0.40f);
    c[ImGuiCol_HeaderActive] = accentDim;
    c[ImGuiCol_Separator] = ImVec4(0.07f, 0.075f, 0.085f, 1.0f);
    c[ImGuiCol_Tab] = bg0;
    c[ImGuiCol_TabHovered] = bg2;
    c[ImGuiCol_TabSelected] = bg1;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = bg0;
    c[ImGuiCol_TabDimmedSelected] = bg1;
    c[ImGuiCol_DockingPreview] = accentDim;
    c[ImGuiCol_DockingEmptyBg] = bg0;
    c[ImGuiCol_TableHeaderBg] = bg0;
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_NavCursor] = accent;

    if (dpiScale > 0.0f && dpiScale != 1.0f) {
        s.ScaleAllSizes(dpiScale);
        s.FontScaleDpi = dpiScale;
    }
    loadFont();
}

void buildDefaultLayout(ImGuiID dockspaceId)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.18f, nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.55f, nullptr, &left);

    ImGui::DockBuilderDockWindow(kModelWindow, left);
    ImGui::DockBuilderDockWindow(kPropertiesWindow, leftBottom);
    ImGui::DockBuilderDockWindow(kViewportWindow, center);
    ImGui::DockBuilderDockWindow(kConsoleWindow, bottom);

    if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(center))
        n->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    ImGui::DockBuilderFinish(dockspaceId);
}

} // namespace cf::app::ui
