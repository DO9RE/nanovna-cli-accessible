#include "reactance_effects_config.h"
#include <cmath>
#include <algorithm>

namespace ReactanceEffects {

Config::Config() {
    // Initialize with default values
    resetToDefaults();
}

void Config::setDottedConfig(const ModeConfig& config) {
    dottedModeConfig = config;
}

void Config::setSmoothConfig(const ModeConfig& config) {
    smoothModeConfig = config;
}

void Config::resetToDefaults() {
    // Dotted mode defaults: Reverb for capacitive, Chorus for inductive
    dottedModeConfig.capacitiveCC = CCParameter::REVERB;
    dottedModeConfig.inductiveCC = CCParameter::CHORUS;
    dottedModeConfig.deadzoneEnabled = true;
    dottedModeConfig.deadzoneSize = 5.0;
    dottedModeConfig.mappingFunc = MappingFunction::LINEAR;
    
    // Smooth mode defaults: Same as dotted
    smoothModeConfig.capacitiveCC = CCParameter::REVERB;
    smoothModeConfig.inductiveCC = CCParameter::CHORUS;
    smoothModeConfig.deadzoneEnabled = true;
    smoothModeConfig.deadzoneSize = 5.0;
    smoothModeConfig.mappingFunc = MappingFunction::LINEAR;
}

std::string Config::getCCParameterName(CCParameter param) {
    switch (param) {
        case CCParameter::NONE:
            return "None";
        case CCParameter::MODULATION:
            return "Modulation (CC 1)";
        case CCParameter::BRIGHTNESS:
            return "Brightness (CC 74)";
        case CCParameter::RESONANCE:
            return "Resonance (CC 71)";
        case CCParameter::REVERB:
            return "Reverb (CC 91)";
        case CCParameter::CHORUS:
            return "Chorus (CC 93)";
        default:
            return "Unknown";
    }
}

std::string Config::getMappingFunctionName(MappingFunction func) {
    switch (func) {
        case MappingFunction::LINEAR:
            return "Linear";
        case MappingFunction::LOGARITHMIC:
            return "Logarithmic";
        case MappingFunction::EXPONENTIAL:
            return "Exponential";
        case MappingFunction::SQUARE_ROOT:
            return "Square Root";
        default:
            return "Unknown";
    }
}

int Config::getCCNumber(CCParameter param) {
    return static_cast<int>(param);
}

CCParameter Config::getCCParameterFromNumber(int ccNumber) {
    switch (ccNumber) {
        case 0: return CCParameter::NONE;
        case 1: return CCParameter::MODULATION;
        case 71: return CCParameter::RESONANCE;
        case 74: return CCParameter::BRIGHTNESS;
        case 91: return CCParameter::REVERB;
        case 93: return CCParameter::CHORUS;
        default: return CCParameter::NONE;
    }
}

int calculateCCValue(double reactanceX, bool isCapacitive, const ModeConfig& config) {
    // Check if we're in the deadzone
    if (config.deadzoneEnabled && std::fabs(reactanceX) < config.deadzoneSize) {
        return 0;
    }
    
    // Get the absolute value for mapping
    double absX = std::fabs(reactanceX);
    
    // Clamp to reasonable range (0-MAX_REACTANCE_OHMS)
    absX = std::max(0.0, std::min(absX, MAX_REACTANCE_OHMS));
    
    // Apply mapping function
    double normalizedValue = 0.0;
    
    switch (config.mappingFunc) {
        case MappingFunction::LINEAR:
            // Direct proportional mapping
            normalizedValue = absX / MAX_REACTANCE_OHMS;
            break;
            
        case MappingFunction::LOGARITHMIC:
            // Logarithmic: stronger response for small values
            // log10(1 + |X| / 30.0) / log10(11.0)
            // At X=0: log10(1)/log10(11) = 0
            // At X=MAX_REACTANCE_OHMS: log10(11)/log10(11) = 1
            normalizedValue = std::log10(1.0 + absX / 30.0) / std::log10(11.0);
            break;
            
        case MappingFunction::EXPONENTIAL:
            // Exponential: stronger response for large values
            normalizedValue = (absX / MAX_REACTANCE_OHMS) * (absX / MAX_REACTANCE_OHMS);
            break;
            
        case MappingFunction::SQUARE_ROOT:
            // Square root: softer response
            normalizedValue = std::sqrt(absX / MAX_REACTANCE_OHMS);
            break;
    }
    
    // Clamp normalized value to [0, 1]
    normalizedValue = std::max(0.0, std::min(normalizedValue, 1.0));
    
    // Convert to CC value (0-127)
    int ccValue = static_cast<int>(normalizedValue * 127.0);
    
    // Clamp to valid MIDI CC range
    return std::max(0, std::min(ccValue, 127));
}

} // namespace ReactanceEffects
