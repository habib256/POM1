// VERHILLE Arnaud 2026

// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "CrtEffectStack.h"
#include "OpenGLShader.h"
#include "Logger.h"

#include <algorithm>
#include <string>

#if defined(POM1_HAS_METAL)
// ── macOS Metal backend ──────────────────────────────────────────────────
// The Metal build links no OpenGL framework (see CMakeLists — find_package
// (OpenGL) is skipped on the Metal path), so this GL-only effect stack cannot
// exist there. Provide inert stubs: initialize() fails, process() returns 0,
// and Pom1CrtEffects reports active()==false → the caller presents the raw
// framebuffer unchanged (the graceful passthrough the user opted into).
namespace pom1 {
CrtEffectStack::CrtEffectStack()  = default;
CrtEffectStack::~CrtEffectStack() = default;
bool CrtEffectStack::initialize()
{
    errorMsg = "OpenGL CRT shader unavailable on the Metal backend";
    return false;
}
bool CrtEffectStack::createTextures(int, int) { return false; }
unsigned int CrtEffectStack::process(unsigned int, int, int, int, int) { return 0; }
} // namespace pom1

#else // ── OpenGL / OpenGL-ES backend (Linux / Windows / WASM / macOS-GL) ──

#if defined(__EMSCRIPTEN__)
#  include <GLES3/gl3.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl3.h>
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#  include <GLFW/glfw3.h>

