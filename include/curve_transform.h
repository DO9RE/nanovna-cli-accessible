#pragma once
#include "measurement.h"
#include "config.h"
#include <cmath>
#include <algorithm>
#include <vector>

// Range Preset for RL (Return Loss)
enum class RLPreset {
    RL_0_10 = 0,    // 0–10 dB
    RL_0_30 = 1,    // 0–30 dB (Default)
    RL_0_60 = 2,    // 0–60 dB
    CUSTOM = 3
};

// Range Preset for SWR
enum class SWRPreset {
    SWR_1_3 = 0,    // 1.0–3.0
    SWR_1_10 = 1,   // 1.0–10.0 (Default)
    SWR_1_20 = 2,   // 1.0–20.0
    CUSTOM = 3
};

// Generic curve range
struct CurveRange {
    double min;
    double max;
};

namespace CurveTransform {

/**
 * Returns the transformed curve value for display/audio/braille.
 * Applies: RL inversion (sign flip), |X| transformation.
 * Does NOT apply range clamping (that's per-output).
 *
 * @param pt Measurement point
 * @param curveIdx 0=SWR, 1=RL, 2=|Z|, 3=X, 4=Phase
 * @param rlInverted Whether RL inversion is active (default: true)
 *   When inverted: sign is flipped (negative), so better match = lower value (S11-style)
 * @return Transformed value
 */
inline double getTransformedValue(const MeasurementPoint& pt, int curveIdx, bool rlInverted) {
    switch (curveIdx) {
        case 0: return pt.swr;
        case 1: return rlInverted ? -pt.rl : pt.rl;  // Inverted: sign flip (S11-style, negative = better match)
        case 2: return pt.impedance_mag;
        case 3: return std::abs(pt.X);  // |X| per Pflichtenheft
        case 4: return pt.phase_deg;
        default: return 0.0;
    }
}

/**
 * Returns sign info for reactance: true = inductive, false = capacitive
 */
inline bool isInductive(const MeasurementPoint& pt) {
    return pt.X >= 0.0;
}

/**
 * Resolve the effective RL range from preset/custom config.
 * Applies RL-Min clamp to 0 (Pflichtenheft §2.2).
 */
inline CurveRange resolveRLRange(RLPreset preset, const CurveRange& customRange) {
    switch (preset) {
        case RLPreset::RL_0_10: return {0.0, 10.0};
        case RLPreset::RL_0_30: return {0.0, 30.0};
        case RLPreset::RL_0_60: return {0.0, 60.0};
        case RLPreset::CUSTOM: {
            CurveRange r = customRange;
            r.min = std::max(0.0, r.min);  // RL-Min clamp 0 (Pflichtenheft §2.2)
            if (r.max <= r.min) return {0.0, 30.0};  // Fallback on invalid
            return r;
        }
    }
    return {0.0, 30.0};  // Default fallback
}

/**
 * Resolve the effective SWR range from preset/custom config.
 * Applies SWR-Min clamp to 1.0 (Pflichtenheft §3.1).
 */
inline CurveRange resolveSWRRange(SWRPreset preset, const CurveRange& customRange) {
    switch (preset) {
        case SWRPreset::SWR_1_3: return {1.0, 3.0};
        case SWRPreset::SWR_1_10: return {1.0, 10.0};
        case SWRPreset::SWR_1_20: return {1.0, 20.0};
        case SWRPreset::CUSTOM: {
            CurveRange r = customRange;
            r.min = 1.0;  // SWR-Min clamp 1.0 (Pflichtenheft §3.1)
            r.max = std::max(1.0, r.max);
            if (r.max <= r.min) return {1.0, 10.0};  // Fallback
            return r;
        }
    }
    return {1.0, 10.0};  // Default fallback
}

} // namespace CurveTransform

