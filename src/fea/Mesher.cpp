#include "fea/Mesher.h"
#include "fea/MeshUtils.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>
#include <sstream>

#include <core/ngcore_api.hpp>
#include <core/ngstream.hpp>
namespace nglib {
#include <nglib.h>
}

namespace cf::fea {

namespace {

std::mutex g_netgenMutex; // nglib keeps global state: one meshing job at a time

const char* resultText(nglib::Ng_Result r)
{
    switch (r) {
    case nglib::NG_OK: return "ok";
    case nglib::NG_SURFACE_INPUT_ERROR: return "invalid surface (the solid must be closed)";
    case nglib::NG_VOLUME_FAILURE: return "volume meshing failed";
    case nglib::NG_STL_INPUT_ERROR: return "invalid surface triangulation";
    case nglib::NG_SURFACE_FAILURE: return "surface meshing failed";
    case nglib::NG_FILE_NOT_FOUND: return "file not found";
    default: return "unknown error";
    }
}

void check(nglib::Ng_Result r, const char* stage)
{
    if (r != nglib::NG_OK)
        throw FeaError(std::string("Meshing failed during ") + stage + ": " + resultText(r));
}

/// RAII owner for an nglib mesh.
struct NgMesh {
    nglib::Ng_Mesh* m = nglib::Ng_NewMesh();
    ~NgMesh() { nglib::Ng_DeleteMesh(m); }
};

} // namespace

std::string mesherName()
{
    return "Netgen (nglib)";
}

double automaticElementSize(const MeshData& surface)
{
    const double diag = surface.bounds.diagonal();
    return diag > 0 ? diag / 20.0 : 1.0;
}

VolumeMesh generateVolumeMesh(const MeshData& surface, const MeshSettings& settings)
{
    if (surface.indices.empty())
        throw FeaError("Nothing to mesh: the geometry has no faces");

    const detail::WeldedSurface welded = detail::weld(surface);
    if (welded.triangles.size() < 4)
        throw FeaError("Nothing to mesh: the geometry is degenerate");
    if (const int open = detail::openEdgeCount(welded); open > 0)
        throw FeaError("The geometry is not a closed solid (" + std::to_string(open) +
                       " open edges); only solids can be meshed");

    const double h = settings.maxSize > 0 ? settings.maxSize : automaticElementSize(surface);

    std::lock_guard lock(g_netgenMutex);
    static std::once_flag initOnce;
    std::call_once(initOnce, [] {
        nglib::Ng_Init();
        ngcore::printmessage_importance = 0; // keep Netgen quiet
    });

    // Netgen writes progress chatter to std::cout; silence it while meshing.
    struct CoutSilencer {
        std::ostringstream sink;
        std::streambuf* old = std::cout.rdbuf(sink.rdbuf());
        ~CoutSilencer() { std::cout.rdbuf(old); }
    } silencer;

    try {
        // Note: nglib has no API to free an STL geometry; it is small (input triangles only).
        nglib::Ng_STL_Geometry* geom = nglib::Ng_STL_NewGeometry();
        for (const auto& t : welded.triangles) {
            double p[3][3];
            for (int k = 0; k < 3; ++k)
                for (int c = 0; c < 3; ++c)
                    p[k][c] = welded.points[std::size_t(t[std::size_t(k)])][c];
            const Vec3 n = glm::normalize(glm::cross(welded.points[std::size_t(t[1])] - welded.points[std::size_t(t[0])],
                                                     welded.points[std::size_t(t[2])] - welded.points[std::size_t(t[0])]));
            double nn[3] = {n.x, n.y, n.z};
            nglib::Ng_STL_AddTriangle(geom, p[0], p[1], p[2], nn);
        }
        // Keep the CAD face boundaries as mesh edges, so every mesh triangle lies on one CAD face.
        for (const auto& e : detail::featureEdges(welded)) {
            double a[3], b[3];
            for (int c = 0; c < 3; ++c) {
                a[c] = welded.points[std::size_t(e[0])][c];
                b[c] = welded.points[std::size_t(e[1])][c];
            }
            nglib::Ng_STL_AddEdge(geom, a, b);
        }
        check(nglib::Ng_STL_InitSTLGeometry(geom), "geometry setup");

        nglib::Ng_Meshing_Parameters mp;
        mp.maxh = h;
        mp.minh = settings.minSize > 0 ? settings.minSize : 0.0;
        mp.grading = std::clamp(settings.grading, 0.05, 1.0);
        mp.fineness = 0.5;
        mp.second_order = 0;
        mp.optsteps_3d = 3;

        NgMesh mesh;
        check(nglib::Ng_STL_MakeEdges(geom, mesh.m, &mp), "edge detection");
        check(nglib::Ng_STL_GenerateSurfaceMesh(geom, mesh.m, &mp), "surface meshing");
        check(nglib::Ng_GenerateVolumeMesh(mesh.m, &mp), "volume meshing");
        if (settings.order == ElementOrder::Quadratic)
            nglib::Ng_STL_Generate_SecondOrder(geom, mesh.m);

        // ---- extract ----
        VolumeMesh out;
        const int np = nglib::Ng_GetNP(mesh.m);
        const int ne = nglib::Ng_GetNE(mesh.m);
        const int nse = nglib::Ng_GetNSE(mesh.m);
        if (ne == 0)
            throw FeaError("Meshing produced no elements (element size too large?)");

        out.nodes.resize(std::size_t(np));
        for (int i = 1; i <= np; ++i) {
            double x[3];
            nglib::Ng_GetPoint(mesh.m, i, x);
            out.nodes[std::size_t(i - 1)] = Vec3(x[0], x[1], x[2]);
        }

        int pi[20];
        out.nodesPerElement = 0;
        for (int i = 1; i <= ne; ++i) {
            const auto t = nglib::Ng_GetVolumeElement(mesh.m, i, pi);
            const int n = t == nglib::NG_TET10 ? 10 : t == nglib::NG_TET ? 4 : 0;
            if (n == 0)
                throw FeaError("Unexpected non-tetrahedral element from the mesher");
            if (out.nodesPerElement == 0)
                out.nodesPerElement = n;
            if (n != out.nodesPerElement)
                throw FeaError("Mixed element orders from the mesher");
            for (int k = 0; k < n; ++k)
                out.elements.push_back(pi[k] - 1);
        }

        out.nodesPerFace = out.nodesPerElement == 10 ? 6 : 3;
        const detail::FaceClassifier classifier(welded);
        for (int i = 1; i <= nse; ++i) {
            const auto t = nglib::Ng_GetSurfaceElement(mesh.m, i, pi);
            const int n = t == nglib::NG_TRIG6 ? 6 : t == nglib::NG_TRIG ? 3 : 0;
            if (n != out.nodesPerFace)
                continue;
            for (int k = 0; k < n; ++k)
                out.boundaryFaces.push_back(pi[k] - 1);
            const Vec3 c = (out.nodes[std::size_t(pi[0] - 1)] + out.nodes[std::size_t(pi[1] - 1)] +
                            out.nodes[std::size_t(pi[2] - 1)]) / 3.0;
            out.boundaryFaceIds.push_back(classifier.classify(c));
        }

        detail::canonicalize(out);
        return out;
    } catch (const FeaError&) {
        throw;
    } catch (const std::exception& e) {
        throw FeaError(std::string("Meshing failed: ") + e.what());
    }
}

} // namespace cf::fea
