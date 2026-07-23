// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Ported from POM2's OpenGLShader.cpp.

#include "OpenGLShader.h"
#include "Logger.h"

#if defined(POM1_HAS_METAL)
// macOS Metal backend links no OpenGL framework (see CMakeLists) — provide
// inert stubs so the CRT effect stack links and degrades to passthrough.
namespace pom1 {
unsigned int compileShaderProgram(const char*, const char*, std::string* errorOut)
{
    if (errorOut) *errorOut = "OpenGL shaders unavailable on the Metal backend";
    return 0;
}
void deleteShaderProgram(unsigned int) {}
bool shaderRunningOnGLES() { return false; }
} // namespace pom1

#else // ── OpenGL / OpenGL-ES backend ─────────────────────────────────────

// We need GL 2.0+ entry points (glCreateShader, glCompileShader, …) that
// aren't in the stock <GL/gl.h> 1.1 header on Linux/Windows. Strategy:
//   * macOS  — <OpenGL/gl3.h> declares them directly.
//   * Emscripten / WebGL2 — <GLES3/gl3.h> declares them directly.
//   * Linux / Windows — pull in PFN typedefs from <GL/glext.h> and
//                       resolve the symbols lazily via glfwGetProcAddress
//                       (GLFW is already linked everywhere POM1 runs).

#include <cstddef>
#include <cstring>
#include <string>

#if defined(__EMSCRIPTEN__)
#  include <GLES3/gl3.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl3.h>
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#  include <GLFW/glfw3.h>

// Function-pointer slots for the GL 2.0+ entry points we use. Resolved
// at first call by loadEntryPoints(); zero until then.
namespace {
PFNGLCREATESHADERPROC      glCreateShader_      = nullptr;
PFNGLSHADERSOURCEPROC      glShaderSource_      = nullptr;
PFNGLCOMPILESHADERPROC     glCompileShader_     = nullptr;
PFNGLGETSHADERIVPROC       glGetShaderiv_       = nullptr;
PFNGLGETSHADERINFOLOGPROC  glGetShaderInfoLog_  = nullptr;
PFNGLDELETESHADERPROC      glDeleteShader_      = nullptr;
PFNGLCREATEPROGRAMPROC     glCreateProgram_     = nullptr;
PFNGLATTACHSHADERPROC      glAttachShader_      = nullptr;
PFNGLLINKPROGRAMPROC       glLinkProgram_       = nullptr;
PFNGLGETPROGRAMIVPROC      glGetProgramiv_      = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ = nullptr;
PFNGLDELETEPROGRAMPROC     glDeleteProgram_     = nullptr;
PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation_ = nullptr;
bool entryPointsLoaded_ = false;

bool loadEntryPoints()
{
    if (entryPointsLoaded_) return true;
    auto get = [](const char* name) {
        return reinterpret_cast<void*>(glfwGetProcAddress(name));
    };
    glCreateShader_      = reinterpret_cast<PFNGLCREATESHADERPROC>     (get("glCreateShader"));
    glShaderSource_      = reinterpret_cast<PFNGLSHADERSOURCEPROC>     (get("glShaderSource"));
    glCompileShader_     = reinterpret_cast<PFNGLCOMPILESHADERPROC>    (get("glCompileShader"));
    glGetShaderiv_       = reinterpret_cast<PFNGLGETSHADERIVPROC>      (get("glGetShaderiv"));
    glGetShaderInfoLog_  = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC> (get("glGetShaderInfoLog"));
    glDeleteShader_      = reinterpret_cast<PFNGLDELETESHADERPROC>     (get("glDeleteShader"));
    glCreateProgram_     = reinterpret_cast<PFNGLCREATEPROGRAMPROC>    (get("glCreateProgram"));
    glAttachShader_      = reinterpret_cast<PFNGLATTACHSHADERPROC>     (get("glAttachShader"));
    glLinkProgram_       = reinterpret_cast<PFNGLLINKPROGRAMPROC>      (get("glLinkProgram"));
    glGetProgramiv_      = reinterpret_cast<PFNGLGETPROGRAMIVPROC>     (get("glGetProgramiv"));
    glGetProgramInfoLog_ = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(get("glGetProgramInfoLog"));
    glDeleteProgram_     = reinterpret_cast<PFNGLDELETEPROGRAMPROC>    (get("glDeleteProgram"));
    glBindAttribLocation_ = reinterpret_cast<PFNGLBINDATTRIBLOCATIONPROC>(get("glBindAttribLocation"));
    entryPointsLoaded_ =
        glCreateShader_ && glShaderSource_ && glCompileShader_ &&
        glGetShaderiv_ && glGetShaderInfoLog_ && glDeleteShader_ &&
        glCreateProgram_ && glAttachShader_ && glLinkProgram_ &&
        glGetProgramiv_ && glGetProgramInfoLog_ && glDeleteProgram_ &&
        glBindAttribLocation_;
    return entryPointsLoaded_;
}
} // namespace

