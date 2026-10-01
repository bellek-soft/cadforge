#include "app/Commands.h"
#include "app/AppContext.h"

#include "model/features/PartFeatures.h"

namespace cf::app::cmd {

namespace {
const std::vector<std::string> kProjectFilter = {"CadForge project (*.cfp)", "*.cfp"};
const std::vector<std::string> kStepFilter = {"STEP (*.step *.stp)", "*.step *.stp *.STEP *.STP"};
const std::vector<std::string> kStlFilter = {"STL (*.stl)", "*.stl"};
} // namespace

void newFile(AppContext& ctx)
{
    ctx.guardUnsaved([&ctx] { ctx.newDocument(); });
}

void open(AppContext& ctx)
{
    ctx.guardUnsaved([&ctx] {
        ctx.requestFile({FileRequest::Kind::Open, "Open project", kProjectFilter, ".cfp",
                         [&ctx](const std::string& p) { ctx.openDocument(p); }});
    });
}

void saveAs(AppContext& ctx)
{
    ctx.requestFile({FileRequest::Kind::Save, "Save project as", kProjectFilter, ".cfp",
                     [&ctx](const std::string& p) {
                         if (ctx.saveDocument(p) && ctx.pendingAfterConfirm) {
                             auto next = std::move(ctx.pendingAfterConfirm);
                             ctx.pendingAfterConfirm = nullptr;
                             next();
                         }
                     }});
}

void save(AppContext& ctx)
{
    if (ctx.filePath.empty())
        saveAs(ctx);
    else
        ctx.saveDocument(ctx.filePath);
}

void importStep(AppContext& ctx)
{
    ctx.requestFile({FileRequest::Kind::Open, "Import STEP", kStepFilter, ".step",
                     [&ctx](const std::string& p) { ctx.importStep(p); }});
}

void exportStep(AppContext& ctx)
{
    ctx.requestFile({FileRequest::Kind::Save, "Export STEP", kStepFilter, ".step",
                     [&ctx](const std::string& p) { ctx.exportStep(p); }});
}

void exportStl(AppContext& ctx)
{
    ctx.requestFile({FileRequest::Kind::Save, "Export STL", kStlFilter, ".stl",
                     [&ctx](const std::string& p) { ctx.exportStl(p); }});
}

void loadDemo(AppContext& ctx)
{
    using model::BooleanFeature;
    auto& doc = ctx.doc;
    auto make = [&](const char* type, const char* name) {
        auto* f = doc.create(type);
        f->setName(doc.uniqueName(name));
        return f;
    };
    auto boolean = [&](const char* name, int op, FeatureId base, std::vector<FeatureId> tools) {
        auto* b = make("Part::Boolean", name);
        b->props().set(BooleanFeature::kOperation, op);
        b->props().set(BooleanFeature::kBase, base);
        b->props().set(BooleanFeature::kTools, tools);
        b->setColor(doc.find(base)->color());
        doc.find(base)->setVisible(false);
        for (FeatureId t : tools)
            doc.find(t)->setVisible(false);
        return b;
    };

    // A simple bracket: plate + boss, minus two holes.
    auto* plate = make("Part::Box", "Plate");
    plate->props().set("length", 80.0);
    plate->props().set("width", 50.0);
    plate->props().set("height", 10.0);
    plate->setColor({0.60f, 0.68f, 0.78f, 1.f});
    auto* boss = make("Part::Cylinder", "Boss");
    boss->props().set("radius", 14.0);
    boss->props().set("height", 32.0);
    boss->setPlacement({{22, 25, 0}, {0, 0, 0}});
    auto* body = boolean("Body", BooleanFeature::Union, plate->id(), {boss->id()});

    auto* hole1 = make("Part::Cylinder", "Hole");
    hole1->props().set("radius", 7.0);
    hole1->props().set("height", 60.0);
    hole1->setPlacement({{22, 25, -10}, {0, 0, 0}});
    auto* hole2 = make("Part::Cylinder", "Hole");
    hole2->props().set("radius", 5.0);
    hole2->props().set("height", 60.0);
    hole2->setPlacement({{62, 25, -10}, {0, 0, 0}});
    boolean("Bracket", BooleanFeature::Cut, body->id(), {hole1->id(), hole2->id()});

    // Classic CSG: cube intersected with a sphere.
    auto* cube = make("Part::Box", "Cube");
    for (const char* k : {"length", "width", "height"})
        cube->props().set(k, 30.0);
    cube->props().set("centered", true);
    cube->setPlacement({{130, 25, 15}, {0, 0, 0}});
    cube->setColor({0.80f, 0.66f, 0.50f, 1.f});
    auto* ball = make("Part::Sphere", "Ball");
    ball->props().set("radius", 20.0);
    ball->setPlacement({{130, 25, 15}, {0, 0, 0}});
    boolean("Dice", BooleanFeature::Intersect, cube->id(), {ball->id()});

    auto* ring = make("Part::Torus", "Ring");
    ring->props().set("radius1", 16.0);
    ring->props().set("radius2", 4.0);
    ring->setPlacement({{130, 25, 52}, {90, 0, 0}});
    ring->setColor({0.62f, 0.78f, 0.62f, 1.f});

    ctx.commit("Load demo");
    ctx.fitAll();
    ctx.status("Demo scene loaded - select an object to edit its parameters");
}

} // namespace cf::app::cmd
