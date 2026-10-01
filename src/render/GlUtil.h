#pragma once
// Thin RAII helpers over OpenGL 4.1 core objects.

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

namespace cf::render {

class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    /// Compiles and links; geometry source may be empty. Returns false and logs on error.
    bool build(const char* name, const char* vs, const char* fs, const char* gs = nullptr);
    void use() const { glUseProgram(m_id); }
    GLuint id() const { return m_id; }

    void set(const char* name, float v);
    void set(const char* name, int v);
    void set(const char* name, unsigned v);
    void set(const char* name, const glm::vec2& v);
    void set(const char* name, const glm::vec3& v);
    void set(const char* name, const glm::vec4& v);
    void set(const char* name, const glm::mat4& v);

private:
    GLint location(const char* name);
    GLuint m_id = 0;
    std::unordered_map<std::string, GLint> m_locations;
};

/// Logs any pending GL errors with context (debug aid).
void checkGlError(const char* where);

} // namespace cf::render
