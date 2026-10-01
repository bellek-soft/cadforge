#include "geom/ShapeIO.h"
#include "geom/Operations.h"
#include "geom/OcctShape.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <StlAPI_Writer.hxx>

#include <algorithm>

namespace cf::geom {

Shape importStep(const std::string& path)
{
    return guarded("STEP import", [&] {
        STEPControl_Reader reader;
        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone)
            throw GeomError("cannot read '" + path + "'");
        if (reader.TransferRoots() <= 0)
            throw GeomError("no shapes found in '" + path + "'");
        TopoDS_Shape s = reader.OneShape();
        if (s.IsNull())
            throw GeomError("no shapes found in '" + path + "'");
        return fromOcct(s);
    });
}

void exportStep(const std::vector<Shape>& shapes, const std::string& path)
{
    guarded("STEP export", [&] {
        STEPControl_Writer writer;
        int n = 0;
        for (const auto& s : shapes) {
            if (s.isNull())
                continue;
            if (writer.Transfer(toOcct(s), STEPControl_AsIs) != IFSelect_RetDone)
                throw GeomError("failed to translate a shape");
            ++n;
        }
        if (n == 0)
            throw GeomError("nothing to export");
        if (writer.Write(path.c_str()) != IFSelect_RetDone)
            throw GeomError("cannot write '" + path + "'");
        return 0;
    });
}

void exportStl(const std::vector<Shape>& shapes, const std::string& path, double deflection)
{
    guarded("STL export", [&] {
        Shape comp = makeCompound(shapes);
        if (comp.faceCount() == 0)
            throw GeomError("nothing to export");
        if (deflection <= 0.0)
            deflection = std::max(comp.bounds().diagonal() * 0.0005, 1e-4);
        BRepMesh_IncrementalMesh mesher(toOcct(comp), deflection, false, 0.2, true);
        (void)mesher;
        StlAPI_Writer writer;
        writer.ASCIIMode() = false;
        if (!writer.Write(toOcct(comp), path.c_str()))
            throw GeomError("cannot write '" + path + "'");
        return 0;
    });
}

Shape importBrep(const std::string& path)
{
    return guarded("BREP import", [&] {
        TopoDS_Shape s;
        BRep_Builder builder;
        if (!BRepTools::Read(s, path.c_str(), builder))
            throw GeomError("cannot read '" + path + "'");
        return fromOcct(s);
    });
}

void exportBrep(const Shape& shape, const std::string& path)
{
    guarded("BREP export", [&] {
        if (!BRepTools::Write(toOcct(shape), path.c_str()))
            throw GeomError("cannot write '" + path + "'");
        return 0;
    });
}

} // namespace cf::geom
