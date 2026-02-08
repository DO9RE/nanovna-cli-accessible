#include "raw_math.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace raw_math {

static inline double clamp(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

Complex2 gammaFromMagPhase(double mag, double phaseDeg) {
    double m = clamp(mag, 0.0, 2.0);
    double rad = phaseDeg * (M_PI / 180.0);
    Complex2 g;
    g.re = m * std::cos(rad);
    g.im = m * std::sin(rad);
    return g;
}

void impedanceFromGamma(double z0, const Complex2& g, double& R, double& X) {
    // Z = Z0*(1+G)/(1-G)
    double a = 1.0 + g.re;
    double b = g.im;
    double c = 1.0 - g.re;
    double d = -g.im;

    double denom = c*c + d*d;
    if (denom < 1e-18) {
        R = 1e12;
        X = 0.0;
        return;
    }

    double numRe = a*c - b*d;
    double numIm = a*d + b*c;

    R = z0 * (numRe / denom);
    X = z0 * (numIm / denom);
}

double returnLossFromGammaMag(double magGamma) {
    double m = std::fabs(magGamma);
    if (m <= 0.0) return 1e6;
    return -20.0 * std::log10(m);
}

double swrFromGammaMag(double magGamma) {
    double m = std::fabs(magGamma);
    if (m >= 1.0) return 1e6;
    return (1.0 + m) / (1.0 - m);
}

} // namespace raw_math
