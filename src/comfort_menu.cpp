#include "ui.h"
#include "help.h"
#include "band_definitions.h"
#include "frequency_utils.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <limits>
#include <cmath>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#include <conio.h>
#endif

// Helper function to generate prompts with depth indication
std::string ConsoleUI::getPromptWithDepth(const std::string& promptKey, int depth) const {
    std::string prompt = translation.get(promptKey, promptKey);
    std::string depthIndicator = "";
    for (int i = 0; i < depth; ++i) {
        depthIndicator += ">";
    }
    return prompt + " " + depthIndicator;
}

// Helper to ensure port is selected, offering to navigate to port selection if not
bool ConsoleUI::ensurePortSelected(NanoVNAProtocol* proto) {
    if (!cfg.serial_port.empty()) {
        return true;  // Port is already selected
    }
    
    std::cout << translation.get("ERROR_NO_PORT", "Error: No COM port selected. Use (P)ort to select a port first.") << "\n";
    std::cout << translation.get("PORT_SELECT_OFFER", "Would you like to select a port now?") << "\n";
    
    if (!getYesNo("")) {
        return false;
    }
    
    // Call the port selection function
    return interactiveSelectPort(proto);
}

// Helper to configure measurement settings (band or custom range)
bool ConsoleUI::configureMeasurementSettings() {
    std::cout << "\n" << translation.get("CONFIG_MEASURE_TITLE", "=== Configure Measurement Settings ===") << "\n";
    std::cout << translation.get("CONFIG_MEASURE_SELECT", "Select measurement range:") << "\n";
    std::cout << translation.get("CONFIG_MEASURE_BAND", "B. Select from amateur radio bands") << "\n";
    std::cout << translation.get("CONFIG_MEASURE_CUSTOM", "C. Enter custom frequency range") << "\n";
    std::cout << translation.get("CONFIG_MEASURE_KEEP", "K. Keep current settings") << "\n";
    std::cout << getPromptWithDepth("CONFIG_MEASURE_PROMPT", 3) << " " << std::flush;
    
    std::string input;
    std::getline(std::cin, input);
    
    if (input.empty()) {
        return false;
    }
    
    char choice = input[0];
    if (choice >= 'A' && choice <= 'Z') choice = choice - 'A' + 'a';
    
    if (choice == 'k') {
        // Keep current settings - but check if they are valid
        if (cfg.start_freq == 0 || cfg.end_freq == 0 || cfg.step == 0) {
            std::cout << translation.get("ERROR_NO_RANGE", "No frequency range configured. Please set range first.") << "\n";
            return false;
        }
        return true;
    } else if (choice == 'b') {
        // Select from bands
        auto bands = getAmateurBands(cfg.bandplan);
        
        std::cout << "\n" << translation.get("CONFIG_MEASURE_BAND_SELECT", "Select a band:") << "\n";
        std::cout << translation.get("CONFIG_MEASURE_ALL_BANDS", "A. All bands (full sweep)") << "\n";
        
        for (size_t i = 0; i < bands.size(); ++i) {
            std::cout << (i + 1) << ". " << bands[i].name << " (" 
                      << (bands[i].start_hz / 1000000.0) << " - " 
                      << (bands[i].end_hz / 1000000.0) << " MHz)\n";
        }
        
        std::cout << getPromptWithDepth("CONFIG_MEASURE_BAND_PROMPT", 4) << " " << std::flush;
        std::string bandInput;
        std::getline(std::cin, bandInput);
        
        if (bandInput.empty()) {
            return false;
        }
        
        if (bandInput[0] == 'a' || bandInput[0] == 'A') {
            // Full sweep across all bands
            cfg.start_freq = bands.front().start_hz;
            cfg.end_freq = bands.back().end_hz;
            std::cout << translation.format("CONFIG_MEASURE_SET_ALL", "Set range: {0} Hz to {1} Hz (all bands)", 
                cfg.start_freq, cfg.end_freq) << "\n";
            return true;
        } else {
            try {
                int bandNum = std::stoi(bandInput);
                if (bandNum < 1 || bandNum > static_cast<int>(bands.size())) {
                    std::cout << translation.get("CONFIG_MEASURE_INVALID_BAND", "Invalid band number") << "\n";
                    return false;
                }
                
                const auto& band = bands[bandNum - 1];
                uint64_t scan_start, scan_end;
                getBandWithMargin(band, 5.0, scan_start, scan_end);  // 5% margin
                cfg.start_freq = scan_start;
                cfg.end_freq = scan_end;
                
                std::cout << translation.format("CONFIG_MEASURE_SET_BAND", "Set range for {0}: {1} Hz to {2} Hz", 
                    band.name, cfg.start_freq, cfg.end_freq) << "\n";
                return true;
            } catch (...) {
                std::cout << translation.get("CONFIG_MEASURE_INVALID_INPUT", "Invalid input") << "\n";
                return false;
            }
        }
    } else if (choice == 'c') {
        // Custom frequency range
        std::cout << "\n" << translation.get("CONFIG_MEASURE_CUSTOM_START", "Enter start frequency in Hz: > ") << std::flush;
        std::string startInput;
        std::getline(std::cin, startInput);
        
        std::cout << translation.get("CONFIG_MEASURE_CUSTOM_END", "Enter end frequency in Hz: > ") << std::flush;
        std::string endInput;
        std::getline(std::cin, endInput);
        
        uint64_t start = 0;
        uint64_t end = 0;
        
        if (!parseFrequencyString(startInput, start)) {
            std::cout << translation.get("CONFIG_MEASURE_INVALID_INPUT", "Invalid input") << "\n";
            return false;
        }
        
        if (!parseFrequencyString(endInput, end)) {
            std::cout << translation.get("CONFIG_MEASURE_INVALID_INPUT", "Invalid input") << "\n";
            return false;
        }
        
        if (start >= end) {
            std::cout << translation.get("CONFIG_MEASURE_INVALID_RANGE", "Invalid range: start must be less than end") << "\n";
            return false;
        }
        
        cfg.start_freq = start;
        cfg.end_freq = end;
            
        std::cout << translation.format("CONFIG_MEASURE_SET_CUSTOM", "Set custom range: {0} Hz to {1} Hz", 
            cfg.start_freq, cfg.end_freq) << "\n";
        return true;
    }
    
    return false;
}

