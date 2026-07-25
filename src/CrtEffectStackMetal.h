// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CrtEffectStackMetal — the macOS/Metal twin of CrtEffectStack.
//
// Same contract, same CrtParams, same visual result; only the plumbing under
// it differs. CrtEffectStack is written against GL entry points and cannot be
// made to work on the Metal backend (which links no OpenGL framework at all —
// see CMakeLists), so on macOS it compiles down to inert stubs and this class
// takes over. Pom1CrtEffects owns one or the other per slot and hides the
// choice from every caller.
//
// The fragment shader is a line-by-line MSL translation of the GLSL one in
// CrtEffectStack.cpp — same effect order (barrel → sharpness → hue → BCS →
// phosphor γ → scanlines → shadow mask → vignette → luminance gain → edge
// mask → persistence), same constants, same analytic anti-aliasing via
// fwidth(). Keep the two in lockstep: a knob added to one must be added to
// the other or macOS silently diverges from every other platform.
//
// This header is deliberately plain C++ (void* for every Metal object) so
// Pom1CrtEffects.cpp — an ordinary .cpp — can hold one without becoming
// Objective-C++. The implementation lives in CrtEffectStackMetal.mm, compiled
// only when POM1_USE_METAL is on.
//
// Threading: same rule as the GL stack — every method runs on the render
// thread.

#ifndef POM1_CRT_EFFECT_STACK_METAL_H
#define POM1_CRT_EFFECT_STACK_METAL_H

#include <string>

#include "CrtParams.h"

namespace pom1 {

class CrtEffectStackMetal
{
public:
    CrtEffectStackMetal();
    ~CrtEffectStackMetal();
    CrtEffectStackMetal(const CrtEffectStackMetal&) = delete;
    CrtEffectStackMetal& operator=(const CrtEffectStackMetal&) = delete;

    // Compile the MSL library + build the render pipeline state. `device` and
    // `commandQueue` are unretained borrows of the renderer's objects
    // (PomRenderer::metalDevice() / metalCommandQueue()) and must outlive this
    // object — they do: the renderer is torn down after the UI.
    //
    // The ping-pong render targets are allocated lazily on the first process()
    // (we need the output size). Returns true on success; on failure
    // available() stays false and process() returns nullptr, which
    // Pom1CrtEffects reads as "present the raw framebuffer".
    bool initialize(void* device, void* commandQueue);
    bool available() const { return ready_; }

    void setParams(const CrtParams& p) { params_ = p; }
    const CrtParams& getParams() const { return params_; }

    // Route source id<MTLTexture> `srcTexture` (logical size srcW × srcH,
    // which drives the scanline / shadow-mask frequency) through the effect
    // pass and render at the on-screen size dstW × dstH. Returns the output
    // id<MTLTexture> as void* — hand it to ImGui through ImTextureID exactly
    // like any POM1 texture — or nullptr when not available().
    //
    // Encodes onto its own command buffer and commits immediately. See the
    // ordering note on PomRenderer::metalCommandQueue(): committing here, in
    // the gap between beginFrame() and present(), puts this pass ahead of the
    // ImGui frame in the queue's enqueue order.
    void* process(void* srcTexture, int srcW, int srcH, int dstW, int dstH);

    int outputWidth()  const { return outW_; }
    int outputHeight() const { return outH_; }
    const std::string& lastError() const { return errorMsg_; }

private:
    bool createTextures(int w, int h);
    void releaseTextures();

    bool        ready_ = false;
    std::string errorMsg_;

    // All Metal objects, type-punned. Retained/released manually (this file's
    // .mm is compiled -fno-objc-arc, matching PomRenderer_Metal.mm).
    void* device_       = nullptr;   // borrowed, not retained
    void* commandQueue_ = nullptr;   // borrowed, not retained
    void* pipeline_     = nullptr;   // id<MTLRenderPipelineState>, retained
    void* outputTex_[2] = {nullptr, nullptr};  // ping-pong, retained

    int  outW_ = 0, outH_ = 0;
    int  pingPongIdx_ = 0;
    bool firstFrame_  = true;

    CrtParams params_{};
};

} // namespace pom1

#endif // POM1_CRT_EFFECT_STACK_METAL_H
