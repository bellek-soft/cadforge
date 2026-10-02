#include "app/ui/SketchUi.h"
#include "app/ui/Ui.h"

#include "app/AppContext.h"
#include "model/features/FeaFeatures.h"
#include "model/features/PartFeatures.h"
#include "model/features/SketchFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace cf::app::ui {

using model::Feature;
using model::FeatureState;
using model::Property;
using model::PropertyType;

namespace {

struct EditResult {
    bool changed = false;   // value changed this frame (live preview)
    bool finished = false;  // edit completed -> record undo step
};

bool inputString(const char* id, std::string& s, ImGuiInputTextFlags flags = 0)
{
    char buf[1024];
    std::strncpy(buf, s.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    if (ImGui::InputText(id, buf, sizeof(buf), flags)) {
        s = buf;
        return true;
    }
    return false;
}

void label(const char* text, const std::string& tooltip)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    if (!tooltip.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip.c_str());
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

std::string featureLabel(const model::Document& doc, FeatureId id)
{
    if (id == kNoFeature)
        return "<none>";
    const Feature* f = doc.find(id);
    return f ? f->name() : "<missing #" + std::to_string(id) + ">";
}

/// Combo listing features that `owner` may reference. Returns true on pick.
bool featureCombo(AppContext& ctx, const Feature& owner, const char* id, FeatureId current, FeatureId& picked,
                  bool allowNone, const std::vector<FeatureId>& exclude = {}, const char* preview = nullptr)
{
    bool result = false;
    const std::string label = preview ? std::string(preview) : featureLabel(ctx.doc, current);
    if (ImGui::BeginCombo(id, label.c_str())) {
        if (allowNone && ImGui::Selectable("<none>", current == kNoFeature)) {
            picked = kNoFeature;
            result = true;
        }
        for (const auto& f : ctx.doc.features()) {
            if (!ctx.doc.canReference(owner.id(), f->id()))
                continue;
            if (std::find(exclude.begin(), exclude.end(), f->id()) != exclude.end())
                continue;
            if (ImGui::Selectable(f->name().c_str(), f->id() == current)) {
                picked = f->id();
                result = true;
            }
        }
        ImGui::EndCombo();
    }
    return result;
}

EditResult editProperty(AppContext& ctx, Feature& f, Property& p)
{
    EditResult r;
    std::string fmt = p.format.empty() ? "%.3f" : p.format;
    if (!p.unit.empty())
        fmt += " " + p.unit;

    label(p.label.c_str(), p.tooltip);
    ImGui::PushID(p.key.c_str());

    switch (p.type) {
    case PropertyType::Double: {
        double v = std::get<double>(p.value);
        const float speed = static_cast<float>(std::max(p.speed, std::abs(v) * 0.004));
        if (ImGui::DragScalar("##v", ImGuiDataType_Double, &v, speed, &p.minValue, &p.maxValue, fmt.c_str(),
                              ImGuiSliderFlags_AlwaysClamp))
            r.changed = f.props().set(p.key, v);
        r.finished = ImGui::IsItemDeactivatedAfterEdit();
        break;
    }
    case PropertyType::Int: {
        int v = std::get<int>(p.value);
        if (ImGui::DragInt("##v", &v, 1.0f, int(p.minValue), int(p.maxValue)))
            r.changed = f.props().set(p.key, v);
        r.finished = ImGui::IsItemDeactivatedAfterEdit();
        break;
    }
    case PropertyType::Bool: {
        bool v = std::get<bool>(p.value);
        if (ImGui::Checkbox("##v", &v))
            r.changed = r.finished = f.props().set(p.key, v);
        break;
    }
    case PropertyType::Vec3: {
        Vec3 v = std::get<Vec3>(p.value);
        const float speed = static_cast<float>(p.speed);
        if (ImGui::DragScalarN("##v", ImGuiDataType_Double, &v[0], 3, speed, nullptr, nullptr, "%.2f"))
            r.changed = f.props().set(p.key, v);
        r.finished = ImGui::IsItemDeactivatedAfterEdit();
        break;
    }
    case PropertyType::Enum: {
        int v = std::get<int>(p.value);
        const char* preview = (v >= 0 && v < int(p.enumItems.size())) ? p.enumItems[size_t(v)].c_str() : "?";
        if (ImGui::BeginCombo("##v", preview)) {
            for (int i = 0; i < int(p.enumItems.size()); ++i)
                if (ImGui::Selectable(p.enumItems[size_t(i)].c_str(), i == v))
                    r.changed = r.finished = f.props().set(p.key, i);
            ImGui::EndCombo();
        }
        break;
    }
    case PropertyType::String: {
        std::string v = std::get<std::string>(p.value);
        if (inputString("##v", v))
            r.changed = f.props().set(p.key, v);
        r.finished = ImGui::IsItemDeactivatedAfterEdit();
        break;
    }
    case PropertyType::FilePath: {
        std::string v = std::get<std::string>(p.value);
        const float bw = ImGui::GetFrameHeight() * 1.6f;
        ImGui::SetNextItemWidth(-bw - ImGui::GetStyle().ItemInnerSpacing.x);
        if (inputString("##v", v, ImGuiInputTextFlags_EnterReturnsTrue))
            r.changed = r.finished = f.props().set(p.key, v);
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Button("...", ImVec2(bw, 0))) {
            const FeatureId fid = f.id();
            const std::string key = p.key;
            ctx.requestFile({FileRequest::Kind::Open, "Choose file",
                             {"STEP (*.step *.stp)", "*.step *.stp *.STEP *.STP"}, "",
                             [&ctx, fid, key](const std::string& path) {
                                 if (Feature* ff = ctx.doc.find(fid); ff && ff->props().set(key, path))
                                     ctx.commit("Change file");
                             }});
        }
        break;
    }
    case PropertyType::FeatureRef: {
        FeatureId picked = kNoFeature;
        if (featureCombo(ctx, f, "##v", std::get<FeatureId>(p.value), picked, true))
            r.changed = r.finished = f.props().set(p.key, picked);
        break;
    }
    case PropertyType::FeatureRefList: {
        auto list = std::get<std::vector<FeatureId>>(p.value);
        int removeAt = -1;
        for (int i = 0; i < int(list.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::SmallButton("x"))
                removeAt = i;
            ImGui::SameLine();
            ImGui::TextUnformatted(featureLabel(ctx.doc, list[size_t(i)]).c_str());
            ImGui::PopID();
        }
        if (removeAt >= 0) {
            list.erase(list.begin() + removeAt);
            r.changed = r.finished = f.props().set(p.key, list);
        }
        FeatureId picked = kNoFeature;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (featureCombo(ctx, f, "##add", kNoFeature, picked, false, list, "Add...") && picked != kNoFeature) {
            list.push_back(picked);
            r.changed = r.finished = f.props().set(p.key, list);
        }
        break;
    }
    case PropertyType::IndexList: {
        const auto& list = std::get<std::vector<int>>(p.value);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%d selected", int(list.size()));
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit..."))
            ctx.beginSubShapeEdit(f.id());
        break;
    }
    }
    ImGui::PopID();
    return r;
}

struct Measure {
    std::uint64_t key = 0;
    double volume = 0, area = 0;
    BoundingBox bb;
};

void drawMeasurements(const Feature& f)
{
    static std::unordered_map<FeatureId, Measure> cache;
    if (f.state() != FeatureState::Ok)
        return;
    Measure& m = cache[f.id()];
    if (m.key != f.resultKey()) {
        m.key = f.resultKey();
        try {
            m.volume = f.shape().volume();
            m.area = f.shape().area();
        } catch (const std::exception&) {
            m.volume = m.area = 0;
        }
        m.bb = f.shape().bounds();
    }
    if (!ImGui::CollapsingHeader("Measurements", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (ImGui::BeginTable("##measure", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [](const char* k, const char* fmt, auto... v) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", k);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text(fmt, v...);
        };
        row("Volume", "%.3f mm3", m.volume);
        row("Area", "%.3f mm2", m.area);
        if (m.bb.valid()) {
            const Vec3 s = m.bb.size();
            row("Size", "%.2f x %.2f x %.2f", s.x, s.y, s.z);
        }
        row("Topology", "%d faces, %d edges", f.shape().faceCount(), f.shape().edgeCount());
        ImGui::EndTable();
    }
}

void drawFeatureEditor(AppContext& ctx, Feature& f)
{
    ImGui::TextDisabled("%s", std::string(f.typeLabel()).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("#%llu", static_cast<unsigned long long>(f.id()));

    if (f.state() == FeatureState::Error) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.45f, 1.0f));
        ImGui::TextWrapped("Error: %s", f.error().c_str());
        ImGui::PopStyleColor();
    }

    const ImGuiTableFlags tflags = ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##general", 2, tflags)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        label("Name", "");
        std::string name = f.name();
        if (inputString("##name", name) && !name.empty())
            f.setName(name);
        if (ImGui::IsItemDeactivatedAfterEdit())
            ctx.commit("Rename");

        label("Color", "");
        Color c = f.color();
        float rgb[3] = {c.r, c.g, c.b};
        if (ImGui::ColorEdit3("##color", rgb, ImGuiColorEditFlags_NoInputs))
            f.setColor({rgb[0], rgb[1], rgb[2], c.a});
        if (ImGui::IsItemDeactivatedAfterEdit())
            ctx.commit("Change color");

        if (f.producesGeometry()) {
            label("Visible", "");
            bool vis = f.visible();
            if (ImGui::Checkbox("##visible", &vis))
                ctx.setVisible(f.id(), vis);
        }
        ImGui::EndTable();
    }

