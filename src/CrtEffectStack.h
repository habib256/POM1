// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Universal CRT effect stack (ported from POM2). Takes an already-RGBA
// framebuffer — the output of ANY colour pipeline (the Apple-1 text screen,
// GEN2 HGR, TMS9918, GT-6144) — and applies composable post-effect layers on
// top: barrel geometry, brightness/contrast/saturation/hue, spatial
// sharpness, phosphor gamma + persistence, scanlines, a procedural shadow
// mask, vignette and post-glass luminance gain. This is what makes "CRT
// effects, on any card" possible.
//
// GL-only (the POM1 GL backend on Linux / Windows / WASM). On the macOS Metal
// backend there is no equivalent, so callers detect that (the renderer's
// glTextureName() returns 0) and present the raw framebuffer unchanged.
//
// Safety: opt-in. Pom1CrtEffects only routes a framebuffer through this when
// the user enables CRT effects; if the shader fails to compile / GL entry
// points are missing, available() stays false and the caller presents the
// raw framebuffer unchanged (graceful passthrough).

#ifndef POM1_CRT_EFFECT_STACK_H
#define POM1_CRT_EFFECT_STACK_H

#include <cstdint>
#include <string>

#include "CrtParams.h"

namespace pom1 {

class CrtEffectStack
{
public:
    CrtEffectStack();
    ~CrtEffectStack();
    CrtEffectStack(const CrtEffectStack&) = delete;
    CrtEffectStack& operator=(const CrtEffectStack&) = delete;

    // Compile the shader + allocate the fullscreen-quad VAO. Lazy texture/
    // FBO allocation happens on the first process() (we need the output
    // dimensions). Returns true on success; on any GL failure available()
    // stays false and process() becomes a no-op (returns 0).
    bool initialize();
    bool available() const { return ready; }

    void setParams(const CrtParams& p) { params = p; }
    const CrtParams& getParams() const { return params; }

    // Apply the enabled effect layers to RGBA source texture `srcTex`
    // (logical size srcW × srcH — drives the scanline/mask frequency), and
    // render the result at the on-screen target size dstW × dstH. Returns a
    // GL texture name (dstW × dstH), or 0 when not available().
    unsigned int process(unsigned int srcTex, int srcW, int srcH,
                         int dstW, int dstH);

    int outputWidth () const { return outW; }
    int outputHeight() const { return outH; }
    const std::string& lastError() const { return errorMsg; }

private:
    bool        ready       = false;
    bool        initialized = false;
    std::string errorMsg;

    unsigned int program      = 0;
    unsigned int outputTex[2] = {0, 0};   // ping-pong for persistence
    unsigned int fbo[2]       = {0, 0};
    unsigned int vao          = 0;
    unsigned int vbo          = 0;

    int uSrc         = -1;
    int uPrevFrame   = -1;
    int uSrcSize     = -1;
    int uOutSize     = -1;
    int uBrightness  = -1;
    int uContrast    = -1;
    int uSaturation  = -1;
    int uHue         = -1;
    int uSharpness   = -1;
    int uPersistence = -1;
    int uScanlines   = -1;
    int uBarrel      = -1;
    int uShadowMask  = -1;
    int uShadowStr   = -1;
    int uLuminanceGain = -1;
    int uCenterLighting = -1;
    int uPhosphorGamma = -1;

    int  outW = 0, outH = 0;
    int  srcW_ = 0, srcH_ = 0;
    int  pingPongIdx = 0;
    bool firstFrame  = true;

    CrtParams params{};

    bool createTextures(int w, int h);
};

} // namespace pom1

#endif // POM1_CRT_EFFECT_STACK_H
