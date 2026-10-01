#pragma once
// Selection model: whole features, or sub-shapes (faces / edges) of features.

#include "core/Types.h"
#include "render/Renderer.h"

#include <algorithm>
#include <vector>

namespace cf::app {

struct SelItem {
    FeatureId feature = kNoFeature;
    render::PickKind kind = render::PickKind::None; // None = whole feature
    int index = 0;                                  // 1-based face/edge index
    bool operator==(const SelItem&) const = default;
};

class Selection {
public:
    const std::vector<SelItem>& items() const { return m_items; }
    bool empty() const { return m_items.empty(); }
    void clear() { m_items.clear(); }

    void set(const SelItem& it) { m_items = {it}; }
    void add(const SelItem& it)
    {
        if (!contains(it))
            m_items.push_back(it);
    }
    void toggle(const SelItem& it)
    {
        auto pos = std::find(m_items.begin(), m_items.end(), it);
        if (pos == m_items.end())
            m_items.push_back(it);
        else
            m_items.erase(pos);
    }
    bool contains(const SelItem& it) const { return std::find(m_items.begin(), m_items.end(), it) != m_items.end(); }

    bool isFeatureSelected(FeatureId id) const { return contains({id, render::PickKind::None, 0}); }

    /// Unique features involved in the selection, in selection order.
    std::vector<FeatureId> features() const
    {
        std::vector<FeatureId> out;
        for (const auto& it : m_items)
            if (std::find(out.begin(), out.end(), it.feature) == out.end())
                out.push_back(it.feature);
        return out;
    }

    std::vector<int> subShapes(FeatureId id, render::PickKind kind) const
    {
        std::vector<int> out;
        for (const auto& it : m_items)
            if (it.feature == id && it.kind == kind)
                out.push_back(it.index);
        return out;
    }

    template <typename Pred> void removeIf(Pred p)
    {
        m_items.erase(std::remove_if(m_items.begin(), m_items.end(), p), m_items.end());
    }

private:
    std::vector<SelItem> m_items;
};

} // namespace cf::app
