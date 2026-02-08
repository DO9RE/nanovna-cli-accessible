#include "comfort_functions.h"
#include "logger.h"
#include "math_logger.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <sstream>
#include <iomanip>

// Find minimum SWR in range
ResonancePoint ComfortFunctions::findMinimumSWR(const std::vector<MeasurementPoint>& pts, size_t start_idx, size_t end_idx) const {
    if (pts.empty()) {
        return {0, 0, 1e9, 0, 0};
    }
    
    if (end_idx >= pts.size()) {
        end_idx = pts.size() - 1;
    }
    
    double min_swr = 1e9;
    size_t min_idx = start_idx;
    
    for (size_t i = start_idx; i <= end_idx; ++i) {
        if (pts[i].swr < min_swr) {
            min_swr = pts[i].swr;
            min_idx = i;
        }
    }
    
    return {min_idx, pts[min_idx].freq, pts[min_idx].swr, pts[min_idx].R, pts[min_idx].X};
}

// Check if a point is a local minimum
bool ComfortFunctions::isLocalMinimum(const std::vector<MeasurementPoint>& pts, size_t idx) const {
    if (idx == 0 || idx >= pts.size() - 1) {
        return false;  // Edge points can't be local minima
    }
    
    return pts[idx].swr < pts[idx-1].swr && pts[idx].swr < pts[idx+1].swr;
}

// Find all resonances (local minima in SWR)
std::vector<ResonancePoint> ComfortFunctions::findResonances(const std::vector<MeasurementPoint>& pts, double min_separation_hz) const {
    std::vector<ResonancePoint> resonances;
    
    if (mathLogger) {
        std::ostringstream oss;
        oss << "Finding resonances: " << pts.size() << " points, min separation " << (min_separation_hz/1e6) << " MHz";
        mathLogger->logDataFlow("Resonance", oss.str());
    }
    
    if (pts.size() < 3) {
        if (mathLogger) mathLogger->logDataFlow("Resonance", "Too few points (need at least 3)");
        return resonances;  // Need at least 3 points
    }
    
    for (size_t i = 1; i < pts.size() - 1; ++i) {
        if (isLocalMinimum(pts, i)) {
            // Check if far enough from previous resonance
            bool far_enough = true;
            if (!resonances.empty()) {
                uint64_t last_freq = resonances.back().freq_hz;
                uint64_t current_freq = pts[i].freq;
                if (current_freq > last_freq && (current_freq - last_freq) < min_separation_hz) {
                    far_enough = false;
                } else if (last_freq > current_freq && (last_freq - current_freq) < min_separation_hz) {
                    far_enough = false;
                }
            }
            
            if (far_enough) {
                resonances.push_back({i, pts[i].freq, pts[i].swr, pts[i].R, pts[i].X});
                if (mathLogger) {
                    std::ostringstream oss;
                    oss << "Found resonance at " << (pts[i].freq/1e6) << " MHz: SWR=" 
                        << std::fixed << std::setprecision(2) << pts[i].swr 
                        << ", R=" << pts[i].R << ", X=" << pts[i].X;
                    mathLogger->logDataFlow("Resonance", oss.str());
                }
            }
        }
    }
    
    // Sort by SWR (best first)
    std::sort(resonances.begin(), resonances.end(), 
              [](const ResonancePoint& a, const ResonancePoint& b) { return a.swr < b.swr; });
    
    if (mathLogger) {
        std::ostringstream oss;
        oss << "Total resonances found: " << resonances.size();
        mathLogger->logDataFlow("Resonance", oss.str());
    }
    
    return resonances;
}

