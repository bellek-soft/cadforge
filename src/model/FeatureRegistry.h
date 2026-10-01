#pragma once
// Factory for feature types. Modules register their features explicitly
// (registerPartFeatures, later registerCsgFeatures / registerFeaFeatures ...),
// which avoids static-initialization pitfalls with static libraries.

#include "model/Feature.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cf::model {

struct FeatureTypeInfo {
    std::string type;      // "Part::Box"
    std::string label;     // "Box"
    std::string category;  // "Primitives", "Boolean", "Dress-up", "Import"
    std::function<std::unique_ptr<Feature>()> create;
};

class FeatureRegistry {
public:
    void add(FeatureTypeInfo info);
    const FeatureTypeInfo* find(std::string_view type) const;
    std::unique_ptr<Feature> create(std::string_view type) const;
    const std::vector<FeatureTypeInfo>& types() const { return m_types; }

private:
    std::vector<FeatureTypeInfo> m_types;
};

/// Registry with every built-in feature type.
const FeatureRegistry& builtinRegistry();

} // namespace cf::model
