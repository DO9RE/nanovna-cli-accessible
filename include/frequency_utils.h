#pragma once
#include <string>
#include <cstdint>
#include <cctype>
#include <limits>
#include <sstream>
#include <iomanip>

// Task 1.23: Helper function to remove trailing zeros after decimal point
// Eliminates code duplication in formatFrequencyWithUnit
inline void stripTrailingZeros(std::string& s) {
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        size_t end = s.find_last_not_of('0');
        if (end > dot) {
            s = s.substr(0, end + 1);
        }
        // Remove decimal point if no fractional part
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
}

// Parse frequency string with optional units (K/M/G, kHz/MHz/GHz, etc.)
// Accepts: "144000000", "144M", "144 MHz", "144.5m", "144.5 mhz", etc.
// Returns true if parsing succeeded, false otherwise
inline bool parseFrequencyString(const std::string& input, uint64_t& result) {
    if (input.empty()) {
        return false;
    }
    
    std::string trimmed = input;
    // Trim leading/trailing whitespace
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
        trimmed.erase(trimmed.begin());
    }
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
        trimmed.pop_back();
    }
    
    if (trimmed.empty()) {
        return false;
    }
    
    // Find where the unit starts (first letter after the number)
    size_t unit_start = 0;
    bool found_digit = false;
    bool found_decimal = false;
    
    for (size_t i = 0; i < trimmed.length(); i++) {
        char c = trimmed[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            found_digit = true;
        } else if (c == '.' && found_digit && !found_decimal) {
            found_decimal = true;
        } else if (std::isalpha(static_cast<unsigned char>(c))) {
            unit_start = i;
            break;
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            // Invalid character
            return false;
        }
    }
    
    if (!found_digit) {
        return false;
    }
    
    // Extract number part
    std::string num_str;
    if (unit_start > 0) {
        num_str = trimmed.substr(0, unit_start);
    } else {
        num_str = trimmed;
    }
    
    // Trim trailing whitespace from number
    while (!num_str.empty() && std::isspace(static_cast<unsigned char>(num_str.back()))) {
        num_str.pop_back();
    }
    
    // Parse the numeric value
    double value = 0.0;
    try {
        value = std::stod(num_str);
    } catch (...) {
        return false;
    }
    
    if (value < 0) {
        return false;
    }
    
    // Extract unit part
    std::string unit_str;
    if (unit_start > 0 && unit_start < trimmed.length()) {
        unit_str = trimmed.substr(unit_start);
        // Trim leading whitespace from unit
        while (!unit_str.empty() && std::isspace(static_cast<unsigned char>(unit_str.front()))) {
            unit_str.erase(unit_str.begin());
        }
        // Convert to lowercase for comparison
        for (char& c : unit_str) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    
    // Determine multiplier based on unit
    double multiplier = 1.0;
    
    if (!unit_str.empty()) {
        // Check first character for K/M/G/H
        char first = unit_str[0];
        
        if (first == 'k') {
            multiplier = 1e3;  // Kilo
        } else if (first == 'm') {
            multiplier = 1e6;  // Mega
        } else if (first == 'g') {
            multiplier = 1e9;  // Giga
        } else if (first == 'h') {
            // Plain Hz - no multiplier needed
            multiplier = 1.0;
        } else {
            // Be lenient: accept any letter (per requirements), default to Hz
            // This allows typos/variations like "mhz", "mHz", etc. to work
            multiplier = 1.0;
        }
    }
    
    // Calculate final value
    double freq_hz = value * multiplier;
    
    // Check if value is within reasonable range
    // Use a more conservative limit to avoid precision loss with large doubles
    const double MAX_SAFE_FREQ = 1e15;  // 1 PHz, far beyond NanoVNA range (~6 GHz)
    if (freq_hz < 0 || freq_hz > MAX_SAFE_FREQ || freq_hz > static_cast<double>(UINT64_MAX)) {
        return false;
    }
    
    result = static_cast<uint64_t>(freq_hz);
    return true;
}

// Format frequency value with appropriate unit (Hz, kHz, MHz, GHz)
// Example: 144000000 -> "144 MHz", 1500000 -> "1.5 MHz"
inline std::string formatFrequencyWithUnit(uint64_t freq_hz) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    if (freq_hz >= 1000000000) {
        // GHz range
        double ghz = static_cast<double>(freq_hz) / 1e9;
        oss << ghz;
        std::string result = oss.str();
        // Task 1.23: Use helper function instead of inline duplicate code
        stripTrailingZeros(result);
        return result + " GHz";
    } else if (freq_hz >= 1000000) {
        // MHz range
        double mhz = static_cast<double>(freq_hz) / 1e6;
        oss << mhz;
        std::string result = oss.str();
        // Task 1.23: Use helper function instead of inline duplicate code
        stripTrailingZeros(result);
        return result + " MHz";
    } else if (freq_hz >= 1000) {
        // kHz range
        double khz = static_cast<double>(freq_hz) / 1e3;
        oss << khz;
        std::string result = oss.str();
        // Task 1.23: Use helper function instead of inline duplicate code
        stripTrailingZeros(result);
        return result + " kHz";
    } else {
        // Hz range
        return std::to_string(freq_hz) + " Hz";
    }
}