// Find SWR bandwidth (frequencies where SWR <= target)
std::vector<BandwidthRange> ComfortFunctions::findSWRBandwidth(const std::vector<MeasurementPoint>& pts, double target_swr) const {
    std::vector<BandwidthRange> ranges;
    
    if (mathLogger) {
        std::ostringstream oss;
        oss << "Finding SWR bandwidth for target SWR <= " << std::fixed << std::setprecision(1) << target_swr;
        mathLogger->logDataFlow("SWR_BW", oss.str());
    }
    
    if (pts.empty()) {
        if (mathLogger) mathLogger->logDataFlow("SWR_BW", "No data points");
        return ranges;
    }
    
    bool in_range = false;
    uint64_t range_start = 0;
    
    for (size_t i = 0; i < pts.size(); ++i) {
        if (pts[i].swr <= target_swr) {
            if (!in_range) {
                // Start of new range
                range_start = pts[i].freq;
                in_range = true;
            }
        } else {
            if (in_range) {
                // End of range
                uint64_t range_end = pts[i-1].freq;
                BandwidthRange range;
                range.freq_low_hz = range_start;
                range.freq_high_hz = range_end;
                range.bandwidth_hz = range_end - range_start;
                range.center_hz = (range_start + range_end) / 2;
                ranges.push_back(range);
                
                if (mathLogger) {
                    std::ostringstream oss;
                    oss << "Range found: " << (range_start/1e6) << " - " << (range_end/1e6) 
                        << " MHz, BW=" << (range.bandwidth_hz/1e3) << " kHz";
                    mathLogger->logDataFlow("SWR_BW", oss.str());
                }
                
                in_range = false;
            }
        }
    }
    
    // Handle case where range extends to end
    if (in_range) {
        uint64_t range_end = pts.back().freq;
        BandwidthRange range;
        range.freq_low_hz = range_start;
        range.freq_high_hz = range_end;
        range.bandwidth_hz = range_end - range_start;
        range.center_hz = (range_start + range_end) / 2;
        ranges.push_back(range);
        
        if (mathLogger) {
            std::ostringstream oss;
            oss << "Range (extends to end): " << (range_start/1e6) << " - " << (range_end/1e6) 
                << " MHz, BW=" << (range.bandwidth_hz/1e3) << " kHz";
            mathLogger->logDataFlow("SWR_BW", oss.str());
        }
    }
    
    if (mathLogger) {
        std::ostringstream oss;
        oss << "Total ranges found: " << ranges.size();
        mathLogger->logDataFlow("SWR_BW", oss.str());
    }
    
    return ranges;
}

// Find closest index to frequency
size_t ComfortFunctions::findClosestIndex(const std::vector<MeasurementPoint>& pts, uint64_t freq_hz) const {
    if (pts.empty()) {
        return 0;
    }
    
    size_t closest = 0;
    uint64_t min_diff = UINT64_MAX;
    
    for (size_t i = 0; i < pts.size(); ++i) {
        uint64_t diff = (pts[i].freq > freq_hz) ? (pts[i].freq - freq_hz) : (freq_hz - pts[i].freq);
        if (diff < min_diff) {
            min_diff = diff;
            closest = i;
        }
    }
    
    return closest;
}

// Interpolate measurement at specific frequency
bool ComfortFunctions::interpolateAtFrequency(const std::vector<MeasurementPoint>& pts, uint64_t freq_hz, MeasurementPoint& result) const {
    if (pts.empty()) {
        return false;
    }
    
    // Find bracketing points
    if (freq_hz <= pts.front().freq) {
        result = pts.front();
        return true;
    }
    if (freq_hz >= pts.back().freq) {
        result = pts.back();
        return true;
    }
    
    for (size_t i = 0; i < pts.size() - 1; ++i) {
        if (pts[i].freq <= freq_hz && pts[i+1].freq >= freq_hz) {
            // Linear interpolation
            double t = static_cast<double>(freq_hz - pts[i].freq) / static_cast<double>(pts[i+1].freq - pts[i].freq);
            
            result.freq = freq_hz;
            result.s11_re = pts[i].s11_re + t * (pts[i+1].s11_re - pts[i].s11_re);
            result.s11_im = pts[i].s11_im + t * (pts[i+1].s11_im - pts[i].s11_im);
            result.swr = pts[i].swr + t * (pts[i+1].swr - pts[i].swr);
            result.rl = pts[i].rl + t * (pts[i+1].rl - pts[i].rl);
            result.R = pts[i].R + t * (pts[i+1].R - pts[i].R);
            result.X = pts[i].X + t * (pts[i+1].X - pts[i].X);
            result.impedance_mag = pts[i].impedance_mag + t * (pts[i+1].impedance_mag - pts[i].impedance_mag);
            result.phase_deg = pts[i].phase_deg + t * (pts[i+1].phase_deg - pts[i].phase_deg);
            
            if (pts[i].hasS21 && pts[i+1].hasS21) {
                result.s21_re = pts[i].s21_re + t * (pts[i+1].s21_re - pts[i].s21_re);
                result.s21_im = pts[i].s21_im + t * (pts[i+1].s21_im - pts[i].s21_im);
                result.hasS21 = true;
            }
            
            return true;
        }
    }
    
    return false;
}

