// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CrtEffectStackMetal — Metal implementation of the universal CRT effect
// stack. See CrtEffectStackMetal.h for the contract and the "keep in lockstep
// with the GLSL" rule.
//
// Shape of the pass, per framebuffer, per frame:
//
//   * one MTLRenderPipelineState (built once) drawing a fullscreen triangle
//     pair generated from [[vertex_id]] — no vertex buffer, no VAO analogue;
//   * two RGBA8 render targets ping-ponged so the persistence term can read
//     last frame's output while writing this one (identical to the GL FBO
//     pair);
//   * uniforms pushed with -setFragmentBytes: (80 bytes, well under the 4 KB
//     limit that makes a MTLBuffer unnecessary);
//   * its own command buffer, committed straight away.
//
// Why a private command buffer rather than the ImGui frame's: apply() runs
// while the UI is still being built, and the frame's buffer is not committed
// (hence not enqueued) until present(). Metal executes command buffers in
// ENQUEUE order, so committing here places this pass ahead of the ImGui draws
// that will sample its output. See PomRenderer::metalCommandQueue().
//
// ARC is OFF on this file (CMakeLists sets -fno-objc-arc, matching
// PomRenderer_Metal.mm), so every +1 from an alloc/new/copy method is
// released by hand. The audit is spelled out at each site.

#include "CrtEffectStackMetal.h"
#include "Logger.h"

#import <Metal/Metal.h>

#include <algorithm>