// Lazily-loaded GL 2.0+ entry points (Linux/Windows). Same dynamic-loader
// strategy as OpenGLShader.cpp — see there for the rationale. Kept
// file-local so shader compilation and the effect pass stay independent.
namespace {
PFNGLGENFRAMEBUFFERSPROC        glGenFramebuffers_        = nullptr;
PFNGLBINDFRAMEBUFFERPROC        glBindFramebuffer_        = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC   glFramebufferTexture2D_   = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus_ = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC     glDeleteFramebuffers_     = nullptr;
PFNGLGENVERTEXARRAYSPROC        glGenVertexArrays_        = nullptr;
PFNGLBINDVERTEXARRAYPROC        glBindVertexArray_        = nullptr;
PFNGLDELETEVERTEXARRAYSPROC     glDeleteVertexArrays_     = nullptr;
PFNGLGENBUFFERSPROC             glGenBuffers_             = nullptr;
PFNGLBINDBUFFERPROC             glBindBuffer_             = nullptr;
PFNGLBUFFERDATAPROC             glBufferData_             = nullptr;
PFNGLDELETEBUFFERSPROC          glDeleteBuffers_          = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_ = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC    glVertexAttribPointer_    = nullptr;
PFNGLUSEPROGRAMPROC             glUseProgram_             = nullptr;
PFNGLGETUNIFORMLOCATIONPROC     glGetUniformLocation_     = nullptr;
PFNGLUNIFORM1IPROC              glUniform1i_              = nullptr;
PFNGLUNIFORM1FPROC              glUniform1f_              = nullptr;
PFNGLUNIFORM2FPROC              glUniform2f_              = nullptr;
PFNGLACTIVETEXTUREPROC          glActiveTexture_          = nullptr;
bool entryPointsLoaded_ = false;
bool loadEntryPoints()
{
    if (entryPointsLoaded_) return true;
    auto get = [](const char* n) {
        return reinterpret_cast<void*>(glfwGetProcAddress(n));
    };
#define LOAD(t, v, n) v = reinterpret_cast<t>(get(n))
    LOAD(PFNGLGENFRAMEBUFFERSPROC,        glGenFramebuffers_,        "glGenFramebuffers");
    LOAD(PFNGLBINDFRAMEBUFFERPROC,        glBindFramebuffer_,        "glBindFramebuffer");
    LOAD(PFNGLFRAMEBUFFERTEXTURE2DPROC,   glFramebufferTexture2D_,   "glFramebufferTexture2D");
    LOAD(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus_, "glCheckFramebufferStatus");
    LOAD(PFNGLDELETEFRAMEBUFFERSPROC,     glDeleteFramebuffers_,     "glDeleteFramebuffers");
    LOAD(PFNGLGENVERTEXARRAYSPROC,        glGenVertexArrays_,        "glGenVertexArrays");
    LOAD(PFNGLBINDVERTEXARRAYPROC,        glBindVertexArray_,        "glBindVertexArray");
    LOAD(PFNGLDELETEVERTEXARRAYSPROC,     glDeleteVertexArrays_,     "glDeleteVertexArrays");
    LOAD(PFNGLGENBUFFERSPROC,             glGenBuffers_,             "glGenBuffers");
    LOAD(PFNGLBINDBUFFERPROC,             glBindBuffer_,             "glBindBuffer");
    LOAD(PFNGLBUFFERDATAPROC,             glBufferData_,             "glBufferData");
    LOAD(PFNGLDELETEBUFFERSPROC,          glDeleteBuffers_,          "glDeleteBuffers");
    LOAD(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray_, "glEnableVertexAttribArray");
    LOAD(PFNGLVERTEXATTRIBPOINTERPROC,    glVertexAttribPointer_,    "glVertexAttribPointer");
    LOAD(PFNGLUSEPROGRAMPROC,             glUseProgram_,             "glUseProgram");
    LOAD(PFNGLGETUNIFORMLOCATIONPROC,     glGetUniformLocation_,     "glGetUniformLocation");
    LOAD(PFNGLUNIFORM1IPROC,              glUniform1i_,              "glUniform1i");
    LOAD(PFNGLUNIFORM1FPROC,              glUniform1f_,              "glUniform1f");
    LOAD(PFNGLUNIFORM2FPROC,              glUniform2f_,              "glUniform2f");
    LOAD(PFNGLACTIVETEXTUREPROC,          glActiveTexture_,          "glActiveTexture");
#undef LOAD
    entryPointsLoaded_ =
        glGenFramebuffers_ && glBindFramebuffer_ && glFramebufferTexture2D_ &&
        glCheckFramebufferStatus_ && glDeleteFramebuffers_ &&
        glGenVertexArrays_ && glBindVertexArray_ && glDeleteVertexArrays_ &&
        glGenBuffers_ && glBindBuffer_ && glBufferData_ && glDeleteBuffers_ &&
        glEnableVertexAttribArray_ && glVertexAttribPointer_ &&
        glUseProgram_ && glGetUniformLocation_ &&
        glUniform1i_ && glUniform1f_ && glUniform2f_ && glActiveTexture_;
    return entryPointsLoaded_;
}
} // namespace
#  define glGenFramebuffers        glGenFramebuffers_
#  define glBindFramebuffer        glBindFramebuffer_
#  define glFramebufferTexture2D   glFramebufferTexture2D_
#  define glCheckFramebufferStatus glCheckFramebufferStatus_
#  define glDeleteFramebuffers     glDeleteFramebuffers_
#  define glGenVertexArrays        glGenVertexArrays_
#  define glBindVertexArray        glBindVertexArray_
#  define glDeleteVertexArrays     glDeleteVertexArrays_
#  define glGenBuffers             glGenBuffers_
#  define glBindBuffer             glBindBuffer_
#  define glBufferData             glBufferData_
#  define glDeleteBuffers          glDeleteBuffers_
#  define glEnableVertexAttribArray glEnableVertexAttribArray_
#  define glVertexAttribPointer    glVertexAttribPointer_
#  define glUseProgram             glUseProgram_
#  define glGetUniformLocation     glGetUniformLocation_
#  define glUniform1i              glUniform1i_
#  define glUniform1f              glUniform1f_
#  define glUniform2f              glUniform2f_
#  define glActiveTexture          glActiveTexture_
#endif

