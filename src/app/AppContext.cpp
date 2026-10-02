#include "app/AppContext.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "geom/ShapeIO.h"
#include "model/features/FeaFeatures.h"
#include "model/features/PartFeatures.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <unordered_set>

namespace cf::app {

using model::BooleanFeature;
using model::EdgeFeature;
using model::Feature;
using model::FeatureState;

namespace {
double nowSeconds()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

AppContext::AppContext()
{
    history.reset(doc);
}

void AppContext::status(std::string msg, bool error)
{
    if (error)
        log::error(msg);
    else
        log::info(msg);
    m_status = std::move(msg);
    m_statusError = error;
    m_statusTime = nowSeconds();
}

// ---- evaluation & history -------------------------------------------------------

void AppContext::recompute()
{
    lastRecompute = doc.recompute();
    pruneSelection();
    fea.prune(doc);
}

void AppContext::commit(const std::string& label)
{
    recompute();
    history.commit(doc, label);
}

void AppContext::undo()
{
    if (!history.canUndo())
        return;
    const std::string label = history.undoLabel();
    cancelSubShapeEdit();
    history.undo(doc);
    recompute();
    status("Undo: " + label);
}

void AppContext::redo()
{
    if (!history.canRedo())
        return;
    const std::string label = history.redoLabel();
    cancelSubShapeEdit();
    history.redo(doc);
    recompute();
    status("Redo: " + label);
}

void AppContext::pruneSelection()
{
    selection.removeIf([&](const SelItem& it) {
        const Feature* f = doc.find(it.feature);
        if (!f)
            return true;
        if (it.kind == render::PickKind::Face)
            return it.index < 1 || it.index > f->shape().faceCount();
        if (it.kind == render::PickKind::Edge)
            return it.index < 1 || it.index > f->shape().edgeCount();
        return false;
    });
    if (shapeEdit.active() && (!doc.find(shapeEdit.feature) || !doc.find(shapeEdit.base)))
        shapeEdit = {};
}

// ---- modeling commands ------------------------------------------------------------

Feature* AppContext::createFeature(const std::string& type)
{
    cancelSubShapeEdit();
    Feature* f = doc.create(type);
    if (!f) {
        status("Unknown feature type " + type, true);
        return nullptr;
    }
    commit("Create " + f->name());
    selection.set({f->id()});
    pickFilter = render::PickFilter::Object;
    if (f->state() == FeatureState::Error)
        status(f->name() + ": " + f->error(), true);
    else
        status("Created " + f->name());
    return f;
}

void AppContext::booleanFromSelection(int op)
{
    static const char* names[] = {"Union", "Cut", "Intersect"};
    const auto feats = selection.features();
    if (feats.size() < 2) {
        status(std::string(names[op]) + ": select at least two objects (the first one is the base)", true);
        return;
    }
    cancelSubShapeEdit();
    auto* b = static_cast<BooleanFeature*>(doc.create("Part::Boolean"));
    b->setName(doc.uniqueName(names[op]));
    b->props().set(BooleanFeature::kOperation, op);
    b->props().set(BooleanFeature::kBase, feats[0]);
    b->props().set(BooleanFeature::kTools, std::vector<FeatureId>(feats.begin() + 1, feats.end()));
    for (FeatureId id : feats)
        if (Feature* in = doc.find(id))
            in->setVisible(false);
    if (Feature* base = doc.find(feats[0]))
        b->setColor(base->color());
    commit(b->name());
    selection.set({b->id()});
    pickFilter = render::PickFilter::Object;
    if (b->state() == FeatureState::Error)
        status(b->name() + ": " + b->error(), true);
    else
        status("Created " + b->name());
}

void AppContext::dressUpFromSelection(const std::string& type)
{
    const char* what = type == "Part::Fillet" ? "Fillet" : "Chamfer";
    FeatureId base = kNoFeature;
    std::vector<int> edges;
    for (const auto& it : selection.items()) {
        if (it.kind != render::PickKind::Edge)
            continue;
        if (base != kNoFeature && it.feature != base) {
            status(std::string(what) + ": all edges must belong to the same object", true);
            return;
        }
        base = it.feature;
        edges.push_back(it.index);
    }
    if (edges.empty()) {
        status(std::string(what) + ": select one or more edges first (Edge selection mode, key 3)", true);
        pickFilter = render::PickFilter::Edge;
        return;
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

    cancelSubShapeEdit();
    Feature* f = doc.create(type);
    f->props().set(EdgeFeature::kBase, base);
    f->props().set(EdgeFeature::kEdges, edges);
    if (Feature* b = doc.find(base)) {
        b->setVisible(false);
        f->setColor(b->color());
    }
    commit(f->name());
    selection.set({f->id()});
    pickFilter = render::PickFilter::Object;
    if (f->state() == FeatureState::Error)
        status(f->name() + ": " + f->error(), true);
    else
        status("Created " + f->name() + " on " + std::to_string(edges.size()) + " edge(s)");
}

void AppContext::deleteSelection()
{
    const auto feats = selection.features();
    if (feats.empty())
        return;
    cancelSubShapeEdit();

    std::unordered_set<FeatureId> inputs;
    std::size_t removedCount = 0;
    for (FeatureId id : feats) {
        if (!doc.find(id))
            continue; // already removed as a dependent
        for (FeatureId d : doc.downstream(id))
            if (const Feature* f = doc.find(d))
                for (FeatureId in : f->inputs())
                    inputs.insert(in);
        for (FeatureId in : doc.find(id)->inputs())
            inputs.insert(in);
        removedCount += doc.remove(id).size();
    }
    // Inputs that are no longer consumed become visible again.
    for (FeatureId in : inputs)
        if (Feature* f = doc.find(in); f && !doc.isConsumed(in))
            f->setVisible(true);

    selection.clear();
    commit("Delete");
    status("Deleted " + std::to_string(removedCount) + " feature(s)");
}

void AppContext::setVisible(FeatureId id, bool visible)
{
    if (Feature* f = doc.find(id); f && f->visible() != visible) {
        f->setVisible(visible);
        commit(visible ? "Show " + f->name() : "Hide " + f->name());
    }
}

void AppContext::selectAll()
{
    selection.clear();
    for (FeatureId id : doc.roots())
        if (const Feature* f = doc.find(id); f && f->visible())
            selection.add({id});
}

void AppContext::beginSubShapeEdit(FeatureId id)
{
    Feature* f = doc.find(id);
    if (!f)
        return;
    const model::Property* prop = nullptr;
    for (const auto& p : f->props().all())
        if (p.type == model::PropertyType::IndexList) {
            prop = &p;
            break;
        }
    if (!prop)
        return;
    const FeatureId base = f->subShapeTarget(doc);
    if (!doc.find(base)) {
        status("The referenced solid is missing", true);
        return;
    }
    const auto kind = prop->subShape == "face" ? render::PickKind::Face : render::PickKind::Edge;
    shapeEdit = {id, base, kind, prop->key};
    selection.clear();
    for (int e : std::get<std::vector<int>>(prop->value))
        selection.add({base, kind, e});
    pickFilter = kind == render::PickKind::Face ? render::PickFilter::Face : render::PickFilter::Edge;
    status(std::string("Pick ") + (kind == render::PickKind::Face ? "faces" : "edges") +
           " of '" + doc.find(base)->name() + "', then press Apply (Enter)");
}

void AppContext::applySubShapeEdit()
{
    if (!shapeEdit.active())
        return;
    auto items = selection.subShapes(shapeEdit.base, shapeEdit.kind);
    const char* what = shapeEdit.kind == render::PickKind::Face ? "face" : "edge";
    if (items.empty()) {
        status(std::string("Select at least one ") + what, true);
        return;
    }
    std::sort(items.begin(), items.end());
    const SubShapeEditSession session = shapeEdit;
    shapeEdit = {};
    if (Feature* f = doc.find(session.feature)) {
        f->props().set(session.property, items);
        commit("Edit " + std::string(what) + "s of " + f->name());
        status("Updated " + f->name());
    }
    selection.set({session.feature});
    pickFilter = render::PickFilter::Object;
}

void AppContext::cancelSubShapeEdit()
{
    if (!shapeEdit.active())
        return;
    const FeatureId id = shapeEdit.feature;
    shapeEdit = {};
    selection.set({id});
    pickFilter = render::PickFilter::Object;
}

// ---- analysis -------------------------------------------------------------------

FeatureId AppContext::contextAnalysis() const
{
    const auto feats = selection.features();
    if (feats.size() == 1) {
        if (const Feature* f = doc.find(feats[0])) {
            if (f->type() == model::StaticAnalysisFeature::kType)
                return f->id();
            if (const auto* bc = dynamic_cast<const model::FeaBoundaryFeature*>(f))
                return bc->analysis();
        }
    }
    if (shapeEdit.active())
        if (const auto* bc = dynamic_cast<const model::FeaBoundaryFeature*>(doc.find(shapeEdit.feature)))
            return bc->analysis();
    return fea.shownAnalysis;
}

void AppContext::createAnalysisFromSelection()
{
    const auto feats = selection.features();
    const Feature* solid = feats.size() == 1 ? doc.find(feats[0]) : nullptr;
    if (!solid || !solid->producesGeometry() || solid->state() != FeatureState::Ok) {
        status("Static analysis: select one solid first", true);
        return;
    }
    cancelSubShapeEdit();
    Feature* a = doc.create(model::StaticAnalysisFeature::kType);
    a->props().set(model::StaticAnalysisFeature::kTarget, solid->id());
    a->setName(doc.uniqueName("Static"));
    commit("Create " + a->name());
    selection.clear();
    pickFilter = render::PickFilter::Face;
    fea.shownAnalysis = a->id();
    status("Created " + a->name() + ". Now select faces and add Fixed supports and loads.");
}

void AppContext::createBoundaryFromSelection(const std::string& type)
{
    const char* what = type == model::FixedSupportFeature::kType ? "Fixed support"
                       : type == model::ForceFeature::kType      ? "Force"
                                                                 : "Pressure";
    FeatureId solid = kNoFeature;
    std::vector<int> faces;
    for (const auto& it : selection.items()) {
        if (it.kind != render::PickKind::Face)
            continue;
        if (solid != kNoFeature && it.feature != solid) {
            status(std::string(what) + ": all faces must belong to the same solid", true);
            return;
        }
        solid = it.feature;
        faces.push_back(it.index);
    }
    if (faces.empty()) {
        status(std::string(what) + ": select one or more faces first (Face selection mode, key 2)", true);
        pickFilter = render::PickFilter::Face;
        return;
    }
    std::sort(faces.begin(), faces.end());
    faces.erase(std::unique(faces.begin(), faces.end()), faces.end());

    cancelSubShapeEdit();
    // Use the analysis in context if it is for this solid, else the latest one, else create one.
    FeatureId analysis = kNoFeature;
    if (const auto* a = dynamic_cast<const model::StaticAnalysisFeature*>(doc.find(contextAnalysis()));
        a && a->target() == solid)
        analysis = a->id();
    if (analysis == kNoFeature) {
        const auto list = model::analysesOf(doc, solid);
        if (!list.empty())
            analysis = list.back();
    }
    if (analysis == kNoFeature) {
        Feature* a = doc.create(model::StaticAnalysisFeature::kType);
        a->props().set(model::StaticAnalysisFeature::kTarget, solid);
        a->setName(doc.uniqueName("Static"));
        analysis = a->id();
    }
    Feature* bc = doc.create(type);
    bc->props().set(model::FeaBoundaryFeature::kAnalysis, analysis);
    bc->props().set(model::FeaBoundaryFeature::kFaces, faces);
    commit("Create " + bc->name());
    fea.shownAnalysis = analysis;
    selection.set({bc->id()});
    pickFilter = render::PickFilter::Face;
    status("Created " + bc->name() + " on " + std::to_string(faces.size()) + " face(s)");
}

void AppContext::meshAnalysis(FeatureId analysis)
{
    std::string err;
    if (!fea.startMesh(*this, analysis, err))
        status(err, true);
}

void AppContext::solveAnalysis(FeatureId analysis)
{
    std::string err;
    if (!fea.startSolve(*this, analysis, err))
        status(err, true);
}

// ---- files ---------------------------------------------------------------------

void AppContext::newDocument()
{
    shapeEdit = {};
    fea.reset();
    doc.clear();
    selection.clear();
    scene.clear();
    filePath.clear();
    recompute();
    history.reset(doc);
    camera.fit({}, false);
    camera.setStandardView(render::StandardView::Isometric, false);
    status("New document");
}

bool AppContext::openDocument(const std::string& path)
{
    try {
        shapeEdit = {};
        doc.load(path);
        fea.reset();
        selection.clear();
        recompute();
        history.reset(doc);
        filePath = path;
        fitAll(false);
        status("Opened " + path + (lastRecompute.failed ? " (" + std::to_string(lastRecompute.failed) +
                                                              " feature(s) failed)"
                                                        : ""));
        return true;
    } catch (const std::exception& e) {
        status(std::string("Open failed: ") + e.what(), true);
        return false;
    }
}

bool AppContext::saveDocument(const std::string& path)
{
    try {
        doc.save(path);
        filePath = path;
        history.markSaved();
        status("Saved " + path);
        return true;
    } catch (const std::exception& e) {
        status(std::string("Save failed: ") + e.what(), true);
        return false;
    }
}

void AppContext::importStep(const std::string& path)
{
    Feature* f = doc.create("Part::ImportStep");
    f->props().set(model::ImportStepFeature::kPath, path);
    f->setName(doc.uniqueName(paths::toUtf8(paths::fromUtf8(path).stem())));
    commit("Import " + f->name());
    if (f->state() != FeatureState::Ok) {
        status("Import failed: " + f->error(), true);
        return;
    }
    selection.set({f->id()});
    fitAll();
    status("Imported " + path);
}

std::vector<geom::Shape> AppContext::exportShapes() const
{
    std::vector<geom::Shape> shapes;
    for (FeatureId id : selection.features())
        if (const Feature* f = doc.find(id); f && f->state() == FeatureState::Ok)
            shapes.push_back(f->shape());
    if (!shapes.empty())
        return shapes;
    for (FeatureId id : doc.roots())
        if (const Feature* f = doc.find(id); f && f->visible() && f->state() == FeatureState::Ok)
            shapes.push_back(f->shape());
    return shapes;
}

void AppContext::exportStep(const std::string& path)
{
    try {
        auto shapes = exportShapes();
        geom::exportStep(shapes, path);
        status("Exported " + std::to_string(shapes.size()) + " shape(s) to " + path);
    } catch (const std::exception& e) {
        status(e.what(), true);
    }
}

void AppContext::exportStl(const std::string& path)
{
    try {
        auto shapes = exportShapes();
        geom::exportStl(shapes, path);
        status("Exported " + std::to_string(shapes.size()) + " shape(s) to " + path);
    } catch (const std::exception& e) {
        status(e.what(), true);
    }
}

void AppContext::guardUnsaved(std::function<void()> action)
{
    if (!isModified()) {
        action();
        return;
    }
    pendingAfterConfirm = std::move(action);
    confirmUnsavedOpen = true;
}

void AppContext::requestFile(FileRequest req)
{
    fileRequests.push_back(std::move(req));
}

// ---- view ------------------------------------------------------------------------

void AppContext::fitAll(bool animate)
{
    scene.build(*this);
    camera.fit(scene.bounds(), animate);
}

void AppContext::fitSelection()
{
    const auto feats = selection.features();
    if (feats.empty()) {
        fitAll();
        return;
    }
    camera.fit(scene.bounds(doc, feats), true);
}

std::string AppContext::windowTitle() const
{
    std::string name = filePath.empty() ? "Untitled" : paths::toUtf8(paths::fromUtf8(filePath).filename());
    return name + (isModified() ? " *" : "") + " - CadForge";
}

} // namespace cf::app
