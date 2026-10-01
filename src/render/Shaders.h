#pragma once
// GLSL 4.10 core sources (the highest version macOS supports).

namespace cf::render::shaders {

// ---------------------------------------------------------------------------
// Background gradient (full-screen triangle generated from gl_VertexID)
// ---------------------------------------------------------------------------
inline const char* kBackgroundVS = R"(#version 410 core
out vec2 vUv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.999, 1.0);
}
)";

inline const char* kBackgroundFS = R"(#version 410 core
in vec2 vUv;
uniform vec3 uTop;
uniform vec3 uBottom;
out vec4 fragColor;
void main() {
    float t = smoothstep(0.0, 1.0, vUv.y);
    fragColor = vec4(mix(uBottom, uTop, t), 1.0);
}
)";

// ---------------------------------------------------------------------------
// Shaded faces
// ---------------------------------------------------------------------------
inline const char* kMeshVS = R"(#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in uint aId;
layout(location = 3) in float aScalar;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vPosVS;
out vec3 vNormalVS;
out float vScalar;
flat out uint vId;
void main() {
    vec4 p = uView * uModel * vec4(aPos, 1.0);
    vPosVS = p.xyz;
    vNormalVS = mat3(uView * uModel) * aNormal;
    vScalar = aScalar;
    vId = aId;
    gl_Position = uProj * p;
}
)";

inline const char* kMeshFS = R"(#version 410 core
in vec3 vPosVS;
in vec3 vNormalVS;
in float vScalar;
flat in uint vId;
uniform vec4 uColor;
uniform uint uHoverId;
uniform vec4 uHoverColor;
uniform int uOrtho;
uniform int uUseScalars;
uniform vec2 uScalarRange;
out vec4 fragColor;

// Compact "turbo"-like colormap for analysis results (FEA, curvature ...).
vec3 colormap(float t) {
    const vec3 c0 = vec3(0.19, 0.07, 0.23), c1 = vec3(0.16, 0.47, 0.93),
               c2 = vec3(0.11, 0.81, 0.62), c3 = vec3(0.64, 0.98, 0.24),
               c4 = vec3(0.98, 0.73, 0.18), c5 = vec3(0.80, 0.18, 0.04);
    t = clamp(t, 0.0, 1.0) * 5.0;
    if (t < 1.0) return mix(c0, c1, t);
    if (t < 2.0) return mix(c1, c2, t - 1.0);
    if (t < 3.0) return mix(c2, c3, t - 2.0);
    if (t < 4.0) return mix(c3, c4, t - 3.0);
    return mix(c4, c5, t - 4.0);
}

void main() {
    vec3 n = normalize(vNormalVS);
    if (!gl_FrontFacing) n = -n;
    vec3 v = (uOrtho == 1) ? vec3(0.0, 0.0, 1.0) : normalize(-vPosVS);

    vec3 base = uColor.rgb;
    if (uUseScalars == 1) {
        float span = max(uScalarRange.y - uScalarRange.x, 1e-20);
        base = colormap((vScalar - uScalarRange.x) / span);
    }
    if (vId == uHoverId) base = mix(base, uHoverColor.rgb, uHoverColor.a);

    // Camera-attached key + fill lights, hemispheric ambient, soft specular, rim.
    vec3 L1 = normalize(vec3(-0.40, 0.55, 0.75));
    vec3 L2 = normalize(vec3(0.65, -0.25, 0.45));
    float d1 = max(dot(n, L1), 0.0);
    float d2 = max(dot(n, L2), 0.0);
    float hemi = 0.5 + 0.5 * n.y;
    vec3 h = normalize(L1 + v);
    float spec = pow(max(dot(n, h), 0.0), 60.0) * 0.22;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.06;

    vec3 col = base * (0.30 + 0.18 * hemi + 0.55 * d1 + 0.15 * d2) + vec3(spec + rim);
    fragColor = vec4(col, uColor.a);
}
)";

// ---------------------------------------------------------------------------
// Edges: lines expanded to screen-space quads in a geometry shader
// (core profile has no wide lines, especially on macOS).
// ---------------------------------------------------------------------------
inline const char* kEdgeVS = R"(#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in uint aId;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
flat out uint vIdV;
void main() {
    vIdV = aId;
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
}
)";

