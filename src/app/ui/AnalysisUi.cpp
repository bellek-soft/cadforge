// Analysis (FEA) user interface: toolbar/menu commands, the study panel and
// viewport overlays (support/load glyphs, result legend, progress).

#include "app/ui/Toolbar.h"
#include "app/ui/Ui.h"

#include "app/AppContext.h"
#include "app/FileDialogs.h"
#include "fea/Mesher.h"
#include "fea/Post.h"
#include "model/features/FeaFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cf::app::ui {

using model::FeaBoundaryFeature;
using model::StaticAnalysisFeature;

namespace {

/// The analysis toolbar/menu commands act on: the one in context, or the only one.
FeatureId commandAnalysis(AppContext& ctx)
{
    if (FeatureId a = ctx.contextAnalysis(); a != kNoFeature)
        return a;
    FeatureId only = kNoFeature;
    for (const auto& f : ctx.doc.features())
        if (f->type() == StaticAnalysisFeature::kType) {
            if (only != kNoFeature)
                return kNoFeature; // ambiguous
            only = f->id();
        }
    return only;
}

void solveCommand(AppContext& ctx)
{
    const FeatureId a = commandAnalysis(ctx);
    if (a == kNoFeature)
        ctx.status("Solve: select the analysis to solve", true);
    else
        ctx.solveAnalysis(a);
}

void exportVtkCommand(AppContext& ctx, FeatureId analysis)
{
    ctx.requestFile({FileRequest::Kind::Save, "Export results (VTK)", {"VTK unstructured grid (*.vtu)", "*.vtu"},
                     ".vtu", [&ctx, analysis](const std::string& path) {
                         std::string err;
                         if (ctx.fea.exportVtu(analysis, path, err))
                             ctx.status("Exported " + path + " (open it in ParaView)");
                         else
                             ctx.status("VTK export failed: " + err, true);
                     }});
}

const std::vector<fea::ResultField>& fieldsFor(const AnalysisRuntime* rt)
{
    static const std::vector<fea::ResultField> all = {
        fea::ResultField::VonMises, fea::ResultField::DisplacementMagnitude, fea::ResultField::DisplacementX,
        fea::ResultField::DisplacementY, fea::ResultField::DisplacementZ};
    static const std::vector<fea::ResultField> modal(all.begin() + 1, all.end());
    return rt && rt->modal ? modal : all;
}

ImU32 toU32(const glm::vec3& c, float a = 1.0f)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, a));
}

std::string num(double v, const char* unit)
{
    if (std::abs(v) < 1e-9)
        v = 0.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4g %s", v, unit);
    return buf;
}

void arrowShape(ImDrawList* dl, ImVec2 tail, ImVec2 tip, ImU32 col, float thickness, float grow);

/// Arrow with a dark outline so it stays visible on top of colored faces.
void arrow(ImDrawList* dl, ImVec2 tail, ImVec2 tip, ImU32 col, float thickness = 2.5f)
{
    arrowShape(dl, tail, tip, IM_COL32(12, 12, 14, 220), thickness + 2.5f, 2.0f);
    arrowShape(dl, tail, tip, col, thickness, 0.0f);
}

void arrowShape(ImDrawList* dl, ImVec2 tail, ImVec2 tip, ImU32 col, float thickness, float grow)
{
    const float dx = tip.x - tail.x, dy = tip.y - tail.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f)
        return;
    const float ux = dx / len, uy = dy / len;
    const float head = std::min(12.0f, len * 0.45f) + grow;
    const ImVec2 base(tip.x - ux * head, tip.y - uy * head);
    dl->AddLine(tail, base, col, thickness);
    dl->AddTriangleFilled(tip, ImVec2(base.x - uy * head * 0.45f, base.y + ux * head * 0.45f),
                          ImVec2(base.x + uy * head * 0.45f, base.y - ux * head * 0.45f), col);
}

void outlinedText(ImDrawList* dl, ImVec2 p, ImU32 col, const char* text)
{
    const ImU32 shadow = IM_COL32(10, 10, 12, 200);
    dl->AddText(ImVec2(p.x + 1, p.y + 1), shadow, text);
    dl->AddText(p, col, text);
}

} // namespace

// ---- toolbar & menu ------------------------------------------------------------------