// Helper to ensure measurement data exists, offering to measure if not
bool ConsoleUI::ensureMeasurementData(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto, bool needsS21) {
    // Check if frequency range is configured
    bool rangeConfigured = (cfg.start_freq != 0 && cfg.end_freq != 0 && cfg.step != 0);
    
    // Check if data exists
    if (!pts.empty()) {
        // If range is configured, check if existing data covers it
        if (rangeConfigured) {
            // Check if existing data covers the current configured frequency range
            bool coversRange = (pts.front().freq <= cfg.start_freq && pts.back().freq >= cfg.end_freq);
            
            if (coversRange) {
            // If S21 is needed, check if we have it
            if (needsS21) {
                bool hasS21 = comfortFuncs.hasS21Data(pts);
                if (!hasS21) {
                    // No S21 data, ask to measure
                    std::cout << translation.get("NO_DATA_S21_OFFER_MEASURE", "No S21 measurement data available. Would you like to perform an S21 (through) measurement now?") << "\n";
                } else {
                    // We have valid S11/S21 data covering the range - ask user what to do
                    std::cout << translation.get("EXISTING_DATA_FOUND", "Existing measurement data found that covers the current range.") << "\n";
                    std::cout << translation.format("EXISTING_DATA_RANGE", "  Current range: {0} - {1} MHz", 
                        pts.front().freq / 1000000.0, pts.back().freq / 1000000.0) << "\n";
                    std::cout << translation.get("USE_EXISTING_OR_NEW", "Would you like to:") << "\n";
                    std::cout << translation.get("USE_EXISTING", "  1. Use existing data") << "\n";
                    std::cout << translation.get("CONFIG_NEW", "  2. Configure new measurement") << "\n";
                    std::cout << "> " << std::flush;
                    
                    std::string choice;
                    std::getline(std::cin, choice);
                    
                    if (choice == "1") {
                        return true;  // Use existing data
                    } else if (choice == "2") {
                        // Configure new measurement
                        if (!configureMeasurementSettings()) {
                            return false;  // User canceled configuration
                        }
                        // Fall through to measurement prompt below
                    } else {
                        return false;  // Invalid choice or canceled
                    }
                }
            } else {
                // We have valid S11 data covering the range - ask user what to do
                std::cout << translation.get("EXISTING_DATA_FOUND", "Existing measurement data found that covers the current range.") << "\n";
                std::cout << translation.format("EXISTING_DATA_RANGE", "  Current range: {0} - {1} MHz", 
                    pts.front().freq / 1000000.0, pts.back().freq / 1000000.0) << "\n";
                std::cout << translation.get("USE_EXISTING_OR_NEW", "Would you like to:") << "\n";
                std::cout << translation.get("USE_EXISTING", "  1. Use existing data") << "\n";
                std::cout << translation.get("CONFIG_NEW", "  2. Configure new measurement") << "\n";
                std::cout << getPromptWithDepth("CONFIG_CHOICE_PROMPT", 3) << " " << std::flush;
                
                std::string choice;
                std::getline(std::cin, choice);
                
                if (choice == "1") {
                    return true;  // Use existing data
                } else if (choice == "2") {
                    // Configure new measurement
                    if (!configureMeasurementSettings()) {
                        return false;  // User canceled configuration
                    }
                    // Fall through to measurement prompt below
                } else {
                    return false;  // Invalid choice or canceled
                }
            }
        } else {
            // Data exists but doesn't cover the requested range
            std::cout << translation.get("DATA_OUT_OF_RANGE", "Existing measurement data does not cover the current range.") << "\n";
            std::cout << translation.format("EXISTING_DATA_RANGE", "  Existing: {0} - {1} MHz", 
                pts.front().freq / 1000000.0, pts.back().freq / 1000000.0) << "\n";
            std::cout << translation.format("REQUESTED_RANGE", "  Requested: {0} - {1} MHz", 
                cfg.start_freq / 1000000.0, cfg.end_freq / 1000000.0) << "\n";
            
            // Ask if they want to configure settings or use current
            std::cout << translation.get("RECONFIGURE_OR_MEASURE", "Would you like to:") << "\n";
            std::cout << translation.get("MEASURE_CURRENT", "  1. Measure with current settings") << "\n";
            std::cout << translation.get("CONFIG_NEW", "  2. Configure new measurement") << "\n";
            std::cout << getPromptWithDepth("CONFIG_CHOICE_PROMPT", 3) << " " << std::flush;
            
            std::string choice;
            std::getline(std::cin, choice);
            
            if (choice == "2") {
                // Configure new measurement
                if (!configureMeasurementSettings()) {
                    return false;  // User canceled configuration
                }
            } else if (choice != "1") {
                return false;  // Invalid choice or canceled
            }
            // Fall through to measurement prompt below
        }
        } else {
            // Data exists but range is not configured - offer to use existing data
            std::cout << translation.get("EXISTING_DATA_FOUND", "Existing measurement data found.") << "\n";
            std::cout << translation.format("EXISTING_DATA_RANGE", "  Current range: {0} - {1} MHz", 
                pts.front().freq / 1000000.0, pts.back().freq / 1000000.0) << "\n";
            std::cout << translation.get("USE_EXISTING_OR_NEW", "Would you like to:") << "\n";
            std::cout << translation.get("USE_EXISTING", "  1. Use existing data") << "\n";
            std::cout << translation.get("CONFIG_NEW", "  2. Configure new measurement") << "\n";
            std::cout << getPromptWithDepth("CONFIG_CHOICE_PROMPT", 3) << " " << std::flush;
            
            std::string choice;
            std::getline(std::cin, choice);
            
            if (choice == "1") {
                return true;  // Use existing data
            } else if (choice == "2") {
                // Configure new measurement
                if (!configureMeasurementSettings()) {
                    return false;  // User canceled configuration
                }
                // Fall through to measurement prompt below
            } else {
                return false;  // Invalid choice or canceled
            }
        }
    } else {
        // No data at all - offer to configure and measure
        std::cout << translation.get("NO_DATA_AVAILABLE", "No measurement data available.") << "\n";
        
        // Ask if they want to configure settings first
        std::cout << translation.get("CONFIG_BEFORE_MEASURE", "Would you like to configure measurement settings before measuring?") << "\n";
        if (getYesNo("")) {
            if (!configureMeasurementSettings()) {
                return false;  // User canceled configuration
            }
        }
        // Fall through to measurement prompt below
    }
    
    // At this point, we need to perform a measurement
    if (needsS21) {
        std::cout << translation.get("NO_DATA_S21_OFFER_MEASURE", "Would you like to perform an S21 (through) measurement now?") << "\n";
    } else {
        std::cout << translation.get("NO_DATA_OFFER_MEASURE", "Would you like to perform a measurement now?") << "\n";
    }
    
    // Ask user if they want to measure
    if (!getYesNo("")) {
        std::cout << translation.get("CANCELLED", "Measurement canceled.") << "\n";
        return false;
    }
    
    // Check if port is selected, and offer to select if not
    if (!ensurePortSelected(proto)) {
        return false;
    }
    
    // Show setup instructions
    if (needsS21) {
        std::cout << translation.get("MEASURE_SETUP_S21", "Setup: Connect cable or component between Port 1 (CH0) and Port 2 (CH1)") << "\n";
    } else {
        std::cout << translation.get("MEASURE_SETUP_S11", "Setup: Connect antenna or cable to Port 1") << "\n";
    }
    
    std::cout << translation.get("MEASURE_CONFIRM", "Press Enter when ready to measure, or ESC to cancel") << std::flush;
    
#if defined(_WIN32)
    char key = static_cast<char>(_getch());
    if (key == 27) {  // ESC
        std::cout << "\n" << translation.get("CANCELLED", "Measurement canceled.") << "\n";
        return false;
    }
    std::cout << "\n";
#else
    std::string input;
    std::getline(std::cin, input);
#endif
    
    // Perform measurement using existing functions
    std::cout << translation.get("MEASURE_PERFORMING", "Performing measurement...") << "\n";
    auto newPts = performMeasurementWithTiming(proto, cfg.start_freq, cfg.end_freq, cfg.step);
    
    if (newPts.empty()) {
        std::cout << translation.get("MEASURE_FAILED", "Measurement failed.") << "\n";
        return false;
    }
    
    // Update the data
    pts = newPts;
    std::cout << translation.format("MEASURE_COMPLETE", "Measurement complete. {0} data points collected.", pts.size()) << "\n";
    
    return true;
}