namespace pom1 {

namespace {

// Mirrors `struct CrtUniforms` in the MSL below, field for field. MSL gives
// float2 an 8-byte alignment, which puts srcSize at 0, outSize at 8 and the
// scalar block at 16 — exactly where the C++ layout lands them. The three
// trailing ints pad the struct to 80 bytes so the two languages agree on the
// size as well as the offsets.
struct CrtUniformsMetal {
    float srcSize[2];
    float outSize[2];
    float brightness;
    float contrast;
    float saturation;
    float hue;
    float sharpness;
    float persistence;
    float scanlines;
    float barrel;
    float shadowStrength;
    float luminanceGain;
    float centerLighting;
    float phosphorGamma;
    int   shadowMask;
    int   pad0, pad1, pad2;
};
static_assert(sizeof(CrtUniformsMetal) == 80,
              "CrtUniformsMetal must match the MSL struct byte for byte");

// ── The shader ───────────────────────────────────────────────────────────
// Line-by-line translation of kVertexShader / kFragmentShader in
// CrtEffectStack.cpp. Deliberate differences, all mechanical:
//
//   * texture(s, uv)  → tex.sample(smp, uv)
//   * mod(x, y)       → gmod(x, y). MSL's fmod() truncates toward zero and
//                       therefore disagrees with GLSL's floor-based mod for
//                       NEGATIVE arguments — which DO occur here, because the
//                       barrel warp pushes uv outside [0,1] near the corners
//                       and the mask/scanline phases are derived from it. A
//                       straight fmod() swap would misphase the shadow mask in
//                       exactly the four corners of the screen.
//   * UV origin       → Metal render targets are top-left origin (GL is
//                       bottom-left), so the vertex stage emits
//                       uv.y = (1 - pos.y) * 0.5 instead of pos.y * 0.5 + 0.5.
//                       Both end up with "uv.y == 0 is the top row of the
//                       image as ImGui draws it", which is what makes the
//                       source sampling and the persistence feedback line up.
//   * uniforms        → one constant struct instead of loose uniforms.
constexpr const char* kShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct CrtUniforms {
    float2 srcSize;        // logical source size — sets scanline/mask pitch
    float2 outSize;        // this pass's output size (= on-screen size)
    float  brightness;
    float  contrast;
    float  saturation;
    float  hue;            // -0.5..+0.5 -> +/-pi chroma rotation
    float  sharpness;      // 0.5 neutral; >0.5 sharpen, <0.5 soften
    float  persistence;
    float  scanlines;
    float  barrel;
    float  shadowStrength; // 0..1
    float  luminanceGain;  // post-glass re-brighten, 1.0 neutral
    float  centerLighting; // vignette: 1.0 flat (off), <1 darkens edges
    float  phosphorGamma;  // CRT phosphor response gamma, 1.0 flat (off)
    int    shadowMask;     // 0=off,1=triad,2=aperture grille,3=dot
    int    pad0, pad1, pad2;
};

struct VSOut {
    float4 position [[position]];
    float2 uv;
};

// Fullscreen quad as two triangles, straight out of the vertex id.
constant float2 kQuad[6] = {
    float2(-1.0, -1.0), float2( 1.0, -1.0), float2(-1.0,  1.0),
    float2(-1.0,  1.0), float2( 1.0, -1.0), float2( 1.0,  1.0)
};

vertex VSOut crtVertex(uint vid [[vertex_id]])
{
    const float2 p = kQuad[vid];
    VSOut o;
    o.position = float4(p, 0.0, 1.0);
    // Top-left origin: NDC y = +1 (top of the target) must map to uv.y = 0.
    o.uv = float2(p.x * 0.5 + 0.5, (1.0 - p.y) * 0.5);
    return o;
}

// GLSL-compatible modulo (floor-based), see the note above.
inline float gmod(float x, float y)
{
    return x - y * floor(x / y);
}

// Catmull-Rom cubic weight (4-tap per axis). Used when the CRT pass upscales
// the low-res framebuffer so scanlines/mask sit on smooth colour instead of
// NEAREST blocks.
inline float cubicWeight(float x)
{
    x = abs(x);
    if (x < 1.0) return x * x * (1.5 * x - 2.5) + 1.0;
    if (x < 2.0) return x * (x * (-0.5 * x + 2.5) - 4.0) + 2.0;
    return 0.0;
}

inline float3 sampleSrc(texture2d<float> src, sampler smp,
                        constant CrtUniforms& u, float2 uv)
{
    uv = clamp(uv, 0.0, 1.0);
    const float mag = max(u.outSize.x / u.srcSize.x, u.outSize.y / u.srcSize.y);
    if (mag <= 1.25)
        return src.sample(smp, uv).rgb;

    float2 coord = uv * u.srcSize - 0.5;
    const float2 f = fract(coord);
    coord = floor(coord);
    float3 col = float3(0.0);
    float wsum = 0.0;
    for (int j = -1; j <= 2; ++j) {
        for (int i = -1; i <= 2; ++i) {
            const float2 offs = float2(float(i), float(j));
            const float2 samp = (coord + offs + 0.5) / u.srcSize;
            const float w = cubicWeight(offs.x - f.x) * cubicWeight(offs.y - f.y);
            col  += src.sample(smp, clamp(samp, 0.0, 1.0)).rgb * w;
            wsum += w;
        }
    }
    return col / max(wsum, 1e-4);
}

fragment float4 crtFragment(VSOut in [[stage_in]],
                            texture2d<float> uSrc  [[texture(0)]],
                            texture2d<float> uPrev [[texture(1)]],
                            constant CrtUniforms& u [[buffer(0)]])
{
    // The GL path temporarily forces GL_LINEAR on the source for this pass
    // (the UI otherwise uploads it NEAREST for crisp integer scaling). Here
    // the sampler is baked into the shader, so it is linear by construction
    // and the source texture's own filtering is never disturbed.
    constexpr sampler smp(filter::linear,
                          mip_filter::none,
                          address::clamp_to_edge);

    // -- Barrel distortion -------------------------------------------
    const float2 cuv = in.uv * 2.0 - 1.0;
    const float  r2  = dot(cuv, cuv);
    const float2 buv = cuv * (1.0 + u.barrel * r2);
    const float2 uv  = buv * 0.5 + 0.5;
    // Soft, analytic-AA border: fade to black across one output pixel at the
    // warped edge instead of a hard 1-pixel cutoff (which goes jaggy under
    // curvature). edgeMask is 1 inside, ramps to 0 right at the border.
    const float2 edge     = min(uv, 1.0 - uv);
    const float2 edgeFw   = max(fwidth(uv), float2(1e-4));
    const float  edgeMask = clamp(min(edge.x / edgeFw.x, edge.y / edgeFw.y), 0.0, 1.0);
    float3 rgb = sampleSrc(uSrc, smp, u, uv);

    // -- Sharpness (unsharp mask / soften, centre-neutral at 0.5) -----
    // The source is already-decoded RGB (any pipeline), so the OE shader's
    // chroma-bandwidth notion of "sharpness" has no meaning here; instead we
    // give the same knob a spatial meaning that works on ANY framebuffer.
    {
        const float amt = (u.sharpness - 0.5) * 2.0;   // -1 (soft) .. +1 (sharp)
        if (amt != 0.0) {
            const float2 t = 1.0 / u.srcSize;
            const float3 blur = (
                sampleSrc(uSrc, smp, u, uv + float2(-t.x, 0.0)) +
                sampleSrc(uSrc, smp, u, uv + float2( t.x, 0.0)) +
                sampleSrc(uSrc, smp, u, uv + float2(0.0, -t.y)) +
                sampleSrc(uSrc, smp, u, uv + float2(0.0,  t.y))) * 0.25;
            rgb = clamp(rgb + amt * (rgb - blur), 0.0, 1.0);
        }
    }

    // -- Hue rotation -------------------------------------------------
    // RGB->YUV (BT.601), spin U/V by hue*pi, YUV->RGB with the OpenEmulator
    // decoder matrix. Same convention as POM2's NTSC demodulator.
    if (u.hue != 0.0) {
        const float Y = dot(rgb, float3( 0.299,    0.587,    0.114));
        const float U = dot(rgb, float3(-0.14713, -0.28886,  0.436));
        const float V = dot(rgb, float3( 0.615,   -0.51499, -0.10001));
        const float a  = u.hue * 3.14159265;
        const float cs = cos(a), sn = sin(a);
        const float Ur = U * cs - V * sn;
        const float Vr = U * sn + V * cs;
        rgb = float3(Y                 + 1.139883 * Vr,
                     Y - 0.394642 * Ur - 0.580622 * Vr,
                     Y + 2.032062 * Ur);
    }

    // -- Brightness / contrast / saturation ---------------------------
    rgb = (rgb - 0.5) * u.contrast + 0.5 + u.brightness;
    const float luma = dot(rgb, float3(0.299, 0.587, 0.114));
    rgb = mix(float3(luma), rgb, clamp(u.saturation, 0.0, 4.0));
    rgb = clamp(rgb, 0.0, 1.0);

    // -- Phosphor response curve (CRT gamma) --------------------------
    // Per-channel power law on beam intensity -> emitted light, applied
    // before the spatial scanline/mask modulation. gamma = 1.0 is identity.
    if (u.phosphorGamma != 1.0) {
        rgb = pow(max(rgb, float3(0.0)), float3(u.phosphorGamma));
    }

    // -- Scanlines (smooth beam, analytic anti-alias) -----------------
    // Logical scanline coordinate: 2 units per source row. fwidth() is how
    // many scanline-units one OUTPUT pixel spans; where the barrel warp
    // compresses the picture that rises past ~1 and a hard pattern would
    // moire, so the modulation fades out exactly there.
    const float outRow = uv.y * (u.srcSize.y * 2.0);
    const float rowFw  = max(fwidth(outRow), 1e-4);
    const float scanAA = clamp(1.0 - (rowFw - 0.5) / 0.5, 0.0, 1.0); // 1 crisp -> 0 alias
    const float beam   = 0.5 + 0.5 * cos(3.14159265 * outRow);       // period 2, smooth
    rgb *= 1.0 - u.scanlines * (1.0 - beam) * scanAA;

    // -- Shadow mask (procedural, analytic anti-alias) ----------------
    if (u.shadowMask != 0 && u.shadowStrength > 0.0) {
        const float oxBase = uv.x * (u.srcSize.x * 2.0);
        // Triad period is 3 units; as one output pixel approaches a whole
        // triad the mask is undersampled and would moire, so fade it to
        // neutral there. Derivative taken on the base coord (before the
        // dot-mask vertical stagger) so a row-boundary jump doesn't spike it.
        const float maskFw = max(fwidth(oxBase), 1e-4);
        const float maskAA = clamp(1.0 - (maskFw - 1.0) / 2.0, 0.0, 1.0);
        float ox = oxBase;
        if (u.shadowMask == 3) {
            ox += (gmod(floor(outRow * 0.5), 2.0) < 1.0) ? 0.0 : 1.5;
        }
        const float strength = u.shadowStrength * maskAA;
        const int phase = int(gmod(floor(ox), 3.0));
        // Lottes dark/light triplet: the lit channel is boosted to maskLight
        // and the two off-channels dimmed to maskDark, so the triad preserves
        // average luminance instead of crushing 2/3 channels to black.
        const float maskDark = 0.5, maskLight = 1.5;
        float3 mask = float3(maskDark);
        if      (phase == 0) mask.r = maskLight;
        else if (phase == 1) mask.g = maskLight;
        else                 mask.b = maskLight;
        float3 atten = mix(float3(1.0), mask, strength);
        if (u.shadowMask == 1 || u.shadowMask == 3) {
            // Triad/dot also gap horizontally — dim one row in three, gently.
            const float vrow = gmod(floor(outRow), 3.0);
            if (vrow < 1.0) atten *= mix(1.0, 0.7, strength);
        }
        rgb *= atten;
    }

    // -- Center lighting / vignette (OpenEmulator order: after mask) ---
    // lighting = cuv*(1/cl - 1); rgb *= exp(-dot(lighting)). cl = 1.0 -> 0 ->
    // exp(0) = 1 (flat); lower cl darkens the edges.
    {
        const float2 lighting = cuv * (1.0 / u.centerLighting - 1.0);
        rgb *= exp(-dot(lighting, lighting));
    }

    // -- Luminance gain (post-glass) ----------------------------------
    rgb *= u.luminanceGain;

    rgb *= edgeMask;

    // -- Persistence (CRT phosphor decay) -----------------------------
    // Applied LAST, on the final glass-corrected colour, feeding back the
    // final colour — decaying the DISPLAYED colour gives a clean exponential
    // afterglow the slider visibly controls. The -0.5/256 floor
    // (OpenEmulator) drags faint trails to black in finite time instead of
    // lingering forever at the quantization step.
    const float3 prev = uPrev.sample(smp, in.uv).rgb;
    rgb = max(rgb, prev * clamp(u.persistence, 0.0, 0.98) - 0.5 / 256.0);

    return float4(rgb, 1.0);
}
)MSL";

} // namespace

