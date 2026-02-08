#include "measurement.h"
#include "raw_math.h"
#include "math_logger.h"

#include <sstream>
#include <vector>
#include <string>
#include <cctype>
#include <cmath>

// Use M_PI constant for pi value
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void split_tokens(const std::string& line, std::vector<std::string>& out) {
    out.clear();
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok) out.push_back(tok);
}

static inline bool looks_numeric(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!(std::isdigit((unsigned char)c) || c=='.' || c=='-' || c=='+' || c=='e' || c=='E')) return false;
    }
    return true;
}

static bool try_parse_double(const std::string& s, double& out) {
    if (!looks_numeric(s)) return false;
    try { out = std::stod(s); return true; } catch(...) { return false; }
}

static bool try_parse_u64(const std::string& s, uint64_t& out) {
    if (!looks_numeric(s)) return false;
    try { out = (uint64_t)std::stoull(s); return true; } catch(...) { return false; }
}

std::vector<MeasurementPoint> MeasurementModule::parseDeviceData(const std::string& text, uint64_t startFreq, uint64_t endFreq, double z0) {
    struct Row {
        uint64_t freq = 0;
        bool hasFreq = false;
        double s11_re = 0.0, s11_im = 0.0;
        double s21_re = 0.0, s21_im = 0.0;
        bool hasS21 = false;
    };

    std::vector<Row> rows;

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        // strip CR
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
        if (line.empty()) continue;

        // ignore prompt
        if (line.rfind("ch>", 0) == 0) continue;

        std::vector<std::string> toks;
        split_tokens(line, toks);
        if (toks.size() < 2) continue;

        Row r;

        // If first token is freq.
        uint64_t f=0;
        if (toks.size() >= 3 && try_parse_u64(toks[0], f) && f > 1000) {
            r.hasFreq = true;
            r.freq = f;

            double a=0.0, b=0.0;
            if (!try_parse_double(toks[1], a) || !try_parse_double(toks[2], b)) continue;

            // For NanoVNA H4 outmask 7 (from your PuTTY log): s11_re can be negative.
            // So interpret tokens[1], tokens[2] as re/im (not mag/phase).
            r.s11_re = a;
            r.s11_im = b;

            if (toks.size() >= 5) {
                double c=0.0,d=0.0;
                if (try_parse_double(toks[3], c) && try_parse_double(toks[4], d)) {
                    r.hasS21 = true;
                    r.s21_re = c;
                    r.s21_im = d;
                }
            }
            rows.push_back(r);
            continue;
        }

        // No freq: assume re/im in first two tokens
        double re=0.0, im=0.0;
        if (!try_parse_double(toks[0], re) || !try_parse_double(toks[1], im)) continue;
        r.s11_re = re;
        r.s11_im = im;
        rows.push_back(r);
    }

    if (rows.empty()) return {};

    // If frequencies missing, distribute evenly.
    bool haveFreq = false;
    for (auto& r : rows) { if (r.hasFreq) { haveFreq = true; break; } }
    if (!haveFreq) {
        size_t n = rows.size();
        double stepHz = 0.0;
        if (n > 1 && endFreq > startFreq) stepHz = double(endFreq - startFreq) / double(n - 1);
        for (size_t i=0;i<n;i++) {
            rows[i].hasFreq = true;
            rows[i].freq = startFreq + (uint64_t)std::llround(i * stepHz);
        }
    }

    std::vector<MeasurementPoint> out;
    out.reserve(rows.size());
    
    if (mathLogger && mathLogger->isEnabled()) {
        mathLogger->logSeparator("MEASUREMENT DATA PARSING AND CALCULATION");
        mathLogger->logDataFlow("PARSE_START", "Starting to process " + std::to_string(rows.size()) + " measurement points from device");
    }
    
    for (size_t idx = 0; idx < rows.size(); idx++) {
        auto& r = rows[idx];
        MeasurementPoint mp;
        mp.freq = r.freq;
        mp.s11_re = r.s11_re;
        mp.s11_im = r.s11_im;
        mp.hasS21 = r.hasS21;
        mp.s21_re = r.s21_re;
        mp.s21_im = r.s21_im;

        if (mathLogger && mathLogger->isEnabled()) {
            mathLogger->logRawMeasurement(idx, mp.freq, mp.s11_re, mp.s11_im, "Device scan data");
        }

        Complex2 g{mp.s11_re, mp.s11_im};
        double magGamma = std::sqrt(g.re*g.re + g.im*g.im);
        
        if (mathLogger && mathLogger->isEnabled()) {
            double phase_deg = std::atan2(g.im, g.re) * (180.0 / M_PI);
            mathLogger->logGammaCalculation(mp.s11_re, mp.s11_im, magGamma, phase_deg, "From S11 complex");
        }
        
        mp.rl = raw_math::returnLossFromGammaMag(magGamma);
        if (mathLogger && mathLogger->isEnabled()) {
            mathLogger->logReturnLossCalculation(magGamma, mp.rl, "Point " + std::to_string(idx));
        }
        
        mp.swr = raw_math::swrFromGammaMag(magGamma);
        if (mathLogger && mathLogger->isEnabled()) {
            mathLogger->logSWRCalculation(magGamma, mp.swr, "Point " + std::to_string(idx));
        }
        
        raw_math::impedanceFromGamma(z0, g, mp.R, mp.X);
        if (mathLogger && mathLogger->isEnabled()) {
            mathLogger->logImpedanceCalculation(g.re, g.im, z0, mp.R, mp.X, "Point " + std::to_string(idx));
        }
        
        // Calculate additional derived values
        mp.impedance_mag = std::sqrt(mp.R * mp.R + mp.X * mp.X);
        if (mathLogger && mathLogger->isEnabled()) {
            mathLogger->logMagnitudeCalculation(mp.R, mp.X, mp.impedance_mag, "|Z| from R and X");
        }
        
        mp.phase_deg = std::atan2(g.im, g.re) * (180.0 / M_PI);
        if (mathLogger && mathLogger->isEnabled()) {
            mathLogger->logPhaseCalculation(g.im, g.re, mp.phase_deg, "S11 phase");
        }

        out.push_back(mp);
    }
    
    if (mathLogger && mathLogger->isEnabled()) {
        mathLogger->logDataFlow("PARSE_COMPLETE", "Successfully processed " + std::to_string(out.size()) + " measurement points");
    }
    
    return out;
}