// Helper function to read yes/no
bool ConsoleUI::getYesNo(const std::string& prompt) {
    // Get localized yes key (fallback to 'y' if translation fails)
    std::string yesKey = translation.get("YES_KEY", "y");
    std::string yesNoPrompt = translation.get("YES_NO_PROMPT", "(y/n)");
    
    // Ensure yesKey is not empty (safety check, should never happen with fallback)
    if (yesKey.empty()) {
        yesKey = "y";
    }
    // yesKey is guaranteed to be non-empty from this point forward
    
    std::cout << prompt << " " << yesNoPrompt << ": " << std::flush;
#if defined(_WIN32)
    char key = static_cast<char>(_getch());
    
    // Handle ESC key as "No"
    if (key == 27) {
        std::cout << "ESC\n";
        return false;
    }
    
    // Convert uppercase to lowercase
    if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
    std::cout << key << "\n";
    
    // Check against localized yes key (safe: yesKey is non-empty)
    return key == yesKey[0];
#else
    std::string input;
    std::getline(std::cin, input);
    
    // Empty input or ESC is treated as "No"
    if (input.empty()) return false;
    
    char firstChar = input[0];
    // Convert to lowercase for comparison
    if (firstChar >= 'A' && firstChar <= 'Z') firstChar = firstChar - 'A' + 'a';
    
    // Check against localized yes key (safe: yesKey is non-empty)
    return firstChar == yesKey[0];
#endif
}

// Helper function to read frequency
uint64_t ConsoleUI::getFrequencyInput(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string input;
    std::getline(std::cin, input);
    uint64_t result = 0;
    if (parseFrequencyString(input, result)) {
        return result;
    }
    return 0;
}

// Helper function to read double with validation
double ConsoleUI::getDoubleInput(const std::string& prompt, double min_val, double max_val) {
    std::cout << prompt << std::flush;
    std::string input;
    std::getline(std::cin, input);
    try {
        double val = std::stod(input);
        if (val < min_val) return min_val;
        if (val > max_val) return max_val;
        return val;
    } catch (...) {
        return min_val;
    }
}

