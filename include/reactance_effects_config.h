#pragma once
#include <string>
#include <cstdint>

/**
 * Reactance Effects Configuration
 * 
 * This module provides MIDI Control Change (CC) effects for the Reactance curve (curve index 3)
 * to enable auditory differentiation between capacitive (X < 0) and inductive (X > 0) reactance.
 * 
 * Features:
 * - Separate configuration profiles for Dotted and Smooth playback modes
 * - User-selectable MIDI CC parameters for capacitive and inductive reactance
 * - Configurable deadzone around X = 0
 * - Multiple mapping functions (linear, logarithmic, exponential, square root)
 * - Platform-compatible CC parameters (Reverb, Chorus, Brightness, Resonance, Modulation)
 */

namespace ReactanceEffects {

    // Maximum reactance value for mapping calculations (in Ohms)
    constexpr double MAX_REACTANCE_OHMS = 300.0;

    /**
     * Available MIDI CC parameters for reactance representation
     */
    enum class CCParameter {
        NONE = 0,              // No effect applied
        MODULATION = 1,        // CC 1: Modulation Wheel (Vibrato) - All platforms
        BRIGHTNESS = 74,       // CC 74: Brightness (Filter Cutoff) - Soundfont-dependent
        RESONANCE = 71,        // CC 71: Resonance (Filter Resonance) - Soundfont-dependent
        REVERB = 91,           // CC 91: Reverb Send - All platforms (may need enabling on macOS)
        CHORUS = 93            // CC 93: Chorus Send - All platforms
    };

    /**
     * Mapping functions for converting reactance values to CC intensity
     */
    enum class MappingFunction {
        LINEAR = 0,      // Direct proportional mapping: CC = |X| / 300.0 * 127.0
        LOGARITHMIC = 1, // Stronger response for small values: CC = log10(1 + |X| / 30.0) / log10(11.0) * 127.0
        EXPONENTIAL = 2, // Stronger response for large values: CC = (|X| / 300.0)^2 * 127.0
        SQUARE_ROOT = 3  // Softer response: CC = sqrt(|X| / 300.0) * 127.0
    };

    /**
     * Configuration for a single mode (Dotted or Smooth)
     */
    struct ModeConfig {
        CCParameter capacitiveCC;   // MIDI CC for X < 0 (capacitive)
        CCParameter inductiveCC;    // MIDI CC for X > 0 (inductive)
        bool deadzoneEnabled;       // Enable deadzone around X = 0
        double deadzoneSize;        // Deadzone size in Ohms (e.g., ±5Ω)
        MappingFunction mappingFunc; // Mapping function to use
        
        // Default constructor
        ModeConfig() 
            : capacitiveCC(CCParameter::REVERB)
            , inductiveCC(CCParameter::CHORUS)
            , deadzoneEnabled(true)
            , deadzoneSize(5.0)
            , mappingFunc(MappingFunction::LINEAR)
        {}
    };

    /**
     * Main configuration class for reactance effects
     */
    class Config {
    public:
        Config();
        
        // Get configuration for specific mode
        const ModeConfig& getDottedConfig() const { return dottedModeConfig; }
        const ModeConfig& getSmoothConfig() const { return smoothModeConfig; }
        
        // Set configuration for specific mode
        void setDottedConfig(const ModeConfig& config);
        void setSmoothConfig(const ModeConfig& config);
        
        // Reset to default values
        void resetToDefaults();
        
        // Get CC parameter name for display
        static std::string getCCParameterName(CCParameter param);
        
        // Get mapping function name for display
        static std::string getMappingFunctionName(MappingFunction func);
        
        // Get CC number from parameter
        static int getCCNumber(CCParameter param);
        
        // Get CCParameter from CC number
        static CCParameter getCCParameterFromNumber(int ccNumber);
        
    private:
        ModeConfig dottedModeConfig;
        ModeConfig smoothModeConfig;
    };

    /**
     * Calculate CC value from reactance
     * 
     * @param reactanceX Reactance value in Ohms
     * @param isCapacitive True if X < 0 (capacitive), false if X > 0 (inductive)
     * @param config Configuration to use for calculation
     * @return CC value (0-127)
     */
    int calculateCCValue(double reactanceX, bool isCapacitive, const ModeConfig& config);

} // namespace ReactanceEffects