namespace pom1 {

#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
namespace { [[maybe_unused]] bool loadEntryPoints() { return true; } }
#endif

namespace {

const char* kVertexShader = R"GLSL(
in vec2 aPos;
out vec2 vUv;
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// Effect-only fragment shader. Input is an already-decoded RGBA framebuffer
// (any colour pipeline); we apply the same post-effects the OE shader does,
// in the same order, so the look is consistent whichever path produced the
// pixels. Hue is applied here too (chroma rotation on RGB); only the genuine
// demod-stage knobs (sharpness/PAL) don't appear here.
const char* kFragmentShader = R"GLSL(
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uSrc;        // RGBA Apple II framebuffer
uniform sampler2D uPrev;       // previous output (persistence)
uniform vec2  uSrcSize;        // (width, height) of uSrc
uniform vec2  uOutSize;        // (width, height) of this pass output
uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform float uHue;            // -0.5..+0.5 → ±π chroma rotation
uniform float uSharpness;      // 0.5 = neutral; >0.5 sharpen, <0.5 soften
uniform float uPersistence;
uniform float uScanlines;
uniform float uBarrel;
uniform int   uShadowMask;     // 0=off,1=triad,2=aperture grille,3=dot
uniform float uShadowStrength; // 0..1
uniform float uLuminanceGain;  // post-glass re-brighten, 1.0 = neutral
uniform float uCenterLighting; // vignette: 1.0 = flat (off), <1 darkens edges
uniform float uPhosphorGamma;  // CRT phosphor response γ: 1.0 = flat (off)

// Catmull-Rom cubic weight (4-tap per axis). Used when the CRT pass upscales
// the low-res Apple II framebuffer so scanlines/mask sit on smooth colour
// instead of NEAREST blocks.
float cubicWeight(float x)
{
    x = abs(x);
    if (x < 1.0) return x * x * (1.5 * x - 2.5) + 1.0;
    if (x < 2.0) return x * (x * (-0.5 * x + 2.5) - 4.0) + 2.0;
    return 0.0;
}

vec3 sampleSrc(vec2 uv)
{
    uv = clamp(uv, 0.0, 1.0);
    float mag = max(uOutSize.x / uSrcSize.x, uOutSize.y / uSrcSize.y);
    if (mag <= 1.25)
        return texture(uSrc, uv).rgb;

    vec2 coord = uv * uSrcSize - 0.5;
    vec2 f = fract(coord);
    coord = floor(coord);
    vec3 col = vec3(0.0);
    float wsum = 0.0;
    for (int j = -1; j <= 2; ++j) {
        for (int i = -1; i <= 2; ++i) {
            vec2 offs = vec2(float(i), float(j));
            vec2 samp = (coord + offs + 0.5) / uSrcSize;
            float w = cubicWeight(offs.x - f.x) * cubicWeight(offs.y - f.y);
            col += texture(uSrc, clamp(samp, 0.0, 1.0)).rgb * w;
            wsum += w;
        }
    }
    return col / max(wsum, 1e-4);
}

void main()
{
    // ── Barrel distortion ─────────────────────────────────────────
    vec2 cuv = vUv * 2.0 - 1.0;
    float r2 = dot(cuv, cuv);
    vec2 buv = cuv * (1.0 + uBarrel * r2);
    vec2 uv  = buv * 0.5 + 0.5;
    // Soft, analytic-AA border: fade to black across one output pixel at the
    // warped edge instead of a hard 1-pixel cutoff (which goes jaggy under
    // curvature). edgeMask is 1 inside, ramps to 0 right at the border.
    vec2  edge     = min(uv, 1.0 - uv);
    vec2  edgeFw   = max(fwidth(uv), vec2(1e-4));
    float edgeMask = clamp(min(edge.x / edgeFw.x, edge.y / edgeFw.y), 0.0, 1.0);
    vec3 rgb = sampleSrc(uv);

    // ── Sharpness (unsharp mask / soften, centre-neutral at 0.5) ──
    // The source is already-decoded RGB (any pipeline), so the OE shader's
    // chroma-bandwidth notion of "sharpness" has no meaning here; instead we
    // give the same knob a spatial meaning that works on ANY framebuffer:
    // 0.5 = passthrough, >0.5 sharpens (unsharp mask against a 4-tap cross
    // blur), <0.5 softens (blends toward that blur). The OE path forces this
    // neutral (its demod stage already did the chroma-bandwidth sharpness).
    {
        float amt = (uSharpness - 0.5) * 2.0;   // -1 (soft) .. +1 (sharp)
        if (amt != 0.0) {
            vec2 t = 1.0 / uSrcSize;
            vec3 blur = (
                sampleSrc(uv + vec2(-t.x, 0.0)) +
                sampleSrc(uv + vec2( t.x, 0.0)) +
                sampleSrc(uv + vec2(0.0, -t.y)) +
                sampleSrc(uv + vec2(0.0,  t.y))) * 0.25;
            rgb = clamp(rgb + amt * (rgb - blur), 0.0, 1.0);
        }
    }

    // ── Hue rotation ──────────────────────────────────────────────
    // The source is already RGB (any colour pipeline), so we rotate the
    // chroma the same way the OE demod does: RGB→YUV (BT.601), spin U/V by
    // uHue·π, YUV→RGB with the OpenEmulator decoder matrix. Same convention
    // as POM2's NTSC demodulator so the knob behaves identically there.
    if (uHue != 0.0) {
        float Y = dot(rgb, vec3( 0.299,    0.587,    0.114));
        float U = dot(rgb, vec3(-0.14713, -0.28886,  0.436));
        float V = dot(rgb, vec3( 0.615,   -0.51499, -0.10001));
        float a  = uHue * 3.14159265;
        float cs = cos(a), sn = sin(a);
        float Ur = U * cs - V * sn;
        float Vr = U * sn + V * cs;
        rgb = vec3(Y                 + 1.139883 * Vr,
                   Y - 0.394642 * Ur - 0.580622 * Vr,
                   Y + 2.032062 * Ur);
    }

    // ── Brightness / contrast / saturation ────────────────────────
    rgb = (rgb - 0.5) * uContrast + 0.5 + uBrightness;
    float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
    rgb = mix(vec3(luma), rgb, clamp(uSaturation, 0.0, 4.0));
    rgb = clamp(rgb, 0.0, 1.0);

    // ── Phosphor response curve (CRT gamma) ───────────────────────
    // Per-channel power law on the beam intensity → emitted light, applied
    // before the spatial scanline/mask modulation (which attenuates the light
    // the phosphor already produced). γ = 1.0 is identity, so the default
    // leaves every existing golden untouched; γ > 1 deepens shadows.
    if (uPhosphorGamma != 1.0) {
        rgb = pow(max(rgb, vec3(0.0)), vec3(uPhosphorGamma));
    }

    // ── Scanlines (smooth beam, analytic anti-alias) ──────────────
    // Logical scanline coordinate: 2 units per source row. fwidth() is how
    // many scanline-units one OUTPUT pixel spans. Where the barrel warp
    // compresses the picture (the curved edges) that rises past ~1 and a
    // hard scanline pattern would alias into moiré, so we fade the
    // modulation out exactly there. Because the pass now renders at the
    // native on-screen resolution (see CrtEffectStack::process), this
    // derivative is screen-pixel accurate — no resample beat downstream.
    float outRow = uv.y * (uSrcSize.y * 2.0);
    float rowFw  = max(fwidth(outRow), 1e-4);
    float scanAA = clamp(1.0 - (rowFw - 0.5) / 0.5, 0.0, 1.0); // 1 crisp → 0 alias
    float beam   = 0.5 + 0.5 * cos(3.14159265 * outRow);       // period 2, smooth
    rgb *= 1.0 - uScanlines * (1.0 - beam) * scanAA;

    // ── Shadow mask (procedural, analytic anti-alias) ─────────────
    if (uShadowMask != 0 && uShadowStrength > 0.0) {
        float oxBase = uv.x * (uSrcSize.x * 2.0);
        // Triad period is 3 units; as one output pixel approaches a whole
        // triad the mask is undersampled and would moiré, so fade it to
        // neutral there. Keeps the mask crisp where the picture has room.
        // Derivative taken on the base coord (before the dot-mask vertical
        // stagger) so a row-boundary jump doesn't spike fwidth.
        float maskFw   = max(fwidth(oxBase), 1e-4);
        float maskAA   = clamp(1.0 - (maskFw - 1.0) / 2.0, 0.0, 1.0);
        float ox = oxBase;
        if (uShadowMask == 3) {
            ox += (mod(floor(outRow * 0.5), 2.0) < 1.0) ? 0.0 : 1.5;
        }
        float strength = uShadowStrength * maskAA;
        int phase = int(mod(floor(ox), 3.0));
        // Lottes dark/light triplet (lottes.glsl): the lit channel is boosted
        // to maskLight and the two off-channels dimmed to maskDark, so the
        // triad preserves average luminance instead of crushing 2/3 channels
        // to black (the old pure-primary mask over-saturated and darkened).
        const float maskDark = 0.5, maskLight = 1.5;
        vec3 mask = vec3(maskDark);
        if      (phase == 0) mask.r = maskLight;
        else if (phase == 1) mask.g = maskLight;
        else                 mask.b = maskLight;
        vec3 atten = mix(vec3(1.0), mask, strength);
        if (uShadowMask == 1 || uShadowMask == 3) {
            // Triad/dot also gap horizontally — dim one row in three, gently.
            float vrow = mod(floor(outRow), 3.0);
            if (vrow < 1.0) atten *= mix(1.0, 0.7, strength);
        }
        rgb *= atten;
    }

    // ── Center lighting / vignette (OpenEmulator order: after mask) ───
    // lighting = cuv·(1/cl − 1); rgb *= exp(−dot(lighting)). cl = 1.0 → 0 →
    // exp(0) = 1 (flat, the OE Apple II default); lower cl darkens the edges.
    {
        vec2 lighting = cuv * (1.0 / uCenterLighting - 1.0);
        rgb *= exp(-dot(lighting, lighting));
    }

    // ── Luminance gain (post-glass) ───────────────────────────────
    // Re-brighten what scanlines/mask dimmed (OpenEmulator's luminanceGain).
    rgb *= uLuminanceGain;

    rgb *= edgeMask;

    // ── Persistence (CRT phosphor decay) ──────────────────────────
    // Applied LAST, on the final glass-corrected colour, feeding back the
    // final colour. When persistence ran before the scanline/mask multiply
    // (and fed back the post-glass output) the trail was re-attenuated by
    // the glass every frame, crushing the afterglow to near-invisible.
    // Decaying the displayed colour instead gives a clean exponential
    // afterglow that the slider visibly controls in every mode.
    // The -0.5/256 floor (OpenEmulator) drags faint trails all the way to
    // black in finite time instead of lingering forever at the quantization
    // step. (Slider stays a per-frame retention factor — POM2's documented
    // punchy model — rather than OE's seconds time-constant.)
    vec3 prev = texture(uPrev, vUv).rgb;
    rgb = max(rgb, prev * clamp(uPersistence, 0.0, 0.98) - 0.5 / 256.0);

    fragColor = vec4(rgb, 1.0);
}
)GLSL";

} // namespace

