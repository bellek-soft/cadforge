// Topological naming: face / edge references follow upstream changes.

#include "geom/Naming.h"
#include "model/Document.h"
#include "model/features/FeaFeatures.h"
#include "model/features/PartFeatures.h"

#include "TestHarness.h"

#include <nlohmann/json.hpp>

#include <cmath>

using namespace cf;

namespace {

double kPiHole2() { return 3.14159265358979323846 * 16.0 * 20.0; } // second hole r=4, h=20

/// 1-based index of the sub-shape of `s` whose centroid is closest to `p` (with matching type if given).
int nearest(const geom::Shape& s, geom::SubShapeKind kind, Vec3 p)
{
    const int n = kind == geom::SubShapeKind::Face ? s.faceCount() : s.edgeCount();
    int best = 0;
    double bd = 1e300;
    for (int i = 1; i <= n; ++i) {
        const double d = glm::length(geom::signature(s, kind, i).center - p);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

struct Scene {
    model::Document doc;
    model::Feature* box = nullptr;
    model::Feature* hole = nullptr;
    model::Feature* hole2 = nullptr;
    model::Feature* body = nullptr;

    Scene()
    {
        box = doc.create("Part::Box");
        box->props().set("length", 40.0);
        box->props().set("width", 40.0);
        box->props().set("height", 20.0);
        hole = doc.create("Part::Cylinder");
        hole->props().set("radius", 5.0);
        hole->props().set("height", 60.0);
        hole->setPlacement({{12, 12, -10}, {0, 0, 0}});
        hole2 = doc.create("Part::Cylinder");
        hole2->props().set("radius", 4.0);
        hole2->props().set("height", 60.0);
        hole2->setPlacement({{28, 28, -10}, {0, 0, 0}});
        body = doc.create("Part::Boolean");
        body->props().set(model::BooleanFeature::kOperation, int(model::BooleanFeature::Cut));
        body->props().set(model::BooleanFeature::kBase, box->id());
        body->props().set(model::BooleanFeature::kTools, std::vector<FeatureId>{hole->id()});
        doc.recompute();
    }
};

} // namespace

TEST(naming_signatures)
{
    model::Document doc;
    auto* b = doc.create("Part::Box");
    doc.recompute();
    const auto& s = b->shape();
    const auto top = geom::signature(s, geom::SubShapeKind::Face, nearest(s, geom::SubShapeKind::Face, {10, 10, 20}));
    CHECK(top.valid());
    CHECK_NEAR(top.size, 400.0, 1e-6);
    CHECK_NEAR(std::abs(top.dir.z), 1.0, 1e-9);
    CHECK(!geom::signature(s, geom::SubShapeKind::Edge, 13).valid());
    for (int i = 1; i <= 12; ++i) {
        const auto m = geom::findBestMatch(s, geom::SubShapeKind::Edge, geom::signature(s, geom::SubShapeKind::Edge, i));
        CHECK(m.index == i);
        CHECK(m.distance < 1e-12);
    }
}

TEST(naming_fillet_follows_edges)
{
    Scene sc;
    auto& doc = sc.doc;
    // Fillet the top edge along X at y = 40 and the top edge of the hole.
    const int topEdge = nearest(sc.body->shape(), geom::SubShapeKind::Edge, {20, 40, 20});
    const int holeEdge = nearest(sc.body->shape(), geom::SubShapeKind::Edge, {12, 12, 20});
    auto* fil = doc.create("Part::Fillet");
    fil->props().set(model::EdgeFeature::kBase, sc.body->id());
    fil->props().set(model::EdgeFeature::kEdges, std::vector<int>{topEdge, holeEdge});
    fil->props().set("radius", 1.0);
    doc.recompute();
    CHECK(fil->state() == model::FeatureState::Ok);
    CHECK(fil->subShapeRefs(model::EdgeFeature::kEdges) && fil->subShapeRefs(model::EdgeFeature::kEdges)->size() == 2);
    const double v0 = fil->shape().volume();

    // Adding a second hole changes the topology (and the edge numbering) of the base.
    sc.body->props().set(model::BooleanFeature::kTools, std::vector<FeatureId>{sc.hole->id(), sc.hole2->id()});
    doc.recompute();
    CHECK(fil->state() == model::FeatureState::Ok);
    const auto& edges = fil->props().get<std::vector<int>>(model::EdgeFeature::kEdges);
    CHECK(edges.size() == 2);
    const int newTop = nearest(sc.body->shape(), geom::SubShapeKind::Edge, {20, 40, 20});
    const int newHole = nearest(sc.body->shape(), geom::SubShapeKind::Edge, {12, 12, 20});
    CHECK(edges[0] == newTop);
    CHECK(edges[1] == newHole);
    // Same fillets, a second hole less material.
    CHECK_NEAR(fil->shape().volume(), v0 - kPiHole2(), 1e-3);

    // Changing the hole radius: the circular edge is still recognised.
    sc.hole->props().set("radius", 6.0);
    doc.recompute();
    CHECK(fil->state() == model::FeatureState::Ok);
    CHECK(fil->props().get<std::vector<int>>(model::EdgeFeature::kEdges)[1] ==
          nearest(sc.body->shape(), geom::SubShapeKind::Edge, {12, 12, 20}));

    // References survive save / load.
    const auto j = doc.toJson();
    CHECK(j["features"][4].contains("refs"));
    model::Document doc2;
    doc2.loadJson(j);
    doc2.recompute();
    CHECK(doc2.find(fil->id())->state() == model::FeatureState::Ok);
    CHECK(doc2.find(fil->id())->subShapeRefs(model::EdgeFeature::kEdges)->size() == 2);

    // Removing the hole entirely: its edge is gone -> clear error, and it comes back with the hole.
    sc.body->props().set(model::BooleanFeature::kTools, std::vector<FeatureId>{sc.hole2->id()});
    doc.recompute();
    CHECK(fil->state() == model::FeatureState::Error);
    CHECK(fil->error().find("re-pick") != std::string::npos);
    sc.body->props().set(model::BooleanFeature::kTools, std::vector<FeatureId>{sc.hole->id(), sc.hole2->id()});
    doc.recompute();
    CHECK(fil->state() == model::FeatureState::Ok);
}

TEST(naming_placement_and_fea_faces)
{
    Scene sc;
    auto& doc = sc.doc;
    auto* study = doc.create("FEA::StaticAnalysis");
    study->props().set(model::StaticAnalysisFeature::kTarget, sc.body->id());
    auto* fix = doc.create("FEA::FixedSupport");
    fix->props().set(model::FeaBoundaryFeature::kAnalysis, study->id());
    const int face = nearest(sc.body->shape(), geom::SubShapeKind::Face, {0, 20, 10}); // x = 0 side
    fix->props().set(model::FeaBoundaryFeature::kFaces, std::vector<int>{face});
    doc.recompute();
    CHECK(fix->state() == model::FeatureState::Ok);

    // Moving the solid keeps the reference (signatures are placement independent).
    sc.body->setPlacement({{100, 0, 0}, {0, 0, 30}});
    doc.recompute();
    CHECK(fix->props().get<std::vector<int>>(model::FeaBoundaryFeature::kFaces)[0] == face);

    // Topology change upstream: the support stays on the x = 0 side face.
    sc.body->setPlacement({});
    sc.body->props().set(model::BooleanFeature::kTools, std::vector<FeatureId>{sc.hole->id(), sc.hole2->id()});
    doc.recompute();
    CHECK(fix->state() == model::FeatureState::Ok);
    CHECK(fix->props().get<std::vector<int>>(model::FeaBoundaryFeature::kFaces)[0] ==
          nearest(sc.body->shape(), geom::SubShapeKind::Face, {0, 20, 10}));

    // Making the box longer moves the face's neighbours but not the face itself.
    sc.box->props().set("width", 60.0);
    doc.recompute();
    CHECK(fix->state() == model::FeatureState::Ok);
    CHECK(fix->props().get<std::vector<int>>(model::FeaBoundaryFeature::kFaces)[0] ==
          nearest(sc.body->shape(), geom::SubShapeKind::Face, {0, 30, 10}));
}
