#include "app/FeaController.h"
#include "app/AppContext.h"

#include "core/Log.h"
#include "fea/Export.h"
#include "fea/Mesher.h"
#include "fea/Solver.h"
#include "geom/Tessellator.h"
#include "model/Document.h"
#include "model/features/FeaFeatures.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace cf::app {

using model::StaticAnalysisFeature;

namespace {

std::uint64_t mix(std::uint64_t h, std::uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t bits(double d)
{
    std::uint64_t u = 0;
    std::memcpy(&u, &d, sizeof(u));
    return u;
}

const StaticAnalysisFeature* analysisFeature(const model::Document& doc, FeatureId id)
{
    const auto* f = doc.find(id);
    return f && f->type() == StaticAnalysisFeature::kType ? static_cast<const StaticAnalysisFeature*>(f) : nullptr;
}

/// A mode shape presented as a (displacement-only) static result for display.
std::shared_ptr<const fea::StaticResult> modeView(const fea::ModalResult& m, int mode)
{
    auto r = std::make_shared<fea::StaticResult>();
    if (mode < 0 || mode >= int(m.shapes.size()))
        return r;
    r->displacement = m.shapes[std::size_t(mode)];
    r->maxDisplacement = 1.0;
    r->equations = m.equations;
    r->solver = m.solver;
    return r;
}

std::string fmt(const char* f, double a, double b = 0, double c = 0)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), f, a, b, c);
    return buf;
}

} // namespace

struct FeaController::Job {
    FeatureId analysis = kNoFeature;
    bool solve = false;
    std::uint64_t meshKey = 0, solveKey = 0;

    MeshData surface;
    fea::MeshSettings meshSettings;
    fea::StaticSetup setup;
    bool modal = false;
    fea::ModalSetup modalSetup;
    std::shared_ptr<const fea::VolumeMesh> existingMesh;

    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    mutable std::mutex mutex;
    std::string stage = "Starting";
    double progress = 0.0;

    std::shared_ptr<const fea::VolumeMesh> mesh;
    double meshSeconds = 0.0;
    std::shared_ptr<const fea::StaticResult> result;
    std::shared_ptr<const fea::ModalResult> modalResult;
    std::string error;

    void report(const std::string& s, double p)
    {
        std::lock_guard lock(mutex);
        stage = s;
        progress = p;
    }

    void run()
    {
        try {
            mesh = existingMesh;
            if (!mesh) {
                report("Meshing", 0.02);
                const auto t0 = std::chrono::steady_clock::now();
                mesh = std::make_shared<const fea::VolumeMesh>(fea::generateVolumeMesh(surface, meshSettings));
                meshSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                if (cancel)
                    throw fea::FeaError("Cancelled");
            }
            if (solve) {
                fea::SolveControl ctl;
                ctl.cancel = &cancel;
                ctl.progress = [this](const std::string& s, double p) { report(s, 0.3 + 0.7 * p); };
                if (modal)
                    modalResult = std::make_shared<const fea::ModalResult>(fea::solveModal(*mesh, modalSetup, ctl));
                else
                    result = std::make_shared<const fea::StaticResult>(fea::solveStatic(*mesh, setup, ctl));
            }
        } catch (const std::exception& e) {
            error = e.what();
        }
        done = true;
    }
};

FeaController::~FeaController()
{
    if (m_job)
        m_job->cancel = true;
    if (m_thread.joinable())
        m_thread.join();
}

// ---- keys --------------------------------------------------------------------------

std::uint64_t FeaController::currentMeshKey(const model::Document& doc, FeatureId id) const
{
    const auto* a = analysisFeature(doc, id);
    if (!a)
        return 0;
    const auto* target = doc.find(a->target());
    if (!target)
        return 0;
    const fea::MeshSettings ms = a->meshSettings();
    std::uint64_t h = mix(0x51ed270b27f0a2bdull, target->resultKey());
    h = mix(h, bits(ms.maxSize));
    h = mix(h, std::uint64_t(ms.order));
    for (const auto& l : model::meshSettingsOf(doc, id).localSizes) {
        h = mix(h, bits(l.size));
        for (int f : l.faces)
            h = mix(h, std::uint64_t(f));
    }
    return h;
}

std::uint64_t FeaController::currentSolveKey(const model::Document& doc, FeatureId id) const
{
    const auto* a = analysisFeature(doc, id);
    if (!a)
        return 0;
    std::uint64_t h = mix(currentMeshKey(doc, id), a->resultKey());
    for (FeatureId c : doc.nestedChildren(id))
        if (const auto* f = doc.find(c); f && f->type() != model::MeshRefinementFeature::kType)
            h = mix(h, f->resultKey());
    return h;
}

bool FeaController::meshUpToDate(const model::Document& doc, FeatureId id) const
{
    const auto* rt = runtime(id);
    return rt && rt->mesh && rt->meshKey == currentMeshKey(doc, id);
}

bool FeaController::resultUpToDate(const model::Document& doc, FeatureId id) const
{
    const auto* rt = runtime(id);
    return rt && rt->result && rt->solveKey == currentSolveKey(doc, id);
}

