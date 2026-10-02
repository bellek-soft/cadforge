// FEA engine tests against closed-form solutions.

#include "TestHarness.h"

#include "fea/MeshUtils.h"
#include "fea/Mesher.h"
#include "fea/Post.h"
#include "fea/Solver.h"
#include "geom/Operations.h"
#include "geom/Primitives.h"
#include "geom/Tessellator.h"

#include <cstdio>

using namespace cf;

namespace {

MeshData fineSurface(const geom::Shape& s)
{
    geom::TessellationParams p;
    p.relativeDeflection = 0.0005;
    p.angularDeflectionDeg = 8.0;
    return geom::tessellate(s, p);
}

/// 1-based id of the face whose centroid is closest to `target`.
int faceNear(const MeshData& m, const Vec3& target)
{
    int best = 0;
    double bestD = 1e300;
    for (std::size_t f = 0; f < m.faceRanges.size(); ++f) {
        const auto& r = m.faceRanges[f];
        Vec3 c(0.0);
        for (std::uint32_t i = r.first; i < r.first + r.count; ++i) {
            const auto v = m.indices[i];
            c += Vec3(m.positions[3 * v], m.positions[3 * v + 1], m.positions[3 * v + 2]);
        }
        c /= double(r.count);
        if (glm::distance(c, target) < bestD) {
            bestD = glm::distance(c, target);
            best = int(f) + 1;
        }
    }
    return best;
}

double maxAbsComponent(const fea::StaticResult& r, int comp)
{
    double m = 0;
    for (const auto& u : r.displacement)
        m = std::max(m, std::abs(u[comp]));
    return m;
}

} // namespace

TEST(fea_weld_and_classify)
{
    auto surf = fineSurface(geom::makeBox(10, 20, 30));
    auto w = fea::detail::weld(surf);
    CHECK(w.points.size() == 8 || w.points.size() > 8); // box: at least the 8 corners
    CHECK(fea::detail::openEdgeCount(w) == 0);
    CHECK(fea::detail::featureEdges(w).size() >= 12);
    fea::detail::FaceClassifier cls(w);
    const int top = faceNear(surf, {5, 10, 30});
    CHECK(cls.classify({5, 10, 30.001}) == top);
    CHECK(fea::detail::pointTriangleDistanceSq({0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}) == 1.0);
}

TEST(fea_mesh_box)
{
    auto surf = fineSurface(geom::makeBox(40, 20, 10));
    fea::MeshSettings ms;
    ms.maxSize = 5.0;
    for (auto order : {fea::ElementOrder::Linear, fea::ElementOrder::Quadratic}) {
        ms.order = order;
        const auto mesh = fea::generateVolumeMesh(surf, ms);
        std::printf("    %s: %zu nodes, %zu tets, %zu boundary tris\n",
                    order == fea::ElementOrder::Linear ? "Tet4" : "Tet10", mesh.nodeCount(), mesh.elementCount(),
                    mesh.boundaryFaceCount());
        CHECK(mesh.nodesPerElement == (order == fea::ElementOrder::Linear ? 4 : 10));
        CHECK(mesh.elementCount() > 50);
        CHECK_NEAR(mesh.volume(), 8000.0, 1e-6 * 8000.0);
        // Every boundary triangle maps to one of the 6 faces; every face is present.
        std::vector<int> count(7, 0);
        for (int id : mesh.boundaryFaceIds) {
            CHECK(id >= 1 && id <= 6);
            if (id >= 1 && id <= 6)
                ++count[std::size_t(id)];
        }
        for (int f = 1; f <= 6; ++f)
            CHECK(count[std::size_t(f)] > 0);
        // Boundary triangles point outwards: sum of area vectors vanishes, and
        // the top face normals point +Z.
        const int top = faceNear(surf, {20, 10, 10});
        const std::size_t nf = std::size_t(mesh.nodesPerFace);
        for (std::size_t t = 0; t < mesh.boundaryFaceCount(); ++t) {
            if (mesh.boundaryFaceIds[t] != top)
                continue;
            const int* tn = &mesh.boundaryFaces[t * nf];
            const Vec3 n = glm::cross(mesh.nodes[std::size_t(tn[1])] - mesh.nodes[std::size_t(tn[0])],
                                      mesh.nodes[std::size_t(tn[2])] - mesh.nodes[std::size_t(tn[0])]);
            CHECK(n.z > 0);
        }
    }
}

