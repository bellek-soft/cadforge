#include "render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace cf::render {

namespace {
constexpr double kOrbitDegPerPx = 0.35;

Vec3 dirFromAngles(double yawDeg, double pitchDeg)
{
    const double y = glm::radians(yawDeg), p = glm::radians(pitchDeg);
    return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
}

double wrapDeg(double a)
{
    a = std::fmod(a + 180.0, 360.0);
    if (a < 0)
        a += 360.0;
    return a - 180.0;
}
} // namespace

void Camera::setViewport(int w, int h)
{
    m_width = std::max(1, w);
    m_height = std::max(1, h);
}

Vec3 Camera::forward() const
{
    return -dirFromAngles(m_state.yawDeg, m_state.pitchDeg);
}

Vec3 Camera::up() const
{
    const double y = glm::radians(m_state.yawDeg), p = glm::radians(m_state.pitchDeg);
    return {-std::sin(p) * std::cos(y), -std::sin(p) * std::sin(y), std::cos(p)};
}

Vec3 Camera::right() const
{
    return glm::normalize(glm::cross(forward(), up()));
}

Vec3 Camera::eye() const
{
    return m_state.target - forward() * m_state.distance;
}

Mat4 Camera::view() const
{
    return glm::lookAt(eye(), m_state.target, up());
}

double Camera::viewHeight() const
{
    return 2.0 * m_state.distance * std::tan(glm::radians(m_fovDeg) * 0.5);
}

Mat4 Camera::projection() const
{
    const double aspect = double(m_width) / double(m_height);
    double radius = m_state.distance;
    if (m_scene.valid())
        radius = std::max(1.0, m_scene.diagonal() * 0.5 + glm::length(m_scene.center() - m_state.target));
    const double farPlane = m_state.distance + 2.0 * radius + 1.0;

    if (m_ortho) {
        const double h = viewHeight() * 0.5, w = h * aspect;
        return glm::ortho(-w, w, -h, h, -farPlane, farPlane);
    }
    const double nearPlane = std::max(farPlane * 2e-5, m_state.distance - 1.5 * radius);
    return glm::perspective(glm::radians(m_fovDeg), aspect, nearPlane, farPlane);
}

void Camera::orbit(double dx, double dy)
{
    m_animating = false;
    m_state.yawDeg = wrapDeg(m_state.yawDeg - dx * kOrbitDegPerPx);
    m_state.pitchDeg = std::clamp(m_state.pitchDeg + dy * kOrbitDegPerPx, -90.0, 90.0);
}

void Camera::pan(double dx, double dy)
{
    m_animating = false;
    const double unitsPerPx = viewHeight() / double(m_height);
    m_state.target += (-right() * dx + up() * dy) * unitsPerPx;
}

void Camera::zoom(double steps, double cx, double cy)
{
    m_animating = false;
    auto onFocusPlane = [&](double px, double py) {
        const Ray r = rayThrough(px, py);
        const Vec3 f = forward();
        const double denom = glm::dot(r.dir, f);
        if (std::abs(denom) < 1e-12)
            return m_state.target;
        const double t = glm::dot(m_state.target - r.origin, f) / denom;
        return r.origin + r.dir * t;
    };
    const Vec3 before = onFocusPlane(cx, cy);
    m_state.distance = std::clamp(m_state.distance * std::pow(0.85, steps), 1e-3, 1e7);
    const Vec3 after = onFocusPlane(cx, cy);
    m_state.target += before - after;
}

void Camera::fit(const BoundingBox& bb, bool animate)
{
    State s = m_animating ? m_to : m_state;
    if (bb.valid()) {
        const double radius = std::max(bb.diagonal() * 0.5, 1.0);
        s.target = bb.center();
        s.distance = radius / std::sin(glm::radians(m_fovDeg) * 0.5) * 1.1;
    } else {
        s.target = Vec3(0.0);
        s.distance = 120.0;
    }
    animate ? animateTo(s) : setState(s);
}

void Camera::setStandardView(StandardView v, bool animate)
{
    State s = m_animating ? m_to : m_state;
    viewAngles(v, s.yawDeg, s.pitchDeg);
    animate ? animateTo(s) : setState(s);
}

void Camera::viewFrom(StandardView v, const Vec3& target, double distance, bool animate)
{
    State s;
    viewAngles(v, s.yawDeg, s.pitchDeg);
    s.target = target;
    s.distance = std::clamp(distance, 1e-3, 1e7);
    animate ? animateTo(s) : setState(s);
}

void Camera::viewAngles(StandardView v, double& yawDeg, double& pitchDeg)
{
    switch (v) {
    case StandardView::Front:     yawDeg = -90; pitchDeg = 0; break;
    case StandardView::Back:      yawDeg = 90;  pitchDeg = 0; break;
    case StandardView::Right:     yawDeg = 0;   pitchDeg = 0; break;
    case StandardView::Left:      yawDeg = 180; pitchDeg = 0; break;
    case StandardView::Top:       yawDeg = -90; pitchDeg = 90; break;
    case StandardView::Bottom:    yawDeg = -90; pitchDeg = -90; break;
    case StandardView::Isometric: yawDeg = -45; pitchDeg = 35.264; break;
    }
}

void Camera::animateTo(const State& s)
{
    m_from = m_state;
    m_to = s;
    // Rotate the short way around.
    m_to.yawDeg = m_from.yawDeg + wrapDeg(m_to.yawDeg - m_from.yawDeg);
    m_animT = 0.0;
    m_animating = true;
}

bool Camera::update(double dt)
{
    if (!m_animating)
        return false;
    m_animT = std::min(1.0, m_animT + dt / 0.35);
    const double t = m_animT * m_animT * (3.0 - 2.0 * m_animT); // smoothstep
    m_state.target = glm::mix(m_from.target, m_to.target, t);
    m_state.distance = std::exp(glm::mix(std::log(m_from.distance), std::log(m_to.distance), t));
    m_state.yawDeg = glm::mix(m_from.yawDeg, m_to.yawDeg, t);
    m_state.pitchDeg = glm::mix(m_from.pitchDeg, m_to.pitchDeg, t);
    if (m_animT >= 1.0) {
        m_state = m_to;
        m_state.yawDeg = wrapDeg(m_state.yawDeg);
        m_animating = false;
    }
    return true;
}

Ray Camera::rayThrough(double px, double py) const
{
    const double x = 2.0 * px / m_width - 1.0;
    const double y = 1.0 - 2.0 * py / m_height;
    const double aspect = double(m_width) / double(m_height);
    const double halfH = std::tan(glm::radians(m_fovDeg) * 0.5);
    const Vec3 f = forward(), u = up(), r = right();
    if (m_ortho) {
        const double h = viewHeight() * 0.5;
        return {eye() + r * (x * h * aspect) + u * (y * h), f};
    }
    return {eye(), glm::normalize(f + r * (x * halfH * aspect) + u * (y * halfH))};
}

Vec3 Camera::project(const Vec3& w) const
{
    const glm::dvec4 clip = projection() * view() * glm::dvec4(w, 1.0);
    if (std::abs(clip.w) < 1e-12)
        return Vec3(-1e9);
    const Vec3 ndc = Vec3(clip) / clip.w;
    return {(ndc.x + 1.0) * 0.5 * m_width, (1.0 - ndc.y) * 0.5 * m_height, ndc.z};
}

} // namespace cf::render