// Main comfort functions menu
void ConsoleUI::comfortFunctionsMenu(std::vector<MeasurementPoint>& lastPts, NanoVNAProtocol* proto) {
    // Check if continuous sweep is enabled and disable it
    bool continuous_sweep_was_enabled = cfg.continuous_sweep_enabled;
    if (cfg.continuous_sweep_enabled) {
        cfg.continuous_sweep_enabled = false;
        std::cout << "\n" << translation.get("UMENU_SWEEP_DISABLED", "Note: Continuous sweep has been automatically disabled to prevent data conflicts.") << "\n";
    }
    
    while (true) {
        std::cout << "\n" << translation.get("UMENU_TITLE", "=== Comfort Functions Menu ===") << "\n";
        std::cout << translation.get("UMENU_SUBTITLE", "Convenient measurement tools for antenna and cable analysis") << "\n\n";
        
        std::cout << translation.get("UMENU_1", "1. Band Suitability Check (S11)") << "\n";
        std::cout << translation.get("UMENU_1_DESC", "   Check antenna performance on amateur radio bands") << "\n";
        std::cout << translation.get("UMENU_2", "2. Resonance Finder (S11)") << "\n";
        std::cout << translation.get("UMENU_2_DESC", "   Find frequencies with minimum SWR") << "\n";
        std::cout << translation.get("UMENU_3", "3. SWR Bandwidth Calculator (S11)") << "\n";
        std::cout << translation.get("UMENU_3_DESC", "   Calculate 1.5:1 and 2:1 SWR bandwidth") << "\n";
        std::cout << translation.get("UMENU_4", "4. Feedpoint Impedance Report (S11)") << "\n";
        std::cout << translation.get("UMENU_4_DESC", "   Detailed impedance at selected frequency") << "\n";
        std::cout << translation.get("UMENU_5", "5. Matching Hints (S11)") << "\n";
        std::cout << translation.get("UMENU_5_DESC", "   Suggestions for impedance matching") << "\n";
        std::cout << translation.get("UMENU_6", "6. Cable Length Measurement (S11)") << "\n";
        std::cout << translation.get("UMENU_6_DESC", "   Estimate cable length from phase") << "\n";
        std::cout << translation.get("UMENU_7", "7. Cable Fault Detection (S11)") << "\n";
        std::cout << translation.get("UMENU_7_DESC", "   Detect shorts, opens, and damage") << "\n";
        std::cout << translation.get("UMENU_8", "8. Cable Attenuation (S21)") << "\n";
        std::cout << translation.get("UMENU_8_DESC", "   Measure cable loss per meter") << "\n";
        std::cout << translation.get("UMENU_9", "9. Filter Quick Check (S21)") << "\n";
        std::cout << translation.get("UMENU_9_DESC", "   Analyze filter characteristics") << "\n";
        std::cout << translation.get("UMENU_10", "10. Before/After Comparison") << "\n";
        std::cout << translation.get("UMENU_10_DESC", "    Compare two measurements") << "\n";
        std::cout << translation.get("UMENU_11", "11. Auto-Marker Placement") << "\n";
        std::cout << translation.get("UMENU_11_DESC", "    Automatically set markers at key points") << "\n";
        std::cout << translation.get("UMENU_12", "12. Configuration") << "\n";
        std::cout << translation.get("UMENU_12_DESC", "    Set velocity factor, SWR limits, etc.") << "\n\n";
        
        std::cout << translation.get("HELP_COMMAND", "Press H for help") << "\n";
        std::cout << translation.get("MSG_PRESS_ESC_BACK", "Press ESC to go back") << "\n";
        std::cout << getPromptWithDepth("UMENU_PROMPT", 2) << " " << std::flush;
        
        // Read input with Enter confirmation to allow multi-digit numbers
        std::string input;
        
#if defined(_WIN32)
        // On Windows, check for ESC key while allowing typing
        // User can press ESC at any time, or Enter to submit
        bool escPressed = false;
        int maxIterations = 30000;  // Timeout after ~25 minutes (30000 * 50ms)
        int iterations = 0;
        while (iterations < maxIterations) {
            if (_kbhit()) {
                int ch = _getch();
                if (ch == 27) {  // ESC key
                    escPressed = true;
                    std::cout << "\n";
                    break;
                } else if (ch == '\r' || ch == '\n') {  // Enter key
                    std::cout << "\n";
                    break;
                } else if (ch == '\b' || ch == 127) {  // Backspace
                    if (!input.empty()) {
                        input.pop_back();
                        std::cout << "\b \b" << std::flush;  // Erase character from display
                    }
                } else if (ch >= 32 && ch < 127) {  // Printable character
                    input += static_cast<char>(ch);
                    std::cout << static_cast<char>(ch) << std::flush;
                }
                iterations = 0;  // Reset timeout on any key press
            }
            iterations++;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));  // 50ms for better responsiveness/CPU balance
        }
        
        if (escPressed) {
            // Re-enable continuous sweep if it was enabled
            if (continuous_sweep_was_enabled) {
                cfg.continuous_sweep_enabled = true;
                std::cout << translation.get("UMENU_SWEEP_REENABLED", "Continuous sweep has been re-enabled.") << "\n";
            }
            return;
        }
#else
        // On Linux, use getline (ESC must be typed as text)
        std::getline(std::cin, input);