CrtEffectStack::CrtEffectStack() = default;
CrtEffectStack::~CrtEffectStack() = default;

bool CrtEffectStack::initialize()
{
    if (initialized) return ready;
    initialized = true;

#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
    if (!loadEntryPoints()) {
        errorMsg = "GL 3.x entry points unavailable";
        return false;
    }
#endif

    program = compileShaderProgram(kVertexShader, kFragmentShader, &errorMsg);
    if (!program) return false;

    uSrc         = glGetUniformLocation(program, "uSrc");
    uPrevFrame   = glGetUniformLocation(program, "uPrev");
    uSrcSize     = glGetUniformLocation(program, "uSrcSize");
    uOutSize     = glGetUniformLocation(program, "uOutSize");
    uBrightness  = glGetUniformLocation(program, "uBrightness");
    uContrast    = glGetUniformLocation(program, "uContrast");
    uSaturation  = glGetUniformLocation(program, "uSaturation");
    uHue         = glGetUniformLocation(program, "uHue");
    uSharpness   = glGetUniformLocation(program, "uSharpness");
    uPersistence = glGetUniformLocation(program, "uPersistence");
    uScanlines   = glGetUniformLocation(program, "uScanlines");
    uBarrel      = glGetUniformLocation(program, "uBarrel");
    uShadowMask  = glGetUniformLocation(program, "uShadowMask");
    uShadowStr   = glGetUniformLocation(program, "uShadowStrength");
    uLuminanceGain = glGetUniformLocation(program, "uLuminanceGain");
    uCenterLighting = glGetUniformLocation(program, "uCenterLighting");
    uPhosphorGamma = glGetUniformLocation(program, "uPhosphorGamma");

    const float verts[] = {
        -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
    };
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    ready = true;
    pom1::log().info("CRT", "Universal CRT effect stack ready");
    return true;
}