TEST(fea_tension_bar)
{
    // Bar 100 x 10 x 10 pulled along X: delta = F L / (E A), sigma = F / A.
    // Lateral contraction is free (support only in X on the end face, plus
    // minimal Y/Z restraints on the edge faces would be ideal; here the end is
    // fixed in X only and one side face is fixed in Y and the bottom in Z).
    auto surf = fineSurface(geom::makeBox(100, 10, 10));
    const int xmin = faceNear(surf, {0, 5, 5}), xmax = faceNear(surf, {100, 5, 5});
    const int ymin = faceNear(surf, {50, 0, 5}), zmin = faceNear(surf, {50, 5, 0});
    for (auto order : {fea::ElementOrder::Linear, fea::ElementOrder::Quadratic}) {
        fea::MeshSettings ms;
        ms.maxSize = 4.0;
        ms.order = order;
        const auto mesh = fea::generateVolumeMesh(surf, ms);
        fea::StaticSetup setup;
        setup.supports.push_back({{xmin}, true, false, false});
        setup.supports.push_back({{ymin}, false, true, false});
        setup.supports.push_back({{zmin}, false, false, true});
        setup.forces.push_back({{xmax}, Vec3(1000.0, 0, 0)});
        const auto r = fea::solveStatic(mesh, setup);
        const double expected = 1000.0 * 100.0 / (210000.0 * 100.0);
        std::printf("    %s: dx = %.6f mm (exact %.6f), max vM = %.3f MPa (exact 10)\n",
                    order == fea::ElementOrder::Linear ? "Tet4" : "Tet10", maxAbsComponent(r, 0), expected,
                    r.maxVonMises);
        CHECK_NEAR(maxAbsComponent(r, 0), expected, expected * 0.01);
        CHECK_NEAR(r.maxVonMises, 10.0, 0.5);
        // Equilibrium: reactions balance the applied load.
        CHECK_NEAR(r.appliedLoad.x, 1000.0, 1e-6);
        CHECK_NEAR(r.reaction.x, -1000.0, 1e-3);
    }
}

TEST(fea_cantilever_tet10)
{
    // Cantilever 100 x 10 x 10, clamped at x = 0, 100 N downwards at the free end.
    // Euler-Bernoulli tip deflection F L^3 / (3 E I) = 0.1905 mm; Timoshenko shear
    // correction adds ~0.4 %. Quadratic tets should be within a few percent.
    auto surf = fineSurface(geom::makeBox(100, 10, 10));
    const int xmin = faceNear(surf, {0, 5, 5}), xmax = faceNear(surf, {100, 5, 5});
    fea::MeshSettings ms;
    ms.maxSize = 3.0;
    ms.order = fea::ElementOrder::Quadratic;
    const auto mesh = fea::generateVolumeMesh(surf, ms);
    fea::StaticSetup setup;
    setup.supports.push_back({{xmin}});
    setup.forces.push_back({{xmax}, Vec3(0, 0, -100.0)});
    const auto r = fea::solveStatic(mesh, setup);
    const double I = 10.0 * 1000.0 / 12.0;
    const double eb = 100.0 * 1e6 / (3.0 * 210000.0 * I);
    const double tip = maxAbsComponent(r, 2);
    std::printf("    %zu nodes, %d equations, %s, assembly %.2fs, solve %.2fs\n", mesh.nodeCount(), r.equations,
                r.solver.c_str(), r.assemblySeconds, r.solveSeconds);
    std::printf("    tip deflection %.5f mm (Euler-Bernoulli %.5f), max vM %.2f MPa (beam theory 60)\n", tip, eb,
                r.maxVonMises);
    CHECK(tip > eb * 0.97 && tip < eb * 1.06);
    CHECK_NEAR(r.reaction.z, 100.0, 1e-3);
    // Peak bending stress M c / I = 60 MPa at the clamp (singular corners give somewhat more).
    CHECK(r.maxVonMises > 50.0 && r.maxVonMises < 120.0);

    // Same problem with the iterative solver.
    fea::SolveControl ctl;
    ctl.directSolverLimit = 10;
    const auto ri = fea::solveStatic(mesh, setup, ctl);
    CHECK_NEAR(maxAbsComponent(ri, 2), tip, tip * 1e-4);
}