CrtEffectStackMetal::CrtEffectStackMetal()  = default;

CrtEffectStackMetal::~CrtEffectStackMetal()
{
    releaseTextures();
    if (pipeline_) {
        [(id<MTLRenderPipelineState>)pipeline_ release];
        pipeline_ = nullptr;
    }
}

bool CrtEffectStackMetal::initialize(void* device, void* commandQueue)
{
    if (ready_) return true;
    if (!device || !commandQueue) {
        errorMsg_ = "no Metal device / command queue";
        return false;
    }
    device_       = device;          // borrowed — the renderer owns these
    commandQueue_ = commandQueue;

    @autoreleasepool {
        id<MTLDevice> dev = (id<MTLDevice>)device_;

        NSError* err = nil;
        NSString* src = [NSString stringWithUTF8String:kShaderSource];
        // -newLibraryWithSource: is +1 (the "new" prefix), released below once
        // the two function objects have been pulled out of it.
        id<MTLLibrary> lib = [dev newLibraryWithSource:src
                                               options:nil
                                                 error:&err];
        if (!lib) {
            errorMsg_ = err ? [[err localizedDescription] UTF8String]
                            : "MSL compile failed";
            pom1::log().warn("CRT", "Metal shader compile failed: " + errorMsg_);
            return false;
        }

        // -newFunctionWithName: is also +1.
        id<MTLFunction> vs = [lib newFunctionWithName:@"crtVertex"];
        id<MTLFunction> fs = [lib newFunctionWithName:@"crtFragment"];
        if (!vs || !fs) {
            errorMsg_ = "CRT shader entry points not found";
            [vs release]; [fs release]; [lib release];
            pom1::log().warn("CRT", errorMsg_);
            return false;
        }

        MTLRenderPipelineDescriptor* desc =
            [[MTLRenderPipelineDescriptor alloc] init];   // +1
        desc.vertexFunction   = vs;
        desc.fragmentFunction = fs;
        // Must match the render targets allocated in createTextures(), and
        // RGBA8Unorm is also what PomRenderer_Metal gives every POM1 texture,
        // so ImGui samples our output through exactly the same path.
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        // No blending: the pass writes opaque, fully-covering geometry.
        desc.colorAttachments[0].blendingEnabled = NO;

        id<MTLRenderPipelineState> pso =
            [dev newRenderPipelineStateWithDescriptor:desc error:&err];   // +1

        [desc release];
        [vs   release];
        [fs   release];
        [lib  release];

        if (!pso) {
            errorMsg_ = err ? [[err localizedDescription] UTF8String]
                            : "pipeline state creation failed";
            pom1::log().warn("CRT", "Metal pipeline failed: " + errorMsg_);
            return false;
        }

        pipeline_ = (void*)pso;   // keep the +1; released in the destructor
    }

    ready_ = true;
    pom1::log().info("CRT", "Universal CRT effect stack ready (Metal)");
    return true;
}

