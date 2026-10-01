#pragma once
// The parametric document: an ordered set of features forming a DAG through
// their FeatureRef properties.
//
//  * recompute() evaluates features in dependency order. Results are cached by
//    a content hash of (type, parameters, input results), so undo/redo, file
//    reloads and placement-only edits are cheap.
//  * The JSON form (toJson / loadJson) is the file format AND the undo
//    snapshot format; geometry is always regenerated from parameters.

#include "model/Feature.h"
#include "model/FeatureRegistry.h"

#include <nlohmann/json_fwd.hpp>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cf::model {

struct RecomputeStats {
    int executed = 0;  // features actually executed (cache misses)
    int failed = 0;
    double milliseconds = 0.0;
};

class Document {
public:
    explicit Document(const FeatureRegistry& registry = builtinRegistry());
    ~Document();

    const FeatureRegistry& registry() const { return m_registry; }

    // --- structure ---
    /// Creates a feature of `type` (e.g. "Part::Box"), assigns id + unique name.
    Feature* create(std::string_view type);
    Feature* add(std::unique_ptr<Feature> feature);
    /// Removes `id` and every feature that (transitively) depends on it.
    /// Returns the removed ids.
    std::vector<FeatureId> remove(FeatureId id);
    void clear();

    Feature* find(FeatureId id);
    const Feature* find(FeatureId id) const;
    const std::vector<std::unique_ptr<Feature>>& features() const { return m_features; }

    // --- dependency queries ---
    /// Features that directly reference `id`.
    std::vector<FeatureId> consumers(FeatureId id) const;
    /// True if a consuming feature (e.g. a boolean) uses `id` as input.
    bool isConsumed(FeatureId id) const;
    /// Features not consumed by anything (the top level of the tree).
    std::vector<FeatureId> roots() const;
    /// All features that transitively depend on `id`.
    std::vector<FeatureId> downstream(FeatureId id) const;
    /// Whether `from` may reference `candidate` without creating a cycle.
    bool canReference(FeatureId from, FeatureId candidate) const;

    // --- evaluation ---
    RecomputeStats recompute();

    // --- persistence ---
    nlohmann::ordered_json toJson() const;
    /// Replaces the content. Cached results of unchanged features are reused.
    /// Throws std::runtime_error on malformed input.
    void loadJson(const nlohmann::ordered_json& j);
    void save(const std::string& path) const;
    void load(const std::string& path);

    std::string uniqueName(std::string_view base) const;

private:
    std::vector<Feature*> topologicalOrder(std::vector<Feature*>& cyclic) const;
    void cacheStore(std::uint64_t key, const geom::Shape& s);
    const geom::Shape* cacheFind(std::uint64_t key) const;

    const FeatureRegistry& m_registry;
    std::vector<std::unique_ptr<Feature>> m_features;
    FeatureId m_nextId = 1;

    // Bounded LRU-ish cache of local (pre-placement) results keyed by content hash.
    std::unordered_map<std::uint64_t, geom::Shape> m_cache;
    std::list<std::uint64_t> m_cacheOrder;
    static constexpr std::size_t kCacheCapacity = 256;
};

} // namespace cf::model
