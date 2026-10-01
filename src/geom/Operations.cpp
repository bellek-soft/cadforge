#include "geom/Operations.h"
#include "geom/OcctShape.h"

#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRep_Builder.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_List.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <memory>

namespace cf::geom {

namespace {

bool hasFaces(const TopoDS_Shape& s)
{
    return TopExp_Explorer(s, TopAbs_FACE).More();
}

const TopoDS_Edge& edgeAt(const Shape& shape, int index)
{
    if (index < 1 || index > shape.edgeCount())
        throw GeomError("Edge index " + std::to_string(index) + " is out of range (shape has " +
                        std::to_string(shape.edgeCount()) + " edges)");
    return TopoDS::Edge(shape.impl()->edges(index));
}

} // namespace

Shape booleanOp(BooleanOp op, const Shape& base, const std::vector<Shape>& tools)
{
    if (base.isNull())
        throw GeomError("Boolean: base shape is missing");
    if (tools.empty())
        throw GeomError("Boolean: at least one tool shape is required");

    return guarded("Boolean", [&] {
        std::unique_ptr<BRepAlgoAPI_BooleanOperation> algo;
        switch (op) {
        case BooleanOp::Union:     algo = std::make_unique<BRepAlgoAPI_Fuse>(); break;
        case BooleanOp::Cut:       algo = std::make_unique<BRepAlgoAPI_Cut>(); break;
        case BooleanOp::Intersect: algo = std::make_unique<BRepAlgoAPI_Common>(); break;
        }
        NCollection_List<TopoDS_Shape> args, tls;
        args.Append(toOcct(base));
        for (const auto& t : tools) {
            if (t.isNull())
                throw GeomError("Boolean: a tool shape is missing");
            tls.Append(toOcct(t));
        }
        algo->SetArguments(args);
        algo->SetTools(tls);
        algo->SetRunParallel(true);
        algo->Build();
        if (!algo->IsDone() || algo->HasErrors())
            throw GeomError("Boolean operation failed");

        TopoDS_Shape result = algo->Shape();
        if (!hasFaces(result))
            throw GeomError("Boolean result is empty (the shapes do not overlap?)");

        ShapeUpgrade_UnifySameDomain unify(result, true, true, false);
        unify.Build();
        return fromOcct(unify.Shape());
    });
}

Shape fillet(const Shape& shape, const std::vector<int>& edgeIndices, double radius)
{
    if (shape.isNull())
        throw GeomError("Fillet: base shape is missing");
    if (edgeIndices.empty())
        throw GeomError("Fillet: no edges selected");
    if (!(radius > 0.0))
        throw GeomError("Fillet: radius must be greater than zero");

    return guarded("Fillet", [&] {
        BRepFilletAPI_MakeFillet mk(toOcct(shape));
        for (int idx : edgeIndices)
            mk.Add(radius, edgeAt(shape, idx));
        mk.Build();
        if (!mk.IsDone())
            throw GeomError("Fillet failed (radius too large for the selected edges?)");
        return fromOcct(mk.Shape());
    });
}

Shape chamfer(const Shape& shape, const std::vector<int>& edgeIndices, double distance)
{
    if (shape.isNull())
        throw GeomError("Chamfer: base shape is missing");
    if (edgeIndices.empty())
        throw GeomError("Chamfer: no edges selected");
    if (!(distance > 0.0))
        throw GeomError("Chamfer: distance must be greater than zero");

    return guarded("Chamfer", [&] {
        BRepFilletAPI_MakeChamfer mk(toOcct(shape));
        for (int idx : edgeIndices)
            mk.Add(distance, edgeAt(shape, idx));
        mk.Build();
        if (!mk.IsDone())
            throw GeomError("Chamfer failed (distance too large for the selected edges?)");
        return fromOcct(mk.Shape());
    });
}

Shape makeCompound(const std::vector<Shape>& shapes)
{
    return guarded("Compound", [&] {
        BRep_Builder builder;
        TopoDS_Compound comp;
        builder.MakeCompound(comp);
        for (const auto& s : shapes)
            if (!s.isNull())
                builder.Add(comp, toOcct(s));
        return fromOcct(comp);
    });
}

} // namespace cf::geom
