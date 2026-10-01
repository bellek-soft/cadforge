#include "app/SceneView.h"
#include "app/AppContext.h"

#include "core/Log.h"
#include "geom/Tessellator.h"

#include <algorithm>

namespace cf::app {

using model::Feature;
using model::FeatureState;
using render::DrawItem;
using render::PickKind;

void SceneView::clear()
{
    m_meshes.clear();
    m_items.clear();
    m_pickMap.clear();
}

const render::GpuMesh* SceneView::meshFor(const Feature& f)
{
    if (f.state() != FeatureState::Ok || f.shape().isNull())
        return nullptr;
    Entry& e = m_meshes[f.id()];
    if (e.mesh && e.key == f.resultKey())
        return e.mesh.get();
    try {
        const MeshData data = geom::tessellate(f.shape());
        if (!e.mesh)
            e.mesh = std::make_unique<render::GpuMesh>();
        e.mesh->upload(data);
        e.key = f.resultKey();
        return e.mesh.get();
    } catch (const std::exception& ex) {
        log::error(f.name(), ": ", ex.what());
        m_meshes.erase(f.id());
        return nullptr;
    }
}

const std::vector<DrawItem>& SceneView::build(AppContext& ctx)
{
    const model::Document& doc = ctx.doc;
    m_items.clear();
    m_pickMap.clear();

    // Drop meshes of deleted features.
    for (auto it = m_meshes.begin(); it != m_meshes.end();)
        it = doc.find(it->first) ? std::next(it) : m_meshes.erase(it);

    auto addSolid = [&](const Feature& f) {
        const render::GpuMesh* mesh = meshFor(f);
        if (!mesh)
            return;
        DrawItem item;
        item.mesh = mesh;
        m_pickMap.push_back(f.id());
        item.pickId = static_cast<std::uint32_t>(m_pickMap.size());
        const Color& c = f.color();
        item.color = {c.r, c.g, c.b, c.a};
        item.selected = ctx.selection.isFeatureSelected(f.id());
        item.selectedFaces = ctx.selection.subShapes(f.id(), PickKind::Face);
        item.selectedEdges = ctx.selection.subShapes(f.id(), PickKind::Edge);
        if (ctx.hoverFeature == f.id()) {
            switch (ctx.pickFilter) {
            case render::PickFilter::Object: item.hovered = true; break;
            case render::PickFilter::Face:
                if (ctx.hover.kind == PickKind::Face) item.hoverFace = ctx.hover.index;
                break;
            case render::PickFilter::Edge:
                if (ctx.hover.kind == PickKind::Edge) item.hoverEdge = ctx.hover.index;
                break;
            }
        }
        m_items.push_back(std::move(item));
    };

    auto addGhost = [&](const Feature& f) {
        const render::GpuMesh* mesh = meshFor(f);
        if (!mesh)
            return;
        DrawItem item;
        item.mesh = mesh;
        item.style = DrawItem::Style::Ghost;
        const Color& c = f.color();
        item.color = {c.r, c.g, c.b, 1.0f};
        m_items.push_back(std::move(item));
    };

    if (ctx.edgeEdit.active()) {
        // While re-picking fillet edges, show the base instead of the result.
        for (const auto& f : doc.features()) {
            if (f->id() == ctx.edgeEdit.feature)
                continue;
            if (f->id() == ctx.edgeEdit.base || (f->visible() && !doc.isConsumed(f->id())))
                addSolid(*f);
        }
        if (const Feature* f = doc.find(ctx.edgeEdit.feature))
            addGhost(*f);
        return m_items;
    }

    for (const auto& f : doc.features())
        if (f->visible())
            addSolid(*f);

    // Ghost hidden features that are selected (e.g. a boolean input picked in the tree)
    // and the hidden inputs of selected CSG / dress-up features.
    for (FeatureId id : ctx.selection.features())
        if (const Feature* f = doc.find(id); f && !f->visible())
            addGhost(*f);
    for (FeatureId id : ctx.selection.features()) {
        const Feature* f = doc.find(id);
        if (!f || !f->consumesInputs() || !ctx.selection.isFeatureSelected(id))
            continue;
        for (FeatureId in : f->inputs())
            if (const Feature* i = doc.find(in); i && !i->visible())
                addGhost(*i);
    }
    return m_items;
}

FeatureId SceneView::featureForPickId(std::uint32_t pickId) const
{
    if (pickId == 0 || pickId > m_pickMap.size())
        return kNoFeature;
    return m_pickMap[pickId - 1];
}

BoundingBox SceneView::bounds() const
{
    BoundingBox bb;
    for (const auto& it : m_items)
        if (it.mesh && it.style == DrawItem::Style::Solid)
            bb.add(it.mesh->bounds());
    return bb;
}

BoundingBox SceneView::bounds(const model::Document& doc, const std::vector<FeatureId>& ids)
{
    BoundingBox bb;
    for (FeatureId id : ids)
        if (const Feature* f = doc.find(id))
            if (const render::GpuMesh* m = meshFor(*f))
                bb.add(m->bounds());
    return bb;
}

} // namespace cf::app
