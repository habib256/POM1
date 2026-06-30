// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// PomRenderer — thin opaque graphics-backend seam.
//
// POM1 talks to its rendering backend through this interface so the per-
// platform GL/Metal differences live in a single place. Phase 1 ships only
// the OpenGL implementation (`PomRenderer_GL.cpp`) — every existing gl*
// call in main_imgui.cpp / Screen_ImGui / MainWindow_HardwareWindows /
// MainWindow_Dialogs moves behind these methods with NO behaviour change.
// Phase 2 adds `PomRenderer_Metal.mm` for macOS and main_imgui picks the
// right factory at boot. WASM stays on OpenGL forever (Metal isn't a thing
// in browsers, and WebGL2 maps cleanly onto the GL backend).
//
// Lifecycle:
//   1. After GLFW creates the window, main_imgui.cpp calls
//      `pom1::makeRenderer(window)` once and stores the result in a
//      global accessible via `pom1::renderer()` (see below). The factory
//      makes the context current (GL) or attaches the CAMetalLayer (Metal).
//   2. `initImGuiBackend(glsl_version)` wires `ImGui_ImplOpenGL3_Init` or
//      `ImGui_ImplMetal_Init`. Called once after `ImGui::CreateContext`.
//   3. Each frame the loop calls beginFrame → newImGuiFrame → clear →
//      renderDrawData → present.
//   4. `shutdownImGuiBackend` mirrors step 2 on quit.
//
// Threading: every method runs on the GL/main thread (where the GL context
// is current). POM1 already enforces that by keeping render() on a single
// thread; the renderer makes no extra promises.

#ifndef POM1_RENDERER_H
#define POM1_RENDERER_H

#include "imgui.h"        // ImTextureID + ImDrawData typedefs

#include <cstdint>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace pom1 {

/// Opaque texture handle. Defined out-of-line in PomRenderer.cpp so every
/// backend agrees on the layout (one of each underlying field is used per
/// backend; the others are zero-init). Callers only ever see Texture*.
struct Texture;

class PomRenderer {
public:
    enum class Filter { Nearest, Linear };

    virtual ~PomRenderer() = default;

    // ── Texture lifecycle ───────────────────────────────────────────────
    //
    // `createTexture` allocates a w×h RGBA8 surface with CLAMP_TO_EDGE wrap
    // (the only mode any POM1 site uses today). `pixels` is optional; pass
    // nullptr to allocate empty and fill via `updateTexture` later (matches
    // the HGR/TMS9918/GT6144 framebuffer pattern, where the pixels are
    // produced by the emulator each frame).
    //
    // `updateTexture` does a full-area replace. Caller guarantees `pixels`
    // matches the texture's creation w×h. Cheap on GL (glTexSubImage2D);
    // on Metal it goes through replaceRegion:.
    //
    // `destroyTexture` is null-safe.
    //
    // `asImTextureID` produces the value to hand to ImGui::Image. On GL it
    // casts the GLuint via uintptr_t; on Metal it pokes through a bridged
    // pointer.
    //
    // Backend-quirk note on Filter: the GL backend honours it via
    // glTexParameteri (Nearest = crisp pixel art; Linear = bilinear). The
    // Metal backend currently DROPS the parameter — imgui_impl_metal.mm
    // binds a single sampler (linear) into its pipeline state, and
    // ImDrawCmd::UserCallback can't reach the encoder from outside.
    // Pixel-art surfaces created with Filter::Nearest therefore sample
    // bilinear on macOS-Metal. See the TODO inside PomRenderer_Metal.mm
    // ::createTexture for the path forward (per-cmd sampler hook).
    virtual Texture* createTexture(int w, int h, Filter,
                                   const uint32_t* pixels = nullptr) = 0;
    virtual void     updateTexture(Texture*, const uint32_t* pixels) = 0;
    virtual void     destroyTexture(Texture*) = 0;
    virtual ImTextureID asImTextureID(const Texture*) const = 0;

    /// Texture dimensions, or 0 when `t == nullptr`. Lets callers (notably
    /// the paint hosts) decide whether an incoming RGBA buffer can be sent
    /// through `updateTexture` (cheap glTexSubImage2D / replaceRegion) or
    /// whether the texture has to be reallocated via destroy+create.
    virtual int  textureWidth(const Texture* t)  const = 0;
    virtual int  textureHeight(const Texture* t) const = 0;

    // ── ImGui backend lifecycle ────────────────────────────────────────
    //
    // `initImGuiBackend` wires the ImGui rendering backend after
    // ImGui_ImplGlfw_Init*. `glslVersion` is only used by the GL impl
    // (Metal ignores it). Returns false on failure.
    virtual bool initImGuiBackend(const char* glslVersion) = 0;
    virtual void shutdownImGuiBackend() = 0;

    // ── Per-frame methods ──────────────────────────────────────────────
    //
    // `beginFrame` wraps the impl_NewFrame call (GL: zero work; Metal:
    // grab the next CAMetalDrawable + start a command buffer). It must
    // run BEFORE `ImGui::NewFrame()` — same ordering as the existing code.
    //
    // `clear` clears the back-buffer with the supplied colour and resets
    // the viewport to `fbW × fbH`. fbW/fbH are framebuffer-pixel sizes
    // (post-DPI scale) — `glfwGetFramebufferSize` on desktop.
    //
    // `renderDrawData` submits the ImGui draw list to the backend. POM1
    // calls it after `ImGui::Render()`.
    //
    // `present` swaps the back-buffer to the screen (glfwSwapBuffers on
    // GL; presentDrawable: + commit on Metal).
    virtual void beginFrame() = 0;
    virtual void clear(int fbW, int fbH,
                       float r, float g, float b, float a) = 0;
    virtual void renderDrawData(ImDrawData*) = 0;
    virtual void present() = 0;

    // Read the just-rendered back-buffer (between renderDrawData and
    // present). Returns true and fills outW/outH/outPixels (top-down
    // RGBA8) on success. Used only by the Terminal Card screenshot path
    // — Metal will route through a staging texture blit.
    virtual bool readBackbufferRGBA(int& outW, int& outH,
                                    std::vector<uint8_t>& outPixels) = 0;
};

/// Construct the per-platform renderer. Today: always returns the GL
/// backend. Phase 2 will switch on POM1_RENDERER_METAL / __APPLE__.
std::unique_ptr<PomRenderer> makeRenderer(GLFWwindow* window);

/// Global accessor — set by main_imgui.cpp right after `makeRenderer`.
/// Every TU that uploads a texture (Screen_ImGui, MainWindow_*,
/// future Metal-aware paint hosts) goes through this rather than
/// threading a renderer pointer everywhere.
///
/// Returns nullptr only when the binary runs headless (no GLFW window,
/// no GL context) — callers that touch textures must be inside the
/// non-headless render path so this is never observed null in practice.
PomRenderer*  renderer();
void          setRenderer(PomRenderer*);   // main_imgui.cpp wires this once

} // namespace pom1

#endif // POM1_RENDERER_H
