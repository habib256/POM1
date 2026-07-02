// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CAM16-UCS forward model — see Cam16.h. Follows Material Color Utilities'
// well-tested CAM16 implementation (the same maths Android's "Material You" uses),
// with its default viewing conditions.

#include "Cam16.h"

#include <algorithm>
#include <cmath>

namespace hgrpaint {
namespace {

constexpr double kPi = 3.14159265358979323846;

// sRGB component → linear light (0..1).
inline double srgbToLinear(double c)
{
    return (c <= 0.04045) ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// Precomputed viewing conditions (Material Color Utilities defaults: D65 white,
// La≈11.73, Yb≈18.4, average surround, no illuminant discounting).
struct ViewingConditions {
    double aw, nbb, ncb, c, nc, fl, fLRoot, z, n;
    double rgbD[3];
};

ViewingConditions makeDefault()
{
    // White point (D65) in XYZ, Y scaled to 100.
    const double Xw = 95.047, Yw = 100.0, Zw = 108.883;
    const double La = 11.725676537;     // 200/π · Y(L*=50)/100
    const double Yb = 18.418651851;     // Y(L*=50)
    const double F = 1.0, c = 0.69, Nc = 1.0;   // average surround

    // CAT16 cone responses of the white point.
    const double rW =  0.401288 * Xw + 0.650173 * Yw - 0.051461 * Zw;
    const double gW = -0.250268 * Xw + 1.204414 * Yw + 0.045854 * Zw;
    const double bW = -0.002079 * Xw + 0.048952 * Yw + 0.953127 * Zw;

    const double n = Yb / Yw;
    const double z = 1.48 + std::sqrt(n);
    const double nbb = 0.725 * std::pow(1.0 / n, 0.2);
    const double ncb = nbb;

    const double k = 1.0 / (5.0 * La + 1.0);
    const double k4 = k * k * k * k;
    const double fl = k4 * La + 0.1 * (1.0 - k4) * (1.0 - k4) * std::cbrt(5.0 * La);

    const double d = std::min(1.0, std::max(0.0,
        F * (1.0 - (1.0 / 3.6) * std::exp((-La - 42.0) / 92.0))));
    ViewingConditions vc;
    vc.rgbD[0] = d * 100.0 / rW + 1.0 - d;
    vc.rgbD[1] = d * 100.0 / gW + 1.0 - d;
    vc.rgbD[2] = d * 100.0 / bW + 1.0 - d;
    vc.fl = fl;
    vc.fLRoot = std::pow(fl, 0.25);
    vc.n = n;
    vc.z = z;
    vc.nbb = nbb;
    vc.ncb = ncb;
    vc.c = c;
    vc.nc = Nc;

    auto adapt = [&](double rgbD, double cone) {
        const double af = std::pow(vc.fl * rgbD * cone / 100.0, 0.42);
        return (400.0 * af) / (af + 27.13);
    };
    const double rA = adapt(vc.rgbD[0], rW);
    const double gA = adapt(vc.rgbD[1], gW);
    const double bA = adapt(vc.rgbD[2], bW);
    vc.aw = (2.0 * rA + gA + 0.05 * bA) * vc.nbb;
    return vc;
}

const ViewingConditions& vc()
{
    static const ViewingConditions kVc = makeDefault();
    return kVc;
}

// Float-precision constants derived from the viewing conditions, with the
// linear-sRGB→XYZ and CAT16 matrices folded together (and the per-cone
// discounting + fl/100 of the adaptation folded into the matrix rows), so the
// hot conversion below runs on floats with four powf calls. The importers call
// this ~50k times per conversion; double precision buys nothing perceptually.
struct FloatVc {
    float m[9];        // folded cone matrix: cone_i' = fl/100 · rgbD_i · CAT16 · XYZ
    float aw, nbb, cz, fLRoot, p1c, alphaK;
};

FloatVc makeFloatVc()
{
    const ViewingConditions& v = vc();
    // linear sRGB → XYZ (D65, Y=100) — same coefficients as srgbToCam16Ucs's.
    const double xyz[9] = {
        0.41233895, 0.35762064, 0.18051042,
        0.21263901, 0.71516868, 0.07219232,
        0.01933082, 0.11919478, 0.95053215,
    };
    const double cat[9] = {
        0.401288, 0.650173, -0.051461,
        -0.250268, 1.204414, 0.045854,
        -0.002079, 0.048952, 0.953127,
    };
    FloatVc f;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) s += cat[i * 3 + k] * xyz[k * 3 + j];
            // ×100 (Y scale) × rgbD (discounting) × fl/100 (adaptation input)
            f.m[i * 3 + j] = static_cast<float>(s * 100.0 * v.rgbD[i] * v.fl / 100.0);
        }
    f.aw = static_cast<float>(v.aw);
    f.nbb = static_cast<float>(v.nbb);
    f.cz = static_cast<float>(v.c * v.z);
    f.fLRoot = static_cast<float>(v.fLRoot);
    f.p1c = static_cast<float>((50000.0 / 13.0) * v.nc * v.ncb);
    f.alphaK = static_cast<float>(std::pow(1.64 - std::pow(0.29, v.n), 0.73));
    return f;
}

const FloatVc& fvc()
{
    static const FloatVc kFvc = makeFloatVc();
    return kFvc;
}

} // namespace

