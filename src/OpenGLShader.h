// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Minimal GLSL shader compile/link helper. Used by CrtEffectStack to build
// the CRT post-effect shader pass without dragging in a full shader
// framework. Header-only API; the cpp owns the platform GL include and the
// diagnostic strings. Ported from POM2's OpenGLShader.

#ifndef POM1_OPENGL_SHADER_H
#define POM1_OPENGL_SHADER_H

#include <string>

namespace pom1 {

// Compile + link a single (vertex, fragment) program. Returns the GL
// program object on success, 0 on failure. The chosen GLSL version line
// (e.g. "#version 150" on desktop, "#version 300 es" on Emscripten) is
// prepended automatically — pass only the body of each shader. Any
// compile / link errors are written to `errorOut` and also logged via
// pom1::log() at warn level.
unsigned int compileShaderProgram(const char* vertexBody,
                                  const char* fragmentBody,
                                  std::string* errorOut = nullptr);

// Delete a program returned by compileShaderProgram (no-op on 0).
void deleteShaderProgram(unsigned int program);

// True when the running GL context is GLES (Emscripten / WebGL2).
bool shaderRunningOnGLES();

} // namespace pom1

#endif // POM1_OPENGL_SHADER_H
