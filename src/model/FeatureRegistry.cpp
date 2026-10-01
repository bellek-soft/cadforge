#include "model/FeatureRegistry.h"
#include "model/features/PartFeatures.h"

namespace cf::model {

void FeatureRegistry::add(FeatureTypeInfo info)
{
    for (auto& t : m_types)
        if (t.type == info.type) {
            t = std::move(info);
            return;
        }
    m_types.push_back(std::move(info));
}

const FeatureTypeInfo* FeatureRegistry::find(std::string_view type) const
{
    for (const auto& t : m_types)
        if (t.type == type)
            return &t;
    return nullptr;
}

std::unique_ptr<Feature> FeatureRegistry::create(std::string_view type) const
{
    const FeatureTypeInfo* info = find(type);
    return info ? info->create() : nullptr;
}

const FeatureRegistry& builtinRegistry()
{
    static const FeatureRegistry reg = [] {
        FeatureRegistry r;
        registerPartFeatures(r);
        // Future: registerSketchFeatures(r); registerFeaFeatures(r); ...
        return r;
    }();
    return reg;
}

} // namespace cf::model
