#include "ui.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <limits>
#include <cmath>
#include <ctime>

// Cable length measurement
void ConsoleUI::cableLengthMeasurement(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        std::cout << "\n" << translation.get("CABLE_LEN_TITLE", "=== Cable Length Measurement ===") << "\n";
    
    if (!ensureMeasurementData(pts, proto, false)) {
        return;
    }
    
    std::cout << translation.get("CABLE_LEN_INST1", "This function estimates cable length from electrical length.") << "\n";
    std::cout << translation.get("CABLE_LEN_INST2", "Connect cable to Port 1. Leave far end open or shorted.") << "\n";
    std::cout << translation.get("CABLE_LEN_INST3", "Sweep should cover wide frequency range for best accuracy.") << "\n\n";
    
    std::cout << translation.get("CABLE_LEN_TERM", "Cable termination:") << "\n";
    std::cout << translation.get("CABLE_LEN_OPEN", "1. Open (far end disconnected)") << "\n";
    std::cout << translation.get("CABLE_LEN_SHORT", "2. Short (far end shorted)") << "\n";
    std::cout << getPromptWithDepth("CONFIG_CHOICE_PROMPT", 3) << " " << std::flush;
    
    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return;
    
    bool open_end = (input[0] == '1');
    
    // Cable type selection
    auto cablePresets = getCablePresets();
    std::cout << "\n" << translation.get("CABLE_LEN_SELECT_TYPE", "Select cable type:") << "\n";
    
    for (size_t i = 0; i < cablePresets.size(); ++i) {
        std::cout << (i + 1) << ". " << cablePresets[i].name 
                  << " (VF: " << cablePresets[i].velocity_factor << ")";
        if (!cablePresets[i].description.empty()) {
            std::cout << " - " << cablePresets[i].description;
        }
        std::cout << "\n";
    }
    
    std::cout << getPromptWithDepth("CABLE_PRESET_TITLE", 4) << " " << std::flush;
    std::string cableInput;
    std::getline(std::cin, cableInput);
    if (cableInput.empty()) return;
    
    double vf = 0.66;  // Default
    try {
        size_t choice_size_t = std::stoull(cableInput);
        // Bounds check to prevent overflow and ensure valid range
        if (choice_size_t >= 1 && choice_size_t <= cablePresets.size()) {
            const auto& preset = cablePresets[choice_size_t - 1];
            
            if (preset.name == "Custom") {
                // Manual VF entry for custom cable
                std::cout << translation.get("CABLE_LEN_MANUAL_VF", "Manual entry:") << "\n";
                vf = getDoubleInput(translation.get("CABLE_LEN_VF", "Enter Velocity Factor (0.6-0.95): > "), 0.6, 0.95);
                if (vf < 0.6) vf = 0.66;  // Default fallback
            } else {
                vf = preset.velocity_factor;
                std::cout << translation.format("CABLE_LEN_USING_CABLE", "Using {0} with VF = {1}", preset.name, vf) << "\n";
            }
        } else {
            std::cout << translation.get("INVALID_INPUT", "Invalid selection, using default VF = 0.66") << "\n";
            vf = 0.66;
        }
    } catch (...) {
        std::cout << translation.get("INVALID_INPUT", "Invalid input, using default VF = 0.66") << "\n";
        vf = 0.66;
    }
    
    std::cout << translation.get("CABLE_LEN_ANALYZING", "Analyzing phase response...") << "\n";
    
    auto result = comfortFuncs.estimateCableLength(pts, open_end, vf);
    
    double feet = result.length_m * 3.28084;
    std::cout << "\n" << translation.format("CABLE_LEN_RESULT", "Estimated Cable Length: {0} meters ({1} feet)", 
        result.length_m, feet) << "\n";
    std::cout << translation.format("CABLE_LEN_CONFIDENCE", "Confidence: {0}%", result.confidence * 100.0) << "\n";
    
    if (!result.warning.empty()) {
        if (result.warning == "too_few_points") {
            std::cout << translation.get("CABLE_LEN_WARN_FEW_POINTS", "Warning: Too few measurement points") << "\n";
        } else if (result.warning == "freq_span_small") {
            std::cout << translation.get("CABLE_LEN_WARN_SMALL_SPAN", "Warning: Frequency span too small") << "\n";
        }
    }
    
    // Display measurement accuracy information
    std::cout << "\n" << translation.get("CABLE_LEN_ACCURACY_NOTE", "Note on measurement accuracy:") << "\n";
    std::cout << translation.get("CABLE_LEN_ACCURACY_1", "- Typical accuracy: ±10-20% due to VF variation and connectors") << "\n";
    std::cout << translation.get("CABLE_LEN_ACCURACY_2", "- Connectors add ~10-30cm equivalent length") << "\n";
    std::cout << translation.get("CABLE_LEN_ACCURACY_3", "- Actual cable VF may differ from nominal by ±5-10%") << "\n";
    
    // Offer VF correction if user knows actual length
    std::cout << "\n" << translation.get("CABLE_LEN_KNOWN_LENGTH", "Do you know the actual cable length?") << "\n";
    if (getYesNo("")) {
        double actual_length = getDoubleInput(translation.get("CABLE_LEN_ENTER_ACTUAL", "Enter actual cable length in meters: > "), 0.1, 1000.0);
        if (actual_length > 0.1 && actual_length < 1000.0) {
            // Calculate corrected VF
            double corrected_vf = vf * (result.length_m / actual_length);
            if (corrected_vf >= 0.5 && corrected_vf <= 1.0) {
                std::cout << "\n" << translation.format("CABLE_LEN_CORRECTED_VF", "Corrected VF for this cable: {0}", corrected_vf) << "\n";
                std::cout << translation.format("CABLE_LEN_VF_NOTE", "Original VF: {0}, Measured: {1}m, Actual: {2}m", 
                    vf, result.length_m, actual_length) << "\n";
                std::cout << translation.get("CABLE_LEN_VF_HINT", "Use this corrected VF for future measurements with this cable.") << "\n";
            } else {
                std::cout << translation.get("CABLE_LEN_VF_INVALID", "Calculated VF out of valid range. Check measurements.") << "\n";
            }
        }
    }
    
    std::cout << "\n" << translation.get("CABLE_LEN_HINT", "Hint: For best results, sweep at least 50-100 MHz range.") << "\n";
    
    // Ask if user wants to scan again
    std::cout << "\n" << translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") << "\n";
    continueScanning = getYesNo("");
    }
}