    if (f.type() == model::StaticAnalysisFeature::kType)
        drawAnalysisPanel(ctx, f);
    if (const auto* sk = dynamic_cast<const model::SketchFeature*>(&f)) {
        if (ImGui::Button("Edit sketch"))
            ctx.editSketch(f.id());
        ImGui::SameLine();
        ImGui::TextDisabled("%d curves, %d constraints", int(sk->sketch().geometry.size()),
                            int(sk->sketch().constraints.size()));
        if (f.state() == FeatureState::Ok)
            ImGui::TextDisabled("%s", sk->lastSolve().message.c_str());
    }

    // Group properties by their declared group, preserving declaration order.
    std::vector<std::string> groups;
    for (const auto& p : f.props().all())
        if (!p.hidden && std::find(groups.begin(), groups.end(), p.group) == groups.end())
            groups.push_back(p.group);
    // Placement last: it's common to every feature.
    std::stable_partition(groups.begin(), groups.end(), [](const std::string& g) { return g != "Placement"; });

    bool changed = false;
    std::string finishedLabel;
    for (const auto& g : groups) {
        if (!ImGui::CollapsingHeader(g.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            continue;
        if (ImGui::BeginTable(("##grp" + g).c_str(), 2, tflags)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
            for (auto& p : f.props().all()) {
                if (p.hidden || p.group != g)
                    continue;
                EditResult r = editProperty(ctx, f, p);
                if (r.changed)
                    f.onPropertyEdited(p.key); // e.g. material presets
                changed |= r.changed;
                if (r.finished)
                    finishedLabel = "Edit " + f.name() + "." + p.label;
            }
            ImGui::EndTable();
        }
    }
    if (!finishedLabel.empty())
        ctx.commit(finishedLabel);
    else if (changed)
        ctx.recompute(); // live preview while dragging

    if (f.producesGeometry())
        drawMeasurements(f);
}

void drawSubShapeEditPanel(AppContext& ctx)
{
    const Feature* f = ctx.doc.find(ctx.shapeEdit.feature);
    const Feature* base = ctx.doc.find(ctx.shapeEdit.base);
    if (!f || !base)
        return;
    const bool faces = ctx.shapeEdit.kind == render::PickKind::Face;
    const char* what = faces ? "faces" : "edges";
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Editing %s of %s", what, f->name().c_str());
    ImGui::TextWrapped("Click %s of '%s' in the viewport. Ctrl/Shift+click adds or removes %s.", what,
                       base->name().c_str(), what);
    ImGui::Separator();
    const auto items = ctx.selection.subShapes(ctx.shapeEdit.base, ctx.shapeEdit.kind);
    ImGui::Text("%d %s selected", int(items.size()), what);
    if (ImGui::Button("Apply (Enter)"))
        ctx.applySubShapeEdit();
    ImGui::SameLine();
    if (ImGui::Button("Cancel (Esc)"))
        ctx.cancelSubShapeEdit();
}

void drawMultiSelection(AppContext& ctx, const std::vector<FeatureId>& feats)
{
    ImGui::Text("%d objects selected", int(feats.size()));
    if (const Feature* b = ctx.doc.find(feats[0]))
        ImGui::TextDisabled("Base (first selected): %s", b->name().c_str());
    ImGui::Separator();
    ImGui::TextUnformatted("Combine:");
    if (ImGui::Button("Union")) ctx.booleanFromSelection(model::BooleanFeature::Union);
    ImGui::SameLine();
    if (ImGui::Button("Cut")) ctx.booleanFromSelection(model::BooleanFeature::Cut);
    ImGui::SameLine();
    if (ImGui::Button("Intersect")) ctx.booleanFromSelection(model::BooleanFeature::Intersect);
}

void drawSubShapeInfo(AppContext& ctx)
{
    int faces = 0, edges = 0;
    for (const auto& it : ctx.selection.items()) {
        faces += it.kind == render::PickKind::Face;
        edges += it.kind == render::PickKind::Edge;
    }
    if (!faces && !edges)
        return;
    ImGui::Separator();
    ImGui::Text("Sub-shapes: %d face(s), %d edge(s)", faces, edges);
    if (edges) {
        if (ImGui::Button("Fillet")) ctx.dressUpFromSelection("Part::Fillet");
        ImGui::SameLine();
        if (ImGui::Button("Chamfer")) ctx.dressUpFromSelection("Part::Chamfer");
    }
}

} // namespace

void drawProperties(AppContext& ctx)
{
    if (!ImGui::Begin(kPropertiesWindow)) {
        ImGui::End();
        return;
    }
    if (ctx.sketchEdit.active()) {
        drawSketchPanel(ctx);
        ImGui::End();
        return;
    }
    if (ctx.shapeEdit.active()) {
        drawSubShapeEditPanel(ctx);
        ImGui::End();
        return;
    }

    const auto feats = ctx.selection.features();
    if (feats.empty()) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::Spacing();
        ImGui::TextWrapped("Select an object in the viewport or the model tree to edit its parameters. "
                           "Select two or more objects to combine them with Union / Cut / Intersect.");
    } else if (feats.size() > 1) {
        drawMultiSelection(ctx, feats);
    } else if (Feature* f = ctx.doc.find(feats[0])) {
        drawSubShapeInfo(ctx);
        drawFeatureEditor(ctx, *f);
    }
    ImGui::End();
}

} // namespace cf::app::ui
