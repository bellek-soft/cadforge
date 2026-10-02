// Kernel + model tests. Deliberately framework-free (no extra dependency):
// each TEST registers itself, main() runs them all and reports failures.

#include "core/Placement.h"
#include "geom/Operations.h"
#include "geom/Primitives.h"
#include "geom/ShapeIO.h"
#include "geom/Tessellator.h"
#include "model/Document.h"
#include "model/History.h"
#include "model/features/PartFeatures.h"

#include "TestHarness.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace cf;

namespace {
constexpr double kPi = 3.14159265358979323846;

model::Feature* box(model::Document& doc, double l, double w, double h)
{
    auto* f = doc.create("Part::Box");
    f->props().set("length", l);
    f->props().set("width", w);
    f->props().set("height", h);
    return f;
}
} // namespace

TEST(placement_roundtrip)
{
    Placement p;
    p.position = {1, 2, 3};
    p.rotationDeg = {10, 20, 30};
    Placement q = Placement::fromMatrix(p.matrix());
    for (int i = 0; i < 3; ++i) {
        CHECK_NEAR(q.position[i], p.position[i], 1e-9);
        CHECK_NEAR(q.rotationDeg[i], p.rotationDeg[i], 1e-9);
    }
}

TEST(primitive_volumes)
{
    CHECK_NEAR(geom::makeBox(10, 20, 30).volume(), 6000.0, 1e-6);
    CHECK_NEAR(geom::makeCylinder(5, 10).volume(), kPi * 25 * 10, 1e-6);
    CHECK_NEAR(geom::makeSphere(3).volume(), 4.0 / 3.0 * kPi * 27, 1e-4);
    CHECK(geom::makeBox(1, 1, 1).faceCount() == 6);
    CHECK(geom::makeBox(1, 1, 1).edgeCount() == 12);
    bool threw = false;
    try { geom::makeBox(0, 1, 1); } catch (const geom::GeomError&) { threw = true; }
    CHECK(threw);
}

TEST(tessellation_keeps_topology)
{
    auto s = geom::makeBox(10, 10, 10);
    auto m = geom::tessellate(s);
    CHECK(m.faceRanges.size() == 6);
    CHECK(m.edgeRanges.size() == 12);
    CHECK(m.triangleCount() >= 12);
    CHECK(m.faceIds.size() == m.vertexCount());
    CHECK(m.edgeIds.size() == m.edgeVertexCount());
    for (auto& r : m.faceRanges) CHECK(r.count >= 6);
    CHECK(m.bounds.valid());
    CHECK_NEAR(m.bounds.max.x, 10.0, 1e-6);
}

TEST(document_boolean_cut)
{
    model::Document doc;
    auto* b = box(doc, 20, 20, 20);
    auto* c = doc.create("Part::Cylinder");
    c->props().set("radius", 5.0);
    c->props().set("height", 40.0);
    c->setPlacement({{10, 10, -10}, {0, 0, 0}});

    auto* cut = doc.create("Part::Boolean");
    cut->props().set(model::BooleanFeature::kOperation, int(model::BooleanFeature::Cut));
    cut->props().set(model::BooleanFeature::kBase, b->id());
    cut->props().set(model::BooleanFeature::kTools, std::vector<FeatureId>{c->id()});

    auto stats = doc.recompute();
    CHECK(stats.failed == 0);
    CHECK(cut->state() == model::FeatureState::Ok);
    CHECK_NEAR(cut->shape().volume(), 8000.0 - kPi * 25 * 20, 1e-3);

    CHECK(doc.isConsumed(b->id()));
    CHECK(doc.isConsumed(c->id()));
    CHECK(doc.roots().size() == 1 && doc.roots()[0] == cut->id());
    CHECK(!doc.canReference(b->id(), cut->id())); // would create a cycle

    // Nothing changed -> nothing re-executed.
    CHECK(doc.recompute().executed == 0);
    // Moving the tool re-executes only the boolean.
    c->setPlacement({{0, 0, -10}, {0, 0, 0}});
    CHECK(doc.recompute().executed == 1);
    CHECK_NEAR(cut->shape().volume(), 8000.0 - kPi * 25 * 20 / 4.0, 1e-3);
}