#endif
        
        // Trim whitespace
        input.erase(0, input.find_first_not_of(" \t\n\r"));
        input.erase(input.find_last_not_of(" \t\n\r") + 1);
        
        // Check for empty input or "esc" text (for non-Windows or if user typed "esc")
        if (input.empty() || 
            (input.length() >= 3 && (input.substr(0, 3) == "esc" || input.substr(0, 3) == "ESC" || input.substr(0, 3) == "Esc"))) {
            // Re-enable continuous sweep if it was enabled
            if (continuous_sweep_was_enabled) {
                cfg.continuous_sweep_enabled = true;
                std::cout << translation.get("UMENU_SWEEP_REENABLED", "Continuous sweep has been re-enabled.") << "\n";
            }
            return;
        }
        
        // Try to parse as number
        int choice = 0;
        try {
            choice = std::stoi(input);
        } catch (...) {
            // Not a number, maybe a letter
            if (input.length() > 0) {
                char key = input[0];
                if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
                
                if (key == 'a') choice = 11;
                else if (key == 'c') choice = 12;
                else if (key == 'h') choice = -1;  // Special value for help
            }
        }
        
        // Execute selected function
        switch (choice) {
            case -1:
                // Show help
                std::cout << HelpModule::getComfortFunctionsMenuHelp(translation);
                std::cout << translation.get("PRESS_ENTER", "Press Enter to continue...") << std::flush;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                break;
            case 1:
                bandSuitabilityCheck(lastPts, proto);
                break;
            case 2:
                resonanceFinder(lastPts, proto);
                break;
            case 3:
                swrBandwidthCalculator(lastPts, proto);
                break;
            case 4:
                feedpointImpedanceReport(lastPts, proto);
                break;
            case 5:
                matchingHints(lastPts, proto);
                break;
            case 6:
                cableLengthMeasurement(lastPts, proto);
                break;
            case 7:
                cableFaultDetection(lastPts, proto);
                break;
            case 8:
                cableAttenuationMeasurement(lastPts, proto);
                break;
            case 9:
                filterQuickCheck(lastPts, proto);
                break;
            case 10:
                beforeAfterComparison(lastPts);
                break;
            case 11:
                autoMarkerPlacement(lastPts, proto);
                break;
            case 12:
                comfortConfiguration();
                break;
            default:
                std::cout << translation.get("MSG_UNKNOWN_COMMAND", "Unknown command.") << "\n";
                break;
        }
        
        std::cout << translation.get("PRESS_ENTER", "Press Enter to continue...") << std::flush;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

