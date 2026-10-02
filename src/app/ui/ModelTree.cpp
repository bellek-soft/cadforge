#include "app/ui/Ui.h"

#include "app/AppContext.h"

#include <unordered_set>

namespace cf::app::ui {

using model::Feature;
using model::FeatureState;

namespace {

const char* typeTag(const Feature& f)
{
    const auto t = f.type();
    if (t == "Part::Boolean") {
        switch (f.props().get<int>("operation")) {
        case 0: return "U";
        case 1: return "-";
        default: return "n";
        }
    }
    if (t == "Part::Fillet") return "F";
    if (t == "Part::Chamfer") return "C";
    if (t == "Part::ImportStep") return "S";
    if (t == "Sketch::Sketch") return "~";
    if (t == "Part::Extrude") return "E";
    if (t == "Part::Revolve") return "R";
    if (t == "FEA::StaticAnalysis") return "A";
    if (t == "FEA::FixedSupport") return "|";
    if (t == "FEA::Force") return ">";
    if (t == "FEA::Pressure") return "P";
    return "#";
}

void drawNode(AppContext& ctx, const Feature& f, std::unordered_set<FeatureId>& path)
{
    const bool isSelected = ctx.selection.isFeatureSelected(f.id()) ||
                            (ctx.selection.features().size() == 1 && ctx.selection.features()[0] == f.id());
    auto inputs = f.consumesInputs() ? f.inputs() : std::vector<FeatureId>{};
    for (FeatureId c : ctx.doc.nestedChildren(f.id()))
        inputs.push_back(c);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushID(static_cast<int>(f.id()));

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding;
    if (inputs.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (isSelected)
        flags |= ImGuiTreeNodeFlags_Selected;

    // Color swatch + type tag before the name.
    const Color& c = f.color();
    ImVec4 tagColor(c.r, c.g, c.b, 1.0f);
    if (!f.visible())
        tagColor.w = 0.45f;

    const bool open = ImGui::TreeNodeEx("##node", flags);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        const SelItem item{f.id()};
        if (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift)
            ctx.selection.toggle(item);
        else
            ctx.selection.set(item);
        if (ctx.shapeEdit.active())
            ctx.cancelSubShapeEdit();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (f.type() == "Sketch::Sketch")
            ctx.editSketch(f.id());
        else if (inputs.empty())
            ctx.fitSelection();
    }

    if (ImGui::BeginPopupContextItem("ctx")) {
        if (!ctx.selection.isFeatureSelected(f.id()))
            ctx.selection.set({f.id()});
        if (ImGui::MenuItem(f.visible() ? "Hide" : "Show"))
            ctx.setVisible(f.id(), !f.visible());
        if (ImGui::MenuItem("Fit to view"))
            ctx.fitSelection();
        if (f.type() == "Sketch::Sketch" && ImGui::MenuItem("Edit sketch"))
            ctx.editSketch(f.id());
        ImGui::Separator();
        if (ImGui::MenuItem("Delete"))
            ctx.deleteSelection();
        ImGui::EndPopup();
    }

    ImGui::SameLine(0, 0);
    ImGui::TextColored(tagColor, " %s ", typeTag(f));
    ImGui::SameLine(0, 2);
    if (f.state() == FeatureState::Error)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s  (!)", f.name().c_str());
    else if (!f.visible())
        ImGui::TextDisabled("%s", f.name().c_str());
    else
        ImGui::TextUnformatted(f.name().c_str());
    if (f.state() == FeatureState::Error && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", f.error().c_str());

    // Visibility toggle column.
    ImGui::TableSetColumnIndex(1);
    if (f.producesGeometry()) {
        bool visible = f.visible();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
        if (ImGui::Checkbox("##vis", &visible))
            ctx.setVisible(f.id(), visible);
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(visible ? "Hide" : "Show");
    }

    if (open && !inputs.empty()) {
        for (FeatureId in : inputs) {
            const Feature* child = ctx.doc.find(in);
            if (!child || path.count(in))
                continue;
            path.insert(in);
            drawNode(ctx, *child, path);
            path.erase(in);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace

void drawModelTree(AppContext& ctx)
{
    if (!ImGui::Begin(kModelWindow)) {
        ImGui::End();
        return;
    }
    if (ctx.doc.features().empty()) {
        ImGui::TextDisabled("The document is empty.");
        ImGui::TextDisabled("Use the toolbar or Create menu,");
        ImGui::TextDisabled("or File > Load Demo Scene.");
    }

    const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##tree", 2, tflags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Vis", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        std::unordered_set<FeatureId> path;
        for (FeatureId id : ctx.doc.roots()) {
            if (const Feature* f = ctx.doc.find(id)) {
                path.insert(id);
                drawNode(ctx, *f, path);
                path.erase(id);
            }
        }
        ImGui::EndTable();
    }

    // Click on empty space clears the selection.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        ctx.selection.clear();
    ImGui::End();
}

} // namespace cf::app::ui