TEST(fea_gravity_and_pressure)
{
    // A 50 mm steel cube standing on its bottom face.
    auto surf = fineSurface(geom::makeBox(50, 50, 50));
    const int bottom = faceNear(surf, {25, 25, 0}), top = faceNear(surf, {25, 25, 50});
    fea::MeshSettings ms;
    ms.maxSize = 12.0;
    ms.order = fea::ElementOrder::Quadratic;
    const auto mesh = fea::generateVolumeMesh(surf, ms);
    fea::StaticSetup setup;
    setup.supports.push_back({{bottom}});
    setup.gravity = true;
    setup.pressures.push_back({{top}, 2.0}); // 2 MPa on 2500 mm^2 = 5000 N downwards
    const auto r = fea::solveStatic(mesh, setup);
    const double weight = 7.85e-9 * 125000.0 * 9810.0; // N
    std::printf("    applied %.4f N, reaction %.4f N, expected %.4f N\n", r.appliedLoad.z, r.reaction.z,
                -(5000.0 + weight));
    CHECK_NEAR(r.appliedLoad.z, -(5000.0 + weight), 1e-6 * 5000.0);
    CHECK_NEAR(r.reaction.z, 5000.0 + weight, 1e-3);
}

TEST(fea_errors)
{
    auto surf = fineSurface(geom::makeBox(10, 10, 10));
    fea::MeshSettings ms;
    ms.maxSize = 5;
    const auto mesh = fea::generateVolumeMesh(surf, ms);
    bool threw = false;
    try {
        fea::solveStatic(mesh, fea::StaticSetup{}); // no supports
    } catch (const fea::FeaError&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        fea::StaticSetup s;
        s.supports.push_back({{faceNear(surf, {0, 5, 5})}, true, false, false}); // only X: mechanism
        s.forces.push_back({{faceNear(surf, {10, 5, 5})}, Vec3(10, 0, 0)});
        fea::solveStatic(mesh, s);
    } catch (const fea::FeaError& e) {
        threw = true;
        std::printf("    expected error: %s\n", e.what());
    }
    CHECK(threw);

    // The iterative (AMG) path must detect the mechanism as well.
    threw = false;
    try {
        fea::StaticSetup s;
        s.supports.push_back({{faceNear(surf, {0, 5, 5})}, true, false, false});
        s.forces.push_back({{faceNear(surf, {10, 5, 5})}, Vec3(10, 0, 0)});
        fea::SolveControl ctl;
        ctl.method = fea::SolveControl::Method::AmgCg;
        fea::solveStatic(mesh, s, ctl);
    } catch (const fea::FeaError& e) {
        threw = true;
        std::printf("    expected error (AMG): %s\n", e.what());
    }
    CHECK(threw);
}

