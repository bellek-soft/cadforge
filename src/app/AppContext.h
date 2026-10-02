#pragma once
// Application state shared by all UI panels plus the high-level editing
// operations ("commands"). Panels stay thin: they read state and call these.

#include "app/FeaController.h"
#include "app/SceneView.h"
#include "app/SketchEditor.h"
#include "app/Selection.h"
#include "model/Document.h"
#include "model/History.h"
#include "render/Camera.h"
#include "render/Renderer.h"

#include <functional>
#include <string>
#include <vector>

namespace cf::app {

enum class GizmoMode { None, Translate, Rotate };

/// Active while the user re-picks the faces/edges referenced by a feature
/// (fillet/chamfer edges, faces of a support or load).
struct SubShapeEditSession {
    FeatureId feature = kNoFeature;                 // the feature being edited
    FeatureId base = kNoFeature;                    // the solid whose sub-shapes are picked
    render::PickKind kind = render::PickKind::Edge; // Face or Edge
    std::string property;                           // IndexList property key
    bool active() const { return feature != kNoFeature; }
};

struct FileRequest {
    enum class Kind { Open, Save } kind = Kind::Open;
    std::string title;
    std::vector<std::string> filters; // pfd style: {"Label (*.ext)", "*.ext", ...}
    std::string defaultExtension;     // appended on save when missing (".cfp")
    std::function<void(const std::string&)> onAccept;
};

class AppContext {
public:
    AppContext();

    // --- core state ---
    model::Document doc;
    model::History history;
    Selection selection;
    SceneView scene;
    FeaController fea;
    render::Camera camera;
    render::Renderer renderer;
    render::RenderSettings settings;
    render::PickFilter pickFilter = render::PickFilter::Object;
    GizmoMode gizmo = GizmoMode::Translate;
    SubShapeEditSession shapeEdit;
    SketchEditor sketchEdit;      // active while a sketch is edited in place
    render::PickResult hover;     // what is under the cursor
    FeatureId hoverFeature = kNoFeature;
    model::RecomputeStats lastRecompute;
    std::string filePath;         // empty = untitled

    // UI flags
    bool showConsole = true;
    bool showImGuiDemo = false;
    bool showAbout = false;
    bool showControls = false;
    bool quitConfirmed = false;

    // --- status line ---
    void status(std::string msg, bool error = false);
    const std::string& statusText() const { return m_status; }
    bool statusIsError() const { return m_statusError; }
    double statusTime() const { return m_statusTime; }

    // --- document operations ---
    /// Re-evaluates the document and drops selection entries that became invalid.
    void recompute();
    /// recompute() + record an undo step.
    void commit(const std::string& label);
    void undo();
    void redo();

    model::Feature* createFeature(const std::string& type);
    void booleanFromSelection(int op);              // BooleanFeature::Op
    void dressUpFromSelection(const std::string& type); // "Part::Fillet" / "Part::Chamfer"
    void deleteSelection();

    // --- sketches ---
    /// Creates a sketch on base plane `plane` (SketchFeature::Plane) and starts editing it.
    void createSketch(int plane);
    void editSketch(FeatureId id);
    /// Creates "Part::Extrude" / "Part::Revolve" from the selected sketch. If a solid is
    /// selected too, it becomes the target of a Join.
    void profileFeatureFromSelection(const std::string& type);
    void setVisible(FeatureId id, bool visible);
    void selectAll();

    // --- analysis ---
    /// Creates a static analysis for the selected solid.
    void createAnalysisFromSelection();
    /// Creates a support / load ("FEA::FixedSupport", "FEA::Force", "FEA::Pressure")
    /// on the selected faces (creating an analysis if the solid has none).
    void createBoundaryFromSelection(const std::string& type);
    /// Analysis the user is working on: the selected analysis, or the analysis of the
    /// selected support/load, or the one whose results are shown.
    FeatureId contextAnalysis() const;
    void meshAnalysis(FeatureId analysis);
    void solveAnalysis(FeatureId analysis);

    void beginSubShapeEdit(FeatureId feature);
    void applySubShapeEdit();
    void cancelSubShapeEdit();

    // --- files ---
    void newDocument();
    bool openDocument(const std::string& path);
    bool saveDocument(const std::string& path);
    void importStep(const std::string& path);
    void exportStep(const std::string& path);
    void exportStl(const std::string& path);
    /// Shapes to export: selected features, or all visible top-level results.
    std::vector<geom::Shape> exportShapes() const;

    /// Runs `action` immediately, or after asking to save unsaved changes.
    void guardUnsaved(std::function<void()> action);
    void requestFile(FileRequest req);

    // UI-facing requests (consumed by the UI layer each frame).
    std::function<void()> pendingAfterConfirm;
    bool confirmUnsavedOpen = false;
    std::vector<FileRequest> fileRequests;
    bool requestFitAll = false;

    void fitAll(bool animate = true);
    void fitSelection();

    std::string windowTitle() const;
    bool isModified() const { return history.isModified(); }

private:
    void pruneSelection();

    std::string m_status;
    bool m_statusError = false;
    double m_statusTime = 0.0;
};

} // namespace cf::app
