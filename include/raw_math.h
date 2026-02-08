#pragma once

// All calculations derived from raw NanoVNA scan values.
// Keep this module isolated so it can be edited independently.

struct Complex2 {
double re = 0.0;
double im = 0.0;
};

namespace raw_math {

// Convert S11 magnitude and phase (degrees) to complex gamma.
Complex2 gammaFromMagPhase(double mag, double phaseDeg);

// Convert complex gamma to impedance Z = Z0*(1+G)/(1-G).
// Returns R and X (Ohms).
void impedanceFromGamma(double z0, const Complex2& g, double& R, double& X);

// Return Loss in dB: -20*log10(|Gamma|). For |Gamma|<1 => positive dB.
double returnLossFromGammaMag(double magGamma);

// SWR: (1+|Gamma|)/(1-|Gamma|). For |Gamma|>=1 => very large.
double swrFromGammaMag(double magGamma);

}