TEST(fillet_and_chamfer)
{
    model::Document doc;
    auto* b = box(doc, 10, 10, 10);
    auto* fil = doc.create("Part::Fillet");
    fil->props().set(model::EdgeFeature::kBase, b->id());
    fil->props().set(model::EdgeFeature::kEdges, std::vector<int>{1, 2, 3});
    fil->props().set("radius", 1.0);
    auto* ch = doc.create("Part::Chamfer");
    ch->props().set(model::EdgeFeature::kBase, fil->id());
    ch->props().set(model::EdgeFeature::kEdges, std::vector<int>{1});
    ch->props().set("distance", 0.5);
    doc.recompute();
    CHECK(fil->state() == model::FeatureState::Ok);
    CHECK(fil->shape().volume() < 1000.0);
    CHECK(fil->shape().faceCount() > 6);
    CHECK(ch->state() == model::FeatureState::Ok);
    CHECK(ch->shape().volume() < fil->shape().volume());

    // Invalid radius -> error state, not a crash; downstream fails too.
    fil->props().set("radius", 50.0);
    doc.recompute();
    CHECK(fil->state() == model::FeatureState::Error);
    CHECK(ch->state() == model::FeatureState::Error);
}

TEST(json_roundtrip_and_undo)
{
    model::Document doc;
    model::History hist;
    hist.reset(doc);
    auto* b = box(doc, 10, 20, 30);
    hist.commit(doc, "Add box");
    b->props().set("height", 50.0);
    hist.commit(doc, "Edit");
    doc.recompute();
    CHECK_NEAR(doc.features()[0]->shape().volume(), 10000.0, 1e-6);

    auto j = doc.toJson();
    model::Document doc2;
    doc2.loadJson(j);
    doc2.recompute();
    CHECK(doc2.features().size() == 1);
    CHECK_NEAR(doc2.features()[0]->shape().volume(), 10000.0, 1e-6);
    CHECK(doc2.toJson() == j);

    CHECK(hist.undo(doc));
    doc.recompute();
    CHECK_NEAR(doc.features()[0]->shape().volume(), 6000.0, 1e-6);
    CHECK(hist.undo(doc));
    doc.recompute();
    CHECK(doc.features().empty());
    CHECK(!hist.canUndo());
    CHECK(hist.redo(doc) && hist.redo(doc));
    doc.recompute();
    CHECK_NEAR(doc.features()[0]->shape().volume(), 10000.0, 1e-6);
}

TEST(remove_cascades)
{
    model::Document doc;
    auto* a = box(doc, 10, 10, 10);
    auto* b = box(doc, 5, 5, 5);
    auto* u = doc.create("Part::Boolean");
    u->props().set(model::BooleanFeature::kBase, a->id());
    u->props().set(model::BooleanFeature::kTools, std::vector<FeatureId>{b->id()});
    auto removed = doc.remove(a->id());
    CHECK(removed.size() == 2);
    CHECK(doc.features().size() == 1);
}

TEST(step_and_stl_export_import)
{
    auto dir = std::filesystem::temp_directory_path();
    auto step = (dir / "cadforge_test.step").string();
    auto stl = (dir / "cadforge_test.stl").string();
    auto s = geom::booleanOp(geom::BooleanOp::Union, geom::makeBox(10, 10, 10),
                             {geom::makeSphere(6)});
    geom::exportStep({s}, step);
    geom::exportStl({s}, stl);
    auto r = geom::importStep(step);
    CHECK_NEAR(r.volume(), s.volume(), s.volume() * 1e-4);
    CHECK(std::filesystem::file_size(stl) > 84);
    std::filesystem::remove(step);
    std::filesystem::remove(stl);
}
