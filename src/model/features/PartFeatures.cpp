#include "model/features/PartFeatures.h"
#include "model/FeatureRegistry.h"

#include "geom/Operations.h"
#include "geom/Primitives.h"
#include "geom/ShapeIO.h"
#include "core/Paths.h"

#include <filesystem>
#include <system_error>

namespace cf::model {

namespace {
constexpr double kMaxLen = 1e6;
constexpr double kMinLen = 1e-3;

template <typename T>
FeatureTypeInfo info(const char* type, const char* label, const char* category)
{
    return {type, label, category, [] { return std::make_unique<T>(); }};
}
} // namespace

void registerPartFeatures(FeatureRegistry& r)
{
    r.add(info<BoxFeature>("Part::Box", "Box", "Primitives"));
    r.add(info<CylinderFeature>("Part::Cylinder", "Cylinder", "Primitives"));
    r.add(info<SphereFeature>("Part::Sphere", "Sphere", "Primitives"));
    r.add(info<ConeFeature>("Part::Cone", "Cone", "Primitives"));
    r.add(info<TorusFeature>("Part::Torus", "Torus", "Primitives"));
    r.add(info<BooleanFeature>("Part::Boolean", "Boolean", "Boolean"));
    r.add(info<FilletFeature>("Part::Fillet", "Fillet", "Dress-up"));
    r.add(info<ChamferFeature>("Part::Chamfer", "Chamfer", "Dress-up"));
    r.add(info<ImportStepFeature>("Part::ImportStep", "STEP", "Import"));
}

// ---- Box --------------------------------------------------------------------
BoxFeature::BoxFeature()
{
    props().add(makeDouble("length", "Length (X)", 20.0, kMinLen, kMaxLen, "mm", "Dimensions"));
    props().add(makeDouble("width", "Width (Y)", 20.0, kMinLen, kMaxLen, "mm", "Dimensions"));
    props().add(makeDouble("height", "Height (Z)", 20.0, kMinLen, kMaxLen, "mm", "Dimensions"));
    props().add(makeBool("centered", "Centered", false, "Dimensions"));
}

geom::Shape BoxFeature::execute(const ExecContext&)
{
    return geom::makeBox(props().get<double>("length"), props().get<double>("width"),
                         props().get<double>("height"), props().get<bool>("centered"));
}

// ---- Cylinder -----------------------------------------------------------------
CylinderFeature::CylinderFeature()
{
    props().add(makeDouble("radius", "Radius", 10.0, kMinLen, kMaxLen, "mm", "Dimensions"));
    props().add(makeDouble("height", "Height", 20.0, kMinLen, kMaxLen, "mm", "Dimensions"));
    Property& a = props().add(makeDouble("angle", "Angle", 360.0, 1.0, 360.0, "deg", "Dimensions"));
    a.speed = 1.0;
}

geom::Shape CylinderFeature::execute(const ExecContext&)
{
    return geom::makeCylinder(props().get<double>("radius"), props().get<double>("height"),
                              props().get<double>("angle"));
}

// ---- Sphere -------------------------------------------------------------------
SphereFeature::SphereFeature()
{
    props().add(makeDouble("radius", "Radius", 10.0, kMinLen, kMaxLen, "mm", "Dimensions"));
}

geom::Shape SphereFeature::execute(const ExecContext&)
{
    return geom::makeSphere(props().get<double>("radius"));
}

// ---- Cone ---------------------------------------------------------------------
ConeFeature::ConeFeature()
{
    props().add(makeDouble("radius1", "Bottom radius", 10.0, 0.0, kMaxLen, "mm", "Dimensions"));
    props().add(makeDouble("radius2", "Top radius", 0.0, 0.0, kMaxLen, "mm", "Dimensions"));
    props().add(makeDouble("height", "Height", 20.0, kMinLen, kMaxLen, "mm", "Dimensions"));
}

geom::Shape ConeFeature::execute(const ExecContext&)
{
    return geom::makeCone(props().get<double>("radius1"), props().get<double>("radius2"),
                          props().get<double>("height"));
}

// ---- Torus --------------------------------------------------------------------
TorusFeature::TorusFeature()
{
    props().add(makeDouble("radius1", "Major radius", 15.0, kMinLen, kMaxLen, "mm", "Dimensions"));
    props().add(makeDouble("radius2", "Minor radius", 4.0, kMinLen, kMaxLen, "mm", "Dimensions"));
}

geom::Shape TorusFeature::execute(const ExecContext&)
{
    return geom::makeTorus(props().get<double>("radius1"), props().get<double>("radius2"));
}

// ---- Boolean ------------------------------------------------------------------
BooleanFeature::BooleanFeature()
{
    props().add(makeEnum(kOperation, "Operation", Union, {"Union", "Cut", "Intersect"}, "Boolean"));
    props().add(makeRef(kBase, "Base", "Inputs"));
    props().add(makeRefList(kTools, "Tools", "Inputs"));
}

geom::Shape BooleanFeature::execute(const ExecContext& ctx)
{
    const auto base = ctx.input(props().get<FeatureId>(kBase));
    std::vector<geom::Shape> tools;
    for (FeatureId id : props().get<std::vector<FeatureId>>(kTools))
        tools.push_back(ctx.input(id));
    return geom::booleanOp(static_cast<geom::BooleanOp>(props().get<int>(kOperation)), base, tools);
}

// ---- Fillet / Chamfer -----------------------------------------------------------
EdgeFeature::EdgeFeature()
{
    props().add(makeRef(kBase, "Base", "Inputs"));
    Property& e = props().add(makeIndexList(kEdges, "Edges", "Edges"));
    e.tooltip = "1-based edge indices of the base shape";
}

FilletFeature::FilletFeature()
{
    props().add(makeDouble("radius", "Radius", 2.0, kMinLen, kMaxLen, "mm", "Fillet"));
}

geom::Shape FilletFeature::execute(const ExecContext& ctx)
{
    return geom::fillet(ctx.input(props().get<FeatureId>(kBase)), props().get<std::vector<int>>(kEdges),
                        props().get<double>("radius"));
}

ChamferFeature::ChamferFeature()
{
    props().add(makeDouble("distance", "Distance", 1.0, kMinLen, kMaxLen, "mm", "Chamfer"));
}

geom::Shape ChamferFeature::execute(const ExecContext& ctx)
{
    return geom::chamfer(ctx.input(props().get<FeatureId>(kBase)), props().get<std::vector<int>>(kEdges),
                         props().get<double>("distance"));
}

// ---- Import STEP ----------------------------------------------------------------
ImportStepFeature::ImportStepFeature()
{
    props().add(makeFilePath(kPath, "File", "", "Source"));
}

geom::Shape ImportStepFeature::execute(const ExecContext&)
{
    const auto& path = props().get<std::string>(kPath);
    if (path.empty())
        throw geom::GeomError("No file selected");
    if (!std::filesystem::exists(paths::fromUtf8(path)))
        throw geom::GeomError("File not found: " + path);
    return geom::importStep(path);
}

std::string ImportStepFeature::cacheSalt() const
{
    std::error_code ec;
    auto t = std::filesystem::last_write_time(paths::fromUtf8(props().get<std::string>(kPath)), ec);
    return ec ? std::string() : std::to_string(static_cast<long long>(t.time_since_epoch().count()));
}

} // namespace cf::model