// Band suitability check
void ConsoleUI::bandSuitabilityCheck(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        std::cout << "\n" << translation.get("BAND_SUIT_TITLE", "=== Band Suitability Check ===") << "\n";
    
    auto bands = getAmateurBands(cfg.bandplan);
    
    std::cout << translation.get("BAND_SUIT_SELECT", "Select band to check:") << "\n";
    std::cout << translation.get("BAND_SUIT_ALL", "A. Check all bands") << "\n";
    
    // List bands
    for (size_t i = 0; i < bands.size(); ++i) {
        std::cout << (i + 1) << ". " << bands[i].name << " (" 
                  << (bands[i].start_hz / 1000000.0) << " - " 
                  << (bands[i].end_hz / 1000000.0) << " MHz)\n";
    }
    
    std::cout << getPromptWithDepth("BAND_SUIT_PROMPT", 3) << " " << std::flush;
    std::string input;
    std::getline(std::cin, input);
    
    // Empty input (ESC or just Enter) - break to return to parent menu
    if (input.empty()) break;
    
    bool checkAll = (input[0] == 'a' || input[0] == 'A');
    
    if (checkAll) {
        // Check all bands - measure each band individually for efficiency
        for (const auto& band : bands) {
            std::cout << "\n" << translation.format("BAND_SUIT_CHECKING", "Checking band: {0}", band.name) << "\n";
            
            // Set scan range for this band
            uint64_t scan_start, scan_end;
            getBandWithMargin(band, 5.0, scan_start, scan_end);  // 5% margin
            cfg.start_freq = scan_start;
            cfg.end_freq = scan_end;
            
            // Check if we have data for this band (including margin)
            bool hasDataForBand = false;
            if (!pts.empty()) {
                hasDataForBand = (pts.front().freq <= scan_start && pts.back().freq >= scan_end);
            }
            
            // If no data or data doesn't cover this band, offer to measure
            if (!hasDataForBand) {
                std::cout << translation.get("REMEASURE_OFFER", "Would you like to measure this band now?") << "\n";
                if (!getYesNo("")) {
                    std::cout << translation.format("BAND_SUIT_SKIPPED", "  Skipping band {0}", band.name) << "\n";
                    continue;  // Skip this band
                }
                
                // Check if port is selected, and offer to select if not
                if (!ensurePortSelected(proto)) {
                    std::cout << translation.format("BAND_SUIT_MEASURE_FAILED", "  Failed to measure band {0}", band.name) << "\n";
                    continue;  // Skip this band
                }
                
                // Show setup instructions
                std::cout << translation.get("MEASURE_SETUP_S11", "Setup: Connect antenna or cable to Port 1") << "\n";
                std::cout << translation.get("MEASURE_CONFIRM", "Press Enter when ready to measure, or ESC to cancel") << std::flush;
                
#if defined(_WIN32)
                char key = static_cast<char>(_getch());
                if (key == 27) {  // ESC
                    std::cout << "\n" << translation.get("CANCELLED", "Measurement canceled.") << "\n";
                    std::cout << translation.format("BAND_SUIT_SKIPPED", "  Skipping band {0}", band.name) << "\n";
                    continue;
                }
                std::cout << "\n";
#else
                std::string confirmInput;
                std::getline(std::cin, confirmInput);
#endif
                
                // Perform measurement using existing functions
                std::cout << translation.get("MEASURE_PERFORMING", "Performing measurement...") << "\n";
                auto newPts = performMeasurementWithTiming(proto, cfg.start_freq, cfg.end_freq, cfg.step);
                
                if (newPts.empty()) {
                    std::cout << translation.get("MEASURE_FAILED", "Measurement failed.") << "\n";
                    std::cout << translation.format("BAND_SUIT_MEASURE_FAILED", "  Failed to measure band {0}", band.name) << "\n";
                    continue;  // Skip this band
                }
                
                // Update the data
                pts = newPts;
                std::cout << translation.format("MEASURE_COMPLETE", "Measurement complete. {0} data points collected.", pts.size()) << "\n";
            }
            
            // Verify we have data for this band after potential measurement
            if (pts.empty() || pts.front().freq > band.end_hz || pts.back().freq < band.start_hz) {
                std::cout << translation.format("BAND_SUIT_OUT_OF_RANGE", 
                    "Band {0} is outside measurement range", band.name) << "\n";
                continue;
            }
            
            auto result = comfortFuncs.checkBandSuitability(pts, band);
            
            std::cout << translation.format("BAND_SUIT_RESULT_TITLE", "Results for {0}:", band.name) << "\n";
            std::cout << translation.format("BAND_SUIT_CENTER", "  SWR at band center ({1} Hz): {2}", 
                band.name, band.center(), result.swr_at_center) << "\n";
            std::cout << translation.format("BAND_SUIT_MIN", "  Minimum SWR in band: {0} at {1} Hz", 
                result.min_swr, result.min_swr_freq_hz) << "\n";
            std::cout << translation.format("BAND_SUIT_RL", "  Return Loss at center: {0} dB", 
                result.rl_at_center_db) << "\n";
            
            if (result.swr_bandwidth.bandwidth_hz > 0) {
                std::cout << translation.format("BAND_SUIT_BW", "  2:1 SWR Bandwidth: {0} kHz ({1} Hz to {2} Hz)", 
                    result.swr_bandwidth.bandwidth_khz(), result.swr_bandwidth.freq_low_hz, result.swr_bandwidth.freq_high_hz) << "\n";
            } else {
                std::cout << translation.get("BAND_SUIT_NO_BW", "  No 2:1 bandwidth found in this band") << "\n";
            }
            
            if (result.passed) {
                std::cout << translation.format("BAND_SUIT_PASS", "  Result: PASS (SWR <= {0})", 
                    comfortFuncs.getConfig().swr_threshold) << "\n";
            } else {
                std::cout << translation.format("BAND_SUIT_FAIL", "  Result: FAIL (SWR > {0})", 
                    comfortFuncs.getConfig().swr_threshold) << "\n";
            }
        }
    } else {
        // Check specific band
        try {
            int bandNum = std::stoi(input);
            if (bandNum < 1 || bandNum > static_cast<int>(bands.size())) {
                std::cout << translation.get("COMFORT_INVALID_BAND", "Invalid band number") << "\n";
                continue;  // Continue the loop instead of return
            }
            
            const auto& band = bands[bandNum - 1];
            
            // Set scan range for the selected band BEFORE measuring
            uint64_t scan_start, scan_end;
            getBandWithMargin(band, 5.0, scan_start, scan_end);  // 5% margin
            cfg.start_freq = scan_start;
            cfg.end_freq = scan_end;
            
            // Now that we know which band and have set the range, check if we need to measure for it
            if (!ensureMeasurementData(pts, proto, false)) {
                continue;  // Continue the loop instead of return
            }
            
            // Check if band is in measurement range
            if (pts.front().freq > band.end_hz || pts.back().freq < band.start_hz) {
                std::cout << translation.format("BAND_SUIT_OUT_OF_RANGE", 
                    "Band {0} is outside measurement range. Current range: {1} - {2} MHz", 
                    band.name, pts.front().freq / 1000000.0, pts.back().freq / 1000000.0) << "\n";
                
                // Offer to remeasure with correct range
                std::cout << translation.get("REMEASURE_OFFER", "Would you like to measure this band now?") << "\n";
                if (getYesNo("")) {
                    // Set range for this band
                    uint64_t scan_start, scan_end;
                    getBandWithMargin(band, 5.0, scan_start, scan_end);
                    cfg.start_freq = scan_start;
                    cfg.end_freq = scan_end;
                    
                    // Measure
                    if (!ensureMeasurementData(pts, proto, false)) {
                        continue;
                    }
                    // Continue with the measurement now that we have data for this band
                } else {
                    continue;  // Skip this band
                }
            }
            
            auto result = comfortFuncs.checkBandSuitability(pts, band);
            
            std::cout << "\n" << translation.format("BAND_SUIT_RESULT_TITLE", "Band: {0}", band.name) << "\n";
            std::cout << translation.format("BAND_SUIT_CENTER", "  SWR at band center ({1} Hz): {2}", 
                band.name, band.center(), result.swr_at_center) << "\n";
            std::cout << translation.format("BAND_SUIT_MIN", "  Minimum SWR in band: {0} at {1} Hz", 
                result.min_swr, result.min_swr_freq_hz) << "\n";
            std::cout << translation.format("BAND_SUIT_RL", "  Return Loss at center: {0} dB", 
                result.rl_at_center_db) << "\n";
            
            if (result.swr_bandwidth.bandwidth_hz > 0) {
                std::cout << translation.format("BAND_SUIT_BW", "  2:1 SWR Bandwidth: {0} kHz ({1} Hz to {2} Hz)", 
                    result.swr_bandwidth.bandwidth_khz(), result.swr_bandwidth.freq_low_hz, result.swr_bandwidth.freq_high_hz) << "\n";
            }
            
            if (result.passed) {
                std::cout << translation.format("BAND_SUIT_PASS", "  Result: PASS (SWR <= {0})", 
                    comfortFuncs.getConfig().swr_threshold) << "\n";
            } else {
                std::cout << translation.format("BAND_SUIT_FAIL", "  Result: FAIL (SWR > {0})", 
                    comfortFuncs.getConfig().swr_threshold) << "\n";
            }
        } catch (...) {
            std::cout << translation.get("COMFORT_INVALID_INPUT", "Invalid input") << "\n";
        }
    }
    
    // Ask if user wants to scan again
    std::cout << "\n" << translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") << "\n";
    continueScanning = getYesNo("");
    }
}

