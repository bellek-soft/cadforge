#include "geom/Tessellator.h"
#include "geom/OcctShape.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GeomLProp_SLProps.hxx>
#include <Geom_Surface.hxx>
#include <Poly_Triangulation.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>

#include <algorithm>

namespace cf::geom {

namespace {

void pushVec(std::vector<float>& v, const gp_XYZ& p)
{
    v.push_back(static_cast<float>(p.X()));
    v.push_back(static_cast<float>(p.Y()));
    v.push_back(static_cast<float>(p.Z()));
}

/// Per-node normals in the triangulation's local frame. Uses the exact surface
/// normal when UV parameters are available, otherwise averages triangle normals.
std::vector<gp_Dir> nodeNormals(const TopoDS_Face& face, const Handle(Poly_Triangulation)& tri)
{
    const int nbNodes = tri->NbNodes();
    std::vector<gp_XYZ> acc(static_cast<std::size_t>(nbNodes), gp_XYZ(0, 0, 0));

    // Mesh-based fallback (also used where the surface normal is undefined, e.g. apexes).
    for (int t = 1; t <= tri->NbTriangles(); ++t) {
        int n1, n2, n3;
        tri->Triangle(t).Get(n1, n2, n3);
        const gp_XYZ a = tri->Node(n1).XYZ(), b = tri->Node(n2).XYZ(), c = tri->Node(n3).XYZ();
        const gp_XYZ n = (b - a).Crossed(c - a); // area weighted
        acc[n1 - 1] += n;
        acc[n2 - 1] += n;
        acc[n3 - 1] += n;
    }

    TopLoc_Location surfLoc;
    Handle(Geom_Surface) surf = tri->HasUVNodes() ? BRep_Tool::Surface(face, surfLoc) : nullptr;
    // The triangulation's nodes and the surface share the face location, so a
    // surface normal is directly usable in the triangulation's frame.

    std::vector<gp_Dir> out(static_cast<std::size_t>(nbNodes), gp_Dir(0, 0, 1));
    for (int n = 1; n <= nbNodes; ++n) {
        gp_XYZ v = acc[n - 1];
        if (!surf.IsNull()) {
            const gp_Pnt2d uv = tri->UVNode(n);
            GeomLProp_SLProps props(surf, uv.X(), uv.Y(), 1, 1e-9);
            if (props.IsNormalDefined()) {
                gp_XYZ sn = props.Normal().XYZ();
                // Surface normal sign is relative to the surface parametrization;
                // the mesh winding already follows it, so align with the mesh.
                if (v.SquareModulus() > 0 && sn.Dot(v) < 0)
                    sn.Reverse();
                v = sn;
            }
        }
        if (v.SquareModulus() > 1e-30)
            out[static_cast<std::size_t>(n - 1)] = gp_Dir(v);
    }
    return out;
}

void tessellateFaces(const Shape::Impl& impl, MeshData& out)
{
    const int nbFaces = impl.faces.Extent();
    out.faceRanges.resize(static_cast<std::size_t>(nbFaces));

    for (int fi = 1; fi <= nbFaces; ++fi) {
        const TopoDS_Face& face = TopoDS::Face(impl.faces(fi));
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        IndexRange& range = out.faceRanges[static_cast<std::size_t>(fi - 1)];
        range.first = static_cast<std::uint32_t>(out.indices.size());
        if (tri.IsNull() || tri->NbTriangles() == 0)
            continue;

        const std::vector<gp_Dir> normals = nodeNormals(face, tri);
        const gp_Trsf trsf = loc.Transformation();
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        const auto base = static_cast<std::uint32_t>(out.vertexCount());

        for (int n = 1; n <= tri->NbNodes(); ++n) {
            gp_Pnt p = tri->Node(n).Transformed(trsf);
            gp_Dir d = normals[static_cast<std::size_t>(n - 1)];
            d.Transform(trsf);
            if (reversed)
                d.Reverse();
            pushVec(out.positions, p.XYZ());
            pushVec(out.normals, d.XYZ());
            out.faceIds.push_back(static_cast<std::uint32_t>(fi));
            out.bounds.add(Vec3(p.X(), p.Y(), p.Z()));
        }
        for (int t = 1; t <= tri->NbTriangles(); ++t) {
            int n1, n2, n3;
            tri->Triangle(t).Get(n1, n2, n3);
            if (reversed)
                std::swap(n2, n3);
            out.indices.push_back(base + static_cast<std::uint32_t>(n1 - 1));
            out.indices.push_back(base + static_cast<std::uint32_t>(n2 - 1));
            out.indices.push_back(base + static_cast<std::uint32_t>(n3 - 1));
        }
        range.count = static_cast<std::uint32_t>(out.indices.size()) - range.first;
    }
}

void tessellateEdges(const Shape::Impl& impl, double linDefl, double angDefl, MeshData& out)
{
    const int nbEdges = impl.edges.Extent();
    out.edgeRanges.resize(static_cast<std::size_t>(nbEdges));

    for (int ei = 1; ei <= nbEdges; ++ei) {
        const TopoDS_Edge& edge = TopoDS::Edge(impl.edges(ei));
        IndexRange& range = out.edgeRanges[static_cast<std::size_t>(ei - 1)];
        range.first = static_cast<std::uint32_t>(out.edgeVertexCount());
        if (BRep_Tool::Degenerated(edge))
            continue;
        try {
            BRepAdaptor_Curve curve(edge);
            GCPnts_TangentialDeflection disc(curve, angDefl, linDefl, 2);
            const int np = disc.NbPoints();
            for (int i = 1; i < np; ++i) {
                const gp_Pnt a = disc.Value(i);
                const gp_Pnt b = disc.Value(i + 1);
                pushVec(out.edgePositions, a.XYZ());
                pushVec(out.edgePositions, b.XYZ());
                out.edgeIds.push_back(static_cast<std::uint32_t>(ei));
                out.edgeIds.push_back(static_cast<std::uint32_t>(ei));
                out.bounds.add(Vec3(a.X(), a.Y(), a.Z()));
                out.bounds.add(Vec3(b.X(), b.Y(), b.Z()));
            }
        } catch (const Standard_Failure&) {
            // Edge without a usable 3D curve: skip it, it simply won't be drawn.
        }
        range.count = static_cast<std::uint32_t>(out.edgeVertexCount()) - range.first;
    }
}

} // namespace

MeshData tessellate(const Shape& shape, const TessellationParams& params)
{
    MeshData out;
    if (shape.isNull())
        return out;

    return guarded("Tessellation", [&] {
        const auto& impl = *shape.impl();
        double lin = params.absoluteDeflection;
        if (lin <= 0.0) {
            const double diag = shape.bounds().diagonal();
            lin = std::max(diag * params.relativeDeflection, 1e-4);
        }
        const double ang = params.angularDeflectionDeg * 3.14159265358979323846 / 180.0;

        BRepMesh_IncrementalMesh mesher(impl.shape, lin, false, ang, true);
        (void)mesher;

        tessellateFaces(impl, out);
        tessellateEdges(impl, lin, ang, out);
        return out;
    });
}

} // namespace cf::geom
