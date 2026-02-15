#include "ui.h"
#include "reactance_effects_config.h"
#include <sstream>
#include <iomanip>

/**
 * Reactance Effects Configuration Screen
 * 
 * This screen allows users to configure MIDI effects for the Reactance curve (index 3)
 * to enable auditory differentiation between capacitive (X < 0) and inductive (X > 0) reactance.
 */

bool ConsoleUI::runReactanceEffectsConfigurationScreen(AcousticAnalyzer* analyzer) {
    clearScreen();
    bool running = true;
    bool isConfiguringSmoothMode = false;  // false = Dotted, true = Smooth
    
    while (running) {
        clearScreen();
        print(formatHeading(translation.get("REACTANCE_EFFECTS_TITLE", "Reactance Effects Configuration")));
        
        // Display current mode
        const char* modeText = isConfiguringSmoothMode ? "Smooth" : "Dotted";
        print(translation.format("REACTANCE_EFFECTS_MODE", "Configuring mode: {0}\n", modeText));
        print("\n");
        
        // Get current configuration based on mode
        int capacitiveCC = isConfiguringSmoothMode ? cfg.reactance_smooth_capacitive_cc : cfg.reactance_dotted_capacitive_cc;
        int inductiveCC = isConfiguringSmoothMode ? cfg.reactance_smooth_inductive_cc : cfg.reactance_dotted_inductive_cc;
        bool deadzoneEnabled = isConfiguringSmoothMode ? cfg.reactance_smooth_deadzone_enabled : cfg.reactance_dotted_deadzone_enabled;
        double deadzoneSize = isConfiguringSmoothMode ? cfg.reactance_smooth_deadzone_size : cfg.reactance_dotted_deadzone_size;
        int mappingFunc = isConfiguringSmoothMode ? cfg.reactance_smooth_mapping_function : cfg.reactance_dotted_mapping_function;
        
        // Display current settings
        print(translation.get("REACTANCE_EFFECTS_CURRENT", "Current Settings:") + "\n");
        auto capParam = ReactanceEffects::Config::getCCParameterFromNumber(capacitiveCC);
        auto indParam = ReactanceEffects::Config::getCCParameterFromNumber(inductiveCC);
        print(translation.format("REACTANCE_EFFECTS_CAPACITIVE", "  Capacitive (X < 0): {0}", 
            ReactanceEffects::Config::getCCParameterName(capParam)) + "\n");
        print(translation.format("REACTANCE_EFFECTS_INDUCTIVE", "  Inductive (X > 0): {0}", 
            ReactanceEffects::Config::getCCParameterName(indParam)) + "\n");
        print(translation.format("REACTANCE_EFFECTS_DEADZONE", "  Deadzone: {0} (Size: {1:.1f} Ω)", 
            deadzoneEnabled ? "Enabled" : "Disabled", deadzoneSize) + "\n");
        
        std::string mappingName;
        switch (mappingFunc) {
            case 0: mappingName = "Linear"; break;
            case 1: mappingName = "Logarithmic"; break;
            case 2: mappingName = "Exponential"; break;
            case 3: mappingName = "Square Root"; break;
            default: mappingName = "Unknown"; break;
        }
        print(translation.format("REACTANCE_EFFECTS_MAPPING", "  Mapping Function: {0}", mappingName) + "\n");
        print("\n");
        
        // Display commands
        print(translation.get("REACTANCE_EFFECTS_COMMANDS", "Commands:") + "\n");
        print(translation.get("REACTANCE_EFFECTS_MODE_CMD", "  M - Toggle Mode (Dotted/Smooth)") + "\n");
        print(translation.get("REACTANCE_EFFECTS_CAPACITIVE_CMD", "  C - Configure Capacitive effect") + "\n");
        print(translation.get("REACTANCE_EFFECTS_INDUCTIVE_CMD", "  I - Configure Inductive effect") + "\n");
        print(translation.get("REACTANCE_EFFECTS_DEADZONE_CMD", "  D - Toggle Deadzone") + "\n");
        print(translation.get("REACTANCE_EFFECTS_DEADZONE_SIZE_CMD", "  Z - Configure deadZone size") + "\n");
        print(translation.get("REACTANCE_EFFECTS_MAPPING_CMD", "  F - Select mapping Function") + "\n");
        print(translation.get("REACTANCE_EFFECTS_TEST_CMD", "  T - Test effects (play demo tones)") + "\n");
        print(translation.get("REACTANCE_EFFECTS_RESET_CMD", "  R - Reset to defaults") + "\n");
        print(translation.get("HELP_COMMAND", "  H - Help") + "\n");
        print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
        print(getPromptWithDepth("REACTANCE_EFFECTS_PROMPT", 4) + " ");
        
        // Read input
        int ch = 0;
        bool hasInput = false;
        
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
        if (!hasInput) {
            ch = consoleInput->getch();
            hasInput = true;
        }
        
        if (ch == 27) {  // ESC - Back
            running = false;
        } else {
            char key = static_cast<char>(ch);
            if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
            
            switch (key) {
                case 'm':  // Toggle mode
                    print("M\n");
                    isConfiguringSmoothMode = !isConfiguringSmoothMode;
                    print(translation.format("REACTANCE_EFFECTS_MODE_SWITCHED", "[Switched to {0} mode]\n", 
                        isConfiguringSmoothMode ? "Smooth" : "Dotted"));
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    break;
                    
                case 'c':  // Configure capacitive effect
                    {
                        print("C\n\n");
                        print(formatHeading(translation.get("REACTANCE_EFFECTS_CAPACITIVE_TITLE", "Configure Capacitive Effect")));
                        print(translation.get("REACTANCE_EFFECTS_SELECT_CC", "Select MIDI CC parameter:\n"));
                        print("  0 - None (disable)\n");
                        print("  1 - Modulation (CC 1) - Vibrato\n");
                        print("  2 - Reverb (CC 91) - Spatial depth\n");
                        print("  3 - Chorus (CC 93) - Detuning effect\n");
                        print("  4 - Brightness (CC 74) - Filter cutoff\n");
                        print("  5 - Resonance (CC 71) - Filter resonance\n\n");
                        
                        int selection;
                        if (readNumericInput(translation.get("REACTANCE_EFFECTS_CC_PROMPT", "Enter selection (0-5), or press ESC to cancel:"), selection, 5)) {
                            int newCC = 0;
                            switch (selection) {
                                case 0: newCC = 0; break;
                                case 1: newCC = 1; break;
                                case 2: newCC = 91; break;
                                case 3: newCC = 93; break;
                                case 4: newCC = 74; break;
                                case 5: newCC = 71; break;
                                default:
                                    print("\n" + translation.get("REACTANCE_EFFECTS_INVALID_SELECTION", "[Invalid selection]") + "\n");
                                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                                    continue;
                            }
                            
                            if (isConfiguringSmoothMode) {
                                cfg.reactance_smooth_capacitive_cc = newCC;
                            } else {
                                cfg.reactance_dotted_capacitive_cc = newCC;
                            }
                            
                            auto param = ReactanceEffects::Config::getCCParameterFromNumber(newCC);
                            print("\n" + translation.format("REACTANCE_EFFECTS_CC_SET", "[Capacitive effect set to: {0}]", 
                                ReactanceEffects::Config::getCCParameterName(param)) + "\n");
                            saveSettings();
                            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                        }
                    }
                    break;
                    
                case 'i':  // Configure inductive effect
                    {
                        print("I\n\n");
                        print(formatHeading(translation.get("REACTANCE_EFFECTS_INDUCTIVE_TITLE", "Configure Inductive Effect")));
                        print(translation.get("REACTANCE_EFFECTS_SELECT_CC", "Select MIDI CC parameter:\n"));
                        print("  0 - None (disable)\n");
                        print("  1 - Modulation (CC 1) - Vibrato\n");
                        print("  2 - Reverb (CC 91) - Spatial depth\n");
                        print("  3 - Chorus (CC 93) - Detuning effect\n");
                        print("  4 - Brightness (CC 74) - Filter cutoff\n");
                        print("  5 - Resonance (CC 71) - Filter resonance\n\n");
                        
                        int selection;
                        if (readNumericInput(translation.get("REACTANCE_EFFECTS_CC_PROMPT", "Enter selection (0-5), or press ESC to cancel:"), selection, 5)) {
                            int newCC = 0;
                            switch (selection) {
                                case 0: newCC = 0; break;
                                case 1: newCC = 1; break;
                                case 2: newCC = 91; break;
                                case 3: newCC = 93; break;
                                case 4: newCC = 74; break;
                                case 5: newCC = 71; break;
                                default:
                                    print("\n" + translation.get("REACTANCE_EFFECTS_INVALID_SELECTION", "[Invalid selection]") + "\n");
                                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                                    continue;
                            }
                            
                            if (isConfiguringSmoothMode) {
                                cfg.reactance_smooth_inductive_cc = newCC;
                            } else {
                                cfg.reactance_dotted_inductive_cc = newCC;
                            }
                            
                            auto param = ReactanceEffects::Config::getCCParameterFromNumber(newCC);
                            print("\n" + translation.format("REACTANCE_EFFECTS_CC_SET", "[Inductive effect set to: {0}]", 
                                ReactanceEffects::Config::getCCParameterName(param)) + "\n");
                            saveSettings();
                            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                        }
                    }
                    break;
                    
                case 'd':  // Toggle deadzone
                    {
                        print("D\n");
                        if (isConfiguringSmoothMode) {
                            cfg.reactance_smooth_deadzone_enabled = !cfg.reactance_smooth_deadzone_enabled;
                            print(translation.format("REACTANCE_EFFECTS_DEADZONE_TOGGLED", "[Deadzone {0}]\n", 
                                cfg.reactance_smooth_deadzone_enabled ? "Enabled" : "Disabled"));
                        } else {
                            cfg.reactance_dotted_deadzone_enabled = !cfg.reactance_dotted_deadzone_enabled;
                            print(translation.format("REACTANCE_EFFECTS_DEADZONE_TOGGLED", "[Deadzone {0}]\n", 
                                cfg.reactance_dotted_deadzone_enabled ? "Enabled" : "Disabled"));
                        }
                        saveSettings();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    }
                    break;
                    
                case 'z':  // Configure deadzone size
                    {
                        print("Z\n\n");
                        print(formatHeading(translation.get("REACTANCE_EFFECTS_DEADZONE_SIZE_TITLE", "Configure Deadzone Size")));
                        print(translation.get("REACTANCE_EFFECTS_DEADZONE_SIZE_DESC", 
                            "Deadzone size determines the range around X=0 where effects are not applied.\n"
                            "This helps avoid oscillation when reactance is near zero.") + "\n\n");
                        print(translation.get("REACTANCE_EFFECTS_DEADZONE_SIZE_RANGE", "Valid range: 0 - 100 Ω\n\n"));
                        
                        int size;
                        if (readNumericInput(translation.get("REACTANCE_EFFECTS_DEADZONE_SIZE_PROMPT", "Enter deadzone size in Ohms (0-100), or press ESC to cancel:"), size, 5)) {
                            if (size >= 0 && size <= 100) {
                                if (isConfiguringSmoothMode) {
                                    cfg.reactance_smooth_deadzone_size = static_cast<double>(size);
                                } else {
                                    cfg.reactance_dotted_deadzone_size = static_cast<double>(size);
                                }
                                print("\n" + translation.format("REACTANCE_EFFECTS_DEADZONE_SIZE_SET", "[Deadzone size set to {0} Ω]", size) + "\n");
                                saveSettings();
                                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                            } else {
                                print("\n" + translation.get("REACTANCE_EFFECTS_DEADZONE_SIZE_ERROR", "[Error: Size must be between 0 and 100 Ω]") + "\n");
                                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                            }
                        }
                    }
                    break;
                    
                case 'f':  // Select mapping function
                    {
                        print("F\n\n");
                        print(formatHeading(translation.get("REACTANCE_EFFECTS_MAPPING_TITLE", "Select Mapping Function")));
                        print(translation.get("REACTANCE_EFFECTS_MAPPING_DESC", "Mapping functions:\n"));
                        print("  0 - Linear (Direct proportional)\n");
                        print("  1 - Logarithmic (Emphasize small values)\n");
                        print("  2 - Exponential (Emphasize large values)\n");
                        print("  3 - Square Root (Softer response)\n\n");
                        
                        int func;
                        if (readNumericInput(translation.get("REACTANCE_EFFECTS_MAPPING_PROMPT", "Enter function (0-3), or press ESC to cancel:"), func, 5)) {
                            if (func >= 0 && func <= 3) {
                                if (isConfiguringSmoothMode) {
                                    cfg.reactance_smooth_mapping_function = func;
                                } else {
                                    cfg.reactance_dotted_mapping_function = func;
                                }
                                
                                std::string funcName;
                                switch (func) {
                                    case 0: funcName = "Linear"; break;
                                    case 1: funcName = "Logarithmic"; break;
                                    case 2: funcName = "Exponential"; break;
                                    case 3: funcName = "Square Root"; break;
                                }
                                print("\n" + translation.format("REACTANCE_EFFECTS_MAPPING_SET", "[Mapping function set to: {0}]", funcName) + "\n");
                                saveSettings();
                                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                            } else {
                                print("\n" + translation.get("REACTANCE_EFFECTS_MAPPING_ERROR", "[Error: Function must be 0-3]") + "\n");
                                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                            }
                        }
                    }
                    break;
                    
                case 't':  // Test effects
                    print("T\n\n");
                    print(translation.get("REACTANCE_EFFECTS_TEST", "[Test effects not yet implemented - coming soon!]") + "\n");
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    break;
                    
                case 'r':  // Reset to defaults
                    {
                        print("R\n\n");
                        print(translation.get("REACTANCE_EFFECTS_RESET_CONFIRM", "Reset to default values? (Y/N): "));
                        
                        int confirm = 0;
                        bool hasConfirmInput = false;
                        
                        // Check for web interface input first
                        if (webServer && webServer->isRunning() && webServer->hasInput()) {
                            std::string webInput = webServer->readInput();
                            if (!webInput.empty()) {
                                confirm = static_cast<unsigned char>(webInput[0]);
                                hasConfirmInput = true;
                            }
                        }
                        
                        // Check for keyboard input if no web input
                        if (!hasConfirmInput) {
                            confirm = consoleInput->getch();
                            hasConfirmInput = true;
                        }
                        
                        if (confirm == 'y' || confirm == 'Y') {
                            print("Y\n");
                            if (isConfiguringSmoothMode) {
                                cfg.reactance_smooth_capacitive_cc = 91;
                                cfg.reactance_smooth_inductive_cc = 93;
                                cfg.reactance_smooth_deadzone_enabled = true;
                                cfg.reactance_smooth_deadzone_size = 5.0;
                                cfg.reactance_smooth_mapping_function = 0;
                            } else {
                                cfg.reactance_dotted_capacitive_cc = 91;
                                cfg.reactance_dotted_inductive_cc = 93;
                                cfg.reactance_dotted_deadzone_enabled = true;
                                cfg.reactance_dotted_deadzone_size = 5.0;
                                cfg.reactance_dotted_mapping_function = 0;
                            }
                            print(translation.get("REACTANCE_EFFECTS_RESET_DONE", "[Reset to defaults]") + "\n");
                            saveSettings();
                            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                        } else {
                            print("N\n" + translation.get("REACTANCE_EFFECTS_RESET_CANCELLED", "[Cancelled]") + "\n");
                            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                        }
                    }
                    break;
                    
                case 'h':  // Help
                    print("H\n\n");
                    print(formatHeading(translation.get("REACTANCE_EFFECTS_HELP_TITLE", "Reactance Effects Help")));
                    print(translation.get("REACTANCE_EFFECTS_HELP_TEXT", 
                        "Reactance effects help you distinguish capacitive from inductive reactance by ear.\n\n"
                        "- Capacitive (X < 0): Use one effect (e.g., Reverb)\n"
                        "- Inductive (X > 0): Use another effect (e.g., Chorus)\n\n"
                        "Deadzone: Creates a neutral zone around X=0 where no effects are applied.\n\n"
                        "Mapping functions control how reactance values map to effect intensity:\n"
                        "- Linear: Direct proportional (good for precision)\n"
                        "- Logarithmic: Better for hearing small reactance changes\n"
                        "- Exponential: Emphasizes large reactance values\n"
                        "- Square Root: Softer, more gradual response\n\n"
                        "These effects ONLY apply to the Reactance curve (curve 3).\n"));
                    print(translation.get("PRESS_ANY_KEY", "\nPress any key to continue..."));
                    consoleInput->getch();
                    break;
            }
        }
    }
    
    return false;  // No changes that require re-initialization
}
