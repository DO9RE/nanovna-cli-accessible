#pragma once
#include "config.h"
#include "logger.h"
#include "math_logger.h"
#include "protocol.h"
#include "measurement.h"
#include "audio.h"
#include "acoustic_analyzer.h"
#include "comm_serial.h"
#include "translation.h"
#include "comfort_functions.h"
#include "web_server.h"
#include "console_input.h"
#include "navigation_stack.h"
#include <vector>
#include <memory>

class ConsoleUI {
public:
    ConsoleUI(AppConfig cfg, Logger* logger, MathLogger* mathLogger, SerialComm* serial);
    ~ConsoleUI();
    void run(NanoVNAProtocol* proto);
    
    // Public output method for use outside the class (e.g., main.cpp)
    void output(const std::string& text);

private:
    AppConfig cfg;
    Logger* logger;
    MathLogger* mathLogger;
    SerialComm* serial;
    std::unique_ptr<IConsoleInput> consoleInput;

    std::vector<std::string> activeColumns;
    AudioEngine audio;
    TranslationManager translation;
    ComfortFunctions comfortFuncs;
    std::unique_ptr<WebServer> webServer;
    NavigationStack navStack;  // Central state stack for managing UI depth
    
    // Snapshots for before/after comparison
    MeasurementSnapshot snapshotA;
    MeasurementSnapshot snapshotB;
    
    // Current UI context for web interface (which actions are available)
    struct UIAction {
        std::string key;           // The key to press (e.g., "s", "1", "q")
        std::string label;         // Human-readable label (e.g., "Summary")
        bool needsEnter;           // Whether the action requires Enter confirmation
    };
    std::vector<UIAction> currentActions;
    std::string currentContext;    // Current context name for web interface
    std::string currentInputMode; // "menu" (default), "navigation" (arrow=playback), "text_edit" (arrow=cursor)

    void printOptionsLine();
    void showSummary(const std::vector<MeasurementPoint>& pts);
    void showTable(const std::vector<MeasurementPoint>& pts, size_t center = 0, size_t maxRows=20);
    void showTablePaginated(const std::vector<MeasurementPoint>& pts);  // New paginated table viewer
    void customizeMenu();
    void exportMenu(const std::vector<MeasurementPoint>& pts, const AcousticAnalyzer* analyzer = nullptr);
    void importMenu(std::vector<MeasurementPoint>& pts);  // New import function
    void deviceInfoMenu(NanoVNAProtocol* proto);  // New device info submenu
    void calibrationMenu(NanoVNAProtocol* proto);  // New calibration submenu
    void calibrateFlow(NanoVNAProtocol* proto);
    std::vector<MeasurementPoint> autostartMeasurement(NanoVNAProtocol* proto);
    std::vector<MeasurementPoint> performMeasurementWithTiming(NanoVNAProtocol* proto, uint64_t startHz, uint64_t endHz, uint64_t stepHz, bool suppressProgress = false);

    bool interactiveSelectPort(NanoVNAProtocol* proto);
    std::vector<MeasurementPoint> interactiveRangeAndScan(NanoVNAProtocol* proto);
    void showInfo(NanoVNAProtocol* proto);
    void showBattery(NanoVNAProtocol* proto);  // New battery display
    void debugShell(NanoVNAProtocol* proto);  // Debug shell for direct communication
    void loadCalibrationProfile(NanoVNAProtocol* proto);  // Load calibration from bank
    void performCalibrationWizard(NanoVNAProtocol* proto);  // Extended calibration wizard
    void saveSettings();
    
    // Acoustic analysis mode
    void runAcousticAnalysis(const std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto);
    
    // Audio configuration screen
    bool runAudioConfigurationScreen(AcousticAnalyzer* analyzer = nullptr);  // Returns true if engine changed
    bool runSmithConfigurationScreen(AcousticAnalyzer* analyzer = nullptr);  // Smith audio settings submenu
    bool runSurroundConfigurationScreen(SmithVisualizer* smith);  // Surround sound configuration submenu
    bool runSpatialAudioCalibrationWizard(AcousticAnalyzer* analyzer);  // Spatial audio calibration wizard (NEW)
    bool runDurationConfigurationScreen(AcousticAnalyzer* analyzer = nullptr);  // Duration submenu
    bool runFreezePauseConfigurationScreen(AcousticAnalyzer* analyzer = nullptr);  // Freeze pause submenu
    bool runLoopPauseConfigurationScreen(AcousticAnalyzer* analyzer = nullptr);  // Loop pause submenu
    bool runInvertedLoopGapConfigurationScreen(AcousticAnalyzer* analyzer = nullptr);  // Inverted loop gap submenu
    bool readNumericInput(const std::string& prompt, int& result, int depth = 0);  // Helper for numeric input with ESC (depth adds depth indicator)
    
    // Unified raw-mode input helper (Phase 4: Canonical mode elimination)
    // Replaces enableCanonicalMode/getline sequences
    struct RawInputResult {
        std::string value;
        bool cancelled;  // true if user pressed ESC
    };
    RawInputResult readRawLineInput(const std::string& prompt, const std::string& defaultValue = "", bool silentCancel = false);
    
