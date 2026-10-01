#pragma once
// CAD-style turntable camera, Z-up. Supports perspective and orthographic
// projection, zoom-to-cursor, panning, fit-to-bounds and animated transitions.

#include "core/Types.h"

namespace cf::render {

enum class StandardView { Front, Back, Left, Right, Top, Bottom, Isometric };

struct Ray {
    Vec3 origin;
    Vec3 dir;
};

class Camera {
public:
    struct State {
        Vec3 target{0.0};
        double distance = 120.0;
        double yawDeg = -55.0;   // rotation around Z, 0 = looking from +X
        double pitchDeg = 28.0;  // elevation above the XY plane
    };

    void setViewport(int widthPx, int heightPx);
    int width() const { return m_width; }
    int height() const { return m_height; }

    void setOrthographic(bool o) { m_ortho = o; }
    bool orthographic() const { return m_ortho; }
    double fovDeg() const { return m_fovDeg; }

    /// Scene bounds are used to choose near/far planes.
    void setSceneBounds(const BoundingBox& bb) { m_scene = bb; }

    const State& state() const { return m_state; }
    void setState(const State& s) { m_state = s; m_animating = false; }

    Vec3 eye() const;
    Vec3 forward() const;
    Vec3 up() const;
    Vec3 right() const;
    Mat4 view() const;
    Mat4 projection() const;

    /// World-space height of the view at the target distance.
    double viewHeight() const;

    // --- interaction (pixel deltas) ---
    void orbit(double dxPx, double dyPx);
    void pan(double dxPx, double dyPx);
    /// steps > 0 zooms in; the point under the cursor stays fixed.
    void zoom(double steps, double cursorX, double cursorY);

    void fit(const BoundingBox& bb, bool animate = true);
    void setStandardView(StandardView v, bool animate = true);

    /// Advances transition animations. Returns true while animating.
    bool update(double dtSeconds);

    Ray rayThrough(double px, double py) const;
    /// Projects world point to pixel coordinates (origin top-left). z = NDC depth.
    Vec3 project(const Vec3& world) const;

private:
    void animateTo(const State& s);

    State m_state;
    State m_from, m_to;
    double m_animT = 1.0;
    bool m_animating = false;

    int m_width = 1, m_height = 1;
    bool m_ortho = false;
    double m_fovDeg = 35.0;
    BoundingBox m_scene;
};

} // namespace cf::render
