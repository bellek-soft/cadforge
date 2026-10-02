#include "app/SceneView.h"
#include "app/AppContext.h"

#include "core/Log.h"
#include "geom/Tessellator.h"
#include "model/features/FeaFeatures.h"

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
    m_feaMesh.reset();
    m_feaKey = 0;
    m_feaAnalysis = kNoFeature;
}

const std::vector<FaceInfo>* SceneView::faceInfo(FeatureId id) const
{
    auto it = m_meshes.find(id);
    return it == m_meshes.end() || !it->second.mesh ? nullptr : &it->second.faces;
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
        e.faces.assign(data.faceRanges.size(), FaceInfo{});
        for (std::size_t fi = 0; fi < data.faceRanges.size(); ++fi) {
            const IndexRange& r = data.faceRanges[fi];
            FaceInfo& info = e.faces[fi];
            Vec3 c(0.0), n(0.0);
            double area = 0.0;
            for (std::uint32_t t = r.first; t + 2 < r.first + r.count; t += 3) {
                Vec3 p[3];
                for (int k = 0; k < 3; ++k) {
                    const auto v = data.indices[t + std::uint32_t(k)];
                    p[k] = Vec3(data.positions[3 * v], data.positions[3 * v + 1], data.positions[3 * v + 2]);
                }
                const Vec3 an = 0.5 * glm::cross(p[1] - p[0], p[2] - p[0]);
                const double a = glm::length(an);
                c += a * (p[0] + p[1] + p[2]) / 3.0;
                n += an;
                area += a;
            }
            if (area > 0) {
                info.centroid = c / area;
                info.area = area;
                if (glm::length(n) > 1e-12 * area)
                    info.normal = glm::normalize(n);
            }
        }
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

    // ---- analysis display: mesh / results replace the analysed solid ----
    FeatureId feaTarget = kNoFeature;
    bool feaScalars = false;
    glm::vec2 feaRange(0.0f, 1.0f);
    const FeatureId shown = ctx.fea.shownAnalysis;
    if (shown != kNoFeature && !ctx.shapeEdit.active() && ctx.fea.meshUpToDate(doc, shown)) {
        const auto* a = dynamic_cast<const model::StaticAnalysisFeature*>(doc.find(shown));
        float range[2];
        const MeshData* surf = a ? ctx.fea.displaySurface(shown, range) : nullptr;
        if (surf) {
            const std::uint64_t key = ctx.fea.displayKey(shown);
            if (!m_feaMesh || key != m_feaKey || m_feaAnalysis != shown) {
                if (!m_feaMesh)
                    m_feaMesh = std::make_unique<render::GpuMesh>();
                m_feaMesh->upload(*surf);
                m_feaKey = key;
                m_feaAnalysis = shown;
            }
            feaTarget = a->target();
            feaScalars = !surf->scalars.empty();
            feaRange = {range[0], range[1]};
        }
    }

    // ---- support / load tints for the analysis in context ----
    FeatureId tintTarget = kNoFeature;
    std::vector<std::pair<int, glm::vec3>> tints;
    if (const auto* a = dynamic_cast<const model::StaticAnalysisFeature*>(doc.find(ctx.contextAnalysis()))) {
        tintTarget = a->target();
        for (FeatureId c : doc.nestedChildren(a->id()))
            if (const auto* bc = dynamic_cast<const model::FeaBoundaryFeature*>(doc.find(c)))
                for (int face : bc->faces())
                    tints.emplace_back(face, glm::vec3(bc->color().r, bc->color().g, bc->color().b));
        if (const Feature* t = doc.find(tintTarget)) {
            const glm::vec3 base(t->color().r, t->color().g, t->color().b);
            for (auto& [face, color] : tints)
                color = glm::mix(base, color, 0.7f);
        }
    }

    auto addSolid = [&](const Feature& f) {
        const bool fea = f.id() == feaTarget;
        const render::GpuMesh* mesh = fea ? m_feaMesh.get() : meshFor(f);
        if (!mesh)
            return;
        DrawItem item;
        item.mesh = mesh;
        if (fea) {
            item.showScalars = feaScalars;
            item.scalarRange = feaRange;
        }
        if (f.id() == tintTarget && !(fea && feaScalars))
            item.faceTints = tints;
        m_pickMap.push_back(f.id());
        item.pickId = static_cast<std::uint32_t>(m_pickMap.size());
        const Color& c = f.color();
        item.color = fea ? glm::vec4(0.78f, 0.80f, 0.83f, 1.0f) : glm::vec4(c.r, c.g, c.b, c.a);
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

    if (ctx.shapeEdit.active()) {
        // While re-picking fillet edges, show the base instead of the result.
        for (const auto& f : doc.features()) {
            if (f->id() == ctx.shapeEdit.feature)
                continue;
            if (f->id() == ctx.shapeEdit.base || (f->visible() && !doc.isConsumed(f->id())))
                addSolid(*f);
        }
        if (const Feature* f = doc.find(ctx.shapeEdit.feature))
            addGhost(*f);
        return m_items;
    }

    for (const auto& f : doc.features())
        if (f->visible() || f->id() == feaTarget)
            addSolid(*f);

    // Undeformed outline under a deformed result.
    if (feaTarget != kNoFeature && feaScalars && ctx.fea.effectiveDeformationScale(shown) > 0.0)
        if (const Feature* f = doc.find(feaTarget))
            addGhost(*f);

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
