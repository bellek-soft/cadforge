// Measure tool: measures the current selection (one or two objects / faces / edges).

#include "app/ui/Ui.h"

#include "app/AppContext.h"
#include "geom/Measure.h"

#include <LucideIcons.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace cf::app::ui {

namespace {

struct MeasureState {
    std::vector<SelItem> items;
    std::uint64_t key = 0;
    geom::EntityMeasure entity[2];
    geom::DistanceMeasure distance;
};
MeasureState g_m;

std::uint64_t selectionKey(AppContext& ctx)
{
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (const auto& it : ctx.selection.items()) {
        mix(it.feature);
        mix(std::uint64_t(it.kind));
        mix(std::uint64_t(it.index));
        if (const auto* f = ctx.doc.find(it.feature))
            mix(f->resultKey());
    }
    return h;
}

void update(AppContext& ctx)
{
    const std::uint64_t key = selectionKey(ctx);
    if (key == g_m.key)
        return;
    g_m = {};
    g_m.key = key;
    for (const auto& it : ctx.selection.items())
        if (g_m.items.size() < 2)
            g_m.items.push_back(it);
    geom::MeasureTarget t[2];
    for (std::size_t i = 0; i < g_m.items.size(); ++i) {
        const auto* f = ctx.doc.find(g_m.items[i].feature);
        if (!f || f->shape().isNull())
            continue;
        t[i] = {&f->shape(), int(g_m.items[i].kind), g_m.items[i].index};
        g_m.entity[i] = geom::measureEntity(t[i]);
    }
    if (g_m.items.size() == 2 && t[0].shape && t[1].shape)
        g_m.distance = geom::measureDistance(t[0], t[1]);
}

std::string num(AppContext& ctx, double v, const char* unit)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), ctx.prefs.numberFormat().c_str(), std::abs(v) < 1e-12 ? 0.0 : v);
    return std::string(buf) + (unit[0] ? " " : "") + unit;
}

std::string vec(AppContext& ctx, const Vec3& v)
{
    return "(" + num(ctx, v.x, "") + ", " + num(ctx, v.y, "") + ", " + num(ctx, v.z, "") + ")";
}

void row(const char* k, const std::string& v)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", k);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(v.c_str());
}

void entityRows(AppContext& ctx, const geom::EntityMeasure& m)
{
    row("Type", m.typeName);
    if (m.kind == 2)
        row("Length", num(ctx, m.length, "mm"));
    if (m.kind == 1 || (m.kind == 0 && m.area > 0))
        row("Area", num(ctx, m.area, "mm2"));
    if (m.kind == 0 && m.volume > 0)
        row("Volume", num(ctx, m.volume, "mm3"));
    if (m.hasRadius) {
        row("Radius", num(ctx, m.radius, "mm"));
        row("Diameter", num(ctx, 2.0 * m.radius, "mm"));
    }
    if (m.hasDirection)
        row(m.kind == 1 && std::string(m.typeName) == "Plane" ? "Normal" : "Axis", vec(ctx, m.direction));
    row(m.hasRadius && m.kind == 2 ? "Center" : "Centroid", vec(ctx, m.center));
    if (m.kind == 0 && m.bounds.valid())
        row("Size (bbox)", vec(ctx, m.bounds.size()));
}

std::string label(AppContext& ctx, const SelItem& it)
{
    const auto* f = ctx.doc.find(it.feature);
    std::string s = f ? f->name() : "?";
    if (it.kind == render::PickKind::Face)
        s += "  Face" + std::to_string(it.index);
    else if (it.kind == render::PickKind::Edge)
        s += "  Edge" + std::to_string(it.index);
    return s;
}

} // namespace

void drawMeasureWindow(AppContext& ctx)
{
    if (!ctx.measureMode)
        return;
    update(ctx);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 400.0f, vp->WorkPos.y + 110.0f),
                            ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin(ICON_MEASURE " Measure", &open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (g_m.items.empty()) {
            ImGui::TextWrapped("Select an object, a face (key 2) or an edge (key 3). Ctrl+click a second item to "
                               "measure the distance and angle between them.");
        }
        const ImGuiTableFlags tf = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg;
        for (std::size_t i = 0; i < g_m.items.size(); ++i) {
            ImGui::SeparatorText(label(ctx, g_m.items[i]).c_str());
            if (!g_m.entity[i].valid) {
                ImGui::TextDisabled("(no geometry)");
                continue;
            }
            if (ImGui::BeginTable(i ? "##m1" : "##m0", 2, tf)) {
                entityRows(ctx, g_m.entity[i]);
                ImGui::EndTable();
            }
        }
        if (g_m.distance.valid) {
            ImGui::SeparatorText("Between");
            if (ImGui::BeginTable("##md", 2, tf)) {
                row("Distance", num(ctx, g_m.distance.distance, "mm"));
                row("dX, dY, dZ", vec(ctx, g_m.distance.delta));
                if (g_m.distance.hasAngle)
                    row("Angle", num(ctx, g_m.distance.angleDeg, "deg"));
                ImGui::EndTable();
            }
        }
        if (ctx.selection.items().size() > 2)
            ImGui::TextDisabled("Only the first two selected items are measured.");
    }
    ImGui::End();
    if (!open)
        ctx.measureMode = false;
}

void drawMeasureOverlay(AppContext& ctx, ImDrawList* dl, const ImVec2& origin)
{
    if (!ctx.measureMode)
        return;
    update(ctx);
    const ImVec2 fb = ImGui::GetIO().DisplayFramebufferScale;
    auto screen = [&](const Vec3& w, bool& ok) {
        const Vec3 p = ctx.camera.project(w);
        ok = p.z > -1.0 && p.z < 1.0;
        return ImVec2(origin.x + float(p.x) / fb.x, origin.y + float(p.y) / fb.y);
    };
    const ImU32 col = IM_COL32(255, 220, 90, 255), shadow = IM_COL32(10, 10, 12, 220);
    for (std::size_t i = 0; i < g_m.items.size(); ++i) {
        if (!g_m.entity[i].valid)
            continue;
        bool ok = false;
        const ImVec2 c = screen(g_m.entity[i].center, ok);
        if (!ok)
            continue;
        dl->AddCircle(c, 6.0f, shadow, 16, 3.5f);
        dl->AddCircle(c, 6.0f, col, 16, 1.5f);
    }
    if (!g_m.distance.valid)
        return;
    bool okA = false, okB = false;
    const ImVec2 a = screen(g_m.distance.pointA, okA), b = screen(g_m.distance.pointB, okB);
    if (!okA || !okB)
        return;
    dl->AddLine(a, b, shadow, 4.0f);
    dl->AddLine(a, b, col, 2.0f);
    for (const ImVec2& p : {a, b}) {
        dl->AddCircleFilled(p, 4.5f, shadow);
        dl->AddCircleFilled(p, 3.0f, col);
    }
    std::string text = num(ctx, g_m.distance.distance, "mm");
    if (g_m.distance.hasAngle)
        text += "   " + num(ctx, g_m.distance.angleDeg, "deg");
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    const ImVec2 m((a.x + b.x) * 0.5f + 8.0f, (a.y + b.y) * 0.5f - ts.y - 6.0f);
    dl->AddRectFilled(ImVec2(m.x - 5, m.y - 3), ImVec2(m.x + ts.x + 5, m.y + ts.y + 3), IM_COL32(18, 20, 24, 225), 4.0f);
    dl->AddText(m, col, text.c_str());
}

} // namespace cf::app::ui
