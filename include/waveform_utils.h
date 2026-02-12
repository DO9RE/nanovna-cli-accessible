#pragma once

#include "waveform.h"
#include <cmath>
#include <vector>
#include <cstdint>
#include <algorithm>

/**
 * Waveform Utilities
 * 
 * Central module for all audio waveform generation, panning, volume scaling,
 * phase accumulation, and buffer management functions.
 * 
 * This consolidates redundant implementations from:
 * - audio.cpp (AudioEngine::waveformSample)
 * - synthesizer_engine.cpp (SynthesizerEngine::waveformSample)
 * - smith_visualizer.cpp (inline waveform calculations)
 * - acoustic_analyzer.cpp (various audio generation functions)
 */

namespace WaveformUtils {

// ============================================================================
// Constants
// ============================================================================

/**
 * Audio amplitude scaling constant
 * Used to scale normalized audio samples (-1.0 to +1.0) to int16_t range
 * Previously defined locally in audio.cpp, synthesizer_engine.cpp, and acoustic_analyzer.cpp
 */
constexpr double AUDIO_AMPLITUDE_SCALE = 30000.0;

/**
 * PI constant for waveform calculations
 */
constexpr double PI_CONST = 3.14159265358979323846;

// ============================================================================
// 1.1 Waveform Generation
// ============================================================================

/**
 * Generate a single waveform sample at a given phase
 * 
 * @param phase Phase value (0.0 to 1.0 represents one full cycle)
 * @param wf Waveform type to generate
 * @return Sample value in range -1.0 to +1.0
 * 
 * Replaces:
 * - AudioEngine::waveformSample() in audio.cpp
 * - SynthesizerEngine::waveformSample() in synthesizer_engine.cpp
 * - Inline waveform calculations in smith_visualizer.cpp
 */
inline double generateWaveformSample(double phase, Waveform wf) {
    switch (wf) {
        case Waveform::SINE:
            return std::sin(2.0 * PI_CONST * phase);
            
        case Waveform::SQUARE:
            return (std::sin(2.0 * PI_CONST * phase) >= 0.0) ? 1.0 : -1.0;
            
        case Waveform::TRIANGLE: {
            double v = 2.0 * std::fabs(2.0 * (phase - std::floor(phase + 0.5))) - 1.0;
            return v;
        }
        
        case Waveform::SAWTOOTH:
            // Rising sawtooth waveform (ramp from -1 to +1)
            return 2.0 * (phase - std::floor(phase)) - 1.0;
        
        case Waveform::SAWTOOTH_INV:
            // Falling sawtooth waveform (ramp from +1 to -1)
            return 1.0 - 2.0 * (phase - std::floor(phase));
        
        case Waveform::PULSE:
            // Pulse wave with 25% duty cycle
            return ((phase - std::floor(phase)) < 0.25) ? 1.0 : -1.0;
    }
    return 0.0;
}

// ============================================================================
// 1.2 Constant-Power Panning
// ============================================================================

/**
 * Constant-power stereo panning using trigonometric curves
 * 
 * @param fraction Pan position (0.0 = full left, 0.5 = center, 1.0 = full right)
 * @param outLeft Output left channel gain (0.0 to 1.0)
 * @param outRight Output right channel gain (0.0 to 1.0)
 * 
 * Replaces identical panning logic in:
 * - synthesizer_engine.cpp (generateAudio, generateRulerAudio, generateXAxisRulerAudio)
 * - spatial_audio_wizard.cpp (test functions)
 * - acoustic_analyzer.cpp (10+ audio generation functions)
 */
inline void constantPowerPan(double fraction, double& outLeft, double& outRight) {
    double angle = fraction * (PI_CONST / 2.0);
    outLeft = std::cos(angle);
    outRight = std::sin(angle);
}

/**
 * Linear stereo panning
 * 
 * @param fraction Pan position (0.0 = full left, 0.5 = center, 1.0 = full right)
 * @param outLeft Output left channel gain (0.0 to 1.0)
 * @param outRight Output right channel gain (0.0 to 1.0)
 * 
 * Replaces linear panning logic in:
 * - audio.cpp (playSequence)
 */
inline void linearPan(double fraction, double& outLeft, double& outRight) {
    outLeft = 1.0 - fraction;
    outRight = fraction;
}

// ============================================================================
// 1.3 Volume Scaling and Clamping
// ============================================================================

/**
 * Normalize volume percentage to a scaling factor
 * 
 * @param volumePercent Volume in percent
 * @param minPercent Minimum allowed volume percent
 * @param maxPercent Maximum allowed volume percent
 * @return Volume scaling factor (0.0 to maxPercent/100.0)
 * 
 * Replaces inline volume normalization in:
 * - synthesizer_engine.cpp (3x identical clamp operations)
 * - smith_visualizer.cpp (3x with different limits)
 */
inline double normalizeVolume(int volumePercent, int minPercent = 0, int maxPercent = 200) {
    return std::clamp(volumePercent, minPercent, maxPercent) / 100.0;
}

/**
 * Convert volume percentage to MIDI velocity value
 * 
 * @param volumePercent Volume in percent (0-100)
 * @return MIDI velocity (0-127)
 * 
 * Replaces MIDI-specific volume scaling in:
 * - midi_engine.cpp
 */
inline int volumeToMIDI(int volumePercent) {
    int clamped = std::clamp(volumePercent, 0, 100);
    return static_cast<int>(clamped * 127.0 / 100.0);
}

// ============================================================================
// 1.4 Phase Accumulation
// ============================================================================

/**
 * Advance phase with automatic wrapping
 * 
 * @param phase Current phase value
 * @param increment Phase increment per step
 * @return New phase value, wrapped to [0.0, 1.0)
 * 
 * Replaces 13+ instances of:
 * - phase += phaseInc; while (phase >= 1.0) phase -= 1.0;
 * In synthesizer_engine.cpp, audio.cpp, and acoustic_analyzer.cpp
 */
inline double advancePhase(double phase, double increment) {
    phase += increment;
    while (phase >= 1.0) {
        phase -= 1.0;
    }
    while (phase < 0.0) {
        phase += 1.0;
    }
    return phase;
}

// ============================================================================
// 1.6 Audio Buffer Resize Pattern
// ============================================================================

/**
 * Ensure stereo buffer has sufficient capacity
 * 
 * @param buffer Buffer to resize if necessary
 * @param sampleCount Number of mono samples (stereo requires 2x capacity)
 * 
 * Replaces 13+ instances of:
 * - if (buffer.size() < static_cast<size_t>(samples * 2)) buffer.resize(samples * 2, 0);
 * In synthesizer_engine.cpp and acoustic_analyzer.cpp
 */
inline void ensureStereoBuffer(std::vector<int16_t>& buffer, size_t sampleCount) {
    size_t requiredSize = sampleCount * 2;
    if (buffer.size() < requiredSize) {
        buffer.resize(requiredSize, 0);
    }
}

// ============================================================================
// 1.7 Stereo Buffer Mixing
// ============================================================================

/**
 * Mix two audio samples with clipping prevention
 * 
 * @param existing Existing sample value
 * @param newSample New sample to mix in
 * @return Mixed and clamped result
 * 
 * Replaces duplicated mixing logic in:
 * - synthesizer_engine.cpp (generateAudio, generateRulerAudio)
 */
inline int16_t mixSamples(int16_t existing, int16_t newSample) {
    int32_t mixed = static_cast<int32_t>(existing) + static_cast<int32_t>(newSample);
    return static_cast<int16_t>(std::clamp(mixed, 
        static_cast<int32_t>(INT16_MIN), 
        static_cast<int32_t>(INT16_MAX)));
}

/**
 * Write stereo sample directly to buffer
 * 
 * @param buffer Output buffer
 * @param index Sample index (not byte index)
 * @param left Left channel value
 * @param right Right channel value
 * 
 * Replaces direct buffer writes in:
 * - audio.cpp (synthAndPlay)
 */
inline void writeStereoSample(std::vector<int16_t>& buffer, size_t index, int16_t left, int16_t right) {
    size_t bufferIndex = index * 2;
    if (bufferIndex + 1 < buffer.size()) {
        buffer[bufferIndex] = left;
        buffer[bufferIndex + 1] = right;
    }
}

} // namespace WaveformUtils
