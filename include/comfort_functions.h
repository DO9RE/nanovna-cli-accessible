#pragma once
#include "measurement.h"
#include "band_definitions.h"
#include <vector>
#include <string>
#include <cstdint>
#include <fstream>
#include <sstream>

class TranslationManager;  // Forward declaration
class MathLogger;  // Forward declaration

// Cable type preset information
struct CableTypePreset {
    std::string name;
    double velocity_factor;
    double loss_db_per_100m_at_100mhz;  // Typical loss at 100 MHz
    std::string description;
};

// Load cable presets from config file
// Returns cable database from config/cables.cfg if available, otherwise returns default hardcoded list
inline std::vector<CableTypePreset> getCablePresets() {
    std::vector<CableTypePreset> presets;
    
    // Try to load from config/cables.cfg
    std::ifstream file("config/cables.cfg");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            // Trim whitespace
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
                line.erase(line.begin());
            }
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
                line.pop_back();
            }
            
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;
            
            // Parse line format: name|velocity_factor|loss|impedance|description
            std::vector<std::string> parts;
            std::stringstream ss(line);
            std::string part;
            while (std::getline(ss, part, '|')) {
                parts.push_back(part);
            }
            
            if (parts.size() >= 5) {
                try {
                    CableTypePreset preset;
                    preset.name = parts[0];
                    double vf = std::stod(parts[1]);
                    // Validate velocity factor range (must be between 0.5 and 1.0)
                    if (vf < 0.5 || vf > 1.0) {
                        continue;  // Skip invalid entries
                    }
                    preset.velocity_factor = vf;
                    preset.loss_db_per_100m_at_100mhz = std::stod(parts[2]);
                    // parts[3] is impedance (not stored in struct currently)
                    preset.description = parts[4];
                    presets.push_back(preset);
                } catch (...) {
                    // Skip malformed lines
                }
            }
        }
        file.close();
    }
    
    // If loading failed or file doesn't exist, return default hardcoded presets
    if (presets.empty()) {
        presets = {
            {"RG-58", 0.66, 32.0, "50 Ohm, 5mm, flexible, common for HF/VHF"},
            {"RG-58 Foam", 0.78, 26.0, "50 Ohm, foam dielectric, lower loss"},
            {"RG-213", 0.66, 16.0, "50 Ohm, 10.3mm, lower loss, stiff"},
            {"RG-8", 0.66, 14.0, "50 Ohm, 10.3mm, low loss"},
            {"RG-8X", 0.82, 22.0, "50 Ohm, 6.1mm, mini-8, flexible"},
            {"RG-174", 0.66, 65.0, "50 Ohm, 2.5mm, thin, high loss"},
            {"RG-316", 0.70, 66.0, "50 Ohm, 2.5mm, PTFE, high temp"},
            {"H-155", 0.80, 18.0, "50 Ohm, 5.4mm, low loss, flexible"},
            {"Ecoflex-10", 0.86, 11.5, "50 Ohm, 10.3mm, very low loss"},
            {"Aircom Plus", 0.84, 14.5, "50 Ohm, 10.3mm, low loss, stiff"},
            {"Custom", 0.66, 0.0, "Enter custom values"}
        };
    }
    
    return presets;
}

// Configuration for comfort functions
struct ComfortConfig {
    double velocity_factor = 0.66;  // Default for RG58
    double cable_loss_db_per_m = 0.0;  // Optional cable loss compensation
    double swr_threshold = 2.0;  // Default acceptable SWR
    std::vector<std::string> preferred_bands;  // User's preferred bands
    bool continuous_sweep_was_enabled = false;  // Track if we disabled it
};

// Result structures for various analyses
struct ResonancePoint {
    size_t index;
    uint64_t freq_hz;
    double swr;
    double R;
    double X;
};

struct BandwidthRange {
    uint64_t freq_low_hz;
    uint64_t freq_high_hz;
    uint64_t bandwidth_hz;
    uint64_t center_hz;
    
    double bandwidth_khz() const { return bandwidth_hz / 1000.0; }
    double bandwidth_mhz() const { return bandwidth_hz / 1000000.0; }
};

struct BandSuitabilityResult {
    std::string band_name;
    double swr_at_center;
    double min_swr;
    uint64_t min_swr_freq_hz;
    double rl_at_center_db;
    bool passed;  // Based on threshold
    BandwidthRange swr_bandwidth;  // 2:1 bandwidth if available
};