    // Options menu
    void optionsMenu();  // New options submenu
    void languageSelectionMenu();  // New language selection
    void bandplanSelectionMenu();  // Band plan selection
    void braillePrinterSettingsMenu();  // Braille printer settings submenu
    
    // Web interface menu
    void webInterfaceMenu();  // Web interface control
    
    // Documentation and training menu
    void documentationMenu();  // Documentation, training, and feedback menu
    void openDocumentation(const std::string& docPath);  // Open HTML file in browser
    void feedbackToDeveloper();  // Send feedback email to developer
    
    // First-start wizard
    void runFirstStartWizard();  // First-time setup wizard
    
    // Go To function
    void goToMenu(std::vector<MeasurementPoint>& pts, size_t& currentPage, size_t rowsPerPage);  // For table view
    void goToMenuAcoustic(AcousticAnalyzer& analyzer, const std::vector<MeasurementPoint>& pts);  // For acoustic view
    
    // Helper functions for formatting
    std::string formatOhm() const;  // Returns "Ω" or "Ohm" based on settings
    std::string formatDegree() const;  // Returns "°" or "deg" based on settings
    
    // Comfort functions menu
    void comfortFunctionsMenu(std::vector<MeasurementPoint>& lastPts, NanoVNAProtocol* proto);
    
    // Individual comfort function implementations
    void bandSuitabilityCheck(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto);
    void resonanceFinder(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto);
    void swrBandwidthCalculator(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto);
    void feedpointImpedanceReport(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto);
    void matchingHints(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto);
    void cableLengthMeasurement(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto);
    void cableFaultDetection(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto);
    void cableAttenuationMeasurement(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto);
    void filterQuickCheck(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto);
    void beforeAfterComparison(const std::vector<MeasurementPoint>& pts);
    void autoMarkerPlacement(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto);
    void comfortConfiguration();
    
    // Helper for reading input (Task 1.18: Input validation helpers)
    bool getYesNo(const std::string& prompt);
    uint64_t getFrequencyInput(const std::string& prompt);
    double getDoubleInput(const std::string& prompt, double min_val, double max_val);
    char getMenuChoice(const std::string& prompt = "");  // Single character menu choice
    std::string getLineInput(const std::string& prompt = "");  // Line input with empty check
    
    // Task 1.17: Cable preset selection helper
    struct CableSelection {
        double velocity_factor;
        double loss_db_per_m;
        std::string name;
        bool selected;  // false if user cancelled
    };
    CableSelection selectCablePreset();
    
    // Helper for generating prompts with depth indication
    // Uses navStack if depth is USE_NAVIGATION_STACK, otherwise uses provided depth
    // USE_NAVIGATION_STACK (-1) is a sentinel value indicating automatic depth detection
    static const int USE_NAVIGATION_STACK = -1;
    std::string getPromptWithDepth(const std::string& promptKey, int depth = USE_NAVIGATION_STACK) const;
    
    // Get just the depth indicator (e.g., ">>>" for depth 3)
    std::string getDepthIndicator(int depth) const;
    
    // Helper to check if measurement data is available, and offer to measure if not
    bool ensureMeasurementData(std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto, bool needsS21 = false);
    
    // Helper to configure measurement settings (band or custom range)
    bool configureMeasurementSettings();
    
    // Helper to ensure port is selected, offering to navigate to port selection if not
    bool ensurePortSelected(NanoVNAProtocol* proto);
    
    // Helper function for Braille curve selection UI
    bool selectBrailleCurves(bool curveFlags[5]);
    
    // Helper function to offer repeat option after playing a sound in wizard
    // Returns true if user wants to repeat, false if user wants to continue
    bool offerRepeat();
    
    // Screen clearing function - clears the screen when entering a new context
    // Respects the debug flag (-d) - does not clear if debug is enabled
    // When preserve is true, the screen is not cleared (content stays visible)
    // This is the ONLY place in the code where screen clearing occurs
    void clearScreen(bool preserve = false);
    
    // Format a heading using ANSI colors instead of decorative characters
    // Uses bold cyan for visibility without generating screenreader noise
    std::string formatHeading(const std::string& title) const;
    
    // Format a subheading using ANSI colors
    std::string formatSubHeading(const std::string& title) const;
    
    // Post-process text for display: replaces decorative === and ═══ patterns
    // with ANSI color codes. Applied automatically in print() so that
    // translation strings containing these patterns are handled transparently.
    static std::string processTextForDisplay(const std::string& text);
    
    // Set current UI context and available actions for web interface
    void setUIContext(const std::string& context, const std::vector<UIAction>& actions, const std::string& inputMode = "menu");
    
    // Get current context as JSON for web interface
    std::string getContextJSON() const;
    
    // Output wrapper for web interface integration
    void print(const std::string& text);
    
    // Helper to read line input from either keyboard or web interface
    bool readLine(std::string& result);
};