// Cable fault detection
void ConsoleUI::cableFaultDetection(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        std::cout << "\n" << translation.get("CABLE_FAULT_TITLE", "=== Cable Fault Detection ===") << "\n";
    
    if (!ensureMeasurementData(pts, proto, false)) {
        return;
    }
    
    std::cout << translation.get("CABLE_FAULT_INST1", "This function checks for cable faults.") << "\n";
    std::cout << translation.get("CABLE_FAULT_INST2", "Connect cable to Port 1.") << "\n\n";
    
    std::cout << translation.get("CABLE_FAULT_TERM", "Cable termination at far end:") << "\n";
    std::cout << translation.get("CABLE_FAULT_OPEN", "1. Open (disconnected)") << "\n";
    std::cout << translation.get("CABLE_FAULT_SHORT", "2. Short (shorted)") << "\n";
    std::cout << translation.get("CABLE_FAULT_UNKNOWN", "3. Unknown") << "\n";
    std::cout << getPromptWithDepth("CONFIG_CHOICE_PROMPT", 3) << " " << std::flush;
    
    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return;
    
    std::string termination = "unknown";
    if (input[0] == '1') termination = "open";
    else if (input[0] == '2') termination = "short";
    
    std::cout << translation.get("CABLE_FAULT_ANALYZING", "Analyzing impedance pattern...") << "\n";
    
    auto result = comfortFuncs.detectCableFault(pts, termination);
    
    std::cout << "\n" << translation.get("CABLE_FAULT_RESULT_TITLE", "Fault Detection Result:") << "\n";
    
    // Translate diagnosis
    std::string diag_text = result.diagnosis;
    if (result.diagnosis == "short_circuit") {
        diag_text = translation.get("CABLE_FAULT_SHORT", "Short circuit detected");
    } else if (result.diagnosis == "possible_short") {
        diag_text = translation.get("CABLE_FAULT_POSSIBLE_SHORT", "Possible short circuit");
    } else if (result.diagnosis == "open_circuit") {
        diag_text = translation.get("CABLE_FAULT_OPEN", "Open circuit detected");
    } else if (result.diagnosis == "possible_open") {
        diag_text = translation.get("CABLE_FAULT_POSSIBLE_OPEN", "Possible open circuit");
    } else if (result.diagnosis == "freq_dependent_fault") {
        diag_text = translation.get("CABLE_FAULT_FREQ_DEP", "Frequency-dependent fault");
    } else if (result.diagnosis == "no_fault_detected") {
        diag_text = translation.get("CABLE_FAULT_NONE", "No obvious fault detected");
    }
    
    std::cout << translation.format("CABLE_FAULT_DIAG", "  Diagnosis: {0}", diag_text) << "\n";
    std::cout << translation.format("CABLE_FAULT_CONF", "  Confidence: {0}", result.confidence) << "\n";
    
    if (!result.details.empty()) {
        std::string detail_text = result.details;
        if (result.details == "R_very_low") {
            detail_text = translation.get("CABLE_FAULT_R_LOW", "Resistance very low");
        } else if (result.details == "R_very_high") {
            detail_text = translation.get("CABLE_FAULT_R_HIGH", "Resistance very high");
        } else if (result.details == "possible_water_damage") {
            detail_text = translation.get("CABLE_FAULT_WATER", "High variation with frequency");
        }
        std::cout << translation.format("CABLE_FAULT_DETAIL", "  Details: {0}", detail_text) << "\n";
    }
    
    std::cout << "\n" << translation.get("CABLE_FAULT_DISCLAIMER", "Note: This is a rough analysis. Use TDR for definitive fault location.") << "\n";
    
    // Ask if user wants to scan again
    std::cout << "\n" << translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") << "\n";
    continueScanning = getYesNo("");
    }
}