struct ImpedanceReport {
    uint64_t freq_hz;
    double R;
    double X;
    double Z_mag;
    double phase_deg;
    double swr;
    std::string reactance_type;  // "inductive", "capacitive", "resistive"
    std::string impedance_hint;  // "too low", "too high", "near 50 ohm"
};

struct MatchingHint {
    std::string primary_hint;
    std::string secondary_hint;
    std::string network_type_hint;
};

struct CableLengthResult {
    double length_m;
    double confidence;  // 0.0 to 1.0
    std::string warning;
};

struct CableFaultResult {
    std::string diagnosis;
    std::string confidence;  // "low", "medium", "high"
    std::string details;
};

struct CableAttenuationResult {
    double attenuation_db;
    double attenuation_db_per_m;
    uint64_t freq_hz;
};

struct FilterCheckResult {
    std::string filter_type;
    double min_s21_db;
    double max_s21_db;
    double ripple_db;
    std::vector<uint64_t> cutoff_freqs_hz;  // -3dB points
    double bandwidth_hz;
    double stopband_rejection_db;
};

// Comfort functions utility class
class ComfortFunctions {
public:
    ComfortFunctions() : mathLogger(nullptr) {}
    ComfortFunctions(MathLogger* logger) : mathLogger(logger) {}
    
    // Set math logger
    void setMathLogger(MathLogger* logger) { mathLogger = logger; }
    
    // Configuration
    void setConfig(const ComfortConfig& config) { cfg = config; }
    ComfortConfig& getConfig() { return cfg; }
    
    // Analysis functions
    
    // Find minimum SWR in range
    ResonancePoint findMinimumSWR(const std::vector<MeasurementPoint>& pts, size_t start_idx = 0, size_t end_idx = SIZE_MAX) const;
    
    // Find all resonances (local minima in SWR with X near zero)
    std::vector<ResonancePoint> findResonances(const std::vector<MeasurementPoint>& pts, double min_separation_hz = 1000000.0) const;
    
    // Find SWR bandwidth (frequencies where SWR <= target)
    std::vector<BandwidthRange> findSWRBandwidth(const std::vector<MeasurementPoint>& pts, double target_swr) const;
    
    // Check band suitability
    BandSuitabilityResult checkBandSuitability(const std::vector<MeasurementPoint>& pts, const AmateurBand& band) const;
    
    // Get impedance report at specific frequency
    ImpedanceReport getImpedanceReport(const std::vector<MeasurementPoint>& pts, uint64_t freq_hz) const;
    
    // Get matching hints based on impedance
    MatchingHint getMatchingHint(double R, double X) const;
    
    // Estimate cable length from phase/impedance periodicity
    CableLengthResult estimateCableLength(const std::vector<MeasurementPoint>& pts, bool open_end, double velocity_factor) const;
    
    // Detect cable faults
    CableFaultResult detectCableFault(const std::vector<MeasurementPoint>& pts, const std::string& termination) const;
    
    // Calculate cable attenuation (requires S21 data)
    CableAttenuationResult calculateCableAttenuation(const std::vector<MeasurementPoint>& pts, double cable_length_m, uint64_t freq_hz) const;
    
    // Filter quick check (requires S21 data)
    FilterCheckResult checkFilter(const std::vector<MeasurementPoint>& pts, const std::string& filter_type, 
                                   uint64_t passband_start_hz, uint64_t passband_end_hz) const;
    
    // Utility functions
    
    // Interpolate measurement at specific frequency
    bool interpolateAtFrequency(const std::vector<MeasurementPoint>& pts, uint64_t freq_hz, MeasurementPoint& result) const;
    
    // Find closest point to frequency
    size_t findClosestIndex(const std::vector<MeasurementPoint>& pts, uint64_t freq_hz) const;
    
    // Calculate S21 magnitude in dB
    double calculateS21dB(const MeasurementPoint& pt) const;
    
    // Check if points have S21 data
    bool hasS21Data(const std::vector<MeasurementPoint>& pts) const;
    
private:
    ComfortConfig cfg;
    MathLogger* mathLogger;
    
    // Helper functions
    bool isLocalMinimum(const std::vector<MeasurementPoint>& pts, size_t idx) const;
    double estimatePhaseSlope(const std::vector<MeasurementPoint>& pts) const;
};

// Snapshot storage for before/after comparison
struct MeasurementSnapshot {
    std::string label;
    std::vector<MeasurementPoint> data;
    uint64_t timestamp;
};
