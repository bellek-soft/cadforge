#pragma once
// Built-in solid modeling features (primitives, CSG booleans, dress-up, import).

#include "model/Feature.h"

namespace cf::model {

class FeatureRegistry;
void registerPartFeatures(FeatureRegistry& registry);

// ---- Primitives -------------------------------------------------------------

class BoxFeature final : public Feature {
public:
    BoxFeature();
    std::string_view type() const override { return "Part::Box"; }
    std::string_view typeLabel() const override { return "Box"; }
    geom::Shape execute(const ExecContext&) override;
};

class CylinderFeature final : public Feature {
public:
    CylinderFeature();
    std::string_view type() const override { return "Part::Cylinder"; }
    std::string_view typeLabel() const override { return "Cylinder"; }
    geom::Shape execute(const ExecContext&) override;
};

class SphereFeature final : public Feature {
public:
    SphereFeature();
    std::string_view type() const override { return "Part::Sphere"; }
    std::string_view typeLabel() const override { return "Sphere"; }
    geom::Shape execute(const ExecContext&) override;
};

class ConeFeature final : public Feature {
public:
    ConeFeature();
    std::string_view type() const override { return "Part::Cone"; }
    std::string_view typeLabel() const override { return "Cone"; }
    geom::Shape execute(const ExecContext&) override;
};

class TorusFeature final : public Feature {
public:
    TorusFeature();
    std::string_view type() const override { return "Part::Torus"; }
    std::string_view typeLabel() const override { return "Torus"; }
    geom::Shape execute(const ExecContext&) override;
};

// ---- CSG ----------------------------------------------------------------------

class BooleanFeature final : public Feature {
public:
    enum Op { Union = 0, Cut = 1, Intersect = 2 };
    static constexpr const char* kOperation = "operation";
    static constexpr const char* kBase = "base";
    static constexpr const char* kTools = "tools";

    BooleanFeature();
    std::string_view type() const override { return "Part::Boolean"; }
    std::string_view typeLabel() const override { return "Boolean"; }
    geom::Shape execute(const ExecContext&) override;
};

// ---- Dress-up -------------------------------------------------------------------

/// Common base for edge-based dress-up features (fillet, chamfer).
class EdgeFeature : public Feature {
public:
    static constexpr const char* kBase = "base";
    static constexpr const char* kEdges = "edges";
    EdgeFeature();
};

class FilletFeature final : public EdgeFeature {
public:
    FilletFeature();
    std::string_view type() const override { return "Part::Fillet"; }
    std::string_view typeLabel() const override { return "Fillet"; }
    geom::Shape execute(const ExecContext&) override;
};

class ChamferFeature final : public EdgeFeature {
public:
    ChamferFeature();
    std::string_view type() const override { return "Part::Chamfer"; }
    std::string_view typeLabel() const override { return "Chamfer"; }
    geom::Shape execute(const ExecContext&) override;
};

// ---- Import -----------------------------------------------------------------------

class ImportStepFeature final : public Feature {
public:
    static constexpr const char* kPath = "path";
    ImportStepFeature();
    std::string_view type() const override { return "Part::ImportStep"; }
    std::string_view typeLabel() const override { return "STEP"; }
    geom::Shape execute(const ExecContext&) override;
    std::string cacheSalt() const override;
};

} // namespace cf::model
