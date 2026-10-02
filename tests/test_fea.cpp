// FEA engine tests against closed-form solutions.

#include "TestHarness.h"

#include "fea/Export.h"
#include "fea/MeshUtils.h"
#include "fea/Mesher.h"
#include "fea/Post.h"
#include "fea/Solver.h"
#include "geom/Operations.h"
#include "geom/Primitives.h"
#include "geom/Tessellator.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

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

    // Local refinement + modal setup from the document.
    auto* ref = doc.create(MeshRefinementFeature::kType);
    ref->props().set(FeaBoundaryFeature::kAnalysis, a->id());
    ref->props().set(FeaBoundaryFeature::kFaces, std::vector<int>{faceNear(surf, {0, 5, 5})});
    ref->props().set(MeshRefinementFeature::kSize, 1.0);
    doc.recompute();
    CHECK(ref->state() == FeatureState::Ok);
    const auto ms = meshSettingsOf(doc, a->id());
    CHECK(ms.localSizes.size() == 1 && ms.localSizes[0].size == 1.0);
    CHECK(buildStaticSetup(doc, a->id()).supports.size() == 1); // refinement is not a load
    a->props().set(StaticAnalysisFeature::kAnalysisType, int(StaticAnalysisFeature::Modal));
    a->props().set(StaticAnalysisFeature::kModes, 3);
    doc.recompute();
    CHECK(a->isModal());
    const auto modalSetup = buildModalSetup(doc, a->id());
    CHECK(modalSetup.modes == 3 && modalSetup.supports.size() == 1);
    doc.remove(ref->id());
    a->props().set(StaticAnalysisFeature::kAnalysisType, int(StaticAnalysisFeature::Static));
    doc.recompute();

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

TEST(fea_modal_cantilever)
{
    // Cantilever 100 x 10 x 10 steel beam, clamped at x = 0. Euler-Bernoulli:
    // f1 = 1.8751^2 / (2 pi) * sqrt(E I / (rho A L^4)) = 835.6 Hz (twice: y and z bending),
    // second bending mode 6.267 x f1. Timoshenko effects lower the FE values slightly.
    auto surf = fineSurface(geom::makeBox(100, 10, 10));
    const int xmin = faceNear(surf, {0, 5, 5});
    fea::MeshSettings ms;
    ms.maxSize = 4.0;
    ms.order = fea::ElementOrder::Quadratic;
    const auto mesh = fea::generateVolumeMesh(surf, ms);
    fea::ModalSetup setup;
    setup.supports.push_back({{xmin}});
    setup.modes = 5;
    const auto r = fea::solveModal(mesh, setup);
    const double E = 210000.0, I = 10.0 * 1000.0 / 12.0, rho = 7.85e-9, A = 100.0, L = 100.0;
    const double f1 = 1.8751 * 1.8751 / (2.0 * 3.14159265358979) * std::sqrt(E * I / (rho * A * L * L * L * L));
    std::printf("    %d equations, assembly %.2fs, solve %.2fs; f = ", r.equations, r.assemblySeconds, r.solveSeconds);
    for (double f : r.frequencies)
        std::printf("%.1f ", f);
    std::printf("Hz (beam theory %.1f)\n", f1);
    CHECK(r.frequencies.size() == 5);
    CHECK(r.shapes.size() == 5 && r.shapes[0].size() == mesh.nodeCount());
    CHECK(r.frequencies[0] > f1 * 0.95 && r.frequencies[0] < f1 * 1.03);
    CHECK(r.frequencies[1] > f1 * 0.95 && r.frequencies[1] < f1 * 1.03);
    for (std::size_t i = 1; i < r.frequencies.size(); ++i)
        CHECK(r.frequencies[i] >= r.frequencies[i - 1] - 1e-6);
    // Effective masses: the first bending pair carries ~61 % of the mass in y + z together.
    const double m1 = r.effectiveMassRatio[0].y + r.effectiveMassRatio[0].z + r.effectiveMassRatio[1].y +
                      r.effectiveMassRatio[1].z;
    CHECK(m1 > 1.1 && m1 < 1.35); // ~0.61 per direction, two directions
    CHECK(r.totalMass > 7.85e-5 * 0.9 && r.totalMass < 7.85e-5 * 1.05); // free DOFs only
    double umax = 0.0;
    for (const auto& u : r.shapes[0])
        umax = std::max(umax, glm::length(u));
    CHECK_NEAR(umax, 1.0, 1e-9);

    // Linear tetrahedra are stiffer but must be in the right range.
    ms.order = fea::ElementOrder::Linear;
    ms.maxSize = 3.0;
    const auto r4 = fea::solveModal(fea::generateVolumeMesh(surf, ms), setup);
    CHECK(r4.frequencies[0] > f1 * 0.95 && r4.frequencies[0] < f1 * 1.5);
}