void CrtEffectStackMetal::releaseTextures()
{
    for (int i = 0; i < 2; ++i) {
        if (outputTex_[i]) {
            [(id<MTLTexture>)outputTex_[i] release];
            outputTex_[i] = nullptr;
        }
    }
}

bool CrtEffectStackMetal::createTextures(int w, int h)
{
    // w,h are the OUTPUT (on-screen) dimensions. Rendering the effect pass at
    // native screen resolution is what lets the scanline / shadow-mask
    // patterns be sampled finely enough for the fwidth() anti-aliasing to
    // work — and avoids a second resample when ImGui blits the result 1:1.
    releaseTextures();
    outW_ = w;
    outH_ = h;

    @autoreleasepool {
        id<MTLDevice> dev = (id<MTLDevice>)device_;
        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:(NSUInteger)outW_
                                        height:(NSUInteger)outH_
                                     mipmapped:NO];
        // Written by our render pass, then read twice: by ImGui when it draws
        // the result, and by the NEXT frame's persistence tap.
        desc.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;   // GPU-only, never mapped

        for (int i = 0; i < 2; ++i) {
            id<MTLTexture> t = [dev newTextureWithDescriptor:desc];   // +1
            if (!t) {
                errorMsg_ = "CRT render target allocation failed";
                releaseTextures();
                return false;
            }
            outputTex_[i] = (void*)t;
        }
    }

    firstFrame_ = true;
    return true;
}

