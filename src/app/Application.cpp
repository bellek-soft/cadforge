#include "app/Application.h"

#include "app/AppContext.h"
#include "app/Commands.h"
#include "app/ui/Ui.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <ImGuizmo.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace cf::app {

namespace {

struct WindowState {
    AppContext* ctx = nullptr;
    bool quitRequested = false;
    std::vector<std::string> dropped;
};

void glfwErrorCallback(int code, const char* desc)
{
    log::error("GLFW error ", code, ": ", desc);
}

bool savePpm(const std::string& path, int w, int h)
{
    std::vector<unsigned char> px(static_cast<std::size_t>(w * h * 3));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream out(paths::fromUtf8(path), std::ios::binary);
    if (!out)
        return false;
    out << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y)
        out.write(reinterpret_cast<const char*>(px.data() + static_cast<std::size_t>(y * w * 3)), w * 3);
    return bool(out);
}

std::string lowerExt(const std::string& path)
{
    std::string e = paths::fromUtf8(path).extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e;
}

void openAny(AppContext& ctx, const std::string& path)
{
    const std::string ext = lowerExt(path);
    if (ext == ".step" || ext == ".stp")
        ctx.importStep(path);
    else
        ctx.guardUnsaved([&ctx, path] { ctx.openDocument(path); });
}

} // namespace

int Application::run(const AppOptions& opt)
{
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        log::error("Failed to initialize GLFW");
        return 1;
    }

    // OpenGL 4.1 core: the newest version available on macOS, widely supported elsewhere.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif

    int width = opt.width, height = opt.height;
    if (width <= 0 || height <= 0) {
        width = 1600;
        height = 1000;
        int mx = 0, my = 0, mw = 0, mh = 0;
        if (GLFWmonitor* mon = glfwGetPrimaryMonitor())
            glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
        if (mw > 0 && mh > 0) {
            width = std::min(width, int(mw * 0.9));
            height = std::min(height, int(mh * 0.9));
        }
    }

    GLFWwindow* window = glfwCreateWindow(width, height, "CadForge", nullptr, nullptr);
    if (!window) {
        log::error("Failed to create an OpenGL 4.1 core window. Please update your graphics driver.");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    const int glVersion = gladLoadGL(glfwGetProcAddress);
    if (!glVersion || GLAD_VERSION_MAJOR(glVersion) * 10 + GLAD_VERSION_MINOR(glVersion) < 41) {
        log::error("OpenGL 4.1 is required");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    log::info("OpenGL ", reinterpret_cast<const char*>(glGetString(GL_VERSION)), " on ",
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    // ---- Dear ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    static const std::string iniPath = paths::toUtf8(paths::configDir() / "layout.ini");
    const bool firstRun = !std::filesystem::exists(paths::fromUtf8(iniPath)) || !opt.screenshotPath.empty();
    io.IniFilename = opt.screenshotPath.empty() ? iniPath.c_str() : nullptr;

    float dpiScale = 1.0f;
#if !defined(__APPLE__)
    {
        float xs = 1.0f, ys = 1.0f;
        glfwGetWindowContentScale(window, &xs, &ys);
        dpiScale = std::max(1.0f, xs);
    }
#endif
    ui::setupStyle(dpiScale);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");

    // ---- application state ----
    auto ctx = std::make_unique<AppContext>();
    if (!ctx->renderer.init()) {
        log::error("Renderer initialization failed");
        return 1;
    }
    ctx->camera.setStandardView(render::StandardView::Isometric, false);

    WindowState ws;
    ws.ctx = ctx.get();
    glfwSetWindowUserPointer(window, &ws);
    glfwSetWindowCloseCallback(window, [](GLFWwindow* w) {
        auto* s = static_cast<WindowState*>(glfwGetWindowUserPointer(w));
        glfwSetWindowShouldClose(w, GLFW_FALSE); // we decide after the unsaved-changes check
        s->quitRequested = true;
    });
    glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths) {
        auto* s = static_cast<WindowState*>(glfwGetWindowUserPointer(w));
        for (int i = 0; i < count; ++i)
            s->dropped.emplace_back(paths[i]);
    });

    if (opt.demo)
        cmd::loadDemo(*ctx);
    if (!opt.openPath.empty())
        openAny(*ctx, opt.openPath);
    if (!opt.demo && opt.openPath.empty())
        ctx->status("Welcome to CadForge. Create a primitive or open File > Load Demo Scene.");

    // ---- main loop ----
    double lastTime = glfwGetTime();
    int activeFrames = 5;
    int frame = 0;
    bool layoutBuilt = false;
    std::string lastTitle;

    while (!ctx->quitConfirmed) {
        // Event-driven redraw: sleep while idle, render continuously while interacting.
        const bool busy = ImGui::IsAnyMouseDown() || ctx->camera.update(0.0) || !opt.screenshotPath.empty();
        if (busy || activeFrames > 0) {
            glfwPollEvents();
            --activeFrames;
        } else {
            const double t0 = glfwGetTime();
            glfwWaitEventsTimeout(0.5);
            if (glfwGetTime() - t0 < 0.49)
                activeFrames = 6; // woken by an event: let ImGui settle for a few frames
        }
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        const double now = glfwGetTime();
        const double dt = now - lastTime;
        lastTime = now;
        if (ctx->camera.update(dt))
            activeFrames = std::max(activeFrames, 2);

        for (const auto& p : ws.dropped)
            openAny(*ctx, p);
        ws.dropped.clear();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        ui::drawMainMenu(*ctx, ws.quitRequested);
        ui::drawStatusBar(*ctx);
        const ImGuiID dockId = ImGui::GetID("MainDockSpace");
        if (firstRun && !layoutBuilt)
            ui::buildDefaultLayout(dockId);
        layoutBuilt = true;
        ImGui::DockSpaceOverViewport(dockId, ImGui::GetMainViewport());

        ui::handleShortcuts(*ctx, ws.quitRequested);
        ui::drawModelTree(*ctx);
        ui::drawProperties(*ctx);
        ui::drawViewport(*ctx);
        ui::drawConsole(*ctx);
        ui::drawDialogs(*ctx, ws.quitRequested);

        const std::string title = ctx->windowTitle();
        if (title != lastTitle) {
            glfwSetWindowTitle(window, title.c_str());
            lastTitle = title;
        }

        ImGui::Render();
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.1f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (!opt.screenshotPath.empty() && ++frame >= opt.screenshotFrames) {
            if (savePpm(opt.screenshotPath, fbw, fbh))
                log::info("Screenshot written to ", opt.screenshotPath);
            ctx->quitConfirmed = true;
        }
        glfwSwapBuffers(window);
    }

    // GL resources must be released while the context is alive.
    ctx.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace cf::app
