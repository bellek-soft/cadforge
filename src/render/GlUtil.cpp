#include "render/GlUtil.h"

#include "core/Log.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <vector>

namespace cf::render {

namespace {
GLuint compile(GLenum type, const char* src, const char* name)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> buf(static_cast<std::size_t>(std::max(len, 1)));
        glGetShaderInfoLog(s, len, nullptr, buf.data());
        log::error("Shader '", name, "' compile error:\n", buf.data());
        glDeleteShader(s);
        return 0;
    }
    return s;
}
} // namespace

ShaderProgram::~ShaderProgram()
{
    if (m_id)
        glDeleteProgram(m_id);
}

bool ShaderProgram::build(const char* name, const char* vs, const char* fs, const char* gs)
{
    GLuint v = compile(GL_VERTEX_SHADER, vs, name);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs, name);
    GLuint g = gs ? compile(GL_GEOMETRY_SHADER, gs, name) : 0;
    if (!v || !f || (gs && !g)) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        if (g) glDeleteShader(g);
        return false;
    }
    m_id = glCreateProgram();
    glAttachShader(m_id, v);
    glAttachShader(m_id, f);
    if (g)
        glAttachShader(m_id, g);
    glLinkProgram(m_id);
    glDeleteShader(v);
    glDeleteShader(f);
    if (g)
        glDeleteShader(g);

    GLint ok = 0;
    glGetProgramiv(m_id, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(m_id, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> buf(static_cast<std::size_t>(std::max(len, 1)));
        glGetProgramInfoLog(m_id, len, nullptr, buf.data());
        log::error("Shader '", name, "' link error:\n", buf.data());
        glDeleteProgram(m_id);
        m_id = 0;
        return false;
    }
    return true;
}

GLint ShaderProgram::location(const char* name)
{
    auto it = m_locations.find(name);
    if (it != m_locations.end())
        return it->second;
    GLint loc = glGetUniformLocation(m_id, name);
    m_locations.emplace(name, loc);
    return loc;
}

void ShaderProgram::set(const char* n, float v) { glUniform1f(location(n), v); }
void ShaderProgram::set(const char* n, int v) { glUniform1i(location(n), v); }
void ShaderProgram::set(const char* n, unsigned v) { glUniform1ui(location(n), v); }
void ShaderProgram::set(const char* n, const glm::vec2& v) { glUniform2fv(location(n), 1, glm::value_ptr(v)); }
void ShaderProgram::set(const char* n, const glm::vec3& v) { glUniform3fv(location(n), 1, glm::value_ptr(v)); }
void ShaderProgram::set(const char* n, const glm::vec4& v) { glUniform4fv(location(n), 1, glm::value_ptr(v)); }
void ShaderProgram::set(const char* n, const glm::mat4& v)
{
    glUniformMatrix4fv(location(n), 1, GL_FALSE, glm::value_ptr(v));
}

void checkGlError(const char* where)
{
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError())
        log::error("OpenGL error 0x", std::hex, e, std::dec, " at ", where);
}

} // namespace cf::render