// Resonance finder
void ConsoleUI::resonanceFinder(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        std::cout << "\n" << translation.get("RESON_TITLE", "=== Resonance Finder ===") << "\n";
    
    if (!ensureMeasurementData(pts, proto, false)) {
        return;
    }
    
    std::cout << translation.get("RESON_ANALYZING", "Analyzing current sweep for resonances...") << "\n";
    
    auto resonances = comfortFuncs.findResonances(pts);
    
    if (resonances.empty()) {
        std::cout << translation.get("RESON_NONE", "No clear resonance points found.") << "\n";
        std::cout << translation.get("RESON_HINT", "Hint: Resonances are local minima in SWR.") << "\n";
        return;
    }
    
    std::cout << translation.format("RESON_FOUND", "Found {0} resonance point(s):", resonances.size()) << "\n\n";
    
    // Show top 3 resonances
    size_t count = std::min(resonances.size(), size_t(3));
    for (size_t i = 0; i < count; ++i) {
        const auto& res = resonances[i];
        std::cout << translation.format("RESON_POINT", "Resonance {0}:", i + 1) << "\n";
        std::cout << translation.format("RESON_FREQ", "  Frequency: {0} Hz ({1} MHz)", 
            res.freq_hz, res.freq_hz / 1000000.0) << "\n";
        std::cout << translation.format("RESON_SWR", "  SWR: {0}", res.swr) << "\n";
        std::cout << translation.format("RESON_IMPEDANCE", "  Impedance: {0} + j{1} Ohm", 
            res.R, res.X) << "\n\n";
    }
    
    // Ask if user wants to scan again
    std::cout << "\n" << translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") << "\n";
    continueScanning = getYesNo("");
    }
}

// SWR bandwidth calculator
void ConsoleUI::swrBandwidthCalculator(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        std::cout << "\n" << translation.get("SWR_BW_TITLE", "=== SWR Bandwidth Calculator ===") << "\n";
    
    if (!ensureMeasurementData(pts, proto, false)) {
        return;
    }
    
    std::cout << translation.get("SWR_BW_SELECT", "Select target SWR:") << "\n";
    std::cout << translation.get("SWR_BW_1_5", "1. SWR <= 1.5:1") << "\n";
    std::cout << translation.get("SWR_BW_2_0", "2. SWR <= 2.0:1") << "\n";
    std::cout << translation.get("SWR_BW_3_0", "3. SWR <= 3.0:1") << "\n";
    std::cout << translation.get("SWR_BW_CUSTOM", "4. Custom SWR value") << "\n";
    std::cout << getPromptWithDepth("SWR_BW_PROMPT", 3) << " " << std::flush;
    
    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return;
    
    double target_swr = 2.0;
    if (input[0] == '1') target_swr = 1.5;
    else if (input[0] == '2') target_swr = 2.0;
    else if (input[0] == '3') target_swr = 3.0;
    else if (input[0] == '4') {
        target_swr = getDoubleInput(translation.get("SWR_BW_ENTER_CUSTOM", "Enter custom SWR value: > "), 1.0, 10.0);
    }
    
    auto ranges = comfortFuncs.findSWRBandwidth(pts, target_swr);
    
    if (ranges.empty()) {
        std::cout << translation.format("SWR_BW_NONE", "No frequency range found with SWR <= {0}", target_swr) << "\n";
        std::cout << translation.get("SWR_BW_HINT", "This may indicate antenna needs tuning.") << "\n";
        return;
    }
    
    std::cout << "\n" << translation.format("SWR_BW_RESULT_TITLE", "SWR Bandwidth for SWR <= {0}:", target_swr) << "\n\n";
    
    for (size_t i = 0; i < ranges.size(); ++i) {
        const auto& range = ranges[i];
        std::cout << translation.format("SWR_BW_RANGE", "Range {0}:", i + 1) << "\n";
        std::cout << translation.format("SWR_BW_FREQ_RANGE", "  Frequency: {0} Hz to {1} Hz", 
            range.freq_low_hz, range.freq_high_hz) << "\n";
        std::cout << translation.format("SWR_BW_BW_KHZ", "  Bandwidth: {0} kHz", range.bandwidth_khz()) << "\n";
        std::cout << translation.format("SWR_BW_CENTER", "  Center: {0} Hz ({1} MHz)", 
            range.center_hz, range.center_hz / 1000000.0) << "\n\n";
    }
    
    // Ask if user wants to scan again
    std::cout << "\n" << translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") << "\n";
    continueScanning = getYesNo("");
    }
}

