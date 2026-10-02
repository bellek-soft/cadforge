#include "sketch/Sketch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cf::sketch {

using json = nlohmann::ordered_json;

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;

const char* const kConstraintNames[] = {
    "Coincident", "PointOnObject", "Horizontal", "Vertical", "Parallel", "Perpendicular",
    "Tangent",    "Equal",         "Symmetric",  "Midpoint", "Fixed",    "Distance",
    "DistanceX",  "DistanceY",     "Radius",     "Diameter", "Angle",
};
static_assert(sizeof(kConstraintNames) / sizeof(kConstraintNames[0]) == size_t(ConstraintType::Count_));

const char* const kGeoNames[] = {"point", "line", "circle", "arc"};

json vec(Vec2 v) { return json::array({v.x, v.y}); }
Vec2 vec(const json& j)
{
    if (!j.is_array() || j.size() != 2)
        throw std::runtime_error("sketch: expected [x, y]");
    return {j[0].get<double>(), j[1].get<double>()};
}

json refJson(const Ref& r) { return json::array({r.geo, int(r.pos)}); }
Ref refFrom(const json& j)
{
    if (!j.is_array() || j.size() != 2)
        throw std::runtime_error("sketch: expected [geometry, position]");
    const int pos = j[1].get<int>();
    if (pos < 0 || pos > 3)
        throw std::runtime_error("sketch: invalid point position");
    return {j[0].get<int>(), PointPos(pos)};
}

void renumber(Ref& r, int removed)
{
    if (r.valid() && r.geo > removed)
        --r.geo;
}
} // namespace

// ---- Geometry -----------------------------------------------------------------

Geometry Geometry::point(Vec2 p)
{
    Geometry g;
    g.type = GeoType::Point;
    g.p1 = p;
    return g;
}

Geometry Geometry::line(Vec2 a, Vec2 b)
{
    Geometry g;
    g.type = GeoType::Line;
    g.p1 = a;
    g.p2 = b;
    return g;
}

Geometry Geometry::circle(Vec2 c, double r)
{
    Geometry g;
    g.type = GeoType::Circle;
    g.center = c;
    g.radius = r;
    return g;
}

Geometry Geometry::arc(Vec2 c, double r, double a0, double a1)
{
    Geometry g;
    g.type = GeoType::Arc;
    g.center = c;
    g.radius = r;
    g.startAngle = a0;
    double sweep = std::fmod(a1 - a0, kTwoPi);
    if (sweep <= 0.0)
        sweep += kTwoPi;
    g.endAngle = a0 + sweep;
    return g;
}

bool Geometry::hasPoint(PointPos pos) const
{
    switch (type) {
    case GeoType::Point: return pos == PointPos::Start;
    case GeoType::Line: return pos == PointPos::Start || pos == PointPos::End;
    case GeoType::Circle: return pos == PointPos::Center;
    case GeoType::Arc: return pos != PointPos::Edge;
    }
    return false;
}

Vec2 Geometry::point(PointPos pos) const
{
    switch (type) {
    case GeoType::Point: return p1;
    case GeoType::Line: return pos == PointPos::End ? p2 : p1;
    case GeoType::Circle: return center;
    case GeoType::Arc:
        if (pos == PointPos::Start)
            return center + radius * Vec2(std::cos(startAngle), std::sin(startAngle));
        if (pos == PointPos::End)
            return center + radius * Vec2(std::cos(endAngle), std::sin(endAngle));
        return center;
    }
    return p1;
}

Vec2 Geometry::closestPoint(Vec2 p) const
{
    switch (type) {
    case GeoType::Point: return p1;
    case GeoType::Line: {
        const Vec2 d = p2 - p1;
        const double l2 = glm::dot(d, d);
        const double t = l2 > 0.0 ? std::clamp(glm::dot(p - p1, d) / l2, 0.0, 1.0) : 0.0;
        return p1 + t * d;
    }
    case GeoType::Circle: {
        const Vec2 d = p - center;
        const double l = glm::length(d);
        return l > 0.0 ? center + d * (radius / l) : center + Vec2(radius, 0.0);
    }
    case GeoType::Arc: {
        double a = std::atan2(p.y - center.y, p.x - center.x);
        double rel = std::fmod(a - startAngle, kTwoPi);
        if (rel < 0.0)
            rel += kTwoPi;
        const double sweep = endAngle - startAngle;
        if (rel > sweep) {
            // Outside the arc: the nearer end point.
            const Vec2 s = point(PointPos::Start), e = point(PointPos::End);
            return glm::length(p - s) < glm::length(p - e) ? s : e;
        }
        a = startAngle + rel;
        return center + radius * Vec2(std::cos(a), std::sin(a));
    }
    }
    return p1;
}

// ---- Constraint -------------------------------------------------------------------

bool Constraint::isDimension() const
{
    switch (type) {
    case ConstraintType::Distance:
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
    case ConstraintType::Angle:
        return true;
    default:
        return false;
    }
}

bool Constraint::references(int geo) const
{
    return a.geo == geo || b.geo == geo || c.geo == geo;
}

const char* constraintName(ConstraintType t)
{
    const int i = int(t);
    return i >= 0 && i < int(ConstraintType::Count_) ? kConstraintNames[i] : "?";
}

// ---- Sketch -------------------------------------------------------------------------

int Sketch::add(const Geometry& g)
{
    geometry.push_back(g);
    return int(geometry.size()) - 1;
}

int Sketch::addConstraint(const Constraint& c)
{
    constraints.push_back(c);
    return int(constraints.size()) - 1;
}

