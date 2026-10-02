#include "model/features/FeaFeatures.h"

#include "model/Document.h"
#include "model/FeatureRegistry.h"

#include <algorithm>

namespace cf::model {

namespace {

template <typename T>
FeatureTypeInfo info(const char* type, const char* label)
{
    return {type, label, "Analysis", [] { return std::make_unique<T>(); }};
}

void hidePlacement(Feature& f)
{
    f.props().find(Feature::kPosition)->hidden = true;
    f.props().find(Feature::kRotation)->hidden = true;
}

std::vector<std::string> materialNames()
{
    std::vector<std::string> names;
    for (const auto& m : fea::materialLibrary())
        names.push_back(m.name);
    names.push_back("Custom");
    return names;
}

const StaticAnalysisFeature* asAnalysis(const Feature* f)
{
    return f && f->type() == StaticAnalysisFeature::kType ? static_cast<const StaticAnalysisFeature*>(f) : nullptr;
}

} // namespace

void registerFeaFeatures(FeatureRegistry& r)
{
    r.add(info<StaticAnalysisFeature>(StaticAnalysisFeature::kType, "Analysis"));
    r.add(info<FixedSupportFeature>(FixedSupportFeature::kType, "Fixed Support"));
    r.add(info<ForceFeature>(ForceFeature::kType, "Force"));
    r.add(info<PressureFeature>(PressureFeature::kType, "Pressure"));
    r.add(info<MeshRefinementFeature>(MeshRefinementFeature::kType, "Mesh Refinement"));
}

// ---- StaticAnalysis -----------------------------------------------------------------

StaticAnalysisFeature::StaticAnalysisFeature()
{
    hidePlacement(*this);
    setColor({0.85f, 0.55f, 0.35f, 1.0f});
    Property& t = props().add(makeRef(kTarget, "Solid", "Model"));
    t.tooltip = "The solid to analyse";
    Property& at = props().add(makeEnum(kAnalysisType, "Type", Static, {"Static structural", "Modal (frequencies)"}, "Model"));
    at.tooltip = "Static: stresses and deformation under loads. Modal: natural frequencies and mode shapes "
                 "(uses supports, ignores loads)";
    Property& nm = props().add(makeInt(kModes, "Modes", 6, 1, 50, "Model"));
    nm.tooltip = "Number of natural frequencies to compute (modal analysis)";

    const fea::Material steel = fea::materialLibrary().front();
    props().add(makeEnum(kMaterial, "Preset", 0, materialNames(), "Material"));
    Property& e = props().add(makeDouble(kYoungs, "Young's modulus", steel.youngsModulus, 1.0, 1e7, "MPa", "Material"));
    e.speed = 100.0;
    e.format = "%.0f";
    Property& nu = props().add(makeDouble(kPoisson, "Poisson's ratio", steel.poissonRatio, -0.99, 0.499, "", "Material"));
    nu.speed = 0.005;
    Property& rho = props().add(makeDouble(kDensity, "Density", steel.density, 0.0, 1e-4, "t/mm3", "Material"));
    rho.speed = 1e-11;
    rho.format = "%.3e";
    Property& sy = props().add(makeDouble(kYield, "Yield strength", steel.yieldStrength, 0.0, 1e5, "MPa", "Material"));
    sy.speed = 1.0;
    sy.format = "%.1f";
    sy.tooltip = "Used for the safety factor";

    Property& h = props().add(makeDouble(kElementSize, "Element size", 0.0, 0.0, 1e6, "mm", "Mesh"));
    h.tooltip = "0 = automatic (model diagonal / 20)";
    props().add(makeEnum(kOrder, "Elements", 1, {"Linear (Tet4)", "Quadratic (Tet10)"}, "Mesh"));

    props().add(makeBool(kGravity, "Gravity", false, "Loads"));
    Property& g = props().add(makeVec3(kGravityVector, "Acceleration", Vec3(0, 0, -9810.0), "mm/s2", "Loads"));
    g.speed = 10.0;
}

void StaticAnalysisFeature::validate(const ExecContext& ctx)
{
    const Feature* t = ctx.document().find(target());
    if (!t)
        throw geom::GeomError("Choose the solid to analyse");
    if (!t->producesGeometry())
        throw geom::GeomError("'" + t->name() + "' is not a solid");
    if (t->state() != FeatureState::Ok || t->shape().faceCount() == 0)
        throw geom::GeomError("'" + t->name() + "' has no valid geometry");
}

bool StaticAnalysisFeature::onPropertyEdited(std::string_view key)
{
    const auto& lib = fea::materialLibrary();
    if (key == kMaterial) {
        const int i = props().get<int>(kMaterial);
        if (i < 0 || i >= int(lib.size()))
            return false; // "Custom": keep values
        const fea::Material& m = lib[std::size_t(i)];
        props().set(kYoungs, m.youngsModulus);
        props().set(kPoisson, m.poissonRatio);
        props().set(kDensity, m.density);
        props().set(kYield, m.yieldStrength);
        return true;
    }
    if (key == kYoungs || key == kPoisson || key == kDensity || key == kYield)
        return props().set(kMaterial, int(lib.size())); // switch to "Custom"
    return false;
}

fea::Material StaticAnalysisFeature::material() const
{
    fea::Material m;
    const int i = props().get<int>(kMaterial);
    const auto& lib = fea::materialLibrary();
    m.name = i >= 0 && i < int(lib.size()) ? lib[std::size_t(i)].name : "Custom";
    m.youngsModulus = props().get<double>(kYoungs);
    m.poissonRatio = props().get<double>(kPoisson);
    m.density = props().get<double>(kDensity);
    m.yieldStrength = props().get<double>(kYield);
    return m;
}

fea::MeshSettings StaticAnalysisFeature::meshSettings() const
{
    fea::MeshSettings s;
    s.maxSize = props().get<double>(kElementSize);
    s.order = props().get<int>(kOrder) == 0 ? fea::ElementOrder::Linear : fea::ElementOrder::Quadratic;
    return s;
}

// ---- supports & loads ------------------------------------------------------------------

FeaBoundaryFeature::FeaBoundaryFeature()
{
    hidePlacement(*this);
    props().add(makeRef(kAnalysis, "Analysis", "Inputs"));
    Property& f = props().add(makeIndexList(kFaces, "Faces", "face", "Faces"));
    f.tooltip = "1-based face indices of the analysed solid";
}

FeatureId FeaBoundaryFeature::subShapeTarget(const Document& doc) const
{
    const auto* a = asAnalysis(doc.find(analysis()));
    return a ? a->target() : kNoFeature;
}

void FeaBoundaryFeature::validate(const ExecContext& ctx)
{
    const auto* a = asAnalysis(ctx.document().find(analysis()));
    if (!a)
        throw geom::GeomError("Not attached to a static analysis");
    const Feature* solid = ctx.document().find(a->target());
    if (faces().empty())
        throw geom::GeomError("No faces selected");
    if (solid && solid->state() == FeatureState::Ok) {
        const int n = solid->shape().faceCount();
        for (int f : faces())
            if (f < 1 || f > n)
                throw geom::GeomError("Face " + std::to_string(f) + " no longer exists on '" + solid->name() +
                                      "' (re-pick the faces)");
    }
}

FixedSupportFeature::FixedSupportFeature()
{
    setColor({0.35f, 0.65f, 0.95f, 1.0f});
    props().add(makeBool("fixX", "Fix X", true, "Support"));
    props().add(makeBool("fixY", "Fix Y", true, "Support"));
    props().add(makeBool("fixZ", "Fix Z", true, "Support"));
}

ForceFeature::ForceFeature()
{
    setColor({0.95f, 0.35f, 0.30f, 1.0f});
    Property& f = props().add(makeVec3(kForce, "Force", Vec3(0, 0, -100.0), "N", "Load"));
    f.speed = 1.0;
    f.tooltip = "Total force, distributed over the faces by area";
}

MeshRefinementFeature::MeshRefinementFeature()
{
    setColor({0.55f, 0.85f, 0.45f, 1.0f});
    Property& h = props().add(makeDouble(kSize, "Element size", 1.0, 0.01, 1e6, "mm", "Refinement"));
    h.speed = 0.05;
    h.tooltip = "Element size on the selected faces (graded smoothly into the global size)";
}

PressureFeature::PressureFeature()
{
    setColor({0.85f, 0.40f, 0.85f, 1.0f});
    Property& p = props().add(makeDouble(kPressure, "Pressure", 1.0, -1e6, 1e6, "MPa", "Load"));
    p.speed = 0.01;
    p.tooltip = "Positive values push on the surface";
}

// ---- setup -----------------------------------------------------------------------------

std::vector<FeatureId> analysesOf(const Document& doc, FeatureId solid)
{
    std::vector<FeatureId> out;
    for (const auto& f : doc.features())
        if (const auto* a = asAnalysis(f.get()); a && a->target() == solid)
            out.push_back(a->id());
    return out;
}

fea::MeshSettings meshSettingsOf(const Document& doc, FeatureId analysisId)
{
    const auto* a = asAnalysis(doc.find(analysisId));
    if (!a)
        return {};
    fea::MeshSettings s = a->meshSettings();
    for (FeatureId id : doc.nestedChildren(analysisId)) {
        const Feature* f = doc.find(id);
        if (f && f->type() == MeshRefinementFeature::kType && f->state() == FeatureState::Ok)
            s.localSizes.push_back({static_cast<const FeaBoundaryFeature*>(f)->faces(),
                                    f->props().get<double>(MeshRefinementFeature::kSize)});
    }
    return s;
}

fea::ModalSetup buildModalSetup(const Document& doc, FeatureId analysisId)
{
    const auto* a = asAnalysis(doc.find(analysisId));
    if (!a)
        throw geom::GeomError("Not an analysis");
    if (a->state() != FeatureState::Ok)
        throw geom::GeomError(a->error());
    fea::ModalSetup s;
    s.material = a->material();
    s.modes = a->props().get<int>(StaticAnalysisFeature::kModes);
    for (FeatureId id : doc.nestedChildren(analysisId)) {
        const Feature* f = doc.find(id);
        if (!f || f->type() != FixedSupportFeature::kType)
            continue;
        if (f->state() != FeatureState::Ok)
            throw geom::GeomError(f->name() + ": " + f->error());
        s.supports.push_back({static_cast<const FeaBoundaryFeature*>(f)->faces(), f->props().get<bool>("fixX"),
                              f->props().get<bool>("fixY"), f->props().get<bool>("fixZ")});
    }
    if (s.supports.empty())
        throw geom::GeomError("Add a fixed support (select faces, then 'Fixed')");
    if (!(s.material.density > 0.0))
        throw geom::GeomError("A modal analysis needs a positive density");
    return s;
}

fea::StaticSetup buildStaticSetup(const Document& doc, FeatureId analysisId)
{
    const auto* a = asAnalysis(doc.find(analysisId));
    if (!a)
        throw geom::GeomError("Not a static analysis");
    if (a->state() != FeatureState::Ok)
        throw geom::GeomError(a->error());

    fea::StaticSetup s;
    s.material = a->material();
    s.gravity = a->props().get<bool>(StaticAnalysisFeature::kGravity);
    s.gravityAcceleration = a->props().get<Vec3>(StaticAnalysisFeature::kGravityVector);

    for (FeatureId id : doc.nestedChildren(analysisId)) {
        const Feature* f = doc.find(id);
        if (!f || f->type() == MeshRefinementFeature::kType)
            continue;
        if (f->state() != FeatureState::Ok)
            throw geom::GeomError(f->name() + ": " + f->error());
        const auto& bc = static_cast<const FeaBoundaryFeature&>(*f);
        if (f->type() == FixedSupportFeature::kType) {
            s.supports.push_back({bc.faces(), f->props().get<bool>("fixX"), f->props().get<bool>("fixY"),
                                  f->props().get<bool>("fixZ")});
        } else if (f->type() == ForceFeature::kType) {
            s.forces.push_back({bc.faces(), f->props().get<Vec3>(ForceFeature::kForce)});
        } else if (f->type() == PressureFeature::kType) {
            s.pressures.push_back({bc.faces(), f->props().get<double>(PressureFeature::kPressure)});
        }
    }
    if (s.supports.empty())
        throw geom::GeomError("Add a fixed support (select faces, then 'Fixed')");
    if (s.forces.empty() && s.pressures.empty() && !s.gravity)
        throw geom::GeomError("Add a load (force, pressure or gravity)");
    return s;
}

} // namespace cf::model
