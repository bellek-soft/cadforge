#include "app/Application.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
void usage()
{
    std::printf("Usage: CadForge [file.cfp | file.step] [--demo] [--size WxH]\n"
                "                [--screenshot out.ppm [--frames N]]\n");
}
} // namespace

int main(int argc, char** argv)
{
    cf::app::AppOptions opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--demo") {
            opt.demo = true;
        } else if (a == "--screenshot" && i + 1 < argc) {
            opt.screenshotPath = argv[++i];
        } else if (a == "--frames" && i + 1 < argc) {
            opt.screenshotFrames = std::atoi(argv[++i]);
        } else if (a == "--size" && i + 1 < argc) {
            std::sscanf(argv[++i], "%dx%d", &opt.width, &opt.height);
        } else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (!a.empty() && a[0] != '-') {
            opt.openPath = a;
        } else {
            usage();
            return 1;
        }
    }
    return cf::app::Application().run(opt);
}