void drawAnalysisToolbar(AppContext& ctx, Toolbar& tb)
{
    if (tb.button("Study", "New static analysis of the selected solid"))
        ctx.createAnalysisFromSelection();
    if (tb.button("Fixed", "Fixed support on the selected faces"))
        ctx.createBoundaryFromSelection(model::FixedSupportFeature::kType);
    if (tb.button("Force", "Force on the selected faces"))
        ctx.createBoundaryFromSelection(model::ForceFeature::kType);
    if (tb.button("Pressure", "Pressure on the selected faces"))
        ctx.createBoundaryFromSelection(model::PressureFeature::kType);
    if (tb.button("Refine", "Smaller elements on the selected faces"))
        ctx.createBoundaryFromSelection(model::MeshRefinementFeature::kType);
    ImGui::BeginDisabled(ctx.fea.busy());
    if (tb.button("Solve", "Mesh and solve the analysis (F5)"))
        solveCommand(ctx);
    ImGui::EndDisabled();
}

void drawAnalysisMenu(AppContext& ctx)
{
    if (!ImGui::BeginMenu("Analysis"))
        return;
    auto& fea = ctx.fea;
    if (ImGui::MenuItem("New Analysis"))
        ctx.createAnalysisFromSelection();
    ImGui::Separator();
    if (ImGui::MenuItem("Fixed Support"))
        ctx.createBoundaryFromSelection(model::FixedSupportFeature::kType);
    if (ImGui::MenuItem("Force"))
        ctx.createBoundaryFromSelection(model::ForceFeature::kType);
    if (ImGui::MenuItem("Pressure"))
        ctx.createBoundaryFromSelection(model::PressureFeature::kType);
    if (ImGui::MenuItem("Mesh Refinement"))
        ctx.createBoundaryFromSelection(model::MeshRefinementFeature::kType);
    ImGui::Separator();
    const FeatureId a = commandAnalysis(ctx);
    if (ImGui::MenuItem("Mesh", nullptr, false, a != kNoFeature && !fea.busy()))
        ctx.meshAnalysis(a);
    if (ImGui::MenuItem("Solve", "F5", false, a != kNoFeature && !fea.busy()))
        ctx.solveAnalysis(a);
    if (ImGui::MenuItem("Cancel", nullptr, false, fea.busy()))
        fea.cancel();
    if (ImGui::MenuItem("Export Results (VTK)...", nullptr, false,
                        a != kNoFeature && fea.runtime(a) && fea.runtime(a)->mesh))
        exportVtkCommand(ctx, a);
    ImGui::Separator();
    ImGui::MenuItem("Section View", nullptr, &fea.section);
    ImGui::MenuItem("Probe", nullptr, &fea.probeMode);
    ImGui::MenuItem("Show Results", nullptr, &fea.showResults);
    ImGui::MenuItem("Show Mesh Edges", nullptr, &fea.showMeshEdges);
    if (ImGui::BeginMenu("Result Field")) {
        for (auto f : {fea::ResultField::VonMises, fea::ResultField::DisplacementMagnitude,
                       fea::ResultField::DisplacementX, fea::ResultField::DisplacementY,
                       fea::ResultField::DisplacementZ})
            if (ImGui::MenuItem(fea::fieldName(f), nullptr, fea.field == f))
                fea.field = f;
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Hide Analysis View", nullptr, false, fea.shownAnalysis != kNoFeature))
        fea.shownAnalysis = kNoFeature;
    ImGui::EndMenu();
}

// ---- study panel ---------------------------------------------------------------------

void drawAnalysisPanel(AppContext& ctx, model::Feature& f)
{
    auto& fea = ctx.fea;
    const FeatureId id = f.id();
    const bool busy = fea.busy();
    const bool mine = fea.busyAnalysis() == id;

    if (!ImGui::CollapsingHeader("Study", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (ctx.doc.nestedChildren(id).empty())
        ImGui::TextWrapped("Select faces of the solid (key 2), then add Fixed supports and Force / Pressure "
                           "loads from the toolbar.");

    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Mesh"))
        ctx.meshAnalysis(id);
    ImGui::SameLine();
    if (ImGui::Button("Solve"))
        ctx.solveAnalysis(id);
    ImGui::EndDisabled();
    if (mine) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            fea.cancel();
        ImGui::ProgressBar(float(fea.progress()), ImVec2(-FLT_MIN, 0), fea.progressText().c_str());
    }

    const AnalysisRuntime* rt = fea.runtime(id);
    if (rt && !rt->message.empty() && !mine) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.45f, 1.0f));
        ImGui::TextWrapped("%s", rt->message.c_str());
        ImGui::PopStyleColor();
    }

    if (rt && rt->mesh) {
        const auto& m = *rt->mesh;
        ImGui::TextDisabled("Mesh: %zu nodes, %zu %s elements (%.2f s)%s", m.nodeCount(), m.elementCount(),
                            m.nodesPerElement == 10 ? "Tet10" : "Tet4", rt->meshSeconds,
                            fea.meshUpToDate(ctx.doc, id) ? "" : "  [outdated]");
    } else {
        ImGui::TextDisabled("Mesher: %s", fea::mesherName().c_str());
    }

    if (rt && rt->result && rt->modal) {
        const auto& m = *rt->modal;
        if (!fea.resultUpToDate(ctx.doc, id))
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Results are outdated - solve again");
        ImGui::TextDisabled("%d equations, assembly %.2f s, eigen solve %.2f s", m.equations, m.assemblySeconds,
                            m.solveSeconds);
        if (ImGui::BeginTable("##modes", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                                                ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Mode");
            ImGui::TableSetupColumn("Freq. [Hz]");
            ImGui::TableSetupColumn("Mx %");
            ImGui::TableSetupColumn("My %");
            ImGui::TableSetupColumn("Mz %");
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < m.frequencies.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                char label[32];
                std::snprintf(label, sizeof(label), "%zu", i + 1);
                if (ImGui::Selectable(label, rt->mode == int(i), ImGuiSelectableFlags_SpanAllColumns)) {
                    fea.selectMode(id, int(i));
                    fea.shownAnalysis = id;
                    fea.showResults = true;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.5g", m.frequencies[i]);
                const Vec3& r = m.effectiveMassRatio[i];
                for (int c = 0; c < 3; ++c) {
                    ImGui::TableSetColumnIndex(2 + c);
                    ImGui::Text("%.1f", r[c] * 100.0);
                }
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Effective mass in %% of the free mass (%.4g t)", m.totalMass);
    } else if (rt && rt->result) {
        const auto& r = *rt->result;
        if (!fea.resultUpToDate(ctx.doc, id))
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Results are outdated - solve again");
        const auto* a = dynamic_cast<const StaticAnalysisFeature*>(&f);
        const double yield = a ? a->material().yieldStrength : 0.0;
        if (ImGui::BeginTable("##res", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            auto row = [](const char* k, const std::string& v) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", k);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(v.c_str());
            };
            char buf[160];
            row("Max stress (vM)", num(r.maxVonMises, "MPa"));
            row("Max displ.", num(r.maxDisplacement, "mm"));
            if (yield > 0 && r.maxVonMises > 0) {
                const double sf = yield / r.maxVonMises;
                std::snprintf(buf, sizeof(buf), "%.3g%s", sf, sf < 1.0 ? "  (yields!)" : "");
                row("Safety factor", buf);
            }
            auto vec = [&](const Vec3& v) {
                const double tiny = std::max(glm::length(v), 1.0) * 1e-9;
                auto c = [&](double x) { return std::abs(x) < tiny ? 0.0 : x; };
                std::snprintf(buf, sizeof(buf), "%.4g, %.4g, %.4g N", c(v.x), c(v.y), c(v.z));
                return std::string(buf);
            };
            row("Applied load", vec(r.appliedLoad));
            row("Reaction", vec(r.reaction));
            std::snprintf(buf, sizeof(buf), "%d (%s)", r.equations, r.solver.c_str());
            row("Equations", buf);
            std::snprintf(buf, sizeof(buf), "assembly %.2f s, solve %.2f s", r.assemblySeconds, r.solveSeconds);
            row("Time", buf);
            ImGui::EndTable();
        }
    }

    if (rt && rt->mesh) {
        bool shown = fea.shownAnalysis == id;
        if (ImGui::Checkbox("Show in viewport", &shown))
            fea.shownAnalysis = shown ? id : kNoFeature;
        if (rt->result) {
            ImGui::SameLine();
            ImGui::Checkbox("Results", &fea.showResults);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (rt->modal && fea.field == fea::ResultField::VonMises)
                fea.field = fea::ResultField::DisplacementMagnitude;
            if (ImGui::BeginCombo("##field", fea::fieldName(fea.field))) {
                for (auto fld : fieldsFor(rt))
                    if (ImGui::Selectable(fea::fieldName(fld), fea.field == fld))
                        fea.field = fld;
                ImGui::EndCombo();
            }
            ImGui::Checkbox("Auto deformation", &fea.autoDeformation);
            if (!fea.autoDeformation) {
                float s = float(fea.deformationScale);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##scale", &s, std::max(0.01f, s * 0.02f), 0.0f, 1e7f, "scale x%.3g",
                                     ImGuiSliderFlags_Logarithmic))
                    fea.deformationScale = s;
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("x%.3g", fea.effectiveDeformationScale(id));
            }
        }
        ImGui::Checkbox("Mesh edges", &fea.showMeshEdges);

        // Section view
        ImGui::Checkbox("Section", &fea.section);
        if (fea.section) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            ImGui::Combo("##axis", &fea.sectionAxis, "X\0Y\0Z\0");
            ImGui::SameLine();
            ImGui::Checkbox("Flip", &fea.sectionFlip);
            float pos = float(fea.sectionPosition);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderFloat("##secpos", &pos, 0.0f, 1.0f, "position %.3f"))
                fea.sectionPosition = pos;
        }
        // Probe
        if (rt->result) {
            ImGui::Checkbox("Probe (click to pin values)", &fea.probeMode);
            if (!fea.probes.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear"))
                    fea.probes.clear();
            }
        }
        if (ImGui::Button("Export VTK..."))
            exportVtkCommand(ctx, id);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Mesh and results as a VTK unstructured grid (.vtu) for ParaView");
    }
}