TEST(fea_local_refinement)
{
    auto surf = fineSurface(geom::makeBox(40, 20, 10));
    const int top = faceNear(surf, {20, 10, 10});
    fea::MeshSettings ms;
    ms.maxSize = 6.0;
    ms.order = fea::ElementOrder::Linear;
    const auto coarse = fea::generateVolumeMesh(surf, ms);
    ms.localSizes.push_back({{top}, 1.5});
    const auto fine = fea::generateVolumeMesh(surf, ms);
    auto meanEdge = [](const fea::VolumeMesh& m, int face) {
        double sum = 0.0;
        int n = 0;
        for (std::size_t t = 0; t < m.boundaryFaceCount(); ++t) {
            if (m.boundaryFaceIds[t] != face)
                continue;
            const int* v = &m.boundaryFaces[t * 3];
            sum += glm::length(m.nodes[std::size_t(v[0])] - m.nodes[std::size_t(v[1])]);
            ++n;
        }
        return n ? sum / n : 0.0;
    };
    const int bottom = faceNear(surf, {20, 10, 0});
    std::printf("    elements %zu -> %zu, top edge %.2f -> %.2f mm, bottom %.2f mm\n", coarse.elementCount(),
                fine.elementCount(), meanEdge(coarse, top), meanEdge(fine, top), meanEdge(fine, bottom));
    CHECK(fine.elementCount() > coarse.elementCount() * 3);
    CHECK(meanEdge(fine, top) < 2.5);
    CHECK(meanEdge(fine, bottom) > meanEdge(fine, top) * 1.5);
    CHECK_NEAR(fine.volume(), 8000.0, 1e-6 * 8000.0);
}

TEST(fea_section_probe_and_vtk)
{
    auto surf = fineSurface(geom::makeBox(100, 10, 10));
    const int xmin = faceNear(surf, {0, 5, 5}), xmax = faceNear(surf, {100, 5, 5});
    fea::MeshSettings ms;
    ms.maxSize = 4.0;
    const auto mesh = fea::generateVolumeMesh(surf, ms);
    fea::StaticSetup setup;
    setup.supports.push_back({{xmin}});
    setup.forces.push_back({{xmax}, Vec3(0, 0, -100.0)});
    const auto r = fea::solveStatic(mesh, setup);

    fea::SurfaceOptions opt;
    opt.field = fea::ResultField::VonMises;
    const MeshData full = fea::resultSurface(mesh, &r, opt);
    opt.section = true;
    opt.sectionNormal = {1, 0, 0};
    opt.sectionOffset = 50.0;
    const MeshData cut = fea::resultSurface(mesh, &r, opt);
    CHECK(cut.bounds.max.x <= full.bounds.max.x);
    double maxX = -1e9;
    for (std::uint32_t v : cut.indices)
        maxX = std::max(maxX, double(cut.positions[3 * v]));
    CHECK(maxX < 50.0 + 1e-3);
    CHECK(cut.scalars.size() == cut.vertexCount());

    // Probe the cap straight on: the section at x = 50 carries M = 5000 Nmm -> sigma_max = 30 MPa
    // at z = 0 / 10 and ~0 at the neutral axis.
    fea::ProbeHit hit;
    CHECK(fea::probeSurface(cut, {200, 5, 9.5}, {-1, 0, 0}, hit));
    CHECK_NEAR(hit.point.x, 50.0, 1e-3);
    CHECK(hit.face == 0);
    CHECK(hit.value > 18.0 && hit.value < 40.0);
    CHECK(fea::probeSurface(cut, {200, 5, 5.0}, {-1, 0, 0}, hit));
    CHECK(hit.value < 6.0);
    CHECK(!fea::probeSurface(cut, {200, 50, 5.0}, {-1, 0, 0}, hit));

    // VTK export
    const auto path = (std::filesystem::temp_directory_path() / "cadforge_test.vtu").string();
    fea::ModalResult modal;
    modal.frequencies = {123.0};
    modal.shapes = {std::vector<Vec3>(mesh.nodeCount(), Vec3(0, 0, 1))};
    fea::writeVtu(path, mesh, &r, &modal);
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    CHECK(text.find("NumberOfPoints=\"" + std::to_string(mesh.nodeCount()) + "\"") != std::string::npos);
    CHECK(text.find("NumberOfCells=\"" + std::to_string(mesh.elementCount()) + "\"") != std::string::npos);
    CHECK(text.find("Name=\"Displacement\"") != std::string::npos);
    CHECK(text.find("Mode 1 (123.00 Hz)") != std::string::npos);
    CHECK(text.find(mesh.nodesPerElement == 10 ? "24 24" : "10 10") != std::string::npos);
    std::filesystem::remove(path);
}
