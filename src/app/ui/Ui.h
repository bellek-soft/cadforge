#pragma once
// Dear ImGui panels. Each panel is a free function over AppContext.

#include <imgui.h>

namespace cf::app {
class AppContext;
}

namespace cf::app::ui {

/// Style + fonts. `dpiScale` is the monitor content scale (1 on macOS, where
/// Retina is handled by the framebuffer scale).
void setupStyle(float dpiScale);

/// Creates the initial dock layout (only when no saved layout exists).
void buildDefaultLayout(ImGuiID dockspaceId);

void drawMainMenu(AppContext& ctx, bool& quitRequested);
void drawStatusBar(AppContext& ctx);
void drawModelTree(AppContext& ctx);
void drawProperties(AppContext& ctx);
void drawViewport(AppContext& ctx);
void drawConsole(AppContext& ctx);
void drawDialogs(AppContext& ctx, bool& quitRequested);
void handleShortcuts(AppContext& ctx, bool& quitRequested);

// Window names (also used by the default layout).
inline constexpr const char* kModelWindow = "Model";
inline constexpr const char* kPropertiesWindow = "Properties";
inline constexpr const char* kViewportWindow = "Viewport";
inline constexpr const char* kConsoleWindow = "Console";

} // namespace cf::app::ui
