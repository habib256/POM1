// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// GL 2.0+ typedefs/enums for the platforms whose <GL/gl.h> stops at 1.1.
//
// POM1's shader code (OpenGLShader.cpp) and CRT effect pass (CrtEffectStack.cpp)
// resolve their GL 2.0+ entry points lazily through glfwGetProcAddress, so all
// they need at compile time are the PFN* function-pointer types and the enums.
// On Linux, Mesa's <GL/gl.h> pulls <GL/glext.h> automatically and supplies
// both. The **Windows SDK ships no glext.h at all** — only a GL 1.1 gl.h — so
// including it there is a hard "cannot open include file" error. Rather than
// vendor Khronos' 1 MB registry header (or add a vcpkg opengl-registry
// dependency that every local VS build would then also need), declare the ~30
// entry points POM1 actually uses. Same precedent as PomRenderer_GL.cpp, which
// already defines GL_CLAMP_TO_EDGE by hand for that reason.
//
// The whole body is skipped when glext.h was already included (it defines
// GL_VERSION_2_0), so on Linux this header costs nothing and can never
// conflict. Include it AFTER <GLFW/glfw3.h> (which pulls windows.h + gl.h on
// Win32 — <GL/gl.h> alone does not compile there).
//
// To exercise the fallback path on Linux, compile with -DGL_GLEXT_LEGACY:
// Mesa's gl.h then skips glext.h, exactly like the Windows SDK.

#pragma once

#if !defined(GL_VERSION_2_0)

#include <cstddef>

#ifndef APIENTRY
#define APIENTRY
#endif

// ── Types (GL 2.0) ────────────────────────────────────────────────────────
typedef char      GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

// ── Enums beyond GL 1.1 ───────────────────────────────────────────────────
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_VERTEX_SHADER        0x8B31
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_INFO_LOG_LENGTH      0x8B84
#define GL_ARRAY_BUFFER         0x8892
#define GL_STATIC_DRAW          0x88E4
#define GL_TEXTURE0             0x84C0
#define GL_TEXTURE1             0x84C1
#define GL_FRAMEBUFFER          0x8D40
#define GL_FRAMEBUFFER_BINDING  0x8CA6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0    0x8CE0
// GL 1.2 clamp mode — absent from the Windows SDK's 1.1 gl.h. PomRenderer_GL.cpp
// defines it the same way for the same reason; #ifndef so both can coexist.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE        0x812F
#endif
// GL 2.0 query, same absence for the same reason. OpenGLShader.cpp reads it
// through glGetString() (GL 1.0, always declared) to pick the GLSL dialect it
// compiles the CRT shaders with.
#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif

// ── Shader / program objects (GL 2.0) ─────────────────────────────────────
typedef GLuint (APIENTRY* PFNGLCREATESHADERPROC)(GLenum type);
typedef void   (APIENTRY* PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count,
                                                 const GLchar* const* string,
                                                 const GLint* length);
typedef void   (APIENTRY* PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void   (APIENTRY* PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void   (APIENTRY* PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize,
                                                     GLsizei* length, GLchar* infoLog);
typedef void   (APIENTRY* PFNGLDELETESHADERPROC)(GLuint shader);
typedef GLuint (APIENTRY* PFNGLCREATEPROGRAMPROC)(void);
typedef void   (APIENTRY* PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void   (APIENTRY* PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void   (APIENTRY* PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void   (APIENTRY* PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize,
                                                      GLsizei* length, GLchar* infoLog);
typedef void   (APIENTRY* PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void   (APIENTRY* PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void   (APIENTRY* PFNGLBINDATTRIBLOCATIONPROC)(GLuint program, GLuint index,
                                                       const GLchar* name);
typedef GLint  (APIENTRY* PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void   (APIENTRY* PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void   (APIENTRY* PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void   (APIENTRY* PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);

// ── Buffers / vertex arrays (GL 1.5 / 3.0) ────────────────────────────────
typedef void   (APIENTRY* PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
typedef void   (APIENTRY* PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void   (APIENTRY* PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size,
                                               const void* data, GLenum usage);
typedef void   (APIENTRY* PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);
typedef void   (APIENTRY* PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void   (APIENTRY* PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void   (APIENTRY* PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint* arrays);
typedef void   (APIENTRY* PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void   (APIENTRY* PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type,
                                                        GLboolean normalized, GLsizei stride,
                                                        const void* pointer);

// ── Framebuffer objects (GL 3.0) + multitexture (GL 1.3) ──────────────────
typedef void   (APIENTRY* PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void   (APIENTRY* PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void   (APIENTRY* PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment,
                                                         GLenum textarget, GLuint texture,
                                                         GLint level);
typedef GLenum (APIENTRY* PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void   (APIENTRY* PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* framebuffers);
typedef void   (APIENTRY* PFNGLACTIVETEXTUREPROC)(GLenum texture);

#endif // !GL_VERSION_2_0
