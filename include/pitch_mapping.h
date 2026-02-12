#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

/**
 * Task 1.9: Pitch Mapping Utilities
 * 
 * Centralized pitch/frequency mapping functions to ensure consistent
 * acoustic perception across all modules (MIDI, Acoustic Analyzer, Smith Visualizer).
 * 
 * Eliminates three incompatible pitch mapping implementations with a single,
 * well-tested, configurable set of utilities.
 */

namespace PitchMapping {

/**
 * Map a value linearly to a pitch frequency
 * 
 * Performs linear interpolation from a value range to a frequency range.
 * This is the standard mapping used throughout the application for
 * translating measurement values (SWR, impedance, etc.) to audible frequencies.
 * 
 * @param value The input value to map
 * @param valueMin Minimum of input value range
 * @param valueMax Maximum of input value range  
 * @param pitchMinHz Minimum output frequency in Hz
 * @param pitchMaxHz Maximum output frequency in Hz
 * @return Mapped frequency in Hz, clamped to output range
 * 
 * @example
 *   // Map SWR 1.0-10.0 to 200-2000 Hz
 *   double pitch = linearPitchMap(swr, 1.0, 10.0, 200.0, 2000.0);
 */
inline double linearPitchMap(double value, double valueMin, double valueMax, 
                             double pitchMinHz, double pitchMaxHz) {
    // Clamp input value to valid range
    value = std::clamp(value, valueMin, valueMax);
    
    // Normalize to 0.0-1.0 range
    double normalized = (value - valueMin) / (valueMax - valueMin);
    
    // Map to output frequency range
    double pitchHz = pitchMinHz + normalized * (pitchMaxHz - pitchMinHz);
    
    return pitchHz;
}

/**
 * Convert frequency in Hz to MIDI note number and pitch bend
 * 
 * Uses standard MIDI tuning with A4 = 440 Hz = MIDI note 69.
 * Formula: note = 69 + 12 * log2(freq / 440)
 * 
 * The pitch bend is expressed in the standard MIDI range where:
 * - 0 = maximum downward bend (-2 semitones typically)
 * - 8192 = no bend (center)
 * - 16383 = maximum upward bend (+2 semitones typically)
 * 
 * @param freqHz Frequency in Hz (will be clamped to valid MIDI range)
 * @param outNote MIDI note number (0-127)
 * @param outBend Pitch bend value (0-16383, center = 8192)
 * 
 * @example
 *   uint8_t note;
 *   int16_t bend;
 *   frequencyToMIDINote(440.0, note, bend);  // A4: note=69, bend=8192
 */
inline void frequencyToMIDINote(double freqHz, uint8_t& outNote, int16_t& outBend) {
    // MIDI tuning constants
    constexpr double A4_FREQ = 440.0;
    constexpr int A4_NOTE = 69;
    constexpr int16_t PITCH_BEND_CENTER = 8192;
    constexpr int16_t PITCH_BEND_RANGE = 8192;  // ±2 semitones typically
    
    // Convert frequency to MIDI note number with fractional part
    // Formula: note = 69 + 12 * log2(freq / 440)
    double noteFloat = A4_NOTE + 12.0 * std::log2(freqHz / A4_FREQ);
    
    // Clamp to valid MIDI range (0-127)
    noteFloat = std::clamp(noteFloat, 0.0, 127.0);
    
    // Get integer note and fractional part
    outNote = static_cast<uint8_t>(std::floor(noteFloat));
    double fraction = noteFloat - outNote;
    
    // Convert fractional part to pitch bend
    // fraction = 0.0 -> bend slightly down from note
    // fraction = 0.5 -> bend halfway to next note  
    // fraction = 1.0 -> bend to next note (but we floor, so this is next note at fraction=0)
    outBend = static_cast<int16_t>(PITCH_BEND_CENTER + fraction * PITCH_BEND_RANGE);
    
    // Clamp pitch bend to valid range
    outBend = std::clamp(outBend, static_cast<int16_t>(0), static_cast<int16_t>(16383));
}

/**
 * Logarithmic pitch mapping (for future use)
 * 
 * Maps a value logarithmically to a frequency range. Useful when the
 * perceptual importance of the value is exponential (e.g., frequency response).
 * 
 * @param value The input value to map (must be > 0)
 * @param valueMin Minimum of input value range (must be > 0)
 * @param valueMax Maximum of input value range
 * @param pitchMinHz Minimum output frequency in Hz
 * @param pitchMaxHz Maximum output frequency in Hz
 * @return Mapped frequency in Hz
 */
inline double logarithmicPitchMap(double value, double valueMin, double valueMax,
                                  double pitchMinHz, double pitchMaxHz) {
    // Ensure positive values for logarithm
    value = std::max(value, 1e-10);
    valueMin = std::max(valueMin, 1e-10);
    valueMax = std::max(valueMax, 1e-10);
    
    // Clamp input
    value = std::clamp(value, valueMin, valueMax);
    
    // Logarithmic normalization
    double logValue = std::log(value);
    double logMin = std::log(valueMin);
    double logMax = std::log(valueMax);
    double normalized = (logValue - logMin) / (logMax - logMin);
    
    // Map to output range (linear in log space = exponential in linear space)
    double pitchHz = pitchMinHz + normalized * (pitchMaxHz - pitchMinHz);
    
    return pitchHz;
}

} // namespace PitchMapping
