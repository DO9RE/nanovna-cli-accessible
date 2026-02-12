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
#include <vector>
#include <memory>

class ConsoleUI {
public:
    ConsoleUI(AppConfig cfg, Logger* logger, MathLogger* mathLogger, SerialComm* serial);
    ~ConsoleUI();
    void run(NanoVNAProtocol* proto);

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
    
    // Snapshots for before/after comparison
    MeasurementSnapshot snapshotA;
    MeasurementSnapshot snapshotB;

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
    bool readNumericInput(const std::string& prompt, int& result);  // Helper for numeric input with ESC
    
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
    std::string getPromptWithDepth(const std::string& promptKey, int depth = 1) const;
    
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
    
    // Output wrapper for web interface integration
    void print(const std::string& text);
    
    // Helper to read line input from either keyboard or web interface
    bool readLine(std::string& result);
};