TEST(fea_mesh_with_hole_and_post)
{
    auto shape = geom::booleanOp(geom::BooleanOp::Cut, geom::makeBox(60, 30, 10),
                                 {geom::makeCylinder(6, 10).transformed(Mat4(1.0)) });
    auto surf = fineSurface(shape);
    fea::MeshSettings ms;
    ms.maxSize = 4.0;
    const auto mesh = fea::generateVolumeMesh(surf, ms);
    std::printf("    %zu nodes, %zu tets\n", mesh.nodeCount(), mesh.elementCount());
    CHECK(mesh.elementCount() > 100);
    CHECK_NEAR(mesh.volume(), shape.volume(), shape.volume() * 0.01);
    std::vector<int> seen(std::size_t(shape.faceCount()) + 1, 0);
    for (int id : mesh.boundaryFaceIds)
        if (id >= 1 && id <= shape.faceCount())
            seen[std::size_t(id)] = 1;
    int covered = 0;
    for (int f = 1; f <= shape.faceCount(); ++f)
        covered += seen[std::size_t(f)];
    CHECK(covered == shape.faceCount());

    fea::SurfaceOptions so;
    const MeshData vis = fea::resultSurface(mesh, nullptr, so);
    CHECK(vis.triangleCount() >= mesh.boundaryFaceCount() * 4);
    CHECK(vis.faceRanges.size() == std::size_t(shape.faceCount()));
    CHECK(!vis.edgePositions.empty());
}

#include "model/Document.h"
#include "model/features/FeaFeatures.h"

#include <nlohmann/json.hpp>

TEST(fea_document_end_to_end)
{
    using namespace cf::model;
    Document doc;
    auto* beam = doc.create("Part::Box");
    beam->props().set("length", 100.0);
    beam->props().set("width", 10.0);
    beam->props().set("height", 10.0);
    doc.recompute();
    auto surf = fineSurface(beam->shape());

    auto* a = static_cast<StaticAnalysisFeature*>(doc.create(StaticAnalysisFeature::kType));
    a->props().set(StaticAnalysisFeature::kTarget, beam->id());
    a->props().set(StaticAnalysisFeature::kElementSize, 3.0);
    a->props().set(StaticAnalysisFeature::kMaterial, 2); // aluminium
    CHECK(a->onPropertyEdited(StaticAnalysisFeature::kMaterial));
    CHECK_NEAR(a->material().youngsModulus, 68900.0, 1e-9);

    auto* fix = doc.create(FixedSupportFeature::kType);
    fix->props().set(FeaBoundaryFeature::kAnalysis, a->id());
    fix->props().set(FeaBoundaryFeature::kFaces, std::vector<int>{faceNear(surf, {0, 5, 5})});
    auto* force = doc.create(ForceFeature::kType);
    force->props().set(FeaBoundaryFeature::kAnalysis, a->id());
    force->props().set(FeaBoundaryFeature::kFaces, std::vector<int>{faceNear(surf, {100, 5, 5})});
    force->props().set(ForceFeature::kForce, Vec3(0, 0, -100.0));
    const auto stats = doc.recompute();
    CHECK(stats.failed == 0);
    CHECK(a->state() == FeatureState::Ok);
    CHECK(fix->state() == FeatureState::Ok);

    // Tree: loads nest under the analysis, the beam stays a root (not consumed).
    const auto roots = doc.roots();
    CHECK(roots.size() == 2);
    CHECK(doc.nestedChildren(a->id()).size() == 2);
    CHECK(doc.find(fix->id())->subShapeTarget(doc) == beam->id());

    const auto setup = buildStaticSetup(doc, a->id());
    CHECK(setup.supports.size() == 1 && setup.forces.size() == 1);
    const auto mesh = fea::generateVolumeMesh(surf, a->meshSettings());
    const auto r = fea::solveStatic(mesh, setup);
    const double eb = 100.0 * 1e6 / (3.0 * 68900.0 * (1e4 / 12.0));
    CHECK(maxAbsComponent(r, 2) > eb * 0.97 && maxAbsComponent(r, 2) < eb * 1.06);

    // Invalid face index -> the load reports an error, setup refuses to build.
    force->props().set(FeaBoundaryFeature::kFaces, std::vector<int>{99});
    doc.recompute();
    CHECK(force->state() == FeatureState::Error);
    bool threw = false;
    try { buildStaticSetup(doc, a->id()); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // Round trip through JSON.
    Document doc2;
    doc2.loadJson(doc.toJson());
    doc2.recompute();
    CHECK(doc2.features().size() == 4);
    CHECK(doc2.toJson() == doc.toJson());
}
