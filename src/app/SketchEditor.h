#pragma once
// Sketch edit mode: the working copy of a sketch being edited in place in the
// 3D view, its local undo stack, tool state and the editing operations
// (adding geometry with automatic constraints, constraining the selection,
// dragging, dimension edits). The ImGui side lives in ui/SketchUi.cpp.

#include "core/Types.h"
#include "render/Camera.h"
#include "sketch/Sketch.h"

#include <string>
#include <vector>

namespace cf::app {

class AppContext;

enum class SketchTool { Select, Point, Line, Rectangle, Circle, Arc };

/// Where a new point lands: free, on an existing point (-> coincident) or on
/// an edge (-> point on object).
struct SketchSnap {
    Vec2 pos{0.0};
    sketch::Ref ref; // invalid = free point
};

class SketchEditor {
public:
    bool active() const { return m_feature != kNoFeature; }
    FeatureId feature() const { return m_feature; }

    /// Enters edit mode for sketch feature `id` (aligns the camera to the plane).
    bool begin(AppContext& ctx, FeatureId id);
    /// Leaves edit mode keeping the changes (one undo step in the document).
    void finish(AppContext& ctx);
    /// Leaves edit mode discarding the changes.
    void cancel(AppContext& ctx);

    const sketch::Sketch& sketch() const { return m_sketch; }
    const PlaneFrame& frame() const { return m_frame; }
    const sketch::SolveResult& result() const { return m_result; }

    // ---- tool state (used by the UI) ----
    SketchTool tool = SketchTool::Select;
    bool constructionMode = false;     // new geometry is construction geometry
    bool gridSnap = false;
    std::vector<Vec2> toolPoints;      // clicks of the tool in progress
    std::vector<SketchSnap> toolSnaps; // snaps of those clicks
    int chainLine = -1;                // polyline: previous line of the chain
    int chainStartLine = -1;           // polyline: first line of the chain

    std::vector<sketch::Ref> selection;
    std::vector<int> selectedConstraints;
    int editDimension = -1;            // constraint whose value popup should open

    void setTool(SketchTool t);
    void resetTool();
    bool isSelected(const sketch::Ref& r) const;
    void toggleSelect(const sketch::Ref& r, bool additive);
    void toggleSelectConstraint(int index, bool additive);
    void clearSelection();

    // ---- geometry creation (positions in sketch coordinates) ----
    void addPoint(AppContext& ctx, const SketchSnap& p);
    /// Adds a line of the polyline chain; returns true if the chain was closed.
    bool addChainLine(AppContext& ctx, const SketchSnap& a, const SketchSnap& b);
    void addRectangle(AppContext& ctx, const SketchSnap& a, const SketchSnap& b);
    void addCircle(AppContext& ctx, const SketchSnap& center, const SketchSnap& rim);
    void addArc(AppContext& ctx, const SketchSnap& center, const SketchSnap& start, const SketchSnap& end);

    // ---- constraints from the selection ----
    /// Constraint(s) of `type` built from the current selection (empty if it does not fit).
    std::vector<sketch::Constraint> constraintsFromSelection(sketch::ConstraintType type) const;
    bool canConstrain(sketch::ConstraintType type) const { return !constraintsFromSelection(type).empty(); }
    void constrainSelection(AppContext& ctx, sketch::ConstraintType type);

    bool setDimension(AppContext& ctx, int index, double value);
    void deleteSelection(AppContext& ctx);
    void deleteConstraint(AppContext& ctx, int index);
    void toggleConstruction(AppContext& ctx);

    // ---- dragging ----
    void beginDrag(const sketch::Ref& ref, Vec2 grab);
    void dragTo(Vec2 cursor);
    void endDrag(AppContext& ctx);
    bool dragging() const { return m_dragging; }

    // ---- local undo ----
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    void undo(AppContext& ctx);
    void redo(AppContext& ctx);

    /// Message of the last refused operation (shown in the panel).
    const std::string& message() const { return m_message; }

private:
    /// Replaces the working sketch by `s` (solving it) when acceptable; records undo.
    bool commit(AppContext& ctx, sketch::Sketch s, const std::string& what);
    /// Adds `c` to `s` if it keeps the sketch solvable and adds no redundancy.
    static bool tryAdd(sketch::Sketch& s, const sketch::Constraint& c);
    /// Adds the constraint implied by a snap on point `target`.
    static void snapConstraint(sketch::Sketch& s, const SketchSnap& snap, const sketch::Ref& target);
    void pushToDocument(AppContext& ctx);
    void resolve();

    FeatureId m_feature = kNoFeature;
    sketch::Sketch m_sketch;
    sketch::Sketch m_original;
    PlaneFrame m_frame;
    sketch::SolveResult m_result;
    std::vector<sketch::Sketch> m_undo, m_redo;
    std::string m_message;

    bool m_dragging = false;
    sketch::Drag m_drag;
    Vec2 m_grab{0.0};
    sketch::Sketch m_dragStart;

    render::Camera::State m_savedCamera;
    bool m_savedOrtho = false;
    bool m_savedGrid = true;
};

} // namespace cf::app
