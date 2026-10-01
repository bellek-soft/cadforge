#include "model/History.h"
#include "model/Document.h"

namespace cf::model {

void History::reset(const Document& doc)
{
    m_states.clear();
    m_states.push_back({doc.toJson(), "Initial"});
    m_index = 0;
    m_savedIndex = 0;
}

void History::commit(const Document& doc, std::string label)
{
    if (m_states.empty()) {
        reset(doc);
        return;
    }
    nlohmann::ordered_json state = doc.toJson();
    if (state == m_states[m_index].data)
        return;

    m_states.resize(m_index + 1); // drop redo branch
    if (m_savedIndex > m_index)
        m_savedIndex = static_cast<std::size_t>(-1); // saved state no longer reachable
    m_states.push_back({std::move(state), std::move(label)});
    m_index = m_states.size() - 1;

    if (m_states.size() > kMaxStates) {
        m_states.erase(m_states.begin());
        --m_index;
        if (m_savedIndex != static_cast<std::size_t>(-1))
            m_savedIndex = m_savedIndex == 0 ? static_cast<std::size_t>(-1) : m_savedIndex - 1;
    }
}

std::string History::undoLabel() const
{
    return canUndo() ? m_states[m_index].label : std::string();
}

std::string History::redoLabel() const
{
    return canRedo() ? m_states[m_index + 1].label : std::string();
}

bool History::undo(Document& doc)
{
    if (!canUndo())
        return false;
    --m_index;
    doc.loadJson(m_states[m_index].data);
    return true;
}

bool History::redo(Document& doc)
{
    if (!canRedo())
        return false;
    ++m_index;
    doc.loadJson(m_states[m_index].data);
    return true;
}

} // namespace cf::model