void* CrtEffectStackMetal::process(void* srcTexture, int srcW, int srcH,
                                   int dstW, int dstH)
{
    if (!ready_ || !srcTexture) return nullptr;

    dstW = std::max(1, dstW);
    dstH = std::max(1, dstH);

    if (!outputTex_[0]) {
        if (!createTextures(dstW, dstH)) { ready_ = false; return nullptr; }
    } else if (dstW != outW_ || dstH != outH_) {
        // Window/zoom changed — reallocate the ping-pong pair. (Metal textures
        // are immutable in size, so unlike GL's glTexImage2D respecify this is
        // a destroy + create; firstFrame_ is reset either way, which discards
        // the stale persistence tap that would otherwise smear across resizes.)
        if (!createTextures(dstW, dstH)) { ready_ = false; return nullptr; }
    }

    const int writeIdx = pingPongIdx_;
    const int readIdx  = 1 - pingPongIdx_;
    pingPongIdx_ = readIdx;

    id<MTLTexture> outTex = (id<MTLTexture>)outputTex_[writeIdx];

    @autoreleasepool {
        id<MTLCommandQueue> queue = (id<MTLCommandQueue>)commandQueue_;
        id<MTLCommandBuffer> cb = [queue commandBuffer];   // autoreleased
        if (!cb) return nullptr;
        cb.label = @"POM1 CRT effect";

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture     = outTex;
        // The quad covers every pixel, so the clear value is never observed —
        // but DontCare on a private texture would leave the contents undefined
        // for tile-based deferred renderers if the draw were ever culled.
        // Clearing is free on Apple GPUs and removes that class of surprise.
        pass.colorAttachments[0].loadAction  = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor  = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> enc =
            [cb renderCommandEncoderWithDescriptor:pass];   // autoreleased
        if (!enc) return nullptr;
        [enc setRenderPipelineState:(id<MTLRenderPipelineState>)pipeline_];

        [enc setFragmentTexture:(id<MTLTexture>)srcTexture atIndex:0];
        // First frame has no previous output to decay from — feed the source
        // itself, exactly like the GL path, so the persistence term starts
        // from a sane image instead of an uninitialised target.
        [enc setFragmentTexture:(firstFrame_ ? (id<MTLTexture>)srcTexture
                                             : (id<MTLTexture>)outputTex_[readIdx])
                        atIndex:1];

        CrtUniformsMetal u{};
        u.srcSize[0]     = float(srcW);
        u.srcSize[1]     = float(srcH);
        u.outSize[0]     = float(outW_);
        u.outSize[1]     = float(outH_);
        u.brightness     = params_.brightness;
        u.contrast       = params_.contrast;
        u.saturation     = params_.saturation;
        u.hue            = params_.hue;
        u.sharpness      = params_.sharpness;
        u.persistence    = params_.persistence;
        u.scanlines      = params_.scanlines;
        u.barrel         = params_.barrel;
        u.shadowStrength = params_.shadowMaskStrength;
        u.luminanceGain  = params_.luminanceGain;
        u.centerLighting = params_.centerLighting;
        u.phosphorGamma  = params_.phosphorGamma;
        u.shadowMask     = static_cast<int>(params_.shadowMask);
        [enc setFragmentBytes:&u length:sizeof(u) atIndex:0];

        // The attachment is exactly outW_ × outH_, so this restates the
        // default — but stating it keeps the pass correct if a future caller
        // ever renders into a larger shared target. Field-by-field rather
        // than a compound literal: those are a C construct that clang only
        // tolerates in C++ as an extension.
        MTLViewport vp;
        vp.originX = 0.0;
        vp.originY = 0.0;
        vp.width   = double(outW_);
        vp.height  = double(outH_);
        vp.znear   = 0.0;
        vp.zfar    = 1.0;
        [enc setViewport:vp];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
        [enc endEncoding];

        // Commit (and thereby enqueue) now: this is the gap between the ImGui
        // frame's beginFrame() and its present(), so the queue orders this
        // pass strictly before the draws that sample outTex.
        [cb commit];
    }

    firstFrame_ = false;
    return (void*)outTex;
}

} // namespace pom1