// Cable attenuation measurement
void ConsoleUI::cableAttenuationMeasurement(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        std::cout << "\n" << translation.get("CABLE_ATT_TITLE", "=== Cable Attenuation Measurement (S21) ===") << "\n";
    
    if (!ensureMeasurementData(pts, proto, true)) {
        return;
    }
    
    std::cout << translation.get("CABLE_ATT_INST1", "Setup: Connect cable between Port 1 and Port 2") << "\n";
    std::cout << translation.get("CABLE_ATT_INST2", "Perform S21 measurement across frequency range") << "\n\n";
    
    double cable_len_m = getDoubleInput(translation.get("CABLE_ATT_LEN", "Enter cable length in meters: > "), 0.1, 1000.0);
    if (cable_len_m < 0.1) return;
    
    std::cout << translation.get("CABLE_ATT_FREQ_SELECT", "Select frequency for measurement:") << "\n";
    std::cout << translation.get("CABLE_ATT_MANUAL", "2. Manual frequency") << "\n";
    std::cout << getPromptWithDepth("CONFIG_CHOICE_PROMPT", 3) << " " << std::flush;
    
    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return;
    
    uint64_t freq_hz = 0;
    
    if (input[0] == '2') {
        freq_hz = getFrequencyInput(translation.get("CABLE_ATT_ENTER_FREQ", "Enter frequency in Hz: > "));
        if (freq_hz == 0) return;
        
        auto result = comfortFuncs.calculateCableAttenuation(pts, cable_len_m, freq_hz);
        
        std::cout << translation.format("CABLE_ATT_RESULT", "Cable Attenuation at {0} Hz ({1} MHz):", 
            result.freq_hz, result.freq_hz / 1000000.0) << "\n";
        std::cout << translation.format("CABLE_ATT_TOTAL", "  Total Attenuation: {0} dB", result.attenuation_db) << "\n";
        std::cout << translation.format("CABLE_ATT_PER_M", "  Attenuation per meter: {0} dB/m", result.attenuation_db_per_m) << "\n";
        
        double per_100ft = result.attenuation_db_per_m * 30.48;
        std::cout << translation.format("CABLE_ATT_PER_100FT", "  Attenuation per 100ft: {0} dB/100ft", per_100ft) << "\n";
    }
    
    // Ask if user wants to scan again
    std::cout << "\n" << translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") << "\n";
    continueScanning = getYesNo("");
    }
}

