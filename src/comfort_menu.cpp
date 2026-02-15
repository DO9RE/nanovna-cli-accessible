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

// Ensure cfg.step is valid for the current start/end range.
// Auto-calculates a reasonable step (~100 points) if step is 0 or too large.
static void ensureValidStep(AppConfig& cfg) {
    if (cfg.start_freq < cfg.end_freq &&
        (cfg.step == 0 || cfg.step > (cfg.end_freq - cfg.start_freq) / 2)) {
        cfg.step = (cfg.end_freq - cfg.start_freq) / 100;
        if (cfg.step == 0) cfg.step = 1000;
    }
}

// Helper function to generate prompts with depth indication
std::string ConsoleUI::getPromptWithDepth(const std::string& promptKey, int depth) const {
    std::string prompt = translation.get(promptKey, promptKey);
    // If depth is USE_NAVIGATION_STACK (-1), use navigation stack; otherwise use provided depth
    if (depth == USE_NAVIGATION_STACK) {
        return prompt + " " + navStack.getPromptSuffix();
    } else {
        return prompt + " " + getDepthIndicator(depth);
    }
}

// Get just the depth indicator
std::string ConsoleUI::getDepthIndicator(int depth) const {
    std::string depthIndicator;
    for (int i = 0; i < depth; ++i) {
        depthIndicator += ">";
    }
    return depthIndicator;
}

// Helper to ensure port is selected, offering to navigate to port selection if not
bool ConsoleUI::ensurePortSelected(NanoVNAProtocol* proto) {
    if (!cfg.serial_port.empty()) {
        return true;  // Port is already selected
    }
    
    print(translation.get("ERROR_NO_PORT", "Error: No COM port selected. Use (P)ort to select a port first.") + "\n");
    print(translation.get("PORT_SELECT_OFFER", "Would you like to select a port now?") + "\n");
    
    if (!getYesNo("")) {
        return false;
    }
    
    // Call the port selection function
    return interactiveSelectPort(proto);
}