const AnalysisRuntime* FeaController::runtime(FeatureId id) const
{
    auto it = m_runtimes.find(id);
    return it == m_runtimes.end() ? nullptr : &it->second;
}

// ---- jobs ---------------------------------------------------------------------------

bool FeaController::startMesh(AppContext& ctx, FeatureId id, std::string& error)
{
    return start(ctx, id, false, error);
}

bool FeaController::startSolve(AppContext& ctx, FeatureId id, std::string& error)
{
    return start(ctx, id, true, error);
}

bool FeaController::start(AppContext& ctx, FeatureId id, bool solve, std::string& error)
{
    if (busy()) {
        error = "An analysis job is already running";
        return false;
    }
    const auto* a = analysisFeature(ctx.doc, id);
    if (!a) {
        error = "Select an analysis first";
        return false;
    }
    if (a->state() != model::FeatureState::Ok) {
        error = a->name() + ": " + a->error();
        return false;
    }
    const auto* target = ctx.doc.find(a->target());

    auto job = std::make_shared<Job>();
    job->analysis = id;
    job->solve = solve;
    job->meshKey = currentMeshKey(ctx.doc, id);
    job->solveKey = currentSolveKey(ctx.doc, id);
    job->meshSettings = model::meshSettingsOf(ctx.doc, id);
    job->modal = a->isModal();
    if (solve) {
        try {
            if (job->modal)
                job->modalSetup = model::buildModalSetup(ctx.doc, id);
            else
                job->setup = model::buildStaticSetup(ctx.doc, id);
        } catch (const std::exception& e) {
            error = a->name() + ": " + e.what();
            return false;
        }
    }
    if (const auto* rt = runtime(id); rt && rt->mesh && rt->meshKey == job->meshKey && solve)
        job->existingMesh = rt->mesh;
    if (!job->existingMesh) {
        try {
            geom::TessellationParams tp;
            tp.relativeDeflection = 0.0005;
            tp.angularDeflectionDeg = 8.0;
            job->surface = geom::tessellate(target->shape(), tp);
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }

    m_job = job;
    m_thread = std::thread([job] { job->run(); });
    shownAnalysis = id;
    ctx.status(std::string(solve ? "Solving " : "Meshing ") + a->name() + " ...");
    return true;
}

void FeaController::cancel()
{
    if (m_job)
        m_job->cancel = true;
}

FeatureId FeaController::busyAnalysis() const
{
    return m_job ? m_job->analysis : kNoFeature;
}

std::string FeaController::progressText() const
{
    if (!m_job)
        return {};
    std::lock_guard lock(m_job->mutex);
    return m_job->stage;
}

double FeaController::progress() const
{
    if (!m_job)
        return 0.0;
    std::lock_guard lock(m_job->mutex);
    return m_job->progress;
}

void FeaController::poll(AppContext& ctx)
{
    if (!m_job || !m_job->done)
        return;
    if (m_thread.joinable())
        m_thread.join();
    std::shared_ptr<Job> job = std::move(m_job);
    m_job.reset();

    AnalysisRuntime& rt = m_runtimes[job->analysis];
    const auto* a = ctx.doc.find(job->analysis);
    const std::string name = a ? a->name() : "Analysis";
    if (job->mesh && job->mesh != rt.mesh) {
        rt.mesh = job->mesh;
        rt.meshKey = job->meshKey;
        rt.meshSeconds = job->meshSeconds;
        rt.result.reset(); // a new mesh invalidates old results
        rt.modal.reset();
        rt.solveKey = 0;
    }
    if (!job->error.empty()) {
        rt.message = job->error;
        ctx.status(name + ": " + job->error, job->error != "Cancelled");
        return;
    }
    rt.message.clear();
    if (job->modalResult) {
        rt.modal = job->modalResult;
        rt.mode = 0;
        rt.result = modeView(*rt.modal, 0);
        rt.solveKey = job->solveKey;
        shownAnalysis = job->analysis;
        showResults = true;
        if (field == fea::ResultField::VonMises)
            field = fea::ResultField::DisplacementMagnitude;
        std::string freqs;
        for (std::size_t i = 0; i < rt.modal->frequencies.size() && i < 4; ++i)
            freqs += fmt(i ? ", %.4g" : "%.4g", rt.modal->frequencies[i]);
        ctx.status(name + ": " + std::to_string(rt.modal->frequencies.size()) + " modes - " + freqs +
                   (rt.modal->frequencies.size() > 4 ? ", ... Hz" : " Hz"));
    } else if (job->result) {
        rt.modal.reset();
        rt.result = job->result;
        rt.solveKey = job->solveKey;
        shownAnalysis = job->analysis;
        showResults = true;
        const auto& r = *job->result;
        ctx.status(name + fmt(": solved - max von Mises %.4g MPa, max displacement %.4g mm", r.maxVonMises,
                              r.maxDisplacement));
    } else if (job->mesh) {
        shownAnalysis = job->analysis;
        ctx.status(name + ": " + std::to_string(job->mesh->nodeCount()) + " nodes, " +
                   std::to_string(job->mesh->elementCount()) + " elements" +
                   fmt(" (%.2f s)", job->meshSeconds));
    }
}

void FeaController::prune(const model::Document& doc)
{
    for (auto it = m_runtimes.begin(); it != m_runtimes.end();)
        it = analysisFeature(doc, it->first) ? std::next(it) : m_runtimes.erase(it);
    for (auto it = m_display.begin(); it != m_display.end();)
        it = m_runtimes.count(it->first) ? std::next(it) : m_display.erase(it);
    if (!analysisFeature(doc, shownAnalysis))
        shownAnalysis = kNoFeature;
}

void FeaController::reset()
{
    cancel();
    if (m_thread.joinable())
        m_thread.join();
    m_job.reset();
    m_runtimes.clear();
    m_display.clear();
    shownAnalysis = kNoFeature;
}

// ---- display -------------------------------------------------------------------------

double FeaController::effectiveDeformationScale(FeatureId id) const
{
    const auto* rt = runtime(id);
    if (!rt || !rt->result || !rt->mesh || !showResults)
        return 0.0;
    return autoDeformation ? fea::automaticDeformationScale(*rt->mesh, *rt->result) : deformationScale;
}

std::uint64_t FeaController::displayKey(FeatureId id) const
{
    const auto* rt = runtime(id);
    if (!rt || !rt->mesh)
        return 0;
    const bool withResult = showResults && rt->result;
    std::uint64_t h = mix(rt->meshKey, std::uint64_t(reinterpret_cast<std::uintptr_t>(rt->mesh.get())));
    h = mix(h, withResult ? std::uint64_t(reinterpret_cast<std::uintptr_t>(rt->result.get())) : 0);
    h = mix(h, withResult ? std::uint64_t(field) : 0);
    h = mix(h, bits(effectiveDeformationScale(id)));
    h = mix(h, showMeshEdges ? 1 : 0);
    h = mix(h, section ? 1 + std::uint64_t(sectionAxis) * 2 + (sectionFlip ? 1 : 0) : 0);
    h = mix(h, section ? bits(sectionPosition) : 0);
    return h;
}

const MeshData* FeaController::displaySurface(FeatureId id, float range[2])
{
    const auto* rt = runtime(id);
    if (!rt || !rt->mesh)
        return nullptr;
    DisplayCache& c = m_display[id];
    const std::uint64_t key = displayKey(id);
    if (c.key != key) {
        const bool withResult = showResults && rt->result;
        fea::SurfaceOptions o;
        o.field = withResult ? field : fea::ResultField::None;
        o.deformationScale = effectiveDeformationScale(id);
        o.elementEdges = showMeshEdges;
        o.section = sectionPlane(id, o.sectionNormal, o.sectionOffset);
        c.surface = fea::resultSurface(*rt->mesh, withResult ? rt->result.get() : nullptr, o);
        c.range[0] = 0.0f;
        c.range[1] = 1.0f;
        if (!c.surface.scalars.empty()) {
            const auto [mn, mx] = std::minmax_element(c.surface.scalars.begin(), c.surface.scalars.end());
            c.range[0] = *mn;
            c.range[1] = std::max(*mx, *mn + std::max(std::abs(*mn) * 1e-6f, 1e-12f));
        }
        c.key = key;
    }
    range[0] = c.range[0];
    range[1] = c.range[1];
    return &c.surface;
}

void FeaController::selectMode(FeatureId id, int mode)
{
    auto it = m_runtimes.find(id);
    if (it == m_runtimes.end() || !it->second.modal)
        return;
    AnalysisRuntime& rt = it->second;
    mode = std::clamp(mode, 0, std::max(0, int(rt.modal->shapes.size()) - 1));
    if (mode == rt.mode && rt.result)
        return;
    rt.mode = mode;
    rt.result = modeView(*rt.modal, mode);
}

bool FeaController::sectionPlane(FeatureId id, Vec3& normal, double& offset) const
{
    const auto* rt = runtime(id);
    if (!section || !rt || !rt->mesh)
        return false;
    BoundingBox bb;
    for (const auto& p : rt->mesh->nodes)
        bb.add(p);
    if (!bb.valid())
        return false;
    const int ax = std::clamp(sectionAxis, 0, 2);
    normal = Vec3(0.0);
    normal[ax] = sectionFlip ? -1.0 : 1.0;
    const double pos = bb.min[ax] + std::clamp(sectionPosition, 0.0, 1.0) * (bb.max[ax] - bb.min[ax]);
    offset = sectionFlip ? -pos : pos;
    return true;
}

bool FeaController::exportVtu(FeatureId id, const std::string& path, std::string& error) const
{
    const auto* rt = runtime(id);
    if (!rt || !rt->mesh) {
        error = "Mesh the analysis first";
        return false;
    }
    try {
        fea::writeVtu(path, *rt->mesh, rt->modal ? nullptr : rt->result.get(), rt->modal.get());
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace cf::app
