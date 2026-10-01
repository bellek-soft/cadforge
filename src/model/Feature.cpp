#include "model/Feature.h"
#include "model/Document.h"

#include <algorithm>

namespace cf::model {

namespace {
// A pleasant default palette so new parts are distinguishable.
Color nextDefaultColor()
{
    static const Color palette[] = {
        {0.62f, 0.70f, 0.80f, 1.f}, {0.80f, 0.68f, 0.52f, 1.f}, {0.60f, 0.76f, 0.62f, 1.f},
        {0.78f, 0.60f, 0.62f, 1.f}, {0.70f, 0.66f, 0.82f, 1.f}, {0.82f, 0.78f, 0.55f, 1.f},
    };
    static unsigned counter = 0;
    return palette[counter++ % (sizeof(palette) / sizeof(palette[0]))];
}
} // namespace

geom::Shape ExecContext::input(FeatureId id) const
{
    const Feature* f = m_doc.find(id);
    if (!f)
        throw geom::GeomError("Input feature #" + std::to_string(id) + " does not exist");
    if (f->state() != FeatureState::Ok || f->shape().isNull())
        throw geom::GeomError("Input '" + f->name() + "' has no valid result");
    return f->shape();
}

Feature::Feature()
    : m_color(nextDefaultColor())
{
    m_props.add(makeVec3(kPosition, "Position", Vec3(0.0), "mm", "Placement"));
    Property& rot = m_props.add(makeVec3(kRotation, "Rotation", Vec3(0.0), "deg", "Placement"));
    rot.speed = 0.5;
    rot.tooltip = "XYZ Euler angles in degrees";
}

Placement Feature::placement() const
{
    Placement p;
    p.position = m_props.get<Vec3>(kPosition);
    p.rotationDeg = m_props.get<Vec3>(kRotation);
    return p;
}

bool Feature::setPlacement(const Placement& p)
{
    bool changed = m_props.set(kPosition, p.position);
    changed |= m_props.set(kRotation, p.rotationDeg);
    return changed;
}

std::vector<FeatureId> Feature::inputs() const
{
    std::vector<FeatureId> out;
    for (const auto& p : m_props.all()) {
        if (p.type == PropertyType::FeatureRef) {
            FeatureId id = std::get<FeatureId>(p.value);
            if (id != kNoFeature)
                out.push_back(id);
        } else if (p.type == PropertyType::FeatureRefList) {
            for (FeatureId id : std::get<std::vector<FeatureId>>(p.value))
                if (id != kNoFeature)
                    out.push_back(id);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace cf::model
