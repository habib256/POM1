// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// PomRenderer_Internal.h — PRIVATE header shared by PomRenderer.cpp,
// PomRenderer_GL.cpp and PomRenderer_Metal.mm. Carries the full layout of
// `pom1::Texture` so every backend can read/write its own field. Not
// included by any other TU — the public PomRenderer.h keeps the struct
// forward-declared so callers see only Texture* and never the fields.

#ifndef POM1_RENDERER_INTERNAL_H
#define POM1_RENDERER_INTERNAL_H

#include "PomRenderer.h"

namespace pom1 {

struct Texture {
    // OpenGL backend (PomRenderer_GL.cpp): GLuint texture name.
    unsigned int glId = 0;
    // Metal backend (PomRenderer_Metal.mm): retained id<MTLTexture> stashed
    // as void* so this header file never has to drag in Metal/. Metal-side
    // bridging is done in the .mm via __bridge / __bridge_retained.
    void* mtlTexture = nullptr;
    int   w = 0;
    int   h = 0;
};

} // namespace pom1

#endif // POM1_RENDERER_INTERNAL_H
