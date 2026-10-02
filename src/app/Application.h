#pragma once

#include <string>

namespace cf::app {

struct AppOptions {
    std::string openPath;        // project (.cfp) or STEP file to open at startup
    bool demo = false;           // load the demo scene
    bool feaDemo = false;        // load (and solve) the analysis demo
    bool sketchDemo = false;     // load the sketch / extrude / revolve demo
    bool editSketch = false;     // start editing the first sketch (docs / testing)
    std::string screenshotPath;  // render N frames, save a PPM screenshot, exit (CI / docs)
    int screenshotFrames = 30;
    int width = 0, height = 0;   // 0 = choose from the monitor size
};

/// Owns the window, the OpenGL context, Dear ImGui and the main loop.
class Application {
public:
    int run(const AppOptions& options);
};

} // namespace cf::app