// Feedpoint impedance report
void ConsoleUI::feedpointImpedanceReport(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        std::cout << "\n" << translation.get("FP_IMP_TITLE", "=== Feedpoint Impedance Report ===") << "\n";
    
    if (!ensureMeasurementData(pts, proto, false)) {
        return;
    }
    
    std::cout << translation.get("FP_IMP_SELECT", "Select frequency:") << "\n";
    std::cout << translation.get("FP_IMP_MIN_SWR", "S. At minimum SWR") << "\n";
    std::cout << translation.get("FP_IMP_MANUAL", "F. Manual frequency entry") << "\n";
    std::cout << "> " << std::flush;
    
    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return;
    
    uint64_t freq_hz = 0;
    
    if (input[0] == 's' || input[0] == 'S') {
        // Find minimum SWR
        auto min_point = comfortFuncs.findMinimumSWR(pts);
        freq_hz = min_point.freq_hz;
    } else if (input[0] == 'f' || input[0] == 'F') {
        freq_hz = getFrequencyInput(translation.get("FP_IMP_ENTER_FREQ", "Enter frequency in Hz: > "));
        if (freq_hz == 0) return;
    } else {
        return;
    }
    
    auto report = comfortFuncs.getImpedanceReport(pts, freq_hz);
    
    std::cout << "\n" << translation.format("FP_IMP_REPORT_TITLE", "Impedance Report at {0} Hz ({1} MHz):", 
        report.freq_hz, report.freq_hz / 1000000.0) << "\n";
    std::cout << translation.format("FP_IMP_R", "  Resistance (R): {0} Ohm", report.R) << "\n";
    std::cout << translation.format("FP_IMP_X", "  Reactance (X): {0} Ohm", report.X) << "\n";
    std::cout << translation.format("FP_IMP_Z_MAG", "  Impedance Magnitude |Z|: {0} Ohm", report.Z_mag) << "\n";
    std::cout << translation.format("FP_IMP_PHASE", "  Phase: {0} degrees", report.phase_deg) << "\n";
    std::cout << translation.format("FP_IMP_SWR", "  SWR: {0}", report.swr) << "\n";
    
    // Reactance type
    if (report.reactance_type == "inductive") {
        std::cout << translation.get("FP_IMP_TYPE_INDUCTIVE", "  Reactance Type: Inductive (X > 0)") << "\n";
    } else if (report.reactance_type == "capacitive") {
        std::cout << translation.get("FP_IMP_TYPE_CAPACITIVE", "  Reactance Type: Capacitive (X < 0)") << "\n";
    } else {
        std::cout << translation.get("FP_IMP_TYPE_RESISTIVE", "  Reactance Type: Resistive (X ≈ 0)") << "\n";
    }
    
    // Impedance hint
    if (report.impedance_hint == "too_low") {
        std::cout << translation.get("FP_IMP_HINT_LOW", "  Impedance Hint: Too low compared to 50 Ohm") << "\n";
    } else if (report.impedance_hint == "too_high") {
        std::cout << translation.get("FP_IMP_HINT_HIGH", "  Impedance Hint: Too high compared to 50 Ohm") << "\n";
    } else {
        std::cout << translation.get("FP_IMP_HINT_GOOD", "  Impedance Hint: Near 50 Ohm") << "\n";
    }
    
    // Ask if user wants to scan again
    std::cout << "\n" << translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") << "\n";
    continueScanning = getYesNo("");
    }
}

// Matching hints
void ConsoleUI::matchingHints(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        std::cout << "\n" << translation.get("MATCH_TITLE", "=== Matching Hints ===") << "\n";
    
    if (!ensureMeasurementData(pts, proto, false)) {
        return;
    }
    
    // Use minimum SWR point
    auto min_point = comfortFuncs.findMinimumSWR(pts);
    
    std::cout << translation.format("MATCH_USING_DATA", "Using impedance data at {0} Hz", min_point.freq_hz) << "\n";
    std::cout << translation.format("MATCH_R_X", "  Impedance: {0} + j{1} Ohm", min_point.R, min_point.X) << "\n\n";
    
    auto hint = comfortFuncs.getMatchingHint(min_point.R, min_point.X);
    
    std::cout << translation.get("MATCH_HINT_TITLE", "Matching Suggestions (rough guidance):") << "\n";
    
    // Reactance hint
    if (hint.primary_hint == "add_capacitance") {
        std::cout << translation.get("MATCH_X_INDUCTIVE", "  Reactance is INDUCTIVE (X > 0): Add series CAPACITANCE") << "\n";
    } else if (hint.primary_hint == "add_inductance") {
        std::cout << translation.get("MATCH_X_CAPACITIVE", "  Reactance is CAPACITIVE (X < 0): Add series INDUCTANCE") << "\n";
    } else {
        std::cout << translation.get("MATCH_X_GOOD", "  Reactance is near zero: Good reactance") << "\n";
    }
    
    // Resistance hint
    if (hint.secondary_hint == "r_too_low") {
        std::cout << translation.get("MATCH_R_LOW", "  Resistance is LOW (R < 50 Ohm): Consider L-network with step-up") << "\n";
    } else if (hint.secondary_hint == "r_too_high") {
        std::cout << translation.get("MATCH_R_HIGH", "  Resistance is HIGH (R > 50 Ohm): Consider L-network with step-down") << "\n";
    } else {
        std::cout << translation.get("MATCH_R_GOOD", "  Resistance is near 50 Ohm: Good match") << "\n";
    }
    
    std::cout << "\n" << translation.get("MATCH_DISCLAIMER", "Note: These are rough guidelines.") << "\n";
    
    // Ask if user wants to scan again
    std::cout << "\n" << translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") << "\n";
    continueScanning = getYesNo("");
    }
}

// Remaining implementations will continue in next part...
// Cable length, cable fault, cable attenuation, filter check, comparison, markers, config