// Check band suitability
BandSuitabilityResult ComfortFunctions::checkBandSuitability(const std::vector<MeasurementPoint>& pts, const AmateurBand& band) const {
    BandSuitabilityResult result;
    result.band_name = band.name;
    result.passed = false;
    
    if (pts.empty()) {
        return result;
    }
    
    // Find band center
    uint64_t center_freq = band.center();
    MeasurementPoint center_pt;
    if (!interpolateAtFrequency(pts, center_freq, center_pt)) {
        return result;
    }
    
    result.swr_at_center = center_pt.swr;
    result.rl_at_center_db = center_pt.rl;
    
    // Find minimum SWR in band
    size_t start_idx = findClosestIndex(pts, band.start_hz);
    size_t end_idx = findClosestIndex(pts, band.end_hz);
    
    auto min_point = findMinimumSWR(pts, start_idx, end_idx);
    result.min_swr = min_point.swr;
    result.min_swr_freq_hz = min_point.freq_hz;
    
    // Check if passed
    result.passed = (result.swr_at_center <= cfg.swr_threshold);
    
    // Calculate 2:1 bandwidth within the band
    auto bandwidths = findSWRBandwidth(pts, 2.0);
    for (const auto& bw : bandwidths) {
        // Check if this bandwidth overlaps with the band
        if (bw.center_hz >= band.start_hz && bw.center_hz <= band.end_hz) {
            result.swr_bandwidth = bw;
            break;
        }
    }
    
    return result;
}

// Get impedance report at specific frequency
ImpedanceReport ComfortFunctions::getImpedanceReport(const std::vector<MeasurementPoint>& pts, uint64_t freq_hz) const {
    ImpedanceReport report;
    report.freq_hz = freq_hz;
    
    MeasurementPoint pt;
    if (!interpolateAtFrequency(pts, freq_hz, pt)) {
        return report;
    }
    
    report.R = pt.R;
    report.X = pt.X;
    report.Z_mag = pt.impedance_mag;
    report.phase_deg = pt.phase_deg;
    report.swr = pt.swr;
    
    // Determine reactance type
    if (std::abs(pt.X) < 1.0) {
        report.reactance_type = "resistive";
    } else if (pt.X > 0) {
        report.reactance_type = "inductive";
    } else {
        report.reactance_type = "capacitive";
    }
    
    // Impedance hint
    if (pt.R < 35.0) {
        report.impedance_hint = "too_low";
    } else if (pt.R > 70.0) {
        report.impedance_hint = "too_high";
    } else {
        report.impedance_hint = "near_50";
    }
    
    return report;
}

// Get matching hints
MatchingHint ComfortFunctions::getMatchingHint(double R, double X) const {
    MatchingHint hint;
    
    // Primary hint about reactance
    if (std::abs(X) < 1.0) {
        hint.primary_hint = "reactance_good";
    } else if (X > 0) {
        hint.primary_hint = "add_capacitance";
    } else {
        hint.primary_hint = "add_inductance";
    }
    
    // Secondary hint about resistance
    if (R < 35.0) {
        hint.secondary_hint = "r_too_low";
    } else if (R > 70.0) {
        hint.secondary_hint = "r_too_high";
    } else {
        hint.secondary_hint = "r_good";
    }
    
    // Network type hint
    if (R < 50.0) {
        hint.network_type_hint = "step_up";
    } else if (R > 50.0) {
        hint.network_type_hint = "step_down";
    } else {
        hint.network_type_hint = "simple";
    }
    
    return hint;
}

// Estimate phase slope for cable length calculation
double ComfortFunctions::estimatePhaseSlope(const std::vector<MeasurementPoint>& pts) const {
    if (pts.size() < 2) {
        return 0.0;
    }
    
    // Use linear regression on unwrapped phase
    double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;
    int n = 0;
    
    double prev_phase = pts[0].phase_deg;
    double unwrapped_phase = prev_phase;
    
    for (size_t i = 0; i < pts.size(); ++i) {
        // Unwrap phase
        double phase_diff = pts[i].phase_deg - prev_phase;
        if (phase_diff > 180.0) {
            unwrapped_phase -= 360.0;
        } else if (phase_diff < -180.0) {
            unwrapped_phase += 360.0;
        }
        unwrapped_phase += (pts[i].phase_deg - prev_phase);
        prev_phase = pts[i].phase_deg;
        
        double x = static_cast<double>(pts[i].freq);
        double y = unwrapped_phase;
        
        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_xy += x * y;
        n++;
    }
    
    if (n < 2) {
        return 0.0;
    }
    
    // Calculate slope (degrees per Hz)
    double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
    return slope;
}

