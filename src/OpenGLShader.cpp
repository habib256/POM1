// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Ported from POM2's OpenGLShader.cpp.

#include "OpenGLShader.h"
#include "Logger.h"
#include "POM1Build.h"   // POM1_GL_ES — "we speak GLES", not "we are a browser"

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
//   * GLES 3.0 — <GLES3/gl3.h> declares them directly, and libGLESv2 (native,
//                -DPOM1_GLES=ON) / the WebGL2 shim (Emscripten) exports them.
//                POM1_GL_ES covers BOTH; keying this on __EMSCRIPTEN__ was
//                what pinned the GLES path to the browser build.
//   * Linux / Windows desktop GL — pull in PFN typedefs from <GL/glext.h> and
//                       resolve the symbols lazily via glfwGetProcAddress
//                       (GLFW is already linked everywhere POM1 runs).

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if POM1_GL_ES
#  include <GLES3/gl3.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl3.h>
#else
// GLFW FIRST, deliberately: on Win32 the system <GL/gl.h> declares its
// prototypes with WINGDIAPI/APIENTRY and does NOT include <windows.h> that
// defines them, so including it standalone is 100+ syntax errors under MSVC.
// glfw3.h pulls windows.h and then gl.h in the right order (and gl.h's own
// include guard makes the explicit include below a no-op everywhere).
#  include <GLFW/glfw3.h>
#  include <GL/gl.h>
// PFN typedefs + post-1.1 enums. Linux gets them from <GL/glext.h> (pulled by
// Mesa's gl.h); the Windows SDK ships no glext.h, so GLProcs.h declares the
// entry points POM1 uses. Skipped entirely when glext.h already provided them.
#  include "GLProcs.h"

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
    return POM1_GL_ES != 0;
}

void deleteShaderProgram(unsigned int program)
{
    if (!program) return;
#if POM1_GL_ES || defined(__APPLE__)
    glDeleteProgram(program);
#else
    if (loadEntryPoints() && glDeleteProgram_) glDeleteProgram_(program);
#endif
}

#if POM1_GL_ES || defined(__APPLE__)
[[maybe_unused]] static bool loadEntryPoints() { return true; }
#endif

namespace {

/// One candidate GLSL preamble: the `#version` line plus the precision lines
/// that dialect needs.
struct GlslDialect {
    const char* version;
    const char* precision;
};

/// Dialects to try, richest first.
///
/// The CRT shader bodies only use GLSL **1.30** constructs (`in`/`out`,
/// `texture()`, `fwidth()`), so 130 and 140 run them just as well as 150 —
/// hardcoding `#version 150` was the *only* thing that made them 3.2-core-only.
/// That mattered on the Raspberry Pi: Mesa's V3D caps desktop GLSL at **1.40**
/// and rejects the shader with "GLSL 1.50 is not supported. Supported versions
/// are: 1.10, 1.20, 1.30, 1.40, 1.00 ES, 3.00 ES" — the CRT panel then read
/// "shader unavailable" on a stack that could have run the effect fine.
/// (Ported from NeoST, which hit exactly this on its Pi kiosk.)
std::vector<GlslDialect> glslDialects()
{
    const char* kEsPrecision = "precision highp float;\nprecision highp int;\n";

#if POM1_GL_ES
    // WASM (WebGL 2) and the native GLES tier: one dialect, no probing.
    return { { "#version 300 es\n", kEsPrecision } };
#else
    const char* sl = reinterpret_cast<const char*>(
        glGetString(GL_SHADING_LANGUAGE_VERSION));

    // A *desktop-GL build* can still end up on a GLES context (a driver handing
    // out GLES through EGL): "OpenGL ES GLSL ES 3.20".
    if (sl && std::strstr(sl, "ES ") != nullptr)
        return { { "#version 300 es\n", kEsPrecision } };

    // Desktop: "1.40" or "4.60 NVIDIA". With no usable string we try the whole
    // cascade — worst case two failed compiles, once, at startup.
    int major = 0, minor = 0;
    if (sl) std::sscanf(sl, "%d.%d", &major, &minor);
    if (minor < 10) minor *= 10;           // "4.6" ≡ "4.60"
    const int ver = major * 100 + minor;   // 1.40 → 140

    std::vector<GlslDialect> out;
    if (ver == 0 || ver >= 150) out.push_back({ "#version 150\n", "\n" });
    if (ver == 0 || ver >= 140) out.push_back({ "#version 140\n", "\n" });
    if (ver == 0 || ver >= 130) out.push_back({ "#version 130\n", "\n" });
    if (out.empty()) out.push_back({ "#version 130\n", "\n" });  // last resort
    return out;
#endif
}

} // namespace

/// `quiet` silences the log for fallback attempts, whose failure is expected
/// and must not read as a defect.
static unsigned int compileOne(unsigned int kind,
                               const char* versionLine,
                               const char* precisionLine,
                               const char* body,
                               std::string* errorOut,
                               bool quiet)
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
        if (!quiet) pom1::log().warn("CRT", msg);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

unsigned int compileShaderProgram(const char* vertexBody,
                                  const char* fragmentBody,
                                  std::string* errorOut)
{
#if !POM1_GL_ES && !defined(__APPLE__)
    if (!loadEntryPoints()) {
        if (errorOut) *errorOut = "GL 3.x entry points unavailable";
        pom1::log().warn("CRT", "GL 3.x entry points unavailable — "
                                "CRT effect shader disabled");
        return 0;
    }
#endif

    // Try each dialect until BOTH shaders compile. The cascade is a net, not a
    // flourish: a driver can advertise a version and still refuse it in *this*
    // context — only the actual compile settles it.
    const std::vector<GlslDialect> dialects = glslDialects();
    const char* versionLine = nullptr;
    unsigned int vs = 0, fs = 0;
    for (std::size_t i = 0; i < dialects.size(); ++i) {
        const bool last = (i + 1 == dialects.size());
        vs = compileOne(GL_VERTEX_SHADER, dialects[i].version,
                        dialects[i].precision, vertexBody, errorOut, !last);
        if (!vs) continue;
        fs = compileOne(GL_FRAGMENT_SHADER, dialects[i].version,
                        dialects[i].precision, fragmentBody, errorOut, !last);
        if (fs) { versionLine = dialects[i].version; break; }
        glDeleteShader(vs);
        vs = 0;
    }
    if (!versionLine) return 0;   // errorOut carries the last attempt's failure

    // A dialect went through: clear the failed attempts' error, or the CRT
    // panel would say "shader unavailable" with a working stack behind it.
    if (errorOut) errorOut->clear();
    // Always traced: on a machine where the CRT stack misbehaves (unknown
    // driver, Raspberry Pi kiosk…) this is the line that says which dialect was
    // actually accepted and what the driver claimed.
    {
        const char* sl = reinterpret_cast<const char*>(
            glGetString(GL_SHADING_LANGUAGE_VERSION));
        std::string chosen(versionLine + std::strlen("#version "));
        while (!chosen.empty() && chosen.back() == '\n') chosen.pop_back();
        pom1::log().info("CRT", "GLSL " + chosen +
                                " (driver: " + (sl ? sl : "?") + ")");
    }

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
