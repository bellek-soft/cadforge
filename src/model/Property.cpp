#include "model/Property.h"

#include <algorithm>

namespace cf::model {

Property& PropertySet::add(Property p)
{
    m_props.push_back(std::move(p));
    return m_props.back();
}

Property* PropertySet::find(std::string_view key)
{
    for (auto& p : m_props)
        if (p.key == key)
            return &p;
    return nullptr;
}

const Property* PropertySet::find(std::string_view key) const
{
    for (const auto& p : m_props)
        if (p.key == key)
            return &p;
    return nullptr;
}

bool PropertySet::set(std::string_view key, const PropertyValue& v)
{
    Property* p = find(key);
    if (!p || p->value.index() != v.index())
        return false;

    PropertyValue nv = v;
    if (auto* d = std::get_if<double>(&nv))
        *d = std::clamp(*d, p->minValue, p->maxValue);
    else if (auto* i = std::get_if<int>(&nv)) {
        if (p->type == PropertyType::Enum)
            *i = std::clamp(*i, 0, std::max(0, static_cast<int>(p->enumItems.size()) - 1));
        else
            *i = std::clamp(*i, static_cast<int>(p->minValue), static_cast<int>(p->maxValue));
    }

    if (nv == p->value)
        return false;
    p->value = std::move(nv);
    return true;
}

Property makeDouble(std::string key, std::string label, double value, double minV, double maxV,
                    std::string unit, std::string group)
{
    Property p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.group = std::move(group);
    p.type = PropertyType::Double;
    p.value = value;
    p.minValue = minV;
    p.maxValue = maxV;
    p.unit = std::move(unit);
    return p;
}

Property makeInt(std::string key, std::string label, int value, int minV, int maxV, std::string group)
{
    Property p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.group = std::move(group);
    p.type = PropertyType::Int;
    p.value = value;
    p.minValue = minV;
    p.maxValue = maxV;
    p.speed = 1.0;
    return p;
}

Property makeBool(std::string key, std::string label, bool value, std::string group)
{
    Property p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.group = std::move(group);
    p.type = PropertyType::Bool;
    p.value = value;
    return p;
}

Property makeVec3(std::string key, std::string label, Vec3 value, std::string unit, std::string group)
{
    Property p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.group = std::move(group);
    p.type = PropertyType::Vec3;
    p.value = value;
    p.unit = std::move(unit);
    return p;
}

Property makeEnum(std::string key, std::string label, int value, std::vector<std::string> items,
                  std::string group)
{
    Property p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.group = std::move(group);
    p.type = PropertyType::Enum;
    p.value = value;
    p.enumItems = std::move(items);
    return p;
}

Property makeString(std::string key, std::string label, std::string value, std::string group)
{
    Property p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.group = std::move(group);
    p.type = PropertyType::String;
    p.value = std::move(value);
    return p;
}

Property makeFilePath(std::string key, std::string label, std::string value, std::string group)
{
    Property p = makeString(std::move(key), std::move(label), std::move(value), std::move(group));
    p.type = PropertyType::FilePath;
    return p;
}

Property makeRef(std::string key, std::string label, std::string group)
{
    Property p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.group = std::move(group);
    p.type = PropertyType::FeatureRef;
    p.value = kNoFeature;
    return p;
}

Property makeRefList(std::string key, std::string label, std::string group)
{
    Property p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.group = std::move(group);
    p.type = PropertyType::FeatureRefList;
    p.value = std::vector<FeatureId>{};
    return p;
}

Property makeIndexList(std::string key, std::string label, std::string group)
{
    Property p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.group = std::move(group);
    p.type = PropertyType::IndexList;
    p.value = std::vector<int>{};
    return p;
}

} // namespace cf::model