// Map the unqualified call sites below onto the loaded slots.
#  define glCreateShader      glCreateShader_
#  define glShaderSource      glShaderSource_
#  define glCompileShader     glCompileShader_
#  define glGetShaderiv       glGetShaderiv_
#  define glGetShaderInfoLog  glGetShaderInfoLog_
#  define glDeleteShader      glDeleteShader_
#  define glCreateProgram     glCreateProgram_
#  define glAttachShader      glAttachShader_
#  define glLinkProgram       glLinkProgram_
#  define glGetProgramiv      glGetProgramiv_
#  define glGetProgramInfoLog glGetProgramInfoLog_
#  define glDeleteProgram     glDeleteProgram_
#  define glBindAttribLocation glBindAttribLocation_
#endif

namespace pom1 {

bool shaderRunningOnGLES()
{
#if defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}

void deleteShaderProgram(unsigned int program)
{
    if (!program) return;
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
    glDeleteProgram(program);
#else
    if (loadEntryPoints() && glDeleteProgram_) glDeleteProgram_(program);
#endif
}

#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
[[maybe_unused]] static bool loadEntryPoints() { return true; }
#endif

static unsigned int compileOne(unsigned int kind,
                               const char* versionLine,
                               const char* precisionLine,
                               const char* body,
                               std::string* errorOut)
{
    unsigned int sh = glCreateShader(kind);
    if (!sh) {
        if (errorOut) *errorOut = "glCreateShader returned 0";
        return 0;
    }
    const char* parts[3] = { versionLine, precisionLine, body };
    glShaderSource(sh, 3, parts, nullptr);
    glCompileShader(sh);
    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        int len = 0;
        glGetShaderInfoLog(sh, sizeof(log) - 1, &len, log);
        std::string msg = "shader compile failed: ";
        msg.append(log, len);
        if (errorOut) *errorOut = msg;
        pom1::log().warn("CRT", msg);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

unsigned int compileShaderProgram(const char* vertexBody,
                                  const char* fragmentBody,
                                  std::string* errorOut)
{
#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
    if (!loadEntryPoints()) {
        if (errorOut) *errorOut = "GL 3.x entry points unavailable";
        pom1::log().warn("CRT", "GL 3.x entry points unavailable — "
                                "CRT effect shader disabled");
        return 0;
    }
#endif

#if defined(__EMSCRIPTEN__)
    const char* versionLine   = "#version 300 es\n";
    const char* precisionLine = "precision highp float;\nprecision highp int;\n";
#else
    const char* versionLine   = "#version 150\n";
    const char* precisionLine = "\n";
#endif

    unsigned int vs = compileOne(GL_VERTEX_SHADER,
                                 versionLine, precisionLine, vertexBody, errorOut);
    if (!vs) return 0;
    unsigned int fs = compileOne(GL_FRAGMENT_SHADER,
                                 versionLine, precisionLine, fragmentBody, errorOut);
    if (!fs) { glDeleteShader(vs); return 0; }

    unsigned int prog = glCreateProgram();
    if (!prog) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (errorOut) *errorOut = "glCreateProgram returned 0";
        return 0;
    }
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    // Pin the fullscreen-quad position attribute to location 0 before linking.
    // Under GLSL 1.50 / 3.00 es the linker may otherwise assign `aPos` any
    // generic slot, yet callers hardcode glVertexAttribPointer(0, ...).
    glBindAttribLocation(prog, 0, "aPos");
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[2048] = {0};
        int len = 0;
        glGetProgramInfoLog(prog, sizeof(log) - 1, &len, log);
        std::string msg = "shader link failed: ";
        msg.append(log, len);
        if (errorOut) *errorOut = msg;
        pom1::log().warn("CRT", msg);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

} // namespace pom1

#endif // POM1_HAS_METAL
