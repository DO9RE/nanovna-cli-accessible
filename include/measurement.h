#pragma once
#include <vector>
#include <string>
#include <cstdint>

class MathLogger;  // Forward declaration

struct MeasurementPoint {
    uint64_t freq = 0;

    // Raw complex S11 (gamma) from scan output.
    double s11_re = 0.0;
    double s11_im = 0.0;

    // Optional S21 (if outmask provides it)
    double s21_re = 0.0;
    double s21_im = 0.0;
    bool hasS21 = false;

    // Derived:
    double swr = 0.0;
    double rl = 0.0;
    double R = 0.0;
    double X = 0.0;
    
    // Additional derived values for acoustic analysis
    double impedance_mag = 0.0;  // |Z| = sqrt(R^2 + X^2)
    double phase_deg = 0.0;      // Phase in degrees
};

class MeasurementModule {
public:
    MeasurementModule() = default;

    // Set math logger for debugging (optional)
    void setMathLogger(MathLogger* logger) { mathLogger = logger; }

    // Parse device response (scan output):
    // Common on NanoVNA H4 with outmask 7:
    //   <freq> <s11_re> <s11_im> <s21_re> <s21_im>
    // Other possible formats:
    //   <freq> <s11_re> <s11_im>
    //   <s11_re> <s11_im>   (freq inferred from start/end)
    std::vector<MeasurementPoint> parseDeviceData(const std::string& text, uint64_t startFreq, uint64_t endFreq, double z0 = 50.0);

private:
    MathLogger* mathLogger = nullptr;
};