inline const char* kEdgeGS = R"(#version 410 core
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
uniform vec2 uViewport;
uniform float uWidth;
flat in uint vIdV[];
flat out uint gId;
void main() {
    vec4 p0 = gl_in[0].gl_Position;
    vec4 p1 = gl_in[1].gl_Position;
    if (p0.w <= 0.0 || p1.w <= 0.0) return; // crosses the eye plane
    vec2 halfVp = uViewport * 0.5;
    vec2 s0 = p0.xy / p0.w * halfVp;
    vec2 s1 = p1.xy / p1.w * halfVp;
    vec2 d = s1 - s0;
    float len = length(d);
    d = len > 1e-6 ? d / len : vec2(1.0, 0.0);
    vec2 n = vec2(-d.y, d.x) * (uWidth * 0.5);
    // Extend slightly along the line so joints between segments look continuous.
    vec2 e = d * (uWidth * 0.5);
    vec2 o0a = (n - e) / halfVp * p0.w, o0b = (-n - e) / halfVp * p0.w;
    vec2 o1a = (n + e) / halfVp * p1.w, o1b = (-n + e) / halfVp * p1.w;
    gId = vIdV[0]; gl_Position = vec4(p0.xy + o0a, p0.zw); EmitVertex();
    gId = vIdV[0]; gl_Position = vec4(p0.xy + o0b, p0.zw); EmitVertex();
    gId = vIdV[0]; gl_Position = vec4(p1.xy + o1a, p1.zw); EmitVertex();
    gId = vIdV[0]; gl_Position = vec4(p1.xy + o1b, p1.zw); EmitVertex();
    EndPrimitive();
}
)";

inline const char* kEdgeFS = R"(#version 410 core
flat in uint gId;
uniform vec4 uColor;
uniform uint uHoverId;
uniform vec4 uHoverColor;
out vec4 fragColor;
void main() {
    fragColor = (gId == uHoverId) ? vec4(uHoverColor.rgb, 1.0) : uColor;
}
)";

// ---------------------------------------------------------------------------
// Picking: writes (object pick id, kind << 28 | sub-shape id) as RG32UI
// ---------------------------------------------------------------------------
inline const char* kPickMeshFS = R"(#version 410 core
flat in uint vId;
in vec3 vPosVS;
in vec3 vNormalVS;
in float vScalar;
uniform uint uPickId;
layout(location = 0) out uvec2 outId;
void main() { outId = uvec2(uPickId, (1u << 28) | vId); }
)";

inline const char* kPickEdgeFS = R"(#version 410 core
flat in uint gId;
uniform uint uPickId;
layout(location = 0) out uvec2 outId;
void main() { outId = uvec2(uPickId, (2u << 28) | gId); }
)";

// ---------------------------------------------------------------------------
// Infinite-looking XY grid with adaptive spacing and colored axes
// ---------------------------------------------------------------------------
inline const char* kGridVS = R"(#version 410 core
layout(location = 0) in vec2 aPos;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec2 uCenter;
uniform float uExtent;
out vec2 vWorld;
void main() {
    vWorld = uCenter + aPos * uExtent;
    gl_Position = uProj * uView * vec4(vWorld, 0.0, 1.0);
}
)";

inline const char* kGridFS = R"(#version 410 core
in vec2 vWorld;
uniform vec2 uCenter;
uniform float uExtent;
uniform float uSpacing;
uniform vec3 uLineColor;
out vec4 fragColor;

float gridLine(vec2 p, float spacing, float widthPx) {
    vec2 c = p / spacing;
    vec2 w = fwidth(c);
    vec2 g = abs(fract(c - 0.5) - 0.5) / max(w, vec2(1e-6));
    return 1.0 - clamp(min(g.x, g.y) / widthPx, 0.0, 1.0);
}

void main() {
    float minor = gridLine(vWorld, uSpacing, 1.0);
    float major = gridLine(vWorld, uSpacing * 10.0, 1.3);
    float fade = 1.0 - smoothstep(0.35, 1.0, length(vWorld - uCenter) / uExtent);

    vec2 fw = max(fwidth(vWorld), vec2(1e-6));
    float axisX = 1.0 - clamp(abs(vWorld.y) / fw.y / 1.5, 0.0, 1.0); // X axis (y == 0)
    float axisY = 1.0 - clamp(abs(vWorld.x) / fw.x / 1.5, 0.0, 1.0); // Y axis (x == 0)

    vec3 col = uLineColor;
    float a = max(minor * 0.18, major * 0.38);
    if (axisX > 0.0) { col = mix(col, vec3(0.86, 0.26, 0.26), axisX); a = max(a, axisX * 0.85); }
    if (axisY > 0.0) { col = mix(col, vec3(0.30, 0.72, 0.30), axisY); a = max(a, axisY * 0.85); }
    a *= fade;
    if (a < 0.003) discard;
    fragColor = vec4(col, a);
}
)";

} // namespace cf::render::shaders