// Filter quick check
void ConsoleUI::filterQuickCheck(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        std::cout << "\n" << translation.get("FILTER_TITLE", "=== Filter Quick Check (S21) ===") << "\n";
    
    if (!ensureMeasurementData(pts, proto, true)) {
        return;
    }
    
    std::cout << translation.get("FILTER_INST1", "Setup: Connect filter between Port 1 and Port 2") << "\n";
    std::cout << translation.get("FILTER_INST2", "Perform S21 measurement across frequency range") << "\n\n";
    
    std::cout << translation.get("FILTER_TYPE_SELECT", "Select filter type:") << "\n";
    std::cout << translation.get("FILTER_BANDPASS", "1. Bandpass") << "\n";
    std::cout << translation.get("FILTER_LOWPASS", "2. Lowpass") << "\n";
    std::cout << translation.get("FILTER_HIGHPASS", "3. Highpass") << "\n";
    std::cout << translation.get("FILTER_NOTCH", "4. Notch/Band-stop") << "\n";
    std::cout << getPromptWithDepth("CONFIG_CHOICE_PROMPT", 3) << " " << std::flush;
    
    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return;
    
    std::string filter_type = "bandpass";
    if (input[0] == '1') filter_type = "bandpass";
    else if (input[0] == '2') filter_type = "lowpass";
    else if (input[0] == '3') filter_type = "highpass";
    else if (input[0] == '4') filter_type = "notch";
    
    std::cout << translation.get("FILTER_PASS_RANGE", "Enter passband frequency range:") << "\n";
    uint64_t start_hz = getFrequencyInput(translation.get("FILTER_START_FREQ", "  Start frequency (Hz): > "));
    uint64_t end_hz = getFrequencyInput(translation.get("FILTER_END_FREQ", "  End frequency (Hz): > "));
    
    if (start_hz == 0 || end_hz == 0 || start_hz >= end_hz) return;
    
    std::cout << translation.get("FILTER_ANALYZING", "Analyzing filter response...") << "\n";
    
    auto result = comfortFuncs.checkFilter(pts, filter_type, start_hz, end_hz);
    
    std::cout << "\n" << translation.get("FILTER_RESULT_TITLE", "Filter Analysis Result:") << "\n";
    std::cout << translation.format("FILTER_TYPE", "  Filter Type: {0}", filter_type) << "\n";
    std::cout << translation.format("FILTER_PASS_MIN", "  Minimum insertion loss in passband: {0} dB", result.min_s21_db) << "\n";
    std::cout << translation.format("FILTER_PASS_MAX", "  Maximum insertion loss in passband: {0} dB", result.max_s21_db) << "\n";
    std::cout << translation.format("FILTER_RIPPLE", "  Passband ripple: {0} dB", result.ripple_db) << "\n";
    
    if (result.cutoff_freqs_hz.size() >= 2) {
        std::cout << translation.get("FILTER_3DB_POINTS", "-3dB cutoff frequencies:") << "\n";
        std::cout << translation.format("FILTER_3DB_LOW", "  Lower: {0} Hz ({1} MHz)", 
            result.cutoff_freqs_hz[0], result.cutoff_freqs_hz[0] / 1000000.0) << "\n";
        std::cout << translation.format("FILTER_3DB_HIGH", "  Upper: {0} Hz ({1} MHz)", 
            result.cutoff_freqs_hz[1], result.cutoff_freqs_hz[1] / 1000000.0) << "\n";
        std::cout << translation.format("FILTER_BW", "  Bandwidth: {0} kHz", result.bandwidth_hz / 1000.0) << "\n";
    }
    
    // Ask if user wants to scan again
    std::cout << "\n" << translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") << "\n";
    continueScanning = getYesNo("");
    }
}

