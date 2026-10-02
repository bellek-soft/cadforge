#include "app/Commands.h"
#include "app/AppContext.h"

#include "geom/Tessellator.h"
#include "model/features/FeaFeatures.h"
#include "model/features/PartFeatures.h"
#include "model/features/SketchFeatures.h"

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

void loadSketchDemo(AppContext& ctx)
{
    using namespace model;
    using namespace sketch;
    auto& doc = ctx.doc;
    auto ref = [](int g, PointPos p = PointPos::Edge) { return Ref{g, p}; };
    auto con = [](ConstraintType t, Ref a, Ref b = {}, double v = 0.0) {
        Constraint c;
        c.type = t;
        c.a = a;
        c.b = b;
        c.value = v;
        return c;
    };
    // Closed polygon with coincident corners and H/V constraints on axis-aligned sides.
    auto polygon = [&](Sketch& s, const std::vector<Vec2>& pts) {
        const int first = int(s.geometry.size());
        const int n = int(pts.size());
        for (int i = 0; i < n; ++i)
            s.add(Geometry::line(pts[size_t(i)], pts[size_t((i + 1) % n)]));
        for (int i = 0; i < n; ++i)
            s.addConstraint(con(ConstraintType::Coincident, ref(first + i, PointPos::End),
                                ref(first + (i + 1) % n, PointPos::Start)));
        for (int i = 0; i < n; ++i) {
            const Vec2 d = pts[size_t((i + 1) % n)] - pts[size_t(i)];
            if (std::abs(d.y) < 1e-9)
                s.addConstraint(con(ConstraintType::Horizontal, ref(first + i)));
            else if (std::abs(d.x) < 1e-9)
                s.addConstraint(con(ConstraintType::Vertical, ref(first + i)));
        }
        return first;
    };

    // 1) L-shaped bracket profile on the XZ plane, extruded symmetrically.
    auto* profile = static_cast<SketchFeature*>(doc.create("Sketch::Sketch"));
    profile->setName(doc.uniqueName("BracketProfile"));
    profile->props().set(SketchFeature::kPlane, int(SketchFeature::XZ));
    {
        Sketch s;
        const int l = polygon(s, {{0, 0}, {60, 0}, {60, 8}, {8, 8}, {8, 45}, {0, 45}});
        s.addConstraint(con(ConstraintType::Coincident, ref(l, PointPos::Start), ref(kOrigin, PointPos::Start)));
        s.addConstraint(con(ConstraintType::Distance, ref(l), {}, 60.0));
        s.addConstraint(con(ConstraintType::Distance, ref(l + 1), {}, 8.0));
        s.addConstraint(con(ConstraintType::Distance, ref(l + 4), {}, 8.0));
        s.addConstraint(con(ConstraintType::Distance, ref(l + 5), {}, 45.0));
        solve(s);
        profile->setSketch(s);
    }
    auto* bracket = doc.create("Part::Extrude");
    bracket->setName(doc.uniqueName("Bracket"));
    bracket->props().set(ProfileFeature::kSketch, profile->id());
    bracket->props().set("length", 40.0);
    bracket->props().set("symmetric", true);
    bracket->setColor({0.60f, 0.68f, 0.78f, 1.f});
    profile->setVisible(false);

    // 2) Two holes cut through the base from a sketch on the XY plane.
    auto* holes = static_cast<SketchFeature*>(doc.create("Sketch::Sketch"));
    holes->setName(doc.uniqueName("Holes"));
    holes->props().set(SketchFeature::kOffset, 8.0);
    {
        Sketch s;
        const int c1 = s.add(Geometry::circle({25, 0}, 5));
        const int c2 = s.add(Geometry::circle({48, 0}, 5));
        s.addConstraint(con(ConstraintType::PointOnObject, ref(c1, PointPos::Center), ref(kHAxis)));
        s.addConstraint(con(ConstraintType::PointOnObject, ref(c2, PointPos::Center), ref(kHAxis)));
        s.addConstraint(con(ConstraintType::Equal, ref(c1), ref(c2)));
        s.addConstraint(con(ConstraintType::Diameter, ref(c1), {}, 9.0));
        s.addConstraint(con(ConstraintType::DistanceX, ref(c1, PointPos::Center), {}, 26.0));
        s.addConstraint(con(ConstraintType::DistanceX, ref(c1, PointPos::Center), ref(c2, PointPos::Center), 22.0));
        solve(s);
        holes->setSketch(s);
    }
    auto* drilled = doc.create("Part::Extrude");
    drilled->setName(doc.uniqueName("Drilled"));
    drilled->props().set(ProfileFeature::kSketch, holes->id());
    drilled->props().set(ProfileFeature::kOperation, int(ProfileFeature::Cut));
    drilled->props().set(ProfileFeature::kTarget, bracket->id());
    drilled->props().set("length", 20.0);
    drilled->props().set("reversed", true);
    drilled->setColor(bracket->color());
    holes->setVisible(false);
    bracket->setVisible(false);

    // 3) A pulley: half cross-section revolved around the sketch V axis.
    auto* section = static_cast<SketchFeature*>(doc.create("Sketch::Sketch"));
    section->setName(doc.uniqueName("PulleySection"));
    section->props().set(SketchFeature::kPlane, int(SketchFeature::XZ));
    {
        Sketch s;
        const int l = polygon(s, {{6, 0}, {30, 0}, {30, 4}, {24, 8}, {30, 12}, {30, 16}, {6, 16}});
        s.addConstraint(con(ConstraintType::Distance, ref(l, PointPos::Start), ref(kVAxis), 6.0));
        s.addConstraint(con(ConstraintType::PointOnObject, ref(l, PointPos::Start), ref(kHAxis)));
        s.addConstraint(con(ConstraintType::Distance, ref(l), {}, 24.0));
        s.addConstraint(con(ConstraintType::Distance, ref(l + 5), {}, 24.0));
        s.addConstraint(con(ConstraintType::Distance, ref(l + 6), {}, 16.0));
        s.addConstraint(con(ConstraintType::Distance, ref(l + 1), {}, 4.0));
        s.addConstraint(con(ConstraintType::Distance, ref(l + 4), {}, 4.0));
        s.addConstraint(con(ConstraintType::Equal, ref(l + 2), ref(l + 3)));
        s.addConstraint(con(ConstraintType::DistanceX, ref(l + 3, PointPos::Start), ref(l + 2, PointPos::Start), 6.0));
        solve(s);
        section->setSketch(s);
    }
    auto* pulley = doc.create("Part::Revolve");
    pulley->setName(doc.uniqueName("Pulley"));
    pulley->props().set(ProfileFeature::kSketch, section->id());
    pulley->setColor({0.80f, 0.66f, 0.50f, 1.f});
    pulley->setPlacement({{110, 0, 0}, {0, 0, 0}});
    section->setVisible(false);

    ctx.commit("Load sketch demo");
    ctx.fitAll();
    ctx.status("Sketch demo loaded - double-click a sketch in the tree to edit it");
}

} // namespace cf::app::cmd