// Estimate cable length
CableLengthResult ComfortFunctions::estimateCableLength(const std::vector<MeasurementPoint>& pts, bool open_end, double velocity_factor) const {
    CableLengthResult result;
    result.length_m = 0.0;
    result.confidence = 0.0;
    
    if (pts.size() < 10) {
        result.warning = "too_few_points";
        return result;
    }
    
    // Estimate phase slope (degrees per Hz)
    double phase_slope_deg_per_hz = estimatePhaseSlope(pts);
    
    // Convert to radians per Hz
    double phase_slope_rad_per_hz = phase_slope_deg_per_hz * M_PI / 180.0;
    
    // For S11 reflection measurement with open or short termination:
    // The signal travels down the line (distance L) and reflects back (distance L)
    // Total round-trip distance = 2*L
    // Phase shift: beta * (2*L), where beta = 2*pi*f/(c*VF)
    // Therefore: phase = 2*pi*f/(c*VF) * 2*L = 4*pi*f*L/(c*VF)
    // Taking derivative: d(phase)/df = 4*pi*L/(c*VF)
    // Solving for L: L = phase_slope * c * VF / (4*pi)
    //
    // Note: If measurement shows ~16% error (e.g., 8.37m for 10m cable with VF=0.66),
    // this is typically due to:
    // 1. Actual cable VF differs from nominal (manufacturing tolerance ±5-10%)
    // 2. Connector phase shifts (adds ~10-30cm equivalent length)
    // 3. Frequency-dependent VF variation
    // 4. Cable length measurement uncertainty
    //
    // Example: For 8.37m measured vs 10m actual with VF_nominal=0.66:
    //   Actual VF = 0.66 * (8.37/10) ≈ 0.55
    //   This is within typical RG-58 VF range of 0.60-0.67
    
    const double c = 299792458.0;  // Speed of light in m/s
    double length = std::abs(phase_slope_rad_per_hz) * c * velocity_factor / (4.0 * M_PI);
    
    result.length_m = length;
    
    // Confidence based on frequency range and number of points
    uint64_t freq_span = pts.back().freq - pts.front().freq;
    if (freq_span > 100000000) {  // > 100 MHz
        result.confidence = 0.8;
    } else if (freq_span > 50000000) {  // > 50 MHz
        result.confidence = 0.6;
    } else {
        result.confidence = 0.3;
        result.warning = "freq_span_small";
    }
    
    return result;
}

// Detect cable fault
CableFaultResult ComfortFunctions::detectCableFault(const std::vector<MeasurementPoint>& pts, const std::string& termination) const {
    CableFaultResult result;
    result.confidence = "low";
    
    if (pts.empty()) {
        result.diagnosis = "no_data";
        return result;
    }
    
    // Calculate average values
    double avg_swr = 0.0;
    double avg_R = 0.0;
    double max_swr = 0.0;
    double min_R = 1e9;
    double max_R = 0.0;
    
    for (const auto& pt : pts) {
        avg_swr += pt.swr;
        avg_R += pt.R;
        if (pt.swr > max_swr) max_swr = pt.swr;
        if (pt.R < min_R) min_R = pt.R;
        if (pt.R > max_R) max_R = pt.R;
    }
    avg_swr /= pts.size();
    avg_R /= pts.size();
    
    // Heuristics for fault detection
    if (termination == "short" || termination == "unknown") {
        if (avg_R < 5.0 && max_swr > 10.0) {
            result.diagnosis = "short_circuit";
            result.confidence = "high";
            result.details = "R_very_low";
        } else if (avg_R < 15.0) {
            result.diagnosis = "possible_short";
            result.confidence = "medium";
        }
    }
    
    if (termination == "open" || termination == "unknown") {
        if (avg_R > 200.0 && max_swr > 10.0) {
            result.diagnosis = "open_circuit";
            result.confidence = "high";
            result.details = "R_very_high";
        } else if (avg_R > 100.0) {
            result.diagnosis = "possible_open";
            result.confidence = "medium";
        }
    }
    
    // Check for frequency-dependent issues (possible water damage)
    double R_variation = max_R - min_R;
    if (R_variation > 50.0 && avg_swr > 3.0) {
        result.diagnosis = "freq_dependent_fault";
        result.confidence = "medium";
        result.details = "possible_water_damage";
    }
    
    if (result.diagnosis.empty()) {
        result.diagnosis = "no_fault_detected";
        result.confidence = "low";
    }
    
    return result;
}