// Helper to configure measurement settings (band or custom range)
bool ConsoleUI::configureMeasurementSettings() {
    print(formatHeading(translation.get("CONFIG_MEASURE_TITLE", "Configure Measurement Settings")));
    print(translation.get("CONFIG_MEASURE_SELECT", "Select measurement range:") + "\n");
    print(translation.get("CONFIG_MEASURE_BAND", "B. Select from amateur radio bands") + "\n");
    print(translation.get("CONFIG_MEASURE_CUSTOM", "C. Enter custom frequency range") + "\n");
    print(translation.get("CONFIG_MEASURE_KEEP", "K. Keep current settings") + "\n");
    print(getPromptWithDepth("CONFIG_MEASURE_PROMPT", 3) + " ");
    
    setUIContext("config_measure", {
        {"b", translation.get("CONFIG_MEASURE_BAND", "Select from amateur radio bands"), false},
        {"c", translation.get("CONFIG_MEASURE_CUSTOM", "Enter custom frequency range"), false},
        {"k", translation.get("CONFIG_MEASURE_KEEP", "Keep current settings"), false}
    });
    
    // Use raw mode input with Escape support (Phase 4)
    auto inputResult = readRawLineInput("");
    if (inputResult.cancelled || inputResult.value.empty()) {
        return false;
    }
    
    char choice = inputResult.value[0];
    if (choice >= 'A' && choice <= 'Z') choice = choice - 'A' + 'a';
    
    if (choice == 'k') {
        // Keep current settings - but check if they are valid
        if (cfg.start_freq == 0 || cfg.end_freq == 0 || cfg.step == 0) {
            print(translation.get("ERROR_NO_RANGE", "No frequency range configured. Please set range first.") + "\n");
            return false;
        }
        return true;
    } else if (choice == 'b') {
        // Select from bands
        auto bands = getAmateurBands(cfg.bandplan);
        
        print("\n" + translation.get("CONFIG_MEASURE_BAND_SELECT", "Select a band:") + "\n");
        print(translation.get("CONFIG_MEASURE_ALL_BANDS", "A. All bands (full sweep)") + "\n");
        
        for (size_t i = 0; i < bands.size(); ++i) {
            std::ostringstream oss;
            oss << (i + 1) << ". " << bands[i].name << " (" << (bands[i].start_hz / 1000000.0) << " - " << (bands[i].end_hz / 1000000.0) << " MHz)\n";
            print(oss.str());
        }
        
        print(getPromptWithDepth("CONFIG_MEASURE_BAND_PROMPT", 4) + " ");
        
        {
            std::vector<UIAction> bandActions;
            bandActions.push_back({"a", translation.get("CONFIG_MEASURE_ALL_BANDS", "All bands"), false});
            for (size_t i = 0; i < bands.size(); ++i) {
                bandActions.push_back({std::to_string(i + 1), bands[i].name, i >= 9});
            }
            setUIContext("band_select", bandActions);
        }
        
        // Use raw mode input with Escape support (Phase 4)
        auto bandResult = readRawLineInput("");
        if (bandResult.cancelled || bandResult.value.empty()) {
            return false;
        }
        std::string bandInput = bandResult.value;
        
        if (bandInput.empty()) {
            return false;
        }
        
        if (bandInput[0] == 'a' || bandInput[0] == 'A') {
            // Full sweep across all bands
            cfg.start_freq = bands.front().start_hz;
            cfg.end_freq = bands.back().end_hz;
            ensureValidStep(cfg);
            print(translation.format("CONFIG_MEASURE_SET_ALL", "Set range: {0} Hz to {1} Hz (all bands)", 
                cfg.start_freq, cfg.end_freq) + "\n");
            return true;
        } else {
            try {
                int bandNum = std::stoi(bandInput);
                if (bandNum < 1 || bandNum > static_cast<int>(bands.size())) {
                    print(translation.get("CONFIG_MEASURE_INVALID_BAND", "Invalid band number") + "\n");
                    return false;
                }
                
                const auto& band = bands[bandNum - 1];
                uint64_t scan_start, scan_end;
                getBandWithMargin(band, 5.0, scan_start, scan_end);  // 5% margin
                cfg.start_freq = scan_start;
                cfg.end_freq = scan_end;
                ensureValidStep(cfg);
                
                print(translation.format("CONFIG_MEASURE_SET_BAND", "Set range for {0}: {1} Hz to {2} Hz", 
                    band.name, cfg.start_freq, cfg.end_freq) + "\n");
                return true;
            } catch (...) {
                print(translation.get("CONFIG_MEASURE_INVALID_INPUT", "Invalid input") + "\n");
                return false;
            }
        }
    } else if (choice == 'c') {
        // Custom frequency range with sequential input and backtracking (Phase 3 & 4)
        // Escape in step 2 returns to step 1, Escape in step 1 exits
        
        uint64_t start = 0;
        uint64_t end = 0;
        std::string startInput;
        std::string endInput;
        
        // Sequential input loop - allows backtracking
        while (true) {
            // Step 1: Get start frequency
            print("\n" + translation.get("CONFIG_MEASURE_CUSTOM_START", "Enter start frequency in Hz: > "));
            auto startResult = readRawLineInput("", startInput);  // Show previous value if returning from step 2
            
            if (startResult.cancelled) {
                // Escape in step 1 - cancel entire operation
                return false;
            }
            
            startInput = startResult.value;
            
            if (!parseFrequencyString(startInput, start)) {
                print(translation.get("CONFIG_MEASURE_INVALID_INPUT", "Invalid input") + "\n");
                continue;  // Ask again
            }
            
            // Step 2: Get end frequency (with backtracking support)
            while (true) {
                print(translation.get("CONFIG_MEASURE_CUSTOM_END", "Enter end frequency in Hz: > "));
                auto endResult = readRawLineInput("", endInput, true);  // silentCancel = true, we handle the message
                
                if (endResult.cancelled) {
                    // Escape in step 2 - return to step 1 (not cancelling, just going back)
                    print(translation.get("GOING_BACK", "[Going back to previous step]") + "\n");
                    break;  // Break inner loop, continue outer loop
                }
                
                endInput = endResult.value;
                
                if (!parseFrequencyString(endInput, end)) {
                    print(translation.get("CONFIG_MEASURE_INVALID_INPUT", "Invalid input") + "\n");
                    continue;  // Ask again
                }
                
                if (start >= end) {
                    print(translation.get("CONFIG_MEASURE_INVALID_RANGE", "Invalid range: start must be less than end") + "\n");
                    continue;  // Ask again
                }
                
                // Both steps complete successfully
                cfg.start_freq = start;
                cfg.end_freq = end;
                ensureValidStep(cfg);
                goto frequency_input_complete;  // Exit both loops
            }
            // If we get here, user pressed Escape in step 2, loop back to step 1
        }
        
        frequency_input_complete:  // Label for successful completion
            
        print(translation.format("CONFIG_MEASURE_SET_CUSTOM", "Set custom range: {0} Hz to {1} Hz", 
            cfg.start_freq, cfg.end_freq) + "\n");
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
                    print(translation.get("NO_DATA_S21_OFFER_MEASURE", "No S21 measurement data available. Would you like to perform an S21 (through) measurement now?") + "\n");
                } else {
                    // We have valid S11/S21 data covering the range - ask user what to do
                    print(translation.get("EXISTING_DATA_FOUND", "Existing measurement data found that covers the current range.") + "\n");
                    print(translation.format("EXISTING_DATA_RANGE", "  Current range: {0} - {1} MHz", 
                        pts.front().freq / 1000000.0, pts.back().freq / 1000000.0) + "\n");
                    print(translation.get("USE_EXISTING_OR_NEW", "Would you like to:") + "\n");
                    print(translation.get("USE_EXISTING", "  1. Use existing data") + "\n");
                    print(translation.get("CONFIG_NEW", "  2. Configure new measurement") + "\n");
                    print(getPromptWithDepth("ENSURE_DATA_PROMPT", 3) + " ");
                    
                    setUIContext("data_choice", {
                        {"1", translation.get("USE_EXISTING", "Use existing data"), false},
                        {"2", translation.get("CONFIG_NEW", "Configure new measurement"), false}
                    });
                    
                    // Use raw mode input with Escape support (Phase 4)
                    auto choiceResult = readRawLineInput("");
                    if (choiceResult.cancelled || choiceResult.value.empty()) {
                        return false;
                    }
                    std::string choice = choiceResult.value;
                    
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
                print(translation.get("EXISTING_DATA_FOUND", "Existing measurement data found that covers the current range.") + "\n");
                print(translation.format("EXISTING_DATA_RANGE", "  Current range: {0} - {1} MHz", 
                    pts.front().freq / 1000000.0, pts.back().freq / 1000000.0) + "\n");
                print(translation.get("USE_EXISTING_OR_NEW", "Would you like to:") + "\n");
                print(translation.get("USE_EXISTING", "  1. Use existing data") + "\n");
                print(translation.get("CONFIG_NEW", "  2. Configure new measurement") + "\n");
                print(getPromptWithDepth("CONFIG_CHOICE_PROMPT", 3) + " ");
                
                setUIContext("data_choice", {
                    {"1", translation.get("USE_EXISTING", "Use existing data"), false},
                    {"2", translation.get("CONFIG_NEW", "Configure new measurement"), false}
                });
                
                // Use raw mode input with Escape support (Phase 4)
                auto choiceResult = readRawLineInput("");
                if (choiceResult.cancelled || choiceResult.value.empty()) {
                    return false;
                }
                std::string choice = choiceResult.value;
                
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
            print(translation.get("DATA_OUT_OF_RANGE", "Existing measurement data does not cover the current range.") + "\n");
            print(translation.format("EXISTING_DATA_RANGE", "  Existing: {0} - {1} MHz", 
                pts.front().freq / 1000000.0, pts.back().freq / 1000000.0) + "\n");
            print(translation.format("REQUESTED_RANGE", "  Requested: {0} - {1} MHz", 
                cfg.start_freq / 1000000.0, cfg.end_freq / 1000000.0) + "\n");
            
            // Ask if they want to configure settings or use current
            print(translation.get("RECONFIGURE_OR_MEASURE", "Would you like to:") + "\n");
            print(translation.get("MEASURE_CURRENT", "  1. Measure with current settings") + "\n");
            print(translation.get("CONFIG_NEW", "  2. Configure new measurement") + "\n");
            print(getPromptWithDepth("CONFIG_CHOICE_PROMPT", 3) + " ");
            
            setUIContext("measure_choice", {
                {"1", translation.get("MEASURE_CURRENT", "Measure with current settings"), false},
                {"2", translation.get("CONFIG_NEW", "Configure new measurement"), false}
            });
            
            // Use raw mode input with Escape support (Phase 4)
            auto choiceResult = readRawLineInput("");
            if (choiceResult.cancelled || choiceResult.value.empty()) {
                return false;
            }
            std::string choice = choiceResult.value;
            
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
            print(translation.get("EXISTING_DATA_FOUND", "Existing measurement data found.") + "\n");
            print(translation.format("EXISTING_DATA_RANGE", "  Current range: {0} - {1} MHz", 
                pts.front().freq / 1000000.0, pts.back().freq / 1000000.0) + "\n");
            print(translation.get("USE_EXISTING_OR_NEW", "Would you like to:") + "\n");
            print(translation.get("USE_EXISTING", "  1. Use existing data") + "\n");
            print(translation.get("CONFIG_NEW", "  2. Configure new measurement") + "\n");
            print(getPromptWithDepth("CONFIG_CHOICE_PROMPT", 3) + " ");
            
            setUIContext("data_choice", {
                {"1", translation.get("USE_EXISTING", "Use existing data"), false},
                {"2", translation.get("CONFIG_NEW", "Configure new measurement"), false}
            });
            
            // Use raw mode input with Escape support (Phase 4)
            auto choiceResult = readRawLineInput("");
            if (choiceResult.cancelled || choiceResult.value.empty()) {
                return false;
            }
            std::string choice = choiceResult.value;
            
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
        print(translation.get("NO_DATA_AVAILABLE", "No measurement data available.") + "\n");
        
        // Ask if they want to configure settings first
        print(translation.get("CONFIG_BEFORE_MEASURE", "Would you like to configure measurement settings before measuring?") + "\n");
        if (getYesNo("")) {
            if (!configureMeasurementSettings()) {
                return false;  // User canceled configuration
            }
        }
        // Fall through to measurement prompt below
    }
    
    // At this point, we need to perform a measurement
    if (needsS21) {
        print(translation.get("NO_DATA_S21_OFFER_MEASURE", "Would you like to perform an S21 (through) measurement now?") + "\n");
    } else {
        print(translation.get("NO_DATA_OFFER_MEASURE", "Would you like to perform a measurement now?") + "\n");
    }
    
    // Ask user if they want to measure
    if (!getYesNo("")) {
        print(translation.get("CANCELLED", "Measurement canceled.") + "\n");
        return false;
    }
    
    // Check if port is selected, and offer to select if not
    if (!ensurePortSelected(proto)) {
        return false;
    }
    
    // Show setup instructions
    if (needsS21) {
        print(translation.get("MEASURE_SETUP_S21", "Setup: Connect cable or component between Port 1 (CH0) and Port 2 (CH1)") + "\n");
    } else {
        print(translation.get("MEASURE_SETUP_S11", "Setup: Connect antenna or cable to Port 1") + "\n");
    }
    
    print(translation.get("MEASURE_CONFIRM", "Press Enter when ready to measure, or ESC to cancel"));
    
    setUIContext("measure_confirm", {
        {"\r", translation.get("MEASURE_START", "Start Measurement"), false}
    });
    
    int ch = 0;
    bool hasInput = false;
    
    // Poll for input from web interface or console
    while (!hasInput) {
        // Check for web interface input first
        if (webServer && webServer->isRunning() && webServer->hasInput()) {
            std::string webInput = webServer->readInput();
            if (!webInput.empty()) {
                if (webInput[0] == '\x1B') {
                    ch = 27;  // ESC key
                } else {
                    ch = static_cast<unsigned char>(webInput[0]);
                }
                hasInput = true;
            }
        }
        
        // Check for keyboard input if no web input
        if (!hasInput && consoleInput->kbhit()) {
            ch = consoleInput->getch();
            hasInput = true;
        }
        
        if (!hasInput) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    char key = static_cast<char>(ch);
    if (key == 27) {  // ESC
        print("\n" + translation.get("CANCELLED", "Measurement canceled.") + "\n");
        return false;
    }
    print("\n");
    
    // Ensure step is valid before measuring
    ensureValidStep(cfg);
    
    // Perform measurement using existing functions
    print(translation.get("MEASURE_PERFORMING", "Performing measurement...") + "\n");
    auto newPts = performMeasurementWithTiming(proto, cfg.start_freq, cfg.end_freq, cfg.step);
    
    if (newPts.empty()) {
        print(translation.get("MEASURE_FAILED", "Measurement failed.") + "\n");
        return false;
    }
    
    // Update the data
    pts = newPts;
    print(translation.format("MEASURE_COMPLETE", "Measurement complete. {0} data points collected.", pts.size()) + "\n");
    
    return true;
}

// Helper function to read yes/no
bool ConsoleUI::getYesNo(const std::string& prompt) {
    // Get localized yes key (fallback to 'y' if translation fails)
    std::string yesKey = translation.get("YES_KEY", "y");
    std::string noKey = translation.get("NO_KEY", "n");
    std::string yesNoPrompt = translation.get("YES_NO_PROMPT", "(y/n)");
    
    // Ensure yesKey is not empty (safety check, should never happen with fallback)
    if (yesKey.empty()) {
        yesKey = "y";
    }
    if (noKey.empty()) {
        noKey = "n";
    }
    // yesKey is guaranteed to be non-empty from this point forward
    
    print(prompt + " " + yesNoPrompt + ": ");
    
    // Update web interface context with Yes/No buttons
    setUIContext("yes_no", {
        {yesKey, translation.get("YES_LABEL", "Yes"), false},
        {noKey, translation.get("NO_LABEL", "No"), false}
    });
    
    int ch = 0;
    bool hasInput = false;
    
    // Poll for input from web interface or console
    while (!hasInput) {
        // Check for web interface input first
        if (webServer && webServer->isRunning() && webServer->hasInput()) {
            std::string webInput = webServer->readInput();
            if (!webInput.empty()) {
                if (webInput[0] == '\x1B') {
                    ch = 27;  // ESC key
                } else {
                    ch = static_cast<unsigned char>(webInput[0]);
                }
                hasInput = true;
            }
        }
        
        // Check for keyboard input if no web input
        if (!hasInput && consoleInput->kbhit()) {
            ch = consoleInput->getch();
            hasInput = true;
        }
        
        if (!hasInput) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    char key = static_cast<char>(ch);
    
    // Handle ESC key as "No"
    if (key == 27) {
        print("ESC\n");
        return false;
    }
    
    // Convert uppercase to lowercase
    if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
    print(std::string(1, key) + "\n");
    
    // Check against localized yes key (safe: yesKey is non-empty)
    return key == yesKey[0];
}

// Helper function to read frequency
uint64_t ConsoleUI::getFrequencyInput(const std::string& prompt) {
    print(prompt);
    // Use raw mode input with Escape support (Phase 4)
    auto result = readRawLineInput("");
    if (result.cancelled || result.value.empty()) {
        return 0;
    }
    uint64_t freq = 0;
    if (parseFrequencyString(result.value, freq)) {
        return freq;
    }
    return 0;
}

// Helper function to read double with validation
double ConsoleUI::getDoubleInput(const std::string& prompt, double min_val, double max_val) {
    print(prompt);
    // Use raw mode input with Escape support (Phase 4)
    auto result = readRawLineInput("");
    if (result.cancelled || result.value.empty()) {
        return min_val;
    }
    try {
        double val = std::stod(result.value);
        if (val < min_val) return min_val;
        if (val > max_val) return max_val;
        return val;
    } catch (...) {
        return min_val;
    }
}

// Task 1.18: Input validation helper - get single character menu choice
char ConsoleUI::getMenuChoice(const std::string& prompt) {
    if (!prompt.empty()) {
        print(prompt + " ");
    }
    // Use raw mode input with Escape support (Phase 4)
    auto result = readRawLineInput("");
    if (result.cancelled || result.value.empty()) {
        return '\0';  // Return null char for cancelled/empty input
    }
    return result.value[0];
}

// Task 1.18: Input validation helper - get line input with empty check
std::string ConsoleUI::getLineInput(const std::string& prompt) {
    if (!prompt.empty()) {
        print(prompt + " ");
    }
    
    // Use raw mode input with Escape support (Phase 4)
    auto result = readRawLineInput("");
    if (result.cancelled) {
        return "";  // Return empty string for cancelled input
    }
    
    return result.value;  // Caller can check if empty
}

// Task 1.17: Cable preset selection helper
ConsoleUI::CableSelection ConsoleUI::selectCablePreset() {
    CableSelection result;
    result.velocity_factor = 0.66;  // Default
    result.loss_db_per_m = 0.0;
    result.name = "Default";
    result.selected = false;
    
    auto presets = getCablePresets();
    print("\n" + translation.get("CABLE_PRESET_TITLE", "Select cable type:") + "\n");
    
    for (size_t i = 0; i < presets.size(); ++i) {
        std::ostringstream oss;
        oss << (i + 1) << ". " << presets[i].name << " (VF: " << presets[i].velocity_factor << ")";
        if (!presets[i].description.empty()) {
            oss << " - " << presets[i].description;
        }
        oss << "\n";
        print(oss.str());
    }
    
    print(getPromptWithDepth("CABLE_PRESET_TITLE", 4) + " ");
    
    {
        std::vector<UIAction> presetActions;
        for (size_t i = 0; i < presets.size(); ++i) {
            presetActions.push_back({std::to_string(i + 1), presets[i].name, i >= 9});
        }
        setUIContext("cable_preset", presetActions);
    }
    
    std::string presetInput = getLineInput("");
    
    if (presetInput.empty()) {
        return result;  // User cancelled
    }
    
    try {
        size_t choice = std::stoull(presetInput);
        if (choice >= 1 && choice <= presets.size()) {
            const auto& preset = presets[choice - 1];
            
            if (preset.name == "Custom") {
                // Custom entry
                print(translation.get("CONFIG_CABLE_CUSTOM_VF", "Enter custom values:") + "\n");
                result.velocity_factor = getDoubleInput(
                    translation.get("CABLE_LEN_VF", "Enter Velocity Factor (0.6-0.95): > "), 
                    0.6, 0.95);
                result.loss_db_per_m = getDoubleInput(
                    translation.get("CONFIG_CABLE_CUSTOM_LOSS", "Enter loss in dB/100m at 100 MHz (0 to skip): > "),
                    0.0, 200.0) / 100.0;
                result.name = "Custom";
                result.selected = true;
            } else {
                // Use preset
                result.velocity_factor = preset.velocity_factor;
                result.loss_db_per_m = preset.loss_db_per_100m_at_100mhz / 100.0;
                result.name = preset.name;
                result.selected = true;
                print(translation.format("CABLE_PRESET_SELECTED", 
                    "Using {0}: VF = {1}", preset.name, preset.velocity_factor) + "\n");
            }
        } else {
            print(translation.get("INVALID_INPUT", "Invalid selection, using default") + "\n");
        }
    } catch (...) {
        print(translation.get("INVALID_INPUT", "Invalid input, using default") + "\n");
    }
    
    return result;
}

// Main comfort functions menu
void ConsoleUI::comfortFunctionsMenu(std::vector<MeasurementPoint>& lastPts, NanoVNAProtocol* proto) {
    // Check if continuous sweep is enabled and disable it
    bool continuous_sweep_was_enabled = cfg.continuous_sweep_enabled;
    if (cfg.continuous_sweep_enabled) {
        cfg.continuous_sweep_enabled = false;
        print("\n" + translation.get("UMENU_SWEEP_DISABLED", "Note: Continuous sweep has been automatically disabled to prevent data conflicts.") + "\n");
    }
    
    while (true) {
        clearScreen();  // Clear screen at the start of each loop iteration
        print(formatHeading(translation.get("UMENU_TITLE", "Comfort Functions Menu")));
        print(translation.get("UMENU_SUBTITLE", "Convenient measurement tools for antenna and cable analysis") + "\n\n");
        
        print(translation.get("UMENU_1", "1. Band Suitability Check (S11)") + "\n");
        print(translation.get("UMENU_1_DESC", "   Check antenna performance on amateur radio bands") + "\n");
        print(translation.get("UMENU_2", "2. Resonance Finder (S11)") + "\n");
        print(translation.get("UMENU_2_DESC", "   Find frequencies with minimum SWR") + "\n");
        print(translation.get("UMENU_3", "3. SWR Bandwidth Calculator (S11)") + "\n");
        print(translation.get("UMENU_3_DESC", "   Calculate 1.5:1 and 2:1 SWR bandwidth") + "\n");
        print(translation.get("UMENU_4", "4. Feedpoint Impedance Report (S11)") + "\n");
        print(translation.get("UMENU_4_DESC", "   Detailed impedance at selected frequency") + "\n");
        print(translation.get("UMENU_5", "5. Matching Hints (S11)") + "\n");
        print(translation.get("UMENU_5_DESC", "   Suggestions for impedance matching") + "\n");
        print(translation.get("UMENU_6", "6. Cable Length Measurement (S11)") + "\n");
        print(translation.get("UMENU_6_DESC", "   Estimate cable length from phase") + "\n");
        print(translation.get("UMENU_7", "7. Cable Fault Detection (S11)") + "\n");
        print(translation.get("UMENU_7_DESC", "   Detect shorts, opens, and damage") + "\n");
        print(translation.get("UMENU_8", "8. Cable Attenuation (S21)") + "\n");
        print(translation.get("UMENU_8_DESC", "   Measure cable loss per meter") + "\n");
        print(translation.get("UMENU_9", "9. Filter Quick Check (S21)") + "\n");
        print(translation.get("UMENU_9_DESC", "   Analyze filter characteristics") + "\n");
        print(translation.get("UMENU_10", "10. Before/After Comparison") + "\n");
        print(translation.get("UMENU_10_DESC", "    Compare two measurements") + "\n");
        print(translation.get("UMENU_11", "11. Auto-Marker Placement") + "\n");
        print(translation.get("UMENU_11_DESC", "    Automatically set markers at key points") + "\n");
        print(translation.get("UMENU_12", "12. Configuration") + "\n");
        print(translation.get("UMENU_12_DESC", "    Set velocity factor, SWR limits, etc.") + "\n\n");
        
        print(translation.get("HELP_COMMAND", "Press H for help") + "\n");
        print(translation.get("MSG_PRESS_ESC_BACK", "Press ESC to go back") + "\n");
        
        setUIContext("comfort_menu", {
            {"1", translation.get("UMENU_1", "Band Suitability Check"), false},
            {"2", translation.get("UMENU_2", "Resonance Finder"), false},
            {"3", translation.get("UMENU_3", "SWR Bandwidth Calculator"), false},
            {"4", translation.get("UMENU_4", "Feedpoint Impedance Report"), false},
            {"5", translation.get("UMENU_5", "Matching Hints"), false},
            {"6", translation.get("UMENU_6", "Cable Length Measurement"), false},
            {"7", translation.get("UMENU_7", "Cable Fault Detection"), false},
            {"8", translation.get("UMENU_8", "Cable Attenuation"), false},
            {"9", translation.get("UMENU_9", "Filter Quick Check"), false},
            {"10", translation.get("UMENU_10", "Before/After Comparison"), true},
            {"11", translation.get("UMENU_11", "Auto-Marker Placement"), true},
            {"12", translation.get("UMENU_12", "Configuration"), true},
            {"h", translation.get("MENU_HELP", "(H)elp"), false}
        });
        
        print(getPromptWithDepth("UMENU_PROMPT", 2) + " ");
        
        // Read input with Enter confirmation to allow multi-digit numbers
        std::string input;
        
        // Check for ESC key while allowing typing
        // User can press ESC at any time, or Enter to submit
        bool escPressed = false;
        int maxIterations = 30000;  // Timeout after ~25 minutes (30000 * 50ms)
        int iterations = 0;
        while (iterations < maxIterations) {
            int ch = 0;
            bool hasChar = false;
            
            // Check for web interface input first
            if (webServer && webServer->isRunning() && webServer->hasInput()) {
                std::string webInput = webServer->readInput();
                if (!webInput.empty()) {
                    if (webInput[0] == '\x1B') {
                        ch = 27;  // ESC key
                    } else if (webInput[0] == '\r' || webInput[0] == '\n') {
                        ch = '\r';  // Enter key
                    } else if (webInput[0] == '\x08' || webInput[0] == '\x7F') {
                        ch = '\b';  // Backspace
                    } else {
                        ch = static_cast<unsigned char>(webInput[0]);
                    }
                    hasChar = true;
                }
            }
            
            // Check for keyboard input if no web input
            if (!hasChar && consoleInput->kbhit()) {
                ch = consoleInput->getch();
                hasChar = true;
            }
            
            if (hasChar) {
                if (ch == 27) {  // ESC key
                    escPressed = true;
                    print("\n");
                    break;
                } else if (ch == '\r' || ch == '\n') {  // Enter key
                    print("\n");
                    break;
                } else if (ch == '\b' || ch == 127) {  // Backspace
                    if (!input.empty()) {
                        input.pop_back();
                        print("\b \b");  // Erase character from display
                    }
                } else if (ch >= 32 && ch < 127) {  // Printable character
                    input += static_cast<char>(ch);
                    print(std::string(1, static_cast<char>(ch)));
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
                print(translation.get("UMENU_SWEEP_REENABLED", "Continuous sweep has been re-enabled.") + "\n");
            }
            return;
        }
        
        // Trim whitespace
        input.erase(0, input.find_first_not_of(" \t\n\r"));
        input.erase(input.find_last_not_of(" \t\n\r") + 1);
        
        // Check for empty input or "esc" text (for non-Windows or if user typed "esc")
        if (input.empty() || 
            (input.length() >= 3 && (input.substr(0, 3) == "esc" || input.substr(0, 3) == "ESC" || input.substr(0, 3) == "Esc"))) {
            // Re-enable continuous sweep if it was enabled
            if (continuous_sweep_was_enabled) {
                cfg.continuous_sweep_enabled = true;
                print(translation.get("UMENU_SWEEP_REENABLED", "Continuous sweep has been re-enabled.") + "\n");
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
                print(HelpModule::getComfortFunctionsMenuHelp(translation));
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
                print(translation.get("MSG_UNKNOWN_COMMAND", "Unknown command.") + "\n");
                break;
        }
    }
    clearScreen();
}

// Band suitability check
void ConsoleUI::bandSuitabilityCheck(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        print(formatHeading(translation.get("BAND_SUIT_TITLE", "Band Suitability Check")));
    
    auto bands = getAmateurBands(cfg.bandplan);
    
    print(translation.get("BAND_SUIT_SELECT", "Select band to check:") + "\n");
    print(translation.get("BAND_SUIT_ALL", "A. Check all bands") + "\n");
    
    // List bands
    for (size_t i = 0; i < bands.size(); ++i) {
        std::ostringstream oss;
        oss << (i + 1) << ". " << bands[i].name << " (" << (bands[i].start_hz / 1000000.0) << " - " << (bands[i].end_hz / 1000000.0) << " MHz)\n";
        print(oss.str());
    }
    
    print(getPromptWithDepth("BAND_SUIT_PROMPT", 3) + " ");
    
    {
        std::vector<UIAction> bandActions;
        bandActions.push_back({"a", translation.get("BAND_SUIT_ALL", "Check all bands"), false});
        for (size_t i = 0; i < bands.size(); ++i) {
            bandActions.push_back({std::to_string(i + 1), bands[i].name, i >= 9});
        }
        setUIContext("band_suit_select", bandActions);
    }
    
    // Use raw mode input with Escape support (Phase 4)
    auto inputResult = readRawLineInput("");
    if (inputResult.cancelled || inputResult.value.empty()) {
        break;  // Exit to parent menu
    }
    std::string input = inputResult.value;
    
    bool checkAll = (input[0] == 'a' || input[0] == 'A');
    
    if (checkAll) {
        // Check all bands - measure each band individually for efficiency
        for (const auto& band : bands) {
            print("\n" + translation.format("BAND_SUIT_CHECKING", "Checking band: {0}", band.name) + "\n");
            
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
                print(translation.get("REMEASURE_OFFER", "Would you like to measure this band now?") + "\n");
                if (!getYesNo("")) {
                    print(translation.format("BAND_SUIT_SKIPPED", "  Skipping band {0}", band.name) + "\n");
                    continue;  // Skip this band
                }
                
                // Check if port is selected, and offer to select if not
                if (!ensurePortSelected(proto)) {
                    print(translation.format("BAND_SUIT_MEASURE_FAILED", "  Failed to measure band {0}", band.name) + "\n");
                    continue;  // Skip this band
                }
                
                // Show setup instructions
                print(translation.get("MEASURE_SETUP_S11", "Setup: Connect antenna or cable to Port 1") + "\n");
                print(translation.get("MEASURE_CONFIRM", "Press Enter when ready to measure, or ESC to cancel"));
                
                setUIContext("measure_confirm", {
                    {"\r", translation.get("MEASURE_START", "Start Measurement"), false}
                });
                
                {
                    int ch = 0;
                    bool hasInput = false;
                    
                    // Poll for input from web interface or console
                    while (!hasInput) {
                        if (webServer && webServer->isRunning() && webServer->hasInput()) {
                            std::string webInput = webServer->readInput();
                            if (!webInput.empty()) {
                                if (webInput[0] == '\x1B') {
                                    ch = 27;
                                } else {
                                    ch = static_cast<unsigned char>(webInput[0]);
                                }
                                hasInput = true;
                            }
                        }
                        if (!hasInput && consoleInput->kbhit()) {
                            ch = consoleInput->getch();
                            hasInput = true;
                        }
                        if (!hasInput) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                    }
                    
                    char key = static_cast<char>(ch);
                    if (key == 27) {  // ESC
                        print("\n" + translation.get("CANCELLED", "Measurement canceled.") + "\n");
                        print(translation.format("BAND_SUIT_SKIPPED", "  Skipping band {0}", band.name) + "\n");
                        continue;
                    }
                }
                print("\n");
                
                // Ensure step is valid before measuring
                ensureValidStep(cfg);
                
                // Perform measurement using existing functions
                print(translation.get("MEASURE_PERFORMING", "Performing measurement...") + "\n");
                auto newPts = performMeasurementWithTiming(proto, cfg.start_freq, cfg.end_freq, cfg.step);
                
                if (newPts.empty()) {
                    print(translation.get("MEASURE_FAILED", "Measurement failed.") + "\n");
                    print(translation.format("BAND_SUIT_MEASURE_FAILED", "  Failed to measure band {0}", band.name) + "\n");
                    continue;  // Skip this band
                }
                
                // Update the data
                pts = newPts;
                print(translation.format("MEASURE_COMPLETE", "Measurement complete. {0} data points collected.", pts.size()) + "\n");
            }
            
            // Verify we have data for this band after potential measurement
            if (pts.empty() || pts.front().freq > band.end_hz || pts.back().freq < band.start_hz) {
                print(translation.format("BAND_SUIT_OUT_OF_RANGE", 
                    "Band {0} is outside measurement range", band.name) + "\n");
                continue;
            }
            
            auto result = comfortFuncs.checkBandSuitability(pts, band);
            
            print(translation.format("BAND_SUIT_RESULT_TITLE", "Results for {0}:", band.name) + "\n");
            print(translation.format("BAND_SUIT_CENTER", "  SWR at band center ({1} Hz): {2}", 
                band.name, band.center(), result.swr_at_center) + "\n");
            print(translation.format("BAND_SUIT_MIN", "  Minimum SWR in band: {0} at {1} Hz", 
                result.min_swr, result.min_swr_freq_hz) + "\n");
            print(translation.format("BAND_SUIT_RL", "  Return Loss at center: {0} dB", 
                result.rl_at_center_db) + "\n");
            
            if (result.swr_bandwidth.bandwidth_hz > 0) {
                print(translation.format("BAND_SUIT_BW", "  2:1 SWR Bandwidth: {0} kHz ({1} Hz to {2} Hz)", 
                    result.swr_bandwidth.bandwidth_khz(), result.swr_bandwidth.freq_low_hz, result.swr_bandwidth.freq_high_hz) + "\n");
            } else {
                print(translation.get("BAND_SUIT_NO_BW", "  No 2:1 bandwidth found in this band") + "\n");
            }
            
            if (result.passed) {
                print(translation.format("BAND_SUIT_PASS", "  Result: PASS (SWR <= {0})", 
                    comfortFuncs.getConfig().swr_threshold) + "\n");
            } else {
                print(translation.format("BAND_SUIT_FAIL", "  Result: FAIL (SWR > {0})", 
                    comfortFuncs.getConfig().swr_threshold) + "\n");
            }
        }
    } else {
        // Check specific band
        try {
            int bandNum = std::stoi(input);
            if (bandNum < 1 || bandNum > static_cast<int>(bands.size())) {
                print(translation.get("COMFORT_INVALID_BAND", "Invalid band number") + "\n");
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
                print(translation.format("BAND_SUIT_OUT_OF_RANGE", 
                    "Band {0} is outside measurement range. Current range: {1} - {2} MHz", 
                    band.name, pts.front().freq / 1000000.0, pts.back().freq / 1000000.0) + "\n");
                
                // Offer to remeasure with correct range
                print(translation.get("REMEASURE_OFFER", "Would you like to measure this band now?") + "\n");
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
            
            print("\n" + translation.format("BAND_SUIT_RESULT_TITLE", "Band: {0}", band.name) + "\n");
            print(translation.format("BAND_SUIT_CENTER", "  SWR at band center ({1} Hz): {2}", 
                band.name, band.center(), result.swr_at_center) + "\n");
            print(translation.format("BAND_SUIT_MIN", "  Minimum SWR in band: {0} at {1} Hz", 
                result.min_swr, result.min_swr_freq_hz) + "\n");
            print(translation.format("BAND_SUIT_RL", "  Return Loss at center: {0} dB", 
                result.rl_at_center_db) + "\n");
            
            if (result.swr_bandwidth.bandwidth_hz > 0) {
                print(translation.format("BAND_SUIT_BW", "  2:1 SWR Bandwidth: {0} kHz ({1} Hz to {2} Hz)", 
                    result.swr_bandwidth.bandwidth_khz(), result.swr_bandwidth.freq_low_hz, result.swr_bandwidth.freq_high_hz) + "\n");
            } else {
                print(translation.get("BAND_SUIT_NO_BW", "  No 2:1 bandwidth found in this band") + "\n");
            }
            
            if (result.passed) {
                print(translation.format("BAND_SUIT_PASS", "  Result: PASS (SWR <= {0})", 
                    comfortFuncs.getConfig().swr_threshold) + "\n");
            } else {
                print(translation.format("BAND_SUIT_FAIL", "  Result: FAIL (SWR > {0})", 
                    comfortFuncs.getConfig().swr_threshold) + "\n");
            }
        } catch (...) {
            print(translation.get("COMFORT_INVALID_INPUT", "Invalid input") + "\n");
        }
    }
    
    // Ask if user wants to scan again
    print("\n" + translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") + "\n");
    continueScanning = getYesNo("");
    }
}

// Resonance finder
void ConsoleUI::resonanceFinder(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        print(formatHeading(translation.get("RESON_TITLE", "Resonance Finder")));
    
    if (!ensureMeasurementData(pts, proto, false)) {
        return;
    }
    
    print(translation.get("RESON_ANALYZING", "Analyzing current sweep for resonances...") + "\n");
    
    auto resonances = comfortFuncs.findResonances(pts);
    
    if (resonances.empty()) {
        print(translation.get("RESON_NONE", "No clear resonance points found.") + "\n");
        print(translation.get("RESON_HINT", "Hint: Resonances are local minima in SWR.") + "\n");
        return;
    }
    
    print(translation.format("RESON_FOUND", "Found {0} resonance point(s):", resonances.size()) + "\n\n");
    
    // Show top 3 resonances
    size_t count = std::min(resonances.size(), size_t(3));
    for (size_t i = 0; i < count; ++i) {
        const auto& res = resonances[i];
        print(translation.format("RESON_POINT", "Resonance {0}:", i + 1) + "\n");
        print(translation.format("RESON_FREQ", "  Frequency: {0} Hz ({1} MHz)", 
            res.freq_hz, res.freq_hz / 1000000.0) + "\n");
        print(translation.format("RESON_SWR", "  SWR: {0}", res.swr) + "\n");
        print(translation.format("RESON_IMPEDANCE", "  Impedance: {0} + j{1} Ohm", 
            res.R, res.X) + "\n\n");
    }
    
    // Ask if user wants to scan again
    print("\n" + translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") + "\n");
    continueScanning = getYesNo("");
    }
}

// SWR bandwidth calculator
void ConsoleUI::swrBandwidthCalculator(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        print(formatHeading(translation.get("SWR_BW_TITLE", "SWR Bandwidth Calculator")));
    
    if (!ensureMeasurementData(pts, proto, false)) {
        return;
    }
    
    print(translation.get("SWR_BW_SELECT", "Select target SWR:") + "\n");
    print(translation.get("SWR_BW_1_5", "1. SWR <= 1.5:1") + "\n");
    print(translation.get("SWR_BW_2_0", "2. SWR <= 2.0:1") + "\n");
    print(translation.get("SWR_BW_3_0", "3. SWR <= 3.0:1") + "\n");
    print(translation.get("SWR_BW_CUSTOM", "4. Custom SWR value") + "\n");
    print(getPromptWithDepth("SWR_BW_PROMPT", 3) + " ");
    
    setUIContext("swr_bw_select", {
        {"1", translation.get("SWR_BW_1_5", "SWR <= 1.5:1"), false},
        {"2", translation.get("SWR_BW_2_0", "SWR <= 2.0:1"), false},
        {"3", translation.get("SWR_BW_3_0", "SWR <= 3.0:1"), false},
        {"4", translation.get("SWR_BW_CUSTOM", "Custom SWR value"), false}
    });
    
    // Use raw mode input with Escape support (Phase 4)
    auto inputResult = readRawLineInput("");
    if (inputResult.cancelled || inputResult.value.empty()) {
        return;
    }
    std::string input = inputResult.value;
    
    double target_swr = 2.0;
    if (input[0] == '1') target_swr = 1.5;
    else if (input[0] == '2') target_swr = 2.0;
    else if (input[0] == '3') target_swr = 3.0;
    else if (input[0] == '4') {
        target_swr = getDoubleInput(translation.get("SWR_BW_ENTER_CUSTOM", "Enter custom SWR value: > "), 1.0, 10.0);
    }
    
    auto ranges = comfortFuncs.findSWRBandwidth(pts, target_swr);
    
    if (ranges.empty()) {
        print(translation.format("SWR_BW_NONE", "No frequency range found with SWR <= {0}", target_swr) + "\n");
        print(translation.get("SWR_BW_HINT", "This may indicate antenna needs tuning.") + "\n");
        return;
    }
    
    print("\n" + translation.format("SWR_BW_RESULT_TITLE", "SWR Bandwidth for SWR <= {0}:", target_swr) + "\n\n");
    
    for (size_t i = 0; i < ranges.size(); ++i) {
        const auto& range = ranges[i];
        print(translation.format("SWR_BW_RANGE", "Range {0}:", i + 1) + "\n");
        print(translation.format("SWR_BW_FREQ_RANGE", "  Frequency: {0} Hz to {1} Hz", 
            range.freq_low_hz, range.freq_high_hz) + "\n");
        print(translation.format("SWR_BW_BW_KHZ", "  Bandwidth: {0} kHz", range.bandwidth_khz()) + "\n");
        print(translation.format("SWR_BW_CENTER", "  Center: {0} Hz ({1} MHz)", 
            range.center_hz, range.center_hz / 1000000.0) + "\n\n");
    }
    
    // Ask if user wants to scan again
    print("\n" + translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") + "\n");
    continueScanning = getYesNo("");
    }
}

// Feedpoint impedance report
void ConsoleUI::feedpointImpedanceReport(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        print(formatHeading(translation.get("FP_IMP_TITLE", "Feedpoint Impedance Report")));
    
    if (!ensureMeasurementData(pts, proto, false)) {
        return;
    }
    
    print(translation.get("FP_IMP_SELECT", "Select frequency:") + "\n");
    print(translation.get("FP_IMP_MIN_SWR", "S. At minimum SWR") + "\n");
    print(translation.get("FP_IMP_MANUAL", "F. Manual frequency entry") + "\n");
    print(getPromptWithDepth("FP_IMP_PROMPT", 3) + " ");
    
    setUIContext("fp_imp_select", {
        {"s", translation.get("FP_IMP_MIN_SWR", "At minimum SWR"), false},
        {"f", translation.get("FP_IMP_MANUAL", "Manual frequency entry"), false}
    });
    
    // Use raw mode input with Escape support (Phase 4)
    auto inputResult = readRawLineInput("");
    if (inputResult.cancelled || inputResult.value.empty()) {
        return;
    }
    std::string input = inputResult.value;
    
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
    
    print("\n" + translation.format("FP_IMP_REPORT_TITLE", "Impedance Report at {0} Hz ({1} MHz):", 
        report.freq_hz, report.freq_hz / 1000000.0) + "\n");
    print(translation.format("FP_IMP_R", "  Resistance (R): {0} Ohm", report.R) + "\n");
    print(translation.format("FP_IMP_X", "  Reactance (X): {0} Ohm", report.X) + "\n");
    print(translation.format("FP_IMP_Z_MAG", "  Impedance Magnitude |Z|: {0} Ohm", report.Z_mag) + "\n");
    print(translation.format("FP_IMP_PHASE", "  Phase: {0} degrees", report.phase_deg) + "\n");
    print(translation.format("FP_IMP_SWR", "  SWR: {0}", report.swr) + "\n");
    
    // Reactance type
    if (report.reactance_type == "inductive") {
        print(translation.get("FP_IMP_TYPE_INDUCTIVE", "  Reactance Type: Inductive (X > 0)") + "\n");
    } else if (report.reactance_type == "capacitive") {
        print(translation.get("FP_IMP_TYPE_CAPACITIVE", "  Reactance Type: Capacitive (X < 0)") + "\n");
    } else {
        print(translation.get("FP_IMP_TYPE_RESISTIVE", "  Reactance Type: Resistive (X ≈ 0)") + "\n");
    }
    
    // Impedance hint
    if (report.impedance_hint == "too_low") {
        print(translation.get("FP_IMP_HINT_LOW", "  Impedance Hint: Too low compared to 50 Ohm") + "\n");
    } else if (report.impedance_hint == "too_high") {
        print(translation.get("FP_IMP_HINT_HIGH", "  Impedance Hint: Too high compared to 50 Ohm") + "\n");
    } else {
        print(translation.get("FP_IMP_HINT_GOOD", "  Impedance Hint: Near 50 Ohm") + "\n");
    }
    
    // Ask if user wants to scan again
    print("\n" + translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") + "\n");
    continueScanning = getYesNo("");
    }
}

// Matching hints
void ConsoleUI::matchingHints(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    bool continueScanning = true;
    while (continueScanning) {
        print(formatHeading(translation.get("MATCH_TITLE", "Matching Hints")));
    
    if (!ensureMeasurementData(pts, proto, false)) {
        return;
    }
    
    // Use minimum SWR point
    auto min_point = comfortFuncs.findMinimumSWR(pts);
    
    print(translation.format("MATCH_USING_DATA", "Using impedance data at {0} Hz", min_point.freq_hz) + "\n");
    print(translation.format("MATCH_R_X", "  Impedance: {0} + j{1} Ohm", min_point.R, min_point.X) + "\n\n");
    
    auto hint = comfortFuncs.getMatchingHint(min_point.R, min_point.X);
    
    print(translation.get("MATCH_HINT_TITLE", "Matching Suggestions (rough guidance):") + "\n");
    
    // Reactance hint
    if (hint.primary_hint == "add_capacitance") {
        print(translation.get("MATCH_X_INDUCTIVE", "  Reactance is INDUCTIVE (X > 0): Add series CAPACITANCE") + "\n");
    } else if (hint.primary_hint == "add_inductance") {
        print(translation.get("MATCH_X_CAPACITIVE", "  Reactance is CAPACITIVE (X < 0): Add series INDUCTANCE") + "\n");
    } else {
        print(translation.get("MATCH_X_GOOD", "  Reactance is near zero: Good reactance") + "\n");
    }
    
    // Resistance hint
    if (hint.secondary_hint == "r_too_low") {
        print(translation.get("MATCH_R_LOW", "  Resistance is LOW (R < 50 Ohm): Consider L-network with step-up") + "\n");
    } else if (hint.secondary_hint == "r_too_high") {
        print(translation.get("MATCH_R_HIGH", "  Resistance is HIGH (R > 50 Ohm): Consider L-network with step-down") + "\n");
    } else {
        print(translation.get("MATCH_R_GOOD", "  Resistance is near 50 Ohm: Good match") + "\n");
    }
    
    print("\n" + translation.get("MATCH_DISCLAIMER", "Note: These are rough guidelines.") + "\n");
    
    // Ask if user wants to scan again
    print("\n" + translation.get("SCAN_AGAIN", "Would you like to scan again with different settings?") + "\n");
    continueScanning = getYesNo("");
    }
}

// Remaining implementations will continue in next part...
// Cable length, cable fault, cable attenuation, filter check, comparison, markers, config