void Sketch::removeGeometry(int index)
{
    if (index < 0 || index >= int(geometry.size()))
        return;
    constraints.erase(std::remove_if(constraints.begin(), constraints.end(),
                                     [&](const Constraint& c) { return c.references(index); }),
                      constraints.end());
    for (auto& c : constraints) {
        renumber(c.a, index);
        renumber(c.b, index);
        renumber(c.c, index);
    }
    geometry.erase(geometry.begin() + index);
}

void Sketch::removeConstraint(int index)
{
    if (index >= 0 && index < int(constraints.size()))
        constraints.erase(constraints.begin() + index);
}

bool Sketch::validRef(const Ref& r) const
{
    if (r.geo == kOrigin)
        return r.pos == PointPos::Start || r.pos == PointPos::Center;
    if (r.geo == kHAxis || r.geo == kVAxis)
        return r.pos == PointPos::Edge;
    if (r.geo < 0 || r.geo >= int(geometry.size()))
        return false;
    return r.pos == PointPos::Edge || geometry[size_t(r.geo)].hasPoint(r.pos);
}

GeoType Sketch::typeOf(int geo) const
{
    if (geo == kOrigin)
        return GeoType::Point;
    if (geo == kHAxis || geo == kVAxis)
        return GeoType::Line;
    return geometry.at(size_t(geo)).type;
}

Vec2 Sketch::pointAt(const Ref& r) const
{
    if (r.geo < 0)
        return Vec2(0.0);
    return geometry.at(size_t(r.geo)).point(r.pos);
}

std::vector<int> Sketch::profileGeometry() const
{
    std::vector<int> out;
    for (int i = 0; i < int(geometry.size()); ++i)
        if (!geometry[size_t(i)].construction && geometry[size_t(i)].type != GeoType::Point)
            out.push_back(i);
    return out;
}

std::vector<int> Sketch::constructionLines() const
{
    std::vector<int> out;
    for (int i = 0; i < int(geometry.size()); ++i)
        if (geometry[size_t(i)].construction && geometry[size_t(i)].type == GeoType::Line)
            out.push_back(i);
    return out;
}

json Sketch::toJson() const
{
    json geos = json::array();
    for (const auto& g : geometry) {
        json jg = {{"type", kGeoNames[int(g.type)]}};
        switch (g.type) {
        case GeoType::Point: jg["p"] = vec(g.p1); break;
        case GeoType::Line:
            jg["p1"] = vec(g.p1);
            jg["p2"] = vec(g.p2);
            break;
        case GeoType::Circle:
            jg["center"] = vec(g.center);
            jg["radius"] = g.radius;
            break;
        case GeoType::Arc:
            jg["center"] = vec(g.center);
            jg["radius"] = g.radius;
            jg["startAngle"] = g.startAngle;
            jg["endAngle"] = g.endAngle;
            break;
        }
        if (g.construction)
            jg["construction"] = true;
        geos.push_back(std::move(jg));
    }
    json cons = json::array();
    for (const auto& c : constraints) {
        json jc = {{"type", constraintName(c.type)}, {"a", refJson(c.a)}};
        if (c.b.valid())
            jc["b"] = refJson(c.b);
        if (c.c.valid())
            jc["c"] = refJson(c.c);
        if (c.isDimension() || c.type == ConstraintType::Fixed)
            jc["value"] = c.value;
        if (c.type == ConstraintType::Fixed)
            jc["value2"] = c.value2;
        cons.push_back(std::move(jc));
    }
    return {{"geometry", std::move(geos)}, {"constraints", std::move(cons)}};
}

Sketch Sketch::fromJson(const json& j)
{
    Sketch s;
    if (!j.is_object())
        return s;
    if (auto it = j.find("geometry"); it != j.end())
        for (const auto& jg : *it) {
            const std::string type = jg.at("type").get<std::string>();
            Geometry g;
            if (type == "point")
                g = Geometry::point(vec(jg.at("p")));
            else if (type == "line")
                g = Geometry::line(vec(jg.at("p1")), vec(jg.at("p2")));
            else if (type == "circle")
                g = Geometry::circle(vec(jg.at("center")), jg.at("radius").get<double>());
            else if (type == "arc")
                g = Geometry::arc(vec(jg.at("center")), jg.at("radius").get<double>(),
                                  jg.at("startAngle").get<double>(), jg.at("endAngle").get<double>());
            else
                throw std::runtime_error("sketch: unknown geometry type '" + type + "'");
            g.construction = jg.value("construction", false);
            s.geometry.push_back(g);
        }
    if (auto it = j.find("constraints"); it != j.end())
        for (const auto& jc : *it) {
            const std::string type = jc.at("type").get<std::string>();
            Constraint c;
            bool found = false;
            for (int i = 0; i < int(ConstraintType::Count_); ++i)
                if (type == kConstraintNames[i]) {
                    c.type = ConstraintType(i);
                    found = true;
                }
            if (!found)
                throw std::runtime_error("sketch: unknown constraint type '" + type + "'");
            c.a = refFrom(jc.at("a"));
            if (auto b = jc.find("b"); b != jc.end())
                c.b = refFrom(*b);
            if (auto cc = jc.find("c"); cc != jc.end())
                c.c = refFrom(*cc);
            c.value = jc.value("value", 0.0);
            c.value2 = jc.value("value2", 0.0);
            s.constraints.push_back(c);
        }
    return s;
}

bool Sketch::operator==(const Sketch& other) const
{
    return toJson() == other.toJson();
}

} // namespace cf::sketch
