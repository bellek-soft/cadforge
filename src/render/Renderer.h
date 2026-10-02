#pragma once
// Scene renderer: draws into an offscreen MSAA target (shown by the UI as an
// image) and provides GPU id-buffer picking of objects, faces and edges.
// The renderer knows nothing about features - only meshes and draw items.

#include "core/Types.h"
#include "render/Camera.h"
#include "render/GlUtil.h"
#include "render/GpuMesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace cf::render {

struct DrawItem {
    enum class Style { Solid, Ghost };

    const GpuMesh* mesh = nullptr;
    std::uint32_t pickId = 0;        // 0 = not pickable
    glm::vec4 color{0.7f, 0.7f, 0.7f, 1.0f};
    Style style = Style::Solid;
    bool wire = false;               // edges-only object (sketch): edges drawn in `color`, always visible

    bool selected = false;           // whole-object selection
    bool hovered = false;            // whole-object hover
    std::vector<int> selectedFaces;  // 1-based
    std::vector<int> selectedEdges;  // 1-based
    std::vector<std::pair<int, glm::vec3>> faceTints; // 1-based face -> color (e.g. loads)
    int hoverFace = 0;
    int hoverEdge = 0;

    bool showScalars = false;        // render MeshData::scalars with a colormap
    glm::vec2 scalarRange{0.0f, 1.0f};
};

struct RenderSettings {
    bool showFaces = true;
    bool showEdges = true;
    bool showGrid = true;
    float edgeWidth = 1.4f;          // in framebuffer pixels
    glm::vec3 backgroundTop{0.23f, 0.25f, 0.29f};
    glm::vec3 backgroundBottom{0.11f, 0.12f, 0.14f};
    glm::vec3 edgeColor{0.08f, 0.09f, 0.11f};
    glm::vec3 selectionColor{1.0f, 0.62f, 0.12f};
    glm::vec3 hoverColor{0.45f, 0.82f, 1.0f};
};

enum class PickKind : int { None = 0, Face = 1, Edge = 2 };

enum class PickFilter { Object, Face, Edge };

struct PickResult {
    std::uint32_t pickId = 0;  // 0 = nothing
    PickKind kind = PickKind::None;
    int index = 0;             // 1-based face/edge index
    explicit operator bool() const { return pickId != 0; }
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    /// Requires a current OpenGL 4.1+ core context with glad loaded.
    bool init();

    /// Makes sure the internal targets have the given pixel size.
    void resize(int widthPx, int heightPx) { ensureTargets(widthPx, heightPx); }

    /// Renders the scene into the internal color texture of size (w, h) pixels.
    void render(const Camera& cam, const std::vector<DrawItem>& items, const RenderSettings& s,
                int widthPx, int heightPx);

    /// Picks at pixel (x, y) (origin top-left) searching a square of `radius` pixels.
    PickResult pick(const Camera& cam, const std::vector<DrawItem>& items, PickFilter filter, int x, int y,
                    int radius, float edgeWidthPx);

    GLuint colorTexture() const { return m_resolveTex; }
    /// Current grid spacing (world units) chosen from the camera distance.
    double gridSpacing() const { return m_gridSpacing; }

private:
    void ensureTargets(int w, int h);
    void destroyTargets();
    void drawBackground(const RenderSettings& s);
    void drawGrid(const Camera& cam, const RenderSettings& s);
    void setCommonUniforms(ShaderProgram& p, const glm::mat4& view, const glm::mat4& proj);

    ShaderProgram m_bgProg, m_meshProg, m_edgeProg, m_gridProg, m_pickMeshProg, m_pickEdgeProg;
    GLuint m_emptyVao = 0, m_gridVao = 0, m_gridVbo = 0;

    int m_width = 0, m_height = 0, m_samples = 4;
    GLuint m_msFbo = 0, m_msColor = 0, m_msDepth = 0;
    GLuint m_resolveFbo = 0, m_resolveTex = 0;
    GLuint m_pickFbo = 0, m_pickColor = 0, m_pickDepth = 0;
    double m_gridSpacing = 10.0;
};

} // namespace cf::render