// Before/After comparison
void ConsoleUI::beforeAfterComparison(const std::vector<MeasurementPoint>& pts) {
    std::cout << "\n" << translation.get("COMPARE_TITLE", "=== Before/After Comparison ===") << "\n";
    std::cout << translation.get("COMPARE_INST", "This function compares two measurements") << "\n\n";
    
    std::cout << translation.get("COMPARE_MENU", "1. Save Snapshot A\n2. Save Snapshot B\n3. Compare A vs B\n4. Clear snapshots") << "\n";
    std::cout << getPromptWithDepth("CONFIG_CHOICE_PROMPT", 3) << " " << std::flush;
    
    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return;
    
    if (input[0] == '1') {
        if (pts.empty()) {
            std::cout << translation.get("ERROR_NO_DATA", "No measurement data available.") << "\n";
            return;
        }
        snapshotA.data = pts;
        snapshotA.timestamp = static_cast<uint64_t>(std::time(nullptr));
        snapshotA.label = "Snapshot A";
        std::cout << translation.format("COMPARE_SAVED_A", "Snapshot A saved ({0} points)", pts.size()) << "\n";
    } else if (input[0] == '2') {
        if (pts.empty()) {
            std::cout << translation.get("ERROR_NO_DATA", "No measurement data available.") << "\n";
            return;
        }
        snapshotB.data = pts;
        snapshotB.timestamp = static_cast<uint64_t>(std::time(nullptr));
        snapshotB.label = "Snapshot B";
        std::cout << translation.format("COMPARE_SAVED_B", "Snapshot B saved ({0} points)", pts.size()) << "\n";
    } else if (input[0] == '3') {
        if (snapshotA.data.empty()) {
            std::cout << translation.get("COMPARE_NO_A", "Snapshot A not yet saved") << "\n";
            return;
        }
        if (snapshotB.data.empty()) {
            std::cout << translation.get("COMPARE_NO_B", "Snapshot B not yet saved") << "\n";
            return;
        }
        
        std::cout << "\n" << translation.get("COMPARE_RESULT_TITLE", "Comparison: Snapshot A vs Snapshot B") << "\n\n";
        
        // Compare at minimum SWR frequencies
        auto minA = comfortFuncs.findMinimumSWR(snapshotA.data);
        auto minB = comfortFuncs.findMinimumSWR(snapshotB.data);
        
        std::cout << translation.format("COMPARE_MIN_SWR_A", "Minimum SWR A: {0} at {1} Hz", minA.swr, minA.freq_hz) << "\n";
        std::cout << translation.format("COMPARE_MIN_SWR_B", "Minimum SWR B: {0} at {1} Hz", minB.swr, minB.freq_hz) << "\n";
        
        std::string summary;
        if (minB.swr < minA.swr * 0.9) {
            summary = translation.get("COMPARE_IMPROVED", "Improved");
        } else if (minB.swr > minA.swr * 1.1) {
            summary = translation.get("COMPARE_WORSE", "Worse");
        } else {
            summary = translation.get("COMPARE_SIMILAR", "Similar");
        }
        
        std::cout << translation.format("COMPARE_SUMMARY", "Summary: {0}", summary) << "\n";
    } else if (input[0] == '4') {
        snapshotA.data.clear();
        snapshotB.data.clear();
        std::cout << translation.get("COMPARE_CLEARED", "Snapshots cleared") << "\n";
    }
}

// Auto-marker placement
void ConsoleUI::autoMarkerPlacement(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        std::cout << "\n" << translation.get("MARKER_TITLE", "=== Auto-Marker Placement ===") << "\n";
    
    if (!ensureMeasurementData(pts, proto, false)) {
        return;
    }
    
    std::cout << translation.get("MARKER_INST", "Automatically place markers at important points") << "\n";
    std::cout << translation.get("MARKER_ANALYZING", "Analyzing measurement data...") << "\n\n";
    
    // Find minimum SWR
    auto minSWR = comfortFuncs.findMinimumSWR(pts);
    std::cout << translation.format("MARKER_MIN_SWR", "Marker 1: Minimum SWR at {0} Hz - SWR {1}", 
        minSWR.freq_hz, minSWR.swr) << "\n";
    
    // Find X nearest to zero
    size_t x_zero_idx = 0;
    double min_abs_x = 1e9;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (std::abs(pts[i].X) < min_abs_x) {
            min_abs_x = std::abs(pts[i].X);
            x_zero_idx = i;
        }
    }
    std::cout << translation.format("MARKER_X_ZERO", "Marker 2: X nearest to zero at {0} Hz - X {1} Ohm", 
        pts[x_zero_idx].freq, pts[x_zero_idx].X) << "\n";
    
    // If S21 data available
    if (comfortFuncs.hasS21Data(pts)) {
        // Find max S21
        size_t max_s21_idx = 0;
        double max_s21 = -999.0;
        for (size_t i = 0; i < pts.size(); ++i) {
            double s21 = comfortFuncs.calculateS21dB(pts[i]);
            if (s21 > max_s21) {
                max_s21 = s21;
                max_s21_idx = i;
            }
        }
        std::cout << translation.format("MARKER_MAX_S21", "Marker 3: Maximum S21 at {0} Hz - {1} dB", 
            pts[max_s21_idx].freq, max_s21) << "\n";
    } else {
        std::cout << translation.get("MARKER_NO_S21", "Note: S21 markers not available (no S21 data)") << "\n";
    }
    
    std::cout << "\n" << translation.get("MARKER_PLACED", "Markers placed successfully") << "\n";
    
    // Ask if user wants to scan again
    std::cout << "\n" << translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") << "\n";
    continueScanning = getYesNo("");
    }
}