Cam16Ucs srgbToCam16Ucs(float r, float g, float b)
{
    return linearSrgbToCam16Ucs(static_cast<float>(srgbToLinear(r)),
                                static_cast<float>(srgbToLinear(g)),
                                static_cast<float>(srgbToLinear(b)));
}

Cam16Ucs linearSrgbToCam16Ucs(float r, float g, float b)
{
    const FloatVc& v = fvc();

    // Folded linear-sRGB → discounted CAT16 cone responses (already scaled for
    // the adaptation nonlinearity), then the post-adaptation compression.
    const float rD = v.m[0] * r + v.m[1] * g + v.m[2] * b;
    const float gD = v.m[3] * r + v.m[4] * g + v.m[5] * b;
    const float bD = v.m[6] * r + v.m[7] * g + v.m[8] * b;
    auto adapt = [](float x) {
        const float af = std::pow(std::fabs(x), 0.42f);
        return (x < 0 ? -1.0f : 1.0f) * 400.0f * af / (af + 27.13f);
    };
    const float rA = adapt(rD), gA = adapt(gD), bA = adapt(bD);

    const float a = (11.0f * rA - 12.0f * gA + bA) / 11.0f;
    const float bb = (rA + gA - 2.0f * bA) / 9.0f;
    const float u = (20.0f * rA + 20.0f * gA + 21.0f * bA) / 20.0f;
    const float p2 = (40.0f * rA + 20.0f * gA + bA) / 20.0f;

    float hr = std::atan2(bb, a);
    if (hr < 0) hr += 2.0f * static_cast<float>(kPi);

    const float ac = p2 * v.nbb;
    const float J = 100.0f * std::pow(ac / v.aw, v.cz);

    const float eHue = 0.25f * (std::cos(hr + 2.0f) + 3.8f);
    const float t = v.p1c * eHue * std::hypot(a, bb) / (u + 0.305f);
    const float alpha = std::pow(t, 0.9f) * v.alphaK;
    const float C = alpha * std::sqrt(J / 100.0f);
    const float M = C * v.fLRoot;

    Cam16Ucs out;
    out.J = 1.7f * J / (1.0f + 0.007f * J);
    const float mstar = (1.0f / 0.0228f) * std::log1p(0.0228f * M);
    out.a = mstar * std::cos(hr);
    out.b = mstar * std::sin(hr);
    return out;
}

Cam16Ucs srgb8ToCam16Ucs(int r, int g, int b)
{
    return srgbToCam16Ucs(r / 255.0f, g / 255.0f, b / 255.0f);
}

namespace {

// 17³ Jacobian lattice over linear sRGB, indexed on a sqrt scale (u = √lin) so
// the nodes crowd toward black where the CAM16 response curves hardest. Central
// finite differences of the (float) forward model, one-sided at the cube faces.
constexpr int kJacN = 17;

struct JacLattice {
    Cam16Jac node[kJacN * kJacN * kJacN];
};

JacLattice makeJacLattice()
{
    JacLattice lat;
    const float h = 0.02f;
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    for (int i = 0; i < kJacN; ++i)
        for (int j = 0; j < kJacN; ++j)
            for (int k = 0; k < kJacN; ++k) {
                const float u[3] = {i / float(kJacN - 1), j / float(kJacN - 1),
                                    k / float(kJacN - 1)};
                const float c[3] = {u[0] * u[0], u[1] * u[1], u[2] * u[2]};
                Cam16Jac& J = lat.node[(i * kJacN + j) * kJacN + k];
                for (int ch = 0; ch < 3; ++ch) {
                    float lo[3] = {c[0], c[1], c[2]}, hi[3] = {c[0], c[1], c[2]};
                    lo[ch] = clamp01(c[ch] - h);
                    hi[ch] = clamp01(c[ch] + h);
                    const float d = hi[ch] - lo[ch];
                    const Cam16Ucs a = linearSrgbToCam16Ucs(lo[0], lo[1], lo[2]);
                    const Cam16Ucs b = linearSrgbToCam16Ucs(hi[0], hi[1], hi[2]);
                    J.m[0 + ch] = (b.J - a.J) / d;
                    J.m[3 + ch] = (b.a - a.a) / d;
                    J.m[6 + ch] = (b.b - a.b) / d;
                }
            }
    return lat;
}

} // namespace

const Cam16Jac& cam16JacobianAt(float r, float g, float b)
{
    static const JacLattice kLat = makeJacLattice();
    auto idx = [](float v) {
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<int>(std::sqrt(v) * (kJacN - 1) + 0.5f);
    };
    return kLat.node[(idx(r) * kJacN + idx(g)) * kJacN + idx(b)];
}

} // namespace hgrpaint
