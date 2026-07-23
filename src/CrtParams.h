// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CrtParams — the knob set driving the universal CRT effect stack
// (CrtEffectStack). Ported from POM2's NtscParams, trimmed to the fields
// the effect-only shader actually consumes: POM1 has no NTSC composite
// demodulator, so the demod-only knobs (palMode / textSharp) are dropped.
//
// One shared CrtParams (owned by MainWindow, persisted under ini/ui.settings)
// drives every framebuffer's per-slot CrtEffectStack — see Pom1CrtEffects.

#ifndef POM1_CRT_PARAMS_H
#define POM1_CRT_PARAMS_H

namespace pom1 {

struct CrtParams
{
    // Standard composite-TV knobs.
    float brightness  = 0.0f;   // -0.5..+0.5 added to luma
    float contrast    = 1.0f;   //  0.5..1.5 scaling around 0.5
    float saturation  = 1.0f;   //  0..2 chroma multiplier
    float hue         = 0.0f;   // -0.5..+0.5 chroma rotation

    // Spatial sharpness (unsharp-mask / soften); 0.5 = neutral passthrough,
    // >0.5 sharpens, <0.5 softens. On an already-decoded RGB framebuffer this
    // is a purely spatial operation (no chroma-bandwidth meaning).
    float sharpness   = 0.5f;

    // Phosphor persistence (temporal afterglow): 0 = none, ~0.3..0.6 CRT-ish.
    float persistence = 0.4f;

    // Pure post-effects.
    float scanlines   = 0.25f;  // 0 = off, 1 = black between every line
    float barrel      = 0.05f;  // 0 = flat, 0.2..0.3 = old curved CRT

    // Shadow-mask emulation (procedural, no texture upload; free when Off).
    enum class ShadowMask : int {
        Off            = 0,
        Triad          = 1,   // classic 3-stripe shadow mask
        ApertureGrille = 2,   // Trinitron vertical stripes
        Dot            = 3,   // offset triads (consumer CRT)
    };
    ShadowMask shadowMask         = ShadowMask::Off;
    float      shadowMaskStrength = 0.5f;  // 0..1

    // Post-glass luminance gain — re-brightens what scanlines + mask dim
    // (OpenEmulator's luminanceGain). 1.0 = neutral.
    float luminanceGain = 1.0f;  // 1.0..2.0

    // Center lighting / vignette (OpenEmulator-faithful): 1.0 = flat (off),
    // lower darkens the edges.
    float centerLighting = 1.0f; // 0.5..1.0 (1.0 = flat)

    // Phosphor response curve (CRT gamma): rgb = rgb^gamma. 1.0 = identity.
    float phosphorGamma = 1.0f;  // 0.6..2.6 (1.0 = flat)
};

} // namespace pom1

#endif // POM1_CRT_PARAMS_H
