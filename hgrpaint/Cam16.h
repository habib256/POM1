// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CAM16-UCS perceptual colour space — the metric ii-pix uses for high-quality
// image→Apple II conversion. Distances in CAM16-UCS (J', a', b') match human
// colour perception far better than RGB or even CIELAB, so "nearest colour"
// picks during dithering look right.
//
// Pure + emulator-agnostic (part of the portable hgrpaint/ toolkit). Forward
// model + viewing conditions follow Material Color Utilities (a well-tested CAM16
// implementation): D65 white, La≈11.73, Yb≈18.4, average surround.

#ifndef HGRPAINT_CAM16_H
#define HGRPAINT_CAM16_H

namespace hgrpaint {

// A colour in CAM16-UCS: J' (lightness), a'/b' (opponent chroma axes).
// Euclidean distance between two of these is a perceptual difference.
struct Cam16Ucs { float J, a, b; };

// Convert an sRGB colour (each channel 0..1, gamma-encoded) to CAM16-UCS.
Cam16Ucs srgbToCam16Ucs(float r, float g, float b);

// Same, from LINEAR-light sRGB components (0..1, already de-gammaed) — for the
// importers, which resample + error-diffuse in linear RGB and convert their
// per-byte/cell effective targets once for perceptual scoring.
Cam16Ucs linearSrgbToCam16Ucs(float r, float g, float b);

// Convenience: 8-bit sRGB channels (0..255).
Cam16Ucs srgb8ToCam16Ucs(int r, int g, int b);

// Local Jacobian d(J',a',b')/d(r,g,b) of the CAM16-UCS forward model at a
// LINEAR sRGB point, fetched from a precomputed sqrt-indexed 17³ lattice
// (built lazily, once per process). The importers keep their in-candidate
// error walk in linear RGB (ii-pix semantics — walking in CAM16 space breaks
// tone conservation) and score the walked target as
//     cam(want + d) ≈ cam(want) + J·d
// for the small diffusion offsets d.
struct Cam16Jac { float m[9]; };   // row-major: dJ/dr dJ/dg dJ/db, da/…, db/…
const Cam16Jac& cam16JacobianAt(float r, float g, float b);

// Squared perceptual distance between two CAM16-UCS colours.
inline float cam16DistSq(const Cam16Ucs& p, const Cam16Ucs& q)
{
    const float dJ = p.J - q.J, da = p.a - q.a, db = p.b - q.b;
    return dJ * dJ + da * da + db * db;
}

} // namespace hgrpaint

#endif // HGRPAINT_CAM16_H
