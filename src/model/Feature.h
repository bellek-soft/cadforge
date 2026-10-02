#pragma once
// Base class of every node in the parametric feature tree.
//
// A feature owns typed parameters (PropertySet), may reference other features
// as inputs (FeatureRef / FeatureRefList properties), and produces a result
// shape in execute(). The Document drives recomputation in dependency order
// and caches results, so execute() is a pure function of parameters + inputs.
//
// Extension points for upcoming modules:
//  * CSG:  new boolean-like features just reference inputs and combine them.
//  * FEA:  analysis features will reference a solid and produce a mesh/result
//          payload (see FeatureResult below - it will grow a variant).

#include "core/Placement.h"
#include "core/Types.h"
#include "geom/Naming.h"
#include "geom/Shape.h"
#include "model/Property.h"

#include <nlohmann/json_fwd.hpp>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cf::model {

class Document;

/// Read-only access for execute(): fetch already-computed input shapes.
class ExecContext {
public:
    explicit ExecContext(const Document& doc) : m_doc(doc) {}
    /// Result shape of `id` (placement applied). Throws geom::GeomError when
    /// the input is missing or failed.
    geom::Shape input(FeatureId id) const;
    const Document& document() const { return m_doc; }

private:
    const Document& m_doc;
};

enum class FeatureState { Pending, Ok, Error };

/// A remembered face / edge reference: the index it had and what it looked like
/// (used to re-resolve IndexList properties after upstream changes).
struct SubShapeRef {
    int index = 0;
    geom::SubShapeSignature sig;
};

class Feature {
public:
    Feature();
    virtual ~Feature() = default;
    Feature(const Feature&) = delete;
    Feature& operator=(const Feature&) = delete;

    /// Stable type id used in files, e.g. "Part::Box".
    virtual std::string_view type() const = 0;
    /// Human readable type name, also used as default instance name.
    virtual std::string_view typeLabel() const = 0;
    /// Builds the result in the feature's local frame (placement is applied by the Document).
    virtual geom::Shape execute(const ExecContext& ctx) = 0;
    /// Extra data that should invalidate the cached result (e.g. a file timestamp).
    virtual std::string cacheSalt() const { return {}; }
    /// CSG semantics: inputs of this feature are "consumed" (hidden, shown as children).
    virtual bool consumesInputs() const { return true; }

    /// False for features that carry data but no geometry (analysis studies,
    /// loads, constraints ...). For those execute() is never called; validate()
    /// is used instead to report problems.
    virtual bool producesGeometry() const { return true; }
    /// Checks a non-geometric feature. Throw (e.g. geom::GeomError) to report an error.
    virtual void validate(const ExecContext& ctx) { (void)ctx; }
    /// Show this feature as a child of its (first) input in the model tree,
    /// e.g. loads under their analysis.
    virtual bool nestUnderInput() const { return false; }
    /// The feature whose faces/edges an IndexList property of this feature refers to.
    virtual FeatureId subShapeTarget(const Document& doc) const { (void)doc; return kNoFeature; }
    /// Called by editors after the user changed property `key` (e.g. to apply a
    /// material preset). Returns true if other properties were modified.
    virtual bool onPropertyEdited(std::string_view key) { (void)key; return false; }
    /// Feature-specific data that does not fit into properties (e.g. sketch
    /// geometry). Stored under "data" in files and undo snapshots; leave `out`
    /// null when there is nothing to store. Remember to include it in cacheSalt().
    virtual void saveData(nlohmann::ordered_json& out) const { (void)out; }
    virtual void loadData(const nlohmann::ordered_json& in) { (void)in; }

    // --- identity & appearance ---
    FeatureId id() const { return m_id; }
    const std::string& name() const { return m_name; }
    void setName(std::string n) { m_name = std::move(n); }
    bool visible() const { return m_visible; }
    void setVisible(bool v) { m_visible = v; }
    const Color& color() const { return m_color; }
    void setColor(const Color& c) { m_color = c; }

    // --- parameters ---
    PropertySet& props() { return m_props; }
    const PropertySet& props() const { return m_props; }
    Placement placement() const;
    /// Returns true if it changed.
    bool setPlacement(const Placement& p);
    /// All feature ids referenced by FeatureRef / FeatureRefList properties.
    std::vector<FeatureId> inputs() const;

    // --- results (maintained by Document::recompute) ---
    const geom::Shape& shape() const { return m_shape; }
    FeatureState state() const { return m_state; }
    const std::string& error() const { return m_error; }
    /// Changes whenever the result shape changes (used by render caches).
    std::uint64_t resultKey() const { return m_resultKey; }

    /// Remembered sub-shape references of IndexList property `key` (topological naming).
    const std::vector<SubShapeRef>* subShapeRefs(const std::string& key) const
    {
        auto it = m_subRefs.find(key);
        return it == m_subRefs.end() ? nullptr : &it->second;
    }

    static constexpr const char* kPosition = "position";
    static constexpr const char* kRotation = "rotation";

private:
    friend class Document;

    FeatureId m_id = kNoFeature;
    std::string m_name;
    bool m_visible = true;
    Color m_color;
    PropertySet m_props;

    geom::Shape m_localShape;
    geom::Shape m_shape;
    std::uint64_t m_localKey = 0;
    std::uint64_t m_resultKey = 0;
    FeatureState m_state = FeatureState::Pending;
    std::string m_error;

    std::map<std::string, std::vector<SubShapeRef>> m_subRefs; // IndexList key -> references
    std::uint64_t m_refsTargetKey = 0; // target result the references were last resolved against
};

} // namespace cf::model
