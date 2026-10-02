#pragma once
// Runs meshing / solving of analysis features in a background thread and keeps
// the (in-memory) results. Results are tied to content keys of the document,
// so they are known to be outdated as soon as the model, material, mesh
// settings or loads change.

#include "core/MeshData.h"
#include "fea/FeaTypes.h"
#include "fea/Post.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace cf::model {
class Document;
}

namespace cf::app {

class AppContext;

struct AnalysisRuntime {
    std::shared_ptr<const fea::VolumeMesh> mesh;
    std::uint64_t meshKey = 0;
    double meshSeconds = 0.0;

    std::shared_ptr<const fea::StaticResult> result;
    std::uint64_t solveKey = 0;

    std::string message; // last error, if any
};

class FeaController {
public:
    FeaController() = default;
    ~FeaController();
    FeaController(const FeaController&) = delete;
    FeaController& operator=(const FeaController&) = delete;

    /// Start jobs. Return false (and set `error`) if the job cannot start.
    bool startMesh(AppContext& ctx, FeatureId analysis, std::string& error);
    bool startSolve(AppContext& ctx, FeatureId analysis, std::string& error);
    void cancel();

    /// Collects finished jobs. Call once per frame from the UI thread.
    void poll(AppContext& ctx);

    bool busy() const { return m_job != nullptr; }
    FeatureId busyAnalysis() const;
    /// Current stage text and progress [0, 1] of the running job.
    std::string progressText() const;
    double progress() const;

    const AnalysisRuntime* runtime(FeatureId analysis) const;
    std::uint64_t currentMeshKey(const model::Document& doc, FeatureId analysis) const;
    std::uint64_t currentSolveKey(const model::Document& doc, FeatureId analysis) const;
    bool meshUpToDate(const model::Document& doc, FeatureId analysis) const;
    bool resultUpToDate(const model::Document& doc, FeatureId analysis) const;

    /// Forget results of analyses that no longer exist (or everything).
    void prune(const model::Document& doc);
    void reset();

    // ---- display settings ----
    FeatureId shownAnalysis = kNoFeature; // whose mesh/results replace the solid in the view
    bool showResults = true;
    fea::ResultField field = fea::ResultField::VonMises;
    bool autoDeformation = true;
    double deformationScale = 1.0; // used when !autoDeformation
    bool showMeshEdges = true;

    /// Deformation factor actually used for display.
    double effectiveDeformationScale(FeatureId analysis) const;

    /// Display surface for an analysis (cached; rebuilt when inputs change).
    /// `range` receives the scalar range of the shown field.
    const MeshData* displaySurface(FeatureId analysis, float range[2]);
    std::uint64_t displayKey(FeatureId analysis) const;

private:
    struct Job;
    bool start(AppContext& ctx, FeatureId analysis, bool solve, std::string& error);
    void finish();

    std::unordered_map<FeatureId, AnalysisRuntime> m_runtimes;
    std::shared_ptr<Job> m_job;
    std::thread m_thread;

    struct DisplayCache {
        std::uint64_t key = 0;
        MeshData surface;
        float range[2] = {0, 1};
    };
    std::unordered_map<FeatureId, DisplayCache> m_display;
};

} // namespace cf::app
