#include "app/ui/Ui.h"

#include "app/AppContext.h"
#include "app/Commands.h"
#include "app/FileDialogs.h"
#include "core/Paths.h"

#include <glad/gl.h>

#include <cctype>
#include <cstring>

namespace cf::app::ui {

namespace {

std::string withExtension(std::string path, const std::string& ext)
{
    if (ext.empty())
        return path;
    auto lower = [](std::string s) {
        for (auto& ch : s)
            ch = char(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };
    const std::string l = lower(path);
    if (l.size() >= ext.size() && l.compare(l.size() - ext.size(), ext.size(), lower(ext)) == 0)
        return path;
    // Accept the common alternative spelling of STEP files.
    if (ext == ".step" && l.size() >= 4 && l.compare(l.size() - 4, 4, ".stp") == 0)
        return path;
    return path + ext;
}

// Fallback text-input dialog when no native dialog is available.
struct FallbackDialog {
    bool open = false;
    FileRequest request;
    char path[1024] = {};
};
FallbackDialog g_fallback;

void processFileRequests(AppContext& ctx)
{
    if (ctx.fileRequests.empty() || g_fallback.open)
        return;
    FileRequest req = std::move(ctx.fileRequests.front());
    ctx.fileRequests.erase(ctx.fileRequests.begin());

    if (dialogs::available()) {
        std::optional<std::string> path;
        if (req.kind == FileRequest::Kind::Open) {
            path = dialogs::openFile(req.title, req.filters);
        } else {
            std::string def = ctx.filePath.empty() ? "untitled" + req.defaultExtension : ctx.filePath;
            if (req.defaultExtension != ".cfp" && !ctx.filePath.empty())
                def = paths::toUtf8(paths::fromUtf8(ctx.filePath).replace_extension(req.defaultExtension));
            path = dialogs::saveFile(req.title, def, req.filters);
        }
        if (path && !path->empty())
            req.onAccept(req.kind == FileRequest::Kind::Save ? withExtension(*path, req.defaultExtension) : *path);
        else
            ctx.pendingAfterConfirm = nullptr;
        return;
    }
    g_fallback.open = true;
    g_fallback.request = std::move(req);
    std::memset(g_fallback.path, 0, sizeof(g_fallback.path));
    ImGui::OpenPopup("File##fallback");
}

void drawFallbackDialog(AppContext& ctx)
{
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("File##fallback", nullptr, ImGuiWindowFlags_NoSavedSettings))
        return;
    auto& req = g_fallback.request;
    ImGui::TextUnformatted(req.title.c_str());
    ImGui::TextDisabled("No native file dialog available (install zenity or kdialog). Enter a path:");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##path", g_fallback.path, sizeof(g_fallback.path),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    if ((ImGui::Button("OK", ImVec2(100, 0)) || enter) && g_fallback.path[0]) {
        std::string p = g_fallback.path;
        if (req.kind == FileRequest::Kind::Save)
            p = withExtension(p, req.defaultExtension);
        g_fallback.open = false;
        ImGui::CloseCurrentPopup();
        req.onAccept(p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        g_fallback.open = false;
        ctx.pendingAfterConfirm = nullptr;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawUnsavedDialog(AppContext& ctx)
{
    if (ctx.confirmUnsavedOpen) {
        ImGui::OpenPopup("Unsaved changes");
        ctx.confirmUnsavedOpen = false;
    }
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted("The document has unsaved changes.");
    ImGui::TextUnformatted("Do you want to save them first?");
    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(100, 0))) {
        ImGui::CloseCurrentPopup();
        if (ctx.filePath.empty()) {
            cmd::saveAs(ctx); // continues with the pending action after saving
        } else if (ctx.saveDocument(ctx.filePath) && ctx.pendingAfterConfirm) {
            auto next = std::move(ctx.pendingAfterConfirm);
            ctx.pendingAfterConfirm = nullptr;
            next();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(100, 0))) {
        ImGui::CloseCurrentPopup();
        if (ctx.pendingAfterConfirm) {
            auto next = std::move(ctx.pendingAfterConfirm);
            ctx.pendingAfterConfirm = nullptr;
            next();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ctx.pendingAfterConfirm = nullptr;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawAbout(AppContext& ctx)
{
    if (!ctx.showAbout)
        return;
    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("About CadForge", &ctx.showAbout, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("CadForge 0.1");
        ImGui::TextWrapped("Parametric 3D CAD built on OpenCASCADE (geometry kernel), OpenGL 4.1 "
                           "(rendering) and Dear ImGui (user interface).");
        ImGui::Separator();
        ImGui::TextDisabled("Units: millimeters");
        ImGui::TextDisabled("OpenGL: %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        ImGui::TextDisabled("Renderer: %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    }
    ImGui::End();
}

void drawControls(AppContext& ctx)
{
    if (!ctx.showControls)
        return;
    ImGui::SetNextWindowSize(ImVec2(470, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Mouse & Keyboard", &ctx.showControls, ImGuiWindowFlags_NoDocking)) {
        if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            auto row = [](const char* a, const char* b) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(a);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", b);
            };
            row("Orbit", "Right drag  /  Alt + Left drag  /  Shift + Middle drag");
            row("Pan", "Middle drag  /  Shift + Right drag");
            row("Zoom", "Mouse wheel (zooms to the cursor)");
            row("Select", "Left click; Ctrl/Shift/Cmd + click to add");
            row("Selection mode", "1 objects, 2 faces, 3 edges");
            row("Gizmo", "W move, E rotate, Q off (hold Ctrl to snap)");
            row("Booleans", "U union, X cut, N intersect (first selected = base)");
            row("Fillet / Chamfer", "Shift+F / Shift+C on selected edges");
            row("Views", "Keypad 0 iso, 1 front, 3 right, 7 top (Ctrl = opposite)");
            row("Fit", "F all, Shift+V selection");
            row("Projection / grid", "O or Keypad 5 / G");
            row("Undo / Redo", "Ctrl+Z / Ctrl+Shift+Z (Cmd on macOS)");
            row("Delete", "Del / Backspace");
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace

void drawDialogs(AppContext& ctx, bool& quitRequested)
{
    if (quitRequested) {
        quitRequested = false;
        ctx.guardUnsaved([&ctx] { ctx.quitConfirmed = true; });
    }
    drawUnsavedDialog(ctx);
    processFileRequests(ctx);
    drawFallbackDialog(ctx);
    drawAbout(ctx);
    drawControls(ctx);
    if (ctx.showImGuiDemo)
        ImGui::ShowDemoWindow(&ctx.showImGuiDemo);
}

} // namespace cf::app::ui