// Comfort configuration
void ConsoleUI::comfortConfiguration() {
    std::cout << "\n" << translation.get("CONFIG_TITLE", "=== Comfort Functions Configuration ===") << "\n";
    
    auto& config = comfortFuncs.getConfig();
    
    std::cout << translation.get("CONFIG_MENU", "1. Set Velocity Factor\n2. Set SWR Threshold\n3. Set Cable Loss\n4. Select Cable Type Preset") << "\n";
    std::cout << getPromptWithDepth("CONFIG_TITLE", 3) << " " << std::flush;
    
    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return;
    
    if (input[0] == '1') {
        std::cout << translation.format("CONFIG_VF_CURRENT", "Current Velocity Factor: {0}", config.velocity_factor) << "\n";
        double vf = getDoubleInput(translation.get("CONFIG_VF_ENTER", "Enter new Velocity Factor (0.6-0.95): > "), 0.6, 0.95);
        config.velocity_factor = vf;
        std::cout << translation.format("CONFIG_VF_SET", "Velocity Factor set to {0}", vf) << "\n";
    } else if (input[0] == '2') {
        std::cout << translation.format("CONFIG_SWR_CURRENT", "Current SWR Threshold: {0}", config.swr_threshold) << "\n";
        double swr = getDoubleInput(translation.get("CONFIG_SWR_ENTER", "Enter new SWR Threshold (1.0-3.0): > "), 1.0, 3.0);
        config.swr_threshold = swr;
        std::cout << translation.format("CONFIG_SWR_SET", "SWR Threshold set to {0}", swr) << "\n";
    } else if (input[0] == '3') {
        std::cout << translation.format("CONFIG_LOSS_CURRENT", "Current Cable Loss: {0} dB/m", config.cable_loss_db_per_m) << "\n";
        double loss = getDoubleInput(translation.get("CONFIG_LOSS_ENTER", "Enter cable loss in dB/m: > "), 0.0, 10.0);
        config.cable_loss_db_per_m = loss;
        std::cout << translation.format("CONFIG_LOSS_SET", "Cable Loss set to {0} dB/m", loss) << "\n";
    } else if (input[0] == '4') {
        // Cable type preset selection
        auto presets = getCablePresets();
        std::cout << "\n" << translation.get("CABLE_PRESET_TITLE", "Select cable type:") << "\n";
        std::cout << translation.get("CONFIG_CABLE_LIST", "Available cable types:") << "\n";
        
        for (size_t i = 0; i < presets.size(); ++i) {
            std::cout << (i + 1) << ". " << presets[i].name 
                      << " (VF: " << presets[i].velocity_factor << ")";
            if (presets[i].loss_db_per_100m_at_100mhz > 0) {
                std::cout << " - " << presets[i].description;
            }
            std::cout << "\n";
        }
        
        std::cout << getPromptWithDepth("CABLE_PRESET_TITLE", 4) << " " << std::flush;
        std::string presetInput;
        std::getline(std::cin, presetInput);
        
        if (!presetInput.empty()) {
            try {
                int choice = std::stoi(presetInput);
                if (choice >= 1 && choice <= static_cast<int>(presets.size())) {
                    const auto& preset = presets[choice - 1];
                    
                    if (preset.name == "Custom") {
                        // Custom entry
                        double customVF = getDoubleInput(translation.get("CONFIG_CABLE_CUSTOM_VF", "Enter Velocity Factor (0.6-0.95): >"), 0.6, 0.95);
                        config.velocity_factor = customVF;
                        
                        double customLoss = getDoubleInput(translation.get("CONFIG_CABLE_CUSTOM_LOSS", "Enter loss in dB/100m at 100 MHz (optional, 0 to skip): >"), 0.0, 200.0);
                        if (customLoss > 0) {
                            config.cable_loss_db_per_m = customLoss / 100.0;
                        }
                        std::cout << translation.format("CABLE_PRESET_SELECTED", "Using {0}: VF = {1}", "Custom", config.velocity_factor) << "\n";
                    } else {
                        // Use preset
                        config.velocity_factor = preset.velocity_factor;
                        if (preset.loss_db_per_100m_at_100mhz > 0) {
                            config.cable_loss_db_per_m = preset.loss_db_per_100m_at_100mhz / 100.0;
                        }
                        std::cout << translation.format("CABLE_PRESET_SELECTED", "Using {0}: VF = {1}", preset.name, preset.velocity_factor) << "\n";
                    }
                }
            } catch (...) {
                std::cout << translation.get("INVALID_INPUT", "Invalid input") << "\n";
            }
        }
    }
}