bool analysisProbe(AppContext& ctx, const ImVec2& origin, const ImVec2& size, bool hovered, bool clicked)
{
    auto& fea = ctx.fea;
    const FeatureId shown = fea.shownAnalysis;
    const AnalysisRuntime* rt = fea.runtime(shown);
    if (!fea.probeMode || !rt || !rt->result || !fea.showResults || !fea.meshUpToDate(ctx.doc, shown))
        return false;
    float range[2];
    const MeshData* surf = fea.displaySurface(shown, range);
    if (!surf || surf->scalars.empty())
        return false;
    const std::uint64_t key = fea.displayKey(shown);
    if (key != fea.probesKey) {
        fea.probes.clear();
        fea.probesKey = key;
    }
    if (!hovered)
        return false;
    const ImVec2 fb = ImGui::GetIO().DisplayFramebufferScale;
    const ImVec2 m = ImGui::GetIO().MousePos;
    if (m.x < origin.x || m.y < origin.y || m.x > origin.x + size.x || m.y > origin.y + size.y)
        return false;
    const render::Ray ray = ctx.camera.rayThrough((m.x - origin.x) * fb.x, (m.y - origin.y) * fb.y);
    fea::ProbeHit hit;
    if (!fea::probeSurface(*surf, ray.origin, ray.dir, hit))
        return false;
    ImGui::SetTooltip("%s: %.4g %s\nat %.2f, %.2f, %.2f", fea::fieldName(fea.field), double(hit.value),
                      fea::fieldUnit(fea.field), hit.point.x, hit.point.y, hit.point.z);
    if (clicked) {
        fea.probes.push_back({hit.point, hit.value});
        return true;
    }
    return false;
}