bool CrtEffectStack::createTextures(int w, int h)
{
    // w,h are the OUTPUT (on-screen) dimensions. Rendering the effect pass at
    // native screen resolution is what lets the scanline / shadow-mask
    // patterns be sampled finely enough to be analytically anti-aliased
    // (fwidth in the shader) instead of moiréing — and avoids a second
    // resample when ImGui blits the result 1:1.
    outW = w;
    outH = h;

    glGenFramebuffers(2, fbo);
    glGenTextures(2, outputTex);
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, outputTex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, outW, outH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, outputTex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            errorMsg = "FBO incomplete";
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(2, fbo);
            glDeleteTextures(2, outputTex);
            fbo[0] = fbo[1] = 0;
            outputTex[0] = outputTex[1] = 0;
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    firstFrame = true;
    return true;
}

unsigned int CrtEffectStack::process(unsigned int srcTex, int srcW, int srcH,
                                     int dstW, int dstH)
{
    if (!ready || srcTex == 0) return 0;

    dstW = std::max(1, dstW);
    dstH = std::max(1, dstH);

    // srcW_/srcH_ are the LOGICAL source dimensions (drive uSrcSize, i.e. the
    // scanline/mask frequency tied to source rows); outW/outH are the output
    // FBO size = the on-screen target. They are now decoupled.
    srcW_ = srcW;
    srcH_ = srcH;

    if (outputTex[0] == 0) {
        if (!createTextures(dstW, dstH)) { ready = false; return 0; }
    } else if (dstW != outW || dstH != outH) {
        // Window/zoom changed (or 80-col toggled) — resize the ping-pong pair.
        outW = dstW; outH = dstH;
        for (int i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, outputTex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, outW, outH, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        firstFrame = true;
    }

    // Save GL state so we don't disturb ImGui's render.
    int prevFbo = 0, prevViewport[4] = {0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);
    const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevCull  = glIsEnabled(GL_CULL_FACE);

    const int writeIdx = pingPongIdx;
    const int readIdx  = 1 - pingPongIdx;
    pingPongIdx = readIdx;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo[writeIdx]);
    glViewport(0, 0, outW, outH);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // The UI uploads screenTexture / OE demod with GL_NEAREST for crisp 1:1
    // integer scaling when CRT is off. For the effect pass we temporarily
    // switch to LINEAR so magnification is bilinear-smooth; the shader's
    // sampleSrc() adds bicubic when upscaling further.
    GLint prevMinFilter = GL_NEAREST;
    GLint prevMagFilter = GL_NEAREST;
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &prevMinFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &prevMagFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glUniform1i(uSrc, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, firstFrame ? srcTex : outputTex[readIdx]);
    glUniform1i(uPrevFrame, 1);

    if (uSrcSize     >= 0) glUniform2f(uSrcSize, float(srcW), float(srcH));
    if (uOutSize     >= 0) glUniform2f(uOutSize, float(outW), float(outH));
    if (uBrightness  >= 0) glUniform1f(uBrightness,  params.brightness);
    if (uContrast    >= 0) glUniform1f(uContrast,    params.contrast);
    if (uSaturation  >= 0) glUniform1f(uSaturation,  params.saturation);
    if (uHue         >= 0) glUniform1f(uHue,         params.hue);
    if (uSharpness   >= 0) glUniform1f(uSharpness,   params.sharpness);
    if (uPersistence >= 0) glUniform1f(uPersistence, params.persistence);
    if (uScanlines   >= 0) glUniform1f(uScanlines,   params.scanlines);
    if (uBarrel      >= 0) glUniform1f(uBarrel,      params.barrel);
    if (uShadowMask  >= 0) glUniform1i(uShadowMask,  static_cast<int>(params.shadowMask));
    if (uShadowStr   >= 0) glUniform1f(uShadowStr,   params.shadowMaskStrength);
    if (uLuminanceGain >= 0) glUniform1f(uLuminanceGain, params.luminanceGain);
    if (uCenterLighting >= 0) glUniform1f(uCenterLighting, params.centerLighting);
    if (uPhosphorGamma >= 0) glUniform1f(uPhosphorGamma, params.phosphorGamma);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Restore the caller's NEAREST filter (integer-scale path without CRT).
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, prevMinFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, prevMagFilter);

    // Don't leave our private textures bound on any unit.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevBlend) glEnable(GL_BLEND);      else glDisable(GL_BLEND);
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCull)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);

    firstFrame = false;
    return outputTex[writeIdx];
}

} // namespace pom1

#endif // POM1_HAS_METAL
