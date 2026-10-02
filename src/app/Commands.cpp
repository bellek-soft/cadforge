#include "app/Commands.h"
#include "app/AppContext.h"

#include "geom/Tessellator.h"
#include "model/features/FeaFeatures.h"
#include "model/features/PartFeatures.h"

namespace cf::app::cmd {

namespace {
/// 1-based index of the face of `shape` whose centroid is closest to `p`.
int faceClosestTo(const geom::Shape& shape, const Vec3& p)
{
    const MeshData m = geom::tessellate(shape);
    int best = 0;
    double bestD = 1e300;
    for (std::size_t f = 0; f < m.faceRanges.size(); ++f) {
        const auto& r = m.faceRanges[f];
        if (r.count == 0)
            continue;
        Vec3 c(0.0);
        for (std::uint32_t i = r.first; i < r.first + r.count; ++i) {
            const auto v = m.indices[i];
            c += Vec3(m.positions[3 * v], m.positions[3 * v + 1], m.positions[3 * v + 2]);
        }
        c /= double(r.count);
        if (glm::distance(c, p) < bestD) {
            bestD = glm::distance(c, p);
            best = int(f) + 1;
        }
    }
    return best;
}

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

void loadAnalysisDemo(AppContext& ctx)
{
    using namespace model;
    auto& doc = ctx.doc;
    // A plate with a lightening hole, clamped at one end and loaded at the other.
    auto* plate = doc.create("Part::Box");
    plate->setName(doc.uniqueName("Plate"));
    plate->props().set("length", 120.0);
    plate->props().set("width", 30.0);
    plate->props().set("height", 8.0);
    plate->setColor({0.60f, 0.68f, 0.78f, 1.f});
    auto* hole = doc.create("Part::Cylinder");
    hole->setName(doc.uniqueName("Hole"));
    hole->props().set("radius", 8.0);
    hole->props().set("height", 20.0);
    hole->setPlacement({{70, 15, -5}, {0, 0, 0}});
    auto* part = doc.create("Part::Boolean");
    part->setName(doc.uniqueName("Bracket"));
    part->props().set(BooleanFeature::kOperation, int(BooleanFeature::Cut));
    part->props().set(BooleanFeature::kBase, plate->id());
    part->props().set(BooleanFeature::kTools, std::vector<FeatureId>{hole->id()});
    part->setColor(plate->color());
    plate->setVisible(false);
    hole->setVisible(false);
    doc.recompute();
    if (part->state() != FeatureState::Ok) {
        ctx.status("Demo failed: " + part->error(), true);
        return;
    }

    auto* study = doc.create(StaticAnalysisFeature::kType);
    study->setName(doc.uniqueName("Static"));
    study->props().set(StaticAnalysisFeature::kTarget, part->id());
    study->props().set(StaticAnalysisFeature::kElementSize, 4.0);
    auto* fixed = doc.create(FixedSupportFeature::kType);
    fixed->props().set(FeaBoundaryFeature::kAnalysis, study->id());
    fixed->props().set(FeaBoundaryFeature::kFaces, std::vector<int>{faceClosestTo(part->shape(), {0, 15, 4})});
    auto* force = doc.create(ForceFeature::kType);
    force->props().set(FeaBoundaryFeature::kAnalysis, study->id());
    force->props().set(FeaBoundaryFeature::kFaces, std::vector<int>{faceClosestTo(part->shape(), {120, 15, 4})});
    force->props().set(ForceFeature::kForce, Vec3(0, 0, -400.0));

    ctx.commit("Load analysis demo");
    ctx.selection.set({study->id()});
    ctx.fitAll();
    ctx.solveAnalysis(study->id());
}

} // namespace cf::app::cmd
