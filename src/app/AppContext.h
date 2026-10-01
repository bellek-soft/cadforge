#pragma once
// Application state shared by all UI panels plus the high-level editing
// operations ("commands"). Panels stay thin: they read state and call these.

#include "app/SceneView.h"
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

/// Active while the user re-picks the edges of a fillet/chamfer.
struct EdgeEditSession {
    FeatureId feature = kNoFeature; // the fillet / chamfer being edited
    FeatureId base = kNoFeature;    // its input, shown instead while editing
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
    render::Camera camera;
    render::Renderer renderer;
    render::RenderSettings settings;
    render::PickFilter pickFilter = render::PickFilter::Object;
    GizmoMode gizmo = GizmoMode::Translate;
    EdgeEditSession edgeEdit;
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
    void setVisible(FeatureId id, bool visible);
    void selectAll();

    void beginEdgeEdit(FeatureId feature);
    void applyEdgeEdit();
    void cancelEdgeEdit();

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