// ---- viewport overlays -------------------------------------------------------------------

void drawAnalysisOverlay(AppContext& ctx, ImDrawList* dl, const ImVec2& origin, const ImVec2& size)
{
    const ImVec2 fb = ImGui::GetIO().DisplayFramebufferScale;
    auto toScreen = [&](const Vec3& w, bool& visible) {
        const Vec3 p = ctx.camera.project(w);
        visible = p.z > -1.0 && p.z < 1.0;
        return ImVec2(origin.x + float(p.x) / fb.x, origin.y + float(p.y) / fb.y);
    };

    // ---- support / load glyphs ----
    const auto* analysis = dynamic_cast<const StaticAnalysisFeature*>(ctx.doc.find(ctx.contextAnalysis()));
    const auto* faces = analysis ? ctx.scene.faceInfo(analysis->target()) : nullptr;
    if (analysis && faces) {
        const BoundingBox bb = ctx.scene.bounds();
        const double len = std::max(bb.diagonal(), 1.0) * 0.12;
        for (FeatureId c : ctx.doc.nestedChildren(analysis->id())) {
            const auto* bc = dynamic_cast<const FeaBoundaryFeature*>(ctx.doc.find(c));
            if (!bc)
                continue;
            const glm::vec3 base(bc->color().r, bc->color().g, bc->color().b);
            const ImU32 col = toU32(glm::mix(base, glm::vec3(1.0f), 0.25f));
            int drawn = 0;
            for (int face : bc->faces()) {
                if (face < 1 || face > int(faces->size()) || drawn++ > 24)
                    continue;
                const FaceInfo& fi = (*faces)[std::size_t(face - 1)];
                bool v1 = false, v2 = false;
                const ImVec2 at = toScreen(fi.centroid, v1);
                if (!v1)
                    continue;
                std::string label;
                if (bc->type() == model::ForceFeature::kType) {
                    const Vec3 F = bc->props().get<Vec3>(model::ForceFeature::kForce);
                    if (glm::length(F) <= 0)
                        continue;
                    const ImVec2 tail = toScreen(fi.centroid - glm::normalize(F) * len, v2);
                    arrow(dl, tail, at, col);
                    if (drawn == 1)
                        outlinedText(dl, ImVec2(tail.x + 4, tail.y - 16), col, num(glm::length(F), "N").c_str());
                } else if (bc->type() == model::PressureFeature::kType) {
                    const double p = bc->props().get<double>(model::PressureFeature::kPressure);
                    const Vec3 dir = p >= 0 ? -fi.normal : fi.normal;
                    const ImVec2 tail = toScreen(fi.centroid - dir * len * 0.8, v2);
                    arrow(dl, tail, at, col, 2.0f);
                    if (drawn == 1)
                        outlinedText(dl, ImVec2(tail.x + 4, tail.y - 16), col, num(p, "MPa").c_str());
                } else if (bc->type() == model::MeshRefinementFeature::kType) {
                    // Mesh refinement: a small grid symbol.
                    dl->AddRectFilled(ImVec2(at.x - 6, at.y - 6), ImVec2(at.x + 6, at.y + 6), IM_COL32(12, 12, 14, 200));
                    for (int k = -1; k <= 1; ++k) {
                        dl->AddLine(ImVec2(at.x + 4.0f * float(k), at.y - 5), ImVec2(at.x + 4.0f * float(k), at.y + 5), col);
                        dl->AddLine(ImVec2(at.x - 5, at.y + 4.0f * float(k)), ImVec2(at.x + 5, at.y + 4.0f * float(k)), col);
                    }
                } else {
                    // Fixed support: a small "ground" symbol.
                    dl->AddCircleFilled(at, 6.0f, IM_COL32(12, 12, 14, 200));
                    dl->AddCircleFilled(at, 4.0f, col);
                    dl->AddLine(ImVec2(at.x - 9, at.y + 7), ImVec2(at.x + 9, at.y + 7), col, 2.0f);
                    for (int k = -1; k <= 1; ++k)
                        dl->AddLine(ImVec2(at.x + 6.0f * float(k) + 3, at.y + 7),
                                    ImVec2(at.x + 6.0f * float(k) - 2, at.y + 13), col, 1.5f);
                }
            }
        }
    }

    // ---- result legend ----
    auto& fea = ctx.fea;
    const FeatureId shown = fea.shownAnalysis;
    const AnalysisRuntime* rt = fea.runtime(shown);
    if (rt && rt->result && fea.showResults && fea.meshUpToDate(ctx.doc, shown)) {
        float range[2];
        if (fea.displaySurface(shown, range)) {
            char title[128];
            if (rt->modal && rt->mode < int(rt->modal->frequencies.size()))
                std::snprintf(title, sizeof(title), "Mode %d: %.5g Hz [%s, relative]", rt->mode + 1,
                              rt->modal->frequencies[std::size_t(rt->mode)], fea::fieldName(fea.field));
            else
                std::snprintf(title, sizeof(title), "%s [%s]", fea::fieldName(fea.field), fea::fieldUnit(fea.field));
            const float barW = 18.0f, barH = std::min(240.0f, size.y * 0.45f);
            const float boxW = std::max(ImGui::CalcTextSize(title).x, barW + 80.0f) + 24.0f;
            const float boxX = origin.x + size.x - boxW - 8.0f;
            const ImVec2 p0(boxX + 16.0f, origin.y + 76.0f);
            dl->AddRectFilled(ImVec2(boxX, p0.y - 30), ImVec2(boxX + boxW, p0.y + barH + 54),
                              IM_COL32(18, 20, 24, 190), 6.0f);
            outlinedText(dl, ImVec2(p0.x - 4, p0.y - 24), IM_COL32(230, 232, 236, 255), title);
            const int steps = 48;
            for (int i = 0; i < steps; ++i) {
                const float t0 = float(i) / steps, t1 = float(i + 1) / steps;
                const ImU32 c0 = toU32(fea::colormap(1.0f - t0)), c1 = toU32(fea::colormap(1.0f - t1));
                dl->AddRectFilledMultiColor(ImVec2(p0.x, p0.y + barH * t0), ImVec2(p0.x + barW, p0.y + barH * t1),
                                            c0, c0, c1, c1);
            }
            dl->AddRect(p0, ImVec2(p0.x + barW, p0.y + barH), IM_COL32(220, 220, 220, 160));
            for (int i = 0; i <= 6; ++i) {
                const float t = float(i) / 6.0f;
                const double v = range[1] + (range[0] - range[1]) * t;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.4g", v);
                const float y = p0.y + barH * t;
                dl->AddLine(ImVec2(p0.x + barW, y), ImVec2(p0.x + barW + 4, y), IM_COL32(220, 220, 220, 200));
                outlinedText(dl, ImVec2(p0.x + barW + 7, y - 7), IM_COL32(225, 228, 232, 255), buf);
            }
            char foot[96];
            std::snprintf(foot, sizeof(foot), "Deformation x%.3g", fea.effectiveDeformationScale(shown));
            outlinedText(dl, ImVec2(p0.x - 4, p0.y + barH + 10), IM_COL32(180, 185, 195, 255), foot);
            if (!fea.resultUpToDate(ctx.doc, shown))
                outlinedText(dl, ImVec2(p0.x - 4, p0.y + barH + 28), IM_COL32(255, 190, 80, 255), "Outdated");

            // Pinned probe values
            if (fea.probesKey == fea.displayKey(shown))
                for (const auto& pr : fea.probes) {
                    bool vis = false;
                    const ImVec2 at = toScreen(pr.point, vis);
                    if (!vis)
                        continue;
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.4g", double(pr.value));
                    const ImVec2 ts = ImGui::CalcTextSize(buf);
                    const ImVec2 box(at.x + 10.0f, at.y - ts.y - 10.0f);
                    dl->AddLine(at, ImVec2(box.x, box.y + ts.y + 4.0f), IM_COL32(240, 240, 240, 220), 1.2f);
                    dl->AddCircleFilled(at, 3.5f, IM_COL32(15, 15, 18, 255));
                    dl->AddCircleFilled(at, 2.5f, IM_COL32(255, 255, 255, 255));
                    dl->AddRectFilled(ImVec2(box.x - 3, box.y - 2), ImVec2(box.x + ts.x + 3, box.y + ts.y + 2),
                                      IM_COL32(18, 20, 24, 220), 3.0f);
                    dl->AddText(box, IM_COL32(240, 240, 240, 255), buf);
                }
        }
    }

    // ---- job progress ----
    if (fea.busy()) {
        const std::string text = fea.progressText();
        const float w = 320.0f, h = 46.0f;
        const ImVec2 a(origin.x + (size.x - w) * 0.5f, origin.y + size.y - h - 18.0f);
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), IM_COL32(18, 20, 24, 220), 6.0f);
        outlinedText(dl, ImVec2(a.x + 12, a.y + 6), IM_COL32(230, 232, 236, 255), text.c_str());
        const float p = std::clamp(float(fea.progress()), 0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(a.x + 12, a.y + 28), ImVec2(a.x + w - 12, a.y + 36), IM_COL32(60, 64, 72, 255), 3);
        dl->AddRectFilled(ImVec2(a.x + 12, a.y + 28), ImVec2(a.x + 12 + (w - 24) * p, a.y + 36),
                          IM_COL32(75, 145, 230, 255), 3);
    }
}

} // namespace cf::app::ui
