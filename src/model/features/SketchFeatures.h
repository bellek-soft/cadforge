#pragma once
// Sketch-based modeling: a constrained 2D sketch on a base plane, and the
// Extrude / Revolve features that turn its closed loops into solids, optionally
// combined with an existing body (join / cut / intersect).

#include "model/Feature.h"
#include "sketch/Sketch.h"

namespace cf::model {

class FeatureRegistry;
void registerSketchFeatures(FeatureRegistry& registry);

class SketchFeature final : public Feature {
public:
    enum Plane { XY = 0, XZ = 1, YZ = 2 };
    static constexpr const char* kPlane = "plane";
    static constexpr const char* kOffset = "offset";

    SketchFeature();
    std::string_view type() const override { return "Sketch::Sketch"; }
    std::string_view typeLabel() const override { return "Sketch"; }
    geom::Shape execute(const ExecContext& ctx) override;
    std::string cacheSalt() const override;
    void saveData(nlohmann::ordered_json& out) const override;
    void loadData(const nlohmann::ordered_json& in) override;

    const sketch::Sketch& sketch() const { return m_sketch; }
    void setSketch(sketch::Sketch s) { m_sketch = std::move(s); }

    /// The sketch coordinate system in world space.
    PlaneFrame frame() const;
    static PlaneFrame planeFrame(Plane plane, double offset);

    /// Result of the solve done by the last execute().
    const sketch::SolveResult& lastSolve() const { return m_lastSolve; }

private:
    sketch::Sketch m_sketch;
    sketch::SolveResult m_lastSolve;
};

/// Common base of Extrude and Revolve: profile sketch + boolean with a target body.
class ProfileFeature : public Feature {
public:
    enum Operation { NewBody = 0, Join = 1, Cut = 2, Intersect = 3 };
    static constexpr const char* kSketch = "sketch";
    static constexpr const char* kOperation = "operation";
    static constexpr const char* kTarget = "target";

    ProfileFeature();

protected:
    /// The profile sketch feature (throws if missing / not a sketch).
    const SketchFeature& profileSketch(const ExecContext& ctx) const;
    /// Planar faces of the sketch's closed loops.
    geom::Shape profileFaces(const ExecContext& ctx) const;
    /// Applies the operation with the target body to the swept `tool`.
    geom::Shape combine(const ExecContext& ctx, const geom::Shape& tool) const;
};

class ExtrudeFeature final : public ProfileFeature {
public:
    ExtrudeFeature();
    std::string_view type() const override { return "Part::Extrude"; }
    std::string_view typeLabel() const override { return "Extrude"; }
    geom::Shape execute(const ExecContext& ctx) override;
};

class RevolveFeature final : public ProfileFeature {
public:
    enum Axis { SketchH = 0, SketchV = 1, ConstructionLine = 2 };
    RevolveFeature();
    std::string_view type() const override { return "Part::Revolve"; }
    std::string_view typeLabel() const override { return "Revolve"; }
    geom::Shape execute(const ExecContext& ctx) override;
};

} // namespace cf::model
