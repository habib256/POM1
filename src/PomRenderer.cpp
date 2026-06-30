// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// PomRenderer.cpp — single-definition home for things that must NOT diverge
// between the OpenGL and Metal backends:
//   * the `pom1::Texture` struct (one POD layout, both backends look only at
//     the field they care about; the other one stays zero-init);
//   * the global `pom1::renderer()` / `pom1::setRenderer()` accessor;
//   * the `pom1::makeRenderer(GLFWwindow*)` factory which dispatches to
//     `makeGlRenderer` or `makeMetalRenderer` based on POM1_HAS_METAL.
//
// Splitting this off lets each backend file (PomRenderer_GL.cpp,
// PomRenderer_Metal.mm) own only its own implementation without colliding
// on the public-API entry points.

#include "PomRenderer_Internal.h"   // also pulls in PomRenderer.h
#include "POM1Build.h"

#include <cstdint>

namespace pom1 {

namespace {
PomRenderer* g_renderer = nullptr;
}

PomRenderer*  renderer()                  { return g_renderer; }
void          setRenderer(PomRenderer* r) { g_renderer = r; }

// Backend factories — defined in their respective TUs. Forward-declared here
// so the dispatch below stays in one place. The Metal forward decl is only
// pulled in when CMake set POM1_HAS_METAL=1, so non-Metal builds never see a
// reference to an unresolved symbol at link time.
std::unique_ptr<PomRenderer> makeGlRenderer(GLFWwindow* window);
#if defined(POM1_HAS_METAL) && POM1_HAS_METAL
std::unique_ptr<PomRenderer> makeMetalRenderer(GLFWwindow* window);
#endif

std::unique_ptr<PomRenderer> makeRenderer(GLFWwindow* window)
{
#if defined(POM1_HAS_METAL) && POM1_HAS_METAL
    return makeMetalRenderer(window);
#else
    return makeGlRenderer(window);
#endif
}

} // namespace pom1
