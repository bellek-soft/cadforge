#pragma once
// Bridges the document and the renderer: keeps GPU meshes in sync with
// feature results (re-tessellating only when a result changes) and builds
// the per-frame draw list including selection/hover state.

#include "model/Document.h"
#include "render/GpuMesh.h"
#include "render/Renderer.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace cf::app {

class AppContext;

/// Per-face summary of a tessellated solid (used to place load / support glyphs).
struct FaceInfo {
    Vec3 centroid{0.0};
    Vec3 normal{0.0, 0.0, 1.0}; // area-weighted average, normalized
    double area = 0.0;
};

class SceneView {
public:
    /// Rebuilds the draw list for this frame.
    const std::vector<render::DrawItem>& build(AppContext& ctx);

    const std::vector<render::DrawItem>& items() const { return m_items; }
    FeatureId featureForPickId(std::uint32_t pickId) const;

    /// Bounds of everything currently drawn (or of the given features).
    BoundingBox bounds() const;
    BoundingBox bounds(const model::Document& doc, const std::vector<FeatureId>& ids);

    /// GPU mesh for a feature result (tessellates on demand). nullptr if no result.
    const render::GpuMesh* meshFor(const model::Feature& f);

    /// Face summaries of a feature's current tessellation (nullptr if none).
    const std::vector<FaceInfo>* faceInfo(FeatureId id) const;

    void clear();

private:
    struct Entry {
        std::uint64_t key = 0;
        std::unique_ptr<render::GpuMesh> mesh;
        std::vector<FaceInfo> faces;
    };
    // GPU copy of the analysis display surface (mesh / results).
    std::uint64_t m_feaKey = 0;
    FeatureId m_feaAnalysis = kNoFeature;
    std::unique_ptr<render::GpuMesh> m_feaMesh;
    std::unordered_map<FeatureId, Entry> m_meshes;
    std::vector<render::DrawItem> m_items;
    std::vector<FeatureId> m_pickMap; // pickId - 1 -> feature
};

} // namespace cf::app