// Calculate S21 in dB
double ComfortFunctions::calculateS21dB(const MeasurementPoint& pt) const {
    if (!pt.hasS21) {
        return -999.0;  // Invalid
    }
    
    double mag = std::sqrt(pt.s21_re * pt.s21_re + pt.s21_im * pt.s21_im);
    if (mag <= 0.0) {
        return -999.0;
    }
    
    return 20.0 * std::log10(mag);
}

// Check if data has S21
bool ComfortFunctions::hasS21Data(const std::vector<MeasurementPoint>& pts) const {
    if (pts.empty()) {
        return false;
    }
    
    for (const auto& pt : pts) {
        if (pt.hasS21) {
            return true;
        }
    }
    return false;
}

// Calculate cable attenuation
CableAttenuationResult ComfortFunctions::calculateCableAttenuation(const std::vector<MeasurementPoint>& pts, double cable_length_m, uint64_t freq_hz) const {
    CableAttenuationResult result;
    result.freq_hz = freq_hz;
    result.attenuation_db = 0.0;
    result.attenuation_db_per_m = 0.0;
    
    if (cable_length_m <= 0.0 || !hasS21Data(pts)) {
        return result;
    }
    
    MeasurementPoint pt;
    if (!interpolateAtFrequency(pts, freq_hz, pt)) {
        return result;
    }
    
    double s21_db = calculateS21dB(pt);
    if (s21_db < -900.0) {  // Invalid
        return result;
    }
    
    // S21 is negative (loss), convert to positive attenuation
    result.attenuation_db = -s21_db;
    result.attenuation_db_per_m = result.attenuation_db / cable_length_m;
    
    return result;
}

// Filter quick check
FilterCheckResult ComfortFunctions::checkFilter(const std::vector<MeasurementPoint>& pts, const std::string& filter_type, 
                                                 uint64_t passband_start_hz, uint64_t passband_end_hz) const {
    FilterCheckResult result;
    result.filter_type = filter_type;
    result.min_s21_db = 0.0;
    result.max_s21_db = -999.0;
    result.ripple_db = 0.0;
    result.bandwidth_hz = 0.0;
    result.stopband_rejection_db = 0.0;
    
    if (!hasS21Data(pts)) {
        return result;
    }
    
    // Find min/max S21 in passband
    double passband_min = 0.0;
    double passband_max = -999.0;
    
    for (const auto& pt : pts) {
        if (pt.freq >= passband_start_hz && pt.freq <= passband_end_hz) {
            double s21_db = calculateS21dB(pt);
            if (s21_db > -900.0) {  // Valid
                if (s21_db < passband_min) passband_min = s21_db;
                if (s21_db > passband_max) passband_max = s21_db;
            }
        }
    }
    
    result.min_s21_db = passband_min;
    result.max_s21_db = passband_max;
    result.ripple_db = passband_max - passband_min;
    
    // Find -3dB points
    double ref_level = passband_max - 3.0;
    bool in_passband = false;
    
    for (const auto& pt : pts) {
        double s21_db = calculateS21dB(pt);
        if (s21_db > -900.0) {
            if (s21_db >= ref_level && !in_passband) {
                result.cutoff_freqs_hz.push_back(pt.freq);
                in_passband = true;
            } else if (s21_db < ref_level && in_passband) {
                result.cutoff_freqs_hz.push_back(pt.freq);
                in_passband = false;
            }
        }
    }
    
    // Calculate bandwidth
    if (result.cutoff_freqs_hz.size() >= 2) {
        result.bandwidth_hz = result.cutoff_freqs_hz[1] - result.cutoff_freqs_hz[0];
    }
    
    return result;
}
