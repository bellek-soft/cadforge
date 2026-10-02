#pragma once
// Snapshot-based undo/redo.
//
// Each committed state is the document's JSON (parameters only, no geometry),
// so snapshots are tiny and always consistent. Restoring a snapshot is fast
// because Document reuses cached results for unchanged features.
// (A command-based history can replace this later without touching callers.)

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace cf::model {

class Document;

class History {
public:
    /// Forget everything and use the current document state as the base.
    void reset(const Document& doc);
    /// Records the current document state as a new undo step.
    /// Does nothing if the state is identical to the current one.
    void commit(const Document& doc, std::string label);

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index + 1 < m_states.size(); }
    std::string undoLabel() const;
    std::string redoLabel() const;

    /// Restores the previous/next state into `doc` (call recompute() after).
    bool undo(Document& doc);
    bool redo(Document& doc);

    /// True when the current state differs from the last saved state.
    bool isModified() const { return m_index != m_savedIndex; }
    void markSaved() { m_savedIndex = m_index; }
    /// Forces "modified" (e.g. after restoring an autosave).
    void markModified() { m_savedIndex = static_cast<std::size_t>(-1); }

private:
    struct State {
        nlohmann::ordered_json data;
        std::string label;
    };
    std::vector<State> m_states;
    std::size_t m_index = 0;
    std::size_t m_savedIndex = 0;
    static constexpr std::size_t kMaxStates = 200;
};

} // namespace cf::model