namespace Autoscale {

struct AutoscaleRange {
    double min;
    double max;
};

/**
 * Computes robust autoscale range using percentiles.
 * @param values Transformed curve values for entire sweep
 * @param lowPercentile e.g. 0.005 for p0.5
 * @param highPercentile e.g. 0.995 for p99.5
 * @return AutoscaleRange with computed min/max
 */
inline AutoscaleRange computeRobust(const std::vector<double>& values,
                                     double lowPercentile = 0.005,
                                     double highPercentile = 0.995) {
    if (values.empty()) return {0.0, 1.0};
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    size_t n = sorted.size();
    size_t lowIdx = static_cast<size_t>(lowPercentile * static_cast<double>(n - 1));
    size_t highIdx = static_cast<size_t>(highPercentile * static_cast<double>(n - 1));
    if (highIdx >= n) highIdx = n - 1;
    double minVal = sorted[lowIdx];
    double maxVal = sorted[highIdx];
    if (maxVal <= minVal) maxVal = minVal + 1.0;  // Avoid degenerate range
    return { minVal, maxVal };
}

} // namespace Autoscale

namespace BrailleMarker {

// Braille Dots 2+5 bitmask for X=0 marker (Pflichtenheft §8)
constexpr uint8_t MARKER_DOTS_2_5 = 0x02 | 0x10;  // Dot 2 = bit 1, Dot 5 = bit 4

/**
 * Detect X=0 crossings using Schmitt-trigger hysteresis (Pflichtenheft §8.6).
 * @param pts Measurement points (full set)
 * @param indices Indices to check (may be downsampled)
 * @param hysteresis_ohms H value (default 5.0)
 * @return Vector of indices where zero-crossings occur
 */
inline std::vector<size_t> detectReactanceZeroCrossings(
    const std::vector<MeasurementPoint>& pts,
    const std::vector<size_t>& indices,
    double hysteresis_ohms = 5.0) {
    
    std::vector<size_t> crossings;
    if (indices.size() < 2) return crossings;
    
    // Schmitt-Trigger state: +1 = was last > +H, -1 = was last < -H, 0 = undefined
    int state = 0;
    double H = hysteresis_ohms;
    
    // Initialize state based on first point
    double firstX = pts[indices[0]].X;
    if (firstX > H) state = 1;
    else if (firstX < -H) state = -1;
    
    for (size_t i = 1; i < indices.size(); i++) {
        if (indices[i] >= pts.size()) continue;
        double x = pts[indices[i]].X;
        
        if (state == -1 && x > H) {
            // Transition negative → positive → zero crossing
            crossings.push_back(i);  // Index into the downsampled array
            state = 1;
        } else if (state == 1 && x < -H) {
            // Transition positive → negative → zero crossing
            crossings.push_back(i);  // Index into the downsampled array
            state = -1;
        } else if (state == 0) {
            // Initial state not yet set
            if (x > H) state = 1;
            else if (x < -H) state = -1;
        }
    }
    return crossings;
}

/**
 * Overlay X=0 marker on a Braille row (Pflichtenheft §8).
 * Applies Dots 2+5 to the target cell and its neighbors.
 * @param brailleRow The Braille dot row (modified in place via OR)
 * @param cellIdx The cell index where the crossing occurs
 * @param totalCells Total number of cells in the row
 */
inline void overlayZeroCrossingMarker(std::vector<uint8_t>& brailleRow,
                                       size_t cellIdx, size_t totalCells) {
    if (cellIdx >= totalCells) return;
    
    // Main cell (at the crossing point)
    brailleRow[cellIdx] |= MARKER_DOTS_2_5;
    
    // Left neighbor (if available)
    if (cellIdx > 0) {
        brailleRow[cellIdx - 1] |= MARKER_DOTS_2_5;
    }
    
    // Right neighbor (if available)
    if (cellIdx + 1 < totalCells) {
        brailleRow[cellIdx + 1] |= MARKER_DOTS_2_5;
    }
}

} // namespace BrailleMarker
