#include "model/Document.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "geom/Naming.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace cf::model {

using json = nlohmann::ordered_json;

namespace {

constexpr int kFormatVersion = 1;

std::uint64_t fnv1a(std::string_view s, std::uint64_t h = 1469598103934665603ull)
{
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string num(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

json valueToJson(const Property& p)
{
    return std::visit(
        [](const auto& v) -> json {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Vec3>)
                return json::array({v.x, v.y, v.z});
            else
                return json(v);
        },
        p.value);
}

void valueFromJson(const json& j, Property& p)
{
    switch (p.type) {
    case PropertyType::Double:
        p.value = j.get<double>();
        break;
    case PropertyType::Int:
    case PropertyType::Enum:
        p.value = j.get<int>();
        break;
    case PropertyType::Bool:
        p.value = j.get<bool>();
        break;
    case PropertyType::Vec3:
        if (!j.is_array() || j.size() != 3)
            throw std::runtime_error("property '" + p.key + "' expects [x,y,z]");
        p.value = Vec3(j[0].get<double>(), j[1].get<double>(), j[2].get<double>());
        break;
    case PropertyType::String:
    case PropertyType::FilePath:
        p.value = j.get<std::string>();
        break;
    case PropertyType::FeatureRef:
        p.value = j.get<FeatureId>();
        break;
    case PropertyType::FeatureRefList:
        p.value = j.get<std::vector<FeatureId>>();
        break;
    case PropertyType::IndexList:
        p.value = j.get<std::vector<int>>();
        break;
    }
}

double round4(float v)
{
    return std::round(double(v) * 10000.0) / 10000.0;
}

constexpr double kRefAcceptDistance = 0.3; // see geom::signatureDistance

json refsToJson(const std::vector<SubShapeRef>& refs)
{
    json out = json::array();
    for (const auto& r : refs) {
        const auto& g = r.sig;
        out.push_back(json::array({r.index, g.geomType, g.center.x, g.center.y, g.center.z, g.dir.x, g.dir.y,
                                   g.dir.z, g.size, g.radius}));
    }
    return out;
}

std::vector<SubShapeRef> refsFromJson(const json& j)
{
    std::vector<SubShapeRef> out;
    if (!j.is_array())
        return out;
    for (const auto& e : j) {
        if (!e.is_array() || e.size() != 10)
            continue;
        SubShapeRef r;
        r.index = e[0].get<int>();
        r.sig.geomType = e[1].get<int>();
        r.sig.center = {e[2].get<double>(), e[3].get<double>(), e[4].get<double>()};
        r.sig.dir = {e[5].get<double>(), e[6].get<double>(), e[7].get<double>()};
        r.sig.size = e[8].get<double>();
        r.sig.radius = e[9].get<double>();
        out.push_back(r);
    }
    return out;
}

bool isPlacementKey(std::string_view k)
{
    return k == Feature::kPosition || k == Feature::kRotation;
}

} // namespace

Document::Document(const FeatureRegistry& registry)
    : m_registry(registry)
{
}

Document::~Document() = default;

// ---- structure ---------------------------------------------------------------

std::string Document::uniqueName(std::string_view base) const
{
    std::unordered_set<std::string> used;
    for (const auto& f : m_features)
        used.insert(f->name());
    for (int i = 1;; ++i) {
        std::string n = std::string(base) + std::to_string(i);
        if (!used.count(n))
            return n;
    }
}

Feature* Document::create(std::string_view type)
{
    auto f = m_registry.create(type);
    if (!f)
        return nullptr;
    return add(std::move(f));
}

Feature* Document::add(std::unique_ptr<Feature> feature)
{
    feature->m_id = m_nextId++;
    if (feature->name().empty())
        feature->setName(uniqueName(feature->typeLabel()));
    m_features.push_back(std::move(feature));
    return m_features.back().get();
}

std::vector<FeatureId> Document::remove(FeatureId id)
{
    if (!find(id))
        return {};
    std::vector<FeatureId> removed = downstream(id);
    removed.push_back(id);
    std::unordered_set<FeatureId> set(removed.begin(), removed.end());
    m_features.erase(std::remove_if(m_features.begin(), m_features.end(),
                                    [&](const auto& f) { return set.count(f->id()) > 0; }),
                     m_features.end());
    return removed;
}

void Document::clear()
{
    m_features.clear();
    m_nextId = 1;
}

Feature* Document::find(FeatureId id)
{
    for (auto& f : m_features)
        if (f->id() == id)
            return f.get();
    return nullptr;
}

const Feature* Document::find(FeatureId id) const
{
    for (const auto& f : m_features)
        if (f->id() == id)
            return f.get();
    return nullptr;
}

// ---- dependency queries --------------------------------------------------------

std::vector<FeatureId> Document::consumers(FeatureId id) const
{
    std::vector<FeatureId> out;
    for (const auto& f : m_features) {
        const auto in = f->inputs();
        if (std::find(in.begin(), in.end(), id) != in.end())
            out.push_back(f->id());
    }
    return out;
}

bool Document::isConsumed(FeatureId id) const
{
    for (FeatureId c : consumers(id))
        if (const Feature* f = find(c); f && f->consumesInputs())
            return true;
    return false;
}

std::vector<FeatureId> Document::nestedChildren(FeatureId id) const
{
    std::vector<FeatureId> out;
    for (const auto& f : m_features) {
        if (!f->nestUnderInput())
            continue;
        const auto in = f->inputs();
        if (!in.empty() && in[0] == id)
            out.push_back(f->id());
    }
    return out;
}

std::vector<FeatureId> Document::roots() const
{
    std::vector<FeatureId> out;
    for (const auto& f : m_features)
        if (!isConsumed(f->id()) && !(f->nestUnderInput() && !f->inputs().empty() && find(f->inputs()[0])))
            out.push_back(f->id());
    return out;
}

std::vector<FeatureId> Document::downstream(FeatureId id) const
{
    std::vector<FeatureId> out;
    std::unordered_set<FeatureId> seen{id};
    std::vector<FeatureId> stack{id};
    while (!stack.empty()) {
        FeatureId cur = stack.back();
        stack.pop_back();
        for (FeatureId c : consumers(cur))
            if (seen.insert(c).second) {
                out.push_back(c);
                stack.push_back(c);
            }
    }
    return out;
}

bool Document::canReference(FeatureId from, FeatureId candidate) const
{
    if (from == candidate || !find(candidate))
        return false;
    const auto down = downstream(from);
    return std::find(down.begin(), down.end(), candidate) == down.end();
}

// ---- evaluation ------------------------------------------------------------------

std::vector<Feature*> Document::topologicalOrder(std::vector<Feature*>& cyclic) const
{
    enum Mark { None, Visiting, Done };
    std::unordered_map<FeatureId, Mark> mark;
    std::vector<Feature*> order;
    std::unordered_set<Feature*> inCycle;

    std::function<void(Feature*)> visit = [&](Feature* f) {
        Mark& m = mark[f->id()];
        if (m == Done)
            return;
        if (m == Visiting) {
            inCycle.insert(f);
            return;
        }
        m = Visiting;
        for (FeatureId in : f->inputs())
            if (Feature* dep = const_cast<Document*>(this)->find(in))
                visit(dep);
        mark[f->id()] = Done;
        order.push_back(f);
    };
    for (const auto& f : m_features)
        visit(f.get());

    // Everything downstream of a cycle member is also unusable.
    for (Feature* f : std::vector<Feature*>(inCycle.begin(), inCycle.end()))
        for (FeatureId d : downstream(f->id()))
            inCycle.insert(const_cast<Document*>(this)->find(d));

    std::vector<Feature*> clean;
    for (Feature* f : order)
        (inCycle.count(f) ? cyclic : clean).push_back(f);
    return clean;
}

std::string Document::resolveSubShapeRefs(Feature& f)
{
    bool hasRefs = false;
    for (const auto& p : f.props().all())
        hasRefs |= p.type == PropertyType::IndexList && !p.subShape.empty();
    if (!hasRefs)
        return {};
    const Feature* target = find(f.subShapeTarget(*this));
    if (!target || target->m_state != FeatureState::Ok || target->m_localShape.isNull())
        return {}; // the missing / failed input is reported by the feature itself

    // Nothing to do if the target did not change and every index is known.
    bool known = true;
    for (const auto& p : f.props().all()) {
        if (p.type != PropertyType::IndexList || p.subShape.empty())
            continue;
        const auto it = f.m_subRefs.find(p.key);
        for (int i : std::get<std::vector<int>>(p.value))
            known &= it != f.m_subRefs.end() &&
                     std::any_of(it->second.begin(), it->second.end(),
                                 [&](const SubShapeRef& r) { return r.index == i && r.sig.valid(); });
    }
    if (known && target->m_localKey == f.m_refsTargetKey)
        return {};

    // Signatures live in the target's local frame, so moving it changes nothing.
    const geom::Shape& shape = target->m_localShape;
    const double scale = std::max(shape.bounds().diagonal(), 1e-6);
    std::string lost;
    for (auto& p : f.props().all()) {
        if (p.type != PropertyType::IndexList || p.subShape.empty())
            continue;
        const auto kind = p.subShape == "face" ? geom::SubShapeKind::Face : geom::SubShapeKind::Edge;
        const char* kindName = kind == geom::SubShapeKind::Face ? "Face" : "Edge";
        std::vector<SubShapeRef>& stored = f.m_subRefs[p.key];
        std::vector<int> indices;
        std::vector<SubShapeRef> refs;
        for (int i : std::get<std::vector<int>>(p.value)) {
            const auto old = std::find_if(stored.begin(), stored.end(),
                                          [&](const SubShapeRef& r) { return r.index == i && r.sig.valid(); });
            const geom::SubShapeSignature current = geom::signature(shape, kind, i);
            int resolved = i;
            geom::SubShapeSignature sig = current;
            if (old != stored.end() && geom::signatureDistance(old->sig, current, scale) > 1e-6) {
                // The referenced face / edge changed: find where it went.
                const geom::SubShapeMatch m = geom::findBestMatch(shape, kind, old->sig);
                if (m.index > 0 && m.distance < kRefAcceptDistance) {
                    resolved = m.index;
                    sig = geom::signature(shape, kind, m.index);
                    if (resolved != i)
                        log::info(f.name(), ": ", kindName, " ", i, " of '", target->name(), "' is now ", kindName,
                                  " ", resolved);
                } else {
                    // Keep the old signature: the reference may come back on a later change.
                    sig = old->sig;
                    lost += (lost.empty() ? "" : ", ") + std::to_string(i);
                }
            } else if (!current.valid()) {
                lost += (lost.empty() ? "" : ", ") + std::to_string(i);
            }
            if (std::find(indices.begin(), indices.end(), resolved) != indices.end())
                continue; // two references collapsed onto the same sub-shape
            indices.push_back(resolved);
            refs.push_back({resolved, sig});
        }
        p.value = indices;
        stored = std::move(refs);
        if (!lost.empty()) {
            return std::string(kindName) + (lost.find(',') == std::string::npos ? " " : "s ") + lost + " of '" +
                   target->name() + "' could not be found after the last change - re-pick them";
        }
    }
    f.m_refsTargetKey = target->m_localKey;
    return {};
}

RecomputeStats Document::recompute()
{
    const auto t0 = std::chrono::steady_clock::now();
    RecomputeStats stats;
    ExecContext ctx(*this);

    std::vector<Feature*> cyclic;
    const auto order = topologicalOrder(cyclic);

    for (Feature* f : cyclic) {
        f->m_state = FeatureState::Error;
        f->m_error = "Circular dependency";
        f->m_shape = {};
        f->m_localShape = {};
        f->m_localKey = 0;
        f->m_resultKey = fnv1a("cycle" + std::to_string(f->id()));
        ++stats.failed;
    }

    for (Feature* f : order) {
        // 0) Topological naming: follow referenced faces / edges through upstream changes.
        const std::string refError = resolveSubShapeRefs(*f);

        // 1) Content key of the local result: type + parameters + input results.
        std::string src(f->type());
        if (!refError.empty())
            src += "|referror=" + refError;
        for (const auto& p : f->props().all())
            if (!isPlacementKey(p.key))
                src += "|" + p.key + "=" + valueToJson(p).dump();
        src += "|salt=" + f->cacheSalt();
        for (FeatureId in : f->inputs()) {
            const Feature* i = find(in);
            src += "|in" + std::to_string(in) + "=" + (i ? std::to_string(i->m_resultKey) : "missing");
        }
        const std::uint64_t localKey = fnv1a(src);

        // 2) Execute or reuse.
        if (localKey != f->m_localKey || f->m_state == FeatureState::Pending) {
            f->m_localKey = localKey;
            if (!refError.empty()) {
                f->m_localShape = {};
                f->m_state = FeatureState::Error;
                f->m_error = refError;
                log::warn(f->name(), ": ", refError);
            } else if (!f->producesGeometry()) {
                try {
                    f->validate(ctx);
                    f->m_state = FeatureState::Ok;
                    f->m_error.clear();
                } catch (const std::exception& e) {
                    f->m_state = FeatureState::Error;
                    f->m_error = e.what();
                }
                f->m_localShape = {};
            } else if (const geom::Shape* cached = cacheFind(localKey)) {
                f->m_localShape = *cached;
                f->m_state = FeatureState::Ok;
                f->m_error.clear();
            } else {
                try {
                    geom::Shape s = f->execute(ctx);
                    if (s.isNull())
                        throw geom::GeomError("Operation produced no geometry");
                    f->m_localShape = s;
                    f->m_state = FeatureState::Ok;
                    f->m_error.clear();
                    cacheStore(localKey, s);
                } catch (const std::exception& e) {
                    f->m_localShape = {};
                    f->m_state = FeatureState::Error;
                    f->m_error = e.what();
                    log::warn(f->name(), ": ", e.what());
                }
                ++stats.executed;
            }
        }

        // 3) Apply placement (cheap: shares geometry and triangulation).
        const Placement pl = f->placement();
        std::uint64_t resultKey = localKey;
        if (f->m_state == FeatureState::Ok && f->producesGeometry()) {
            try {
                f->m_shape = pl.isIdentity() ? f->m_localShape : f->m_localShape.transformed(pl.matrix());
            } catch (const std::exception& e) {
                f->m_shape = {};
                f->m_state = FeatureState::Error;
                f->m_error = e.what();
            }
        } else {
            f->m_shape = {};
        }
        for (int i = 0; i < 3; ++i)
            resultKey = fnv1a(num(pl.position[i]) + "," + num(pl.rotationDeg[i]), resultKey);
        if (f->m_state != FeatureState::Ok) {
            resultKey = fnv1a("error:" + f->m_error, resultKey);
            ++stats.failed;
        }
        f->m_resultKey = resultKey;
    }

    stats.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return stats;
}

void Document::cacheStore(std::uint64_t key, const geom::Shape& s)
{
    if (m_cache.count(key))
        return;
    m_cache.emplace(key, s);
    m_cacheOrder.push_back(key);
    while (m_cacheOrder.size() > kCacheCapacity) {
        m_cache.erase(m_cacheOrder.front());
        m_cacheOrder.pop_front();
    }
}

const geom::Shape* Document::cacheFind(std::uint64_t key) const
{
    auto it = m_cache.find(key);
    return it == m_cache.end() ? nullptr : &it->second;
}

// ---- persistence -------------------------------------------------------------------

json Document::toJson() const
{
    json features = json::array();
    for (const auto& f : m_features) {
        json props = json::object();
        for (const auto& p : f->props().all())
            props[p.key] = valueToJson(p);
        const Color& c = f->color();
        json jf = {
            {"id", f->id()},
            {"type", f->type()},
            {"name", f->name()},
            {"visible", f->visible()},
            {"color", {round4(c.r), round4(c.g), round4(c.b), round4(c.a)}},
            {"props", std::move(props)},
        };
        json refs = json::object();
        for (const auto& [key, list] : f->m_subRefs)
            if (!list.empty())
                refs[key] = refsToJson(list);
        if (!refs.empty())
            jf["refs"] = std::move(refs);
        json data;
        f->saveData(data);
        if (!data.is_null())
            jf["data"] = std::move(data);
        features.push_back(std::move(jf));
    }
    return {
        {"app", "CadForge"},
        {"formatVersion", kFormatVersion},
        {"units", "mm"},
        {"nextId", m_nextId},
        {"features", std::move(features)},
    };
}

void Document::loadJson(const json& j)
{
    if (!j.is_object() || !j.contains("features"))
        throw std::runtime_error("not a CadForge document");
    if (j.value("formatVersion", 0) > kFormatVersion)
        throw std::runtime_error("document was written by a newer CadForge version");

    std::vector<std::unique_ptr<Feature>> loaded;
    FeatureId maxId = 0;
    for (const auto& jf : j.at("features")) {
        const std::string type = jf.at("type").get<std::string>();
        auto f = m_registry.create(type);
        if (!f)
            throw std::runtime_error("unknown feature type '" + type + "'");
        f->m_id = jf.at("id").get<FeatureId>();
        f->setName(jf.value("name", std::string()));
        f->setVisible(jf.value("visible", true));
        if (auto c = jf.find("color"); c != jf.end() && c->is_array() && c->size() >= 3)
            f->setColor({(*c)[0].get<float>(), (*c)[1].get<float>(), (*c)[2].get<float>(),
                         c->size() > 3 ? (*c)[3].get<float>() : 1.0f});
        if (auto props = jf.find("props"); props != jf.end())
            for (auto& p : f->props().all())
                if (auto v = props->find(p.key); v != props->end())
                    valueFromJson(*v, p); // unknown keys are ignored (forward compatible)
        if (auto data = jf.find("data"); data != jf.end())
            f->loadData(*data);
        if (auto refs = jf.find("refs"); refs != jf.end() && refs->is_object())
            for (auto it = refs->begin(); it != refs->end(); ++it)
                f->m_subRefs[it.key()] = refsFromJson(it.value());
        maxId = std::max(maxId, f->id());
        loaded.push_back(std::move(f));
    }

    m_features = std::move(loaded);
    m_nextId = std::max<FeatureId>(j.value("nextId", FeatureId{1}), maxId + 1);
    for (auto& f : m_features)
        if (f->name().empty())
            f->setName(uniqueName(f->typeLabel()));
}

void Document::save(const std::string& path) const
{
    std::ofstream out(paths::fromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot open '" + path + "' for writing");
    out << toJson().dump(2) << '\n';
    if (!out)
        throw std::runtime_error("failed to write '" + path + "'");
}

void Document::load(const std::string& path)
{
    std::ifstream in(paths::fromUtf8(path), std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open '" + path + "'");
    json j;
    try {
        in >> j;
    } catch (const json::exception& e) {
        throw std::runtime_error("invalid file '" + path + "': " + e.what());
    }
    try {
        loadJson(j);
    } catch (const json::exception& e) {
        throw std::runtime_error("invalid file '" + path + "': " + e.what());
    }
}

} // namespace cf::model
