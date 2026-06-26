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

} // namespace

Cam16Ucs srgbToCam16Ucs(float r, float g, float b)
{
    const double lr = srgbToLinear(r), lg = srgbToLinear(g), lb = srgbToLinear(b);
    // linear sRGB → XYZ (D65), Y scaled to 100.
    const double X = (0.41233895 * lr + 0.35762064 * lg + 0.18051042 * lb) * 100.0;
    const double Y = (0.21263901 * lr + 0.71516868 * lg + 0.07219232 * lb) * 100.0;
    const double Z = (0.01933082 * lr + 0.11919478 * lg + 0.95053215 * lb) * 100.0;

    const ViewingConditions& v = vc();

    // CAT16 cone responses, chromatic adaptation, post-adaptation nonlinearity.
    const double rC =  0.401288 * X + 0.650173 * Y - 0.051461 * Z;
    const double gC = -0.250268 * X + 1.204414 * Y + 0.045854 * Z;
    const double bC = -0.002079 * X + 0.048952 * Y + 0.953127 * Z;
    const double rD = v.rgbD[0] * rC, gD = v.rgbD[1] * gC, bD = v.rgbD[2] * bC;

    auto adapt = [&](double x) {
        const double af = std::pow(v.fl * std::fabs(x) / 100.0, 0.42);
        return (x < 0 ? -1.0 : 1.0) * 400.0 * af / (af + 27.13);
    };
    const double rA = adapt(rD), gA = adapt(gD), bA = adapt(bD);

    const double a = (11.0 * rA - 12.0 * gA + bA) / 11.0;
    const double bb = (rA + gA - 2.0 * bA) / 9.0;
    const double u = (20.0 * rA + 20.0 * gA + 21.0 * bA) / 20.0;
    const double p2 = (40.0 * rA + 20.0 * gA + bA) / 20.0;

    double hr = std::atan2(bb, a);
    if (hr < 0) hr += 2.0 * kPi;

    const double ac = p2 * v.nbb;
    const double J = 100.0 * std::pow(ac / v.aw, v.c * v.z);

    const double eHue = 0.25 * (std::cos(hr + 2.0) + 3.8);
    const double p1 = (50000.0 / 13.0) * v.nc * v.ncb * eHue;
    const double t = p1 * std::hypot(a, bb) / (u + 0.305);
    const double alpha = std::pow(t, 0.9) * std::pow(1.64 - std::pow(0.29, v.n), 0.73);
    const double C = alpha * std::sqrt(J / 100.0);
    const double M = C * v.fLRoot;

    Cam16Ucs out;
    out.J = static_cast<float>((1.0 + 100.0 * 0.007) * J / (1.0 + 0.007 * J));
    const double mstar = (1.0 / 0.0228) * std::log1p(0.0228 * M);
    out.a = static_cast<float>(mstar * std::cos(hr));
    out.b = static_cast<float>(mstar * std::sin(hr));
    return out;
}

Cam16Ucs srgb8ToCam16Ucs(int r, int g, int b)
{
    return srgbToCam16Ucs(r / 255.0f, g / 255.0f, b / 255.0f);
}

} // namespace hgrpaint
