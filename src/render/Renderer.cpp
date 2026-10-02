#include "render/Renderer.h"
#include "render/Shaders.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace cf::render {

namespace {
glm::mat4 toFloat(const Mat4& m) { return glm::mat4(m); }
} // namespace

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    destroyTargets();
    if (m_emptyVao) glDeleteVertexArrays(1, &m_emptyVao);
    if (m_gridVao) glDeleteVertexArrays(1, &m_gridVao);
    if (m_gridVbo) glDeleteBuffers(1, &m_gridVbo);
}

bool Renderer::init()
{
    using namespace shaders;
    bool ok = m_bgProg.build("background", kBackgroundVS, kBackgroundFS);
    ok &= m_meshProg.build("mesh", kMeshVS, kMeshFS);
    ok &= m_edgeProg.build("edge", kEdgeVS, kEdgeFS, kEdgeGS);
    ok &= m_gridProg.build("grid", kGridVS, kGridFS);
    ok &= m_pickMeshProg.build("pick-mesh", kMeshVS, kPickMeshFS);
    ok &= m_pickEdgeProg.build("pick-edge", kEdgeVS, kPickEdgeFS, kEdgeGS);
    if (!ok)
        return false;

    glGenVertexArrays(1, &m_emptyVao);

    const float quad[] = {-1, -1, 1, -1, 1, 1, -1, 1};
    glGenVertexArrays(1, &m_gridVao);
    glGenBuffers(1, &m_gridVbo);
    glBindVertexArray(m_gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    m_samples = std::clamp(maxSamples, 1, 4);

    checkGlError("Renderer::init");
    return true;
}

// ---- render targets ----------------------------------------------------------

void Renderer::destroyTargets()
{
    const GLuint fbos[] = {m_msFbo, m_resolveFbo, m_pickFbo};
    for (GLuint f : fbos)
        if (f) glDeleteFramebuffers(1, &f);
    const GLuint rbs[] = {m_msColor, m_msDepth, m_pickColor, m_pickDepth};
    for (GLuint r : rbs)
        if (r) glDeleteRenderbuffers(1, &r);
    if (m_resolveTex)
        glDeleteTextures(1, &m_resolveTex);
    m_msFbo = m_resolveFbo = m_pickFbo = 0;
    m_msColor = m_msDepth = m_pickColor = m_pickDepth = 0;
    m_resolveTex = 0;
    m_width = m_height = 0;
}

void Renderer::ensureTargets(int w, int h)
{
    w = std::max(w, 1);
    h = std::max(h, 1);
    if (w == m_width && h == m_height)
        return;
    destroyTargets();
    m_width = w;
    m_height = h;

    // Multisampled scene target
    glGenFramebuffers(1, &m_msFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msFbo);
    glGenRenderbuffers(1, &m_msColor);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_RGBA8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msColor);
    glGenRenderbuffers(1, &m_msDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_msDepth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        log::error("Scene framebuffer incomplete");

    // Resolve target (sampled by ImGui)
    glGenTextures(1, &m_resolveTex);
    glBindTexture(GL_TEXTURE_2D, m_resolveTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &m_resolveFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_resolveTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        log::error("Resolve framebuffer incomplete");

    // Id buffer for picking
    glGenFramebuffers(1, &m_pickFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_pickFbo);
    glGenRenderbuffers(1, &m_pickColor);
    glBindRenderbuffer(GL_RENDERBUFFER, m_pickColor);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RG32UI, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_pickColor);
    glGenRenderbuffers(1, &m_pickDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_pickDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_pickDepth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        log::error("Pick framebuffer incomplete");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    checkGlError("Renderer::ensureTargets");
}

// ---- drawing -------------------------------------------------------------------

void Renderer::setCommonUniforms(ShaderProgram& p, const glm::mat4& view, const glm::mat4& proj)
{
    p.use();
    p.set("uModel", glm::mat4(1.0f));
    p.set("uView", view);
    p.set("uProj", proj);
}

void Renderer::drawBackground(const RenderSettings& s)
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    m_bgProg.use();
    m_bgProg.set("uTop", s.backgroundTop);
    m_bgProg.set("uBottom", s.backgroundBottom);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawGrid(const Camera& cam, const RenderSettings& s)
{
    const double vh = cam.viewHeight();
    m_gridSpacing = std::pow(10.0, std::floor(std::log10(std::max(vh / 3.0, 1e-6))));
    const double major = m_gridSpacing * 10.0;
    const Vec3 t = cam.state().target;
    const glm::vec2 center(float(std::round(t.x / major) * major), float(std::round(t.y / major) * major));
    const float extent = float(std::max(vh * 4.0, cam.state().distance * 3.0));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_gridProg.use();
    m_gridProg.set("uView", toFloat(cam.view()));
    m_gridProg.set("uProj", toFloat(cam.projection()));
    m_gridProg.set("uCenter", center);
    m_gridProg.set("uExtent", extent);
    m_gridProg.set("uSpacing", float(m_gridSpacing));
    m_gridProg.set("uLineColor", glm::vec3(0.62f, 0.66f, 0.72f));
    glBindVertexArray(m_gridVao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::render(const Camera& cam, const std::vector<DrawItem>& items, const RenderSettings& s, int w,
                      int h)
{
    ensureTargets(w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msFbo);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    drawBackground(s);

    const glm::mat4 view = toFloat(cam.view());
    const glm::mat4 proj = toFloat(cam.projection());
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);

    // ---- opaque faces ----
    setCommonUniforms(m_meshProg, view, proj);
    m_meshProg.set("uOrtho", cam.orthographic() ? 1 : 0);
    m_meshProg.set("uHoverColor", glm::vec4(s.hoverColor, 0.45f));
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    for (const DrawItem& it : items) {
        if (!it.mesh || it.style != DrawItem::Style::Solid)
            continue;
        glm::vec3 c = glm::vec3(it.color);
        if (it.selected)
            c = glm::mix(c, s.selectionColor, 0.55f);
        else if (it.hovered)
            c = glm::mix(c, s.hoverColor, 0.30f);
        m_meshProg.set("uUseScalars", it.showScalars && it.mesh->hasScalars() ? 1 : 0);
        m_meshProg.set("uScalarRange", it.scalarRange);
        if (s.showFaces || it.selected) {
            m_meshProg.set("uColor", glm::vec4(c, 1.0f));
            m_meshProg.set("uHoverId", unsigned(it.hoverFace));
            it.mesh->drawFaces();
        }
        if (!it.faceTints.empty()) {
            m_meshProg.set("uUseScalars", 0);
            m_meshProg.set("uHoverId", unsigned(it.hoverFace));
            for (const auto& [face, color] : it.faceTints) {
                m_meshProg.set("uColor", glm::vec4(color, 1.0f));
                it.mesh->drawFace(face);
            }
        }
        if (!it.selectedFaces.empty()) {
            m_meshProg.set("uUseScalars", 0);
            m_meshProg.set("uColor", glm::vec4(s.selectionColor, 1.0f));
            m_meshProg.set("uHoverId", 0u);
            for (int f : it.selectedFaces)
                it.mesh->drawFace(f);
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);

    // ---- edges ----
    setCommonUniforms(m_edgeProg, view, proj);
    m_edgeProg.set("uViewport", glm::vec2(float(m_width), float(m_height)));
    m_edgeProg.set("uHoverColor", glm::vec4(s.hoverColor, 1.0f));
    for (const DrawItem& it : items) {
        if (!it.mesh || it.style != DrawItem::Style::Solid)
            continue;
        if (s.showEdges || it.selected) {
            m_edgeProg.set("uWidth", it.selected ? s.edgeWidth * 1.4f : s.edgeWidth);
            m_edgeProg.set("uColor", glm::vec4(it.selected ? s.selectionColor * 0.85f : s.edgeColor, 1.0f));
            m_edgeProg.set("uHoverId", unsigned(it.hoverEdge));
            it.mesh->drawEdges();
        } else if (it.hoverEdge) {
            m_edgeProg.set("uWidth", s.edgeWidth * 2.0f);
            m_edgeProg.set("uColor", glm::vec4(s.hoverColor, 1.0f));
            m_edgeProg.set("uHoverId", 0u);
            it.mesh->drawEdge(it.hoverEdge);
        }
        if (!it.selectedEdges.empty()) {
            m_edgeProg.set("uWidth", s.edgeWidth * 2.6f);
            m_edgeProg.set("uColor", glm::vec4(s.selectionColor, 1.0f));
            m_edgeProg.set("uHoverId", 0u);
            for (int e : it.selectedEdges)
                it.mesh->drawEdge(e);
        }
        if (it.hoverEdge && s.showEdges) {
            m_edgeProg.set("uWidth", s.edgeWidth * 2.6f);
            m_edgeProg.set("uColor", glm::vec4(s.hoverColor, 1.0f));
            it.mesh->drawEdge(it.hoverEdge);
        }
    }

    // ---- grid ----
    if (s.showGrid)
        drawGrid(cam, s);

    // ---- ghosts (e.g. boolean inputs of the selected feature) ----
    bool anyGhost = std::any_of(items.begin(), items.end(),
                                [](const DrawItem& i) { return i.mesh && i.style == DrawItem::Style::Ghost; });
    if (anyGhost) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        setCommonUniforms(m_meshProg, view, proj);
        m_meshProg.set("uUseScalars", 0);
        m_meshProg.set("uHoverId", 0u);
        for (const DrawItem& it : items) {
            if (!it.mesh || it.style != DrawItem::Style::Ghost)
                continue;
            m_meshProg.set("uColor", glm::vec4(glm::vec3(it.color), 0.22f));
            it.mesh->drawFaces();
        }
        glDisable(GL_CULL_FACE);
        setCommonUniforms(m_edgeProg, view, proj);
        m_edgeProg.set("uWidth", s.edgeWidth);
        m_edgeProg.set("uHoverId", 0u);
        for (const DrawItem& it : items) {
            if (!it.mesh || it.style != DrawItem::Style::Ghost)
                continue;
            m_edgeProg.set("uColor", glm::vec4(glm::vec3(it.color) * 1.1f, 0.55f));
            it.mesh->drawEdges();
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // ---- resolve MSAA into the sampled texture ----
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    checkGlError("Renderer::render");
}

// ---- picking ---------------------------------------------------------------------

PickResult Renderer::pick(const Camera& cam, const std::vector<DrawItem>& items, PickFilter filter, int x, int y,
                          int radius, float edgeWidthPx)
{
    PickResult result;
    if (m_width <= 0 || m_height <= 0 || x < 0 || y < 0 || x >= m_width || y >= m_height)
        return result;

    glBindFramebuffer(GL_FRAMEBUFFER, m_pickFbo);
    glViewport(0, 0, m_width, m_height);
    const GLuint zero[4] = {0, 0, 0, 0};
    glClearBufferuiv(GL_COLOR, 0, zero);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    const glm::mat4 view = toFloat(cam.view());
    const glm::mat4 proj = toFloat(cam.projection());

    setCommonUniforms(m_pickMeshProg, view, proj);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    for (const DrawItem& it : items) {
        if (!it.mesh || !it.pickId || it.style != DrawItem::Style::Solid)
            continue;
        m_pickMeshProg.set("uPickId", unsigned(it.pickId));
        it.mesh->drawFaces();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);

    if (filter != PickFilter::Face) {
        setCommonUniforms(m_pickEdgeProg, view, proj);
        m_pickEdgeProg.set("uViewport", glm::vec2(float(m_width), float(m_height)));
        m_pickEdgeProg.set("uWidth", std::max(edgeWidthPx, 1.0f) * 3.0f);
        for (const DrawItem& it : items) {
            if (!it.mesh || !it.pickId || it.style != DrawItem::Style::Solid)
                continue;
            m_pickEdgeProg.set("uPickId", unsigned(it.pickId));
            it.mesh->drawEdges();
        }
    }

    // Read a small window around the cursor (GL origin is bottom-left).
    const int glY = m_height - 1 - y;
    const int x0 = std::max(0, x - radius), y0 = std::max(0, glY - radius);
    const int x1 = std::min(m_width - 1, x + radius), y1 = std::min(m_height - 1, glY + radius);
    const int ww = x1 - x0 + 1, hh = y1 - y0 + 1;
    std::vector<GLuint> buf(static_cast<std::size_t>(ww * hh * 2));
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(x0, y0, ww, hh, GL_RG_INTEGER, GL_UNSIGNED_INT, buf.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);

    int bestDist = 1 << 30;
    for (int j = 0; j < hh; ++j) {
        for (int i = 0; i < ww; ++i) {
            const std::size_t k = static_cast<std::size_t>((j * ww + i) * 2);
            const GLuint obj = buf[k], sub = buf[k + 1];
            if (!obj)
                continue;
            const auto kind = static_cast<PickKind>(sub >> 28);
            if (filter == PickFilter::Edge && kind != PickKind::Edge)
                continue;
            if (filter == PickFilter::Face && kind != PickKind::Face)
                continue;
            const int dx = x0 + i - x, dy = y0 + j - glY;
            const int d = dx * dx + dy * dy;
            if (d < bestDist) {
                bestDist = d;
                result.pickId = obj;
                result.kind = kind;
                result.index = static_cast<int>(sub & 0x0FFFFFFFu);
            }
        }
    }
    checkGlError("Renderer::pick");
    return result;
}

} // namespace cf::render
