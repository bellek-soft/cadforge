#pragma once
// Typed, self-describing feature parameters.
//
// Features declare their parameters as Properties. Everything generic
// (property editor UI, JSON serialization, change detection, dependency
// discovery) works from these descriptions, so adding a new feature type
// never requires touching the UI or the file format.

#include "core/Types.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cf::model {

enum class PropertyType {
    Double,
    Int,
    Bool,
    Vec3,
    Enum,           // stored as int, labels in Property::enumItems
    String,
    FilePath,       // stored as std::string
    FeatureRef,     // stored as FeatureId (single input)
    FeatureRefList, // stored as std::vector<FeatureId>
    IndexList,      // stored as std::vector<int> (e.g. 1-based edge indices)
};

using PropertyValue = std::variant<double, int, bool, Vec3, std::string, FeatureId,
                                   std::vector<FeatureId>, std::vector<int>>;

struct Property {
    std::string key;     // stable identifier used in files
    std::string label;   // UI label
    std::string group;   // UI section ("Dimensions", "Placement", ...)
    PropertyType type = PropertyType::Double;
    PropertyValue value;

    // UI / validation hints
    double minValue = -1e12;
    double maxValue = 1e12;
    double speed = 0.1;              // drag speed in the UI
    std::string unit;                // "mm", "deg", ...
    std::vector<std::string> enumItems;
    std::string tooltip;
    bool hidden = false;             // not shown in the generic editor
};

class PropertySet {
public:
    Property& add(Property p);

    Property* find(std::string_view key);
    const Property* find(std::string_view key) const;

    template <typename T> const T& get(std::string_view key) const
    {
        const Property* p = find(key);
        return std::get<T>(p->value);
    }

    /// Sets a value (with clamping for numeric types). Returns true if it changed.
    bool set(std::string_view key, const PropertyValue& v);

    std::vector<Property>& all() { return m_props; }
    const std::vector<Property>& all() const { return m_props; }

private:
    std::vector<Property> m_props;
};

// Convenience builders used by feature constructors.
Property makeDouble(std::string key, std::string label, double value, double minV, double maxV,
                    std::string unit = "mm", std::string group = "Parameters");
Property makeInt(std::string key, std::string label, int value, int minV, int maxV,
                 std::string group = "Parameters");
Property makeBool(std::string key, std::string label, bool value, std::string group = "Parameters");
Property makeVec3(std::string key, std::string label, Vec3 value, std::string unit, std::string group);
Property makeEnum(std::string key, std::string label, int value, std::vector<std::string> items,
                  std::string group = "Parameters");
Property makeString(std::string key, std::string label, std::string value, std::string group = "Parameters");
Property makeFilePath(std::string key, std::string label, std::string value, std::string group = "Parameters");
Property makeRef(std::string key, std::string label, std::string group = "Inputs");
Property makeRefList(std::string key, std::string label, std::string group = "Inputs");
Property makeIndexList(std::string key, std::string label, std::string group = "Parameters");

} // namespace cf::model
