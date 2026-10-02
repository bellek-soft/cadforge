#pragma once
// Analysis features: a static structural study and its supports / loads.
// They hold data only (no geometry); meshing and solving are run by the
// application on demand, see fea::generateVolumeMesh / fea::solveStatic.

#include "fea/FeaTypes.h"
#include "fea/Mesher.h"
#include "model/Feature.h"

namespace cf::model {

class Document;
class FeatureRegistry;
void registerFeaFeatures(FeatureRegistry& registry);

class StaticAnalysisFeature final : public Feature {
public:
    static constexpr const char* kType = "FEA::StaticAnalysis";
    static constexpr const char* kTarget = "target";
    static constexpr const char* kMaterial = "material";
    static constexpr const char* kYoungs = "youngsModulus";
    static constexpr const char* kPoisson = "poissonRatio";
    static constexpr const char* kDensity = "density";
    static constexpr const char* kYield = "yieldStrength";
    static constexpr const char* kElementSize = "elementSize";
    static constexpr const char* kOrder = "order";
    static constexpr const char* kGravity = "gravity";
    static constexpr const char* kGravityVector = "gravityVector";

    StaticAnalysisFeature();
    std::string_view type() const override { return kType; }
    std::string_view typeLabel() const override { return "Static Analysis"; }
    geom::Shape execute(const ExecContext&) override { return {}; }
    bool producesGeometry() const override { return false; }
    bool consumesInputs() const override { return false; }
    void validate(const ExecContext& ctx) override;
    bool onPropertyEdited(std::string_view key) override;

    FeatureId target() const { return props().get<FeatureId>(kTarget); }
    fea::Material material() const;
    fea::MeshSettings meshSettings() const;
};

/// Common base of supports and loads: they reference an analysis and a set of faces
/// of the analysed solid.
class FeaBoundaryFeature : public Feature {
public:
    static constexpr const char* kAnalysis = "analysis";
    static constexpr const char* kFaces = "faces";

    FeaBoundaryFeature();
    geom::Shape execute(const ExecContext&) override { return {}; }
    bool producesGeometry() const override { return false; }
    bool consumesInputs() const override { return false; }
    bool nestUnderInput() const override { return true; }
    void validate(const ExecContext& ctx) override;
    FeatureId subShapeTarget(const Document& doc) const override;

    FeatureId analysis() const { return props().get<FeatureId>(kAnalysis); }
    const std::vector<int>& faces() const { return props().get<std::vector<int>>(kFaces); }
};

class FixedSupportFeature final : public FeaBoundaryFeature {
public:
    static constexpr const char* kType = "FEA::FixedSupport";
    FixedSupportFeature();
    std::string_view type() const override { return kType; }
    std::string_view typeLabel() const override { return "Fixed"; }
};

class ForceFeature final : public FeaBoundaryFeature {
public:
    static constexpr const char* kType = "FEA::Force";
    static constexpr const char* kForce = "force";
    ForceFeature();
    std::string_view type() const override { return kType; }
    std::string_view typeLabel() const override { return "Force"; }
};

class PressureFeature final : public FeaBoundaryFeature {
public:
    static constexpr const char* kType = "FEA::Pressure";
    static constexpr const char* kPressure = "pressure";
    PressureFeature();
    std::string_view type() const override { return kType; }
    std::string_view typeLabel() const override { return "Pressure"; }
};

/// Collects material, supports and loads of an analysis into a solver setup.
/// Throws geom::GeomError (with a user-facing message) if something is invalid.
fea::StaticSetup buildStaticSetup(const Document& doc, FeatureId analysis);

/// The analysis features whose target is `solid`.
std::vector<FeatureId> analysesOf(const Document& doc, FeatureId solid);

} // namespace cf::model
