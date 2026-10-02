#include "fea/Export.h"

#include "core/Paths.h"

#include <cstdio>
#include <fstream>

namespace cf::fea {

namespace {

void scalarArray(std::ostream& out, const char* name, const std::vector<double>& v)
{
    out << "        <DataArray type=\"Float64\" Name=\"" << name << "\" format=\"ascii\">\n";
    for (std::size_t i = 0; i < v.size(); ++i)
        out << v[i] << ((i % 8 == 7) ? "\n" : " ");
    out << "\n        </DataArray>\n";
}

void vectorArray(std::ostream& out, const std::string& name, const std::vector<Vec3>& v)
{
    out << "        <DataArray type=\"Float64\" Name=\"" << name << "\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    for (const auto& p : v)
        out << p.x << ' ' << p.y << ' ' << p.z << '\n';
    out << "        </DataArray>\n";
}

} // namespace

void writeVtu(const std::string& path, const VolumeMesh& mesh, const StaticResult* stat, const ModalResult* modal)
{
    if (mesh.empty())
        throw FeaError("Nothing to export: the mesh is empty");
    std::ofstream out(paths::fromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!out)
        throw FeaError("Cannot open '" + path + "' for writing");
    out.precision(10);

    const std::size_t nn = mesh.nodeCount(), ne = mesh.elementCount();
    const int nen = mesh.nodesPerElement;
    out << "<?xml version=\"1.0\"?>\n"
           "<VTKFile type=\"UnstructuredGrid\" version=\"1.0\" byte_order=\"LittleEndian\" header_type=\"UInt64\">\n"
           "  <UnstructuredGrid>\n"
        << "    <Piece NumberOfPoints=\"" << nn << "\" NumberOfCells=\"" << ne << "\">\n";

    // ---- point data ----
    out << "      <PointData" << (stat ? " Scalars=\"von Mises\" Vectors=\"Displacement\"" : "") << ">\n";
    {
        std::vector<double> faceId(nn, 0.0);
        const int npf = mesh.nodesPerFace;
        for (std::size_t t = 0; t < mesh.boundaryFaceCount(); ++t)
            for (int k = 0; k < npf; ++k)
                faceId[std::size_t(mesh.boundaryFaces[t * std::size_t(npf) + std::size_t(k)])] =
                    mesh.boundaryFaceIds[t];
        scalarArray(out, "BRep face", faceId);
    }
    if (stat && stat->displacement.size() == nn) {
        vectorArray(out, "Displacement", stat->displacement);
        scalarArray(out, "von Mises", stat->vonMises);
        out << "        <DataArray type=\"Float64\" Name=\"Stress\" NumberOfComponents=\"6\" "
               "ComponentName0=\"XX\" ComponentName1=\"YY\" ComponentName2=\"ZZ\" ComponentName3=\"XY\" "
               "ComponentName4=\"YZ\" ComponentName5=\"XZ\" format=\"ascii\">\n";
        for (const auto& s : stat->stress)
            out << s[0] << ' ' << s[1] << ' ' << s[2] << ' ' << s[3] << ' ' << s[4] << ' ' << s[5] << '\n';
        out << "        </DataArray>\n";
    }
    if (modal)
        for (std::size_t m = 0; m < modal->shapes.size(); ++m) {
            if (modal->shapes[m].size() != nn)
                continue;
            char name[64];
            std::snprintf(name, sizeof(name), "Mode %zu (%.2f Hz)", m + 1, modal->frequencies[m]);
            vectorArray(out, name, modal->shapes[m]);
        }
    out << "      </PointData>\n";

    // ---- geometry ----
    out << "      <Points>\n";
    vectorArray(out, "Points", mesh.nodes);
    out << "      </Points>\n      <Cells>\n"
           "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n";
    // CadForge's Tet10 order (mid-edges 01 12 20 03 13 23) equals VTK_QUADRATIC_TETRA.
    for (std::size_t e = 0; e < ne; ++e) {
        for (int k = 0; k < nen; ++k)
            out << mesh.elements[e * std::size_t(nen) + std::size_t(k)] << (k + 1 < nen ? ' ' : '\n');
    }
    out << "        </DataArray>\n        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n";
    for (std::size_t e = 0; e < ne; ++e)
        out << (e + 1) * std::size_t(nen) << ((e % 16 == 15) ? "\n" : " ");
    out << "\n        </DataArray>\n        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
    const int type = nen == 10 ? 24 : 10; // VTK_QUADRATIC_TETRA : VTK_TETRA
    for (std::size_t e = 0; e < ne; ++e)
        out << type << ((e % 32 == 31) ? "\n" : " ");
    out << "\n        </DataArray>\n      </Cells>\n    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n";
    if (!out)
        throw FeaError("Failed to write '" + path + "'");
}

} // namespace cf::fea
