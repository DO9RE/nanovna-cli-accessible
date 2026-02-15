#include "ui.h"
#include "export.h"
#include "import.h"
#include "settings.h"
#include "help.h"
#include "synthesizer_engine.h"
#include "midi_engine.h"
#include "band_definitions.h"
#include "frequency_utils.h"
#include "braille_printer.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <memory>
#include <limits>
#include <filesystem>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h> // For GetAsyncKeyState, ShellExecuteA
#include <shellapi.h> // For ShellExecuteA
#endif

ConsoleUI::ConsoleUI(AppConfig cfg_, Logger* logger_, MathLogger* mathLogger_, SerialComm* serial_) 
    : cfg(cfg_), logger(logger_), mathLogger(mathLogger_), serial(serial_), webServer(nullptr) {
    activeColumns = cfg.table_columns;  // Load from config
    audio.open();
    
    // Initialize console input
    consoleInput.reset(createConsoleInput());
    if (consoleInput && !consoleInput->initialize()) {
        std::cerr << "ERROR: Failed to initialize console input. Keyboard interaction may not work properly.\n";
        if (logger) logger->log("UI", "ERROR: Failed to initialize console input");
    } else if (logger) {
        logger->log("UI", "Console input initialized successfully");
    }
    
    // Initialize comfort functions with math logger
    comfortFuncs.setMathLogger(mathLogger_);
    
    // Initialize MIDI controller manager
    midiControllerMgr = std::make_unique<MidiControllerManager>();
    midiControllerMgr->setLogger(logger);
    if (logger) logger->log("UI", "MIDI controller manager initialized");
    
    // Load language file
    std::string err;
    if (!translation.loadLanguage(cfg.language, err)) {
        // If loading fails, fall back to English
        if (logger) logger->log("UI", "Failed to load language " + cfg.language + ": " + err);
        if (cfg.language != "eng") {
            // Try English as fallback
            if (!translation.loadLanguage("eng", err)) {
                if (logger) logger->log("UI", "Failed to load fallback language eng: " + err);
            }
        }
    } else {
        if (logger) logger->log("UI", "Loaded language: " + cfg.language);
    }
}

ConsoleUI::~ConsoleUI() {
    if (consoleInput) {
        consoleInput->cleanup();
    }
}

void ConsoleUI::printOptionsLine() {
    // Do NOT clearScreen here. Each sub-menu/context clears the screen
    // when it ENTERS. This preserves important exit messages (e.g. calibration
    // profile loaded, measurement summary) that are printed before returning.
    print(formatHeading(translation.get("MAIN_MENU_TITLE", "Main Menu")));
    std::ostringstream menu;
    menu << translation.get("MENU_SUMMARY", "(S)ummary") << "  ";
    menu << translation.get("MENU_TABLE", "(T)able") << "  ";
    menu << translation.get("MENU_ACOUSTIC", "(A)coustic") << "  ";
    menu << translation.get("MENU_EXPORT", "(E)xport") << "  ";
    menu << translation.get("MENU_LOAD", "(L)oad") << "  ";
    menu << translation.get("MENU_CALIBRATE", "(K)alibrate") << "\n";
    menu << translation.get("MENU_PORT", "(P)ort") << "  ";
    menu << translation.get("MENU_RANGE", "(R)ange") << "  ";
    menu << translation.get("MENU_DEVICE_INFO", "(D)evice Info") << "  ";
    menu << translation.get("MENU_MANUAL", "(M)anual") << "  ";
    menu << translation.get("MENU_COMFORT", "(U) Comfort Functions") << "\n";
    
    // Show continuous sweep status
    std::string sweepStatus = cfg.continuous_sweep_enabled ? 
        translation.get("MENU_SWEEP_ON", "(W) Sweep: ON") : 
        translation.get("MENU_SWEEP_OFF", "(W) Sweep: OFF");
    menu << sweepStatus << "\n";
    
    menu << translation.get("MENU_OPTIONS", "(O)ptions") << "  ";
    menu << translation.get("MENU_WEB_INTERFACE", "(I) Web Interface") << "  ";
    menu << translation.get("MENU_DOCS", "(?) Manuals and Training") << "\n";
    menu << translation.get("MENU_HELP", "(H)elp") << "  ";
    menu << translation.get("MENU_QUIT", "(Q)uit") << "\n";
    
    print(menu.str());
    
    // Set UI context for web interface
    setUIContext("main_menu", {
        {"s", translation.get("MENU_SUMMARY", "(S)ummary"), false},
        {"t", translation.get("MENU_TABLE", "(T)able"), false},
        {"a", translation.get("MENU_ACOUSTIC", "(A)coustic"), false},
        {"e", translation.get("MENU_EXPORT", "(E)xport"), false},
        {"l", translation.get("MENU_LOAD", "(L)oad"), false},
        {"k", translation.get("MENU_CALIBRATE", "(K)alibrate"), false},
        {"p", translation.get("MENU_PORT", "(P)ort"), false},
        {"r", translation.get("MENU_RANGE", "(R)ange"), false},
        {"d", translation.get("MENU_DEVICE_INFO", "(D)evice Info"), false},
        {"m", translation.get("MENU_MANUAL", "(M)anual"), false},
        {"u", translation.get("MENU_COMFORT", "(U) Comfort Functions"), false},
        {"w", sweepStatus, false},
        {"o", translation.get("MENU_OPTIONS", "(O)ptions"), false},
        {"i", translation.get("MENU_WEB_INTERFACE", "(I) Web Interface"), false},
        {"?", translation.get("MENU_DOCS", "(?) Manuals and Training"), false},
        {"h", translation.get("MENU_HELP", "(H)elp"), false},
        {"q", translation.get("MENU_QUIT", "(Q)uit"), false}
    });
}

void ConsoleUI::showSummary(const std::vector<MeasurementPoint>& pts) {
    clearScreen();
    if (pts.empty()) { 
        print(translation.get("ERROR_NO_DATA", "No data") + "\n");
        return; 
    }
    // At this point, pts is guaranteed to have at least one element
    double minSWR = 1e9; size_t idxMin = 0;
    double maxSWR = 0.0; size_t idxMax = 0;
    double avgSWR = 0.0;
    for (size_t i=0;i<pts.size();++i) {
        auto v = pts[i].swr;
        if (v < minSWR) { minSWR = v; idxMin = i; }
        if (v > maxSWR) { maxSWR = v; idxMax = i; }
        avgSWR += v;
    }
    avgSWR /= pts.size();
    
    if (mathLogger && mathLogger->isEnabled()) {
        mathLogger->logSeparator("USER OUTPUT - SUMMARY");
        std::ostringstream summary;
        summary << "Points: " << pts.size() << "\n";
        summary << "Frequency range: " << pts[0].freq << " Hz to " << pts[pts.size()-1].freq << " Hz\n";
        summary << "Min SWR: " << minSWR << " at " << pts[idxMin].freq << " Hz\n";
        summary << "Max SWR: " << maxSWR << " at " << pts[idxMax].freq << " Hz\n";
        summary << "Avg SWR: " << avgSWR;
        mathLogger->logUserOutput("TEXT_SUMMARY", summary.str());
    }
    
    std::ostringstream output;
    output << formatHeading(translation.get("SUMMARY_TITLE", "Measurement Summary"));
    output << translation.format("SUMMARY_POINTS", "Points: {0}", pts.size()) << "\n";
    output << translation.format("SUMMARY_FREQ_RANGE", "Frequency range: {0} Hz to {1} Hz", pts[0].freq, pts[pts.size()-1].freq) << "\n";
    output << translation.format("SUMMARY_MIN_SWR", "Min SWR: {0} at {1} Hz", minSWR, pts[idxMin].freq) << "\n";
    output << translation.format("SUMMARY_MAX_SWR", "Max SWR: {0} at {1} Hz", maxSWR, pts[idxMax].freq) << "\n";
    output << translation.format("SUMMARY_AVG_SWR", "Avg SWR: {0}", avgSWR) << "\n\n";
    
    print(output.str());
}

void ConsoleUI::showTable(const std::vector<MeasurementPoint>& pts, size_t center, size_t maxRows) {
    if (pts.empty()) { 
        print(translation.get("ERROR_NO_DATA", "No data") + "\n");
        return; 
    }
    size_t n = pts.size();
    size_t start = 0;
    if (center > n) center = n/2;
    if (n > maxRows) {
        if (center < maxRows/2) start = 0;
        else if (center > n - maxRows/2) start = n - maxRows;
        else start = center - maxRows/2;
    }
    
    std::ostringstream output;
    output << "Index\tFreq(Hz)\tSWR\tRL(dB)\tR\tX\t|Z|\tPhase\n";
    for (size_t i=start; i < std::min(n, start+maxRows); ++i) {
        auto &p = pts[i];
        output << i << "\t" << p.freq << "\t" << std::fixed << std::setprecision(3)
               << p.swr << "\t" << p.rl << "\t" << p.R << "\t" << p.X << "\t" 
               << p.impedance_mag << "\t" << p.phase_deg << "\n";
    }
    print(output.str());
}

void ConsoleUI::showTablePaginated(const std::vector<MeasurementPoint>& pts) {
    if (pts.empty()) { 
        clearScreen();
        print(translation.get("ERROR_NO_DATA", "No data") + "\n");
        return; 
    }
    
    const size_t rowsPerPage = 20;
    size_t currentPage = 0;
    size_t totalPages = (pts.size() + rowsPerPage - 1) / rowsPerPage;
    
    while (true) {
        clearScreen();  // Clear screen each time page is displayed
        print(formatHeading(translation.get("TABLE_TITLE", "Table View (Paginated)")));
        print(translation.get("TABLE_NAVIGATION", "Navigation: SPACE/N=Next, P=Previous, C=Customize, G=Go To, ESC=Back, H=Help") + "\n\n");
        
        // Display current page
        size_t startIdx = currentPage * rowsPerPage;
        size_t endIdx = std::min(startIdx + rowsPerPage, pts.size());
        
        std::ostringstream pageOutput;
        pageOutput << formatSubHeading(translation.format("TABLE_PAGE", "Page {0} of {1} (Points {2} to {3})", currentPage + 1, totalPages, startIdx, endIdx - 1));
        
        // Print header based on active columns
        pageOutput << translation.get("TABLE_INDEX", "Index") << "\t";
        for (const auto& col : activeColumns) {
            if (col == "FREQ") pageOutput << translation.get("TABLE_FREQ", "Freq(Hz)") << "\t\t";
            else if (col == "SWR") pageOutput << translation.get("TABLE_SWR", "SWR") << "\t";
            else if (col == "RL") pageOutput << translation.get("TABLE_RL", "RL(dB)") << "\t";
            else if (col == "R") pageOutput << translation.get("TABLE_R", "R") << "\t";
            else if (col == "X") pageOutput << translation.get("TABLE_X", "X") << "\t";
            else if (col == "Z") pageOutput << translation.get("TABLE_Z", "|Z|") << "\t";
            else if (col == "PHASE") pageOutput << translation.get("TABLE_PHASE", "Phase") << "\t";
        }
        pageOutput << "\n" << std::string(70, '-') << "\n";
        
        // Print rows based on active columns
        for (size_t i = startIdx; i < endIdx; ++i) {
            auto &p = pts[i];
            pageOutput << i << "\t";
            for (const auto& col : activeColumns) {
                pageOutput << std::fixed << std::setprecision(3);
                if (col == "FREQ") pageOutput << p.freq << "\t";
                else if (col == "SWR") pageOutput << p.swr << "\t";
                else if (col == "RL") pageOutput << p.rl << "\t";
                else if (col == "R") pageOutput << p.R << "\t";
                else if (col == "X") pageOutput << p.X << "\t";
                else if (col == "Z") pageOutput << p.impedance_mag << "\t";
                else if (col == "PHASE") pageOutput << p.phase_deg << "\t";
            }
            pageOutput << "\n";
        }
        
        pageOutput << "\n" << translation.get("TABLE_PROMPT", "[SPACE/N: Next | P: Previous | C: Customize | G: Go To | ESC: Back | H: Help] >") << " ";
        print(pageOutput.str());
        
        setUIContext("table_view", {
            {" ", translation.get("TABLE_NEXT", "Next Page"), false},
            {"n", translation.get("TABLE_NEXT", "Next Page"), false},
            {"p", translation.get("TABLE_PREV", "Previous Page"), false},
            {"c", translation.get("TABLE_CUSTOMIZE", "Customize Columns"), false},
            {"g", translation.get("TABLE_GOTO", "Go To"), false},
            {"h", translation.get("MENU_HELP", "(H)elp"), false}
        });
        
        char key;
        // Use console input abstraction for cross-platform support
        int ch = consoleInput->getch();
        key = static_cast<char>(ch);
        // Convert uppercase to lowercase for letter keys
        if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
        // Echo the key (but not ESC)
        if (ch != 27) {
            print(std::string(1, key) + "\n");
        } else {
            print(translation.get("KEY_ESC") + "\n");
        }
        
        if (key == ' ' || key == 'n') {
            // Next page
            if (currentPage < totalPages - 1) {
                currentPage++;
            } else {
                print(translation.get("TABLE_ALREADY_LAST", "Already at last page.") + "\n");
            }
        } else if (key == 'p') {
            // Previous page
            if (currentPage > 0) {
                currentPage--;
            } else {
                print(translation.get("TABLE_ALREADY_FIRST", "Already at first page.") + "\n");
            }
        } else if (key == 'c') {
            // Customize columns
            customizeMenu();
            // After customization, redisplay the current page
        } else if (key == 'g') {
            // Go To menu
            std::vector<MeasurementPoint> ptsMutable = pts;  // Create mutable copy for interface
            goToMenu(ptsMutable, currentPage, rowsPerPage);
        } else if (key == 27) {  // ESC key
            // Back to main menu
            break;
        } else if (key == 'h') {
            print(HelpModule::getTableViewHelp(translation));
        } else if (key == '\r' || key == '\n') {
            // Enter key - refresh display (intentional redisplay per interaktionsmodell.md)
            // Note: While content doesn't change, this provides defined behavior for Enter
            // Loop continues naturally, causing page to redisplay
        } else {
            print(translation.get("ERROR_UNKNOWN_COMMAND", "Unknown command.") + "\n");
        }
    }
    clearScreen();
    
    if (logger) logger->log("UI", "Exited paginated table view");
}

void ConsoleUI::customizeMenu() {
    while (true) {
        clearScreen();  // Clear screen each time menu is displayed
        print(formatHeading(translation.get("CUSTOMIZE_TITLE", "Customize Table Columns")));
        std::vector<std::string> cols = {"FREQ","SWR","RL","R","X","Z","PHASE"};
        for (size_t i=0;i<cols.size();++i) {
            bool active = (std::find(activeColumns.begin(), activeColumns.end(), cols[i]) != activeColumns.end());
            print(std::to_string(i+1) + ") [" + (active ? "X" : " ") + "] " + cols[i] + "\n");
        }
        print("\n" + translation.get("CUSTOMIZE_HELP", "(H)elp") + "  ");
        print(translation.get("CUSTOMIZE_PROMPT", "Enter number to toggle or press Enter to return: >") + " ");
        std::string s;
        if (!readLine(s)) return;
        if (s.empty()) return;
        
        // Check for help
        if (s.length() == 1 && (s[0] == 'h' || s[0] == 'H')) {
            print(HelpModule::getCustomizeMenuHelp(translation));
            continue;
        }
        
        int idx = atoi(s.c_str());
        if (idx <= 0 || (size_t)idx > cols.size()) {
            print(translation.get("CUSTOMIZE_INVALID", "Invalid") + "\n");
            continue;
        }
        std::string sel = cols[idx-1];
        auto it = std::find(activeColumns.begin(), activeColumns.end(), sel);
        if (it != activeColumns.end()) activeColumns.erase(it);
        else activeColumns.push_back(sel);
        
        // Update config and save
        cfg.table_columns = activeColumns;
        print(translation.get("CUSTOMIZE_UPDATED", "Updated columns.") + "\n");
        if (logger) logger->log("UI", "User changed columns");
        saveSettings();
    }
}

void ConsoleUI::exportMenu(const std::vector<MeasurementPoint>& pts, const AcousticAnalyzer* analyzer) {
    clearScreen();
    if (pts.empty()) {
        print(translation.get("ERROR_NO_DATA", "No data to export.") + "\n");
        return;
    }
    
    // Determine what to export
    std::vector<MeasurementPoint> toExport;
    uint64_t startFreq = cfg.start_freq;
    uint64_t endFreq = cfg.end_freq;
    uint64_t step = cfg.step;
    
    // Determine what to export based on loop and loop zoom settings
    // This matches the acoustic analyzer behavior:
    // - Loop OFF: Export all data
    // - Loop ON + Loop Zoom OFF: Export all data (loop plays within full context)
    // - Loop ON + Loop Zoom ON: Export only loop range (zoom focuses on loop)
    bool shouldExportOnlyLoop = false;
    if (analyzer && analyzer->isLoopEnabled()) {
        // Only export loop range if loop zoom is also enabled
        shouldExportOnlyLoop = analyzer->isLoopZoomEnabled();
    }
    
    if (shouldExportOnlyLoop) {
        size_t loopLeft = analyzer->getLoopLeft();
        size_t loopRight = analyzer->getLoopRight();
        
        if (loopLeft < pts.size() && loopRight < pts.size() && loopLeft <= loopRight) {
            for (size_t i = loopLeft; i <= loopRight; ++i) {
                toExport.push_back(pts[i]);
            }
            if (!toExport.empty()) {
                startFreq = toExport.front().freq;
                endFreq = toExport.back().freq;
                if (toExport.size() > 1) {
                    step = (endFreq - startFreq) / (toExport.size() - 1);
                }
            }
            print(translation.format("EXPORT_LOOP_RANGE", "Exporting loop range with zoom (markers {0} to {1})", loopLeft, loopRight) + "\n");
        } else {
            toExport = pts;
            print(translation.get("EXPORT_INVALID_LOOP", "Invalid loop range, exporting all data") + "\n");
        }
    } else {
        toExport = pts;
        if (analyzer && analyzer->isLoopEnabled() && !analyzer->isLoopZoomEnabled()) {
            print(translation.get("EXPORT_ALL_DATA", "Exporting all measurement data") + " ");
            print(translation.format("EXPORT_LOOP_NO_ZOOM", "(Loop active at markers {0} to {1}, but zoom off)", 
                                   analyzer->getLoopLeft(), analyzer->getLoopRight()) + "\n");
        } else {
            print(translation.get("EXPORT_ALL_DATA", "Exporting all measurement data") + "\n");
        }
    }
    
    // Helper lambda: Simplified LTTB algorithm for point selection
    // Preserves curve shape better than simple decimation
    auto selectPointsLTTB = [](const std::vector<MeasurementPoint>& points, size_t maxPoints, 
                               const bool curveFlags[5]) -> std::vector<size_t> {
        std::vector<size_t> selected;
        
        if (points.size() <= maxPoints || maxPoints < 3) {
            // Return all indices
            for (size_t i = 0; i < points.size(); i++) {
                selected.push_back(i);
            }
            return selected;
        }
        
        // Always include first point
        selected.push_back(0);
        
        // Calculate bucket size
        double bucketSize = static_cast<double>(points.size() - 2) / static_cast<double>(maxPoints - 2);
        
        // For each bucket
        for (size_t bucket = 0; bucket < maxPoints - 2; bucket++) {
            size_t bucketStart = 1 + static_cast<size_t>(bucket * bucketSize);
            size_t bucketEnd = 1 + static_cast<size_t>((bucket + 1) * bucketSize);
            if (bucketEnd > points.size() - 1) bucketEnd = points.size() - 1;
            
            // Calculate average of next bucket for triangle area calculation
            size_t nextBucketStart = 1 + static_cast<size_t>((bucket + 1) * bucketSize);
            size_t nextBucketEnd = 1 + static_cast<size_t>((bucket + 2) * bucketSize);
            if (nextBucketEnd > points.size()) nextBucketEnd = points.size();
            
            double avgNextX = 0.0, avgNextY = 0.0;
            int count = 0;
            for (size_t i = nextBucketStart; i < nextBucketEnd && i < points.size(); i++) {
                avgNextX += i;
                // Average across selected curves (use SWR as default if none selected)
                double val = 0.0;
                int enabledCount = 0;
                if (curveFlags[0]) { val += points[i].swr; enabledCount++; }
                if (curveFlags[1]) { val += points[i].rl; enabledCount++; }
                if (curveFlags[2]) { val += points[i].impedance_mag; enabledCount++; }
                if (curveFlags[3]) { val += points[i].X; enabledCount++; }
                if (curveFlags[4]) { val += points[i].phase_deg; enabledCount++; }
                avgNextY += (enabledCount > 0) ? val / enabledCount : points[i].swr;
                count++;
            }
            if (count > 0) {
                avgNextX /= count;
                avgNextY /= count;
            }
            
            // Find point in current bucket with largest triangle area
            size_t bestIdx = bucketStart;
            double maxArea = -1.0;
            
            size_t prevIdx = selected.back();
            double prevX = prevIdx;
            double prevY = 0.0;
            int enabledCount = 0;
            if (curveFlags[0]) { prevY += points[prevIdx].swr; enabledCount++; }
            if (curveFlags[1]) { prevY += points[prevIdx].rl; enabledCount++; }
            if (curveFlags[2]) { prevY += points[prevIdx].impedance_mag; enabledCount++; }
            if (curveFlags[3]) { prevY += points[prevIdx].X; enabledCount++; }
            if (curveFlags[4]) { prevY += points[prevIdx].phase_deg; enabledCount++; }
            prevY = (enabledCount > 0) ? prevY / enabledCount : points[prevIdx].swr;
            
            for (size_t i = bucketStart; i <= bucketEnd && i < points.size(); i++) {
                double currX = i;
                double currY = 0.0;
                enabledCount = 0;
                if (curveFlags[0]) { currY += points[i].swr; enabledCount++; }
                if (curveFlags[1]) { currY += points[i].rl; enabledCount++; }
                if (curveFlags[2]) { currY += points[i].impedance_mag; enabledCount++; }
                if (curveFlags[3]) { currY += points[i].X; enabledCount++; }
                if (curveFlags[4]) { currY += points[i].phase_deg; enabledCount++; }
                currY = (enabledCount > 0) ? currY / enabledCount : points[i].swr;
                
                // Triangle area formula
                double area = std::abs(
                    prevX * (currY - avgNextY) +
                    currX * (avgNextY - prevY) +
                    avgNextX * (prevY - currY)
                ) * 0.5;
                
                if (area > maxArea) {
                    maxArea = area;
                    bestIdx = i;
                }
            }
            
            selected.push_back(bestIdx);
        }
        
        // Always include last point
        selected.push_back(points.size() - 1);
        
        return selected;
    };
    
    // For braille exports: always use LTTB point selection (dotted mode approach)
    // This provides better tactile representation regardless of audio playback mode
    // Store original data for CSV/TXT, create filtered version for braille
    std::vector<MeasurementPoint> toExportBraille = toExport;
    int skipFactor = 1;
    if (analyzer) {
        // Always use dotted mode skip factor for braille export consistency
        // This ensures page count doesn't change when switching playback modes
        skipFactor = analyzer->getDottedModeSkipFactor();
        if (skipFactor > 1) {
            // Calculate target number of points
            size_t targetPoints = (toExport.size() + skipFactor - 1) / skipFactor;
            
            // Always use LTTB algorithm for braille (dotted mode approach)
            // This preserves curve features better than simple skip
            // Get curve flags from first braille export (will be asked later)
            // For now, assume all curves for point selection
            bool tempCurveFlags[5] = {true, true, true, true, true};
            
            std::vector<size_t> selectedIndices = selectPointsLTTB(toExport, targetPoints, tempCurveFlags);
            
            std::vector<MeasurementPoint> filtered;
            for (size_t idx : selectedIndices) {
                if (idx < toExport.size()) {
                    filtered.push_back(toExport[idx]);
                }
            }
            toExportBraille = filtered;
            
            if (logger) {
                std::ostringstream oss;
                oss << "Braille export: Applied LTTB algorithm (dotted mode approach) with skip factor " << skipFactor 
                    << " (filtered from " << toExport.size() 
                    << " to " << toExportBraille.size() << " points, target: " << targetPoints << ")";
                logger->log("EXPORT", oss.str());
            }
        }
    }
    
    print(translation.get("EXPORT_FORMAT", "Export format: (1) CSV, (2) TXT, (3) Braille File, (4) Print to Braille Printer, (other) cancel") + "\n> ");
    std::string a;
    if (!readLine(a)) return;
    
    if (a == "1") {
        std::string generatedFilename;
        std::string err;
        if (ExportModule::exportCSV(toExport, startFreq, endFreq, step, generatedFilename, err)) {
            print(translation.format("EXPORT_SUCCESS", "Exported to: {0}", generatedFilename) + "\n");
            if (logger) logger->log("EXPORT", generatedFilename + " created");
        } else {
            print(translation.format("EXPORT_FAILED", "Export failed: {0}", err) + "\n");
            if (logger) logger->log("EXPORT", "CSV export failed: " + err);
        }
    } else if (a == "2") {
        std::string generatedFilename;
        std::string err;
        if (ExportModule::exportTXT(toExport, startFreq, endFreq, step, generatedFilename, err)) {
            print(translation.format("EXPORT_SUCCESS", "Exported to: {0}", generatedFilename) + "\n");
            if (logger) logger->log("EXPORT", generatedFilename + " created");
        } else {
            print(translation.format("EXPORT_FAILED", "Export failed: {0}", err) + "\n");
            if (logger) logger->log("EXPORT", "TXT export failed: " + err);
        }
    } else if (a == "3") {
        // Braille export with curve selection
        bool curveFlags[5];
        if (selectBrailleCurves(curveFlags)) {
            // Perform Braille export
            std::string generatedFilename;
            std::string err;
            
            print(formatHeading(translation.get("BRAILLE_EXPORT_TITLE", "Export to Braille")));
            print(translation.get("BRAILLE_SETTINGS_INFO") + "\n");
            print(translation.get("BRAILLE_PROTOCOL_LABEL") + " " + (cfg.braille_protocol == AppConfig::BrailleProtocol::INDEX_V5 ? translation.get("BRAILLE_PROTOCOL_INDEX_V5") : translation.get("BRAILLE_PROTOCOL_INDEX_V4")) + "\n");
            print(translation.get("BRAILLE_PAPER_LABEL") + " ");
            switch (cfg.braille_paper_size) {
                case AppConfig::BraillePaperSize::A4: print(translation.get("BRAILLE_PAPER_A4")); break;
                case AppConfig::BraillePaperSize::LETTER: print(translation.get("BRAILLE_PAPER_LETTER")); break;
                case AppConfig::BraillePaperSize::A3: print(translation.get("BRAILLE_PAPER_A3")); break;
                case AppConfig::BraillePaperSize::LEGAL: print(translation.get("BRAILLE_PAPER_LEGAL")); break;
                case AppConfig::BraillePaperSize::BLISTA_260x305: print(translation.get("BRAILLE_PAPER_BLISTA_260x305")); break;
                case AppConfig::BraillePaperSize::BLISTA_270x340: print(translation.get("BRAILLE_PAPER_BLISTA_270x340")); break;
                case AppConfig::BraillePaperSize::BLISTA_297x304: print(translation.get("BRAILLE_PAPER_BLISTA_297x304")); break;
            }
            print(" (" + (cfg.braille_orientation == AppConfig::BrailleOrientation::PORTRAIT ? translation.get("BRAILLE_ORIENTATION_PORTRAIT") : translation.get("BRAILLE_ORIENTATION_LANDSCAPE")) + ")\n");
            
            // Show skip factor info if applicable
            if (skipFactor > 1) {
                print(translation.format("BRAILLE_SKIP_FACTOR_INFO", "  Audio playback skip factor: {0} (using {1} of {2} points)", 
                                        std::to_string(skipFactor), std::to_string(toExportBraille.size()), std::to_string(toExport.size())) + "\n");
            }
            
            // Calculate and show page count using filtered points
            BraillePrinter braillePrinter(logger);
            int pageCount = braillePrinter.calculatePageCount(toExportBraille, curveFlags, cfg);
            print(translation.format("BRAILLE_ESTIMATED_PAGES", "  Estimated pages: {0}", std::to_string(pageCount)) + "\n\n");
            
            print(translation.get("BRAILLE_EXPORT_PROCEED") + " ");
            std::string confirm;
            if (!readLine(confirm)) return;
            
            if (confirm == "s" || confirm == "S") {
                braillePrinterSettingsMenu();
                print(translation.get("BRAILLE_EXPORT_CANCELED_SETTINGS") + "\n");
            } else if (confirm == "y" || confirm == "Y") {
                if (ExportModule::exportBraille(toExportBraille, startFreq, endFreq, step, curveFlags, 
                                               cfg, generatedFilename, err)) {
                    print(translation.format("EXPORT_SUCCESS", "Exported to: {0}", generatedFilename) + "\n");
                    if (logger) logger->log("EXPORT", generatedFilename + " created (Braille)");
                } else {
                    print(translation.format("EXPORT_FAILED", "Export failed: {0}", err) + "\n");
                    if (logger) logger->log("EXPORT", "Braille export failed: " + err);
                }
            } else {
                print(translation.get("BRAILLE_EXPORT_CANCELED") + "\n");
            }
        }
    } else if (a == "4") {
        // Direct print to Braille printer
        bool curveFlags[5];
        if (selectBrailleCurves(curveFlags)) {
            BraillePrinter braillePrinter(logger);
            
            // Show skip factor info if applicable
            if (skipFactor > 1) {
                print(translation.format("BRAILLE_SKIP_FACTOR_INFO", "\nAudio playback skip factor: {0} (using {1} of {2} points)", 
                                        std::to_string(skipFactor), std::to_string(toExportBraille.size()), std::to_string(toExport.size())) + "\n");
            }
            
            // Calculate and show page count using filtered points
            int pageCount = braillePrinter.calculatePageCount(toExportBraille, curveFlags, cfg);
            
            print(formatHeading(translation.get("BRAILLE_PRINT_TITLE", "Print to Braille Printer")));
            print(translation.get("BRAILLE_SETTINGS_INFO") + "\n");
            print(translation.get("BRAILLE_PROTOCOL_LABEL") + " " + (cfg.braille_protocol == AppConfig::BrailleProtocol::INDEX_V5 ? translation.get("BRAILLE_PROTOCOL_INDEX_V5") : translation.get("BRAILLE_PROTOCOL_INDEX_V4")) + "\n");
            print(translation.get("BRAILLE_PAPER_LABEL") + " ");
            switch (cfg.braille_paper_size) {
                case AppConfig::BraillePaperSize::A4: print(translation.get("BRAILLE_PAPER_A4")); break;
                case AppConfig::BraillePaperSize::LETTER: print(translation.get("BRAILLE_PAPER_LETTER")); break;
                case AppConfig::BraillePaperSize::A3: print(translation.get("BRAILLE_PAPER_A3")); break;
                case AppConfig::BraillePaperSize::LEGAL: print(translation.get("BRAILLE_PAPER_LEGAL")); break;
                case AppConfig::BraillePaperSize::BLISTA_260x305: print(translation.get("BRAILLE_PAPER_BLISTA_260x305")); break;
                case AppConfig::BraillePaperSize::BLISTA_270x340: print(translation.get("BRAILLE_PAPER_BLISTA_270x340")); break;
                case AppConfig::BraillePaperSize::BLISTA_297x304: print(translation.get("BRAILLE_PAPER_BLISTA_297x304")); break;
            }
            print(" (" + (cfg.braille_orientation == AppConfig::BrailleOrientation::PORTRAIT ? translation.get("BRAILLE_ORIENTATION_PORTRAIT") : translation.get("BRAILLE_ORIENTATION_LANDSCAPE")) + ")\n");
            print(translation.format("BRAILLE_ESTIMATED_PAGES", "  Estimated pages: {0}", std::to_string(pageCount)) + "\n\n");
            
            print(translation.get("BRAILLE_PRINT_PROCEED") + " ");
            std::string confirm;
            if (!readLine(confirm)) return;
            
            if (confirm == "s" || confirm == "S") {
                braillePrinterSettingsMenu();
                print(translation.get("BRAILLE_PRINT_CANCELED_SETTINGS") + "\n");
            } else if (confirm == "y" || confirm == "Y") {
                // Enumerate printers
                std::vector<PrinterInfo> printers;
                std::string err;
                
                if (!braillePrinter.enumeratePrinters(printers, err)) {
                    print(translation.format("BRAILLE_PRINTER_ENUM_FAILED", "Failed to enumerate printers: {0}", err) + "\n");
                    if (logger) logger->log("BRAILLE_PRINTER", "Failed to enumerate printers: " + err);
                } else if (printers.empty()) {
                    print(translation.get("BRAILLE_NO_PRINTERS", "No printers found on this system.") + "\n");
                    print(translation.get("BRAILLE_INSTALL_MSG", "Please make sure your Index Braille printer is installed and connected.") + "\n");
                    if (logger) logger->log("BRAILLE_PRINTER", "No printers found");
                } else {
                    // Display available printers
                    print(translation.get("BRAILLE_AVAIL_PRINTERS", "Available printers:") + "\n");
                    for (size_t i = 0; i < printers.size(); i++) {
                        print("  " + std::to_string(i + 1) + ". " + printers[i].name);
                        if (printers[i].isDefault) {
                            print(" " + translation.get("BRAILLE_DEFAULT", "[DEFAULT]"));
                        }
                        print("\n");
                        print(translation.format("BRAILLE_PRINTER_INFO", "     Port: {0}, Driver: {1}", printers[i].port, printers[i].driver) + "\n");
                    }
                    
                    print(translation.get("BRAILLE_PRINTER_SELECT_PROMPT") + " ");
                    std::string printerChoice;
                    if (!readLine(printerChoice)) return;
                    
                    int choice = 0;
                    try {
                        choice = std::stoi(printerChoice);
                    } catch (...) {
                        choice = 0;
                    }
                    
                    if (choice < 1 || choice > static_cast<int>(printers.size())) {
                        print(translation.get("BRAILLE_PRINT_CANCELED") + "\n");
                    } else {
                        // Print to selected printer
                        std::string selectedPrinter = printers[choice - 1].name;
                        print(translation.format("BRAILLE_PRINTER_PRINTING_TO", "\nPrinting to: {0}", selectedPrinter) + "\n");
                        print(translation.get("BRAILLE_PRINTER_WAIT") + "\n");
                        
                        // Use braille printer settings from config and filtered points
                        if (braillePrinter.printBraille(selectedPrinter, toExportBraille, startFreq, endFreq, step, curveFlags, 
                                                       cfg, err)) {
                            print(translation.get("BRAILLE_PRINT_SUCCESS") + "\n");
                            if (logger) logger->log("BRAILLE_PRINTER", "Print successful to: " + selectedPrinter);
                        } else {
                            print(translation.format("BRAILLE_PRINT_FAILED", "\nPrint failed: {0}", err) + "\n");
                            print(translation.get("BRAILLE_PRINT_CHECK_LOG") + "\n");
                            if (logger) logger->log("BRAILLE_PRINTER", "Print failed: " + err);
                        }
                    }
                }
            } else {
                print(translation.get("BRAILLE_PRINT_CANCELED") + "\n");
            }
        }
    } else {
        print(translation.get("CANCELLED", "Canceled") + "\n");
    }
}

void ConsoleUI::importMenu(std::vector<MeasurementPoint>& pts) {
    clearScreen();
    print(formatHeading(translation.get("IMPORT_TITLE", "Import Measurement Data")));
    
    std::string err;
    auto files = ImportModule::listExportFiles(err);
    
    if (!err.empty()) {
        print(translation.format("IMPORT_ERROR", "Error: {0}", err) + "\n");
        return;
    }
    
    if (files.empty()) {
        print(translation.get("IMPORT_NO_FILES", "No export files found in Export/ directory.") + "\n");
        return;
    }
    
    print(translation.get("IMPORT_AVAILABLE", "Available files:") + "\n");
    for (size_t i = 0; i < files.size(); ++i) {
        print(std::to_string(i + 1) + ") " + files[i] + "\n");
    }
    
    print("\n" + translation.get("IMPORT_PROMPT", "Enter file number to import (or ESC to cancel): >") + " ");
    std::string input;
    if (!readLine(input) || input.empty()) {
        print(translation.get("IMPORT_CANCELED", "Canceled.") + "\n");
        return;
    }
    
    int idx = 0;
    try {
        idx = std::stoi(input);
    } catch (...) {
        print(translation.get("IMPORT_INVALID", "Invalid input.") + "\n");
        return;
    }
    
    if (idx <= 0 || idx > static_cast<int>(files.size())) {
        print(translation.get("IMPORT_CANCELED", "Canceled.") + "\n");
        return;
    }
    
    std::string filename = files[idx - 1];
    std::vector<MeasurementPoint> imported;
    
    if (ImportModule::importFile(filename, imported, err)) {
        pts = imported;
        print(translation.format("IMPORT_SUCCESS", "Successfully imported {0} measurement points from {1}", pts.size(), filename) + "\n");
        if (logger) logger->log("IMPORT", "Imported " + filename + ", " + std::to_string(pts.size()) + " points");
    } else {
        print(translation.format("IMPORT_FAILED", "Import failed: {0}", err) + "\n");
        if (logger) logger->log("IMPORT", "Failed to import " + filename + ": " + err);
    }
}

void ConsoleUI::deviceInfoMenu(NanoVNAProtocol* proto) {
    while (true) {
        clearScreen();  // Clear screen at the start of each loop iteration
        print(formatHeading(translation.get("DEVICE_INFO_TITLE", "Device Information")));
        print(translation.get("DEVICE_INFO_BATTERY", "(B)attery Status") + "  " + translation.get("DEVICE_INFO_INFO", "(I)nfo") + "  " + translation.get("DEVICE_INFO_SHELL", "(S)hell") + "  " + translation.get("MENU_HELP", "(H)elp") + "  " + translation.get("MSG_PRESS_ESC_BACK", "Press ESC to go back") + "\n");
        
        setUIContext("device_info", {
            {"i", translation.get("DEVICE_INFO_INFO", "(I)nfo"), false},
            {"b", translation.get("DEVICE_INFO_BATTERY", "(B)attery Status"), false},
            {"s", translation.get("DEVICE_INFO_SHELL", "(S)hell"), false},
            {"h", translation.get("MENU_HELP", "(H)elp"), false}
        });
        
        print(getPromptWithDepth("DEVICE_INFO_PROMPT", 2) + " ");
        char key;
        int ch = 0;
        bool hasInput = false;
        
        // Check for web interface input first
        if (webServer && webServer->isRunning() && webServer->hasInput()) {
            std::string webInput = webServer->readInput();
            if (!webInput.empty()) {
                if (logger) logger->log("UI_DEVICE_INFO", "Web input received: [" + webInput + "]");
                
                // Handle web input
                if (webInput[0] == '\x1B') {
                    ch = 27;  // ESC key
                } else {
                    ch = static_cast<unsigned char>(webInput[0]);
                }
                hasInput = true;
                
                if (logger) logger->log("UI_DEVICE_INFO", "Web input mapped to ch: " + std::to_string(ch));
            }
        }
        
        // Check for keyboard input if no web input
        if (!hasInput) {
            ch = consoleInput->getch();
            hasInput = true;
        }
        
        key = static_cast<char>(ch);
        // Convert uppercase to lowercase
        if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
        // Echo the key (but not ESC)
        if (ch != 27) print(std::string(1, key) + "\n");
        else print(translation.get("KEY_ESC") + "\n");
        
        if (key == 27) {  // ESC key
            break;
        } else if (key == 'i') {
            showInfo(proto);
        } else if (key == 'b') {
            showBattery(proto);
        } else if (key == 's') {
            debugShell(proto);
        } else if (key == 'h') {
            print(HelpModule::getDeviceInfoHelp(translation));
        } else if (key == '\r' || key == '\n') {
            // Enter key - redisplay menu (defined no-op per interaktionsmodell.md)
            continue;
        } else {
            print(translation.get("ERROR_UNKNOWN_COMMAND", "Unknown command.") + "\n");
        }
    }
    clearScreen();
}

void ConsoleUI::showBattery(NanoVNAProtocol* proto) {
    clearScreen();
    std::string battery;
    std::string err;
    if (!proto->getBattery(battery, err)) {
        print("Failed to get battery info: " + err + "\n");
        if (logger) logger->log("UI", "getBattery failed: " + err);
        return;
    }
    print("Battery voltage:\n" + battery + "\n");
    if (logger) logger->log("UI", "Battery info retrieved");
}

void ConsoleUI::debugShell(NanoVNAProtocol* proto) {
    clearScreen();
    // Check if COM port is selected
    if (!serial || !serial->isOpen()) {
        print("\n" + translation.get("ERROR_NO_PORT", 
            "Error: No COM port selected. Please select a port first using (P)ort menu.") + "\n");
        return;
    }
    
    print(formatHeading(translation.get("DEBUG_SHELL_TITLE", "Debug Shell - Direct NanoVNA Communication")));
    print(translation.get("DEBUG_SHELL_INSTRUCTIONS", 
        "Type commands and press Enter to send to NanoVNA. Press ESC to exit.") + "\n\n");
    
    if (logger) logger->log("SHELL", "Debug shell opened");
    
    bool running = true;
    while (running) {
        print(translation.get("DEBUG_SHELL_PROMPT", "Shell>") + " ");
        
        std::string command;
        bool buildingCommand = true;
        
        while (buildingCommand) {
            if (consoleInput->kbhit()) {
                int ch = consoleInput->getch();
                
                if (ch == 27) {  // ESC
                    print("\n" + translation.get("DEBUG_SHELL_EXIT", "Exiting shell...") + "\n");
                    running = false;
                    buildingCommand = false;
                } else if (ch == '\r' || ch == '\n') {  // Enter
                    print("\n");
                    buildingCommand = false;
                } else if (ch == 8) {  // Backspace
                    if (!command.empty()) {
                        command.pop_back();
                        print("\b \b");
                    }
                } else if (ch == 224 || ch == 0) {  // Arrow keys (extended keys)
                    // Extended key codes - read next character
                    int ext = consoleInput->getch();
                    // For now, we'll ignore arrow keys as they would require
                    // implementing a command history, which is beyond basic editing
                    // Just ignore these keys
                } else if (ch >= 32 && ch < 127) {  // Printable characters
                    command += static_cast<char>(ch);
                    print(std::string(1, ch));
                }
            }
        }
        
        // Send command if we're still running and have a command
        if (running && !command.empty()) {
            std::string err;
            
            // Log the command
            if (logger) logger->log("SHELL", "Sending: " + command);
            
            // Send command using writeData (which adds CR automatically)
            if (serial->writeData(command, err)) {
                // Read response lines until we get the prompt back
                bool readingResponse = true;
                int timeouts = 0;
                const int maxTimeouts = 100;  // Allow up to 10 seconds (100 * 100ms)
                
                while (readingResponse && timeouts < maxTimeouts) {
                    std::string line;
                    if (serial->readLine(line, 100, err)) {
                        // Trim the line
                        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                            line.pop_back();
                        }
                        
                        if (line.empty()) continue;
                        
                        // Check for prompt
                        if (line.find("ch>") == 0) {
                            // Got prompt, stop reading
                            readingResponse = false;
                        } else if (line == command) {
                            // Skip echo of command
                            continue;
                        } else {
                            // Display response line
                            print(line + "\n");
                            if (logger) logger->log("SHELL", "Response: " + line);
                        }
                    } else {
                        if (err == "Timeout") {
                            timeouts++;
                            err.clear();
                        } else {
                            print("Read error: " + err + "\n");
                            if (logger) logger->log("SHELL", "Read error: " + err);
                            readingResponse = false;
                        }
                    }
                }
                
                if (timeouts >= maxTimeouts) {
                    print("(No response or timeout)\n");
                }
            } else {
                print("Send error: " + err + "\n");
                if (logger) logger->log("SHELL", "Send error: " + err);
            }
        }
    }
    
    if (logger) logger->log("SHELL", "Debug shell closed");
}

void ConsoleUI::calibrateFlow(NanoVNAProtocol* proto) {
    clearScreen();
    print(translation.get("CAL_FLOW_TITLE", "Calibration flow: Open -> Short -> Load") + "\n");
    print(translation.get("CAL_FLOW_INSTRUCTIONS", "Press Enter when standard attached for each step.") + "\n");
    std::string err;
    print(translation.get("CAL_FLOW_OPEN", "Attach OPEN and press Enter... >") + " "); 
    readRawLineInput("");  // Wait for Enter (or Escape to cancel)
    if (!proto->sendCal("open", err)) { if (logger) logger->log("CAL", "sendCal open failed: " + err); }
    print(translation.get("CAL_FLOW_SHORT", "Attach SHORT and press Enter... >") + " "); 
    readRawLineInput("");  // Wait for Enter (or Escape to cancel)
    if (!proto->sendCal("short", err)) { if (logger) logger->log("CAL", "sendCal short failed: " + err); }
    print(translation.get("CAL_FLOW_LOAD", "Attach LOAD and press Enter... >") + " "); 
    readRawLineInput("");  // Wait for Enter (or Escape to cancel)
    if (!proto->sendCal("load", err)) { if (logger) logger->log("CAL", "sendCal load failed: " + err); }
    
    // Use localized yes/no prompt
    if (getYesNo(translation.get("CAL_WIZARD_SAVE_PROMPT", "Save calibration?"))) {
        proto->saveCal(err);
        if (logger) logger->log("CAL", "Saved calibration");
        print(translation.get("CAL_WIZARD_SAVED", "Saved.") + "\n");
    } else {
        print(translation.get("CAL_WIZARD_NOT_SAVED", "Calibration not saved.") + "\n");
    }
}

void ConsoleUI::calibrationMenu(NanoVNAProtocol* proto) {
    while (true) {
        clearScreen();  // Clear screen at the start of each loop iteration
        print(formatHeading(translation.get("CAL_MENU_TITLE", "Calibration Menu")));
        print(translation.get("CAL_MENU_LOAD", "(L)oad Calibration Profile") + "  " + translation.get("CAL_MENU_PERFORM", "(P)erform Calibration") + "  " + translation.get("HELP_COMMAND", "(H)elp") + "  " + translation.get("MSG_PRESS_ESC_BACK", "Press ESC to go back") + "\n");
        
        setUIContext("calibration_menu", {
            {"l", translation.get("CAL_MENU_LOAD", "(L)oad Calibration Profile"), false},
            {"p", translation.get("CAL_MENU_PERFORM", "(P)erform Calibration"), false},
            {"h", translation.get("HELP_COMMAND", "(H)elp"), false}
        });
        
        print(getPromptWithDepth("CAL_MENU_PROMPT", 2) + " ");
        int ch = 0;
        bool hasInput = false;
        
        // Check for web interface input first
        if (webServer && webServer->isRunning() && webServer->hasInput()) {
            std::string webInput = webServer->readInput();
            if (!webInput.empty()) {
                if (logger) logger->log("UI_CAL_MENU", "Web input received: [" + webInput + "]");
                
                // Handle web input
                if (webInput[0] == '\x1B') {
                    ch = 27;  // ESC key
                } else {
                    ch = static_cast<unsigned char>(webInput[0]);
                }
                hasInput = true;
                
                if (logger) logger->log("UI_CAL_MENU", "Web input mapped to ch: " + std::to_string(ch));
            }
        }
        
        // Check for keyboard input if no web input
        if (!hasInput) {
            ch = consoleInput->getch();
            hasInput = true;
        }
        
        char key = static_cast<char>(ch);
        // Convert uppercase to lowercase
        if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
        // Echo the key (but not ESC)
        if (ch != 27) print(std::string(1, key) + "\n");
        else print(translation.get("KEY_ESC") + "\n");
        
        if (ch == 27) {  // ESC key
            break;
        }
        
        if (key == 'l') {
            loadCalibrationProfile(proto);
        } else if (key == 'p') {
            performCalibrationWizard(proto);
        } else if (key == 'h') {
            print(HelpModule::getCalibrationMenuHelp(translation));
        } else {
            print(translation.get("ERROR_UNKNOWN_COMMAND", "Unknown command.") + "\n");
        }
    }
    clearScreen();
}

void ConsoleUI::loadCalibrationProfile(NanoVNAProtocol* proto) {
    clearScreen();
    if (!serial || !serial->isOpen()) {
        print(translation.get("ERROR_NO_PORT", "Error: No COM port selected. Use (P)ort to select a port first.") + "\n");
        if (logger) logger->log("CAL", "Attempted to load calibration without port");
        return;
    }
    
    print(formatHeading(translation.get("CAL_LOAD_TITLE", "Load Calibration Profile")));
    print(translation.get("CAL_LOAD_PROMPT", "Enter bank number (0-4 recommended, 0 is auto-loaded on device startup): > "));
    
    std::string bankStr;
    if (!readLine(bankStr)) {
        print(translation.get("CAL_LOAD_CANCELED", "Canceled.") + "\n");
        return;
    }
    
    // Try to parse bank number
    try {
        int bankNumber = std::stoi(bankStr);
        std::string err;
        
        if (proto->loadCal(bankNumber, err)) {
            print(translation.format("CAL_LOAD_SUCCESS", "Calibration profile loaded from bank {0}", bankNumber) + "\n");
            if (logger) logger->log("CAL", "Loaded calibration from bank " + std::to_string(bankNumber));
            
            // Update config and save
            cfg.calibration_bank = bankNumber;
            saveSettings();
        } else {
            print(translation.format("CAL_LOAD_FAILED", "Failed to load calibration from bank {0}: {1}", bankNumber, err) + "\n");
            if (logger) logger->log("CAL", "Failed to load from bank " + std::to_string(bankNumber) + ": " + err);
        }
    } catch (...) {
        print(translation.get("CAL_LOAD_INVALID", "Invalid bank number") + "\n");
    }
}

void ConsoleUI::performCalibrationWizard(NanoVNAProtocol* proto) {
    clearScreen();
    if (!serial || !serial->isOpen()) {
        print(translation.get("ERROR_NO_PORT", "Error: No COM port selected. Use (P)ort to select a port first.") + "\n");
        if (logger) logger->log("CAL", "Attempted to perform calibration without port");
        return;
    }
    
    print(formatHeading(translation.get("CAL_WIZARD_TITLE", "Calibration Wizard")));
    std::string err;
    std::string s;
    
    // Reset calibration first
    if (!proto->calReset(err)) {
        print(translation.format("CAL_RESET_FAILED", "Failed to reset calibration: {0}", err) + "\n");
        if (logger) logger->log("CAL", "calReset failed: " + err);
        return;
    }
    
    // Step 1: Open
    print(translation.get("CAL_WIZARD_OPEN", "Attach OPEN standard and press Enter... > "));
    if (!readLine(s)) return;
    if (!proto->sendCal("open", err)) {
        print(translation.format("CAL_FAILED", "Calibration failed: {0}", err) + "\n");
        if (logger) logger->log("CAL", "sendCal open failed: " + err);
        return;
    }
    
    // Step 2: Short
    print(translation.get("CAL_WIZARD_SHORT", "Attach SHORT standard and press Enter... > "));
    if (!readLine(s)) return;
    if (!proto->sendCal("short", err)) {
        print(translation.format("CAL_FAILED", "Calibration failed: {0}", err) + "\n");
        if (logger) logger->log("CAL", "sendCal short failed: " + err);
        return;
    }
    
    // Step 3: Load
    print(translation.get("CAL_WIZARD_LOAD", "Attach LOAD (50 Ohm) standard and press Enter... > "));
    if (!readLine(s)) return;
    if (!proto->sendCal("load", err)) {
        print(translation.format("CAL_FAILED", "Calibration failed: {0}", err) + "\n");
        if (logger) logger->log("CAL", "sendCal load failed: " + err);
        return;
    }
    
    // Ask about S21 calibration
    bool performS21 = getYesNo(translation.get("CAL_WIZARD_S21_PROMPT", "Do you want to perform additional S21 calibration (isoln and thru)?"));
    
    if (performS21) {
        // Step 4: Isoln
        print(translation.get("CAL_WIZARD_ISOLN", "Attach LOAD (50 Ohm) to Port 1 and press Enter... > "));
        if (!readLine(s)) return;
        if (!proto->sendCal("isoln", err)) {
            print(translation.format("CAL_FAILED", "Calibration failed: {0}", err) + "\n");
            if (logger) logger->log("CAL", "sendCal isoln failed: " + err);
            return;
        }
        
        // Step 5: Thru
        print(translation.get("CAL_WIZARD_THRU", "Connect Port 1 to Port 2 with through cable and press Enter... > "));
        if (!readLine(s)) return;
        if (!proto->sendCal("thru", err)) {
            print(translation.format("CAL_FAILED", "Calibration failed: {0}", err) + "\n");
            if (logger) logger->log("CAL", "sendCal thru failed: " + err);
            return;
        }
    }
    
    // Finalize calibration
    if (!proto->calDone(err)) {
        print(translation.format("CAL_DONE_FAILED", "Failed to finalize calibration: {0}", err) + "\n");
        if (logger) logger->log("CAL", "calDone failed: " + err);
        return;
    }
    
    // Ask for bank number to save
    print(translation.get("CAL_WIZARD_BANK_PROMPT", "Enter bank number to save calibration (0-4 recommended, 0 is auto-loaded): > "));
    std::string bankStr;
    if (!readLine(bankStr)) {
        print(translation.get("CAL_WIZARD_CANCELED", "Calibration not saved.") + "\n");
        return;
    }
    
    // Try to parse bank number
    try {
        int bankNumber = std::stoi(bankStr);
        print(translation.format("CAL_WIZARD_SAVING", "Saving calibration to bank {0}...", bankNumber) + "\n");
        
        if (proto->saveCal(bankNumber, err)) {
            print(translation.format("CAL_WIZARD_COMPLETE", "Calibration complete and saved to bank {0}", bankNumber) + "\n");
            if (logger) logger->log("CAL", "Saved calibration to bank " + std::to_string(bankNumber));
            
            // Update config and save
            cfg.calibration_bank = bankNumber;
            saveSettings();
        } else {
            print(translation.format("CAL_SAVE_FAILED", "Failed to save calibration: {0}", err) + "\n");
            if (logger) logger->log("CAL", "saveCal failed: " + err);
        }
    } catch (...) {
        print(translation.get("CAL_WIZARD_NOT_SAVED", "Calibration not saved.") + "\n");
    }
}


bool ConsoleUI::interactiveSelectPort(NanoVNAProtocol* proto) {
    (void)proto;
    clearScreen();
    if (!serial) {
        print(translation.get("ERROR_SERIAL_NOT_AVAILABLE", "Internal error: SerialComm not available.") + "\n");
        if (logger) logger->log("UI", "SerialComm missing");
        return false;
    }
    print(translation.get("PORT_SCANNING", "Scanning COM ports...") + "\n");
    auto ports = SerialComm::listAvailablePortsWithNames();
    if (ports.empty()) {
        print(translation.get("ERROR_NO_PORTS", "No COM ports detected.") + "\n");
        return false;
    }
    
    // Check if the currently configured port is in the list
    bool currentPortInList = false;
    for (const auto& port : ports) {
        if (port.portName == cfg.serial_port) {
            currentPortInList = true;
            break;
        }
    }
    
    for (size_t i=0;i<ports.size();++i) {
        std::string marker = "";
        if (ports[i].portName == cfg.serial_port) {
            marker = " [CURRENTLY SELECTED]";
        }
        print(std::to_string(i+1) + ") " + ports[i].portName + " - " + ports[i].deviceName + marker + "\n");
    }
    
    // If current port is not in the available list, show it separately
    if (!currentPortInList && !cfg.serial_port.empty()) {
        print("\nCurrently selected port (not detected): " + cfg.serial_port + "\n");
    }
    
    print(getPromptWithDepth("PORT_CHOOSE_PROMPT", 2) + " ");
    // Use raw mode input with Escape support (Phase 4)
    auto result = readRawLineInput("");
    if (result.cancelled || result.value.empty()) {
        // readRawLineInput already prints "CANCELLED" when ESC is pressed, so don't print it again
        return false;
    }
    int idx = atoi(result.value.c_str());
    if (idx <= 0 || (size_t)idx > ports.size()) {
        // Invalid number - don't say cancelled, just invalid
        print(translation.get("ERROR_INVALID_PORT", "Invalid port number.") + "\n");
        return false;
    }

    cfg.serial_port = ports[idx-1].portName;
    print(translation.format("PORT_SELECTED", "Selected port {0} ({1})", cfg.serial_port, ports[idx-1].deviceName) + "\n");
    if (logger) logger->log("UI", "User selected port: " + cfg.serial_port + " (" + ports[idx-1].deviceName + ")");
    saveSettings();

    std::string err;
    if (serial->isOpen()) serial->closePort();
    if (!serial->openPort(cfg.serial_port, cfg.baud, err)) {
        print(translation.format("PORT_OPEN_FAILED", "Failed to open {0}: {1}", cfg.serial_port, err) + "\n");
        if (logger) logger->log("SERIAL", "Open failed: " + err);
        return false;
    }
    if (logger) logger->log("SERIAL", "Port opened from UI");
    
    // Auto-load calibration from saved bank
    if (cfg.calibration_bank >= 0) {
        std::string calErr;
        if (proto->loadCal(cfg.calibration_bank, calErr)) {
            print(translation.format("CAL_LOAD_SUCCESS", "Calibration profile loaded from bank {0}", cfg.calibration_bank) + "\n");
            if (logger) logger->log("CAL", "Auto-loaded calibration from bank " + std::to_string(cfg.calibration_bank));
        } else {
            // Only log error, don't show to user as it might be expected for empty banks
            if (logger) logger->log("CAL", "Auto-load from bank " + std::to_string(cfg.calibration_bank) + " failed: " + calErr);
        }
    }
    
    return true;
}

std::vector<MeasurementPoint> ConsoleUI::interactiveRangeAndScan(NanoVNAProtocol* proto) {
    // Check if COM port is configured before allowing range scan
    if (cfg.serial_port.empty()) {
        print(translation.get("ERROR_NO_PORT", "Error: No COM port selected. Use (P)ort to select a port first.") + "\n");
        if (logger) logger->log("UI", "Range scan attempted without COM port configured");
        return {};
    }
    
    // Phase 3: Sequential input with backtracking (3 steps: start → end → step)
    // Escape in step N returns to step N-1
    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t step = 0;
    std::string startStr;
    std::string endStr;
    std::string stepStr;
    
    // Sequential input loop - allows backtracking through all 3 steps
    while (true) {
        // Step 1: Get start frequency
        print(translation.get("RANGE_ENTER_START", "Enter start frequency in Hz (e.g. 144000000 or 144M): >") + " ");
        auto startResult = readRawLineInput("", startStr);  // Show previous value if returning from step 2
        
        if (startResult.cancelled || startResult.value.empty()) {
            // Escape in step 1 - cancel entire operation
            return {};
        }
        
        startStr = startResult.value;
        
        if (!parseFrequencyString(startStr, start)) {
            print(translation.get("RANGE_INVALID_START", "Invalid start") + "\n");
            continue;  // Ask again
        }
        
        // Step 2: Get end frequency (with backtracking support)
        while (true) {
            print(translation.get("RANGE_ENTER_END", "Enter end frequency in Hz (e.g. 146000000 or 146M): >") + " ");
            auto endResult = readRawLineInput("", endStr, true);  // silentCancel = true, we handle the message
            
            if (endResult.cancelled || endResult.value.empty()) {
                // Escape in step 2 - return to step 1
                print(translation.get("GOING_BACK", "[Going back to previous step]") + "\n");
                break;  // Break inner loop, continue outer loop
            }
            
            endStr = endResult.value;
            
            if (!parseFrequencyString(endStr, end)) {
                print(translation.get("RANGE_INVALID_END", "Invalid end") + "\n");
                continue;  // Ask again
            }
            
            if (start >= end) {
                print(translation.get("RANGE_INVALID_RANGE", "Invalid range: start must be less than end") + "\n");
                continue;  // Ask again
            }
            
            // Step 3: Get step size (with backtracking support)
            while (true) {
                print(translation.get("RANGE_ENTER_STEP", "Enter step in Hz (e.g. 1000 or 1K): >") + " ");
                auto stepResult = readRawLineInput("", stepStr, true);  // silentCancel = true, we handle the message
                
                if (stepResult.cancelled || stepResult.value.empty()) {
                    // Escape in step 3 - return to step 2
                    print(translation.get("GOING_BACK", "[Going back to previous step]") + "\n");
                    break;  // Break innermost loop, return to step 2
                }
                
                stepStr = stepResult.value;
                
                if (!parseFrequencyString(stepStr, step)) {
                    print(translation.get("RANGE_INVALID_STEP", "Invalid step") + "\n");
                    continue;  // Ask again
                }
                
                if (step == 0) {
                    print(translation.get("RANGE_INVALID_STEP_ZERO", "Invalid step: must be greater than zero") + "\n");
                    continue;  // Ask again
                }
                
                if (step > (end - start)) {
                    print(translation.get("RANGE_INVALID_STEP_LARGE", "Invalid step: step is larger than the range") + "\n");
                    continue;  // Ask again
                }
                
                // All 3 steps complete successfully
                cfg.start_freq = start;
                cfg.end_freq = end;
                cfg.step = step;
                saveSettings();
                goto range_scan_complete;  // Exit all loops
            }
            // If we get here, user pressed Escape in step 3, continue to step 2 loop
        }
        // If we get here, user pressed Escape in step 2, loop back to step 1
    }
    
    range_scan_complete:

    if (logger) logger->log("UI", "User set range");

    // Perform measurement with timing
    auto pts = performMeasurementWithTiming(proto, start, end, step);
    
    if (!pts.empty()) {
        showSummary(pts);
        
        // Display measurement duration
        print(translation.format("SCAN_COMPLETE", "Range scan complete. Points: {0}", pts.size()));
        if (cfg.last_measurement_duration_seconds > 0) {
            char buf[100];
            snprintf(buf, sizeof(buf), " (Duration: %.2f seconds)", cfg.last_measurement_duration_seconds);
            print(buf);
        }
        print("\n");
        
        if (logger) logger->log("UI", "Range scan completed");
    }
    
    return pts;
}

void ConsoleUI::saveSettings() {
    std::string err;
    std::string path = "config/app_settings.cfg";
    if (!saveAppSettings(cfg, path, err)) {
        print("Failed to save settings: " + err + "\n");
        if (logger) logger->log("UI", "Failed to save settings: " + err);
    } else {
        if (logger) logger->log("UI", "Settings saved");
    }
}

std::vector<MeasurementPoint> ConsoleUI::autostartMeasurement(NanoVNAProtocol* proto) {
    clearScreen();
    std::string err;
    if (logger) logger->log("UI", "Autostart measurement initiated");
    if (cfg.serial_port.empty()) {
        print(translation.get("ERROR_NO_PORT", "No serial port configured. Use (P)ort to select.") + "\n");
        return {};
    }

    // Check if frequency range is configured
    if (cfg.start_freq == 0 || cfg.end_freq == 0 || cfg.step == 0) {
        print(translation.get("ERROR_NO_RANGE", "No frequency range configured. Use (R)ange command to configure.") + "\n");
        if (logger) logger->log("UI", "Autostart measurement attempted without frequency range configured");
        return {};
    }

    if (serial && !serial->isOpen()) {
        if (!serial->openPort(cfg.serial_port, cfg.baud, err)) {
            print("Failed to open " + cfg.serial_port + ": " + err + "\n");
            if (logger) logger->log("SERIAL", "Open failed: " + err);
            return {};
        }
    }

    // Perform measurement with timing
    auto pts = performMeasurementWithTiming(proto, cfg.start_freq, cfg.end_freq, cfg.step);
    
    if (!pts.empty()) {
        showSummary(pts);
        
        // Display measurement duration
        if (cfg.last_measurement_duration_seconds > 0) {
            char buf[100];
            snprintf(buf, sizeof(buf), " (Duration: %.2f seconds)", cfg.last_measurement_duration_seconds);
            print(std::string(buf) + "\n");
        }
    }

    // Don't auto-play audio - user should enter acoustic analysis mode manually
    // Audio playback removed to prevent unwanted audio after autostart measurement
    
    print(translation.get("AUTOSTART_COMPLETE", "Autostart: measurement complete.") + "\n");
    if (logger) logger->log("UI", "Autostart measurement finished");
    return pts;
}

std::vector<MeasurementPoint> ConsoleUI::performMeasurementWithTiming(NanoVNAProtocol* proto, uint64_t startHz, uint64_t endHz, uint64_t stepHz, bool suppressProgress) {
    std::string err;
    if (cfg.serial_port.empty()) {
        return {};
    }

    if (serial && !serial->isOpen()) {
        if (!serial->openPort(cfg.serial_port, cfg.baud, err)) {
            if (logger) logger->log("SERIAL", "Open failed during timed measurement: " + err);
            return {};
        }
    }
    
    // Show initial progress message only if not suppressed
    if (!suppressProgress) {
        print(translation.format("MEASURING_PROGRESS", "Measuring... {0}% complete", "0"));
    }

    // Set up progress callback for granular updates
    proto->setProgressCallback([this, proto, suppressProgress](uint32_t current, uint32_t total) {
        if (total > 0 && !suppressProgress) {
            int percent = static_cast<int>((current * 100) / total);
            print("\r" + translation.format("MEASURING_PROGRESS", "Measuring... {0}% complete", std::to_string(percent)) + "    ");
        }
        
        // Check for ESC key press to cancel
        if (consoleInput->kbhit()) {
            int key = consoleInput->getch();
            if (key == 27) {  // ESC key
                proto->setCancelled(true);
            }
        }
    });

    // Start timing
    auto startTime = std::chrono::high_resolution_clock::now();

    // Reset cancellation flag before starting
    proto->setCancelled(false);

    uint32_t outmask = 7;
    std::string scanText;
    
    // Special case: single point measurement (start == end)
    // scanByStep forces minimum 2 points, so use scan() directly for single point
    if (startHz == endHz) {
        if (!proto->scan(startHz, endHz, 1, outmask, scanText, err)) {
            print("\n");  // Newline after progress
            if (logger) logger->log("PROTO", "Timed scan (single point) failed: " + err);
            proto->setProgressCallback(nullptr);  // Clear callback
            return {};
        }
    } else {
        if (!proto->scanByStep(startHz, endHz, stepHz, outmask, scanText, err)) {
            print("\n");  // Newline after progress
            if (proto->getCancelled()) {
                print(translation.get("MEASUREMENT_CANCELLED", "[Measurement cancelled]") + "\n");
                if (logger) logger->log("PROTO", "Measurement cancelled by user");
            } else {
                if (logger) logger->log("PROTO", "Timed scanByStep failed: " + err);
            }
            proto->setProgressCallback(nullptr);  // Clear callback
            return {};
        }
    }

    // Clear progress callback
    proto->setProgressCallback(nullptr);

    // End timing
    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = endTime - startTime;
    cfg.last_measurement_duration_seconds = duration.count();
    
    // Show completion (100%) only if not suppressed
    if (!suppressProgress) {
        print("\r" + translation.format("MEASURING_PROGRESS", "Measuring... {0}% complete", "100") + "    \n");
    }
    
    if (logger) {
        char buf[200];
        snprintf(buf, sizeof(buf), "Measurement completed in %.3f seconds", cfg.last_measurement_duration_seconds);
        logger->log("UI", buf);
    }

    MeasurementModule mm;
    if (mathLogger) {
        mm.setMathLogger(mathLogger);
    }
    auto pts = mm.parseDeviceData(scanText, startHz, endHz);
    return pts;
}

void ConsoleUI::showInfo(NanoVNAProtocol* proto) {
    clearScreen();
    std::string info;
    std::string err;
    if (!proto->getInfo(info, err)) {
        print("Failed to get info: " + err + "\n");
        if (logger) logger->log("UI", "getInfo failed: " + err);
        return;
    }
    print("Device info:\n" + info + "\n");
    if (logger) logger->log("UI", "Device info retrieved");
}

void ConsoleUI::run(NanoVNAProtocol* proto) {
    // Check if this is the first start and run wizard if needed
    if (cfg.first_start) {
        runFirstStartWizard();
    }
    
    printOptionsLine();
    std::vector<MeasurementPoint> lastPts;
    if (cfg.autostart) {
        lastPts = autostartMeasurement(proto);
    }
    while (true) {
        print(getPromptWithDepth("MAIN_MENU_PROMPT", 1) + " ");
        char key = '\0';
        bool fromWeb = false;
        
        // Check for web interface input or keyboard input
        // Loop until we get input from either source
        while (key == '\0') {
            // Check for web interface input first
            if (webServer && webServer->isRunning() && webServer->hasInput()) {
                std::string webInput = webServer->readInput();
                if (!webInput.empty()) {
                    if (logger) logger->log("UI", "Web input received: [" + webInput + "]");
                    
                    // Web now sends single characters immediately (matching console behavior)
                    // Process the first character directly
                    key = webInput[0];
                    
                    // If there are more characters (e.g. ANSI escape sequences), 
                    // queue the rest for later processing
                    if (webInput.length() > 1) {
                        std::string remaining = webInput.substr(1);
                        for (char c : remaining) {
                            webServer->queueInput(std::string(1, c));
                        }
                        if (logger) logger->log("UI", "Queued remaining " + std::to_string(remaining.length()) + " characters back");
                    }
                    
                    fromWeb = true;
                    
                    // Echo to web interface and console (only printable characters)
                    if (key >= 32 && key <= 126) {
                        std::string echo = std::string(1, key) + "\n";
                        print(echo);
                    }
                    
                    if (logger) logger->log("UI", "Processing web input as key: " + std::string(1, key));
                    break;
                }
            }
            
            // Check if keyboard key is available
            if (consoleInput->kbhit()) {
                key = static_cast<char>(consoleInput->getch());
                
                // Convert uppercase to lowercase
                if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
                
                // Echo printable ASCII characters only (32-126), filtering control characters like ESC
                if (key >= 32 && key <= 126) {
                    std::string echo = std::string(1, key) + "\n";
                    print(echo);
                }
                break;
            }
            
            // Small sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Additional lowercase conversion for web input
        if (fromWeb && key >= 'A' && key <= 'Z') {
            key = key - 'A' + 'a';
        }
        
        // Get language-specific key mappings
        std::string keySummary = translation.get("MENU_KEY_SUMMARY", "s");
        std::string keyTable = translation.get("MENU_KEY_TABLE", "t");
        std::string keyAcoustic = translation.get("MENU_KEY_ACOUSTIC", "a");
        std::string keyExport = translation.get("MENU_KEY_EXPORT", "e");
        std::string keyLoad = translation.get("MENU_KEY_LOAD", "l");
        std::string keyCalibrate = translation.get("MENU_KEY_CALIBRATE", "k");
        std::string keyPort = translation.get("MENU_KEY_PORT", "p");
        std::string keyRange = translation.get("MENU_KEY_RANGE", "r");
        std::string keyDeviceInfo = translation.get("MENU_KEY_DEVICE_INFO", "d");
        std::string keyManual = translation.get("MENU_KEY_MANUAL", "m");
        std::string keyOptions = translation.get("MENU_KEY_OPTIONS", "o");
        std::string keySweep = translation.get("MENU_KEY_SWEEP", "w");
        std::string keyWebInterface = translation.get("MENU_KEY_WEB_INTERFACE", "i");
        std::string keyHelp = translation.get("MENU_KEY_HELP", "h");
        std::string keyQuit = translation.get("MENU_KEY_QUIT", "q");
        std::string keyComfort = translation.get("MENU_KEY_COMFORT", "u");
        std::string keyDocs = translation.get("MENU_KEY_DOCS", "?");
        
        if (key == keyQuit[0]) {
            if (logger) logger->log("UI", "User quit");
            break;
        } else if (key == keySummary[0]) {
            showSummary(lastPts);
            printOptionsLine();
        } else if (key == keyTable[0]) {
            showTablePaginated(lastPts);
            printOptionsLine();
        } else if (key == keyAcoustic[0]) {
            if (!cfg.audio) { 
                print(translation.get("MSG_AUDIO_DISABLED", "Audio disabled.") + "\n"); 
                continue; 
            }
            runAcousticAnalysis(lastPts, proto);
            printOptionsLine();
        } else if (key == keyExport[0]) {
            exportMenu(lastPts);
            printOptionsLine();
        } else if (key == keyLoad[0]) {
            importMenu(lastPts);
            printOptionsLine();
        } else if (key == keyCalibrate[0]) {
            calibrationMenu(proto);
            printOptionsLine();
        } else if (key == keyPort[0]) {
            (void)interactiveSelectPort(proto);
            printOptionsLine();
        } else if (key == keyRange[0]) {
            auto newPts = interactiveRangeAndScan(proto);
            if (!newPts.empty()) {
                lastPts = std::move(newPts);
            }
            printOptionsLine();
        } else if (key == keyDeviceInfo[0]) {
            deviceInfoMenu(proto);
            printOptionsLine();
        } else if (key == keyManual[0]) {
            auto newPts = autostartMeasurement(proto);
            if (!newPts.empty()) {
                lastPts = std::move(newPts);
            }
            printOptionsLine();
        } else if (key == keySweep[0]) {
            // Toggle continuous sweep mode
            cfg.continuous_sweep_enabled = !cfg.continuous_sweep_enabled;
            saveSettings();
            
            clearScreen();
            if (cfg.continuous_sweep_enabled) {
                // Check if we have timing information
                if (cfg.last_measurement_duration_seconds <= 0.0) {
                    print(translation.get("SWEEP_NO_TIMING", 
                        "Continuous sweep enabled, but no timing information available.\n"
                        "Please perform a Range Scan (R) or Manual Scan (M) first to establish timing.") + "\n");
                } else {
                    char buf[200];
                    snprintf(buf, sizeof(buf), 
                        translation.get("SWEEP_ENABLED", "Continuous sweep ENABLED (measurement time: %.2f seconds)").c_str(), 
                        cfg.last_measurement_duration_seconds);
                    print(std::string(buf) + "\n");
                }
            } else {
                print(translation.get("SWEEP_DISABLED", "Continuous sweep DISABLED") + "\n");
            }
            printOptionsLine();
        } else if (key == keyOptions[0]) {
            optionsMenu();
            // Reload translations after options menu (language might have changed)
            printOptionsLine();
        } else if (key == keyWebInterface[0]) {
            webInterfaceMenu();
            printOptionsLine();
        } else if (key == keyComfort[0]) {
            comfortFunctionsMenu(lastPts, proto);
            printOptionsLine();
        } else if (key == keyHelp[0]) {
            print(HelpModule::getMainMenuHelp(translation));
        } else if (key == keyDocs[0] || key == '?') {
            documentationMenu();
            printOptionsLine();
        } else if (key == 27) {  // ESC key in main menu
            // According to doc/interaktionsmodell.md section 2.2:
            // Escape in main menu (depth 1) should be no-op or show a message
            print(translation.get("MAIN_MENU_ESC_INFO", "[Info: Already at main menu level. Press Q to quit.]") + "\n");
        } else if (key == '\r' || key == '\n') {
            // Enter key - refresh display with clean screen
            clearScreen();
            printOptionsLine();
        } else {
            print(translation.get("MSG_UNKNOWN_COMMAND", "Unknown command. Press H for help.") + "\n");
        }
    }
    
    // Stop web server if running
    if (webServer && webServer->isRunning()) {
        webServer->stop();
        webServer.reset();
        if (logger) logger->log("WEBSERVER", "Web interface stopped on application exit");
    }
    
    audio.close();
}

void ConsoleUI::runAcousticAnalysis(const std::vector<MeasurementPoint>& pts, NanoVNAProtocol* proto) {
    clearScreen();
    if (pts.empty()) {
        print(translation.get("ERROR_NO_DATA", "No measurement data available for acoustic analysis.") + "\n");
        return;
    }
    
    AcousticAnalyzer analyzer(logger, mathLogger, &translation);
    analyzer.setOutputCallback([this](const std::string& text) { print(text); });
    analyzer.setData(pts);
    
    // Create and configure audio engine based on settings
    std::shared_ptr<IAudioEngine> audioEngine;
    if (cfg.audio_engine == AudioEngineType::MIDI) {
        // Use MIDI engine
        auto midiEngine = std::make_shared<MIDIEngine>();
        midiEngine->setLogger(logger);  // Set logger for MIDI engine debug output
        if (!midiEngine->open()) {
            // Log MIDI engine failure to debug log
            if (logger) logger->log("MIDI", "Failed to open MIDI engine, falling back to Synthesizer");
            print(translation.get("MIDI_ERROR_OPEN_FAILED_FALLBACK", "Warning: Failed to open MIDI engine, falling back to Synthesizer.") + "\n");
            // Fall back to synthesizer
            auto synthEngine = std::make_shared<SynthesizerEngine>();
            // Configure waveforms for each curve from config
            for (int i = 0; i < 5; i++) {
                synthEngine->setCurveWaveform(i, cfg.synth_waveforms[i]);
            }
            synthEngine->open();
            audioEngine = synthEngine;
        } else {
            // Log successful MIDI engine opening
            if (logger) logger->log("MIDI", "MIDI engine opened successfully");
            // Configure MIDI instruments for each curve AFTER opening
            for (int i = 0; i < 5; i++) {
                midiEngine->setCurveInstrument(i, cfg.midi_instruments[i]);
            }
            
            // Set MIDI playback mode (gliding vs dotted)
            midiEngine->setGlidingMode(cfg.midi_playback_mode == MIDIPlaybackMode::GLIDING);
            
            // Set synth frequency range for pitch bend calculations
            midiEngine->setSynthFrequencyRange(cfg.synth_min_freq_hz, cfg.synth_max_freq_hz);
            
            // Set interpolated panning mode and strength
            midiEngine->setInterpolatedPanMode(cfg.midi_interpolated_pan_mode);
            midiEngine->setInterpolationStrength(cfg.midi_interpolation_strength);
            
            // Set custom ruler instruments
            midiEngine->setRulerCustomInstruments(cfg.ruler_custom_sound_midi_gliding, cfg.ruler_custom_sound_midi_dotted);
            
            audioEngine = midiEngine;
        }
    } else {
        // Use synthesizer engine (default)
        auto synthEngine = std::make_shared<SynthesizerEngine>();
        // Configure waveforms for each curve from config
        for (int i = 0; i < 5; i++) {
            synthEngine->setCurveWaveform(i, cfg.synth_waveforms[i]);
        }
        synthEngine->open();
        audioEngine = synthEngine;
    }
    analyzer.setAudioEngine(audioEngine);
    
    // Apply Smith configuration from settings
    auto smith = analyzer.getSmithVisualizer();
    if (smith) {
        smith->setSmithCuesVolume(cfg.smith_cues_volume);
        smith->setNoiseType(cfg.smith_noise_type);
        
        // Apply center pulse configuration
        smith->setCenterPulseEnabled(cfg.center_pulse_enabled);
        smith->setCenterPulseVolume(cfg.center_pulse_volume);
        smith->setCenterPulseInterval(cfg.center_pulse_interval);
        smith->setCenterPulseWaveform(cfg.center_pulse_waveform);
        
        // Apply axis crossing events configuration
        smith->setAxisEventsEnabled(cfg.axis_events_enabled);
        smith->setAxisEventsVolume(cfg.axis_events_volume);
        smith->setAxisEventsDuration(cfg.axis_events_duration_ms);
        smith->setAxisEventsPitchRange(cfg.axis_events_pitch_min, cfg.axis_events_pitch_max);
        smith->setAxisCrossingSound(cfg.axis_crossing_sound);
        
        // Apply surround configuration
        smith->setSurroundConfig(cfg.surround_front_distance, cfg.surround_back_distance,
                                cfg.surround_side_distance, cfg.surround_center_strength,
                                cfg.surround_fb_separation, cfg.surround_side_emphasis,
                                cfg.surround_fading_curve);
        
        // Set math logger for debug output
        smith->setMathLogger(mathLogger);
        
        if (logger) {
            logger->log("SMITH", "Applied config: Volume=" + std::to_string(cfg.smith_cues_volume) + 
                       "%, NoiseType=" + std::to_string(static_cast<int>(cfg.smith_noise_type)));
            logger->log("SMITH", "Surround config applied from settings");
        }
    }
    
    // Load settings from config
    analyzer.setSmoothMode(cfg.acoustic_smooth_mode);
    analyzer.setPlaybackTimeSeconds(cfg.acoustic_time_seconds);
    analyzer.setFrequencyRange(cfg.synth_min_freq_hz, cfg.synth_max_freq_hz);
    analyzer.setDottedDurationMs(cfg.dotted_duration_ms);
    analyzer.setDottedPauseMs(cfg.dotted_pause_ms);
    analyzer.setFreezePointPauseMs(cfg.freeze_point_pause_ms);
    analyzer.setLoopPauseMs(cfg.loop_pause_ms);
    analyzer.setInvertedLoopGapMs(cfg.inverted_loop_gap_ms);
    analyzer.setMasterVolume(cfg.master_volume);
    analyzer.setRulerVolume(cfg.ruler_volume);
    analyzer.setRulerBlipDuration(cfg.ruler_blip_duration_ms);
    analyzer.setRulerLengtheningFactor(cfg.ruler_lengthening_factor_percent);
    
    // Set X-axis ruler configuration
    if (cfg.x_axis_ruler_enabled) {
        analyzer.toggleXAxisRuler();  // Enable if configured
    }
    analyzer.setXAxisRulerVolume(cfg.x_axis_ruler_volume);
    analyzer.setXAxisRulerBlipDuration(cfg.x_axis_ruler_blip_duration_ms);
    analyzer.setXAxisRulerNoiseType(static_cast<AcousticAnalyzer::XAxisRulerNoiseType>(cfg.x_axis_ruler_noise_type));
    analyzer.setXAxisRulerMidiDrum(cfg.x_axis_ruler_midi_drum);
    
    // Set status line configuration
    if (cfg.status_line_enabled) {
        analyzer.toggleStatusLine();  // Enable if configured
    }
    analyzer.setStatusLineContent(static_cast<AcousticAnalyzer::StatusLineContent>(cfg.status_line_content));
    analyzer.setStatusLineShowPosition(cfg.status_line_show_position);
    analyzer.setStatusLineShowFrequency(cfg.status_line_show_frequency);
    analyzer.setStatusLineShowSWR(cfg.status_line_show_swr);
    analyzer.setStatusLineShowRL(cfg.status_line_show_rl);
    analyzer.setStatusLineShowImpedance(cfg.status_line_show_impedance);
    analyzer.setStatusLineShowReactance(cfg.status_line_show_reactance);
    analyzer.setStatusLineShowPhase(cfg.status_line_show_phase);
    
    // Set ruler sound mode configuration
    analyzer.setRulerSoundMode(static_cast<AcousticAnalyzer::RulerSoundMode>(cfg.ruler_sound_mode));
    analyzer.setRulerCustomSoundSynth(cfg.ruler_custom_sound_synth);
    analyzer.setRulerCustomSoundMidiGliding(cfg.ruler_custom_sound_midi_gliding);
    analyzer.setRulerCustomSoundMidiDotted(cfg.ruler_custom_sound_midi_dotted);
    
    // Set continuous replay if configured
    if (cfg.continuous_replay && !analyzer.isContinuousReplay()) {
        analyzer.toggleContinuousReplay();
    }
    
    for (int i = 0; i < 5; i++) {
        // Fix: Check current state vs desired state, only toggle if different
        bool isCurrentlyEnabled = analyzer.isCurveEnabled(i);
        bool shouldBeEnabled = cfg.curve_enabled[i];
        if (isCurrentlyEnabled != shouldBeEnabled) {
            analyzer.toggleCurve(i);  // Toggle only if state needs to change
        }
        
        // Set volume based on active audio engine
        if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
            analyzer.setCurveVolume(i, cfg.curve_volume_synth[i]);
        } else {
            analyzer.setCurveVolume(i, cfg.curve_volume_midi[i]);
        }
    }
    
    print(formatHeading(translation.get("ACOUSTIC_TITLE", "Acoustic Analysis Mode")));
    print(translation.format("ACOUSTIC_DATA_POINTS", "Data points: {0}", pts.size()) + "\n");
    
    // Show continuous sweep status
    if (cfg.continuous_sweep_enabled) {
        if (cfg.last_measurement_duration_seconds > 0) {
            char buf[200];
            snprintf(buf, sizeof(buf), "Continuous sweep ACTIVE (measurement time: %.2f seconds)", cfg.last_measurement_duration_seconds);
            print(std::string(buf) + "\n");
        } else {
            print("Continuous sweep enabled but no timing info available\n");
        }
    }
    
    print(translation.get("ACOUSTIC_HELP", "Press H for help, ESC to go back.") + "\n");
    // Removed "Press any key to start" - user wants to be directly in the mode
    
    setUIContext("acoustic_analysis", {
        {" ", translation.get("ACOUSTIC_PLAY_PAUSE", "Play/Pause"), false},
        {"f", translation.get("ACOUSTIC_FREEZE", "Freeze"), false},
        {"s", translation.get("ACOUSTIC_STOP", "Stop"), false},
        {"t", translation.get("ACOUSTIC_TOGGLE_SMOOTH", "Toggle Smooth/Dotted"), false},
        {"+", translation.get("ACOUSTIC_SPEED_UP", "Increase Speed"), false},
        {"-", translation.get("ACOUSTIC_SPEED_DOWN", "Decrease Speed"), false},
        {"l", translation.get("ACOUSTIC_LOOP_LEFT", "Set Left Loop Marker"), false},
        {"r", translation.get("ACOUSTIC_LOOP_RIGHT", "Set Right Loop Marker"), false},
        {"o", translation.get("ACOUSTIC_LOOP_TOGGLE", "Toggle Loop"), false},
        {"z", translation.get("ACOUSTIC_LOOP_ZOOM", "Toggle Loop Zoom"), false},
        {"i", translation.get("ACOUSTIC_LOOP_INVERT", "Invert Loop"), false},
        {"c", translation.get("ACOUSTIC_CONTINUOUS", "Toggle Continuous Replay"), false},
        {"1", translation.get("ACOUSTIC_CURVE_SWR", "SWR Curve"), false},
        {"2", translation.get("ACOUSTIC_CURVE_RL", "Return Loss Curve"), false},
        {"3", translation.get("ACOUSTIC_CURVE_Z", "Impedance Curve"), false},
        {"4", translation.get("ACOUSTIC_CURVE_X", "Reactance Curve"), false},
        {"5", translation.get("ACOUSTIC_CURVE_PHASE", "Phase Curve"), false},
        {"a", translation.get("ACOUSTIC_AUDIO_CONFIG", "Audio Configuration"), false},
        {"y", translation.get("ACOUSTIC_Y_RULER", "Y-Axis Ruler"), false},
        {"x", translation.get("ACOUSTIC_X_RULER", "Toggle X-Axis Ruler"), false},
        {"n", translation.get("ACOUSTIC_STATUS", "Toggle Status Line"), false},
        {"v", translation.get("ACOUSTIC_SMITH", "Toggle Smith Diagram"), false},
        {"b", translation.get("ACOUSTIC_SMITH_MODE", "Change Smith Mode"), false},
        {"g", translation.get("ACOUSTIC_GOTO", "Go To Frequency"), false},
        {"m", translation.get("ACOUSTIC_MEASURE", "Show Measurement"), false},
        {"e", translation.get("ACOUSTIC_EXPORT", "Export Measurement"), false},
        {"h", translation.get("MENU_HELP", "(H)elp"), false}
    }, "navigation");
    
    print(getPromptWithDepth("ACOUSTIC_PROMPT", 2) + " ");
    
    // Prepare continuous sweep if enabled
    std::vector<MeasurementPoint> latestPts = pts;
    std::atomic<bool> continuousSweepRunning(false);
    std::atomic<bool> stopContinuousSweep(false);
    std::mutex dataMutex;
    std::thread continuousSweepThread;
    NanoVNAProtocol* backgroundProto = nullptr;  // Will be set if continuous sweep starts
    
    // Store original scan parameters so we can restore them when exiting freeze/loop mode
    uint64_t original_start_freq = cfg.start_freq;
    uint64_t original_end_freq = cfg.end_freq;
    uint64_t original_step = cfg.step;
    double original_measurement_duration = cfg.last_measurement_duration_seconds;  // Store original full-range timing
    
    // Lambda for continuous sweep background task
    //
    // DYNAMIC TIMING CALCULATION:
    // When the user enters freeze mode or loop mode, this task automatically:
    // 1. Detects the state transition
    // 2. Determines the new scan range (single point for freeze, loop range for loop)
    // 3. Calculates expected timing based on the ratio of points being measured
    // 4. Updates cfg.last_measurement_duration_seconds with the estimated timing
    // 5. Subsequent measurements use this timing for quicker acoustic feedback
    //
    // When exiting freeze/loop back to normal mode, the original full-range timing is restored.
    // This ensures measurements return to their original cadence when viewing the complete dataset.
    //
    // Note: Timing is estimated mathematically to avoid delays from calibration measurements.
    auto continuousSweepTask = [&]() {
        constexpr int FALLBACK_WAIT_SECONDS = 5;  // Wait time when no timing info available
        
        // Track previous state to detect transitions
        PlaybackState previousState = PlaybackState::STOPPED;
        bool previousLoopEnabled = false;
        
        while (!stopContinuousSweep) {
            if (cfg.last_measurement_duration_seconds > 0) {
                // Wait for the measurement duration before starting next measurement
                auto waitTime = std::chrono::duration<double>(cfg.last_measurement_duration_seconds);
                std::this_thread::sleep_for(waitTime);
                
                if (stopContinuousSweep) break;
                
                // Determine scan parameters based on analyzer state
                uint64_t scanStart = original_start_freq;
                uint64_t scanEnd = original_end_freq;
                uint64_t scanStep = original_step;
                bool useFrequencyMatching = false;  // Use frequency matching for freeze/loop to preserve dataset
                
                PlaybackState currentState = analyzer.getState();
                bool currentLoopEnabled = analyzer.isLoopEnabled();
                
                // Detect state transitions
                bool stateChanged = (currentState != previousState) || (currentLoopEnabled != previousLoopEnabled);
                
                // Check if we're entering a restricted mode (freeze or loop)
                if (stateChanged) {
                    // Determine if we're entering freeze mode
                    bool enteringFreezeMode = (currentState == PlaybackState::FROZEN && previousState != PlaybackState::FROZEN);
                    // Determine if we're entering loop mode
                    bool enteringLoopMode = (currentLoopEnabled && !previousLoopEnabled);
                    
                    // Determine if we're exiting freeze mode to normal (not loop)
                    bool exitingFreezeToNormal = (currentState != PlaybackState::FROZEN && 
                                                   previousState == PlaybackState::FROZEN && 
                                                   !currentLoopEnabled);
                    // Determine if we're exiting loop mode to normal (not freeze)
                    bool exitingLoopToNormal = (!currentLoopEnabled && 
                                                previousLoopEnabled && 
                                                currentState != PlaybackState::FROZEN);
                    
                    if (enteringFreezeMode || enteringLoopMode) {
                        // Entering freeze or loop mode - calculate timing based on point ratio
                        // No need for recalibration measurement - we can estimate timing mathematically
                        if (logger) {
                            logger->log("SWEEP", "State transition detected - entering restricted mode");
                        }
                    } else if (exitingFreezeToNormal || exitingLoopToNormal) {
                        // Exiting freeze/loop back to normal mode - restore original timing
                        cfg.last_measurement_duration_seconds = original_measurement_duration;
                        if (logger) {
                            std::ostringstream oss;
                            oss << "Exiting restricted mode - restored original timing: " 
                                << original_measurement_duration << " seconds";
                            logger->log("SWEEP", oss.str());
                        }
                    }
                    
                    // Update tracking variables
                    previousState = currentState;
                    previousLoopEnabled = currentLoopEnabled;
                }
                
                if (currentState == PlaybackState::FROZEN) {
                    // Freeze mode: measure only the current frozen point
                    size_t currentPos = analyzer.getPosition();
                    
                    // Safely access latestPts with mutex protection
                    {
                        std::lock_guard<std::mutex> lock(dataMutex);
                        if (currentPos < latestPts.size() && !latestPts.empty()) {
                            uint64_t freezeFreq = latestPts[currentPos].freq;
                            scanStart = freezeFreq;
                            scanEnd = freezeFreq;
                            scanStep = 1;  // Single point
                            useFrequencyMatching = true;  // Preserve full dataset, update only this point
                            
                            if (logger) {
                                logger->log("SWEEP", "Freeze mode: scanning single point at " + std::to_string(freezeFreq) + " Hz");
                            }
                        } else {
                            // Invalid position, fall back to normal mode
                            if (logger) {
                                logger->log("SWEEP", "Freeze mode: invalid position, falling back to full range scan");
                            }
                        }
                    }
                } else if (analyzer.isLoopEnabled()) {
                    // Loop mode: measure only points within loop boundaries
                    size_t loopLeft = analyzer.getLoopLeft();
                    size_t loopRight = analyzer.getLoopRight();
                    
                    // Safely access latestPts with mutex protection
                    {
                        std::lock_guard<std::mutex> lock(dataMutex);
                        
                        if (!latestPts.empty()) {
                            // Ensure valid loop markers
                            if (loopLeft >= latestPts.size()) loopLeft = 0;
                            if (loopRight >= latestPts.size()) loopRight = latestPts.size() - 1;
                            
                            // Handle inverted markers - swap them to maintain correct range
                            // This is intentional to allow flexible marker placement
                            if (loopLeft > loopRight) {
                                std::swap(loopLeft, loopRight);
                                if (logger) {
                                    logger->log("SWEEP", "Loop mode: loop markers were inverted, corrected to " + 
                                                std::to_string(loopLeft) + " - " + std::to_string(loopRight));
                                }
                            }
                            
                            // Get frequencies at loop boundaries
                            uint64_t loopStartFreq = latestPts[loopLeft].freq;
                            uint64_t loopEndFreq = latestPts[loopRight].freq;
                            
                            scanStart = loopStartFreq;
                            scanEnd = loopEndFreq;
                            // Keep the same step size to maintain measurement density
                            scanStep = original_step;
                            useFrequencyMatching = true;  // Preserve full dataset, update only loop points
                            
                            if (logger) {
                                size_t pointCount = loopRight - loopLeft + 1;
                                logger->log("SWEEP", "Loop mode: scanning " + std::to_string(pointCount) + 
                                            " points from " + std::to_string(loopStartFreq) + 
                                            " to " + std::to_string(loopEndFreq) + " Hz");
                            }
                        } else {
                            // Empty dataset, fall back to normal mode
                            if (logger) {
                                logger->log("SWEEP", "Loop mode: empty dataset, falling back to full range scan");
                            }
                        }
                    }
                } else {
                    // Normal mode: measure the full original range
                    scanStart = original_start_freq;
                    scanEnd = original_end_freq;
                    scanStep = original_step;
                    useFrequencyMatching = false;  // Replace entire dataset
                }
                
                // Calculate expected timing for restricted modes (freeze/loop) based on point ratio
                // This avoids performing a calibration measurement which causes delays
                if (currentState == PlaybackState::FROZEN || analyzer.isLoopEnabled()) {
                    // Validate frequency ranges, step values, and measurement duration to prevent errors
                    if (original_step > 0 && scanStep > 0 && 
                        original_end_freq >= original_start_freq && 
                        scanEnd >= scanStart &&
                        original_measurement_duration > 0) {
                        // Calculate number of points in the restricted range
                        size_t originalNumPoints = ((original_end_freq - original_start_freq) / original_step) + 1;
                        size_t restrictedNumPoints = ((scanEnd - scanStart) / scanStep) + 1;
                        
                        // Estimate timing based on the ratio of points
                        // Each point takes roughly the same time to measure
                        if (originalNumPoints > 0) {
                            double estimatedDuration = original_measurement_duration * 
                                                       (static_cast<double>(restrictedNumPoints) / static_cast<double>(originalNumPoints));
                            
                            // Ensure estimated duration is reasonable (at least 0.1 seconds)
                            if (estimatedDuration < 0.1) {
                                estimatedDuration = 0.1;
                            }
                            
                            // Update timing with the estimated duration
                            cfg.last_measurement_duration_seconds = estimatedDuration;
                            
                            if (logger) {
                                std::ostringstream oss;
                                oss << "Estimated timing for restricted mode: " << estimatedDuration 
                                    << " seconds (" << restrictedNumPoints << " points vs " 
                                    << originalNumPoints << " original points)";
                                logger->log("SWEEP", oss.str());
                            }
                        }
                    }
                }
                
                // Perform measurement with timing (suppress progress in acoustic mode)
                auto newPts = performMeasurementWithTiming(backgroundProto, scanStart, scanEnd, scanStep, true);
                
                if (!newPts.empty() && !stopContinuousSweep) {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    
                    if (useFrequencyMatching) {
                        // Freeze or loop mode: update only specific points, preserve full dataset
                        analyzer.updatePointsByFrequency(newPts);
                        // Update latestPts with merged data for next iteration
                        for (const auto& newPt : newPts) {
                            for (auto& existingPt : latestPts) {
                                if (existingPt.freq == newPt.freq) {
                                    existingPt = newPt;
                                    break;
                                }
                            }
                        }
                    } else {
                        // Normal mode: replace entire dataset
                        latestPts = newPts;
                        analyzer.updateData(latestPts);
                    }
                    
                    if (logger) {
                        logger->log("SWEEP", "Continuous sweep updated data (" + std::to_string(newPts.size()) + " points measured)");
                    }
                }
            } else {
                // No timing info, wait fallback time
                std::this_thread::sleep_for(std::chrono::seconds(FALLBACK_WAIT_SECONDS));
            }
        }
    };
    
    // Helper lambda for position display
    auto displayPosition = [&]() {
        if (analyzer.isStatusLineEnabled()) {
            // Display status line with more information
            print("\r" + analyzer.getStatusLineText() + "   ");
        } else {
            // Display simple position
            print("\r" + translation.format("ACOUSTIC_POSITION", "Position: {0} / {1}", 
                analyzer.getPosition(), analyzer.getDataSize() - 1) + "   ");
        }
    };
    
    // Helper lambda for jump width adjustment warning
    auto displayAdjustmentWarning = [&](int requestedWidth, int adjustedWidth) {
        print("\r" + translation.format("ACOUSTIC_JUMP_WIDTH_ADJUSTED", 
            "[Warning: Jump width {0} exceeds loop range. Adjusted to {1}]",
            requestedWidth, adjustedWidth) + "   " + "\n");
    };
    
    bool running = true;
    bool spaceWasPressed = false;
    
    // MIDI Controller integration: set up command queue for thread-safe event dispatch
    std::mutex midiCommandMutex;
    std::vector<MidiAppCommand> midiCommandQueue;
    std::mutex midiCCMutex;
    std::vector<std::pair<MidiCCFunction, int>> midiCCQueue;
    size_t lastMidiFeedbackPos = std::numeric_limits<size_t>::max();  // Track last motor fader position
    
    // Solo state tracking: save/restore curve enabled state on solo toggle
    bool soloActive[5] = {false, false, false, false, false};  // Which curves are currently solo'd
    bool preSoloEnabled[5] = {false, false, false, false, false};  // Curve states before any solo was activated
    bool anySoloActive = false;  // Whether any solo is currently active
    
    // Frozen fader positions (for snap-back in freeze mode)
    int frozenFaderValues[5] = {0, 0, 0, 0, 0};
    
    // Dynamic curve range tracking for proper 0-127 normalization
    struct CurveRange {
        double minVal = 0.0;
        double maxVal = 1.0;
        bool computed = false;
    };
    CurveRange curveRanges[5];
    
    // Helper: get curve value from a MeasurementPoint by curve index (0-4)
    auto getCurveValue = [](const MeasurementPoint& pt, int curveIndex) -> double {
        switch (curveIndex) {
            case 0: return pt.swr;
            case 1: return pt.rl;
            case 2: return pt.impedance_mag;
            case 3: return pt.X;
            case 4: return pt.phase_deg;
            default: return 0.0;
        }
    };
    
    // Helper: normalize a curve value to 0.0-1.0 using the dynamic range from the dataset
    // Uses the actual min/max of the data for relative mapping to 0-127
    auto normalizeCurveValue = [&](const MeasurementPoint& pt, int curveIndex) -> double {
        double val = getCurveValue(pt, curveIndex);
        // Use fixed ranges that match the physical meaning, but cover practical measurement ranges
        double minV, maxV;
        switch (curveIndex) {
            case 0: // SWR: 1.0 is perfect, higher is worse. Use log-like scale for better resolution
                minV = 1.0; maxV = 10.0;
                break;
            case 1: // RL: 0 dB is worst, -40 dB is very good (negative values)
                // Map so that -40 dB = full fader, 0 dB = zero fader (better match = higher fader)
                minV = -40.0; maxV = 0.0;
                break;
            case 2: // |Z|: 0 to practical maximum
                minV = 0.0; maxV = 500.0;
                break;
            case 3: // X (reactance): can be negative or positive
                minV = -250.0; maxV = 250.0;
                break;
            case 4: // Phase: -180 to +180 degrees
                minV = -180.0; maxV = 180.0;
                break;
            default:
                return 0.0;
        }
        
        // Override with dynamic range from actual data if we have it
        if (curveRanges[curveIndex].computed) {
            minV = curveRanges[curveIndex].minVal;
            maxV = curveRanges[curveIndex].maxVal;
        }
        
        if (maxV <= minV) return 0.5; // Avoid division by zero
        double norm = (val - minV) / (maxV - minV);
        return std::min(1.0, std::max(0.0, norm));
    };
    
    // Helper: send all 5 curve faders to a specific position (0 for reset, or computed values)
    auto sendAllFadersToZero = [&]() {
        if (!midiControllerMgr || !midiControllerMgr->isDeviceOpen() || !cfg.midi_controller_feedback) return;
        for (int i = 0; i < 5; i++) {
            midiControllerMgr->sendCurveValueFeedback(i, 0.0);
            frozenFaderValues[i] = 0;
        }
    };
    
    // Helper: send curve fader values based on current measurement point, respecting enabled/solo state
    auto sendCurveFaderValues = [&](const MeasurementPoint& pt) {
        if (!midiControllerMgr || !midiControllerMgr->isDeviceOpen() || !cfg.midi_controller_feedback) return;
        for (int i = 0; i < 5; i++) {
            bool isActive = analyzer.isCurveEnabled(i);
            if (isActive) {
                double norm = normalizeCurveValue(pt, i);
                midiControllerMgr->sendCurveValueFeedback(i, norm);
                frozenFaderValues[i] = MidiControllerManager::normalizedToMidi(norm);
            } else {
                midiControllerMgr->sendCurveValueFeedback(i, 0.0);
                frozenFaderValues[i] = 0;
            }
        }
    };
    
    // Set up MIDI controller if enabled
    if (midiControllerMgr && cfg.midi_controller_enabled) {
        // Load mapping preset if configured
        if (!cfg.midi_controller_preset.empty()) {
            std::string presetPath = "midi/" + cfg.midi_controller_preset;
            if (midiControllerMgr->loadMappingsFromFile(presetPath)) {
                if (logger) logger->log("MIDI_CTRL", "Loaded MIDI controller preset: " + presetPath);
            } else {
                if (logger) logger->log("MIDI_CTRL", "Failed to load preset: " + presetPath);
            }
        }
        
        // Open the MIDI controller device
        if (cfg.midi_controller_device_id >= 0) {
            if (midiControllerMgr->openDevice(cfg.midi_controller_device_id)) {
                if (logger) logger->log("MIDI_CTRL", "MIDI controller opened: " + midiControllerMgr->getDeviceName());
                print(translation.format("MIDI_CTRL_CONNECTED", "[MIDI Controller: {0}]", midiControllerMgr->getDeviceName()) + "\n");
            } else {
                if (logger) logger->log("MIDI_CTRL", "Failed to open MIDI controller device ID " + std::to_string(cfg.midi_controller_device_id));
            }
        }
        
        // Set command callback: push commands to thread-safe queue
        midiControllerMgr->setCommandCallback([&midiCommandMutex, &midiCommandQueue](MidiAppCommand cmd) {
            std::lock_guard<std::mutex> lock(midiCommandMutex);
            midiCommandQueue.push_back(cmd);
        });
        
        // Set CC value callback: push CC events to thread-safe queue
        midiControllerMgr->setCCValueCallback([&midiCCMutex, &midiCCQueue](MidiCCFunction func, int value) {
            std::lock_guard<std::mutex> lock(midiCCMutex);
            midiCCQueue.push_back({func, value});
        });
    }
    
    // Compute dynamic curve ranges from measurement data for better 0-127 normalization
    {
        size_t dataSize = pts.size();
        if (dataSize > 0) {
            for (int c = 0; c < 5; c++) {
                double minV = std::numeric_limits<double>::max();
                double maxV = std::numeric_limits<double>::lowest();
                for (size_t i = 0; i < dataSize; i++) {
                    double val = getCurveValue(pts[i], c);
                    if (val < minV) minV = val;
                    if (val > maxV) maxV = val;
                }
                // Add a small margin to avoid values sitting exactly at 0 or 127
                double margin = (maxV - minV) * 0.02;
                if (margin < 0.001) margin = 0.001;
                curveRanges[c].minVal = minV - margin;
                curveRanges[c].maxVal = maxV + margin;
                curveRanges[c].computed = true;
                if (logger) logger->log("MIDI_CTRL", "Curve " + std::to_string(c) + " range: " + 
                    std::to_string(minV) + " - " + std::to_string(maxV));
            }
        }
    }
    
    // Send all curve faders to zero on initial entry
    sendAllFadersToZero();
    
    // Lambda to re-initialize MIDI controller (called after config changes)
    auto reinitMidiController = [&]() {
        // Close existing connection if open
        if (midiControllerMgr && midiControllerMgr->isDeviceOpen()) {
            midiControllerMgr->closeDevice();
        }
        
        // Re-open if enabled
        if (midiControllerMgr && cfg.midi_controller_enabled) {
            if (!cfg.midi_controller_preset.empty()) {
                std::string presetPath = "midi/" + cfg.midi_controller_preset;
                if (midiControllerMgr->loadMappingsFromFile(presetPath)) {
                    if (logger) logger->log("MIDI_CTRL", "Reloaded MIDI controller preset: " + presetPath);
                }
            }
            
            if (cfg.midi_controller_device_id >= 0) {
                if (midiControllerMgr->openDevice(cfg.midi_controller_device_id)) {
                    if (logger) logger->log("MIDI_CTRL", "MIDI controller reopened: " + midiControllerMgr->getDeviceName());
                    print(translation.format("MIDI_CTRL_CONNECTED", "[MIDI Controller: {0}]", midiControllerMgr->getDeviceName()) + "\n");
                }
            }
            
            // Re-set callbacks
            midiControllerMgr->setCommandCallback([&midiCommandMutex, &midiCommandQueue](MidiAppCommand cmd) {
                std::lock_guard<std::mutex> lock(midiCommandMutex);
                midiCommandQueue.push_back(cmd);
            });
            midiControllerMgr->setCCValueCallback([&midiCCMutex, &midiCCQueue](MidiCCFunction func, int value) {
                std::lock_guard<std::mutex> lock(midiCCMutex);
                midiCCQueue.push_back({func, value});
            });
            
            // Reset faders to 0 after reconnection
            sendAllFadersToZero();
        }
    };
    
    // Freeze-by-touch state: tracks which faders are currently touched
    bool faderTouched[5] = {false, false, false, false, false};
    bool freezeByTouchActive = false;  // True when at least one fader is touched
    
    // Lambda to process MIDI app commands (same actions as keyboard)
    auto processMidiCommand = [&](MidiAppCommand cmd) {
        if (logger) logger->log("MIDI_CTRL", "Processing command: " + MidiControllerManager::getCommandName(cmd));
        
        switch (cmd) {
            case MidiAppCommand::PLAY_PAUSE:
                // Reset freeze-by-touch state on manual play/pause
                freezeByTouchActive = false;
                for (int i = 0; i < 5; i++) faderTouched[i] = false;
                if (analyzer.getState() == PlaybackState::PLAYING) {
                    analyzer.pause();
                    print("\n" + translation.get("ACOUSTIC_PAUSED", "[PAUSED]") + "\n");
                } else {
                    analyzer.play();
                    print("\n" + translation.get("ACOUSTIC_PLAYING", "[PLAYING]") + "\n");
                }
                break;
            case MidiAppCommand::STOP:
                // Reset freeze-by-touch state on stop
                freezeByTouchActive = false;
                for (int i = 0; i < 5; i++) faderTouched[i] = false;
                analyzer.stopYAxisRuler();
                analyzer.stop();
                print("\n" + translation.get("ACOUSTIC_STOPPED", "[STOPPED and reset to start]") + "\n");
                sendAllFadersToZero();
                break;
            case MidiAppCommand::FREEZE:
                analyzer.freeze();
                print("\n" + translation.format("ACOUSTIC_FROZEN", "[FROZEN at position {0}]", analyzer.getPosition()) + "\n");
                // Capture current fader values for freeze snap-back
                {
                    const MeasurementPoint* freezePt = analyzer.getCurrentMeasurement();
                    if (freezePt) {
                        for (int i = 0; i < 5; i++) {
                            if (analyzer.isCurveEnabled(i)) {
                                double norm = normalizeCurveValue(*freezePt, i);
                                frozenFaderValues[i] = MidiControllerManager::normalizedToMidi(norm);
                            } else {
                                frozenFaderValues[i] = 0;
                            }
                        }
                    }
                }
                break;
            case MidiAppCommand::TOGGLE_SMOOTH_DOTTED:
                analyzer.setSmoothMode(!analyzer.isSmoothMode());
                print("\n" + (analyzer.isSmoothMode() ? 
                        translation.get("ACOUSTIC_MODE_SMOOTH", "[Playback mode: SMOOTH]") : 
                        translation.get("ACOUSTIC_MODE_DOTTED", "[Playback mode: DOTTED]")) + "\n");
                cfg.acoustic_smooth_mode = analyzer.isSmoothMode();
                saveSettings();
                break;
            case MidiAppCommand::TOGGLE_LOOP:
                analyzer.toggleLoop();
                print("\n" + (analyzer.isLoopEnabled() ? 
                        translation.format("ACOUSTIC_LOOP_ENABLED", "[Loop ENABLED ({0} - {1})]", analyzer.getLoopLeft(), analyzer.getLoopRight()) :
                        translation.format("ACOUSTIC_LOOP_DISABLED", "[Loop DISABLED ({0} - {1})]", analyzer.getLoopLeft(), analyzer.getLoopRight())) + "\n");
                break;
            case MidiAppCommand::TOGGLE_LOOP_ZOOM:
                analyzer.toggleLoopZoom();
                break;
            case MidiAppCommand::TOGGLE_LOOP_INVERT:
                analyzer.toggleLoopInvert();
                break;
            case MidiAppCommand::TOGGLE_CONTINUOUS:
                analyzer.toggleContinuousReplay();
                break;
            case MidiAppCommand::SET_LOOP_LEFT:
                analyzer.setLoopLeft(analyzer.getPosition());
                print("\n" + translation.format("ACOUSTIC_LEFT_MARKER", "[Left marker set at {0}]", analyzer.getPosition()) + "\n");
                break;
            case MidiAppCommand::SET_LOOP_RIGHT:
                analyzer.setLoopRight(analyzer.getPosition());
                print("\n" + translation.format("ACOUSTIC_RIGHT_MARKER", "[Right marker set at {0}]", analyzer.getPosition()) + "\n");
                break;
            case MidiAppCommand::MOVE_LEFT:
                analyzer.movePositionWithBoundaryCheck(-cfg.navigation_jump_width);
                displayPosition();
                break;
            case MidiAppCommand::MOVE_RIGHT:
                analyzer.movePositionWithBoundaryCheck(cfg.navigation_jump_width);
                displayPosition();
                break;
            case MidiAppCommand::JUMP_WIDTH_UP:
                if (cfg.navigation_jump_width == 1) cfg.navigation_jump_width = 10;
                else if (cfg.navigation_jump_width == 10) cfg.navigation_jump_width = 100;
                else if (cfg.navigation_jump_width == 100) cfg.navigation_jump_width = 500;
                else if (cfg.navigation_jump_width == 500) cfg.navigation_jump_width = 1000;
                print("\r" + translation.format("ACOUSTIC_JUMP_WIDTH", "[Jump width: {0}]", cfg.navigation_jump_width) + "   ");
                saveSettings();
                break;
            case MidiAppCommand::JUMP_WIDTH_DOWN:
                if (cfg.navigation_jump_width == 1000) cfg.navigation_jump_width = 500;
                else if (cfg.navigation_jump_width == 500) cfg.navigation_jump_width = 100;
                else if (cfg.navigation_jump_width == 100) cfg.navigation_jump_width = 10;
                else if (cfg.navigation_jump_width == 10) cfg.navigation_jump_width = 1;
                print("\r" + translation.format("ACOUSTIC_JUMP_WIDTH", "[Jump width: {0}]", cfg.navigation_jump_width) + "   ");
                saveSettings();
                break;
            case MidiAppCommand::SPEED_UP:
                analyzer.setPlaybackTimeSeconds(analyzer.getPlaybackTimeSeconds() + 1);
                cfg.acoustic_time_seconds = analyzer.getPlaybackTimeSeconds();
                saveSettings();
                break;
            case MidiAppCommand::SPEED_DOWN:
                analyzer.setPlaybackTimeSeconds(analyzer.getPlaybackTimeSeconds() - 1);
                cfg.acoustic_time_seconds = analyzer.getPlaybackTimeSeconds();
                saveSettings();
                break;
            case MidiAppCommand::TOGGLE_CURVE_1:
            case MidiAppCommand::TOGGLE_CURVE_2:
            case MidiAppCommand::TOGGLE_CURVE_3:
            case MidiAppCommand::TOGGLE_CURVE_4:
            case MidiAppCommand::TOGGLE_CURVE_5:
                {
                    int idx = static_cast<int>(cmd) - static_cast<int>(MidiAppCommand::TOGGLE_CURVE_1);
                    analyzer.toggleCurve(idx);
                    // Display message like keyboard handler (same mechanism as number keys 1-5)
                    std::string curveName = analyzer.getCurveName(idx);
                    bool isEnabled = analyzer.isCurveEnabled(idx);
                    std::string statusKey = isEnabled ? "ACOUSTIC_CURVE_ON" : "ACOUSTIC_CURVE_OFF";
                    std::string statusFallback = isEnabled ? "[{0} ON]" : "[{0} OFF]";
                    print("\n" + translation.format(statusKey, statusFallback, curveName) + "\n");
                    cfg.curve_enabled[idx] = isEnabled;
                    saveSettings();
                    // Send fader to 0 for disabled curves
                    if (!isEnabled && midiControllerMgr && midiControllerMgr->isDeviceOpen() && cfg.midi_controller_feedback) {
                        midiControllerMgr->sendCurveValueFeedback(idx, 0.0);
                    }
                }
                break;
            case MidiAppCommand::MUTE_CURVE_1:
            case MidiAppCommand::MUTE_CURVE_2:
            case MidiAppCommand::MUTE_CURVE_3:
            case MidiAppCommand::MUTE_CURVE_4:
            case MidiAppCommand::MUTE_CURVE_5:
                {
                    int idx = static_cast<int>(cmd) - static_cast<int>(MidiAppCommand::MUTE_CURVE_1);
                    // Mute = set volume to 0 if non-zero, restore to default if zero
                    int vol = analyzer.getCurveVolume(idx);
                    if (vol > 0) {
                        analyzer.setCurveVolume(idx, 0);
                    } else {
                        analyzer.setCurveVolume(idx, 100);
                    }
                    print("\n" + translation.format("ACOUSTIC_CURVE_VOLUME", "[Curve {0} volume: {1}%]", idx + 1, analyzer.getCurveVolume(idx)) + "\n");
                }
                break;
            case MidiAppCommand::SOLO_CURVE_1:
            case MidiAppCommand::SOLO_CURVE_2:
            case MidiAppCommand::SOLO_CURVE_3:
            case MidiAppCommand::SOLO_CURVE_4:
            case MidiAppCommand::SOLO_CURVE_5:
                {
                    int soloIdx = static_cast<int>(cmd) - static_cast<int>(MidiAppCommand::SOLO_CURVE_1);
                    
                    if (soloActive[soloIdx]) {
                        // Un-solo this curve: deactivate solo for this curve
                        soloActive[soloIdx] = false;
                        
                        // Check if any other solos are still active
                        anySoloActive = false;
                        for (int i = 0; i < 5; i++) {
                            if (soloActive[i]) { anySoloActive = true; break; }
                        }
                        
                        if (!anySoloActive) {
                            // No more solos active: restore pre-solo state for all curves
                            for (int i = 0; i < 5; i++) {
                                bool shouldBeEnabled = preSoloEnabled[i];
                                if (analyzer.isCurveEnabled(i) != shouldBeEnabled) {
                                    analyzer.toggleCurve(i);
                                }
                                cfg.curve_enabled[i] = analyzer.isCurveEnabled(i);
                            }
                            print("\n" + translation.get("ACOUSTIC_SOLO_OFF", "[Solo OFF - previous state restored]") + "\n");
                        } else {
                            // Other solos still active: disable only this un-solo'd curve (unless it was enabled pre-solo)
                            // In multi-solo mode, only solo'd curves should be enabled
                            bool shouldBeEnabled = false;
                            for (int i = 0; i < 5; i++) {
                                shouldBeEnabled = soloActive[i];
                                if (analyzer.isCurveEnabled(i) != shouldBeEnabled) {
                                    analyzer.toggleCurve(i);
                                }
                                cfg.curve_enabled[i] = analyzer.isCurveEnabled(i);
                            }
                            std::string curveName = analyzer.getCurveName(soloIdx);
                            print("\n" + translation.format("ACOUSTIC_CURVE_UNSOLO", "[{0} unsoloed]", curveName) + "\n");
                            // Send fader to 0 for the un-solo'd curve
                            if (midiControllerMgr && midiControllerMgr->isDeviceOpen() && cfg.midi_controller_feedback) {
                                midiControllerMgr->sendCurveValueFeedback(soloIdx, 0.0);
                            }
                        }
                    } else {
                        // Activate solo for this curve
                        if (!anySoloActive) {
                            // First solo: save current state of all curves
                            for (int i = 0; i < 5; i++) {
                                preSoloEnabled[i] = analyzer.isCurveEnabled(i);
                            }
                        }
                        soloActive[soloIdx] = true;
                        anySoloActive = true;
                        
                        // Enable solo'd curves, disable others (multi-solo support)
                        for (int i = 0; i < 5; i++) {
                            bool shouldBeEnabled = soloActive[i];
                            if (analyzer.isCurveEnabled(i) != shouldBeEnabled) {
                                analyzer.toggleCurve(i);
                            }
                            cfg.curve_enabled[i] = analyzer.isCurveEnabled(i);
                            // Send fader to 0 for disabled curves
                            if (!analyzer.isCurveEnabled(i) && midiControllerMgr && midiControllerMgr->isDeviceOpen() && cfg.midi_controller_feedback) {
                                midiControllerMgr->sendCurveValueFeedback(i, 0.0);
                            }
                        }
                        print("\n" + translation.format("ACOUSTIC_CURVE_SOLO", "[Solo: {0}]", analyzer.getCurveName(soloIdx)) + "\n");
                    }
                    saveSettings();
                }
                break;
            case MidiAppCommand::SHOW_MEASUREMENT:
                {
                    const MeasurementPoint* pt = analyzer.getCurrentMeasurement();
                    if (pt) {
                        print(translation.format("ACOUSTIC_FREQUENCY", "Frequency: {0} Hz", pt->freq) + "\n");
                        print(translation.format("ACOUSTIC_SWR", "SWR: {0}", pt->swr) + "\n");
                    }
                }
                break;
            case MidiAppCommand::TOGGLE_STATUS_LINE:
                analyzer.toggleStatusLine();
                break;
            case MidiAppCommand::TOGGLE_X_RULER:
                analyzer.toggleXAxisRuler();
                break;
            case MidiAppCommand::ANNOUNCE_CURVE_VALUE_1:
            case MidiAppCommand::ANNOUNCE_CURVE_VALUE_2:
            case MidiAppCommand::ANNOUNCE_CURVE_VALUE_3:
            case MidiAppCommand::ANNOUNCE_CURVE_VALUE_4:
            case MidiAppCommand::ANNOUNCE_CURVE_VALUE_5:
                {
                    int curveIdx = static_cast<int>(cmd) - static_cast<int>(MidiAppCommand::ANNOUNCE_CURVE_VALUE_1);
                    const MeasurementPoint* pt = analyzer.getCurrentMeasurement();
                    if (pt) {
                        std::string curveName = analyzer.getCurveName(curveIdx);
                        std::string valueStr = "N/A";
                        switch (curveIdx) {
                            case 0: valueStr = std::to_string(pt->swr); break;
                            case 1: valueStr = std::to_string(pt->rl) + " dB"; break;
                            case 2: valueStr = std::to_string(pt->impedance_mag) + " Ohm"; break;
                            case 3: valueStr = std::to_string(pt->X) + " Ohm"; break;
                            case 4: valueStr = std::to_string(pt->phase_deg) + "°"; break;
                            default: break;
                        }
                        print("\n" + translation.format("ACOUSTIC_CURVE_VALUE_ANNOUNCE", 
                            "[{0}: {1} at {2} Hz]", curveName, valueStr, pt->freq) + "\n");
                    } else {
                        print("\n" + translation.get("ACOUSTIC_NO_DATA", "[No measurement data at current position]") + "\n");
                    }
                }
                break;
            case MidiAppCommand::ANNOUNCE_MASTER_VOLUME:
                {
                    int vol = analyzer.getMasterVolume();
                    print("\n" + translation.format("ACOUSTIC_MASTER_VOLUME_ANNOUNCE", "[Master volume: {0}%]", vol) + "\n");
                }
                break;
            default:
                break;
        }
        
        // Send motor fader feedback after command processing
        if (midiControllerMgr && midiControllerMgr->isDeviceOpen() && cfg.midi_controller_feedback) {
            // Update position feedback
            size_t dataSize = analyzer.getDataSize();
            if (dataSize > 1) {
                double normalizedPos = static_cast<double>(analyzer.getPosition()) / (dataSize - 1);
                midiControllerMgr->sendPositionFeedback(normalizedPos);
            }
            // Update master volume feedback
            midiControllerMgr->sendMasterVolumeFeedback(analyzer.getMasterVolume());
        }
    };
    
    // Track last volume values to suppress redundant min/max boundary messages
    bool masterVolumeAtMin = false, masterVolumeAtMax = false;
    bool curveVolumeAtMin[5] = {false, false, false, false, false};
    bool curveVolumeAtMax[5] = {false, false, false, false, false};
    
    // Debouncing state for volume CC: only apply after fader stops moving
    // This prevents blocking when a motor fader sends every intermediate value
    static constexpr int CC_DEBOUNCE_MS = 50;  // Wait 50ms of inactivity before applying
    int pendingMasterVolume = -1;  // -1 = no pending change
    int pendingCurveVolume[5] = {-1, -1, -1, -1, -1};
    std::chrono::steady_clock::time_point masterVolumeLastChange;
    std::chrono::steady_clock::time_point curveVolumeLastChange[5];
    
    // Lambda to check and apply debounced volume values
    auto applyDebouncedVolumes = [&]() {
        auto now = std::chrono::steady_clock::now();
        
        // Check master volume
        if (pendingMasterVolume >= 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - masterVolumeLastChange).count();
            if (elapsed >= CC_DEBOUNCE_MS) {
                int vol = pendingMasterVolume;
                pendingMasterVolume = -1;
                analyzer.setMasterVolume(vol);
                cfg.master_volume = vol;
                print("\r" + translation.format("ACOUSTIC_MASTER_VOLUME", "[Master volume: {0}%]", vol) + "   ");
                if (midiControllerMgr && midiControllerMgr->isDeviceOpen() && cfg.midi_controller_feedback) {
                    midiControllerMgr->sendMasterVolumeFeedback(vol);
                }
            }
        }
        
        // Check curve volumes
        for (int i = 0; i < 5; i++) {
            if (pendingCurveVolume[i] >= 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - curveVolumeLastChange[i]).count();
                if (elapsed >= CC_DEBOUNCE_MS) {
                    int vol = pendingCurveVolume[i];
                    pendingCurveVolume[i] = -1;
                    std::string curveName = analyzer.getCurveName(i);
                    analyzer.setCurveVolume(i, vol);
                    if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
                        cfg.curve_volume_synth[i] = vol;
                    } else {
                        cfg.curve_volume_midi[i] = vol;
                    }
                    print("\r" + translation.format("ACOUSTIC_CURVE_VOLUME", "[{0} volume: {1}%]", curveName, vol) + "   ");
                }
            }
        }
    };
    
    // Lambda to process MIDI CC value changes
    auto processMidiCC = [&](MidiCCFunction func, int midiValue) {
        if (logger) logger->log("MIDI_CTRL", "Processing CC: " + MidiControllerManager::getCCFunctionName(func) + " = " + std::to_string(midiValue));
        
        switch (func) {
            case MidiCCFunction::MASTER_VOLUME:
                {
                    int vol = MidiControllerManager::midiToPercent(midiValue, 100);
                    int currentVol = analyzer.getMasterVolume();
                    
                    // Suppress messages when turning beyond min/max
                    if (vol == currentVol && (midiValue == 0 || midiValue == 127)) {
                        if (midiValue == 0 && !masterVolumeAtMin) {
                            masterVolumeAtMin = true;
                            print("\r" + translation.get("ACOUSTIC_VOLUME_AT_MINIMUM", "[Master volume: minimum reached]") + "   ");
                        } else if (midiValue == 127 && !masterVolumeAtMax) {
                            masterVolumeAtMax = true;
                            print("\r" + translation.get("ACOUSTIC_VOLUME_AT_MAXIMUM", "[Master volume: maximum reached]") + "   ");
                        }
                        break;
                    }
                    masterVolumeAtMin = false;
                    masterVolumeAtMax = false;
                    
                    // Debounce: store pending value, apply after inactivity
                    pendingMasterVolume = vol;
                    masterVolumeLastChange = std::chrono::steady_clock::now();
                }
                break;
            case MidiCCFunction::CURVE_VOLUME_1:
            case MidiCCFunction::CURVE_VOLUME_2:
            case MidiCCFunction::CURVE_VOLUME_3:
            case MidiCCFunction::CURVE_VOLUME_4:
            case MidiCCFunction::CURVE_VOLUME_5:
                {
                    int idx = static_cast<int>(func) - static_cast<int>(MidiCCFunction::CURVE_VOLUME_1);
                    int vol = MidiControllerManager::midiToPercent(midiValue, 200); // Curve vol 0-200%
                    int currentVol = analyzer.getCurveVolume(idx);
                    std::string curveName = analyzer.getCurveName(idx);
                    
                    // Suppress messages when turning beyond min/max
                    if (vol == currentVol && (midiValue == 0 || midiValue == 127)) {
                        if (midiValue == 0 && !curveVolumeAtMin[idx]) {
                            curveVolumeAtMin[idx] = true;
                            print("\r" + translation.format("ACOUSTIC_CURVE_VOLUME_AT_MINIMUM", 
                                "[{0} volume: minimum reached]", curveName) + "   ");
                        } else if (midiValue == 127 && !curveVolumeAtMax[idx]) {
                            curveVolumeAtMax[idx] = true;
                            print("\r" + translation.format("ACOUSTIC_CURVE_VOLUME_AT_MAXIMUM", 
                                "[{0} volume: maximum reached]", curveName) + "   ");
                        }
                        break;
                    }
                    curveVolumeAtMin[idx] = false;
                    curveVolumeAtMax[idx] = false;
                    
                    // Debounce: store pending value, apply after inactivity
                    pendingCurveVolume[idx] = vol;
                    curveVolumeLastChange[idx] = std::chrono::steady_clock::now();
                }
                break;
            case MidiCCFunction::FADER_TOUCH_1:
            case MidiCCFunction::FADER_TOUCH_2:
            case MidiCCFunction::FADER_TOUCH_3:
            case MidiCCFunction::FADER_TOUCH_4:
            case MidiCCFunction::FADER_TOUCH_5:
                {
                    if (!cfg.midi_controller_freeze_by_touch) break;
                    
                    int idx = static_cast<int>(func) - static_cast<int>(MidiCCFunction::FADER_TOUCH_1);
                    bool touched = (midiValue >= 64);  // 127 = touched, 0 = released
                    faderTouched[idx] = touched;
                    
                    if (logger) logger->log("MIDI_CTRL", "Fader " + std::to_string(idx + 1) + 
                        (touched ? " touched" : " released"));
                    
                    // Check if any fader is still touched
                    bool anyTouched = false;
                    for (int i = 0; i < 5; i++) {
                        if (faderTouched[i]) { anyTouched = true; break; }
                    }
                    
                    if (anyTouched && !freezeByTouchActive) {
                        // Activate freeze-by-touch silently
                        freezeByTouchActive = true;
                        if (analyzer.getState() == PlaybackState::PLAYING) {
                            analyzer.freeze();
                        }
                    } else if (!anyTouched && freezeByTouchActive) {
                        // All faders released - deactivate freeze-by-touch silently
                        freezeByTouchActive = false;
                        if (analyzer.getState() == PlaybackState::FROZEN) {
                            analyzer.play();
                        }
                    }
                }
                break;
            default:
                // Other CC functions (position/curve value readout) are output-only
                break;
        }
    };
    
    while (running) {
        int ch = 0;
        bool hasKeyboardInput = false;
        bool hasWebInput = false;
        
        // Check for web interface input first
        if (webServer && webServer->isRunning() && webServer->hasInput()) {
            std::string webInput = webServer->readInput();
            if (!webInput.empty()) {
                if (logger) logger->log("UI_ACOUSTIC", "Web input received: [" + webInput + "]");
                
                // Handle web input - treat escape sequences and regular keys
                if (webInput[0] == '\x1B' && webInput.length() >= 3 && webInput[1] == '[') {
                    // Arrow keys (ESC [ X format)
                    if (webInput[2] == 'A') ch = 72;  // Up arrow
                    else if (webInput[2] == 'B') ch = 80;  // Down arrow
                    else if (webInput[2] == 'D') ch = 75;  // Left arrow
                    else if (webInput[2] == 'C') ch = 77;  // Right arrow
                    
                    if (ch != 0) {
                        hasWebInput = true;
                    }
                } else {
                    // Regular character (including standalone ESC key)
                    ch = static_cast<unsigned char>(webInput[0]);
                    hasWebInput = true;
                }
                
                if (logger) logger->log("UI_ACOUSTIC", "Web input mapped to ch: " + std::to_string(ch));
            }
        }
        
        // Check for keyboard input if no web input
        if (!hasWebInput && consoleInput->kbhit()) {
            ch = consoleInput->getKey();  // Use getKey() to handle platform-specific extended keys
            hasKeyboardInput = true;
        }
        
        // If we have input, process it
        if (hasKeyboardInput || hasWebInput) {
            
            // Handle arrow keys
            // For web input: ch is set to Windows-style codes (72, 75, 77, 80)
            // For keyboard input: ch is set to LogicalKey codes (KEY_UP, KEY_DOWN, etc.) by getKey()
            // We need to handle both for backward compatibility
            if (ch == 75 || ch == KEY_LEFT) {  // Left arrow
                int adjustedDelta = analyzer.movePositionWithBoundaryCheck(-cfg.navigation_jump_width);
                if (std::abs(adjustedDelta) != cfg.navigation_jump_width && adjustedDelta != 0) {
                    displayAdjustmentWarning(cfg.navigation_jump_width, std::abs(adjustedDelta));
                }
                displayPosition();
            } else if (ch == 77 || ch == KEY_RIGHT) {  // Right arrow
                int adjustedDelta = analyzer.movePositionWithBoundaryCheck(cfg.navigation_jump_width);
                if (std::abs(adjustedDelta) != cfg.navigation_jump_width && adjustedDelta != 0) {
                    displayAdjustmentWarning(cfg.navigation_jump_width, std::abs(adjustedDelta));
                }
                displayPosition();
            } else if (ch == 72 || ch == KEY_UP) {  // Up arrow - increase jump width
                if (cfg.navigation_jump_width == 1) cfg.navigation_jump_width = 10;
                else if (cfg.navigation_jump_width == 10) cfg.navigation_jump_width = 100;
                else if (cfg.navigation_jump_width == 100) cfg.navigation_jump_width = 500;
                else if (cfg.navigation_jump_width == 500) cfg.navigation_jump_width = 1000;
                // else stay at 1000 (non-rotating)
                
                // Display jump width and position on same line
                print("\r" + translation.format("ACOUSTIC_JUMP_WIDTH", "[Jump width: {0}]", cfg.navigation_jump_width) + " - " + translation.format("ACOUSTIC_POSITION", "Position: {0} / {1}", 
                              analyzer.getPosition(), analyzer.getDataSize() - 1) + "   ");
                saveSettings();
            } else if (ch == 80 || ch == KEY_DOWN) {  // Down arrow - decrease jump width
                if (cfg.navigation_jump_width == 1000) cfg.navigation_jump_width = 500;
                else if (cfg.navigation_jump_width == 500) cfg.navigation_jump_width = 100;
                else if (cfg.navigation_jump_width == 100) cfg.navigation_jump_width = 10;
                else if (cfg.navigation_jump_width == 10) cfg.navigation_jump_width = 1;
                // else stay at 1 (non-rotating)
                
                // Display jump width and position on same line
                print("\r" + translation.format("ACOUSTIC_JUMP_WIDTH", "[Jump width: {0}]", cfg.navigation_jump_width) + " - " + translation.format("ACOUSTIC_POSITION", "Position: {0} / {1}", 
                              analyzer.getPosition(), analyzer.getDataSize() - 1) + "   ");
                saveSettings();
            } else {
                // Regular key
                char key = static_cast<char>(ch);
                
                // Check for modifier keys (Windows only)
                // TODO: Implement cross-platform modifier key detection for volume control shortcuts
                // on macOS/Linux (e.g., using ncurses or checking modifier sequences)
                bool ctrlPressed = false;
                bool shiftPressed = false;
#if defined(_WIN32)
                ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#endif
                
                // Convert uppercase to lowercase for letter keys
                if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
                
                // Handle digit keys with modifiers for volume control
                if (key >= '1' && key <= '5') {
                    int curveIndex = key - '1';
                    if (ctrlPressed) {
                        // Decrease volume
                        int vol = analyzer.getCurveVolume(curveIndex) - 10;
                        analyzer.setCurveVolume(curveIndex, vol);
                        print("\n" + translation.format("ACOUSTIC_CURVE_VOLUME", "[Curve {0} volume: {1}%]", curveIndex + 1, analyzer.getCurveVolume(curveIndex)) + "\n");
                        // Save to correct volume array based on audio engine
                        if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
                            cfg.curve_volume_synth[curveIndex] = analyzer.getCurveVolume(curveIndex);
                        } else {
                            cfg.curve_volume_midi[curveIndex] = analyzer.getCurveVolume(curveIndex);
                        }
                        saveSettings();
                    } else if (shiftPressed) {
                        // Increase volume
                        int vol = analyzer.getCurveVolume(curveIndex) + 10;
                        analyzer.setCurveVolume(curveIndex, vol);
                        print("\n" + translation.format("ACOUSTIC_CURVE_VOLUME", "[Curve {0} volume: {1}%]", curveIndex + 1, analyzer.getCurveVolume(curveIndex)) + "\n");
                        // Save to correct volume array based on audio engine
                        if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
                            cfg.curve_volume_synth[curveIndex] = analyzer.getCurveVolume(curveIndex);
                        } else {
                            cfg.curve_volume_midi[curveIndex] = analyzer.getCurveVolume(curveIndex);
                        }
                        saveSettings();
                    } else {
                        // Toggle curve
                        analyzer.toggleCurve(curveIndex);
                        std::string curveName = analyzer.getCurveName(curveIndex);
                        bool isEnabled = analyzer.isCurveEnabled(curveIndex);
                        std::string statusKey = isEnabled ? "ACOUSTIC_CURVE_ON" : "ACOUSTIC_CURVE_OFF";
                        std::string statusFallback = isEnabled ? "[{0} ON]" : "[{0} OFF]";
                        print("\n" + translation.format(statusKey, statusFallback, curveName) + "\n");
                        cfg.curve_enabled[curveIndex] = isEnabled;
                        saveSettings();
                    }
                    continue;  // Skip the switch statement
                }
                
                switch (key) {
                    case ' ':  // Space - Play/Pause toggle
                        if (!spaceWasPressed) {
                            if (analyzer.getState() == PlaybackState::PLAYING) {
                                analyzer.pause();
                                print("\n" + translation.get("ACOUSTIC_PAUSED", "[PAUSED]") + "\n");
                                
                                // Stop continuous sweep when pausing
                                if (continuousSweepRunning) {
                                    stopContinuousSweep = true;
                                    if (continuousSweepThread.joinable()) {
                                        continuousSweepThread.join();
                                    }
                                    continuousSweepRunning = false;
                                }
                            } else {
                                analyzer.play();
                                print("\n" + translation.get("ACOUSTIC_PLAYING", "[PLAYING]") + "\n");
                                
                                // Start continuous sweep if enabled
                                if (cfg.continuous_sweep_enabled && cfg.last_measurement_duration_seconds > 0 && !continuousSweepRunning) {
                                    backgroundProto = proto;
                                    stopContinuousSweep = false;
                                    continuousSweepThread = std::thread(continuousSweepTask);
                                    continuousSweepRunning = true;
                                    if (logger) logger->log("SWEEP", "Continuous sweep thread started");
                                }
                            }
                            spaceWasPressed = true;
                        }
                        break;
                        
                    case 'f':  // Freeze
                        analyzer.freeze();
                        print("\n" + translation.format("ACOUSTIC_FROZEN", "[FROZEN at position {0}]", analyzer.getPosition()) + "\n");
                        // Capture current fader values for freeze snap-back
                        {
                            const MeasurementPoint* freezePt = analyzer.getCurrentMeasurement();
                            if (freezePt) {
                                for (int fi = 0; fi < 5; fi++) {
                                    if (analyzer.isCurveEnabled(fi)) {
                                        double norm = normalizeCurveValue(*freezePt, fi);
                                        frozenFaderValues[fi] = MidiControllerManager::normalizedToMidi(norm);
                                    } else {
                                        frozenFaderValues[fi] = 0;
                                    }
                                }
                            }
                        }
                        break;
                        
                    case 's':  // Stop and reset
                        analyzer.stopYAxisRuler();  // Stop ruler thread first to prevent race condition
                        analyzer.stop();
                        print("\n" + translation.get("ACOUSTIC_STOPPED", "[STOPPED and reset to start]") + "\n");
                        sendAllFadersToZero();  // Reset all motor faders to zero position
                        
                        // Stop continuous sweep when stopping
                        if (continuousSweepRunning) {
                            stopContinuousSweep = true;
                            if (continuousSweepThread.joinable()) {
                                continuousSweepThread.join();
                            }
                            continuousSweepRunning = false;
                        }
                        break;
                    
                    case 't':  // Toggle smooth/dotted mode
                        analyzer.setSmoothMode(!analyzer.isSmoothMode());
                        print("\n" + (analyzer.isSmoothMode() ? 
                                translation.get("ACOUSTIC_MODE_SMOOTH", "[Playback mode: SMOOTH]") : 
                                translation.get("ACOUSTIC_MODE_DOTTED", "[Playback mode: DOTTED]")) + "\n");
                        cfg.acoustic_smooth_mode = analyzer.isSmoothMode();
                        
                        // If MIDI engine is active, also toggle MIDI playback mode and load appropriate preset
                        if (cfg.audio_engine == AudioEngineType::MIDI) {
                            if (analyzer.isSmoothMode()) {
                                // Switch to gliding mode and load gliding preset
                                cfg.midi_playback_mode = MIDIPlaybackMode::GLIDING;
                                for (int i = 0; i < 5; i++) {
                                    cfg.midi_instruments[i] = cfg.midi_instruments_gliding[i];
                                }
                                print(translation.get("ACOUSTIC_MIDI_MODE_GLIDING", 
                                    "[MIDI: Switched to GLIDING mode with sustained instruments]") + "\n");
                                if (logger) {
                                    logger->log("MIDI", "T key: Switched to GLIDING mode and loaded gliding preset");
                                }
                            } else {
                                // Switch to dotted mode and load dotted preset
                                cfg.midi_playback_mode = MIDIPlaybackMode::DOTTED;
                                for (int i = 0; i < 5; i++) {
                                    cfg.midi_instruments[i] = cfg.midi_instruments_dotted[i];
                                }
                                print(translation.get("ACOUSTIC_MIDI_MODE_DOTTED", 
                                    "[MIDI: Switched to DOTTED mode with percussive instruments]") + "\n");
                                if (logger) {
                                    logger->log("MIDI", "T key: Switched to DOTTED mode and loaded dotted preset");
                                }
                            }
                            
                            // Reinitialize the MIDI engine with new settings
                            auto audioEngine = analyzer.getAudioEngine();
                            if (audioEngine && std::string(audioEngine->getName()) == "MIDI") {
                                auto midiEngine = std::dynamic_pointer_cast<MIDIEngine>(audioEngine);
                                if (midiEngine) {
                                    midiEngine->setGlidingMode(cfg.midi_playback_mode == MIDIPlaybackMode::GLIDING);
                                    
                                    // Set interpolated panning mode and strength
                                    midiEngine->setInterpolatedPanMode(cfg.midi_interpolated_pan_mode);
                                    midiEngine->setInterpolationStrength(cfg.midi_interpolation_strength);
                                    
                                    // Update instruments
                                    for (int i = 0; i < 5; i++) {
                                        midiEngine->setCurveInstrument(i, cfg.midi_instruments[i]);
                                    }
                                    midiEngine->reset();  // Reset MIDI state with new settings
                                }
                            }
                        }
                        
                        saveSettings();
                        break;
                    
                    case '+':
                    case '=':  // Increase playback time
                        {
                            int time = analyzer.getPlaybackTimeSeconds() + 1;
                            analyzer.setPlaybackTimeSeconds(time);
                            
                            // Format message with skip factor info
                            std::string timeMsg = translation.format("ACOUSTIC_TIME", "[Playback time: {0} seconds per sweep]", analyzer.getPlaybackTimeSeconds());
                            int skipFactor = analyzer.getCurrentSkipFactor();
                            if (skipFactor > 1) {
                                timeMsg += " (skipping every " + std::to_string(skipFactor) + " points)";
                            }
                            
                            print("\n" + timeMsg + "\n");
                            cfg.acoustic_time_seconds = analyzer.getPlaybackTimeSeconds();
                            saveSettings();
                        }
                        break;
                    
                    case '-':
                    case '_':  // Decrease playback time
                        {
                            int time = analyzer.getPlaybackTimeSeconds() - 1;
                            
                            // Check if continuous sweep is active and enforce minimum playback time
                            bool allowDecrease = true;
                            if (cfg.continuous_sweep_enabled && cfg.last_measurement_duration_seconds > 0) {
                                // Calculate minimum playback time based on measurement duration
                                double minPlaybackTime = cfg.last_measurement_duration_seconds;
                                
                                // Exception: if loop is enabled and only partial section is active
                                if (analyzer.isLoopEnabled()) {
                                    size_t loopSize = analyzer.getLoopRight() - analyzer.getLoopLeft() + 1;
                                    size_t totalSize;
                                    {
                                        std::lock_guard<std::mutex> lock(dataMutex);
                                        totalSize = latestPts.size();
                                    }
                                    if (loopSize < totalSize) {
                                        // Scale minimum time based on loop size
                                        minPlaybackTime = cfg.last_measurement_duration_seconds * ((double)loopSize / (double)totalSize);
                                    }
                                }
                                
                                // Add small buffer (0.5 seconds) to ensure measurement completes
                                minPlaybackTime += 0.5;
                                
                                if (time < minPlaybackTime) {
                                    allowDecrease = false;
                                    char buf[300];
                                    snprintf(buf, sizeof(buf), 
                                        "[Cannot decrease: Continuous sweep requires minimum %.1f seconds for this measurement range]", 
                                        minPlaybackTime);
                                    print("\n" + std::string(buf) + "\n");
                                }
                            }
                            
                            if (allowDecrease) {
                                analyzer.setPlaybackTimeSeconds(time);
                                
                                // Format message with skip factor info
                                std::string timeMsg = translation.format("ACOUSTIC_TIME", "[Playback time: {0} seconds per sweep]", analyzer.getPlaybackTimeSeconds());
                                int skipFactor = analyzer.getCurrentSkipFactor();
                                if (skipFactor > 1) {
                                    timeMsg += " (skipping every " + std::to_string(skipFactor) + " points)";
                                }
                                
                                print("\n" + timeMsg + "\n");
                                cfg.acoustic_time_seconds = analyzer.getPlaybackTimeSeconds();
                                saveSettings();
                            }
                        }
                        break;
                    
                    case 'm':  // Show measurement at current position
                        {
                            const MeasurementPoint* pt = analyzer.getCurrentMeasurement();
                            if (pt) {
                                std::string ohmUnit = translation.get("OHM_TEXT", "Ohm");
                                std::string degUnit = translation.get("DEGREE_TEXT", "degree");
                                
                                print(formatHeading(translation.format("ACOUSTIC_MEASUREMENT_TITLE", "Measurement at position {0}", analyzer.getPosition())));
                                print(translation.format("ACOUSTIC_FREQUENCY", "Frequency: {0} Hz", pt->freq) + "\n");
                                print(translation.format("ACOUSTIC_SWR", "SWR: {0}", pt->swr) + "\n");
                                print(translation.format("ACOUSTIC_RETURN_LOSS", "Return Loss: {0} dB", pt->rl) + "\n");
                                print(translation.format("ACOUSTIC_IMPEDANCE", "Impedance: {0} + j{1}", pt->R, pt->X) + " " + ohmUnit + "\n");
                                print(translation.format("ACOUSTIC_MAGNITUDE", "Magnitude |Z|: {0}", pt->impedance_mag) + " " + ohmUnit + "\n");
                                print(translation.format("ACOUSTIC_PHASE", "Phase: {0}", pt->phase_deg) + " " + degUnit + "\n");
                                print(translation.format("ACOUSTIC_S11", "S11: {0} + j{1}", pt->s11_re, pt->s11_im) + "\n");
                            }
                        }
                        break;
                    
                    case 'e':  // Export with loop markers
                        analyzer.pause();  // Pause during export
                        print("\n" + translation.get("ACOUSTIC_EXPORTING", "[Exporting measurements...]") + "\n");
                        {
                            // Lock data mutex to safely access latestPts
                            std::lock_guard<std::mutex> lock(dataMutex);
                            exportMenu(latestPts, &analyzer);
                        }
                        print(translation.get("ACOUSTIC_CONTINUE", "[Press any key to continue...]") + "\n");
                        consoleInput->getch();
                        break;
                        
                    case 'l':  // Set left marker
                        analyzer.setLoopLeft(analyzer.getPosition());
                        print("\n" + translation.format("ACOUSTIC_LEFT_MARKER", "[Left marker set at {0}]", analyzer.getPosition()) + "\n");
                        break;
                        
                    case 'r':  // Set right marker
                        analyzer.setLoopRight(analyzer.getPosition());
                        print("\n" + translation.format("ACOUSTIC_RIGHT_MARKER", "[Right marker set at {0}]", analyzer.getPosition()) + "\n");
                        break;
                        
                    case 'o':  // Toggle loop
                        analyzer.toggleLoop();
                        print("\n" + (analyzer.isLoopEnabled() ? 
                                translation.format("ACOUSTIC_LOOP_ENABLED", "[Loop ENABLED ({0} - {1})]", analyzer.getLoopLeft(), analyzer.getLoopRight()) :
                                translation.format("ACOUSTIC_LOOP_DISABLED", "[Loop DISABLED ({0} - {1})]", analyzer.getLoopLeft(), analyzer.getLoopRight())) + "\n");
                        break;
                    
                    case 'z':  // Toggle loop zoom
                        analyzer.toggleLoopZoom();
                        if (analyzer.isLoopZoomEnabled()) {
                            print("\n" + translation.get("ACOUSTIC_LOOP_ZOOM_ON", "[Loop Zoom ENABLED]\nLoop section centered in stereo field\nFull playback time applied to loop range") + "\n");
                        } else {
                            print("\n" + translation.get("ACOUSTIC_LOOP_ZOOM_OFF", "[Loop Zoom DISABLED]\nNormal stereo panning restored") + "\n");
                        }
                        break;
                    
                    case 'i':  // Toggle loop invert
                        analyzer.toggleLoopInvert();
                        if (analyzer.isLoopInverted()) {
                            print("\n" + translation.get("ACOUSTIC_LOOP_INVERTED", "[Loop INVERTED: Playing OUTSIDE loop markers]") + "\n");
                        } else {
                            print("\n" + translation.get("ACOUSTIC_LOOP_NORMAL", "[Loop NORMAL: Playing INSIDE loop markers]") + "\n");
                        }
                        break;
                        
                    case 'c':  // Toggle continuous replay
                        analyzer.toggleContinuousReplay();
                        print("\n" + (analyzer.isContinuousReplay() ? 
                                translation.get("ACOUSTIC_CONTINUOUS_ON", "[Continuous replay ENABLED]") :
                                translation.get("ACOUSTIC_CONTINUOUS_OFF", "[Continuous replay DISABLED]")) + "\n");
                        break;
                    
                    case 'a':  // Audio Configuration
                        analyzer.pause();  // Pause during configuration
                        
                        // Stop all MIDI notes to prevent hanging notes when entering audio config
                        {
                            auto engine = analyzer.getAudioEngine();
                            if (engine) {
                                engine->stopAllNotes();
                            }
                        }
                        
                        // Stop Y-axis ruler if it's playing to prevent issues when switching engines
                        analyzer.stopYAxisRuler();
                        
                        print("\n" + translation.get("ACOUSTIC_AUDIO_CONFIG", "[Opening audio configuration...]") + "\n");
                        {
                            // Store current engine type to detect changes
                            AudioEngineType previousEngine = cfg.audio_engine;
                            
                            // Run audio configuration screen and get result
                            bool configChanged = runAudioConfigurationScreen(&analyzer);
                            if (configChanged) {
                                // Update frequency range and dot duration (applies immediately)
                                analyzer.setFrequencyRange(cfg.synth_min_freq_hz, cfg.synth_max_freq_hz);
                                analyzer.setDottedDurationMs(cfg.dotted_duration_ms);
                                
                                // Close the old audio engine before creating the new one
                                // This prevents MIDI error code 4 (MMSYSERR_ALLOCATED - device already open)
                                auto oldEngine = analyzer.getAudioEngine();
                                if (oldEngine) {
                                    if (logger) logger->log("AUDIO", "Closing old audio engine before switching");
                                    oldEngine->close();
                                }
                                
                                // Recreate and reconfigure audio engine
                                std::shared_ptr<IAudioEngine> newEngine;
                                if (cfg.audio_engine == AudioEngineType::MIDI) {
                                    auto midiEngine = std::make_shared<MIDIEngine>();
                                    midiEngine->setLogger(logger);  // Set logger for MIDI engine debug output
                                    if (!midiEngine->open()) {
                                        // Log MIDI engine failure to debug log
                                        if (logger) logger->log("MIDI", "Failed to open MIDI engine during audio config, keeping current engine");
                                        print(translation.get("MIDI_ERROR_OPEN_FAILED_KEEPING", "Warning: Failed to open MIDI engine, keeping current engine.") + "\n");
                                    } else {
                                        // Log successful MIDI engine opening
                                        if (logger) logger->log("MIDI", "MIDI engine opened successfully during audio config");
                                        // Set instruments AFTER opening the MIDI device
                                        for (int i = 0; i < 5; i++) {
                                            midiEngine->setCurveInstrument(i, cfg.midi_instruments[i]);
                                        }
                                        
                                        // Set MIDI playback mode (gliding vs dotted)
                                        midiEngine->setGlidingMode(cfg.midi_playback_mode == MIDIPlaybackMode::GLIDING);
                                        
                                        // Set synth frequency range for pitch bend calculations
                                        midiEngine->setSynthFrequencyRange(cfg.synth_min_freq_hz, cfg.synth_max_freq_hz);
                                        
                                        // Set interpolated panning mode and strength
                                        midiEngine->setInterpolatedPanMode(cfg.midi_interpolated_pan_mode);
                                        midiEngine->setInterpolationStrength(cfg.midi_interpolation_strength);
                                        
                                        // Set custom ruler instruments
                                        midiEngine->setRulerCustomInstruments(cfg.ruler_custom_sound_midi_gliding, cfg.ruler_custom_sound_midi_dotted);
                                        
                                        // Reset MIDI engine to clear any hanging notes
                                        midiEngine->reset();
                                        
                                        analyzer.stopYAxisRuler();  // Stop ruler thread first to prevent race condition
                                        analyzer.stop();  // Stop current playback
                                        newEngine = midiEngine;
                                        analyzer.setAudioEngine(newEngine);
                                        if (previousEngine != AudioEngineType::MIDI) {
                                            print(translation.format("ACOUSTIC_ENGINE_SWITCHED", "[Audio engine switched to {0}]", "MIDI") + "\n");
                                        } else {
                                            print(translation.get("ACOUSTIC_MIDI_UPDATED", "[MIDI instruments updated]") + "\n");
                                        }
                                    }
                                } else {
                                    analyzer.stopYAxisRuler();  // Stop ruler thread first to prevent race condition
                                    analyzer.stop();  // Stop current playback
                                    auto synthEngine = std::make_shared<SynthesizerEngine>();
                                    // Configure waveforms for each curve from config
                                    for (int i = 0; i < 5; i++) {
                                        synthEngine->setCurveWaveform(i, cfg.synth_waveforms[i]);
                                    }
                                    synthEngine->open();
                                    newEngine = synthEngine;
                                    analyzer.setAudioEngine(newEngine);
                                    print(translation.format("ACOUSTIC_ENGINE_SWITCHED", "[Audio engine switched to {0}]", "Synthesizer") + "\n");
                                }
                                
                                // Reload volumes based on the new audio engine type
                                for (int i = 0; i < 5; i++) {
                                    if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
                                        analyzer.setCurveVolume(i, cfg.curve_volume_synth[i]);
                                    } else {
                                        analyzer.setCurveVolume(i, cfg.curve_volume_midi[i]);
                                    }
                                }
                            }
                        }
                        // Re-initialize MIDI controller if settings changed in config screen
                        reinitMidiController();
                        // Removed unnecessary "Press any key to continue" prompt - return directly to acoustic analysis
                        break;
                    
                    case 'g':  // Go To menu
                        analyzer.pause();  // Pause during Go To menu
                        goToMenuAcoustic(analyzer, pts);
                        print(translation.get("ACOUSTIC_CONTINUE", "[Press any key to continue...]") + "\n");
                        consoleInput->getch();
                        break;
                    
                    case 'y':  // Y-Axis Ruler (Acoustic Y-Axis) - Toggle
                        {
                            if (analyzer.isRulerPlaying()) {
                                // Stop the ruler
                                print("\n" + translation.get("ACOUSTIC_RULER_STOPPING", "[Stopping Y-axis ruler...]") + "\n");
                                analyzer.stopYAxisRuler();
                                print(translation.get("ACOUSTIC_RULER_STOPPED", "[Y-axis ruler stopped]") + "\n");
                            } else {
                                // Start the ruler (toggle behavior is now handled in playYAxisRuler)
                                print("\n" + translation.get("ACOUSTIC_RULER_PLAYING", "[Playing Y-axis ruler...]") + "\n");
                                analyzer.playYAxisRuler();
                                // Note: ruler now plays in a separate thread, so we don't wait for completion here
                            }
                        }
                        break;
                    
                    case 'x':  // X-Axis Ruler - Toggle
                        {
                            analyzer.toggleXAxisRuler();
                            if (analyzer.isXAxisRulerEnabled()) {
                                print("\n" + translation.get("ACOUSTIC_X_RULER_ON", "[X-axis ruler ENABLED: Blips at each measurement point]") + "\n");
                            } else {
                                print("\n" + translation.get("ACOUSTIC_X_RULER_OFF", "[X-axis ruler DISABLED]") + "\n");
                            }
                        }
                        break;
                    
                    case 'n':  // Status line - Toggle
                        {
                            analyzer.toggleStatusLine();
                            std::string statusMsg;
                            if (analyzer.isStatusLineEnabled()) {
                                statusMsg = translation.get("ACOUSTIC_STATUS_LINE_ON", "[Status line ENABLED]") + "\n";
                                statusMsg += analyzer.getStatusLineText() + "\n";
                            } else {
                                statusMsg = translation.get("ACOUSTIC_STATUS_LINE_OFF", "[Status line DISABLED]") + "\n";
                            }
                            print("\n" + statusMsg);
                        }
                        break;
                    
                    case 'v':  // Smith Visualization - Toggle
                        {
                            bool currentState = analyzer.isSmithVisualizationEnabled();
                            analyzer.enableSmithVisualization(!currentState);
                            if (analyzer.isSmithVisualizationEnabled()) {
                                auto smith = analyzer.getSmithVisualizer();
                                std::string modeName = smith ? smith->getCurrentModeName() : "Unknown";
                                print("\n" + translation.get("SMITH_CUES_ENABLED", "[Smith spatial cues ENABLED]") + "\n");
                                print(translation.format("SMITH_MODE_SWITCHED", "[Smith mode: {0}]", modeName) + "\n");
                                print(translation.get("SMITH_MODE_HINT", "[Press 'b' to change mode, 'h' for Smith help]") + "\n");
                            } else {
                                print("\n" + translation.get("SMITH_CUES_DISABLED", "[Smith spatial cues DISABLED]") + "\n");
                            }
                        }
                        break;
                    
                    case 'w':  // Export audio: MIDI file (MIDI mode) or WAV file (Synth mode)
                        {
                            analyzer.pause();  // Pause during export
                            
                            // Generate filename with timestamp and mode
                            auto now = std::chrono::system_clock::now();
                            auto now_c = std::chrono::system_clock::to_time_t(now);
                            std::tm now_tm;
#if defined(_WIN32)
                            localtime_s(&now_tm, &now_c);
#else
                            localtime_r(&now_c, &now_tm);
#endif
                            
                            const char* modeName = analyzer.isSmoothMode() ? "smooth" : "dotted";
                            char timestamp[64];
                            std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &now_tm);
                            char filename[256];
                            
                            if (cfg.audio_engine == AudioEngineType::MIDI) {
                                // MIDI mode: export MIDI file
                                print("\n" + translation.get("MIDI_EXPORTING", "[Exporting MIDI file...]") + "\n");
                                std::snprintf(filename, sizeof(filename), "Export/nanovna_midi_%s_%s.mid", modeName, timestamp);
                                
                                if (analyzer.exportMIDIFile(filename)) {
                                    print(translation.format("MIDI_EXPORT_SUCCESS", "[MIDI file saved: {0}]", filename) + "\n");
                                } else {
                                    print(translation.get("MIDI_EXPORT_FAILED", "[MIDI export failed]") + "\n");
                                }
                            } else {
                                // Synth mode: render to WAV file
                                print("\n" + translation.get("SYNTH_RENDERING_WAV", "[Rendering audio to WAV file...]") + "\n");
                                std::snprintf(filename, sizeof(filename), "Export/nanovna_synth_%s_%s.wav", modeName, timestamp);
                                
                                if (analyzer.renderSynthToWav(filename)) {
                                    print(translation.format("SYNTH_WAV_SUCCESS", "[WAV file saved: {0}]", filename) + "\n");
                                } else {
                                    print(translation.get("SYNTH_WAV_FAILED", "[WAV rendering failed]") + "\n");
                                }
                            }
                        }
                        break;
                    
                    case '.':  // Center Pulse Toggle (reference signal for Smith chart center)
                        // Note: Period key has no uppercase variant, only one case needed
                        {
                            auto smith = analyzer.getSmithVisualizer();
                            if (smith) {
                                bool newState = !smith->isCenterPulseEnabled();
                                smith->setCenterPulseEnabled(newState);
                                if (newState) {
                                    print("\n" + translation.get("CENTER_PULSE_ENABLED", 
                                        "[Center pulse ENABLED - periodic reference signal]") + "\n");
                                } else {
                                    print("\n" + translation.get("CENTER_PULSE_DISABLED", 
                                        "[Center pulse DISABLED]") + "\n");
                                }
                            }
                        }
                        break;
                    
                    case 'q':  // Axis Events Toggle (crossing events for horizontal and vertical axes)
                    case 'Q':  // Handle both lowercase and uppercase for consistency
                        {
                            auto smith = analyzer.getSmithVisualizer();
                            if (smith) {
                                bool newState = !smith->isAxisEventsEnabled();
                                smith->setAxisEventsEnabled(newState);
                                if (newState) {
                                    print("\n" + translation.get("AXIS_EVENTS_ENABLED", 
                                        "[Axis crossing events ENABLED - audible markers at axis crossings]") + "\n");
                                } else {
                                    print("\n" + translation.get("AXIS_EVENTS_DISABLED", 
                                        "[Axis crossing events DISABLED]") + "\n");
                                }
                            }
                        }
                        break;
                    
                    case 'b':  // Smith Visualization Mode Selection (B for "Bild"/"Mode")
                        {
                            if (!analyzer.isSmithVisualizationEnabled()) {
                                print("\n" + translation.get("SMITH_ENABLE_FIRST", "[Enable Smith visualization first with 'v']") + "\n");
                                break;
                            }
                            
                            analyzer.pause();
                            print(formatHeading(translation.get("SMITH_MODE_SELECTION", "Smith Visualization Mode Selection")));
                            print(translation.get("SMITH_MODE_PROMPT", "Select visualization mode:") + "\n\n");
                            print(translation.get("SMITH_MODE_1", "1 - Cartesian") + "\n");
                            print(translation.get("SMITH_MODE_2", "2 - Polar") + "\n");
                            print(translation.get("SMITH_MODE_3", "3 - Impedance Direct") + "\n");
                            print(translation.get("SMITH_MODE_4", "4 - SWR Circles") + "\n");
                            print(translation.get("SMITH_MODE_5", "5 - Time Domain") + "\n");
                            print(translation.get("SMITH_MODE_6", "6 - Hybrid Multi") + "\n");
                            print("\n" + translation.get("MSG_PRESS_ESC_BACK", "Press ESC to go back") + "\n");
                            
                            int modeChoice = consoleInput->getch();
                            if (modeChoice >= '1' && modeChoice <= '6') {
                                auto smith = analyzer.getSmithVisualizer();
                                if (smith) {
                                    SmithVisualizationMode mode = static_cast<SmithVisualizationMode>(modeChoice - '1');
                                    smith->setMode(mode);
                                    print("\n" + translation.format("SMITH_MODE_SWITCHED", 
                                        "[Smith mode switched to: {0}]", smith->getCurrentModeName()) + "\n");
                                }
                            }
                            print(translation.get("ACOUSTIC_CONTINUE", "[Press any key to continue...]") + "\n");
                            consoleInput->getch();
                        }
                        break;
                    
                    case 'h':  // Help
                        // Show Smith-specific help if Smith visualization is enabled
                        if (analyzer.isSmithVisualizationEnabled()) {
                            print(HelpModule::getSmithVisualizationHelp(translation));
                        } else {
                            print(HelpModule::getAcousticAnalysisHelp(translation));
                        }
                        break;
                        
                    case 27:  // ESC key - Back to main menu
                        running = false;
                        analyzer.stopYAxisRuler();  // Stop ruler thread first to prevent race condition
                        analyzer.stop();
                        print("\n" + translation.get("ACOUSTIC_RETURN", "[Returning to main menu...]") + "\n");
                        break;
                }
            }
        } else {
            spaceWasPressed = false;
            
            // Update status line if enabled and position has changed during playback
            if (analyzer.isStatusLineEnabled() && analyzer.getState() == PlaybackState::PLAYING) {
                static size_t lastStatusPosition = std::numeric_limits<size_t>::max();
                size_t currentPosition = analyzer.getPosition();
                if (currentPosition != lastStatusPosition) {
                    lastStatusPosition = currentPosition;
                    displayPosition();
                }
            }
        }
        
        // Process MIDI controller command queue
        {
            std::vector<MidiAppCommand> cmds;
            {
                std::lock_guard<std::mutex> lock(midiCommandMutex);
                cmds.swap(midiCommandQueue);
            }
            for (auto cmd : cmds) {
                processMidiCommand(cmd);
            }
        }
        
        // Process MIDI CC value queue
        {
            std::vector<std::pair<MidiCCFunction, int>> ccEvents;
            {
                std::lock_guard<std::mutex> lock(midiCCMutex);
                ccEvents.swap(midiCCQueue);
            }
            for (auto& [func, value] : ccEvents) {
                processMidiCC(func, value);
            }
        }
        
        // Apply debounced volume changes (only after fader stops moving)
        applyDebouncedVolumes();
        
        // Send periodic motor fader feedback during playback or frozen state
        if (midiControllerMgr && midiControllerMgr->isDeviceOpen() && cfg.midi_controller_feedback) {
            PlaybackState currentState = analyzer.getState();
            
            if (currentState == PlaybackState::PLAYING) {
                size_t curPos = analyzer.getPosition();
                if (curPos != lastMidiFeedbackPos) {
                    lastMidiFeedbackPos = curPos;
                    size_t dataSize = analyzer.getDataSize();
                    if (dataSize > 1) {
                        // Send curve value feedback for enabled curves only
                        const MeasurementPoint* pt = analyzer.getCurrentMeasurement();
                        if (pt) {
                            sendCurveFaderValues(*pt);
                        }
                    }
                }
            } else if (currentState == PlaybackState::FROZEN) {
                // In freeze mode: if position changed (user moved playhead), update faders
                size_t curPos = analyzer.getPosition();
                if (curPos != lastMidiFeedbackPos) {
                    lastMidiFeedbackPos = curPos;
                    const MeasurementPoint* pt = analyzer.getCurrentMeasurement();
                    if (pt) {
                        sendCurveFaderValues(*pt);
                    }
                }
            }
        }
        
        // In freeze mode: snap faders back to frozen positions
        if (midiControllerMgr && midiControllerMgr->isDeviceOpen() && cfg.midi_controller_feedback &&
            analyzer.getState() == PlaybackState::FROZEN) {
            // Send frozen positions back for all active faders to override any user movement
            for (int i = 0; i < 5; i++) {
                if (analyzer.isCurveEnabled(i)) {
                    midiControllerMgr->sendCurveValueFeedback(i, frozenFaderValues[i] / 127.0);
                }
            }
        }
        
        // Small sleep to prevent busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Close MIDI controller when leaving acoustic analysis
    if (midiControllerMgr && midiControllerMgr->isDeviceOpen()) {
        midiControllerMgr->closeDevice();
        if (logger) logger->log("MIDI_CTRL", "MIDI controller closed on exit from acoustic analysis");
    }
    
    // Stop continuous sweep thread if running
    if (continuousSweepRunning) {
        stopContinuousSweep = true;
        if (continuousSweepThread.joinable()) {
            continuousSweepThread.join();
        }
    }
    
    clearScreen();
    print("\n" + translation.get("ACOUSTIC_EXITED", "Acoustic analysis mode exited.") + "\n");
    if (logger) logger->log("UI", "Exited acoustic analysis mode");
}

// Waveform type names (must match Waveform enum order exactly)
static const char* WAVEFORM_NAMES[] = {
    "Sine",           // 0 = Waveform::SINE
    "Square",         // 1 = Waveform::SQUARE
    "Triangle",       // 2 = Waveform::TRIANGLE
    "Sawtooth",       // 3 = Waveform::SAWTOOTH
    "Sawtooth Inv",   // 4 = Waveform::SAWTOOTH_INV
    "Pulse"           // 5 = Waveform::PULSE
};
static constexpr int WAVEFORM_NAMES_COUNT = sizeof(WAVEFORM_NAMES) / sizeof(WAVEFORM_NAMES[0]);

// Helper function to safely get waveform name with bounds checking
static const char* getWaveformName(Waveform wf) {
    int index = static_cast<int>(wf);
    if (index >= 0 && index < WAVEFORM_NAMES_COUNT) {
        return WAVEFORM_NAMES[index];
    }
    return "Unknown";
}

// General MIDI instrument names (0-127)
static const char* MIDI_INSTRUMENT_NAMES[128] = {
    // Piano (0-7)
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
    "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
    // Chromatic Percussion (8-15)
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone",
    "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    // Organ (16-23)
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
    "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    // Guitar (24-31)
    "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)",
    "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
    // Bass (32-39)
    "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass",
    "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    // Strings (40-47)
    "Violin", "Viola", "Cello", "Contrabass",
    "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    // Ensemble (48-55)
    "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2",
    "Choir Aahs", "Voice Oohs", "Synth Choir", "Orchestra Hit",
    // Brass (56-63)
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
    "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
    // Reed (64-71)
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
    "Oboe", "English Horn", "Bassoon", "Clarinet",
    // Pipe (72-79)
    "Piccolo", "Flute", "Recorder", "Pan Flute",
    "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    // Synth Lead (80-87)
    "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)",
    "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)",
    // Synth Pad (88-95)
    "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)",
    "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
    // Synth Effects (96-103)
    "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
    "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
    // Ethnic (104-111)
    "Sitar", "Banjo", "Shamisen", "Koto",
    "Kalimba", "Bag pipe", "Fiddle", "Shanai",
    // Percussive (112-119)
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
    "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    // Sound Effects (120-127)
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
    "Telephone Ring", "Helicopter", "Applause", "Gunshot"
};

// Helper function to safely get MIDI instrument name with bounds checking
static const char* getMidiInstrumentName(int instrument) {
    if (instrument >= 0 && instrument < 128) {
        return MIDI_INSTRUMENT_NAMES[instrument];
    }
    return "Unknown";
}

// Helper function to read numeric input with ESC support
bool ConsoleUI::readNumericInput(const std::string& prompt, int& result, int depth) {
    // Add depth indicator if depth > 0
    if (depth > 0) {
        print(prompt + " " + getDepthIndicator(depth) + " ");
    } else {
        print(prompt);
    }
    std::string input;
    bool inputting = true;
    
    while (inputting) {
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
                    ch = 8;  // Backspace
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
            if (ch == 27) {  // ESC
                print("\n" + translation.get("CANCELLED", "[Cancelled]") + "\n");
                return false;  // Cancelled
            } else if (ch == '\r' || ch == '\n') {  // Enter
                inputting = false;
            } else if (ch >= '0' && ch <= '9') {
                input += static_cast<char>(ch);
                print(std::string(1, ch));
            } else if ((ch == 8 || ch == 127) && !input.empty()) {  // Backspace
                input.pop_back();
                print("\b \b");
            }
        }
        
        if (inputting) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    if (!input.empty()) {
        try {
            result = std::stoi(input);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

// Unified raw-mode input helper (Phase 4: Canonical mode elimination)
// Replaces all enableCanonicalMode/getline sequences with consistent Escape support
// Supports both keyboard and web interface input
ConsoleUI::RawInputResult ConsoleUI::readRawLineInput(const std::string& prompt, const std::string& defaultValue, bool silentCancel) {
    print(prompt);
    
    // Show default value if provided
    std::string input = defaultValue;
    size_t cursorPos = input.length();  // Cursor position within the input string
    
    if (!defaultValue.empty()) {
        print(defaultValue);
    }
    
    // Notify web interface of text editing mode (arrows = cursor movement)
    std::string savedInputMode = currentInputMode;
    if (webServer && webServer->isRunning()) {
        currentInputMode = "text_edit";
        webServer->sendContext(getContextJSON());
    }
    
    // Helper to restore input mode before returning
    auto restoreInputMode = [&]() {
        currentInputMode = savedInputMode;
        if (webServer && webServer->isRunning()) {
            webServer->sendContext(getContextJSON());
        }
    };
    
    // Check if web interface is active
    if (webServer && webServer->isRunning()) {
        while (true) {
            int ch = 0;
            bool hasInput = false;
            
            // Check web interface first - now handles character-by-character input
            // (matching console single-keypress behavior)
            if (webServer->hasInput()) {
                std::string webInput = webServer->readInput();
                if (!webInput.empty()) {
                    if (logger) logger->log("UI", "Web raw input received: [" + webInput + "]");
                    
                    // Parse web input into logical key code
                    if (webInput[0] == '\x1B') {
                        if (webInput.length() >= 3 && webInput[1] == '[') {
                            // ANSI escape sequence from web client
                            char seqChar = webInput[2];
                            bool hasTilde = (webInput.length() >= 4 && webInput[3] == '~');
                            switch (seqChar) {
                                case 'A': ch = KEY_UP; break;
                                case 'B': ch = KEY_DOWN; break;
                                case 'C': ch = KEY_RIGHT; break;
                                case 'D': ch = KEY_LEFT; break;
                                case 'H': ch = KEY_HOME; break;
                                case 'F': ch = KEY_END; break;
                                case '3': ch = hasTilde ? KEY_DELETE : KEY_ESCAPE; break;
                                case '5': ch = hasTilde ? KEY_PAGE_UP : KEY_ESCAPE; break;
                                case '6': ch = hasTilde ? KEY_PAGE_DOWN : KEY_ESCAPE; break;
                                default: ch = KEY_ESCAPE; break;
                            }
                        } else {
                            ch = KEY_ESCAPE;
                        }
                    } else if (webInput[0] == '\r' || webInput[0] == '\n') {
                        ch = KEY_ENTER;
                    } else if (webInput[0] == '\x08' || webInput[0] == '\x7F') {
                        ch = KEY_BACKSPACE;
                    } else {
                        ch = static_cast<unsigned char>(webInput[0]);
                    }
                    hasInput = true;
                    
                    if (logger) logger->log("UI", "Web raw input mapped to ch: " + std::to_string(ch));
                }
            }
            
            // Check keyboard input if no web input (non-blocking)
            if (!hasInput && consoleInput->kbhit()) {
                ch = consoleInput->getKey();
                hasInput = true;
            }
            
            // Unified character processing for both web and console input
            if (hasInput) {
                if (ch == KEY_ESCAPE || ch == 27) {  // ESC - Cancel input
                    if (!silentCancel) {
                        print("\n" + translation.get("CANCELLED", "[Cancelled]") + "\n");
                    } else {
                        print("\n");
                    }
                    restoreInputMode();
                    return {input, true};
                } 
                else if (ch == '\r' || ch == '\n' || ch == KEY_ENTER) {  // Enter - Confirm input
                    print("\n");
                    // Echo to web interface
                    if (webServer && webServer->isRunning()) {
                        webServer->sendOutput(input + "\n");
                    }
                    restoreInputMode();
                    return {input, false};
                } 
                else if (ch == 8 || ch == 127 || ch == KEY_BACKSPACE) {  // Backspace
                    if (cursorPos > 0) {
                        // Remove character before cursor
                        input.erase(cursorPos - 1, 1);
                        cursorPos--;
                        
                        // Redraw: move back, print rest of string + space, move cursor back
                        print("\b" + input.substr(cursorPos) + " ");
                        for (size_t i = 0; i <= input.length() - cursorPos; i++) {
                            print("\b");
                        }
                    }
                } 
                else if (ch == KEY_DELETE || ch == 83) {  // Delete key (83 is old Windows code)
                    if (cursorPos < input.length()) {
                        // Remove character at cursor
                        input.erase(cursorPos, 1);
                        
                        // Redraw: print rest of string + space, move cursor back
                        print(input.substr(cursorPos) + " ");
                        for (size_t i = 0; i <= input.length() - cursorPos; i++) {
                            print("\b");
                        }
                    }
                }
                else if (ch == KEY_LEFT || ch == 75) {  // Left arrow (75 is old Windows code for Left)
                    if (cursorPos > 0) {
                        cursorPos--;
                        print("\b");  // Move cursor left
                    }
                }
                else if (ch == KEY_RIGHT || ch == 77) {  // Right arrow (77 is old Windows code for Right)
                    if (cursorPos < input.length()) {
                        print(std::string(1, input[cursorPos]));  // Print character and move cursor right
                        cursorPos++;
                    }
                }
                else if (ch == KEY_HOME || ch == 71) {  // Home key (71 is old Windows code for Home)
                    // Move cursor to beginning
                    while (cursorPos > 0) {
                        print("\b");
                        cursorPos--;
                    }
                }
                else if (ch == KEY_END || ch == 79) {  // End key (79 is old Windows code for End)
                    // Move cursor to end
                    while (cursorPos < input.length()) {
                        print(std::string(1, input[cursorPos]));
                        cursorPos++;
                    }
                }
                else if (ch >= 32 && ch < 127) {  // Printable ASCII character
                    // Insert character at cursor position
                    input.insert(cursorPos, 1, static_cast<char>(ch));
                    cursorPos++;
                    
                    // Redraw from cursor position
                    print(input.substr(cursorPos - 1));
                    // Move cursor back to correct position
                    for (size_t i = cursorPos; i < input.length(); i++) {
                        print("\b");
                    }
                }
                // Ignore other control characters and unknown keys
            }
            
            // Small sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    // Fallback for keyboard-only (no web interface)
    while (true) {
        if (consoleInput->kbhit()) {
            int ch = consoleInput->getKey();  // Use getKey() to properly decode extended keys
            
            if (ch == KEY_ESCAPE || ch == 27) {  // ESC - Cancel input
                if (!silentCancel) {
                    print("\n" + translation.get("CANCELLED", "[Cancelled]") + "\n");
                } else {
                    print("\n");
                }
                restoreInputMode();
                return {input, true};
            } 
            else if (ch == '\r' || ch == '\n' || ch == KEY_ENTER) {  // Enter - Confirm input
                print("\n");
                restoreInputMode();
                return {input, false};
            } 
            else if (ch == 8 || ch == 127 || ch == KEY_BACKSPACE) {  // Backspace
                if (cursorPos > 0) {
                    // Remove character before cursor
                    input.erase(cursorPos - 1, 1);
                    cursorPos--;
                    
                    // Redraw: move back, print rest of string + space, move cursor back
                    print("\b" + input.substr(cursorPos) + " ");
                    for (size_t i = 0; i <= input.length() - cursorPos; i++) {
                        print("\b");
                    }
                }
            } 
            else if (ch == KEY_DELETE || ch == 83) {  // Delete key (83 is old Windows code)
                if (cursorPos < input.length()) {
                    // Remove character at cursor
                    input.erase(cursorPos, 1);
                    
                    // Redraw: print rest of string + space, move cursor back
                    print(input.substr(cursorPos) + " ");
                    for (size_t i = 0; i <= input.length() - cursorPos; i++) {
                        print("\b");
                    }
                }
            }
            else if (ch == KEY_LEFT || ch == 75) {  // Left arrow (75 is old Windows code for Left)
                if (cursorPos > 0) {
                    cursorPos--;
                    print("\b");  // Move cursor left
                }
            }
            else if (ch == KEY_RIGHT || ch == 77) {  // Right arrow (77 is old Windows code for Right)
                if (cursorPos < input.length()) {
                    print(std::string(1, input[cursorPos]));  // Print character and move cursor right
                    cursorPos++;
                }
            }
            else if (ch == KEY_HOME || ch == 71) {  // Home key (71 is old Windows code for Home)
                // Move cursor to beginning
                while (cursorPos > 0) {
                    print("\b");
                    cursorPos--;
                }
            }
            else if (ch == KEY_END || ch == 79) {  // End key (79 is old Windows code for End)
                // Move cursor to end
                while (cursorPos < input.length()) {
                    print(std::string(1, input[cursorPos]));
                    cursorPos++;
                }
            }
            else if (ch >= 32 && ch < 127) {  // Printable ASCII character
                // Insert character at cursor position
                input.insert(cursorPos, 1, static_cast<char>(ch));
                cursorPos++;
                
                // Redraw from cursor position
                print(input.substr(cursorPos - 1));
                // Move cursor back to correct position
                for (size_t i = cursorPos; i < input.length(); i++) {
                    print("\b");
                }
            }
            // Ignore other control characters and unknown keys
        }
        
        // Small sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool ConsoleUI::runSmithConfigurationScreen(AcousticAnalyzer* analyzer) {
    clearScreen();
    print(formatHeading(translation.get("SMITH_AUDIO_CONFIG_TITLE", "Smith Diagram Audio Configuration")));
    
    // Get current settings
    auto smith = analyzer ? analyzer->getSmithVisualizer() : nullptr;
    if (!smith) {
        print(translation.get("SMITH_CONFIG_NO_VISUALIZER", "[Error: Smith visualizer not available]") + "\n");
        return false;
    }
    
    // Detect and display audio capability
    AudioCapability audioCap = smith->getAudioCapability();
    const char* capName = "";
    switch (audioCap) {
        case AudioCapability::STEREO_ONLY: 
            capName = "Stereo (2.0)"; 
            break;
        case AudioCapability::SURROUND_5_1: 
            capName = "5.1 Surround"; 
            break;
        case AudioCapability::SURROUND_7_1: 
            capName = "7.1 Surround"; 
            break;
        case AudioCapability::SURROUND_ATMOS: 
            capName = "Dolby Atmos"; 
            break;
    }
    
    print(translation.format("SMITH_CONFIG_AUDIO_MODE", "\nAudio Mode: {0}", capName) + "\n");
    
    if (audioCap == AudioCapability::STEREO_ONLY) {
        print(translation.get("SMITH_CONFIG_STEREO_NOTE", 
            "  Note: Only left/right panning available in stereo mode.") + "\n");
        print(translation.get("SMITH_CONFIG_STEREO_HINT", 
            "  For full 360° spatial audio, use a 5.1 or 7.1 surround headset.") + "\n");
    } else {
        print(translation.get("SMITH_CONFIG_SURROUND_ACTIVE", 
            "  Full spatial audio (360°) is active!") + "\n");
        print(translation.get("SMITH_CONFIG_SURROUND_HINT", 
            "  Use 'S' to configure surround sound parameters.") + "\n");
    }
    
    int currentVolume = smith->getSmithCuesVolume();
    AppConfig::SmithNoiseType currentNoiseType = smith->getNoiseType();
    
    // Get center pulse and axis events settings
    bool centerPulseEnabled = smith->isCenterPulseEnabled();
    int centerPulseVolume = smith->getCenterPulseVolume();
    double centerPulseInterval = smith->getCenterPulseInterval();
    AppConfig::CenterPulseWaveform centerPulseWaveform = smith->getCenterPulseWaveform();
    bool axisEventsEnabled = smith->isAxisEventsEnabled();
    int axisEventsVolume = smith->getAxisEventsVolume();
    int axisEventsDuration = smith->getAxisEventsDuration();
    double axisPitchMin = smith->getAxisEventsPitchMin();
    double axisPitchMax = smith->getAxisEventsPitchMax();
    AppConfig::AxisCrossingSound axisCrossingSound = smith->getAxisCrossingSound();
    
    // Display current settings
    print(translation.format("SMITH_CONFIG_VOLUME_CURRENT", "\nCurrent volume: {0}%", currentVolume) + "\n");
    
    const char* noiseTypeName = "";
    switch (currentNoiseType) {
        case AppConfig::SmithNoiseType::PINK: noiseTypeName = "Pink Noise"; break;
        case AppConfig::SmithNoiseType::WHITE: noiseTypeName = "White Noise"; break;
        case AppConfig::SmithNoiseType::BROWN: noiseTypeName = "Brown Noise"; break;
        case AppConfig::SmithNoiseType::SINE_WAVE: noiseTypeName = "Sine Wave"; break;
    }
    print(translation.format("SMITH_CONFIG_NOISE_TYPE_CURRENT", "Current sound type: {0}", noiseTypeName) + "\n");
    
    // Display center pulse and axis events status
    print(translation.format("SMITH_CONFIG_CENTER_PULSE_STATUS", "\nCenter pulse: {0}", 
        (centerPulseEnabled ? "ENABLED" : "DISABLED")) + "\n");
    if (centerPulseEnabled) {
        print(translation.format("SMITH_CONFIG_CENTER_PULSE_DETAILS", "  Volume: {0}%, Interval: {1} seconds", 
            centerPulseVolume, centerPulseInterval) + "\n");
    }
    
    print(translation.format("SMITH_CONFIG_AXIS_EVENTS_STATUS", "Axis events: {0}", 
        (axisEventsEnabled ? "ENABLED" : "DISABLED")) + "\n");
    if (axisEventsEnabled) {
        print(translation.format("SMITH_CONFIG_AXIS_EVENTS_DETAILS", "  Volume: {0}%, Pitch: {1}-{2} Hz", 
            axisEventsVolume, static_cast<int>(axisPitchMin), static_cast<int>(axisPitchMax)) + "\n");
    }
    print("\n");
    
    print(translation.get("SMITH_CONFIG_COMMANDS", "Commands:") + "\n");
    print(translation.get("SMITH_CONFIG_VOLUME_CMD", "  V - Configure Volume (10-100%)") + "\n");
    print(translation.get("SMITH_CONFIG_NOISE_TYPE_CMD", "  N - Configure sound type (Pink/White/Brown/Sine)") + "\n");
    print(translation.get("SMITH_CONFIG_CENTER_PULSE_CMD", "  C - Configure Center Pulse (reference signal)") + "\n");
    print(translation.get("SMITH_CONFIG_AXIS_EVENTS_CMD", "  A - Configure Axis crossing Events") + "\n");
    if (audioCap != AudioCapability::STEREO_ONLY) {
        print(translation.get("SMITH_CONFIG_SURROUND_CMD", "  S - Configure Surround Sound parameters") + "\n");
    }
    print(translation.get("SMITH_CONFIG_PREVIEW_CMD", "  P - Preview current Smith sound") + "\n");
    print(translation.get("HELP_COMMAND", "  H - Help") + "\n");
    print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
    
    bool running = true;
    bool settingsChanged = false;
    
    print(getPromptWithDepth("SMITH_CONFIG_PROMPT", 4) + " ");
    
    while (running) {
        int ch = 0;
        bool hasInput = false;
        
        // Check for keyboard input
        if (consoleInput->kbhit()) {
            ch = consoleInput->getch();
            if (ch == 0 || ch == 224) {
                if (consoleInput->kbhit()) consoleInput->getch();
                continue;
            }
            hasInput = true;
        }
        
        if (hasInput) {
            char key = static_cast<char>(ch);
            if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
            
            switch (key) {
                case 'v':  // Volume configuration
                    {
                        print("V\n\n");
                        std::string prompt = translation.format("SMITH_CONFIG_ENTER_VOLUME", 
                            "Enter Smith cues volume (10-100%, current: {0}%), or press ESC to cancel:", currentVolume);
                        print(prompt + " " + getDepthIndicator(5) + " ");
                        
                        std::string volumeInput;
                        bool inputting = true;
                        while (inputting) {
                            if (consoleInput->kbhit()) {
                                int vch = consoleInput->getch();
                                if (vch == 27) {  // ESC
                                    print("\n" + translation.get("CANCELLED", "[Cancelled]") + "\n");
                                    inputting = false;
                                } else if (vch == '\r' || vch == '\n') {  // Enter
                                    if (!volumeInput.empty()) {
                                        try {
                                            int volume = std::stoi(volumeInput);
                                            if (volume >= 10 && volume <= 100) {
                                                smith->setSmithCuesVolume(volume);
                                                cfg.smith_cues_volume = volume;
                                                currentVolume = volume;
                                                print("\n" + translation.format("SMITH_CONFIG_VOLUME_SET", 
                                                    "[Smith cues volume set to: {0}%]", volume) + "\n");
                                                saveSettings();
                                                settingsChanged = true;
                                                inputting = false;
                                            } else {
                                                print("\n" + translation.get("SMITH_CONFIG_VOLUME_ERROR", 
                                                    "[Error: Volume must be between 10 and 100]") + "\n");
                                                std::string prompt = translation.format("SMITH_CONFIG_ENTER_VOLUME", 
                                                    "Enter Smith cues volume (10-100%, current: {0}%), or press ESC to cancel:", currentVolume);
                                                print(prompt + " " + getDepthIndicator(5) + " ");
                                                volumeInput.clear();
                                            }
                                        } catch (...) {
                                            print("\n" + translation.get("ERROR_INVALID_NUMBER", "[Error: Invalid number]") + "\n");
                                            std::string prompt = translation.format("SMITH_CONFIG_ENTER_VOLUME", 
                                                "Enter Smith cues volume (10-100%, current: {0}%), or press ESC to cancel:", currentVolume);
                                            print(prompt + " " + getDepthIndicator(5) + " ");
                                            volumeInput.clear();
                                        }
                                    }
                                } else if (vch >= '0' && vch <= '9') {
                                    volumeInput += static_cast<char>(vch);
                                    print(std::string(1, vch));
                                } else if (vch == 8 && !volumeInput.empty()) {  // Backspace
                                    volumeInput.pop_back();
                                    print("\b \b");
                                }
                            }
                        }
                    }
                    break;
                
                case 'n':  // Noise type configuration
                    {
                        print("N\n\n" + formatSubHeading(translation.get("SMITH_CONFIG_NOISE_TYPE_MENU", "Select Sound Type")));
                        print(translation.get("SMITH_CONFIG_NOISE_TYPE_1", "  1 - Pink Noise (warm, filtered - default)") + "\n");
                        print(translation.get("SMITH_CONFIG_NOISE_TYPE_2", "  2 - White Noise (bright, full spectrum)") + "\n");
                        print(translation.get("SMITH_CONFIG_NOISE_TYPE_3", "  3 - Brown Noise (dark, low frequencies)") + "\n");
                        print(translation.get("SMITH_CONFIG_NOISE_TYPE_4", "  4 - Sine Wave (pure tone, cleaner)") + "\n");
                        print(translation.get("MSG_PRESS_ESC_BACK", "Press ESC to go back") + "\n\n");
                        print(getPromptWithDepth("SMITH_CONFIG_NOISE_TYPE_PROMPT", 5) + " ");
                        
                        int typeChoice = consoleInput->getch();
                        if (typeChoice >= '1' && typeChoice <= '4') {
                            AppConfig::SmithNoiseType newType;
                            const char* newTypeName = "";
                            
                            switch (typeChoice) {
                                case '1': 
                                    newType = AppConfig::SmithNoiseType::PINK;
                                    newTypeName = "Pink Noise";
                                    break;
                                case '2': 
                                    newType = AppConfig::SmithNoiseType::WHITE;
                                    newTypeName = "White Noise";
                                    break;
                                case '3': 
                                    newType = AppConfig::SmithNoiseType::BROWN;
                                    newTypeName = "Brown Noise";
                                    break;
                                case '4': 
                                    newType = AppConfig::SmithNoiseType::SINE_WAVE;
                                    newTypeName = "Sine Wave";
                                    break;
                            }
                            
                            smith->setNoiseType(newType);
                            cfg.smith_noise_type = newType;
                            currentNoiseType = newType;
                            print("\n" + translation.format("SMITH_CONFIG_NOISE_TYPE_SET", 
                                "[Sound type changed to: {0}]", newTypeName) + "\n");
                            saveSettings();
                            settingsChanged = true;
                        }
                    }
                    break;
                
                case 'c':  // Center pulse configuration
                    {
                        print("C\n\n" + formatSubHeading(translation.get("SMITH_CONFIG_CENTER_PULSE_MENU", "Center Pulse Configuration")));
                        print(translation.format("SMITH_CONFIG_CENTER_PULSE_CURRENT_STATUS", "Current status: {0}",
                            (centerPulseEnabled ? "ENABLED" : "DISABLED")) + "\n");
                        print(translation.format("SMITH_CONFIG_CENTER_PULSE_CURRENT_VOL", "Volume: {0}%", centerPulseVolume) + "\n");
                        print(translation.format("SMITH_CONFIG_CENTER_PULSE_CURRENT_INT", "Interval: {0} seconds", centerPulseInterval) + "\n");
                        
                        // Display waveform type
                        const char* waveformName = "Unknown";
                        switch (centerPulseWaveform) {
                            case AppConfig::CenterPulseWaveform::CLICK: waveformName = "Click (filtered noise)"; break;
                            case AppConfig::CenterPulseWaveform::SINE: waveformName = "Sine wave"; break;
                            case AppConfig::CenterPulseWaveform::SQUARE: waveformName = "Square wave"; break;
                            case AppConfig::CenterPulseWaveform::TRIANGLE: waveformName = "Triangle wave"; break;
                            case AppConfig::CenterPulseWaveform::SAWTOOTH: waveformName = "Sawtooth wave"; break;
                            case AppConfig::CenterPulseWaveform::PULSE: waveformName = "Pulse wave"; break;
                        }
                        print(translation.format("SMITH_CONFIG_CENTER_PULSE_CURRENT_WAVE", "Waveform: {0}", waveformName) + "\n\n");
                        
                        print(translation.get("SMITH_CONFIG_CENTER_PULSE_COMMANDS", "Commands:") + "\n");
                        print(translation.get("SMITH_CONFIG_CENTER_PULSE_TOGGLE_CMD", "  T - Toggle center pulse on/off") + "\n");
                        print(translation.get("SMITH_CONFIG_CENTER_PULSE_VOLUME_CMD", "  V - Set volume (10-100%)") + "\n");
                        print(translation.get("SMITH_CONFIG_CENTER_PULSE_INTERVAL_CMD", "  I - Set interval (0.5-2.0 seconds)") + "\n");
                        print(translation.get("SMITH_CONFIG_CENTER_PULSE_WAVEFORM_CMD", "  W - Select waveform type") + "\n");
                        print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
                        print(getPromptWithDepth("SMITH_CONFIG_CENTER_PULSE_PROMPT", 5) + " ");
                        
                        int cpChoice = consoleInput->getch();
                        switch (cpChoice) {
                            case 't':
                            case 'T':
                                centerPulseEnabled = !centerPulseEnabled;
                                smith->setCenterPulseEnabled(centerPulseEnabled);
                                cfg.center_pulse_enabled = centerPulseEnabled;
                                print("\n" + translation.format("SMITH_CONFIG_CENTER_PULSE_TOGGLED",
                                    "[Center pulse: {0}]", (centerPulseEnabled ? "ENABLED" : "DISABLED")) + "\n");
                                saveSettings();
                                settingsChanged = true;
                                break;
                            
                            case 'v':
                            case 'V':
                                {
                                    std::string prompt = translation.format("SMITH_CONFIG_CENTER_PULSE_ENTER_VOL",
                                        "Enter center pulse volume (10-100%, current: {0}%): ", centerPulseVolume);
                                    std::string depthIndicator = getDepthIndicator(6);
                                    print("\n" + prompt + " " + depthIndicator + " ");
                                    std::string volInput;
                                    if (readLine(volInput) && !volInput.empty()) {
                                        try {
                                            int vol = std::stoi(volInput);
                                            if (vol >= 10 && vol <= 100) {
                                                smith->setCenterPulseVolume(vol);
                                                cfg.center_pulse_volume = vol;
                                                centerPulseVolume = vol;
                                                print(translation.format("SMITH_CONFIG_CENTER_PULSE_VOL_SET",
                                                    "[Center pulse volume set to {0}%]", vol) + "\n");
                                                saveSettings();
                                                settingsChanged = true;
                                            } else {
                                                print(translation.get("SMITH_CONFIG_CENTER_PULSE_VOL_ERROR",
                                                    "[Error: Volume must be 10-100]") + "\n");
                                            }
                                        } catch (...) {
                                            print(translation.get("ERROR_INVALID_NUMBER", "[Error: Invalid number]") + "\n");
                                        }
                                    }
                                }
                                break;
                            
                            case 'i':
                            case 'I':
                                {
                                    std::string prompt = translation.format("SMITH_CONFIG_CENTER_PULSE_ENTER_INT",
                                        "Enter pulse interval in seconds (0.5-2.0, current: {0}): ", centerPulseInterval);
                                    std::string depthIndicator = getDepthIndicator(6);
                                    print("\n" + prompt + " " + depthIndicator + " ");
                                    std::string intInput;
                                    if (readLine(intInput) && !intInput.empty()) {
                                        try {
                                            double interval = std::stod(intInput);
                                            if (interval >= 0.5 && interval <= 2.0) {
                                                smith->setCenterPulseInterval(interval);
                                                cfg.center_pulse_interval = interval;
                                                centerPulseInterval = interval;
                                                print(translation.format("SMITH_CONFIG_CENTER_PULSE_INT_SET",
                                                    "[Pulse interval set to {0} seconds]", interval) + "\n");
                                                saveSettings();
                                                settingsChanged = true;
                                            } else {
                                                print(translation.get("SMITH_CONFIG_CENTER_PULSE_INT_ERROR",
                                                    "[Error: Interval must be 0.5-2.0]") + "\n");
                                            }
                                        } catch (...) {
                                            print(translation.get("ERROR_INVALID_NUMBER", "[Error: Invalid number]") + "\n");
                                        }
                                    }
                                }
                                break;
                            
                            case 'w':
                            case 'W':
                                {
                                    print("\n" + formatSubHeading(translation.get("SMITH_CONFIG_CENTER_PULSE_WAVE_MENU", "Select Center Pulse Waveform")));
                                    print(translation.get("SMITH_CONFIG_CENTER_PULSE_WAVE_1", "  1 - Click (filtered noise - default, percussive)") + "\n");
                                    print(translation.get("SMITH_CONFIG_CENTER_PULSE_WAVE_2", "  2 - Sine wave (clean, musical)") + "\n");
                                    print(translation.get("SMITH_CONFIG_CENTER_PULSE_WAVE_3", "  3 - Square wave (bright, synthetic)") + "\n");
                                    print(translation.get("SMITH_CONFIG_CENTER_PULSE_WAVE_4", "  4 - Triangle wave (warm, mellow)") + "\n");
                                    print(translation.get("SMITH_CONFIG_CENTER_PULSE_WAVE_5", "  5 - Sawtooth wave (bright, rich)") + "\n");
                                    print(translation.get("SMITH_CONFIG_CENTER_PULSE_WAVE_6", "  6 - Pulse wave (sharp, electronic)") + "\n\n");
                                    print(getPromptWithDepth("SMITH_CONFIG_WAVEFORM_PROMPT", 6) + " ");
                                    
                                    int waveChoice = consoleInput->getch();
                                    AppConfig::CenterPulseWaveform newWaveform = centerPulseWaveform;
                                    const char* waveName = nullptr;
                                    
                                    switch (waveChoice) {
                                        case '1': newWaveform = AppConfig::CenterPulseWaveform::CLICK; waveName = "Click"; break;
                                        case '2': newWaveform = AppConfig::CenterPulseWaveform::SINE; waveName = "Sine wave"; break;
                                        case '3': newWaveform = AppConfig::CenterPulseWaveform::SQUARE; waveName = "Square wave"; break;
                                        case '4': newWaveform = AppConfig::CenterPulseWaveform::TRIANGLE; waveName = "Triangle wave"; break;
                                        case '5': newWaveform = AppConfig::CenterPulseWaveform::SAWTOOTH; waveName = "Sawtooth wave"; break;
                                        case '6': newWaveform = AppConfig::CenterPulseWaveform::PULSE; waveName = "Pulse wave"; break;
                                    }
                                    
                                    if (waveName) {
                                        smith->setCenterPulseWaveform(newWaveform);
                                        cfg.center_pulse_waveform = newWaveform;
                                        centerPulseWaveform = newWaveform;
                                        print("\n" + translation.format("SMITH_CONFIG_CENTER_PULSE_WAVE_SET",
                                            "[Center pulse waveform set to: {0}]", waveName) + "\n");
                                        saveSettings();
                                        settingsChanged = true;
                                    }
                                }
                                break;
                            
                            case 27:  // ESC
                                break;
                        }
                    }
                    break;
                
                case 'a':  // Axis events configuration
                    {
                        print("A\n\n" + formatSubHeading(translation.get("SMITH_CONFIG_AXIS_EVENTS_MENU", "Axis Crossing Events Configuration")));
                        print(translation.format("SMITH_CONFIG_AXIS_EVENTS_CURRENT_STATUS", "Current status: {0}",
                            (axisEventsEnabled ? "ENABLED" : "DISABLED")) + "\n");
                        print(translation.format("SMITH_CONFIG_AXIS_EVENTS_CURRENT_VOL", "Volume: {0}%", axisEventsVolume) + "\n");
                        print(translation.format("SMITH_CONFIG_AXIS_EVENTS_CURRENT_DURATION", "Duration: {0} ms", axisEventsDuration) + "\n");
                        print(translation.format("SMITH_CONFIG_AXIS_EVENTS_CURRENT_PITCH", "Pitch range: {0}-{1} Hz",
                            static_cast<int>(axisPitchMin), static_cast<int>(axisPitchMax)) + "\n");
                        
                        // Display sound type
                        const char* soundName = "Unknown";
                        switch (axisCrossingSound) {
                            case AppConfig::AxisCrossingSound::PLUCK: soundName = "Pluck (string-like)"; break;
                            case AppConfig::AxisCrossingSound::SWEEP: soundName = "Sweep (pure sine)"; break;
                            case AppConfig::AxisCrossingSound::CHIRP: soundName = "Chirp (complex)"; break;
                            case AppConfig::AxisCrossingSound::BELL: soundName = "Bell (resonant)"; break;
                            case AppConfig::AxisCrossingSound::PERCUSSION: soundName = "Percussion (sharp)"; break;
                        }
                        print(translation.format("SMITH_CONFIG_AXIS_EVENTS_CURRENT_SOUND", "Sound type: {0}", soundName) + "\n\n");
                        
                        print(translation.get("SMITH_CONFIG_AXIS_EVENTS_COMMANDS", "Commands:") + "\n");
                        print(translation.get("SMITH_CONFIG_AXIS_EVENTS_TOGGLE_CMD", "  T - Toggle axis events on/off") + "\n");
                        print(translation.get("SMITH_CONFIG_AXIS_EVENTS_VOLUME_CMD", "  V - Set volume (10-100%)") + "\n");
                        print(translation.get("SMITH_CONFIG_AXIS_EVENTS_DURATION_CMD", "  D - Set duration (50-500ms)") + "\n");
                        print(translation.get("SMITH_CONFIG_AXIS_EVENTS_PITCH_CMD", "  P - Set pitch range") + "\n");
                        print(translation.get("SMITH_CONFIG_AXIS_EVENTS_SOUND_CMD", "  S - Select sound type") + "\n");
                        print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
                        print(getPromptWithDepth("SMITH_CONFIG_AXIS_EVENTS_PROMPT", 5) + " ");
                        
                        int aeChoice = consoleInput->getch();
                        switch (aeChoice) {
                            case 't':
                            case 'T':
                                axisEventsEnabled = !axisEventsEnabled;
                                smith->setAxisEventsEnabled(axisEventsEnabled);
                                cfg.axis_events_enabled = axisEventsEnabled;
                                print("\n" + translation.format("SMITH_CONFIG_AXIS_EVENTS_TOGGLED",
                                    "[Axis events: {0}]", (axisEventsEnabled ? "ENABLED" : "DISABLED")) + "\n");
                                saveSettings();
                                settingsChanged = true;
                                break;
                            
                            case 'v':
                            case 'V':
                                {
                                    std::string prompt = translation.format("SMITH_CONFIG_AXIS_EVENTS_ENTER_VOL",
                                        "Enter axis events volume (10-100%, current: {0}%): ", axisEventsVolume);
                                    std::string depthIndicator = getDepthIndicator(6);
                                    print("\n" + prompt + " " + depthIndicator + " ");
                                    std::string volInput;
                                    if (readLine(volInput) && !volInput.empty()) {
                                        try {
                                            int vol = std::stoi(volInput);
                                            if (vol >= 10 && vol <= 100) {
                                                smith->setAxisEventsVolume(vol);
                                                cfg.axis_events_volume = vol;
                                                axisEventsVolume = vol;
                                                print(translation.format("SMITH_CONFIG_AXIS_EVENTS_VOL_SET",
                                                    "[Axis events volume set to {0}%]", vol) + "\n");
                                                saveSettings();
                                                settingsChanged = true;
                                            } else {
                                                print(translation.get("SMITH_CONFIG_AXIS_EVENTS_VOL_ERROR",
                                                    "[Error: Volume must be 10-100]") + "\n");
                                            }
                                        } catch (...) {
                                            print(translation.get("ERROR_INVALID_NUMBER", "[Error: Invalid number]") + "\n");
                                        }
                                    }
                                }
                                break;
                            
                            case 'd':
                            case 'D':
                                {
                                    std::string prompt = translation.format("SMITH_CONFIG_AXIS_EVENTS_ENTER_DURATION",
                                        "Enter axis events sound duration (50-500ms, current: {0}): ", axisEventsDuration);
                                    std::string depthIndicator = getDepthIndicator(6);
                                    print("\n" + prompt + " " + depthIndicator + " ");
                                    std::string durInput;
                                    if (readLine(durInput) && !durInput.empty()) {
                                        try {
                                            int duration = std::stoi(durInput);
                                            if (duration >= 50 && duration <= 500) {
                                                smith->setAxisEventsDuration(duration);
                                                cfg.axis_events_duration_ms = duration;
                                                axisEventsDuration = duration;
                                                print(translation.format("SMITH_CONFIG_AXIS_EVENTS_DURATION_SET",
                                                    "[Axis events duration set to {0} ms]", duration) + "\n");
                                                saveSettings();
                                                settingsChanged = true;
                                            } else {
                                                print(translation.get("SMITH_CONFIG_AXIS_EVENTS_DURATION_ERROR",
                                                    "[Error: Duration must be 50-500ms]") + "\n");
                                            }
                                        } catch (...) {
                                            print(translation.get("ERROR_INVALID_NUMBER", "[Error: Invalid number]") + "\n");
                                        }
                                    }
                                }
                                break;
                            
                            case 'p':
                            case 'P':
                                {
                                    print("\n" + translation.get("SMITH_CONFIG_AXIS_EVENTS_PITCH_MENU", "Set pitch range:") + "\n");
                                    std::string prompt = translation.format("SMITH_CONFIG_AXIS_EVENTS_ENTER_MIN",
                                        "Enter minimum pitch (200-1000 Hz, current: {0}): ", static_cast<int>(axisPitchMin));
                                    std::string depthIndicator = getDepthIndicator(6);
                                    print(prompt + " " + depthIndicator + " ");
                                    std::string minInput;
                                    if (readLine(minInput) && !minInput.empty()) {
                                        try {
                                            double pitchMin = std::stod(minInput);
                                            std::string prompt2 = translation.format("SMITH_CONFIG_AXIS_EVENTS_ENTER_MAX",
                                                "Enter maximum pitch (400-2000 Hz, current: {0}): ", static_cast<int>(axisPitchMax));
                                            std::string depthIndicator2 = getDepthIndicator(6);
                                            print(prompt2 + " " + depthIndicator2 + " ");
                                            std::string maxInput;
                                            if (readLine(maxInput) && !maxInput.empty()) {
                                                double pitchMax = std::stod(maxInput);
                                                if (pitchMin >= 200.0 && pitchMin <= 1000.0 && 
                                                    pitchMax >= 400.0 && pitchMax <= 2000.0 &&
                                                    pitchMin < pitchMax) {
                                                    smith->setAxisEventsPitchRange(pitchMin, pitchMax);
                                                    cfg.axis_events_pitch_min = pitchMin;
                                                    cfg.axis_events_pitch_max = pitchMax;
                                                    axisPitchMin = pitchMin;
                                                    axisPitchMax = pitchMax;
                                                    print(translation.format("SMITH_CONFIG_AXIS_EVENTS_PITCH_SET",
                                                        "[Pitch range set to {0}-{1} Hz]", 
                                                        static_cast<int>(pitchMin), static_cast<int>(pitchMax)) + "\n");
                                                    saveSettings();
                                                    settingsChanged = true;
                                                } else {
                                                    print(translation.get("SMITH_CONFIG_AXIS_EVENTS_PITCH_ERROR",
                                                        "[Error: Invalid pitch range]") + "\n");
                                                }
                                            }
                                        } catch (...) {
                                            print(translation.get("ERROR_INVALID_NUMBER", "[Error: Invalid number]") + "\n");
                                        }
                                    }
                                }
                                break;
                            
                            case 's':
                            case 'S':
                                {
                                    print("\n" + formatSubHeading(translation.get("SMITH_CONFIG_AXIS_EVENTS_SOUND_MENU", "Select Axis Crossing Sound")));
                                    print(translation.get("SMITH_CONFIG_AXIS_EVENTS_SOUND_1", "  1 - Pluck (string-like, natural - default)") + "\n");
                                    print(translation.get("SMITH_CONFIG_AXIS_EVENTS_SOUND_2", "  2 - Sweep (pure sine, directional)") + "\n");
                                    print(translation.get("SMITH_CONFIG_AXIS_EVENTS_SOUND_3", "  3 - Chirp (complex, attention-grabbing)") + "\n");
                                    print(translation.get("SMITH_CONFIG_AXIS_EVENTS_SOUND_4", "  4 - Bell (resonant, pleasant)") + "\n");
                                    print(translation.get("SMITH_CONFIG_AXIS_EVENTS_SOUND_5", "  5 - Percussion (sharp, distinctive)") + "\n\n");
                                    print(getPromptWithDepth("SMITH_CONFIG_SOUND_TYPE_PROMPT", 6) + " ");
                                    
                                    int soundChoice = consoleInput->getch();
                                    AppConfig::AxisCrossingSound newSound = axisCrossingSound;
                                    const char* soundTypeName = nullptr;
                                    
                                    switch (soundChoice) {
                                        case '1': newSound = AppConfig::AxisCrossingSound::PLUCK; soundTypeName = "Pluck"; break;
                                        case '2': newSound = AppConfig::AxisCrossingSound::SWEEP; soundTypeName = "Sweep"; break;
                                        case '3': newSound = AppConfig::AxisCrossingSound::CHIRP; soundTypeName = "Chirp"; break;
                                        case '4': newSound = AppConfig::AxisCrossingSound::BELL; soundTypeName = "Bell"; break;
                                        case '5': newSound = AppConfig::AxisCrossingSound::PERCUSSION; soundTypeName = "Percussion"; break;
                                    }
                                    
                                    if (soundTypeName) {
                                        smith->setAxisCrossingSound(newSound);
                                        cfg.axis_crossing_sound = newSound;
                                        axisCrossingSound = newSound;
                                        print("\n" + translation.format("SMITH_CONFIG_AXIS_EVENTS_SOUND_SET",
                                            "[Axis crossing sound set to: {0}]", soundTypeName) + "\n");
                                        saveSettings();
                                        settingsChanged = true;
                                    }
                                }
                                break;
                        }
                    }
                    break;
                
                case 's':  // Surround configuration (only if not stereo)
                    if (audioCap != AudioCapability::STEREO_ONLY) {
                        print("S\n");
                        bool surroundChanged = runSurroundConfigurationScreen(smith);
                        if (surroundChanged) {
                            settingsChanged = true;
                        }
                    }
                    break;
                
                case 'p':  // Preview
                    {
                        print("P\n" + translation.get("SMITH_CONFIG_PREVIEW_MSG", 
                            "[Playing preview... Press any key to stop]") + "\n");
                        
                        // TODO: Implement preview playback
                        print(translation.get("SMITH_CONFIG_PREVIEW_TODO", "[Preview not yet implemented in this screen]") + "\n");
                        consoleInput->getch();
                    }
                    break;
                
                case 'h':  // Help
                    print(formatHeading(translation.get("SMITH_CONFIG_HELP_TITLE", "Smith Audio Configuration Help")));
                    print(translation.get("SMITH_CONFIG_HELP_VOLUME", "V - Set volume for Smith ambient cues (10-100%)") + "\n");
                    print(translation.get("SMITH_CONFIG_HELP_NOISE", "N - Change sound type (Pink/White/Brown/Sine)") + "\n");
                    print(translation.get("SMITH_CONFIG_HELP_PINK", "    Pink: Warm, filtered (default, recommended)") + "\n");
                    print(translation.get("SMITH_CONFIG_HELP_WHITE", "    White: Bright, full spectrum") + "\n");
                    print(translation.get("SMITH_CONFIG_HELP_BROWN", "    Brown: Dark, low frequencies") + "\n");
                    print(translation.get("SMITH_CONFIG_HELP_SINE", "    Sine: Pure tone, musical") + "\n");
                    print(translation.get("SMITH_CONFIG_HELP_PREVIEW", "P - Preview current settings") + "\n");
                    break;
                
                case 27:  // ESC
                    running = false;
                    break;
            }
            
            if (running) {
                print(getPromptWithDepth("SMITH_CONFIG_PROMPT", 4) + " ");
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    print("\n");
    return settingsChanged;
}

// Surround sound configuration screen
bool ConsoleUI::runSurroundConfigurationScreen(SmithVisualizer* smith) {
    clearScreen();
    print(formatHeading(translation.get("SURROUND_CONFIG_TITLE", "Surround Sound Configuration")));
    
    if (!smith) {
        print(translation.get("SURROUND_CONFIG_NO_SMITH", "[Error: Smith visualizer not available]") + "\n");
        return false;
    }
    
    bool running = true;
    bool settingsChanged = false;
    
    while (running) {
        // Display current settings
        print("\n" + translation.get("SURROUND_CONFIG_CURRENT", "Current Settings:") + "\n");
        print(translation.format("SURROUND_CONFIG_FRONT_DIST", "  Front distance factor: {0}%", cfg.surround_front_distance) + "\n");
        print(translation.format("SURROUND_CONFIG_BACK_DIST", "  Back distance factor: {0}%", cfg.surround_back_distance) + "\n");
        print(translation.format("SURROUND_CONFIG_SIDE_DIST", "  Side distance factor: {0}%", cfg.surround_side_distance) + "\n");
        print(translation.format("SURROUND_CONFIG_CENTER_STR", "  Center channel strength: {0}%", cfg.surround_center_strength) + "\n");
        print(translation.format("SURROUND_CONFIG_FB_SEP", "  Front/Back separation: {0}%", cfg.surround_fb_separation) + "\n");
        print(translation.format("SURROUND_CONFIG_SIDE_EMP", "  Side channel emphasis: {0}%", cfg.surround_side_emphasis) + "\n");
        
        const char* curveName = "";
        switch (cfg.surround_fading_curve) {
            case AppConfig::SurroundFadingCurve::LINEAR: curveName = "Linear"; break;
            case AppConfig::SurroundFadingCurve::LOGARITHMIC: curveName = "Logarithmic"; break;
            case AppConfig::SurroundFadingCurve::EXPONENTIAL: curveName = "Exponential"; break;
            case AppConfig::SurroundFadingCurve::SINE: curveName = "Sine"; break;
        }
        print(translation.format("SURROUND_CONFIG_FADING", "  Fading curve: {0}", curveName) + "\n\n");
        
        print(translation.get("SURROUND_CONFIG_COMMANDS", "Commands:") + "\n");
        print(translation.get("SURROUND_CONFIG_FRONT_CMD", "  F - Configure Front distance (50-200%)") + "\n");
        print(translation.get("SURROUND_CONFIG_BACK_CMD", "  B - Configure Back distance (50-200%)") + "\n");
        print(translation.get("SURROUND_CONFIG_SIDE_CMD", "  I - Configure Side distance (50-200%)") + "\n");
        print(translation.get("SURROUND_CONFIG_CENTER_CMD", "  C - Configure Center strength (0-100%)") + "\n");
        print(translation.get("SURROUND_CONFIG_FB_SEP_CMD", "  S - Configure front/back Separation (50-200%)") + "\n");
        print(translation.get("SURROUND_CONFIG_SIDE_EMP_CMD", "  E - Configure side channel Emphasis (50-200%)") + "\n");
        print(translation.get("SURROUND_CONFIG_FADING_CMD", "  V - Configure fading curVe (Linear/Log/Exp/Sine)") + "\n");
        print(translation.get("SURROUND_CONFIG_RESET_CMD", "  R - Reset to defaults") + "\n");
        print(translation.get("HELP_COMMAND", "  H - Help") + "\n");
        print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
        
        print(getPromptWithDepth("SURROUND_CONFIG_PROMPT", 5) + " ");
        
        int ch = consoleInput->getch();
        if (ch == 0 || ch == 224) {
            if (consoleInput->kbhit()) consoleInput->getch();
            continue;
        }
        
        char key = static_cast<char>(ch);
        if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
        
        switch (key) {
            case 'f':  // Front distance
                {
                    print("F\n");
                    int value;
                    if (readNumericInput(translation.format("SURROUND_CONFIG_ENTER_FRONT", 
                        "Enter front distance factor (50-200%, current: {0}%):", cfg.surround_front_distance), value)) {
                        if (value >= 50 && value <= 200) {
                            cfg.surround_front_distance = value;
                            smith->setSurroundConfig(cfg.surround_front_distance, cfg.surround_back_distance,
                                                    cfg.surround_side_distance, cfg.surround_center_strength,
                                                    cfg.surround_fb_separation, cfg.surround_side_emphasis,
                                                    cfg.surround_fading_curve);
                            saveSettings();
                            settingsChanged = true;
                            print(translation.format("SURROUND_CONFIG_FRONT_SET", "[Front distance set to: {0}%]", value) + "\n");
                        } else {
                            print(translation.get("SURROUND_CONFIG_RANGE_ERROR", "[Error: Value must be between 50 and 200]") + "\n");
                        }
                    }
                }
                break;
                
            case 'b':  // Back distance
                {
                    print("B\n");
                    int value;
                    if (readNumericInput(translation.format("SURROUND_CONFIG_ENTER_BACK", 
                        "Enter back distance factor (50-200%, current: {0}%):", cfg.surround_back_distance), value)) {
                        if (value >= 50 && value <= 200) {
                            cfg.surround_back_distance = value;
                            smith->setSurroundConfig(cfg.surround_front_distance, cfg.surround_back_distance,
                                                    cfg.surround_side_distance, cfg.surround_center_strength,
                                                    cfg.surround_fb_separation, cfg.surround_side_emphasis,
                                                    cfg.surround_fading_curve);
                            saveSettings();
                            settingsChanged = true;
                            print(translation.format("SURROUND_CONFIG_BACK_SET", "[Back distance set to: {0}%]", value) + "\n");
                        } else {
                            print(translation.get("SURROUND_CONFIG_RANGE_ERROR", "[Error: Value must be between 50 and 200]") + "\n");
                        }
                    }
                }
                break;
                
            case 'i':  // Side distance
                {
                    print("I\n");
                    int value;
                    if (readNumericInput(translation.format("SURROUND_CONFIG_ENTER_SIDE", 
                        "Enter side distance factor (50-200%, current: {0}%):", cfg.surround_side_distance), value)) {
                        if (value >= 50 && value <= 200) {
                            cfg.surround_side_distance = value;
                            smith->setSurroundConfig(cfg.surround_front_distance, cfg.surround_back_distance,
                                                    cfg.surround_side_distance, cfg.surround_center_strength,
                                                    cfg.surround_fb_separation, cfg.surround_side_emphasis,
                                                    cfg.surround_fading_curve);
                            saveSettings();
                            settingsChanged = true;
                            print(translation.format("SURROUND_CONFIG_SIDE_SET", "[Side distance set to: {0}%]", value) + "\n");
                        } else {
                            print(translation.get("SURROUND_CONFIG_RANGE_ERROR", "[Error: Value must be between 50 and 200]") + "\n");
                        }
                    }
                }
                break;
                
            case 'c':  // Center strength
                {
                    print("C\n");
                    int value;
                    if (readNumericInput(translation.format("SURROUND_CONFIG_ENTER_CENTER", 
                        "Enter center strength (0-100%, current: {0}%):", cfg.surround_center_strength), value)) {
                        if (value >= 0 && value <= 100) {
                            cfg.surround_center_strength = value;
                            smith->setSurroundConfig(cfg.surround_front_distance, cfg.surround_back_distance,
                                                    cfg.surround_side_distance, cfg.surround_center_strength,
                                                    cfg.surround_fb_separation, cfg.surround_side_emphasis,
                                                    cfg.surround_fading_curve);
                            saveSettings();
                            settingsChanged = true;
                            print(translation.format("SURROUND_CONFIG_CENTER_SET", "[Center strength set to: {0}%]", value) + "\n");
                        } else {
                            print(translation.get("SURROUND_CONFIG_CENTER_ERROR", "[Error: Value must be between 0 and 100]") + "\n");
                        }
                    }
                }
                break;
                
            case 's':  // FB separation
                {
                    print("S\n");
                    int value;
                    if (readNumericInput(translation.format("SURROUND_CONFIG_ENTER_FB_SEP", 
                        "Enter front/back separation (50-200%, current: {0}%):", cfg.surround_fb_separation), value)) {
                        if (value >= 50 && value <= 200) {
                            cfg.surround_fb_separation = value;
                            smith->setSurroundConfig(cfg.surround_front_distance, cfg.surround_back_distance,
                                                    cfg.surround_side_distance, cfg.surround_center_strength,
                                                    cfg.surround_fb_separation, cfg.surround_side_emphasis,
                                                    cfg.surround_fading_curve);
                            saveSettings();
                            settingsChanged = true;
                            print(translation.format("SURROUND_CONFIG_FB_SEP_SET", "[F/B separation set to: {0}%]", value) + "\n");
                        } else {
                            print(translation.get("SURROUND_CONFIG_RANGE_ERROR", "[Error: Value must be between 50 and 200]") + "\n");
                        }
                    }
                }
                break;
                
            case 'e':  // Side emphasis
                {
                    print("E\n");
                    int value;
                    if (readNumericInput(translation.format("SURROUND_CONFIG_ENTER_SIDE_EMP", 
                        "Enter side channel emphasis (50-200%, current: {0}%):", cfg.surround_side_emphasis), value)) {
                        if (value >= 50 && value <= 200) {
                            cfg.surround_side_emphasis = value;
                            smith->setSurroundConfig(cfg.surround_front_distance, cfg.surround_back_distance,
                                                    cfg.surround_side_distance, cfg.surround_center_strength,
                                                    cfg.surround_fb_separation, cfg.surround_side_emphasis,
                                                    cfg.surround_fading_curve);
                            saveSettings();
                            settingsChanged = true;
                            print(translation.format("SURROUND_CONFIG_SIDE_EMP_SET", "[Side emphasis set to: {0}%]", value) + "\n");
                        } else {
                            print(translation.get("SURROUND_CONFIG_RANGE_ERROR", "[Error: Value must be between 50 and 200]") + "\n");
                        }
                    }
                }
                break;
                
            case 'v':  // Fading curve
                {
                    print("V\n\n" + formatSubHeading(translation.get("SURROUND_CONFIG_FADING_MENU", "Select Fading Curve")));
                    print(translation.get("SURROUND_CONFIG_FADING_1", "  1 - Linear (equal perceived movement)") + "\n");
                    print(translation.get("SURROUND_CONFIG_FADING_2", "  2 - Logarithmic (more emphasis on center)") + "\n");
                    print(translation.get("SURROUND_CONFIG_FADING_3", "  3 - Exponential (more emphasis on edges)") + "\n");
                    print(translation.get("SURROUND_CONFIG_FADING_4", "  4 - Sine (smooth, natural transition)") + "\n");
                    print(translation.get("MSG_PRESS_ESC_BACK", "Press ESC to go back") + "\n\n");
                    print(getPromptWithDepth("SURROUND_CONFIG_FADING_PROMPT", 6) + " ");
                    
                    int curveChoice = consoleInput->getch();
                    if (curveChoice >= '1' && curveChoice <= '4') {
                        AppConfig::SurroundFadingCurve newCurve;
                        const char* newCurveName = "";
                        
                        switch (curveChoice) {
                            case '1': 
                                newCurve = AppConfig::SurroundFadingCurve::LINEAR;
                                newCurveName = "Linear";
                                break;
                            case '2': 
                                newCurve = AppConfig::SurroundFadingCurve::LOGARITHMIC;
                                newCurveName = "Logarithmic";
                                break;
                            case '3': 
                                newCurve = AppConfig::SurroundFadingCurve::EXPONENTIAL;
                                newCurveName = "Exponential";
                                break;
                            case '4': 
                                newCurve = AppConfig::SurroundFadingCurve::SINE;
                                newCurveName = "Sine";
                                break;
                        }
                        
                        cfg.surround_fading_curve = newCurve;
                        smith->setSurroundConfig(cfg.surround_front_distance, cfg.surround_back_distance,
                                                cfg.surround_side_distance, cfg.surround_center_strength,
                                                cfg.surround_fb_separation, cfg.surround_side_emphasis,
                                                cfg.surround_fading_curve);
                        saveSettings();
                        settingsChanged = true;
                        print("\n" + translation.format("SURROUND_CONFIG_FADING_SET", 
                            "[Fading curve changed to: {0}]", newCurveName) + "\n");
                    }
                }
                break;
                
            case 'r':  // Reset to defaults
                {
                    print("R\n");
                    cfg.surround_front_distance = 100;
                    cfg.surround_back_distance = 100;
                    cfg.surround_side_distance = 100;
                    cfg.surround_center_strength = 50;
                    cfg.surround_fb_separation = 100;
                    cfg.surround_side_emphasis = 100;
                    cfg.surround_fading_curve = AppConfig::SurroundFadingCurve::LINEAR;
                    
                    smith->setSurroundConfig(cfg.surround_front_distance, cfg.surround_back_distance,
                                            cfg.surround_side_distance, cfg.surround_center_strength,
                                            cfg.surround_fb_separation, cfg.surround_side_emphasis,
                                            cfg.surround_fading_curve);
                    saveSettings();
                    settingsChanged = true;
                    print(translation.get("SURROUND_CONFIG_RESET_DONE", "[Surround settings reset to defaults]") + "\n");
                }
                break;
                
            case 'h':  // Help
                {
                    print("H\n\n" + formatSubHeading(translation.get("SURROUND_CONFIG_HELP_TITLE", "Surround Configuration Help")));
                    print(translation.get("SURROUND_CONFIG_HELP_DESC", 
                        "Configure spatial audio parameters for optimal Smith diagram localization.") + "\n\n");
                    print(translation.get("SURROUND_CONFIG_HELP_FRONT", 
                        "F - Front distance: Perceived distance to front speakers (50-200%)") + "\n");
                    print(translation.get("SURROUND_CONFIG_HELP_BACK", 
                        "B - Back distance: Perceived distance to back speakers (50-200%)") + "\n");
                    print(translation.get("SURROUND_CONFIG_HELP_SIDE", 
                        "I - Side distance: Perceived distance to side speakers (50-200%)") + "\n");
                    print(translation.get("SURROUND_CONFIG_HELP_CENTER", 
                        "C - Center strength: Center channel strength (0-100%, 0=off)") + "\n");
                    print(translation.get("SURROUND_CONFIG_HELP_FB_SEP", 
                        "S - F/B separation: Front/back distinction enhancement (50-200%)") + "\n");
                    print(translation.get("SURROUND_CONFIG_HELP_SIDE_EMP", 
                        "E - Side emphasis: 90° localization emphasis (50-200%)") + "\n");
                    print(translation.get("SURROUND_CONFIG_HELP_FADING", 
                        "V - Fading curve: Spatial movement perception curve") + "\n");
                    print(translation.get("SURROUND_CONFIG_HELP_RESET", 
                        "R - Reset all settings to defaults") + "\n\n");
                    print(translation.get("SURROUND_CONFIG_HELP_TIP", 
                        "Tip: Start with defaults, then adjust front/back separation if") + "\n");
                    print(translation.get("SURROUND_CONFIG_HELP_TIP2", 
                        "     front and back sound too similar.") + "\n");
                }
                break;
                
            case 27:  // ESC
                running = false;
                break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    print("\n");
    return settingsChanged;
}

// Duration configuration submenu
bool ConsoleUI::runDurationConfigurationScreen(AcousticAnalyzer* analyzer) {
    clearScreen();
    bool running = true;
    
    while (running) {
        // Display submenu
        print(formatHeading(translation.get("DURATION_CONFIG_TITLE", "Duration Configuration")));
        print(translation.format("DURATION_CONFIG_POINT_CURRENT", "Current point duration: {0} ms", cfg.dotted_duration_ms) + "\n");
        print(translation.format("DURATION_CONFIG_PAUSE_CURRENT", "Current pause duration: {0} ms", cfg.dotted_pause_ms) + "\n");
        int total = cfg.dotted_duration_ms + cfg.dotted_pause_ms;
        print(translation.format("DURATION_CONFIG_TOTAL_CURRENT", "Total duration per point: {0} ms", total) + "\n");
        print("\n");
        print(translation.get("DURATION_CONFIG_COMMANDS", "Commands:") + "\n");
        print(translation.get("DURATION_CONFIG_POINT_CMD", "  P - Configure Point duration (30-500 ms)") + "\n");
        print(translation.get("DURATION_CONFIG_PAUSE_CMD", "  U - Configure paUse duration (10-500 ms)") + "\n");
        print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
        print(getPromptWithDepth("AUDIO_CONFIG_PROMPT", 3) + " ");
        
        // Read input
        int ch = 0;
        bool hasInput = false;
        
        // Check for web interface input first
        if (webServer && webServer->isRunning() && webServer->hasInput()) {
            std::string webInput = webServer->readInput();
            if (!webInput.empty()) {
                if (logger) logger->log("UI_DURATION_CONFIG", "Web input received: [" + webInput + "]");
                
                // Handle web input
                if (webInput[0] == '\x1B') {
                    ch = 27;  // ESC key
                } else {
                    ch = static_cast<unsigned char>(webInput[0]);
                }
                hasInput = true;
                
                if (logger) logger->log("UI_DURATION_CONFIG", "Web input mapped to ch: " + std::to_string(ch));
            }
        }
        
        // Check for keyboard input if no web input
        if (!hasInput) {
            ch = consoleInput->getch();
            hasInput = true;
        }
        
        if (ch == 27) {  // ESC - Back
            running = false;
        } else if (ch == 'p' || ch == 'P') {  // Configure point duration
            print("P\n\n");
            print(formatHeading(translation.get("DURATION_POINT_TITLE", "Configure Point Duration")));
            print(translation.get("DURATION_POINT_DESC", 
                "Point duration controls how long each measurement point sounds in dotted playback mode.\n"
                "Longer durations provide more time to hear each point clearly.\n"
                "Shorter durations allow faster sweeps but may be harder to distinguish.") + "\n\n");
            print(translation.get("DURATION_POINT_VALID_RANGE", "Valid range: 30 - 500 ms") + "\n\n");
            
            int duration;
            if (readNumericInput(translation.get("DURATION_POINT_PROMPT", "Enter point duration in milliseconds (30-500), or press ESC to cancel:"), duration, 4)) {
                if (duration >= 30 && duration <= 500) {
                    cfg.dotted_duration_ms = duration;
                    print("\n" + translation.format("DURATION_POINT_SET", "[Point duration set to {0} ms]", duration) + "\n");
                    print(translation.get("DURATION_POINT_APPLIED", "[Change applied immediately - no restart needed]") + "\n");
                    saveSettings();
                    if (analyzer) {
                        analyzer->setDottedDurationMs(duration);
                    }
                } else {
                    print("\n" + translation.get("DURATION_POINT_ERROR", "[Error: Point duration must be between 30 and 500 ms]") + "\n");
                }
            }
        } else if (ch == 'u' || ch == 'U') {  // Configure pause duration
            print("U\n\n");
            print(formatHeading(translation.get("DURATION_PAUSE_TITLE", "Configure Pause Duration")));
            print(translation.get("DURATION_PAUSE_DESC", 
                "Pause duration controls the silence between measurement points in dotted playback mode.\n"
                "Longer pauses provide more time to distinguish between points.\n"
                "Shorter pauses allow faster sweeps.") + "\n\n");
            print(translation.get("DURATION_PAUSE_VALID_RANGE", "Valid range: 10 - 500 ms") + "\n\n");
            
            int pause;
            if (readNumericInput(translation.get("DURATION_PAUSE_PROMPT", "Enter pause duration in milliseconds (10-500), or press ESC to cancel:"), pause, 4)) {
                if (pause >= 10 && pause <= 500) {
                    cfg.dotted_pause_ms = pause;
                    print("\n" + translation.format("DURATION_PAUSE_SET", "[Pause duration set to {0} ms]", pause) + "\n");
                    print(translation.get("DURATION_PAUSE_APPLIED", "[Change applied immediately - no restart needed]") + "\n");
                    saveSettings();
                    if (analyzer) {
                        analyzer->setDottedPauseMs(pause);
                    }
                } else {
                    print("\n" + translation.get("DURATION_PAUSE_ERROR", "[Error: Pause duration must be between 10 and 500 ms]") + "\n");
                }
            }
        }
    }
    
    return false;  // No changes that require re-initialization
}

bool ConsoleUI::runFreezePauseConfigurationScreen(AcousticAnalyzer* analyzer) {
    clearScreen();
    print(formatHeading(translation.get("FREEZE_PAUSE_CONFIG_TITLE", "Freeze Point Pause Configuration")));
    print(translation.format("FREEZE_PAUSE_CONFIG_CURRENT", "Current freeze point pause: {0} ms", cfg.freeze_point_pause_ms) + "\n");
    print(translation.get("FREEZE_PAUSE_CONFIG_DESC", 
        "Freeze point pause controls the silence between repeated points in freeze mode with dotted playback.\n"
        "Longer pauses provide more time to hear each repetition clearly.\n"
        "Shorter pauses create a faster rhythm.") + "\n\n");
    print(translation.get("FREEZE_PAUSE_CONFIG_VALID_RANGE", "Valid range: 50 - 2000 ms") + "\n\n");
    
    int pause;
    if (readNumericInput(translation.get("FREEZE_PAUSE_CONFIG_PROMPT", "Enter freeze point pause in milliseconds (50-2000), or press ESC to cancel:"), pause, 4)) {
        if (pause >= 50 && pause <= 2000) {
            cfg.freeze_point_pause_ms = pause;
            print("\n" + translation.format("FREEZE_PAUSE_CONFIG_SET", "[Freeze point pause set to {0} ms]", pause) + "\n");
            print(translation.get("FREEZE_PAUSE_CONFIG_APPLIED", "[Change applied immediately - no restart needed]") + "\n");
            saveSettings();
            if (analyzer) {
                analyzer->setFreezePointPauseMs(pause);
            }
        } else {
            print("\n" + translation.get("FREEZE_PAUSE_CONFIG_ERROR", "[Error: Freeze point pause must be between 50 and 2000 ms]") + "\n");
        }
    }
    
    return false;  // No changes that require re-initialization
}

bool ConsoleUI::runLoopPauseConfigurationScreen(AcousticAnalyzer* analyzer) {
    clearScreen();
    print(formatHeading(translation.get("LOOP_PAUSE_CONFIG_TITLE", "Loop Pause Configuration")));
    print(translation.format("LOOP_PAUSE_CONFIG_CURRENT", "Current loop pause: {0} ms", cfg.loop_pause_ms) + "\n");
    print(translation.get("LOOP_PAUSE_CONFIG_DESC", 
        "Loop pause adds a configurable silence before the loop repeats.\n"
        "This helps separate iterations and makes it easier to follow complex curves.\n"
        "Longer pauses provide more breathing room between loops.") + "\n\n");
    print(translation.get("LOOP_PAUSE_CONFIG_VALID_RANGE", "Valid range: 0 - 5000 ms (0 = no pause)") + "\n\n");
    
    int pause;
    if (readNumericInput(translation.get("LOOP_PAUSE_CONFIG_PROMPT", "Enter loop pause in milliseconds (0-5000), or press ESC to cancel:"), pause, 4)) {
        if (pause >= 0 && pause <= 5000) {
            cfg.loop_pause_ms = pause;
            print("\n" + translation.format("LOOP_PAUSE_CONFIG_SET", "[Loop pause set to {0} ms]", pause) + "\n");
            print(translation.get("LOOP_PAUSE_CONFIG_APPLIED", "[Change applied immediately - no restart needed]") + "\n");
            saveSettings();
            if (analyzer) {
                analyzer->setLoopPauseMs(pause);
            }
        } else {
            print("\n" + translation.get("LOOP_PAUSE_CONFIG_ERROR", "[Error: Loop pause must be between 0 and 5000 ms]") + "\n");
        }
    }
    
    return false;  // No changes that require re-initialization
}

bool ConsoleUI::runInvertedLoopGapConfigurationScreen(AcousticAnalyzer* analyzer) {
    clearScreen();
    print(formatHeading(translation.get("INVERTED_LOOP_GAP_CONFIG_TITLE", "Inverted Loop Gap Configuration")));
    print(translation.format("INVERTED_LOOP_GAP_CONFIG_CURRENT", "Current inverted loop gap: {0} ms", cfg.inverted_loop_gap_ms) + "\n");
    print(translation.get("INVERTED_LOOP_GAP_CONFIG_DESC", 
        "When loop is inverted, the section inside the loop markers is skipped.\n"
        "This gap duration controls how long the silent playback lasts while skipping.\n"
        "With X-axis ruler enabled, accelerated clicks will play during this gap.\n"
        "Longer gaps make it easier to perceive the skipped section.") + "\n\n");
    print(translation.get("INVERTED_LOOP_GAP_CONFIG_VALID_RANGE", "Valid range: 0 - 5000 ms (0 = instant skip)") + "\n\n");
    
    int gap;
    if (readNumericInput(translation.get("INVERTED_LOOP_GAP_CONFIG_PROMPT", "Enter gap duration in milliseconds (0-5000), or press ESC to cancel:"), gap, 4)) {
        if (gap >= 0 && gap <= 5000) {
            cfg.inverted_loop_gap_ms = gap;
            print("\n" + translation.format("INVERTED_LOOP_GAP_CONFIG_SET", "[Inverted loop gap set to {0} ms]", gap) + "\n");
            print(translation.get("INVERTED_LOOP_GAP_CONFIG_APPLIED", "[Change applied immediately - no restart needed]") + "\n");
            saveSettings();
            if (analyzer) {
                analyzer->setInvertedLoopGapMs(gap);
            }
        } else {
            print("\n" + translation.get("INVERTED_LOOP_GAP_CONFIG_ERROR", "[Error: Gap duration must be between 0 and 5000 ms]") + "\n");
        }
    }
    
    return false;  // No changes that require re-initialization
}

// ============================================================================
// MIDI Controller Configuration Screen
// ============================================================================

bool ConsoleUI::runMidiControllerConfigurationScreen(AcousticAnalyzer* analyzer) {
    clearScreen();
    print(formatHeading(translation.get("MIDI_CTRL_CONFIG_TITLE", "MIDI Controller Configuration")));
    
    // Show current status
    print(translation.format("MIDI_CTRL_CONFIG_ENABLED", "MIDI Controller: {0}", 
        cfg.midi_controller_enabled ? "Enabled" : "Disabled") + "\n");
    
    if (!cfg.midi_controller_device_name.empty()) {
        print(translation.format("MIDI_CTRL_CONFIG_DEVICE", "Device: {0}", cfg.midi_controller_device_name) + "\n");
    }
    if (!cfg.midi_controller_preset.empty()) {
        print(translation.format("MIDI_CTRL_CONFIG_PRESET", "Preset: {0}", cfg.midi_controller_preset) + "\n");
    }
    print(translation.format("MIDI_CTRL_CONFIG_FEEDBACK", "Motor fader feedback: {0}", 
        cfg.midi_controller_feedback ? "ON" : "OFF") + "\n");
    print(translation.format("MIDI_CTRL_CONFIG_FREEZE_TOUCH", "Freeze by touch: {0}", 
        cfg.midi_controller_freeze_by_touch ? "ON" : "OFF") + "\n\n");
    
    print(translation.get("MIDI_CTRL_CONFIG_COMMANDS", "Commands:") + "\n");
    print(translation.get("MIDI_CTRL_CONFIG_TOGGLE_CMD", "  E - Enable/Disable MIDI controller") + "\n");
    print(translation.get("MIDI_CTRL_CONFIG_DEVICE_CMD", "  D - Select MIDI input Device") + "\n");
    print(translation.get("MIDI_CTRL_CONFIG_PRESET_CMD", "  P - Select mapping Preset") + "\n");
    print(translation.get("MIDI_CTRL_CONFIG_MAPPING_CMD", "  M - Edit Mappings") + "\n");
    print(translation.get("MIDI_CTRL_CONFIG_FEEDBACK_CMD", "  F - Toggle motor Fader feedback") + "\n");
    print(translation.get("MIDI_CTRL_CONFIG_FREEZE_TOUCH_CMD", "  T - Toggle freeze by Touch") + "\n");
    print(translation.get("MIDI_CTRL_CONFIG_SAVE_CMD", "  S - Save current mapping as new preset") + "\n");
    print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
    
    print(getPromptWithDepth("MIDI_CTRL_CONFIG_PROMPT", 4) + " ");
    
    bool running = true;
    while (running) {
        if (!consoleInput->kbhit()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        
        int ch = consoleInput->getch();
        // Skip extended key sequences
        if (ch == 0 || ch == 224) {
            if (consoleInput->kbhit()) consoleInput->getch();
            continue;
        }
        
        char key = static_cast<char>(ch);
        if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
        
        switch (key) {
            case 'e':  // Toggle enable
                cfg.midi_controller_enabled = !cfg.midi_controller_enabled;
                print("\n" + translation.format("MIDI_CTRL_TOGGLED", "[MIDI Controller: {0}]", 
                    cfg.midi_controller_enabled ? "Enabled" : "Disabled") + "\n");
                saveSettings();
                break;
                
            case 'd':  // Select device
                {
                    print("\n" + translation.get("MIDI_CTRL_SCANNING", "[Scanning for MIDI input devices...]") + "\n");
                    
                    if (!midiControllerMgr) {
                        print(translation.get("MIDI_CTRL_NOT_AVAILABLE", "[MIDI controller support not available on this platform]") + "\n");
                        break;
                    }
                    
                    auto devices = midiControllerMgr->listDevices();
                    if (devices.empty()) {
                        print(translation.get("MIDI_CTRL_NO_DEVICES", "[No MIDI input devices found]") + "\n");
                        print(translation.get("MIDI_CTRL_CONNECT_HINT", "[Connect a MIDI controller and try again]") + "\n");
                        break;
                    }
                    
                    print(translation.format("MIDI_CTRL_FOUND_DEVICES", "[Found {0} MIDI input device(s):]", static_cast<int>(devices.size())) + "\n");
                    for (size_t i = 0; i < devices.size(); i++) {
                        std::string marker = (devices[i].id == cfg.midi_controller_device_id) ? " *" : "";
                        print("  " + std::to_string(i + 1) + " - " + devices[i].name + marker + "\n");
                    }
                    print(translation.get("MIDI_CTRL_SELECT_DEVICE", "Select device number (or ESC to cancel): "));
                    
                    int devCh = consoleInput->getch();
                    if (devCh >= '1' && devCh <= '9') {
                        int idx = devCh - '1';
                        if (idx < static_cast<int>(devices.size())) {
                            cfg.midi_controller_device_id = devices[idx].id;
                            cfg.midi_controller_device_name = devices[idx].name;
                            print("\n" + translation.format("MIDI_CTRL_DEVICE_SELECTED", "[Selected: {0}]", devices[idx].name) + "\n");
                            saveSettings();
                        }
                    }
                }
                break;
                
            case 'p':  // Select preset
                {
                    print("\n" + translation.get("MIDI_CTRL_PRESETS", "[Available mapping presets:]") + "\n");
                    
                    if (!midiControllerMgr) break;
                    
                    auto presets = midiControllerMgr->listPresetFiles("midi");
                    if (presets.empty()) {
                        print(translation.get("MIDI_CTRL_NO_PRESETS", "[No preset files found in midi/ directory]") + "\n");
                        break;
                    }
                    
                    for (size_t i = 0; i < presets.size(); i++) {
                        std::string marker = (presets[i] == cfg.midi_controller_preset) ? " *" : "";
                        print("  " + std::to_string(i + 1) + " - " + presets[i] + marker + "\n");
                    }
                    print(translation.get("MIDI_CTRL_SELECT_PRESET", "Select preset number (or ESC to cancel): "));
                    
                    int presetCh = consoleInput->getch();
                    if (presetCh >= '1' && presetCh <= '9') {
                        int idx = presetCh - '1';
                        if (idx < static_cast<int>(presets.size())) {
                            cfg.midi_controller_preset = presets[idx];
                            if (midiControllerMgr->loadMappingsFromFile("midi/" + presets[idx])) {
                                print("\n" + translation.format("MIDI_CTRL_PRESET_LOADED", "[Loaded preset: {0}]", presets[idx]) + "\n");
                                auto& preset = midiControllerMgr->getCurrentPreset();
                                print(translation.format("MIDI_CTRL_PRESET_INFO", "[{0}: {1} mappings for {2}]", 
                                    preset.name, static_cast<int>(preset.mappings.size()), preset.controllerName) + "\n");
                            } else {
                                print(translation.get("MIDI_CTRL_PRESET_FAILED", "[Failed to load preset]") + "\n");
                            }
                            saveSettings();
                        }
                    }
                }
                break;
                
            case 'm':  // Edit mappings
                {
                    navStack.push("midi_mapping");
                    runMidiMappingScreen();
                    navStack.pop();
                }
                break;
                
            case 'f':  // Toggle feedback
                cfg.midi_controller_feedback = !cfg.midi_controller_feedback;
                print("\n" + translation.format("MIDI_CTRL_FEEDBACK_TOGGLED", "[Motor fader feedback: {0}]", 
                    cfg.midi_controller_feedback ? "ON" : "OFF") + "\n");
                saveSettings();
                break;
                
            case 't':  // Toggle freeze by touch
                cfg.midi_controller_freeze_by_touch = !cfg.midi_controller_freeze_by_touch;
                print("\n" + translation.format("MIDI_CTRL_FREEZE_TOUCH_TOGGLED", "[Freeze by touch: {0}]", 
                    cfg.midi_controller_freeze_by_touch ? "ON" : "OFF") + "\n");
                saveSettings();
                break;
                
            case 's':  // Save preset
                {
                    if (!midiControllerMgr) break;
                    
                    print("\n");
                    auto result = readRawLineInput(
                        translation.get("MIDI_CTRL_SAVE_FILENAME", "Enter filename for preset (without path/extension): "), 
                        "custom_mapping");
                    
                    if (!result.cancelled && !result.value.empty()) {
                        std::string filepath = "midi/" + result.value + ".cfg";
                        if (midiControllerMgr->saveMappingsToFile(filepath)) {
                            print(translation.format("MIDI_CTRL_PRESET_SAVED", "[Preset saved: {0}]", filepath) + "\n");
                        } else {
                            print(translation.get("MIDI_CTRL_SAVE_FAILED", "[Failed to save preset]") + "\n");
                        }
                    }
                }
                break;
                
            case 27:  // ESC
                running = false;
                break;
        }
        
        if (running) {
            print(getPromptWithDepth("MIDI_CTRL_CONFIG_PROMPT", 4) + " ");
        }
    }
    
    return false;
}

bool ConsoleUI::runMidiMappingScreen() {
    clearScreen();
    print(formatHeading(translation.get("MIDI_MAPPING_TITLE", "MIDI Mapping Editor")));
    
    if (!midiControllerMgr) {
        print(translation.get("MIDI_CTRL_NOT_AVAILABLE", "[MIDI controller support not available]") + "\n");
        return false;
    }
    
    auto& preset = midiControllerMgr->getCurrentPreset();
    
    // Display current mappings
    print(translation.format("MIDI_MAPPING_PRESET_NAME", "Preset: {0}", preset.name) + "\n");
    print(translation.format("MIDI_MAPPING_COUNT", "Total mappings: {0}", static_cast<int>(preset.mappings.size())) + "\n\n");
    
    // Show Note On mappings
    print(translation.get("MIDI_MAPPING_NOTE_ON_HEADER", "--- Note On Mappings (Button presses) ---") + "\n");
    int noteOnCount = 0;
    for (const auto& m : preset.mappings) {
        if (m.triggerType == MidiMessageType::NOTE_ON && m.command != MidiAppCommand::NONE) {
            print("  Ch" + std::to_string(m.channel) + " Note " + std::to_string(m.number) + 
                  " -> " + MidiControllerManager::getCommandName(m.command) + 
                  " (" + m.description + ")\n");
            noteOnCount++;
        }
    }
    if (noteOnCount == 0) print("  (none)\n");
    
    // Show CC mappings
    print("\n" + translation.get("MIDI_MAPPING_CC_HEADER", "--- CC Mappings (Faders/Knobs) ---") + "\n");
    int ccCount = 0;
    for (const auto& m : preset.mappings) {
        if (m.triggerType == MidiMessageType::CONTROL_CHANGE && m.ccFunction != MidiCCFunction::NONE) {
            print("  Ch" + std::to_string(m.channel) + " CC " + std::to_string(m.number) + 
                  " -> " + MidiControllerManager::getCCFunctionName(m.ccFunction) + 
                  " (" + m.description + ")\n");
            ccCount++;
        }
    }
    if (ccCount == 0) print("  (none)\n");
    
    print("\n" + translation.get("MIDI_MAPPING_COMMANDS", "Commands:") + "\n");
    print(translation.get("MIDI_MAPPING_ADD_NOTE_CMD", "  N - Add Note On mapping") + "\n");
    print(translation.get("MIDI_MAPPING_ADD_CC_CMD", "  C - Add CC mapping") + "\n");
    print(translation.get("MIDI_MAPPING_CLEAR_CMD", "  X - Clear all mappings") + "\n");
    print(translation.get("MIDI_MAPPING_LEARN_CMD", "  L - Learn: Press a button/move a fader on your controller") + "\n");
    print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
    
    print(getPromptWithDepth("MIDI_MAPPING_PROMPT", 5) + " ");
    
    bool running = true;
    while (running) {
        if (!consoleInput->kbhit()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        
        int ch = consoleInput->getch();
        if (ch == 0 || ch == 224) {
            if (consoleInput->kbhit()) consoleInput->getch();
            continue;
        }
        
        char key = static_cast<char>(ch);
        if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
        
        switch (key) {
            case 'n':  // Add Note On mapping
                {
                    print("\n" + translation.get("MIDI_MAPPING_ADD_NOTE_INFO", 
                        "[Adding Note On mapping]") + "\n");
                    
                    // Ask for note number
                    int noteNum;
                    if (!readNumericInput(translation.get("MIDI_MAPPING_ENTER_NOTE", "Enter MIDI note number (0-127):"), noteNum, 5)) break;
                    if (noteNum < 0 || noteNum > 127) { print(translation.get("MIDI_MAPPING_INVALID_NOTE", "[Invalid note number]") + "\n"); break; }
                    
                    // Show available commands
                    print(translation.get("MIDI_MAPPING_AVAILABLE_CMDS", "Available commands:") + "\n");
                    print(translation.get("MIDI_MAPPING_CMD_LIST_NOTES",
                        "  1 - Play/Pause\n  2 - Stop\n  3 - Freeze\n  4 - Toggle Smooth/Dotted\n"
                        "  5 - Toggle Loop\n  6 - Toggle Loop Zoom\n  7 - Toggle Loop Invert\n"
                        "  8 - Toggle Continuous\n  9 - Set Loop Left\n  0 - Set Loop Right\n"));
                    
                    int cmdCh = consoleInput->getch();
                    MidiAppCommand cmd = MidiAppCommand::NONE;
                    switch (cmdCh) {
                        case '1': cmd = MidiAppCommand::PLAY_PAUSE; break;
                        case '2': cmd = MidiAppCommand::STOP; break;
                        case '3': cmd = MidiAppCommand::FREEZE; break;
                        case '4': cmd = MidiAppCommand::TOGGLE_SMOOTH_DOTTED; break;
                        case '5': cmd = MidiAppCommand::TOGGLE_LOOP; break;
                        case '6': cmd = MidiAppCommand::TOGGLE_LOOP_ZOOM; break;
                        case '7': cmd = MidiAppCommand::TOGGLE_LOOP_INVERT; break;
                        case '8': cmd = MidiAppCommand::TOGGLE_CONTINUOUS; break;
                        case '9': cmd = MidiAppCommand::SET_LOOP_LEFT; break;
                        case '0': cmd = MidiAppCommand::SET_LOOP_RIGHT; break;
                    }
                    
                    if (cmd != MidiAppCommand::NONE) {
                        MidiMapping mapping;
                        mapping.triggerType = MidiMessageType::NOTE_ON;
                        mapping.channel = 0;
                        mapping.number = static_cast<uint8_t>(noteNum);
                        mapping.command = cmd;
                        mapping.ccFunction = MidiCCFunction::NONE;
                        mapping.description = "User mapping: Note " + std::to_string(noteNum);
                        midiControllerMgr->addMapping(mapping);
                        print("\n" + translation.format("MIDI_MAPPING_ADDED_NOTE", 
                              "[Mapping added: Note {0} -> {1}]", noteNum, 
                              MidiControllerManager::getCommandName(cmd)) + "\n");
                    }
                }
                break;
                
            case 'c':  // Add CC mapping
                {
                    print("\n" + translation.get("MIDI_MAPPING_ADD_CC_INFO", 
                        "[Adding CC mapping]") + "\n");
                    
                    int ccNum;
                    if (!readNumericInput(translation.get("MIDI_MAPPING_ENTER_CC", "Enter CC number (0-127):"), ccNum, 5)) break;
                    if (ccNum < 0 || ccNum > 127) { print(translation.get("MIDI_MAPPING_INVALID_CC", "[Invalid CC number]") + "\n"); break; }
                    
                    print(translation.get("MIDI_MAPPING_AVAILABLE_CC_FUNCS", "Available CC functions:") + "\n");
                    print(translation.get("MIDI_MAPPING_CC_FUNC_LIST",
                        "  1 - Master Volume\n  2 - Curve 1 Volume\n  3 - Curve 2 Volume\n"
                        "  4 - Curve 3 Volume\n  5 - Curve 4 Volume\n  6 - Curve 5 Volume\n"
                        "  7 - X-Axis Position (Motor Fader)\n  8 - SWR Value (Motor Fader)\n"
                        "  9 - RL Value (Motor Fader)\n  0 - |Z| Value (Motor Fader)\n"));
                    
                    int funcCh = consoleInput->getch();
                    MidiCCFunction func = MidiCCFunction::NONE;
                    switch (funcCh) {
                        case '1': func = MidiCCFunction::MASTER_VOLUME; break;
                        case '2': func = MidiCCFunction::CURVE_VOLUME_1; break;
                        case '3': func = MidiCCFunction::CURVE_VOLUME_2; break;
                        case '4': func = MidiCCFunction::CURVE_VOLUME_3; break;
                        case '5': func = MidiCCFunction::CURVE_VOLUME_4; break;
                        case '6': func = MidiCCFunction::CURVE_VOLUME_5; break;
                        case '7': func = MidiCCFunction::POSITION_X_AXIS; break;
                        case '8': func = MidiCCFunction::CURVE_VALUE_SWR; break;
                        case '9': func = MidiCCFunction::CURVE_VALUE_RL; break;
                        case '0': func = MidiCCFunction::CURVE_VALUE_IMPEDANCE; break;
                    }
                    
                    if (func != MidiCCFunction::NONE) {
                        MidiMapping mapping;
                        mapping.triggerType = MidiMessageType::CONTROL_CHANGE;
                        mapping.channel = 0;
                        mapping.number = static_cast<uint8_t>(ccNum);
                        mapping.command = MidiAppCommand::NONE;
                        mapping.ccFunction = func;
                        mapping.description = "User mapping: CC " + std::to_string(ccNum);
                        midiControllerMgr->addMapping(mapping);
                        print("\n" + translation.format("MIDI_MAPPING_ADDED_CC", 
                              "[Mapping added: CC {0} -> {1}]", ccNum,
                              MidiControllerManager::getCCFunctionName(func)) + "\n");
                    }
                }
                break;
                
            case 'x':  // Clear all
                print("\n" + translation.get("MIDI_MAPPING_CONFIRM_CLEAR", "[Clear all mappings? (y/n)]") + "\n");
                if (consoleInput->getch() == 'y') {
                    midiControllerMgr->clearMappings();
                    print(translation.get("MIDI_MAPPING_CLEARED", "[All mappings cleared]") + "\n");
                }
                break;
                
            case 'l':  // MIDI Learn
                {
                    print("\n" + translation.get("MIDI_MAPPING_LEARN_INFO",
                        "[MIDI Learn: Press a button or move a fader on your controller...\n"
                        " The received MIDI event will be shown. Press ESC to cancel.]") + "\n");
                    
                    if (!midiControllerMgr->isDeviceOpen()) {
                        print(translation.get("MIDI_CTRL_NOT_CONNECTED", "[No MIDI controller connected. Select a device first.]") + "\n");
                        break;
                    }
                    
                    // Temporarily suppress normal command processing during learn
                    midiControllerMgr->setCommandCallback([](MidiAppCommand) {}); 
                    midiControllerMgr->setCCValueCallback([](MidiCCFunction, int) {});
                    
                    print(translation.get("MIDI_MAPPING_WAITING", "[Waiting for MIDI input... Check debug log for events. Press ESC to cancel]") + "\n");
                    
                    // Wait for user to press ESC; MIDI events will be visible in debug log
                    while (!consoleInput->kbhit()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    int learnCh = consoleInput->getch();
                    if (learnCh == 27) {
                        print(translation.get("MIDI_MAPPING_LEARN_CANCELLED", "[Learn cancelled]") + "\n");
                    }
                    
                    // Restore callbacks to no-op (they will be properly set when returning to acoustic analysis)
                    midiControllerMgr->setCommandCallback(nullptr);
                    midiControllerMgr->setCCValueCallback(nullptr);
                }
                break;
                
            case 27:  // ESC
                running = false;
                break;
        }
        
        if (running) {
            print(getPromptWithDepth("MIDI_MAPPING_PROMPT", 5) + " ");
        }
    }
    
    return false;
}

bool ConsoleUI::runAudioConfigurationScreen(AcousticAnalyzer* analyzer) {
    clearScreen();
    print(formatHeading(translation.get("AUDIO_CONFIG_TITLE", "Audio Configuration")));
    print(translation.format("AUDIO_CONFIG_ENGINE", "Current engine: {0}", (cfg.audio_engine == AudioEngineType::MIDI ? "MIDI" : "Synthesizer")) + "\n");
    
    // Show synthesizer waveforms if synthesizer engine is active
    if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
        print("Synthesizer waveforms:\n");
        const char* curveNames[] = {"SWR", "RL", "|Z|", "X", "Phase"};
        for (int i = 0; i < 5; i++) {
            print(std::string("  ") + curveNames[i] + ": " + getWaveformName(cfg.synth_waveforms[i]) + "\n");
        }
    }
    
    print(translation.format("AUDIO_CONFIG_FREQ_RANGE", "Frequency range: {0} - {1} Hz", cfg.synth_min_freq_hz, cfg.synth_max_freq_hz) + "\n");
    
    // Show MIDI mode if MIDI engine is active
    if (cfg.audio_engine == AudioEngineType::MIDI) {
        const char* modeText = (cfg.midi_playback_mode == MIDIPlaybackMode::GLIDING) ? "GLIDING" : "DOTTED";
        print(translation.format("AUDIO_CONFIG_MIDI_MODE_CURRENT", "MIDI playback mode: {0}", modeText) + "\n");
        
        // Show MIDI interpolated panning status
        const char* panModeText = cfg.midi_interpolated_pan_mode ? "ON" : "OFF";
        print(translation.format("AUDIO_CONFIG_MIDI_INTERP_PAN_STATUS", "Interpolated panning: {0}", panModeText) + "\n");
        print(translation.format("AUDIO_CONFIG_MIDI_INTERP_STRENGTH_STATUS", "Interpolation strength: {0}", cfg.midi_interpolation_strength) + "\n");
    }
    print("\n");
    
    print(translation.get("AUDIO_CONFIG_COMMANDS", "Commands:") + "\n");
    print(translation.get("AUDIO_CONFIG_ENGINE_CMD", "  E - Toggle audio Engine (Synthesizer/MIDI)") + "\n");
    print(translation.get("AUDIO_CONFIG_RANGE_CMD", "  R - Configure frequency Range for Synthesizer") + "\n");
    print(translation.get("AUDIO_CONFIG_DURATION_CMD", "  D - Configure Duration settings (Point/Pause duration)") + "\n");
    print(translation.get("AUDIO_CONFIG_FREEZE_CMD", "  F - Configure Freeze point pause duration") + "\n");
    print(translation.get("AUDIO_CONFIG_LOOP_PAUSE_CMD", "  U - Configure Loop pause duration") + "\n");
    print(translation.get("AUDIO_CONFIG_INVERTED_LOOP_GAP_CMD", "  G - Configure inverted loop Gap duration") + "\n");
    
    // Reactance effects menu (MIDI mode only)
    if (cfg.audio_engine == AudioEngineType::MIDI) {
        print(translation.get("AUDIO_CONFIG_REACTANCE_EFFECTS_CMD", "  O - Reactance effects cOnfiguration (MIDI mode)") + "\n");
    }
    
    // Context-sensitive commands for 1-5 keys
    if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
        print(translation.get("AUDIO_CONFIG_SYNTH_CURVE_CMD", "  1-5 - Configure Waveform for each curve (Synthesizer mode)") + "\n");
    } else {
        print(translation.get("AUDIO_CONFIG_MIDI_CMD", "  1-5 - Configure MIDI instrument for each curve") + "\n");
    }
    
    print(translation.get("AUDIO_CONFIG_VOLUME_CMD", "  V - Configure Volume for individual curves") + "\n");
    print(translation.get("AUDIO_CONFIG_SMITH_CMD", "  S - Configure Smith diagram audio settings") + "\n");
    print(translation.get("AUDIO_CONFIG_RULER_CMD", "  A - Configure Y-Axis Ruler settings") + "\n");
    print(translation.get("AUDIO_CONFIG_X_RULER_CMD", "  X - Configure X-Axis Ruler settings") + "\n");
    print(translation.get("AUDIO_CONFIG_STATUS_LINE_CMD", "  N - Configure Status Line settings") + "\n");
    print(translation.get("AUDIO_CONFIG_SPATIAL_WIZARD_CMD", "  W - Run Spatial Audio Calibration Wizard (recommended!)") + "\n");
    print(translation.get("AUDIO_CONFIG_MIDI_CTRL_CMD", "  C - MIDI Controller Configuration") + "\n");
    
    // MIDI-specific commands for interpolated panning
    if (cfg.audio_engine == AudioEngineType::MIDI) {
        print(translation.get("AUDIO_CONFIG_MIDI_INTERP_PAN_CMD", "  I - Toggle MIDI interpolated panning") + "\n");
        print(translation.get("AUDIO_CONFIG_MIDI_INTERP_STRENGTH_CMD", "  T - Set MIDI interpolation strength") + "\n");
    }
    
    // Context-sensitive preview message
    if (cfg.audio_engine == AudioEngineType::MIDI) {
        print(translation.get("AUDIO_CONFIG_PREVIEW_CMD", "  P - Preview current MIDI configuration") + "\n");
    } else {
        print(translation.get("AUDIO_CONFIG_PREVIEW_SYNTH_CMD", "  P - Preview current Synthesizer configuration") + "\n");
    }
    
    print(translation.get("HELP_COMMAND", "  H - Help") + "\n");
    print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
    
    bool engineTypeChanged = false;
    bool instrumentsChanged = false;
    bool freqRangeChanged = false;
    bool waveformsChanged = false;
    bool running = true;
    
    // Display prompt once before the loop
    print(getPromptWithDepth("AUDIO_CONFIG_PROMPT", 3) + " ");
    
    while (running) {
        int ch = 0;
        bool hasInput = false;
        
        // Check for web interface input first
        if (webServer && webServer->isRunning() && webServer->hasInput()) {
            std::string webInput = webServer->readInput();
            if (!webInput.empty()) {
                if (logger) logger->log("UI_AUDIO_CONFIG", "Web input received: [" + webInput + "]");
                
                // Handle web input - treat escape sequences and regular keys
                if (webInput[0] == '\x1B') {
                    ch = 27;  // ESC key
                } else {
                    // Regular character
                    ch = static_cast<unsigned char>(webInput[0]);
                }
                hasInput = true;
                
                if (logger) logger->log("UI_AUDIO_CONFIG", "Web input mapped to ch: " + std::to_string(ch));
            }
        }
        
        // Check for keyboard input if no web input
        if (!hasInput && consoleInput->kbhit()) {
            ch = consoleInput->getch();
            
            // Skip extended key sequences (arrow keys, function keys, etc.)
            if (ch == 0 || ch == 224) {
                // Read and discard the second byte
                if (consoleInput->kbhit()) consoleInput->getch();
                continue;  // Ignore arrow keys and other extended keys
            }
            hasInput = true;
        }
        
        // If we have input, process it
        if (hasInput) {
            char key = static_cast<char>(ch);
            if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
            
            switch (key) {
                case 'e':  // Toggle engine
                    {
                        if (cfg.audio_engine == AudioEngineType::MIDI) {
                            cfg.audio_engine = AudioEngineType::SYNTHESIZER;
                            print(translation.get("ENGINE_SWITCHED_SYNTH", "[Engine switched to: Synthesizer]") + "\n");
                        } else {
                            cfg.audio_engine = AudioEngineType::MIDI;
                            print(translation.get("ENGINE_SWITCHED_MIDI", "[Engine switched to: MIDI]") + "\n");
                        }
                        engineTypeChanged = true;
                        saveSettings();
                    }
                    break;
                
                case 'r':  // Configure frequency range
                    {
                        print(formatHeading(translation.get("SYNTH_RANGE_TITLE", "Configure Frequency Range for Synthesizer")));
                        print(translation.format("SYNTH_RANGE_CURRENT", "Current range: {0} - {1} Hz\n", cfg.synth_min_freq_hz, cfg.synth_max_freq_hz));
                        print(translation.get("SYNTH_RANGE_EXPLANATION", "The frequency range determines how measurement values are mapped to audio.") + "\n");
                        print(translation.get("SYNTH_RANGE_EXAMPLE_INTRO", "For example, for SWR:") + "\n");
                        print(translation.get("SYNTH_RANGE_EXAMPLE_MIN", "  - SWR 1.0 (perfect) = minimum frequency") + "\n");
                        print(translation.get("SYNTH_RANGE_EXAMPLE_MAX", "  - SWR 20.0 (poor)   = maximum frequency") + "\n\n");
                        print(translation.format("SYNTH_RANGE_VALID", "Valid range: {0} - {1} Hz (human hearing range)\n", SYNTH_MIN_FREQ_HZ_LIMIT, SYNTH_MAX_FREQ_HZ_LIMIT) + "\n");
                        
                        // Phase 3: Sequential input with backtracking
                        // Escape in step 2 returns to step 1 instead of exiting
                        int minFreq = 0;
                        int maxFreq = 0;
                        std::string minInput;
                        std::string maxInput;
                        
                        // Sequential input loop - allows backtracking
                        while (true) {
                            // Step 1: Get minimum frequency
                            print(translation.format("SYNTH_ENTER_MIN_FREQ", "Enter minimum frequency in Hz ({0}-{1}), or press ESC to cancel:", SYNTH_MIN_FREQ_HZ_LIMIT, SYNTH_MAX_FREQ_HZ_LIMIT) + " " + getDepthIndicator(4) + " ");
                            
                            // Use raw mode numeric input
                            minInput.clear();
                            bool step1Cancelled = false;
                            bool inputting = true;
                            while (inputting) {
                                if (consoleInput->kbhit()) {
                                    int ch = consoleInput->getch();
                                    if (ch == 27) {  // ESC
                                        print("\n[Cancelled]\n");
                                        step1Cancelled = true;
                                        inputting = false;
                                    } else if (ch == '\r' || ch == '\n') {  // Enter
                                        print("\n");
                                        inputting = false;
                                    } else if (ch >= '0' && ch <= '9') {
                                        minInput += static_cast<char>(ch);
                                        print(std::string(1, ch));
                                    } else if (ch == 8 && !minInput.empty()) {  // Backspace
                                        minInput.pop_back();
                                        print("\b \b");
                                    }
                                }
                            }
                            
                            if (step1Cancelled || minInput.empty()) {
                                goto frequency_range_cancelled;  // Exit to parent menu
                            }
                            
                            try {
                                minFreq = std::stoi(minInput);
                            } catch (...) {
                                print(translation.get("SYNTH_ERROR_INVALID_NUMBER", "\n[Error: Invalid number]") + "\n");
                                continue;  // Ask again
                            }
                            
                            if (minFreq < SYNTH_MIN_FREQ_HZ_LIMIT || minFreq > SYNTH_MAX_FREQ_HZ_LIMIT) {
                                print(translation.format("SYNTH_ERROR_MIN_FREQ_RANGE", "\n[Error: Minimum frequency must be between {0} and {1} Hz]", SYNTH_MIN_FREQ_HZ_LIMIT, SYNTH_MAX_FREQ_HZ_LIMIT) + "\n");
                                continue;  // Ask again
                            }
                            
                            // Step 2: Get maximum frequency (with backtracking support)
                            while (true) {
                                print(translation.format("SYNTH_ENTER_MAX_FREQ", "\nEnter maximum frequency in Hz ({0}-{1}), or press ESC to go back:", SYNTH_MIN_FREQ_HZ_LIMIT, SYNTH_MAX_FREQ_HZ_LIMIT) + " " + getDepthIndicator(4) + " ");
                                
                                maxInput.clear();
                                bool step2Cancelled = false;
                                inputting = true;
                                while (inputting) {
                                    if (consoleInput->kbhit()) {
                                        int ch = consoleInput->getch();
                                        if (ch == 27) {  // ESC
                                            print("\n" + translation.get("GOING_BACK", "[Going back to previous step]") + "\n");
                                            step2Cancelled = true;
                                            inputting = false;
                                        } else if (ch == '\r' || ch == '\n') {  // Enter
                                            print("\n");
                                            inputting = false;
                                        } else if (ch >= '0' && ch <= '9') {
                                            maxInput += static_cast<char>(ch);
                                            print(std::string(1, ch));
                                        } else if (ch == 8 && !maxInput.empty()) {  // Backspace
                                            maxInput.pop_back();
                                            print("\b \b");
                                        }
                                    }
                                }
                                
                                if (step2Cancelled || maxInput.empty()) {
                                    break;  // Go back to step 1
                                }
                                
                                try {
                                    maxFreq = std::stoi(maxInput);
                                } catch (...) {
                                    print(translation.get("SYNTH_ERROR_INVALID_NUMBER", "\n[Error: Invalid number]") + "\n");
                                    continue;  // Ask again
                                }
                                
                                if (maxFreq < SYNTH_MIN_FREQ_HZ_LIMIT || maxFreq > SYNTH_MAX_FREQ_HZ_LIMIT) {
                                    print(translation.format("SYNTH_ERROR_MAX_FREQ_RANGE", "\n[Error: Maximum frequency must be between {0} and {1} Hz]", SYNTH_MIN_FREQ_HZ_LIMIT, SYNTH_MAX_FREQ_HZ_LIMIT) + "\n");
                                    continue;  // Ask again
                                }
                                
                                if (minFreq >= maxFreq) {
                                    print(translation.get("SYNTH_ERROR_MAX_LESS_MIN", "\n[Error: Maximum frequency must be greater than minimum frequency]") + "\n");
                                    continue;  // Ask again
                                }
                                
                                // Both steps complete successfully
                                cfg.synth_min_freq_hz = minFreq;
                                cfg.synth_max_freq_hz = maxFreq;
                                print(translation.format("SYNTH_FREQ_RANGE_SET", "\n[Frequency range set to {0} - {1} Hz]", minFreq, maxFreq) + "\n");
                                print(translation.get("SYNTH_CHANGES_IMMEDIATE", "[Changes will take effect immediately in the synthesizer]") + "\n");
                                saveSettings();
                                // Apply the change immediately to the analyzer
                                if (analyzer) {
                                    analyzer->setFrequencyRange(minFreq, maxFreq);
                                    // Also update MIDI engine if it's active
                                    auto engine = analyzer->getAudioEngine();
                                    if (engine && std::string(engine->getName()) == "MIDI") {
                                        auto midiEngine = std::dynamic_pointer_cast<MIDIEngine>(engine);
                                        if (midiEngine) {
                                            midiEngine->setSynthFrequencyRange(minFreq, maxFreq);
                                        }
                                    }
                                }
                                freqRangeChanged = true;
                                goto frequency_range_complete;  // Exit both loops
                            }
                            // If we get here, user pressed Escape in step 2, loop back to step 1
                        }
                        
                        frequency_range_complete:  // Successful completion
                        frequency_range_cancelled:  // User cancelled
                        (void)0;  // Empty statement for label
                    }
                    break;
                
                case 'd':  // Duration configuration submenu
                    print("D\n");
                    runDurationConfigurationScreen(analyzer);
                    break;
                
                case 'f':  // Freeze point pause configuration
                    print("F\n");
                    runFreezePauseConfigurationScreen(analyzer);
                    break;
                
                case 'u':  // Loop pause configuration
                    print("U\n");
                    runLoopPauseConfigurationScreen(analyzer);
                    break;
                
                case 'g':  // Inverted loop gap configuration
                    print("G\n");
                    runInvertedLoopGapConfigurationScreen(analyzer);
                    break;
                
                case 'o':  // Reactance effects configuration (MIDI mode only)
                    if (cfg.audio_engine == AudioEngineType::MIDI) {
                        print("O\n");
                        runReactanceEffectsConfigurationScreen(analyzer);
                    }
                    break;
                
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                    {
                        int curveIndex = key - '1';
                        const char* curveNames[] = {"SWR", "Return Loss", "Impedance |Z|", "Reactance X", "Phase"};
                        
                        // Context-sensitive: Waveform for Synthesizer, Instrument for MIDI
                        if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
                            // Configure waveform for Synthesizer mode
                            print(formatHeading(translation.format("WAVEFORM_CONFIG_TITLE", "Configure {0} Waveform", curveNames[curveIndex])));
                            print(translation.format("WAVEFORM_CONFIG_CURRENT", "Current: {0}\n", getWaveformName(cfg.synth_waveforms[curveIndex])) + "\n");
                            print(translation.get("WAVEFORM_CONFIG_AVAILABLE", "Available waveforms:") + "\n");
                            print(translation.get("WAVEFORM_SINE", "  0 = Sine (smooth, pure tone)") + "\n");
                            print(translation.get("WAVEFORM_SQUARE", "  1 = Square (harsh, buzzing tone)") + "\n");
                            print(translation.get("WAVEFORM_TRIANGLE", "  2 = Triangle (mellow, hollow tone)") + "\n");
                            print(translation.get("WAVEFORM_SAWTOOTH", "  3 = Sawtooth (bright, rising harmonics)") + "\n");
                            print(translation.get("WAVEFORM_SAWTOOTH_INV", "  4 = Sawtooth Inv (bright, falling harmonics)") + "\n");
                            print(translation.get("WAVEFORM_PULSE", "  5 = Pulse (narrow, percussive)") + "\n");
                            print(translation.get("SYNTH_ENTER_WAVEFORM", "\nEnter waveform number (0-5), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                            
                            std::string input;
                            bool inputting = true;
                            while (inputting) {
                                if (consoleInput->kbhit()) {
                                    int ch = consoleInput->getch();
                                    if (ch == 27) {  // ESC
                                        print("\n[Cancelled]\n");
                                        if (logger) {
                                            char msg[512];
                                            snprintf(msg, sizeof(msg), "Waveform change for %s cancelled", curveNames[curveIndex]);
                                            logger->log("SYNTH", msg);
                                        }
                                        inputting = false;
                                    } else if (ch == '\r' || ch == '\n') {  // Enter
                                        try {
                                            int waveformNum = std::stoi(input);
                                            if (waveformNum >= 0 && waveformNum < WAVEFORM_NAMES_COUNT) {
                                                Waveform oldWaveform = cfg.synth_waveforms[curveIndex];
                                                cfg.synth_waveforms[curveIndex] = static_cast<Waveform>(waveformNum);
                                                print("\n[" + std::string(curveNames[curveIndex]) + " waveform set to: " + getWaveformName(static_cast<Waveform>(waveformNum)) + "]\n");
                                                if (logger) {
                                                    char msg[512];
                                                    snprintf(msg, sizeof(msg), "%s waveform changed from %s to %s", 
                                                        curveNames[curveIndex], 
                                                        getWaveformName(oldWaveform),
                                                        getWaveformName(static_cast<Waveform>(waveformNum)));
                                                    logger->log("SYNTH", msg);
                                                }
                                                saveSettings();
                                                waveformsChanged = true;
                                            } else {
                                                print("\n[Error: Number must be 0-" + std::to_string(WAVEFORM_NAMES_COUNT - 1) + "]\n");
                                            }
                                        } catch (...) {
                                            print(translation.get("SYNTH_ERROR_INVALID_NUMBER", "\n[Error: Invalid number]") + "\n");
                                        }
                                        inputting = false;
                                    } else if (ch >= '0' && ch <= '9') {
                                        input += static_cast<char>(ch);
                                        print(std::string(1, ch));
                                    } else if (ch == 8 && !input.empty()) {  // Backspace
                                        input.pop_back();
                                        print("\b \b");
                                    }
                                }
                            }
                        } else {
                            // Configure MIDI instrument for MIDI mode
                            // Mode-aware: edit the appropriate preset based on current playback mode
                            const char* modeText = (cfg.midi_playback_mode == MIDIPlaybackMode::GLIDING) ? "GLIDING" : "DOTTED";
                            std::array<int, 5>& activePreset = (cfg.midi_playback_mode == MIDIPlaybackMode::GLIDING) 
                                ? cfg.midi_instruments_gliding : cfg.midi_instruments_dotted;
                            
                            print(formatHeading(translation.format("MIDI_CONFIG_TITLE", 
                                "Configure {0} MIDI Instrument ({1} mode)", curveNames[curveIndex], modeText)));
                            print(translation.format("MIDI_CONFIG_CURRENT", "Current: {0} ({1})", 
                                MIDI_INSTRUMENT_NAMES[activePreset[curveIndex]], activePreset[curveIndex]) + "\n");
                            print(translation.get("MIDI_CONFIG_COMMANDS", "Commands:") + "\n");
                            print(translation.get("MIDI_CONFIG_ENTER_NUMBER", "  Enter instrument number (0-127)") + "\n");
                            print(translation.get("MIDI_CONFIG_SHOW_LIST", "  L - Show list of available MIDI instruments") + "\n");
                            print(translation.get("BACK_ESC", "  ESC - Cancel") + "\n");
                            print(getPromptWithDepth("MIDI_CONFIG_PROMPT", 4) + " ");
                        
                            std::string input;
                            bool inputting = true;
                            while (inputting) {
                                if (consoleInput->kbhit()) {
                                    int ch = consoleInput->getch();
                                    if (ch == 27) {  // ESC
                                        print("\n" + translation.get("CANCELLED", "[Cancelled]") + "\n");
                                        if (logger) {
                                            char msg[512];
                                            snprintf(msg, sizeof(msg), "Instrument change for %s cancelled", curveNames[curveIndex]);
                                            logger->log("MIDI", msg);
                                        }
                                        inputting = false;
                                    } else if (ch == 'l' || ch == 'L') {  // Show instrument list
                                        print("L\n\n" + formatSubHeading(translation.get("MIDI_CONFIG_LIST_TITLE", 
                                            "MIDI Instrument List (General MIDI)")));
                                        // Display instruments in groups for better readability
                                        print(translation.get("MIDI_CONFIG_PIANO", "Piano (0-7):") + "\n");
                                        for (int i = 0; i <= 7; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_CHROMATIC", "Chromatic Percussion (8-15):") + "\n");
                                        for (int i = 8; i <= 15; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_ORGAN", "Organ (16-23):") + "\n");
                                        for (int i = 16; i <= 23; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_GUITAR", "Guitar (24-31):") + "\n");
                                        for (int i = 24; i <= 31; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_BASS", "Bass (32-39):") + "\n");
                                        for (int i = 32; i <= 39; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_STRINGS", "Strings (40-47):") + "\n");
                                        for (int i = 40; i <= 47; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_ENSEMBLE", "Ensemble (48-55):") + "\n");
                                        for (int i = 48; i <= 55; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_BRASS", "Brass (56-63):") + "\n");
                                        for (int i = 56; i <= 63; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_REED", "Reed (64-71):") + "\n");
                                        for (int i = 64; i <= 71; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_PIPE", "Pipe (72-79):") + "\n");
                                        for (int i = 72; i <= 79; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_SYNTH_LEAD", "Synth Lead (80-87):") + "\n");
                                        for (int i = 80; i <= 87; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_SYNTH_PAD", "Synth Pad (88-95):") + "\n");
                                        for (int i = 88; i <= 95; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_SYNTH_FX", "Synth Effects (96-103):") + "\n");
                                        for (int i = 96; i <= 103; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_ETHNIC", "Ethnic (104-111):") + "\n");
                                        for (int i = 104; i <= 111; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_PERCUSSIVE", "Percussive (112-119):") + "\n");
                                        for (int i = 112; i <= 119; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_SOUND_FX", "Sound Effects (120-127):") + "\n");
                                        for (int i = 120; i <= 127; i++) {
                                            print("  " + std::to_string(i) + " - " + MIDI_INSTRUMENT_NAMES[i] + "\n");
                                        }
                                        print("\n" + translation.get("MIDI_CONFIG_ENTER_AGAIN", 
                                            "Enter instrument number (0-127), L to see list again, or ESC to cancel:") + " ");
                                        std::string depthIndicator = getDepthIndicator(4);
                                        print(depthIndicator + " ");
                                        input.clear();  // Clear any partial input
                                    } else if (ch == '\r' || ch == '\n') {  // Enter
                                        if (!input.empty()) {
                                            try {
                                                int program = std::stoi(input);
                                                if (program >= 0 && program <= 127) {
                                                    int oldProgram = activePreset[curveIndex];
                                                    activePreset[curveIndex] = program;
                                                    // Also update the current instruments array
                                                    cfg.midi_instruments[curveIndex] = program;
                                                    print("\n" + translation.format("MIDI_CONFIG_SET", 
                                                        "[{0} instrument ({1} mode) set to: {2}]", 
                                                        curveNames[curveIndex], modeText, MIDI_INSTRUMENT_NAMES[program]) + "\n");
                                                    if (logger) {
                                                        char msg[512];
                                                        snprintf(msg, sizeof(msg), "%s instrument (%s mode) changed from %d to %d (%s)", 
                                                            curveNames[curveIndex], modeText, oldProgram, program, MIDI_INSTRUMENT_NAMES[program]);
                                                        logger->log("MIDI", msg);
                                                    }
                                                    saveSettings();
                                                    instrumentsChanged = true;  // Instruments changed
                                                    inputting = false;
                                                } else {
                                                    print("\n" + translation.get("MIDI_CONFIG_ERROR_RANGE", 
                                                        "[Error: Number must be 0-127]") + "\n");
                                                    std::string prompt = translation.get("MIDI_CONFIG_ENTER_AGAIN", 
                                                        "Enter instrument number (0-127), L to see list, or ESC to cancel:");
                                                    std::string depthIndicator = getDepthIndicator(4);
                                                    print(prompt + " " + depthIndicator + " ");
                                                    input.clear();
                                                    if (logger) logger->log("MIDI", "Invalid instrument number (out of range 0-127)");
                                                }
                                            } catch (...) {
                                                print("\n" + translation.get("MIDI_CONFIG_ERROR_INVALID", 
                                                    "[Error: Invalid number]") + "\n");
                                                std::string prompt = translation.get("MIDI_CONFIG_ENTER_AGAIN", 
                                                    "Enter instrument number (0-127), L to see list, or ESC to cancel:");
                                                std::string depthIndicator = getDepthIndicator(4);
                                                print(prompt + " " + depthIndicator + " ");
                                                input.clear();
                                                if (logger) logger->log("MIDI", "Invalid instrument number (parse error)");
                                            }
                                        }
                                    } else if (ch >= '0' && ch <= '9') {
                                        input += static_cast<char>(ch);
                                        print(std::string(1, ch));
                                    } else if (ch == 8 && !input.empty()) {  // Backspace
                                        input.pop_back();
                                        print("\b \b");
                                    }
                                }
                            }
                        }
                    }
                    break;
                
                case 'p':  // Preview
                    {
                        // Context-aware preview: check current audio engine type
                        if (cfg.audio_engine == AudioEngineType::MIDI) {
                            // MIDI preview
                            print(translation.get("PREVIEW_MIDI_START", "\n[Preview: Playing each curve with current MIDI instrument...]") + "\n");
                            
                            // Close the main audio engine temporarily to prevent device conflict (if analyzer is available)
                            std::shared_ptr<IAudioEngine> mainEngine;
                            bool needsReopen = false;
                            if (analyzer) {
                                mainEngine = analyzer->getAudioEngine();
                                if (mainEngine && mainEngine->isOpen() && std::string(mainEngine->getName()) == "MIDI") {
                                    if (logger) logger->log("MIDI", "Temporarily closing main MIDI engine for preview");
                                    mainEngine->close();
                                    needsReopen = true;
                                }
                            }
                            
                            auto previewEngine = std::make_shared<MIDIEngine>();
                            previewEngine->setLogger(logger);  // Set logger for MIDI engine debug output
                            if (previewEngine->open()) {
                                // Log successful MIDI engine opening for preview
                                if (logger) logger->log("MIDI", "MIDI engine opened successfully for preview");
                                const char* curveNames[] = {"SWR", "Return Loss", "Impedance |Z|", "Reactance X", "Phase"};
                                for (int i = 0; i < 5; i++) {
                                    previewEngine->setCurveInstrument(i, cfg.midi_instruments[i]);
                                    print(std::string("[") + curveNames[i] + ": " + MIDI_INSTRUMENT_NAMES[cfg.midi_instruments[i]] + "]\n");
                                    previewEngine->playPreview(cfg.midi_instruments[i], 500);
                                    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // Pause between previews
                                }
                                previewEngine->close();
                                print(translation.get("PREVIEW_COMPLETE", "[Preview complete]") + "\n");
                            } else {
                                // Log MIDI engine failure to debug log
                                if (logger) logger->log("MIDI", "Failed to open MIDI device for preview");
                                print(translation.get("MIDI_ERROR_PREVIEW_FAILED", "[Error: Could not open MIDI device for preview]") + "\n");
                            }
                            
                            // Reopen the main engine if we closed it
                            if (needsReopen && mainEngine) {
                                if (logger) logger->log("MIDI", "Reopening main MIDI engine after preview");
                                if (!mainEngine->open()) {
                                    if (logger) logger->log("MIDI", "Warning: Failed to reopen main MIDI engine after preview");
                                    print(translation.get("MIDI_ERROR_REOPEN_FAILED", 
                                        "[Warning: Failed to reopen MIDI engine after preview. You may need to reconfigure audio settings.]") + "\n");
                                }
                            }
                        } else {
                            // Synthesizer preview
                            print(translation.get("PREVIEW_SYNTH_START", "\n[Preview: Playing each curve with current Synthesizer waveform...]") + "\n");
                            
                            auto previewEngine = std::make_shared<SynthesizerEngine>();
                            if (previewEngine->open()) {
                                const char* curveNames[] = {"SWR", "Return Loss", "Impedance |Z|", "Reactance X", "Phase"};
                                for (int i = 0; i < 5; i++) {
                                    previewEngine->setCurveWaveform(i, cfg.synth_waveforms[i]);
                                    print(std::string("[") + curveNames[i] + ": " + getWaveformName(cfg.synth_waveforms[i]) + "]\n");
                                    previewEngine->playPreview(i, 500);
                                    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // Pause between previews
                                }
                                previewEngine->close();
                                print(translation.get("PREVIEW_COMPLETE", "[Preview complete]") + "\n");
                            } else {
                                print(translation.get("SYNTH_ERROR_PREVIEW_FAILED", "[Error: Could not open Synthesizer for preview]") + "\n");
                            }
                        }
                    }
                    break;
                
                case 'v':  // Volume configuration
                    {
                        print(formatHeading(translation.get("VOLUME_CONFIG_TITLE", "Configure Curve Volume")));
                        const char* curveNames[] = {"SWR", "Return Loss", "Impedance |Z|", "Reactance X", "Phase"};
                        
                        // Display current audio engine mode
                        std::string engineName = (cfg.audio_engine == AudioEngineType::SYNTHESIZER) ? "Synthesizer" : "MIDI";
                        print(translation.format("VOLUME_ACTIVE_ENGINE", "Active audio engine: {0}", engineName) + "\n");
                        print(translation.format("VOLUME_MASTER", "Master volume (M): {0}%", cfg.master_volume) + "\n");
                        print(translation.format("VOLUME_RULER", "Ruler volume (R): {0}%", cfg.ruler_volume) + "\n");
                        print(translation.format("VOLUME_X_RULER", "X-axis ruler volume (X): {0}%", cfg.x_axis_ruler_volume) + "\n\n");
                        
                        print(translation.get("VOLUME_CONFIG_CURRENT_VOLUMES", "Current volumes:") + "\n");
                        for (int i = 0; i < 5; i++) {
                            int volume = (cfg.audio_engine == AudioEngineType::SYNTHESIZER) 
                                ? cfg.curve_volume_synth[i] : cfg.curve_volume_midi[i];
                            print("  " + std::to_string(i+1) + ". " + curveNames[i] + ": " + std::to_string(volume) + "%\n");
                        }
                        print("\n" + translation.get("VOLUME_CONFIG_ENTER_CURVE", "Enter curve number (1-5), M for Master volume, R for Ruler volume, X for X-axis ruler volume, or press ESC to cancel:") + " ");
                        std::string depthIndicator = getDepthIndicator(4);
                        print(depthIndicator + " ");
                        
                        std::string curveInput;
                        bool inputting = true;
                        while (inputting) {
                            if (consoleInput->kbhit()) {
                                int ch = consoleInput->getch();
                                if (ch == 27) {  // ESC
                                    print("\n" + translation.get("CANCELLED", "[Cancelled]") + "\n");
                                    inputting = false;
                                } else if (ch == 'm' || ch == 'M') {  // Master volume
                                    print("M\n\n");
                                    std::string prompt = translation.get("VOLUME_CONFIG_ENTER_MASTER", 
                                        "Enter master volume (0-100%), or press ESC to cancel:");
                                    std::string depthIndicator = getDepthIndicator(5);
                                    print(prompt + " " + depthIndicator + " ");
                                    
                                    std::string volumeInput;
                                    bool volumeInputting = true;
                                    while (volumeInputting) {
                                        if (consoleInput->kbhit()) {
                                            int vch = consoleInput->getch();
                                            if (vch == 27) {  // ESC
                                                print("\n" + translation.get("CANCELLED", "[Cancelled]") + "\n");
                                                volumeInputting = false;
                                                inputting = false;
                                            } else if (vch == '\r' || vch == '\n') {  // Enter
                                                if (!volumeInput.empty()) {
                                                    try {
                                                        int volume = std::stoi(volumeInput);
                                                        if (volume >= 0 && volume <= 100) {
                                                            int oldVolume = cfg.master_volume;
                                                            cfg.master_volume = volume;
                                                            print("\n" + translation.format("VOLUME_CONFIG_MASTER_SET", 
                                                                "[Master volume set to: {0}%]", volume) + "\n");
                                                            if (logger) {
                                                                char msg[512];
                                                                snprintf(msg, sizeof(msg), "Master volume changed from %d%% to %d%%", 
                                                                    oldVolume, volume);
                                                                logger->log("AUDIO", msg);
                                                            }
                                                            saveSettings();
                                                            // Update analyzer if available
                                                            if (analyzer) {
                                                                analyzer->setMasterVolume(volume);
                                                            }
                                                            volumeInputting = false;
                                                            inputting = false;
                                                        } else {
                                                            print("\n" + translation.get("VOLUME_CONFIG_ERROR_VOLUME", 
                                                                "[Error: Volume must be between 0 and 100]") + "\n");
                                                            std::string prompt = translation.get("VOLUME_CONFIG_ENTER_MASTER", 
                                                                "Enter master volume (0-100%), or press ESC to cancel:");
                                                            std::string depthIndicator = getDepthIndicator(5);
                                                            print(prompt + " " + depthIndicator + " ");
                                                            volumeInput.clear();
                                                        }
                                                    } catch (...) {
                                                        print("\n" + translation.get("ERROR_INVALID_SELECTION", 
                                                            "[Error: Invalid number]") + "\n");
                                                        std::string prompt = translation.get("VOLUME_CONFIG_ENTER_MASTER", 
                                                            "Enter master volume (0-100%), or press ESC to cancel:");
                                                        std::string depthIndicator = getDepthIndicator(5);
                                                        print(prompt + " " + depthIndicator + " ");
                                                        volumeInput.clear();
                                                    }
                                                }
                                            } else if (vch >= '0' && vch <= '9') {
                                                volumeInput += static_cast<char>(vch);
                                                print(std::string(1, vch));
                                            } else if (vch == 8 && !volumeInput.empty()) {  // Backspace
                                                volumeInput.pop_back();
                                                print("\b \b");
                                            }
                                        }
                                    }
                                } else if (ch == 'r' || ch == 'R') {  // Ruler volume
                                    print("R\n\n");
                                    std::string prompt = translation.get("VOLUME_CONFIG_ENTER_RULER", 
                                        "Enter ruler volume (0-100%), or press ESC to cancel:");
                                    std::string depthIndicator = getDepthIndicator(5);
                                    print(prompt + " " + depthIndicator + " ");
                                    
                                    std::string volumeInput;
                                    bool volumeInputting = true;
                                    while (volumeInputting) {
                                        if (consoleInput->kbhit()) {
                                            int vch = consoleInput->getch();
                                            if (vch == 27) {  // ESC
                                                print("\n" + translation.get("CANCELLED", "[Cancelled]") + "\n");
                                                volumeInputting = false;
                                                inputting = false;
                                            } else if (vch == '\r' || vch == '\n') {  // Enter
                                                if (!volumeInput.empty()) {
                                                    try {
                                                        int volume = std::stoi(volumeInput);
                                                        if (volume >= 0 && volume <= 100) {
                                                            int oldVolume = cfg.ruler_volume;
                                                            cfg.ruler_volume = volume;
                                                            print("\n" + translation.format("VOLUME_CONFIG_RULER_SET", 
                                                                "[Ruler volume set to: {0}%]", volume) + "\n");
                                                            if (logger) {
                                                                char msg[512];
                                                                snprintf(msg, sizeof(msg), "Ruler volume changed from %d%% to %d%%", 
                                                                    oldVolume, volume);
                                                                logger->log("AUDIO", msg);
                                                            }
                                                            saveSettings();
                                                            // Update analyzer if available
                                                            if (analyzer) {
                                                                analyzer->setRulerVolume(volume);
                                                            }
                                                            volumeInputting = false;
                                                            inputting = false;
                                                        } else {
                                                            print("\n" + translation.get("VOLUME_CONFIG_ERROR_VOLUME", 
                                                                "[Error: Volume must be between 0 and 100]") + "\n");
                                                            std::string prompt = translation.get("VOLUME_CONFIG_ENTER_RULER", 
                                                                "Enter ruler volume (0-100%), or press ESC to cancel:");
                                                            std::string depthIndicator = getDepthIndicator(5);
                                                            print(prompt + " " + depthIndicator + " ");
                                                            volumeInput.clear();
                                                        }
                                                    } catch (...) {
                                                        print("\n" + translation.get("ERROR_INVALID_SELECTION", 
                                                            "[Error: Invalid number]") + "\n");
                                                        std::string prompt = translation.get("VOLUME_CONFIG_ENTER_RULER", 
                                                            "Enter ruler volume (0-100%), or press ESC to cancel:");
                                                        std::string depthIndicator = getDepthIndicator(5);
                                                        print(prompt + " " + depthIndicator + " ");
                                                        volumeInput.clear();
                                                    }
                                                }
                                            } else if (vch >= '0' && vch <= '9') {
                                                volumeInput += static_cast<char>(vch);
                                                print(std::string(1, vch));
                                            } else if (vch == 8 && !volumeInput.empty()) {  // Backspace
                                                volumeInput.pop_back();
                                                print("\b \b");
                                            }
                                        }
                                    }
                                } else if (ch == 'x' || ch == 'X') {  // X-axis ruler volume
                                    print("X\n\n");
                                    std::string prompt = translation.get("VOLUME_CONFIG_ENTER_X_RULER", 
                                        "Enter X-axis ruler volume (0-100%), or press ESC to cancel:");
                                    std::string depthIndicator = getDepthIndicator(5);
                                    print(prompt + " " + depthIndicator + " ");
                                    
                                    std::string volumeInput;
                                    bool volumeInputting = true;
                                    while (volumeInputting) {
                                        if (consoleInput->kbhit()) {
                                            int vch = consoleInput->getch();
                                            if (vch == 27) {  // ESC
                                                print("\n" + translation.get("CANCELLED", "[Cancelled]") + "\n");
                                                volumeInputting = false;
                                                inputting = false;
                                            } else if (vch == '\r' || vch == '\n') {  // Enter
                                                if (!volumeInput.empty()) {
                                                    try {
                                                        int volume = std::stoi(volumeInput);
                                                        if (volume >= 0 && volume <= 100) {
                                                            int oldVolume = cfg.x_axis_ruler_volume;
                                                            cfg.x_axis_ruler_volume = volume;
                                                            print("\n" + translation.format("VOLUME_CONFIG_X_RULER_SET", 
                                                                "[X-axis ruler volume set to: {0}%]", volume) + "\n");
                                                            if (logger) {
                                                                char msg[512];
                                                                snprintf(msg, sizeof(msg), "X-axis ruler volume changed from %d%% to %d%%", 
                                                                    oldVolume, volume);
                                                                logger->log("AUDIO", msg);
                                                            }
                                                            saveSettings();
                                                            // Update analyzer if available
                                                            if (analyzer) {
                                                                analyzer->setXAxisRulerVolume(volume);
                                                            }
                                                            volumeInputting = false;
                                                            inputting = false;
                                                        } else {
                                                            print("\n" + translation.get("VOLUME_CONFIG_ERROR_VOLUME", 
                                                                "[Error: Volume must be between 0 and 100]") + "\n");
                                                            std::string prompt = translation.get("VOLUME_CONFIG_ENTER_X_RULER", 
                                                                "Enter X-axis ruler volume (0-100%), or press ESC to cancel:");
                                                            std::string depthIndicator = getDepthIndicator(5);
                                                            print(prompt + " " + depthIndicator + " ");
                                                            volumeInput.clear();
                                                        }
                                                    } catch (...) {
                                                        print("\n" + translation.get("ERROR_INVALID_SELECTION", 
                                                            "[Error: Invalid number]") + "\n");
                                                        std::string prompt = translation.get("VOLUME_CONFIG_ENTER_X_RULER", 
                                                            "Enter X-axis ruler volume (0-100%), or press ESC to cancel:");
                                                        std::string depthIndicator = getDepthIndicator(5);
                                                        print(prompt + " " + depthIndicator + " ");
                                                        volumeInput.clear();
                                                    }
                                                }
                                            } else if (vch >= '0' && vch <= '9') {
                                                volumeInput += static_cast<char>(vch);
                                                print(std::string(1, vch));
                                            } else if (vch == 8 && !volumeInput.empty()) {  // Backspace
                                                volumeInput.pop_back();
                                                print("\b \b");
                                            }
                                        }
                                    }
                                } else if (ch == '\r' || ch == '\n') {  // Enter
                                    if (!curveInput.empty()) {
                                        try {
                                            int curveNum = std::stoi(curveInput);
                                            if (curveNum >= 1 && curveNum <= 5) {
                                                int curveIndex = curveNum - 1;
                                                std::string prompt = translation.format("VOLUME_CONFIG_ENTER_VOLUME", 
                                                    "Enter volume for {0} (0-100%), or press ESC to cancel:", curveNames[curveIndex]);
                                                std::string depthIndicator = getDepthIndicator(5);
                                                print("\n\n" + prompt + " " + depthIndicator + " ");
                                                
                                                std::string volumeInput;
                                                bool volumeInputting = true;
                                                while (volumeInputting) {
                                                    if (consoleInput->kbhit()) {
                                                        int vch = consoleInput->getch();
                                                        if (vch == 27) {  // ESC
                                                            print("\n" + translation.get("CANCELLED", "[Cancelled]") + "\n");
                                                            volumeInputting = false;
                                                            inputting = false;
                                                        } else if (vch == '\r' || vch == '\n') {  // Enter
                                                            if (!volumeInput.empty()) {
                                                                try {
                                                                    int volume = std::stoi(volumeInput);
                                                                    if (volume >= 0 && volume <= 100) {
                                                                        // Get old volume and update the correct array
                                                                        int oldVolume;
                                                                        if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
                                                                            oldVolume = cfg.curve_volume_synth[curveIndex];
                                                                            cfg.curve_volume_synth[curveIndex] = volume;
                                                                        } else {
                                                                            oldVolume = cfg.curve_volume_midi[curveIndex];
                                                                            cfg.curve_volume_midi[curveIndex] = volume;
                                                                        }
                                                                        print("\n" + translation.format("VOLUME_CONFIG_SET", 
                                                                            "[{0} volume set to: {1}%]", curveNames[curveIndex], volume) + "\n");
                                                                        if (logger) {
                                                                            char msg[512];
                                                                            snprintf(msg, sizeof(msg), "%s volume changed from %d%% to %d%% (%s mode)", 
                                                                                curveNames[curveIndex], oldVolume, volume, engineName.c_str());
                                                                            logger->log("AUDIO", msg);
                                                                        }
                                                                        saveSettings();
                                                                        // Update analyzer if available
                                                                        if (analyzer) {
                                                                            analyzer->setCurveVolume(curveIndex, volume);
                                                                        }
                                                                        volumeInputting = false;
                                                                        inputting = false;
                                                                    } else {
                                                                        print("\n" + translation.get("VOLUME_CONFIG_ERROR_VOLUME", 
                                                                            "[Error: Volume must be between 0 and 100]") + "\n");
                                                                        std::string prompt = translation.format("VOLUME_CONFIG_ENTER_VOLUME", 
                                                                            "Enter volume for {0} (0-100%), or press ESC to cancel:", curveNames[curveIndex]);
                                                                        std::string depthIndicator = getDepthIndicator(5);
                                                                        print(prompt + " " + depthIndicator + " ");
                                                                        volumeInput.clear();
                                                                    }
                                                                } catch (...) {
                                                                    print("\n" + translation.get("ERROR_INVALID_SELECTION", 
                                                                        "[Error: Invalid number]") + "\n");
                                                                    std::string prompt = translation.format("VOLUME_CONFIG_ENTER_VOLUME", 
                                                                        "Enter volume for {0} (0-100%), or press ESC to cancel:", curveNames[curveIndex]);
                                                                    std::string depthIndicator = getDepthIndicator(5);
                                                                    print(prompt + " " + depthIndicator + " ");
                                                                    volumeInput.clear();
                                                                }
                                                            }
                                                        } else if (vch >= '0' && vch <= '9') {
                                                            volumeInput += static_cast<char>(vch);
                                                            print(std::string(1, vch));
                                                        } else if (vch == 8 && !volumeInput.empty()) {  // Backspace
                                                            volumeInput.pop_back();
                                                            print("\b \b");
                                                        }
                                                    }
                                                }
                                            } else {
                                                print("\n" + translation.get("VOLUME_CONFIG_ERROR_CURVE", 
                                                    "[Error: Curve number must be 1-5]") + "\n");
                                                inputting = false;
                                            }
                                        } catch (...) {
                                            print("\n" + translation.get("ERROR_INVALID_SELECTION", 
                                                "[Error: Invalid number]") + "\n");
                                            inputting = false;
                                        }
                                    }
                                } else if (ch >= '1' && ch <= '5') {
                                    curveInput += static_cast<char>(ch);
                                    print(std::string(1, ch));
                                } else if (ch == 8 && !curveInput.empty()) {  // Backspace
                                    curveInput.pop_back();
                                    print("\b \b");
                                }
                            }
                        }
                    }
                    break;
                
                case 's':  // Smith Configuration
                    {
                        runSmithConfigurationScreen(analyzer);
                    }
                    break;
                
                case 'w':  // Spatial Audio Calibration Wizard
                    {
                        if (analyzer) {
                            print("\n" + translation.get("SPATIAL_WIZARD_STARTING", "[Starting Spatial Audio Calibration Wizard...]") + "\n");
                            bool wizardCompleted = runSpatialAudioCalibrationWizard(analyzer);
                            if (wizardCompleted) {
                                print("\n" + translation.get("SPATIAL_WIZARD_COMPLETED", "[Calibration completed and saved]") + "\n");
                            }
                        } else {
                            print("\n" + translation.get("ERROR_NO_ANALYZER", "[Error: Audio analyzer not available]") + "\n");
                        }
                    }
                    break;
                
                case 'c':  // MIDI Controller Configuration
                    {
                        navStack.push("midi_controller_config");
                        runMidiControllerConfigurationScreen(analyzer);
                        navStack.pop();
                    }
                    break;
                
                case 'a':  // Ruler configuration
                    {
                        print(formatHeading(translation.get("RULER_CONFIG_TITLE", "Y-Axis Ruler Configuration")));
                        
                        // Display current settings
                        std::string soundMode = (cfg.ruler_sound_mode == AppConfig::RulerSoundMode::FOLLOW_LAST_CURVE) 
                            ? translation.get("RULER_SOUND_MODE_FOLLOW", "Follow last curve")
                            : translation.get("RULER_SOUND_MODE_CUSTOM", "Custom sound");
                        print(translation.format("RULER_CONFIG_SOUND_MODE", "Current sound mode: {0}", soundMode.c_str()) + "\n");
                        
                        if (cfg.ruler_sound_mode == AppConfig::RulerSoundMode::CUSTOM_SOUND) {
                            // Show custom sound settings
                            print(translation.format("RULER_CONFIG_CUSTOM_SOUND_SYNTH", "Custom sound (Synth): {0}", 
                                getWaveformName(static_cast<Waveform>(cfg.ruler_custom_sound_synth))) + "\n");
                            print(translation.format("RULER_CONFIG_CUSTOM_SOUND_MIDI_GLIDING", "Custom sound (MIDI Gliding): {0}", 
                                getMidiInstrumentName(cfg.ruler_custom_sound_midi_gliding)) + "\n");
                            print(translation.format("RULER_CONFIG_CUSTOM_SOUND_MIDI_DOTTED", "Custom sound (MIDI Dotted): {0}", 
                                getMidiInstrumentName(cfg.ruler_custom_sound_midi_dotted)) + "\n");
                        }
                        
                        print(translation.format("RULER_CONFIG_BLIP_DURATION", "Shortest blip duration: {0} ms", cfg.ruler_blip_duration_ms) + "\n");
                        print(translation.format("RULER_CONFIG_LENGTHENING_FACTOR", "Lengthening factor: {0}%", cfg.ruler_lengthening_factor_percent) + "\n");
                        print(translation.format("RULER_CONFIG_VOLUME", "Ruler volume: {0}%", cfg.ruler_volume) + "\n\n");
                        
                        print(translation.get("RULER_CONFIG_COMMANDS", "Commands:") + "\n");
                        print(translation.get("RULER_CONFIG_MODE_CMD", "  M - Toggle sound Mode (Follow last curve / Custom sound)") + "\n");
                        print(translation.get("RULER_CONFIG_CUSTOM_CMD", "  C - Configure Custom sound") + "\n");
                        print(translation.get("RULER_CONFIG_DURATION_CMD", "  D - Configure blip Duration (30-500 ms)") + "\n");
                        print(translation.get("RULER_CONFIG_LENGTHENING_CMD", "  L - Configure Lengthening factor (100-500%)") + "\n");
                        print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
                        
                        bool rulerRunning = true;
                        print(getPromptWithDepth("RULER_CONFIG", 4) + " ");
                        
                        while (rulerRunning) {
                            if (consoleInput->kbhit()) {
                                int rch = consoleInput->getch();
                                
                                // Skip extended key sequences
                                if (rch == 0 || rch == 224) {
                                    if (consoleInput->kbhit()) consoleInput->getch();
                                    continue;
                                }
                                
                                // Convert to lowercase
                                if (rch >= 'A' && rch <= 'Z') {
                                    rch = rch - 'A' + 'a';
                                }
                                
                                switch (rch) {
                                    case 27:  // ESC
                                        rulerRunning = false;
                                        print("\n[Returning to audio configuration...]\n");
                                        break;
                                    
                                    case 'm':  // Toggle sound mode
                                        {
                                            cfg.ruler_sound_mode = (cfg.ruler_sound_mode == AppConfig::RulerSoundMode::FOLLOW_LAST_CURVE) 
                                                ? AppConfig::RulerSoundMode::CUSTOM_SOUND 
                                                : AppConfig::RulerSoundMode::FOLLOW_LAST_CURVE;
                                            
                                            std::string newMode = (cfg.ruler_sound_mode == AppConfig::RulerSoundMode::FOLLOW_LAST_CURVE) 
                                                ? translation.get("RULER_SOUND_MODE_FOLLOW", "Follow last curve")
                                                : translation.get("RULER_SOUND_MODE_CUSTOM", "Custom sound");
                                            
                                            print("M\n" + translation.format("RULER_MODE_TOGGLED", "Ruler sound mode: {0}", newMode.c_str()) + "\n");
                                            saveSettings();
                                            // Update analyzer if available
                                            if (analyzer) {
                                                analyzer->setRulerSoundMode(static_cast<AcousticAnalyzer::RulerSoundMode>(cfg.ruler_sound_mode));
                                            }
                                            rulerRunning = false;
                                        }
                                        break;
                                    
                                    case 'c':  // Configure custom sound
                                        {
                                            print("C\n\n" + formatSubHeading(translation.get("RULER_CUSTOM_TITLE", "Configure Ruler Custom Sound")));
                                            
                                            if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
                                                // Synth mode - select waveform
                                                print(translation.get("RULER_CUSTOM_SYNTH_PROMPT", 
                                                    "Select waveform (0-5): 0=Sine, 1=Square, 2=Triangle, 3=Sawtooth, 4=Sawtooth Inv, 5=Pulse\nEnter number (0-5), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                                                
                                                std::string input;
                                                if (!readLine(input)) break;
                                                
                                                if (!input.empty()) {
                                                    try {
                                                        int waveform = std::stoi(input);
                                                        if (waveform >= 0 && waveform <= 5) {
                                                            cfg.ruler_custom_sound_synth = waveform;
                                                            print(translation.format("RULER_CUSTOM_SYNTH_SET", "Ruler custom sound set to: {0}", 
                                                                getWaveformName(static_cast<Waveform>(waveform))) + "\n");
                                                            saveSettings();
                                                            // Update analyzer if available
                                                            if (analyzer) {
                                                                analyzer->setRulerCustomSoundSynth(waveform);
                                                            }
                                                        } else {
                                                            print(translation.get("RULER_CUSTOM_ERROR", "Error: Invalid value") + "\n");
                                                        }
                                                    } catch (...) {
                                                        print(translation.get("RULER_CUSTOM_ERROR", "Error: Invalid value") + "\n");
                                                    }
                                                }
                                            } else {
                                                // MIDI mode - select instrument based on playback mode
                                                if (cfg.midi_playback_mode == MIDIPlaybackMode::GLIDING) {
                                                    print(translation.get("RULER_CUSTOM_MIDI_GLIDING_PROMPT", 
                                                        "Select MIDI instrument for Gliding mode (0-127), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                                                    
                                                    std::string input;
                                                    if (!readLine(input)) break;
                                                    
                                                    if (!input.empty()) {
                                                        try {
                                                            int instrument = std::stoi(input);
                                                            if (instrument >= 0 && instrument <= 127) {
                                                                cfg.ruler_custom_sound_midi_gliding = instrument;
                                                                print(translation.format("RULER_CUSTOM_MIDI_SET", "Ruler custom MIDI instrument set to: {0} ({1})", 
                                                                    instrument, getMidiInstrumentName(instrument)) + "\n");
                                                                saveSettings();
                                                                // Update analyzer if available
                                                                if (analyzer) {
                                                                    analyzer->setRulerCustomSoundMidiGliding(instrument);
                                                                    // Update the audio engine's custom ruler instruments
                                                                    auto midiEngine = std::dynamic_pointer_cast<MIDIEngine>(analyzer->getAudioEngine());
                                                                    if (midiEngine) {
                                                                        midiEngine->setRulerCustomInstruments(cfg.ruler_custom_sound_midi_gliding, cfg.ruler_custom_sound_midi_dotted);
                                                                    }
                                                                }
                                                            } else {
                                                                print(translation.get("RULER_CUSTOM_ERROR", "Error: Invalid value") + "\n");
                                                            }
                                                        } catch (...) {
                                                            print(translation.get("RULER_CUSTOM_ERROR", "Error: Invalid value") + "\n");
                                                        }
                                                    }
                                                } else {  // DOTTED mode
                                                    print(translation.get("RULER_CUSTOM_MIDI_DOTTED_PROMPT", 
                                                        "Select MIDI instrument for Dotted mode (0-127), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                                                    
                                                    std::string input;
                                                    if (!readLine(input)) break;
                                                    
                                                    if (!input.empty()) {
                                                        try {
                                                            int instrument = std::stoi(input);
                                                            if (instrument >= 0 && instrument <= 127) {
                                                                cfg.ruler_custom_sound_midi_dotted = instrument;
                                                                print(translation.format("RULER_CUSTOM_MIDI_SET", "Ruler custom MIDI instrument set to: {0} ({1})", 
                                                                    instrument, getMidiInstrumentName(instrument)) + "\n");
                                                                saveSettings();
                                                                // Update analyzer if available
                                                                if (analyzer) {
                                                                    analyzer->setRulerCustomSoundMidiDotted(instrument);
                                                                    // Update the audio engine's custom ruler instruments
                                                                    auto midiEngine = std::dynamic_pointer_cast<MIDIEngine>(analyzer->getAudioEngine());
                                                                    if (midiEngine) {
                                                                        midiEngine->setRulerCustomInstruments(cfg.ruler_custom_sound_midi_gliding, cfg.ruler_custom_sound_midi_dotted);
                                                                    }
                                                                }
                                                            } else {
                                                                print(translation.get("RULER_CUSTOM_ERROR", "Error: Invalid value") + "\n");
                                                            }
                                                        } catch (...) {
                                                            print(translation.get("RULER_CUSTOM_ERROR", "Error: Invalid value") + "\n");
                                                        }
                                                    }
                                                }
                                            }
                                            rulerRunning = false;
                                        }
                                        break;
                                    
                                    case 'd':  // Configure blip duration
                                        {
                                            print("D\n\n" + formatSubHeading(translation.get("RULER_DURATION_TITLE", "Configure Ruler Blip Duration")));
                                            print(translation.get("RULER_DURATION_DESC", 
                                                "Blip duration controls the length of the shortest blips (half integers) in the ruler.\nLonger blips are calculated relative to this base duration.") + "\n\n");
                                            print(translation.get("RULER_DURATION_PROMPT", "Enter blip duration in milliseconds (30-500), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                                            
                                            std::string input;
                                            if (!readLine(input)) break;
                                            
                                            if (!input.empty()) {
                                                try {
                                                    int duration = std::stoi(input);
                                                    if (duration >= 30 && duration <= 500) {
                                                        cfg.ruler_blip_duration_ms = duration;
                                                        print(translation.format("RULER_DURATION_SET", "Ruler blip duration set to {0} ms", duration) + "\n");
                                                        saveSettings();
                                                        // Update analyzer if available
                                                        if (analyzer) {
                                                            analyzer->setRulerBlipDuration(duration);
                                                        }
                                                    } else {
                                                        print(translation.get("RULER_DURATION_ERROR", "Error: Blip duration must be between 30 and 500 ms") + "\n");
                                                    }
                                                } catch (...) {
                                                    print(translation.get("RULER_CUSTOM_ERROR", "Error: Invalid value") + "\n");
                                                }
                                            }
                                            rulerRunning = false;
                                        }
                                        break;
                                    
                                    case 'l':  // Configure lengthening factor
                                        {
                                            print("L\n\n" + formatSubHeading(translation.get("RULER_LENGTHENING_TITLE", "Configure Ruler Lengthening Factor")));
                                            print(translation.get("RULER_LENGTHENING_DESC", 
                                                "Lengthening factor controls how much longer the tones at full integers, 5, 10, and 15 are compared to half integers.\nHigher values create more distinction between different tone types.") + "\n\n");
                                            print(translation.get("RULER_LENGTHENING_PROMPT", "Enter lengthening factor in percent (100-500%), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                                            
                                            std::string input;
                                            if (!readLine(input)) break;
                                            
                                            if (!input.empty()) {
                                                try {
                                                    int factor = std::stoi(input);
                                                    if (factor >= 100 && factor <= 500) {
                                                        cfg.ruler_lengthening_factor_percent = factor;
                                                        print(translation.format("RULER_LENGTHENING_SET", "Ruler lengthening factor set to {0}%", factor) + "\n");
                                                        saveSettings();
                                                        // Update analyzer if available
                                                        if (analyzer) {
                                                            analyzer->setRulerLengtheningFactor(factor);
                                                        }
                                                    } else {
                                                        print(translation.get("RULER_LENGTHENING_ERROR", "Error: Lengthening factor must be between 100 and 500%") + "\n");
                                                    }
                                                } catch (...) {
                                                    print(translation.get("RULER_CUSTOM_ERROR", "Error: Invalid value") + "\n");
                                                }
                                            }
                                            rulerRunning = false;
                                        }
                                        break;
                                }
                            }
                            
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                    }
                    break;
                
                case 'x':  // X-Axis Ruler configuration
                    {
                        print(formatHeading(translation.get("X_RULER_CONFIG_TITLE", "X-Axis Ruler Configuration")));
                        
                        // Display current settings
                        const char* noiseTypeNames[] = {"White Noise", "Pink Noise", "Click"};
                        print(translation.format("X_RULER_CONFIG_DURATION", "Blip duration: {0} ms", cfg.x_axis_ruler_blip_duration_ms) + "\n");
                        
                        if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
                            print(translation.format("X_RULER_CONFIG_NOISE_TYPE", "Noise type: {0}", noiseTypeNames[cfg.x_axis_ruler_noise_type]) + "\n");
                        } else {
                            print(translation.format("X_RULER_CONFIG_MIDI_DRUM", "MIDI drum note: {0}", cfg.x_axis_ruler_midi_drum) + "\n");
                        }
                        print("\n");
                        
                        print(translation.get("X_RULER_CONFIG_COMMANDS", "Commands:") + "\n");
                        print(translation.get("X_RULER_CONFIG_DURATION_CMD", "  D - Configure blip Duration (30-200 ms)") + "\n");
                        if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
                            print(translation.get("X_RULER_CONFIG_NOISE_CMD", "  N - Configure Noise type (White/Pink/Click)") + "\n");
                        } else {
                            print(translation.get("X_RULER_CONFIG_DRUM_CMD", "  M - Configure MIDI drum note (35-81)") + "\n");
                        }
                        print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
                        
                        bool xRulerRunning = true;
                        print(getPromptWithDepth("X_RULER_CONFIG", 4) + " ");
                        
                        while (xRulerRunning) {
                            if (consoleInput->kbhit()) {
                                int xch = consoleInput->getch();
                                
                                // Skip extended key sequences
                                if (xch == 0 || xch == 224) {
                                    if (consoleInput->kbhit()) consoleInput->getch();
                                    continue;
                                }
                                
                                // Convert to lowercase
                                if (xch >= 'A' && xch <= 'Z') {
                                    xch = xch - 'A' + 'a';
                                }
                                
                                switch (xch) {
                                    case 27:  // ESC
                                        xRulerRunning = false;
                                        print("\n[Returning to audio configuration...]\n");
                                        break;
                                    
                                    case 'd':  // Configure blip duration
                                        {
                                            print("D\n\n" + formatSubHeading(translation.get("X_RULER_DURATION_TITLE", "Configure X-Axis Ruler Blip Duration")));
                                            print(translation.get("X_RULER_DURATION_DESC", 
                                                "Blip duration controls the length of the noise bursts at each measurement point.") + "\n\n");
                                            print(translation.get("X_RULER_DURATION_PROMPT", "Enter blip duration in milliseconds (30-200), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                                            
                                            std::string input;
                                            if (!readLine(input)) break;
                                            
                                            if (!input.empty()) {
                                                try {
                                                    int duration = std::stoi(input);
                                                    if (duration >= 30 && duration <= 200) {
                                                        cfg.x_axis_ruler_blip_duration_ms = duration;
                                                        print(translation.format("X_RULER_DURATION_SET", "X-axis ruler blip duration set to {0} ms", duration) + "\n");
                                                        saveSettings();
                                                        // Update analyzer if available
                                                        if (analyzer) {
                                                            analyzer->setXAxisRulerBlipDuration(duration);
                                                        }
                                                    } else {
                                                        print(translation.get("X_RULER_DURATION_ERROR", "Error: Blip duration must be between 30 and 200 ms") + "\n");
                                                    }
                                                } catch (...) {
                                                    print(translation.get("ERROR_INVALID_SELECTION", "Error: Invalid value") + "\n");
                                                }
                                            }
                                            xRulerRunning = false;
                                        }
                                        break;
                                    
                                    case 'n':  // Configure noise type (synthesizer mode only)
                                        if (cfg.audio_engine == AudioEngineType::SYNTHESIZER) {
                                            print("N\n\n" + formatSubHeading(translation.get("X_RULER_NOISE_TITLE", "Configure X-Axis Ruler Noise Type")));
                                            print(translation.get("X_RULER_NOISE_DESC", 
                                                "Select the type of noise used for X-axis ruler blips:\n  0 = White Noise (bright, full spectrum)\n  1 = Pink Noise (warmer, filtered)\n  2 = Click (short impulse)") + "\n\n");
                                            print(translation.get("X_RULER_NOISE_PROMPT", "Enter noise type (0-2), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                                            
                                            std::string input;
                                            if (!readLine(input)) break;
                                            
                                            if (!input.empty()) {
                                                try {
                                                    int noiseType = std::stoi(input);
                                                    if (noiseType >= 0 && noiseType <= 2) {
                                                        cfg.x_axis_ruler_noise_type = noiseType;
                                                        print(translation.format("X_RULER_NOISE_SET", "X-axis ruler noise type set to: {0}", noiseTypeNames[noiseType]) + "\n");
                                                        saveSettings();
                                                        // Update analyzer if available
                                                        if (analyzer) {
                                                            analyzer->setXAxisRulerNoiseType(static_cast<AcousticAnalyzer::XAxisRulerNoiseType>(noiseType));
                                                            // Update the audio engine if it's a synthesizer
                                                            auto synthEngine = std::dynamic_pointer_cast<SynthesizerEngine>(analyzer->getAudioEngine());
                                                            if (synthEngine) {
                                                                synthEngine->setXAxisRulerNoiseType(noiseType);
                                                            }
                                                        }
                                                    } else {
                                                        print(translation.get("X_RULER_NOISE_ERROR", "Error: Noise type must be 0, 1, or 2") + "\n");
                                                    }
                                                } catch (...) {
                                                    print(translation.get("ERROR_INVALID_SELECTION", "Error: Invalid value") + "\n");
                                                }
                                            }
                                            xRulerRunning = false;
                                        }
                                        break;
                                    
                                    case 'm':  // Configure MIDI drum (MIDI mode only)
                                        if (cfg.audio_engine == AudioEngineType::MIDI) {
                                            print("M\n\n" + formatSubHeading(translation.get("X_RULER_DRUM_TITLE", "Configure X-Axis Ruler MIDI Drum")));
                                            print(translation.get("X_RULER_DRUM_DESC", 
                                                "Select the MIDI drum note for X-axis ruler blips.\nCommon drums: 35=Acoustic Bass Drum, 36=Bass Drum 1, 38=Acoustic Snare, 42=Closed Hi-Hat, 46=Open Hi-Hat") + "\n\n");
                                            print(translation.get("X_RULER_DRUM_PROMPT", "Enter MIDI drum note (35-81), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                                            
                                            std::string input;
                                            if (!readLine(input)) break;
                                            
                                            if (!input.empty()) {
                                                try {
                                                    int drumNote = std::stoi(input);
                                                    if (drumNote >= 35 && drumNote <= 81) {
                                                        cfg.x_axis_ruler_midi_drum = drumNote;
                                                        print(translation.format("X_RULER_DRUM_SET", "X-axis ruler MIDI drum note set to: {0}", drumNote) + "\n");
                                                        saveSettings();
                                                        // Update analyzer if available
                                                        if (analyzer) {
                                                            analyzer->setXAxisRulerMidiDrum(drumNote);
                                                            // Update the MIDI engine
                                                            auto midiEngine = std::dynamic_pointer_cast<MIDIEngine>(analyzer->getAudioEngine());
                                                            if (midiEngine) {
                                                                midiEngine->setXAxisRulerDrum(drumNote);
                                                            }
                                                        }
                                                    } else {
                                                        print(translation.get("X_RULER_DRUM_ERROR", "Error: MIDI drum note must be between 35 and 81") + "\n");
                                                    }
                                                } catch (...) {
                                                    print(translation.get("ERROR_INVALID_SELECTION", "Error: Invalid value") + "\n");
                                                }
                                            }
                                            xRulerRunning = false;
                                        }
                                        break;
                                }
                            }
                            
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                    }
                    break;
                
                case 'n':  // Status Line configuration
                    {
                        bool statusLineRunning = true;
                        bool shouldDisplayMenu = true;  // Flag to control menu display
                        
                        while (statusLineRunning) {
                            // Display menu only when needed (initial display or after a toggle)
                            if (shouldDisplayMenu) {
                                print(formatHeading(translation.get("STATUS_LINE_CONFIG_TITLE", "Status Line Configuration")));
                                print(translation.get("STATUS_LINE_CONFIG_DESC", "Select which parameters to display in the status line:") + "\n\n");
                                
                                print(translation.format("STATUS_LINE_CONFIG_POSITION", "  P - Position: {0}", 
                                    cfg.status_line_show_position ? "ON" : "OFF") + "\n");
                                print(translation.format("STATUS_LINE_CONFIG_FREQUENCY", "  F - Frequency: {0}", 
                                    cfg.status_line_show_frequency ? "ON" : "OFF") + "\n");
                                print(translation.format("STATUS_LINE_CONFIG_SWR", "  S - SWR: {0}", 
                                    cfg.status_line_show_swr ? "ON" : "OFF") + "\n");
                                print(translation.format("STATUS_LINE_CONFIG_RL", "  R - Return Loss: {0}", 
                                    cfg.status_line_show_rl ? "ON" : "OFF") + "\n");
                                print(translation.format("STATUS_LINE_CONFIG_IMPEDANCE", "  Z - Impedance |Z|: {0}", 
                                    cfg.status_line_show_impedance ? "ON" : "OFF") + "\n");
                                print(translation.format("STATUS_LINE_CONFIG_REACTANCE", "  X - Reactance: {0}", 
                                    cfg.status_line_show_reactance ? "ON" : "OFF") + "\n");
                                print(translation.format("STATUS_LINE_CONFIG_PHASE", "  H - Phase: {0}", 
                                    cfg.status_line_show_phase ? "ON" : "OFF") + "\n\n");
                                
                                print(translation.get("STATUS_LINE_CONFIG_COMMANDS", "Commands:") + "\n");
                                print(translation.get("STATUS_LINE_CONFIG_TOGGLE", "  P/F/S/R/Z/X/H - Toggle respective parameter") + "\n");
                                print(translation.get("BACK_ESC", "  ESC - Back") + "\n\n");
                                print(getPromptWithDepth("STATUS_LINE_CONFIG", 4) + " ");
                                shouldDisplayMenu = false;  // Don't display again until a toggle occurs
                            }
                            
                            if (consoleInput->kbhit()) {
                                int sch = consoleInput->getch();
                                
                                // Skip extended key sequences
                                if (sch == 0 || sch == 224) {
                                    if (consoleInput->kbhit()) consoleInput->getch();
                                    continue;
                                }
                                
                                // Convert to lowercase
                                if (sch >= 'A' && sch <= 'Z') {
                                    sch = sch - 'A' + 'a';
                                }
                                
                                switch (sch) {
                                    case 27:  // ESC
                                        statusLineRunning = false;
                                        print("\n[Returning to audio configuration...]\n");
                                        break;
                                    
                                    case 'p':  // Toggle position
                                        cfg.status_line_show_position = !cfg.status_line_show_position;
                                        print("P\n" + translation.format("STATUS_LINE_POSITION_TOGGLED", "Position display: {0}", 
                                            cfg.status_line_show_position ? "ON" : "OFF") + "\n");
                                        saveSettings();
                                        analyzer->setStatusLineShowPosition(cfg.status_line_show_position);
                                        shouldDisplayMenu = true;  // Redisplay menu after toggle
                                        break;
                                    
                                    case 'f':  // Toggle frequency
                                        cfg.status_line_show_frequency = !cfg.status_line_show_frequency;
                                        print("F\n" + translation.format("STATUS_LINE_FREQUENCY_TOGGLED", "Frequency display: {0}", 
                                            cfg.status_line_show_frequency ? "ON" : "OFF") + "\n");
                                        saveSettings();
                                        analyzer->setStatusLineShowFrequency(cfg.status_line_show_frequency);
                                        shouldDisplayMenu = true;  // Redisplay menu after toggle
                                        break;
                                    
                                    case 's':  // Toggle SWR
                                        cfg.status_line_show_swr = !cfg.status_line_show_swr;
                                        print("S\n" + translation.format("STATUS_LINE_SWR_TOGGLED", "SWR display: {0}", 
                                            cfg.status_line_show_swr ? "ON" : "OFF") + "\n");
                                        saveSettings();
                                        analyzer->setStatusLineShowSWR(cfg.status_line_show_swr);
                                        shouldDisplayMenu = true;  // Redisplay menu after toggle
                                        break;
                                    
                                    case 'r':  // Toggle Return Loss
                                        cfg.status_line_show_rl = !cfg.status_line_show_rl;
                                        print("R\n" + translation.format("STATUS_LINE_RL_TOGGLED", "Return Loss display: {0}", 
                                            cfg.status_line_show_rl ? "ON" : "OFF") + "\n");
                                        saveSettings();
                                        analyzer->setStatusLineShowRL(cfg.status_line_show_rl);
                                        shouldDisplayMenu = true;  // Redisplay menu after toggle
                                        break;
                                    
                                    case 'z':  // Toggle Impedance
                                        cfg.status_line_show_impedance = !cfg.status_line_show_impedance;
                                        print("Z\n" + translation.format("STATUS_LINE_IMPEDANCE_TOGGLED", "Impedance |Z| display: {0}", 
                                            cfg.status_line_show_impedance ? "ON" : "OFF") + "\n");
                                        saveSettings();
                                        analyzer->setStatusLineShowImpedance(cfg.status_line_show_impedance);
                                        shouldDisplayMenu = true;  // Redisplay menu after toggle
                                        break;
                                    
                                    case 'x':  // Toggle Reactance
                                        cfg.status_line_show_reactance = !cfg.status_line_show_reactance;
                                        print("X\n" + translation.format("STATUS_LINE_REACTANCE_TOGGLED", "Reactance display: {0}", 
                                            cfg.status_line_show_reactance ? "ON" : "OFF") + "\n");
                                        saveSettings();
                                        analyzer->setStatusLineShowReactance(cfg.status_line_show_reactance);
                                        shouldDisplayMenu = true;  // Redisplay menu after toggle
                                        break;
                                    
                                    case 'h':  // Toggle Phase
                                        cfg.status_line_show_phase = !cfg.status_line_show_phase;
                                        print("H\n" + translation.format("STATUS_LINE_PHASE_TOGGLED", "Phase display: {0}", 
                                            cfg.status_line_show_phase ? "ON" : "OFF") + "\n");
                                        saveSettings();
                                        analyzer->setStatusLineShowPhase(cfg.status_line_show_phase);
                                        shouldDisplayMenu = true;  // Redisplay menu after toggle
                                        break;
                                }
                            }
                            
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                    }
                    break;
                
                case 'i':  // Toggle MIDI interpolated panning
                    {
                        if (cfg.audio_engine == AudioEngineType::MIDI) {
                            cfg.midi_interpolated_pan_mode = !cfg.midi_interpolated_pan_mode;
                            const char* statusText = cfg.midi_interpolated_pan_mode ? "ON" : "OFF";
                            print("I\n" + translation.format("MIDI_INTERP_PAN_TOGGLED", 
                                "[MIDI interpolated panning: {0}]", statusText) + "\n");
                            
                            if (logger) {
                                char msg[256];
                                snprintf(msg, sizeof(msg), "MIDI interpolated panning toggled to %s", statusText);
                                logger->log("MIDI", msg);
                            }
                            
                            saveSettings();
                            
                            // Apply immediately to active MIDI engine
                            if (analyzer) {
                                auto engine = analyzer->getAudioEngine();
                                if (engine && std::string(engine->getName()) == "MIDI") {
                                    auto midiEngine = std::dynamic_pointer_cast<MIDIEngine>(engine);
                                    if (midiEngine) {
                                        midiEngine->setInterpolatedPanMode(cfg.midi_interpolated_pan_mode);
                                    }
                                }
                            }
                        } else {
                            print("I\n" + translation.get("MIDI_INTERP_PAN_MIDI_ONLY", 
                                "[Error: Interpolated panning is only available in MIDI mode]") + "\n");
                        }
                    }
                    break;
                
                case 't':  // Set MIDI interpolation strength
                    {
                        if (cfg.audio_engine == AudioEngineType::MIDI) {
                            print("T\n\n" + formatSubHeading(translation.get("MIDI_INTERP_STRENGTH_TITLE", 
                                "Configure MIDI Interpolation Strength")));
                            print(translation.format("MIDI_INTERP_STRENGTH_CURRENT", 
                                "Current strength: {0}", cfg.midi_interpolation_strength) + "\n");
                            print(translation.get("MIDI_INTERP_STRENGTH_DESC", 
                                "Strength determines how much volume modulation affects perceived pan position.") + "\n");
                            print(translation.get("MIDI_INTERP_STRENGTH_RANGE", 
                                "Range: 0.0 (no effect) to 1.0 (maximum effect)") + "\n");
                            print(translation.get("MIDI_INTERP_STRENGTH_RECOMMEND", 
                                "Recommended: 0.2-0.4 for subtle effect, 0.5-0.8 for pronounced effect") + "\n\n");
                            print(translation.get("MIDI_INTERP_STRENGTH_PROMPT", 
                                "Enter interpolation strength (0.0-1.0), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                            
                            std::string input;
                            bool inputting = true;
                            while (inputting) {
                                if (consoleInput->kbhit()) {
                                    int ch = consoleInput->getch();
                                    if (ch == 27) {  // ESC
                                        print("\n" + translation.get("CANCELLED", "[Cancelled]") + "\n");
                                        inputting = false;
                                    } else if (ch == '\r' || ch == '\n') {  // Enter
                                        if (!input.empty()) {
                                            try {
                                                double strength = std::stod(input);
                                                if (strength >= 0.0 && strength <= 1.0) {
                                                    double oldStrength = cfg.midi_interpolation_strength;
                                                    cfg.midi_interpolation_strength = strength;
                                                    print("\n" + translation.format("MIDI_INTERP_STRENGTH_SET", 
                                                        "[Interpolation strength set to: {0}]", strength) + "\n");
                                                    
                                                    if (logger) {
                                                        char msg[256];
                                                        snprintf(msg, sizeof(msg), 
                                                            "MIDI interpolation strength changed from %.2f to %.2f", 
                                                            oldStrength, strength);
                                                        logger->log("MIDI", msg);
                                                    }
                                                    
                                                    saveSettings();
                                                    
                                                    // Apply immediately to active MIDI engine
                                                    if (analyzer) {
                                                        auto engine = analyzer->getAudioEngine();
                                                        if (engine && std::string(engine->getName()) == "MIDI") {
                                                            auto midiEngine = std::dynamic_pointer_cast<MIDIEngine>(engine);
                                                            if (midiEngine) {
                                                                midiEngine->setInterpolationStrength(strength);
                                                            }
                                                        }
                                                    }
                                                } else {
                                                    print("\n" + translation.get("MIDI_INTERP_STRENGTH_ERROR", 
                                                        "[Error: Strength must be between 0.0 and 1.0]") + "\n");
                                                        print(translation.get("MIDI_INTERP_STRENGTH_PROMPT", 
                                                        "Enter interpolation strength (0.0-1.0), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                                                    input.clear();
                                                    continue;
                                                }
                                            } catch (...) {
                                                print("\n" + translation.get("ERROR_INVALID_NUMBER", 
                                                    "[Error: Invalid number]") + "\n");
                                                print(translation.get("MIDI_INTERP_STRENGTH_PROMPT", 
                                                    "Enter interpolation strength (0.0-1.0), or press ESC to cancel:") + " " + getDepthIndicator(4) + " ");
                                                input.clear();
                                                continue;
                                            }
                                        }
                                        inputting = false;
                                    } else if ((ch >= '0' && ch <= '9') || ch == '.') {
                                        input += static_cast<char>(ch);
                                        print(std::string(1, ch));
                                    } else if (ch == 8 && !input.empty()) {  // Backspace
                                        input.pop_back();
                                        print("\b \b");
                                    }
                                }
                            }
                        } else {
                            print("S\n" + translation.get("MIDI_INTERP_STRENGTH_MIDI_ONLY", 
                                "[Error: Interpolation strength is only available in MIDI mode]") + "\n");
                        }
                    }
                    break;
                
                case 'h':  // Help
                    print(formatHeading(translation.get("AUDIO_HELP_TITLE", "Audio Configuration Help")));
                    print(translation.get("AUDIO_HELP_ENGINE", "E - Toggle between Synthesizer and MIDI audio engines") + "\n");
                    print(translation.format("AUDIO_HELP_RANGE", "R - Configure frequency Range for Synthesizer ({0}-{1} Hz)", SYNTH_MIN_FREQ_HZ_LIMIT, SYNTH_MAX_FREQ_HZ_LIMIT) + "\n");
                    print(translation.get("AUDIO_HELP_RANGE_DESC", "    Determines how measurement values map to audio frequencies") + "\n");
                    print(translation.get("AUDIO_HELP_RANGE_EXAMPLE", "    Example: SWR 1.0=min freq, SWR 20.0=max freq\n") + "\n");
                    print(translation.get("AUDIO_HELP_FREEZE", "F - Configure Freeze point pause duration (50-2000 ms)") + "\n");
                    print(translation.get("AUDIO_HELP_FREEZE_DESC", "    Controls pause between repeated points in freeze mode with dotted playback\n") + "\n");
                    print(translation.get("AUDIO_HELP_CURVES", "1-5 - Configure curve sound (context-sensitive):") + "\n");
                    print(translation.get("AUDIO_HELP_CURVES_LIST", "      1 = SWR, 2 = Return Loss, 3 = Impedance |Z|") + "\n");
                    print(translation.get("AUDIO_HELP_CURVES_LIST2", "      4 = Reactance X, 5 = Phase") + "\n");
                    print(translation.get("AUDIO_HELP_SYNTH_MODE", "    In SYNTHESIZER mode: Select waveform (0-5)") + "\n");
                    print(translation.get("AUDIO_HELP_WAVEFORMS", "      0=Sine, 1=Square, 2=Triangle, 3=Sawtooth, 4=Sawtooth Inv, 5=Pulse") + "\n");
                    print(translation.get("AUDIO_HELP_MIDI_MODE", "    In MIDI mode: Select instrument (0-127) based on current playback mode") + "\n");
                    print(translation.get("AUDIO_HELP_MIDI_LIST", "      Press L during instrument selection to see full instrument list\n") + "\n");
                    print(translation.get("AUDIO_HELP_VOLUME", "V - Configure Volume for individual curves (0-100%)") + "\n");
                    print(translation.get("AUDIO_HELP_VOLUME_DESC", "    Allows setting volume per curve independently\n") + "\n");
                    print(translation.get("AUDIO_HELP_TOGGLE_MODE", "T key in Acoustic Analysis - Toggle between Smooth/Dotted playback modes") + "\n");
                    print(translation.get("AUDIO_HELP_SMOOTH", "    Smooth/Gliding: Continuous playback for sustained instruments") + "\n");
                    print(translation.get("AUDIO_HELP_DOTTED", "    Dotted: Articulated playback with retriggering for percussive instruments") + "\n");
                    print(translation.get("AUDIO_HELP_MIDI_AUTO", "    When MIDI is enabled, automatically switches instrument presets:") + "\n");
                    print(translation.get("AUDIO_HELP_MIDI_GLIDING", "      Gliding preset: String Ensemble, Church Organ, Drawbar Organ, Violin, Lead 2") + "\n");
                    print(translation.get("AUDIO_HELP_MIDI_DOTTED", "      Dotted preset: Vibraphone, Marimba, Xylophone, Tubular Bells, Celesta\n") + "\n");
                    print(translation.get("AUDIO_HELP_PREVIEW", "P - Preview MIDI instruments (plays each curve)") + "\n");
                    print(translation.get("AUDIO_HELP_ESC", "ESC - Back to acoustic analysis\n") + "\n");
                    print(translation.get("AUDIO_HELP_MIDI_TITLE", "MIDI Instruments:") + "\n");
                    print(translation.get("AUDIO_HELP_MIDI_RANGE", "  Enter a number 0-127 for General MIDI instruments") + "\n");
                    print("  Common instruments:\n");
                    print("    0 = Acoustic Grand Piano\n");
                    print("    11 = Vibraphone (percussive)\n");
                    print("    16 = Drawbar Organ (sustained)\n");
                    print("    40 = Violin (sustained)\n");
                    print("    48 = String Ensemble (sustained)\n");
                    print("    56 = Trumpet\n");
                    print("    73 = Flute\n");
                    print("    81 = Lead 2 (sawtooth, sustained)\n");
                    break;
                
                case 27:  // ESC - Back
                    running = false;
                    print("\n[Returning to acoustic analysis...]\n");
                    break;
            }
            
            // Print prompt after handling each command to ensure it's displayed when returning from submenus
            if (running) {
                print(getPromptWithDepth("AUDIO_CONFIG_PROMPT", 3) + " ");
            }
        }  // End of if (hasInput)
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return engineTypeChanged || instrumentsChanged || freqRangeChanged || waveformsChanged;
}

std::string ConsoleUI::formatOhm() const {
    // Always use text representation for accessibility
    return translation.get("OHM_TEXT", "Ohm");
}

std::string ConsoleUI::formatDegree() const {
    // Always use text representation for accessibility
    return translation.get("DEGREE_TEXT", "degree");
}

void ConsoleUI::optionsMenu() {
    if (logger) logger->log("UI", "Entered options menu");
    
    while (true) {
        clearScreen();  // Clear screen at the start of each loop iteration
        print(formatHeading(translation.get("OPTIONS_TITLE", "Options")));
        print(translation.get("OPTIONS_LANGUAGE", "(L)anguage") + "\n");
        print(translation.get("OPTIONS_BANDPLAN", "(B)andplan") + "\n");
        print(translation.get("OPTIONS_BRAILLE", "(R) Braille Printer") + "\n");
        print(translation.get("HELP_COMMAND", "(H)elp") + "\n");
        print(translation.get("MSG_PRESS_ESC_BACK", "Press ESC to go back") + "\n\n");
        
        setUIContext("options_menu", {
            {"l", translation.get("OPTIONS_LANGUAGE", "(L)anguage"), false},
            {"b", translation.get("OPTIONS_BANDPLAN", "(B)andplan"), false},
            {"r", translation.get("OPTIONS_BRAILLE", "(R) Braille Printer"), false},
            {"h", translation.get("HELP_COMMAND", "(H)elp"), false}
        });
        
        print(getPromptWithDepth("OPTIONS_PROMPT", 2) + " ");
        int ch = 0;
        bool hasInput = false;
        
        // Check for web interface input first
        if (webServer && webServer->isRunning() && webServer->hasInput()) {
            std::string webInput = webServer->readInput();
            if (!webInput.empty()) {
                if (logger) logger->log("UI_OPTIONS", "Web input received: [" + webInput + "]");
                
                // Handle web input
                if (webInput[0] == '\x1B') {
                    ch = 27;  // ESC key
                } else {
                    ch = static_cast<unsigned char>(webInput[0]);
                }
                hasInput = true;
                
                if (logger) logger->log("UI_OPTIONS", "Web input mapped to ch: " + std::to_string(ch));
            }
        }
        
        // Check for keyboard input if no web input
        if (!hasInput) {
            ch = consoleInput->getch();
            hasInput = true;
        }
        
        char key = static_cast<char>(ch);
        if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
        // Echo printable ASCII characters only (32-126), filtering control characters like ESC
        if (key >= 32 && key <= 126) {
            print(std::string(1, key) + "\n");
        }
        
        if (key == 'l' || key == 's') {  // L in English, S in German (Sprache)
            languageSelectionMenu();
        } else if (key == 'b') {  // B for Bandplan
            bandplanSelectionMenu();
        } else if (key == 'r') {  // R for Braille Printer (avoiding D for Drucker to not conflict)
            braillePrinterSettingsMenu();
        } else if (key == 'h') {  // H for Help
            print(HelpModule::getOptionsMenuHelp(translation));
        } else if (key == 27) {  // ESC
            break;
        } else {
            print(translation.get("ERROR_UNKNOWN_COMMAND", "Unknown command.") + "\n");
        }
    }
    clearScreen();
    
    if (logger) logger->log("UI", "Exited options menu");
}

void ConsoleUI::languageSelectionMenu() {
    clearScreen();
    print(formatHeading(translation.get("LANG_TITLE", "Language Selection")));
    
    std::string err;
    auto languages = TranslationManager::getAvailableLanguages(err);
    
    if (!err.empty()) {
        print(translation.get("LANG_NO_LANGS", "No language files found.") + "\n");
        print(err + "\n");
        return;
    }
    
    if (languages.empty()) {
        print(translation.get("LANG_NO_LANGS", "No language files found.") + "\n");
        return;
    }
    
    print(translation.get("LANG_AVAILABLE", "Available languages:") + "\n");
    for (size_t i = 0; i < languages.size(); ++i) {
        std::string marker = "";
        if (languages[i].first == cfg.language) {
            marker = " [CURRENT]";
        }
        print(std::to_string(i + 1) + ") " + languages[i].second + " (" + languages[i].first + ")" + marker + "\n");
    }
    
    print("\n" + translation.get("LANG_PROMPT", "Enter language number (or press Enter to cancel): ") + " ");
    std::string input;
    if (!readLine(input) || input.empty()) {
        print(translation.get("CANCELLED", "Canceled.") + "\n");
        return;
    }
    
    int idx = 0;
    try {
        idx = std::stoi(input);
    } catch (...) {
        print(translation.get("ERROR_INVALID_SELECTION", "Invalid selection.") + "\n");
        return;
    }
    
    if (idx <= 0 || idx > static_cast<int>(languages.size())) {
        print(translation.get("ERROR_INVALID_SELECTION", "Invalid selection.") + "\n");
        return;
    }
    
    std::string selectedLang = languages[idx - 1].first;
    std::string loadErr;
    if (!translation.loadLanguage(selectedLang, loadErr)) {
        print(translation.format("LANG_LOAD_FAILED", "Failed to load language: {0}", loadErr) + "\n");
        if (logger) logger->log("UI", "Failed to load language " + selectedLang + ": " + loadErr);
        return;
    }
    
    cfg.language = selectedLang;
    saveSettings();
    
    // Use the NEW translation for the success message
    print(translation.format("LANG_SELECTED", "Language changed to: {0}", languages[idx - 1].second) + "\n");
    
    if (logger) logger->log("UI", "Language changed to: " + selectedLang);
}

void ConsoleUI::bandplanSelectionMenu() {
    clearScreen();
    print(formatHeading(translation.get("BANDPLAN_TITLE", "Band Plan Selection")));
    
    std::string err;
    auto bandplans = getAvailableBandPlans(err);
    
    if (!err.empty()) {
        print(translation.get("BANDPLAN_NO_PLANS", "No band plan files found.") + "\n");
        print(err + "\n");
        return;
    }
    
    if (bandplans.empty()) {
        print(translation.get("BANDPLAN_NO_PLANS", "No band plan files found.") + "\n");
        return;
    }
    
    print(translation.get("BANDPLAN_AVAILABLE", "Available band plans:") + "\n");
    for (size_t i = 0; i < bandplans.size(); ++i) {
        std::string marker = "";
        if (bandplans[i].first == cfg.bandplan) {
            marker = " [CURRENT]";
        }
        print(std::to_string(i + 1) + ") " + bandplans[i].second + " (" + bandplans[i].first + ")" + marker + "\n");
    }
    
    print("\n" + translation.get("BANDPLAN_PROMPT", "Enter band plan number (or press Enter to cancel): ") + " ");
    std::string input;
    if (!readLine(input) || input.empty()) {
        print(translation.get("CANCELLED", "Canceled.") + "\n");
        return;
    }
    
    int idx = 0;
    try {
        idx = std::stoi(input);
    } catch (...) {
        print(translation.get("ERROR_INVALID_SELECTION", "Invalid selection.") + "\n");
        return;
    }
    
    if (idx <= 0 || idx > static_cast<int>(bandplans.size())) {
        print(translation.get("ERROR_INVALID_SELECTION", "Invalid selection.") + "\n");
        return;
    }
    
    std::string selectedPlan = bandplans[idx - 1].first;
    
    // Test loading the band plan to make sure it's valid
    std::vector<AmateurBand> testBands;
    std::string loadErr;
    std::string filename = "bandplans/" + selectedPlan + ".ini";
    if (!loadBandPlan(filename, testBands, loadErr)) {
        print(translation.format("BANDPLAN_LOAD_FAILED", "Failed to load band plan: {0}", loadErr) + "\n");
        if (logger) logger->log("UI", "Failed to load band plan " + selectedPlan + ": " + loadErr);
        return;
    }
    
    cfg.bandplan = selectedPlan;
    saveSettings();
    
    print(translation.format("BANDPLAN_SELECTED", "Band plan changed to: {0}", bandplans[idx - 1].second) + "\n");
    
    if (logger) logger->log("UI", "Band plan changed to: " + selectedPlan);
}

void ConsoleUI::braillePrinterSettingsMenu() {
    while (true) {
        clearScreen();  // Clear screen at the start of each loop iteration
        print(formatHeading("Braille Printer Settings"));
        
        // Show current settings
        std::string protocolName = (cfg.braille_protocol == AppConfig::BrailleProtocol::INDEX_V5) ? "Index V5 (Floating Dot Area)" : "Index V4 (Raster Graphics)";
        std::string paperName;
        switch (cfg.braille_paper_size) {
            case AppConfig::BraillePaperSize::A4: paperName = "A4 (210x297mm)"; break;
            case AppConfig::BraillePaperSize::LETTER: paperName = "US Letter (216x279mm)"; break;
            case AppConfig::BraillePaperSize::A3: paperName = "A3 (297x420mm)"; break;
            case AppConfig::BraillePaperSize::LEGAL: paperName = "US Legal (216x356mm)"; break;
            case AppConfig::BraillePaperSize::BLISTA_260x305: paperName = "Blista (260x305mm)"; break;
            case AppConfig::BraillePaperSize::BLISTA_270x340: paperName = "Blista (270x340mm)"; break;
            case AppConfig::BraillePaperSize::BLISTA_297x304: paperName = "Blista (297x304mm)"; break;
        }
        std::string orientationName = (cfg.braille_orientation == AppConfig::BrailleOrientation::PORTRAIT) ? "Portrait" : "Landscape";
        std::string gridName;
        switch (cfg.braille_coordinate_grid) {
            case AppConfig::BrailleCoordinateGrid::NONE: gridName = "None"; break;
            case AppConfig::BrailleCoordinateGrid::DOTS: gridName = "Dots at integers"; break;
            case AppConfig::BrailleCoordinateGrid::GRID_LINES: gridName = "Grid lines"; break;
        }
        
        print("\nCurrent Settings:\n");
        print("  Protocol: " + protocolName + "\n");
        print("  Paper Size: " + paperName + "\n");
        print("  Orientation: " + orientationName + "\n");
        print("  Coordinate Grid: " + gridName + "\n");
        
        // Show DPI setting
        std::ostringstream dpiStr;
        dpiStr << std::fixed << std::setprecision(1) << cfg.braille_dpi;
        double spacingMm = 25.4 / cfg.braille_dpi;
        std::ostringstream spacingStr;
        spacingStr << std::fixed << std::setprecision(2) << spacingMm;
        print("  DPI: " + dpiStr.str() + " DPI (min spacing: " + spacingStr.str() + "mm)\n");
        
        // Show phase discontinuity mode
        std::string phaseDiscontinuityName = (cfg.braille_phase_discontinuity == AppConfig::BraillePhaseDiscontinuityMode::ARROWS) 
            ? "Arrows (compact indicators)" 
            : "Vertical line (with pattern)";
        print("  Phase Discontinuity: " + phaseDiscontinuityName + "\n");
        
        print("  Note: X and Y axes are always shown\n\n");
        
        print("(P) Select Protocol\n");
        print("(S) Select Paper Size\n");
        print("(O) Select Orientation\n");
        print("(G) Select Coordinate Grid\n");
        print("(D) Set DPI (Dot Density)\n");
        print("(J) Phase Jump Display Mode\n");
        print("(C) Configure Curve Patterns\n");
        print("(A) Advanced Parameters (margins, spacing, layout)\n");
        print("(L) Load Profile\n");
        print("(V) Save Current as Profile\n");
        print("Press ESC to go back\n\n");
        
        print(getPromptWithDepth("BRAILLE_SETTINGS_PROMPT", 3) + " ");
        int ch = 0;
        bool hasInput = false;
        
        // Check for web interface input first
        if (webServer && webServer->isRunning() && webServer->hasInput()) {
            std::string webInput = webServer->readInput();
            if (!webInput.empty()) {
                if (logger) logger->log("UI_BRAILLE_SETTINGS", "Web input received: [" + webInput + "]");
                
                // Handle web input
                if (webInput[0] == '\x1B') {
                    ch = 27;  // ESC key
                } else {
                    ch = static_cast<unsigned char>(webInput[0]);
                }
                hasInput = true;
                
                if (logger) logger->log("UI_BRAILLE_SETTINGS", "Web input mapped to ch: " + std::to_string(ch));
            }
        }
        
        // Check for keyboard input if no web input
        if (!hasInput) {
            ch = consoleInput->getch();
            hasInput = true;
        }
        
        char key = static_cast<char>(ch);
        if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
        if (key >= 32 && key <= 126) {
            print(std::string(1, key) + "\n");
        }
        
        if (key == 'p') {
            print(formatHeading("Select Protocol"));
            print("1) Index V4 (Raster Graphics - legacy)\n");
            print("2) Index V5 (Floating Dot Area - recommended)\n");
            print("\nEnter choice (1-2): ");
            
            std::string input;
            if (!readLine(input)) continue;
            
            if (input == "1") {
                cfg.braille_protocol = AppConfig::BrailleProtocol::INDEX_V4;
                print("Protocol set to Index V4\n");
                saveSettings();
            } else if (input == "2") {
                cfg.braille_protocol = AppConfig::BrailleProtocol::INDEX_V5;
                print("Protocol set to Index V5\n");
                saveSettings();
            } else {
                print("Invalid selection.\n");
            }
        } else if (key == 's') {
            print(formatHeading("Select Paper Size"));
            print("1) A4 (210x297mm)\n");
            print("2) US Letter (216x279mm)\n");
            print("3) A3 (297x420mm)\n");
            print("4) US Legal (216x356mm)\n");
            print("5) Blista 260x305mm\n");
            print("6) Blista 270x340mm\n");
            print("7) Blista 297x304mm\n");
            print("\nEnter choice (1-7): ");
            
            std::string input;
            if (!readLine(input)) continue;
            
            if (input == "1") {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::A4;
                print("Paper size set to A4\n");
                saveSettings();
            } else if (input == "2") {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::LETTER;
                print("Paper size set to US Letter\n");
                saveSettings();
            } else if (input == "3") {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::A3;
                print("Paper size set to A3\n");
                saveSettings();
            } else if (input == "4") {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::LEGAL;
                print("Paper size set to US Legal\n");
                saveSettings();
            } else if (input == "5") {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::BLISTA_260x305;
                print("Paper size set to Blista 260x305mm\n");
                saveSettings();
            } else if (input == "6") {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::BLISTA_270x340;
                print("Paper size set to Blista 270x340mm\n");
                saveSettings();
            } else if (input == "7") {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::BLISTA_297x304;
                print("Paper size set to Blista 297x304mm\n");
                saveSettings();
            } else {
                print("Invalid selection.\n");
            }
        } else if (key == 'o') {
            print(formatHeading("Select Orientation"));
            print("1) Portrait (paper is vertical)\n");
            print("2) Landscape (paper is horizontal)\n");
            print("\nNote: Paper is always loaded portrait in printer,\n");
            print("this setting rotates the print content.\n");
            print("\nEnter choice (1-2): ");
            
            std::string input;
            if (!readLine(input)) continue;
            
            if (input == "1") {
                cfg.braille_orientation = AppConfig::BrailleOrientation::PORTRAIT;
                print("Orientation set to Portrait\n");
                saveSettings();
            } else if (input == "2") {
                cfg.braille_orientation = AppConfig::BrailleOrientation::LANDSCAPE;
                print("Orientation set to Landscape\n");
                saveSettings();
            } else {
                print("Invalid selection.\n");
            }
        } else if (key == 'g') {
            print(formatHeading("Select Coordinate Grid"));
            print("1) None\n");
            print("2) Dots at integer coordinates\n");
            print("3) Full grid lines\n");
            print("\nEnter choice (1-3): ");
            
            std::string input;
            if (!readLine(input)) continue;
            
            if (input == "1") {
                cfg.braille_coordinate_grid = AppConfig::BrailleCoordinateGrid::NONE;
                print("Grid set to None\n");
                saveSettings();
            } else if (input == "2") {
                cfg.braille_coordinate_grid = AppConfig::BrailleCoordinateGrid::DOTS;
                print("Grid set to Dots\n");
                saveSettings();
            } else if (input == "3") {
                cfg.braille_coordinate_grid = AppConfig::BrailleCoordinateGrid::GRID_LINES;
                print("Grid set to Full lines\n");
                saveSettings();
            } else {
                print("Invalid selection.\n");
            }
        } else if (key == 'd') {
            print(formatHeading("Set DPI (Dot Density)"));
            print("DPI controls the minimum spacing between dots.\n");
            print("Higher DPI = smaller spacing = more dense dots\n");
            print("Lower DPI = larger spacing = less dense dots\n\n");
            print("Recommended values:\n");
            print("  10 DPI = 2.54mm spacing (very sparse, high contrast)\n");
            print("  15 DPI = 1.69mm spacing (sparse)\n");
            print("  20 DPI = 1.27mm spacing (moderate)\n");
            print("  25 DPI = 1.02mm spacing (standard, default)\n");
            print("  30 DPI = 0.85mm spacing (dense)\n");
            print("  40 DPI = 0.64mm spacing (very dense)\n\n");
            
            std::ostringstream currentDpi;
            currentDpi << std::fixed << std::setprecision(1) << cfg.braille_dpi;
            print("Current DPI: " + currentDpi.str() + "\n");
            print("Enter new DPI value (10-40): ");
            
            std::string input;
            if (!readLine(input)) continue;
            
            try {
                double newDpi = std::stod(input);
                if (newDpi >= 10.0 && newDpi <= 40.0) {
                    cfg.braille_dpi = newDpi;
                    double spacingMm = 25.4 / newDpi;
                    std::ostringstream msg;
                    msg << std::fixed << std::setprecision(1) << "DPI set to " << newDpi 
                        << " (min spacing: " << std::setprecision(2) << spacingMm << "mm)\n";
                    print(msg.str());
                    saveSettings();
                } else {
                    print("Invalid DPI. Must be between 10 and 40.\n");
                }
            } catch (...) {
                print("Invalid input. Please enter a number.\n");
            }
        } else if (key == 'j') {
            print(formatHeading("Phase Jump Display Mode"));
            print("Choose how phase discontinuities (jumps at ±180°) are displayed:\n\n");
            print("1) Arrows (compact indicators)\n");
            print("   - Small directional arrows (4mm) show where curve continues\n");
            print("   - Compact and clear, prevents perforation\n\n");
            print("2) Vertical line (with pattern)\n");
            print("   - Full vertical line connecting discontinuity\n");
            print("   - Curve pattern applied to maintain tactile consistency\n");
            print("   - Pattern spacing prevents excessive perforation\n\n");
            
            std::string currentMode = (cfg.braille_phase_discontinuity == AppConfig::BraillePhaseDiscontinuityMode::ARROWS) 
                ? "Arrows" : "Vertical line";
            print("Current mode: " + currentMode + "\n");
            print("\nEnter choice (1-2): ");
            
            std::string input;
            if (!readLine(input)) continue;
            
            if (input == "1") {
                cfg.braille_phase_discontinuity = AppConfig::BraillePhaseDiscontinuityMode::ARROWS;
                print("Phase discontinuity mode set to Arrows\n");
                saveSettings();
            } else if (input == "2") {
                cfg.braille_phase_discontinuity = AppConfig::BraillePhaseDiscontinuityMode::VERTICAL_LINE;
                print("Phase discontinuity mode set to Vertical line with pattern\n");
                saveSettings();
            } else {
                print("Invalid selection.\n");
            }
        } else if (key == 'c') {
            print(formatHeading("Configure Curve Patterns"));
            print("Define tactile patterns for each curve:\n");
            print("Pattern alternates: draw-pause-draw-pause...\n");
            print("Examples: '0' or empty = solid line (all dots)\n");
            print("          '2-1' = draw 2 dots, pause 1 dot, repeat\n");
            print("          '3-1' = draw 3 dots, pause 1 dot, repeat\n");
            print("          '4-1' = draw 4 dots, pause 1 dot, repeat\n\n");
            
            const char* curveNames[] = {"SWR", "RL (Return Loss)", "|Z| (Impedance)", "X (Reactance)", "Phase"};
            for (int i = 0; i < 5; i++) {
                print(std::string(curveNames[i]) + " (current: '" + cfg.braille_curve_patterns[i] + "'): ");
                std::string input;
                if (!readLine(input)) break;
                if (!input.empty()) {
                    cfg.braille_curve_patterns[i] = input;
                }
            }
            print("Patterns updated.\n");
            saveSettings();
        } else if (key == 'a') {
            // Advanced parameters submenu
            print(formatHeading("Advanced Braille Parameters"));
            print("These parameters control fine details of the print layout.\n");
            print("Adjust carefully to maximize paper usage.\n\n");
            
            print("Current values:\n");
            print("  Top Margin (TM): " + std::to_string(cfg.braille_top_margin) + " lines\n");
            print("  Binding Indent (BI): " + std::to_string(cfg.braille_binding_indent) + "\n");
            print("  Chars per Line (CH): " + std::to_string(cfg.braille_chars_per_line) + "\n");
            print("  Line Spacing (LS): " + std::to_string(cfg.braille_line_spacing) + " (= " 
                  + std::to_string(cfg.braille_line_spacing / 10.0) + "mm)\n");
            print("  Graph Width % Portrait: " + std::to_string(static_cast<int>(cfg.braille_graph_width_percent_portrait * 100)) + "%\n");
            print("  Graph Width % Landscape: " + std::to_string(static_cast<int>(cfg.braille_graph_width_percent_landscape * 100)) + "%\n");
            print("  Graph Height % Portrait: " + std::to_string(static_cast<int>(cfg.braille_graph_height_percent_portrait * 100)) + "%\n");
            print("  Graph Height % Landscape: " + std::to_string(static_cast<int>(cfg.braille_graph_height_percent_landscape * 100)) + "%\n");
            print("  Origin X: " + std::to_string(cfg.braille_origin_x_mm) + "mm\n");
            print("  Origin Y: " + std::to_string(cfg.braille_origin_y_mm) + "mm\n");
            print("  Y-Axis Space: " + std::to_string(cfg.braille_y_axis_space_mm) + "mm\n\n");
            
            print("(1) Top Margin (0-10 lines, 0=no margin for max space)\n");
            print("(2) Binding Indent (0-10)\n");
            print("(3) Characters per Line (10-50)\n");
            print("(4) Line Spacing (50-100, where 50=5.0mm, 100=10.0mm)\n");
            print("(5) Graph Width % Portrait (80-99)\n");
            print("(6) Graph Width % Landscape (80-99)\n");
            print("(7) Graph Height % Portrait (60-90)\n");
            print("(8) Graph Height % Landscape (60-90)\n");
            print("(9) Origin X offset (0-10mm)\n");
            print("(0) Origin Y offset (0-20mm)\n");
            print("(Y) Y-Axis Space (1-5mm)\n");
            print("Press ESC to go back\n\n");
            
            bool inAdvanced = true;
            while (inAdvanced) {
                print("Advanced parameter to change: ");
                char advKey = static_cast<char>(consoleInput->getch());
                if (advKey >= 'A' && advKey <= 'Z') advKey = advKey - 'A' + 'a';
                if (advKey >= 32 && advKey <= 126) {
                    print(std::string(1, advKey) + "\n");
                }
                
                if (advKey == 27) {  // ESC
                    inAdvanced = false;
                } else if (advKey == '1') {
                    print("Enter Top Margin (0-10): ");
                    std::string input;
                    if (readLine(input)) {
                        try {
                            int val = std::stoi(input);
                            if (val >= 0 && val <= 10) {
                                cfg.braille_top_margin = val;
                                print("Top Margin set to " + std::to_string(val) + "\n");
                                saveSettings();
                            } else {
                                print("Invalid range. Must be 0-10.\n");
                            }
                        } catch (...) {
                            print("Invalid input.\n");
                        }
                    }
                } else if (advKey == '2') {
                    print("Enter Binding Indent (0-10): ");
                    std::string input;
                    if (readLine(input)) {
                        try {
                            int val = std::stoi(input);
                            if (val >= 0 && val <= 10) {
                                cfg.braille_binding_indent = val;
                                print("Binding Indent set to " + std::to_string(val) + "\n");
                                saveSettings();
                            } else {
                                print("Invalid range. Must be 0-10.\n");
                            }
                        } catch (...) {
                            print("Invalid input.\n");
                        }
                    }
                } else if (advKey == '3') {
                    print("Enter Characters per Line (10-50): ");
                    std::string input;
                    if (readLine(input)) {
                        try {
                            int val = std::stoi(input);
                            if (val >= 10 && val <= 50) {
                                cfg.braille_chars_per_line = val;
                                print("Characters per Line set to " + std::to_string(val) + "\n");
                                saveSettings();
                            } else {
                                print("Invalid range. Must be 10-50.\n");
                            }
                        } catch (...) {
                            print("Invalid input.\n");
                        }
                    }
                } else if (advKey == '4') {
                    print("Enter Line Spacing (50-100, where 50=5.0mm, 100=10.0mm): ");
                    std::string input;
                    if (readLine(input)) {
                        try {
                            int val = std::stoi(input);
                            if (val >= 50 && val <= 100) {
                                cfg.braille_line_spacing = val;
                                print("Line Spacing set to " + std::to_string(val) + " (" + std::to_string(val/10.0) + "mm)\n");
                                saveSettings();
                            } else {
                                print("Invalid range. Must be 50-100.\n");
                            }
                        } catch (...) {
                            print("Invalid input.\n");
                        }
                    }
                } else if (advKey == '5') {
                    print("Enter Graph Width % Portrait (80-99): ");
                    std::string input;
                    if (readLine(input)) {
                        try {
                            int val = std::stoi(input);
                            if (val >= 80 && val <= 99) {
                                cfg.braille_graph_width_percent_portrait = val / 100.0;
                                print("Graph Width % Portrait set to " + std::to_string(val) + "%\n");
                                saveSettings();
                            } else {
                                print("Invalid range. Must be 80-99.\n");
                            }
                        } catch (...) {
                            print("Invalid input.\n");
                        }
                    }
                } else if (advKey == '6') {
                    print("Enter Graph Width % Landscape (80-99): ");
                    std::string input;
                    if (readLine(input)) {
                        try {
                            int val = std::stoi(input);
                            if (val >= 80 && val <= 99) {
                                cfg.braille_graph_width_percent_landscape = val / 100.0;
                                print("Graph Width % Landscape set to " + std::to_string(val) + "%\n");
                                saveSettings();
                            } else {
                                print("Invalid range. Must be 80-99.\n");
                            }
                        } catch (...) {
                            print("Invalid input.\n");
                        }
                    }
                } else if (advKey == '7') {
                    print("Enter Graph Height % Portrait (60-90): ");
                    std::string input;
                    if (readLine(input)) {
                        try {
                            int val = std::stoi(input);
                            if (val >= 60 && val <= 90) {
                                cfg.braille_graph_height_percent_portrait = val / 100.0;
                                print("Graph Height % Portrait set to " + std::to_string(val) + "%\n");
                                saveSettings();
                            } else {
                                print("Invalid range. Must be 60-90.\n");
                            }
                        } catch (...) {
                            print("Invalid input.\n");
                        }
                    }
                } else if (advKey == '8') {
                    print("Enter Graph Height % Landscape (60-90): ");
                    std::string input;
                    if (readLine(input)) {
                        try {
                            int val = std::stoi(input);
                            if (val >= 60 && val <= 90) {
                                cfg.braille_graph_height_percent_landscape = val / 100.0;
                                print("Graph Height % Landscape set to " + std::to_string(val) + "%\n");
                                saveSettings();
                            } else {
                                print("Invalid range. Must be 60-90.\n");
                            }
                        } catch (...) {
                            print("Invalid input.\n");
                        }
                    }
                } else if (advKey == '9') {
                    print("Enter Origin X offset (0-10mm): ");
                    std::string input;
                    if (readLine(input)) {
                        try {
                            double val = std::stod(input);
                            if (val >= 0.0 && val <= 10.0) {
                                cfg.braille_origin_x_mm = val;
                                print("Origin X set to " + std::to_string(val) + "mm\n");
                                saveSettings();
                            } else {
                                print("Invalid range. Must be 0-10.\n");
                            }
                        } catch (...) {
                            print("Invalid input.\n");
                        }
                    }
                } else if (advKey == '0') {
                    print("Enter Origin Y offset (0-20mm): ");
                    std::string input;
                    if (readLine(input)) {
                        try {
                            double val = std::stod(input);
                            if (val >= 0.0 && val <= 20.0) {
                                cfg.braille_origin_y_mm = val;
                                print("Origin Y set to " + std::to_string(val) + "mm\n");
                                saveSettings();
                            } else {
                                print("Invalid range. Must be 0-20.\n");
                            }
                        } catch (...) {
                            print("Invalid input.\n");
                        }
                    }
                } else if (advKey == 'y') {
                    print("Enter Y-Axis Space (1-5mm): ");
                    std::string input;
                    if (readLine(input)) {
                        try {
                            double val = std::stod(input);
                            if (val >= 1.0 && val <= 5.0) {
                                cfg.braille_y_axis_space_mm = val;
                                print("Y-Axis Space set to " + std::to_string(val) + "mm\n");
                                saveSettings();
                            } else {
                                print("Invalid range. Must be 1-5.\n");
                            }
                        } catch (...) {
                            print("Invalid input.\n");
                        }
                    }
                } else {
                    print("Unknown option.\n");
                }
            }
        } else if (key == 'l') {
            // Load braille profile
            print(formatHeading("Load Braille Profile"));
            std::vector<std::string> profiles = listBrailleProfiles();
            
            if (profiles.empty()) {
                print("No braille profiles found in config folder.\n");
                print("Press any key to continue...");
                std::string dummy;
                readLine(dummy);
            } else {
                print("Available profiles:\n");
                for (size_t i = 0; i < profiles.size(); i++) {
                    print("  " + std::to_string(i + 1) + ") " + profiles[i] + "\n");
                }
                print("\nEnter profile number (1-" + std::to_string(profiles.size()) + ") or 0 to cancel: ");
                
                std::string input;
                if (readLine(input)) {
                    try {
                        int choice = std::stoi(input);
                        if (choice > 0 && choice <= static_cast<int>(profiles.size())) {
                            std::string profileName = profiles[choice - 1];
                            std::string err;
                            
                            if (loadBrailleProfile(cfg, profileName, err)) {
                                print("Profile '" + profileName + "' loaded successfully!\n");
                                print("\nApply to main settings? (y/n): ");
                                std::string confirm;
                                if (readLine(confirm) && (confirm == "y" || confirm == "Y")) {
                                    saveSettings();
                                    print("Settings saved to main configuration.\n");
                                } else {
                                    print("Profile loaded but not saved to main settings.\n");
                                    print("Settings will revert on restart unless you save them.\n");
                                }
                            } else {
                                print("Failed to load profile: " + err + "\n");
                            }
                        } else if (choice == 0) {
                            print("Canceled.\n");
                        } else {
                            print("Invalid selection.\n");
                        }
                    } catch (...) {
                        print("Invalid input.\n");
                    }
                }
            }
        } else if (key == 'v') {
            // Save current settings as profile
            print(formatHeading("Save Current Settings as Profile"));
            print("Enter profile name (without .ini extension): ");
            
            std::string profileName;
            if (readLine(profileName)) {
                // Remove leading/trailing whitespace manually
                while (!profileName.empty() && isspace((unsigned char)profileName.front())) 
                    profileName.erase(profileName.begin());
                while (!profileName.empty() && isspace((unsigned char)profileName.back())) 
                    profileName.pop_back();
                    
                if (profileName.empty()) {
                    print("Profile name cannot be empty.\n");
                } else {
                    std::string err;
                    if (saveBrailleProfile(cfg, profileName, err)) {
                        print("Profile '" + profileName + "' saved successfully!\n");
                        print("Location: config/" + profileName + ".ini\n");
                    } else {
                        print("Failed to save profile: " + err + "\n");
                    }
                }
            }
        } else if (key == 27) {  // ESC
            break;
        } else {
            print("Unknown command.\n");
        }
    }
    
    if (logger) logger->log("UI", "Exited braille printer settings menu");
}

void ConsoleUI::webInterfaceMenu() {
    clearScreen();
    if (logger) logger->log("UI", "Entered web interface menu");
    
    print(formatHeading(translation.get("WEB_INTERFACE_TITLE", "Web Interface")));
    
    if (webServer && webServer->isRunning()) {
        // Server is running - show status and stop option
        print(translation.get("WEB_INTERFACE_RUNNING", "Web interface is RUNNING") + "\n\n");
        
        print(translation.get("WEB_INTERFACE_URL", "Local access:") + " " + webServer->getServerURL() + "\n");
        
        // Show all local IP addresses
        auto ipAddresses = webServer->getLocalIPAddresses();
        if (!ipAddresses.empty()) {
            print("\n" + translation.get("WEB_INTERFACE_NETWORK", "Network access:") + "\n");
            for (const auto& ip : ipAddresses) {
                print("  http://" + ip + ":8080\n");
            }
        }
        
        print("\n" + translation.get("WEB_INTERFACE_INFO", 
            "You can now access the terminal from any browser on your local network.\n"
            "Use this URL on your smartphone or tablet to control the device remotely.\n") + "\n");
        
        print(translation.get("WEB_INTERFACE_STOP_PROMPT", "Press 'S' to stop the web interface, or ESC to go back: "));
        
        setUIContext("web_interface", {
            {"s", translation.get("WEB_INTERFACE_STOP", "Stop"), false}
        });
        
        char key = static_cast<char>(consoleInput->getch());
        if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
        if (key >= 32 && key <= 126) {
            print(std::string(1, key) + "\n");
        }
        
        if (key == 's') {
            print(translation.get("WEB_INTERFACE_STOPPING", "Stopping web interface...") + "\n");
            webServer->stop();
            webServer.reset();
            print(translation.get("WEB_INTERFACE_STOPPED", "Web interface stopped.") + "\n");
            if (logger) logger->log("WEBSERVER", "Web interface stopped by user");
        }
    } else {
        // Server is not running - show start option
        print(translation.get("WEB_INTERFACE_NOT_RUNNING", "Web interface is currently OFF") + "\n\n");
        
        print(translation.get("WEB_INTERFACE_DESC", 
            "The web interface allows you to:\n"
            "  - Control the application from any browser\n"
            "  - Use on local network (e.g., smartphone at antenna mast)\n"
            "  - Accessible with screen readers\n"
            "  - Audio output streaming (planned for future release)\n\n"
            "Security note: HTTP only, local network only, no authentication.\n") + "\n");
        
        print(translation.get("WEB_INTERFACE_START_PROMPT", "Press 'S' to start the web interface, or ESC to go back: "));
        
        setUIContext("web_interface", {
            {"s", translation.get("WEB_INTERFACE_START", "Start"), false}
        });
        
        char key = static_cast<char>(consoleInput->getch());
        if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
        if (key >= 32 && key <= 126) {
            print(std::string(1, key) + "\n");
        }
        
        if (key == 's') {
            print(translation.get("WEB_INTERFACE_STARTING", "Starting web interface...") + "\n");
            
            // Create and start web server
            webServer = std::make_unique<WebServer>(logger);
            
            if (webServer->start(8080, "0.0.0.0")) {
                print(translation.get("WEB_INTERFACE_STARTED", "Web interface started successfully!") + "\n\n");
                
                print(translation.get("WEB_INTERFACE_URL", "Local access:") + " " + webServer->getServerURL() + "\n");
                
                // Show all local IP addresses
                auto ipAddresses = webServer->getLocalIPAddresses();
                if (!ipAddresses.empty()) {
                    print("\n" + translation.get("WEB_INTERFACE_NETWORK", "Network access:") + "\n");
                    for (const auto& ip : ipAddresses) {
                        print("  http://" + ip + ":8080\n");
                    }
                }
                
                print("\n" + translation.get("WEB_INTERFACE_READY", "Ready for browser connections.") + "\n");
                if (logger) logger->log("WEBSERVER", "Web interface started successfully");
            } else {
                print(translation.get("WEB_INTERFACE_START_FAILED", "Failed to start web interface. Port might be in use.") + "\n");
                webServer.reset();
                if (logger) logger->log("WEBSERVER", "Failed to start web interface");
            }
            
            print(translation.get("MSG_PRESS_ANY_KEY", "\nPress any key to continue...") + "\n");
            consoleInput->getch();
        }
    }
    
    clearScreen();
    if (logger) logger->log("UI", "Exited web interface menu");
}

void ConsoleUI::runFirstStartWizard() {
    clearScreen();
    print(formatHeading(translation.get("FIRST_START_WELCOME", "Welcome to NanoVNA CLI Accessible!")));
    print(translation.get("FIRST_START_SETUP", "This appears to be your first time running the program. Let's configure some basic settings.") + "\n\n");
    
    // Detect system language
    std::string detectedLang = "eng";  // Default to English
    
#if defined(_WIN32)
    // Windows: Use GetUserDefaultUILanguage
    LANGID langId = GetUserDefaultUILanguage();
    WORD primaryLang = PRIMARYLANGID(langId);
    
    // Map common language IDs to our language codes
    switch (primaryLang) {
        case 0x07:  // LANG_GERMAN
            detectedLang = "deu";
            break;
        case 0x09:  // LANG_ENGLISH
        default:
            detectedLang = "eng";
            break;
    }
#else
    // Linux/Unix: Check LANG environment variable
    const char* lang_env = std::getenv("LANG");
    if (lang_env) {
        std::string lang_str(lang_env);
        // Extract language code (first two characters before underscore or dot)
        if (lang_str.length() >= 2) {
            std::string lang_code = lang_str.substr(0, 2);
            // Convert to lowercase
            std::transform(lang_code.begin(), lang_code.end(), lang_code.begin(), ::tolower);
            
            // Map to our language codes
            if (lang_code == "de") {
                detectedLang = "deu";
            } else if (lang_code == "en") {
                detectedLang = "eng";
            }
            // Add more language mappings as needed
        }
    }
#endif
    
    if (logger) logger->log("WIZARD", "Detected system language: " + detectedLang);
    
    // Get available languages
    std::string err;
    auto languages = TranslationManager::getAvailableLanguages(err);
    
    if (!languages.empty()) {
        // Check if detected language is available
        bool detectedAvailable = false;
        for (const auto& lang : languages) {
            if (lang.first == detectedLang) {
                detectedAvailable = true;
                break;
            }
        }
        
        std::string selectedLang = detectedLang;
        
        // If detected language is available, inform user and offer to change
        if (detectedAvailable) {
            // Find the name for detected language
            std::string detectedName = detectedLang;
            for (const auto& lang : languages) {
                if (lang.first == detectedLang) {
                    detectedName = lang.second;
                    break;
                }
            }
            
            print(translation.format("FIRST_START_LANGUAGE_DETECTED", "System language detected: {0}", detectedName) + "\n");
            
            // Try to load the detected language
            std::string loadErr;
            if (translation.loadLanguage(detectedLang, loadErr)) {
                cfg.language = detectedLang;
                if (logger) logger->log("WIZARD", "Loaded detected language: " + detectedLang);
            }
        }
        
        // Show language selection
        print("\n" + translation.get("FIRST_START_LANGUAGE_SELECT", "Select your preferred language:") + "\n");
        for (size_t i = 0; i < languages.size(); ++i) {
            std::string marker = "";
            if (languages[i].first == selectedLang) {
                marker = " [*]";
            }
            print(std::to_string(i + 1) + ") " + languages[i].second + " (" + languages[i].first + ")" + marker + "\n");
        }
        
        print("\n" + translation.get("FIRST_START_ENTER_NUMBER", "Enter number: ") + getDepthIndicator(1) + " ");
        std::string input;
        if (readLine(input)) {
            if (!input.empty()) {
                try {
                    int idx = std::stoi(input);
                    if (idx > 0 && idx <= static_cast<int>(languages.size())) {
                        selectedLang = languages[idx - 1].first;
                        std::string loadErr;
                        if (translation.loadLanguage(selectedLang, loadErr)) {
                            cfg.language = selectedLang;
                            if (logger) logger->log("WIZARD", "User selected language: " + selectedLang);
                        }
                    }
                } catch (...) {
                    // Invalid input, keep detected language
                }
            }
        }
    }
    
    // Band plan selection
    print("\n" + translation.get("FIRST_START_BANDPLAN_SELECT", "Select your default band plan (amateur radio region):") + "\n");
    
    auto bandplans = getAvailableBandPlans(err);
    
    if (!bandplans.empty()) {
        // Show band plans with descriptions
        for (size_t i = 0; i < bandplans.size(); ++i) {
            print(std::to_string(i + 1) + ") " + bandplans[i].second + " (" + bandplans[i].first + ")");
            
            // Add description if available
            if (bandplans[i].first == "usa") {
                print(" - " + translation.get("FIRST_START_BANDPLAN_DESCRIPTION_USA", "USA (ARRL/FCC band plan)"));
            } else if (bandplans[i].first == "deu") {
                print(" - " + translation.get("FIRST_START_BANDPLAN_DESCRIPTION_DEU", "Germany/Europe (DARC/IARU Region 1)"));
            }
            
            print("\n");
        }
        
        print("\n" + translation.get("FIRST_START_ENTER_NUMBER", "Enter number: ") + getDepthIndicator(1) + " ");
        std::string input;
        if (readLine(input)) {
            if (!input.empty()) {
                try {
                    int idx = std::stoi(input);
                    if (idx > 0 && idx <= static_cast<int>(bandplans.size())) {
                        cfg.bandplan = bandplans[idx - 1].first;
                        if (logger) logger->log("WIZARD", "User selected band plan: " + cfg.bandplan);
                    }
                } catch (...) {
                    // Invalid input, keep default
                }
            }
        }
    }
    
    // Mark first start as complete
    cfg.first_start = false;
    saveSettings();
    
    print("\n" + translation.get("FIRST_START_SETUP_COMPLETE", "Setup complete! Your settings have been saved.") + "\n");
    print("─────────────────────────────────────────────────────────\n\n");
    
    if (logger) logger->log("WIZARD", "First-start wizard completed");
}

void ConsoleUI::goToMenu(std::vector<MeasurementPoint>& pts, size_t& currentPage, size_t rowsPerPage) {
    clearScreen();
    if (pts.empty()) {
        print(translation.get("ERROR_NO_DATA", "No data") + "\n");
        return;
    }
    
    print(formatHeading(translation.get("GOTO_TITLE", "Go To")));
    print(translation.get("GOTO_POINT", "(P)oint") + "\n");
    print(translation.get("GOTO_FREQUENCY", "(F)requency") + "\n");
    print(translation.get("GOTO_MINIMUM", "(M)inimum") + "\n");
    print(translation.get("GOTO_MAXIMUM", "(X)maximum") + "\n");
    print(translation.get("HELP_COMMAND", "(H)elp") + "\n");
    print(translation.get("MSG_PRESS_ESC_BACK", "Press ESC to go back") + "\n\n");
    
    char key = static_cast<char>(consoleInput->getch());
    if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
    // Echo printable ASCII characters only (32-126), filtering control characters like ESC
    if (key >= 32 && key <= 126) {
        print(std::string(1, key) + "\n");
    }
    
    if (key == 'p') {
        // Go to point
        print(translation.format("GOTO_POINT_PROMPT", "Enter point number (0-{0}): ", (pts.size() - 1)) + " ");
        std::string input;
        if (!readLine(input) || input.empty()) return;
        
        try {
            size_t pointNum = std::stoull(input);
            if (pointNum < pts.size()) {
                // Calculate which page this point is on
                currentPage = pointNum / rowsPerPage;
                print(translation.format("GOTO_POINT_JUMPED", "Jumped to point {0} ({1} Hz)", pointNum, pts[pointNum].freq) + "\n");
                if (logger) logger->log("UI", "Go to point: " + std::to_string(pointNum));
            } else {
                print(translation.get("GOTO_POINT_INVALID", "Invalid point number.") + "\n");
            }
        } catch (...) {
            print(translation.get("GOTO_POINT_INVALID", "Invalid point number.") + "\n");
        }
        
    } else if (key == 'f') {
        // Go to frequency
        print(translation.get("GOTO_FREQ_PROMPT", "Enter frequency in Hz: ") + " ");
        std::string input;
        if (!readLine(input) || input.empty()) return;
        
        uint64_t targetFreq = 0;
        if (parseFrequencyString(input, targetFreq)) {
            
            // Find the point with the closest frequency (round down)
            size_t bestIdx = 0;
            uint64_t bestFreq = pts[0].freq;
            
            for (size_t i = 0; i < pts.size(); i++) {
                if (pts[i].freq <= targetFreq && pts[i].freq >= bestFreq) {
                    bestIdx = i;
                    bestFreq = pts[i].freq;
                }
                if (pts[i].freq > targetFreq) {
                    break;  // We've passed the target, stop searching
                }
            }
            
            // Special case: if target is lower than first freq, jump to highest
            if (targetFreq < pts[0].freq && pts.size() > 1) {
                bestIdx = pts.size() - 1;
                bestFreq = pts[bestIdx].freq;
            }
            
            // Calculate which page this point is on
            currentPage = bestIdx / rowsPerPage;
            
            if (bestFreq != targetFreq) {
                print(translation.format("GOTO_FREQ_ROUNDED", "Frequency {0} Hz not found, rounding to {1} Hz", targetFreq, bestFreq) + "\n");
            }
            
            print(translation.format("GOTO_FREQ_JUMPED", "Jumped to frequency {0} Hz (point {1})", bestFreq, bestIdx) + "\n");
            
            if (logger) logger->log("UI", "Go to frequency: " + std::to_string(targetFreq) + " Hz -> point " + std::to_string(bestIdx));
            
        } else {
            print(translation.get("GOTO_FREQ_INVALID", "Invalid frequency.") + "\n");
        }
        
    } else if (key == 'm') {
        // Go to minimum
        print(translation.get("GOTO_MIN_CURVE", "Select curve for minimum:\\n(1) SWR  (2) Return Loss  (3) |Z|  (4) Reactance X  (5) Phase") + "\n");
        print(translation.get("GOTO_MIN_PROMPT", "Enter curve number: ") + " ");
        
        std::string input;
        if (!readLine(input) || input.empty()) return;
        
        try {
            int curveNum = std::stoi(input);
            if (curveNum < 1 || curveNum > 5) {
                print(translation.get("GOTO_MIN_INVALID", "Invalid curve number.") + "\n");
                return;
            }
            
            // Find minimum for selected curve
            size_t minIdx = 0;
            double minVal = 1e100;
            const char* curveNames[] = {"SWR", "Return Loss", "|Z|", "Reactance X", "Phase"};
            
            for (size_t i = 0; i < pts.size(); i++) {
                double val = 0.0;
                switch (curveNum) {
                    case 1: val = pts[i].swr; break;
                    case 2: val = pts[i].rl; break;
                    case 3: val = pts[i].impedance_mag; break;
                    case 4: val = pts[i].X; break;
                    case 5: val = pts[i].phase_deg; break;
                }
                
                if (val < minVal) {
                    minVal = val;
                    minIdx = i;
                }
            }
            
            // Calculate which page this point is on
            currentPage = minIdx / rowsPerPage;
            
            print(translation.format("GOTO_MIN_JUMPED", "Jumped to minimum of {0} at point {1} ({2} Hz)", curveNames[curveNum - 1], minIdx, pts[minIdx].freq) + "\n");
            
            if (logger) logger->log("UI", "Go to minimum of curve " + std::to_string(curveNum) + " at point " + std::to_string(minIdx));
            
        } catch (...) {
            print(translation.get("GOTO_MIN_INVALID", "Invalid curve number.") + "\n");
        }
        
    } else if (key == 'x') {
        // Go to maximum
        print(translation.get("GOTO_MAX_CURVE", "Select curve for maximum:\\n(1) SWR  (2) Return Loss  (3) |Z|  (4) Reactance X  (5) Phase") + "\n");
        print(translation.get("GOTO_MAX_PROMPT", "Enter curve number: ") + " ");
        
        std::string input;
        if (!readLine(input) || input.empty()) return;
        
        try {
            int curveNum = std::stoi(input);
            if (curveNum < 1 || curveNum > 5) {
                print(translation.get("GOTO_MAX_INVALID", "Invalid curve number.") + "\n");
                return;
            }
            
            // Find maximum for selected curve
            size_t maxIdx = 0;
            double maxVal = -1e100;
            const char* curveNames[] = {"SWR", "Return Loss", "|Z|", "Reactance X", "Phase"};
            
            for (size_t i = 0; i < pts.size(); i++) {
                double val = 0.0;
                switch (curveNum) {
                    case 1: val = pts[i].swr; break;
                    case 2: val = pts[i].rl; break;
                    case 3: val = pts[i].impedance_mag; break;
                    case 4: val = pts[i].X; break;
                    case 5: val = pts[i].phase_deg; break;
                }
                
                if (val > maxVal) {
                    maxVal = val;
                    maxIdx = i;
                }
            }
            
            // Calculate which page this point is on
            currentPage = maxIdx / rowsPerPage;
            
            print(translation.format("GOTO_MAX_JUMPED", "Jumped to maximum of {0} at point {1} ({2} Hz)", curveNames[curveNum - 1], maxIdx, pts[maxIdx].freq) + "\n");
            
            if (logger) logger->log("UI", "Go to maximum of curve " + std::to_string(curveNum) + " at point " + std::to_string(maxIdx));
            
        } catch (...) {
            print(translation.get("GOTO_MAX_INVALID", "Invalid curve number.") + "\n");
        }
        
    } else if (key == 'h') {
        // Help
        print(HelpModule::getGoToMenuHelp(translation));
    } else if (key == 27) {
        // ESC - back
        return;
    } else {
        print(translation.get("ERROR_UNKNOWN_COMMAND", "Unknown command.") + "\n");
    }
}

void ConsoleUI::goToMenuAcoustic(AcousticAnalyzer& analyzer, const std::vector<MeasurementPoint>& pts) {
    clearScreen();
    if (pts.empty()) {
        print(translation.get("ERROR_NO_DATA", "No measurement data available for acoustic analysis.") + "\n");
        return;
    }
    
    print(formatHeading(translation.get("GOTO_TITLE", "Go To")));
    print(translation.get("GOTO_POINT", "(P)oint") + "\n");
    print(translation.get("GOTO_FREQUENCY", "(F)requency") + "\n");
    print(translation.get("GOTO_MINIMUM", "(M)inimum") + "\n");
    print(translation.get("GOTO_MAXIMUM", "(X)maximum") + "\n");
    print(translation.get("GOTO_CROSSPOINT", "(C)ross Point") + "\n");
    print(translation.get("HELP_COMMAND", "(H)elp") + "\n");
    print(translation.get("MSG_PRESS_ESC_BACK", "Press ESC to go back") + "\n\n");
    
    char key = static_cast<char>(consoleInput->getch());
    if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
    // Echo printable ASCII characters only (32-126), filtering control characters like ESC
    if (key >= 32 && key <= 126) {
        print(std::string(1, key) + "\n");
    }
    
    // Store current playback state
    PlaybackState previousState = analyzer.getState();
    bool wasFrozen = (previousState == PlaybackState::FROZEN);
    bool wasPaused = (previousState == PlaybackState::PAUSED);
    
    if (key == 'p') {
        // Go to point
        print(translation.format("GOTO_POINT_PROMPT", "Enter point number (0-{0}): ", (pts.size() - 1)) + " ");
        std::string input;
        if (!readLine(input) || input.empty()) return;
        
        try {
            size_t pointNum = std::stoull(input);
            if (pointNum < pts.size()) {
                analyzer.setPosition(pointNum);
                print(translation.format("GOTO_POINT_JUMPED", "Jumped to point {0} ({1} Hz)", pointNum, pts[pointNum].freq) + "\n");
                if (logger) logger->log("UI", "Go to point in acoustic: " + std::to_string(pointNum));
            } else {
                print(translation.get("GOTO_POINT_INVALID", "Invalid point number.") + "\n");
            }
        } catch (...) {
            print(translation.get("GOTO_POINT_INVALID", "Invalid point number.") + "\n");
        }
        
    } else if (key == 'f') {
        // Go to frequency
        print(translation.get("GOTO_FREQ_PROMPT", "Enter frequency in Hz: ") + " ");
        std::string input;
        if (!readLine(input) || input.empty()) return;
        
        uint64_t targetFreq = 0;
        if (parseFrequencyString(input, targetFreq)) {
            
            // Find the point with the closest frequency (round down)
            size_t bestIdx = 0;
            uint64_t bestFreq = pts[0].freq;
            
            for (size_t i = 0; i < pts.size(); i++) {
                if (pts[i].freq <= targetFreq && pts[i].freq >= bestFreq) {
                    bestIdx = i;
                    bestFreq = pts[i].freq;
                }
                if (pts[i].freq > targetFreq) {
                    break;  // We've passed the target, stop searching
                }
            }
            
            // Special case: if target is lower than first freq, jump to highest
            if (targetFreq < pts[0].freq && pts.size() > 1) {
                bestIdx = pts.size() - 1;
                bestFreq = pts[bestIdx].freq;
            }
            
            analyzer.setPosition(bestIdx);
            
            if (bestFreq != targetFreq) {
                print(translation.format("GOTO_FREQ_ROUNDED", "Frequency {0} Hz not found, rounding to {1} Hz", targetFreq, bestFreq) + "\n");
            }
            
            print(translation.format("GOTO_FREQ_JUMPED", "Jumped to frequency {0} Hz (point {1})", bestFreq, bestIdx) + "\n");
            
            if (logger) logger->log("UI", "Go to frequency in acoustic: " + std::to_string(targetFreq) + " Hz -> point " + std::to_string(bestIdx));
            
        } else {
            print(translation.get("GOTO_FREQ_INVALID", "Invalid frequency.") + "\n");
        }
        
    } else if (key == 'm') {
        // Go to minimum
        print(translation.get("GOTO_MIN_CURVE", "Select curve for minimum:\\n(1) SWR  (2) Return Loss  (3) |Z|  (4) Reactance X  (5) Phase") + "\n");
        print(translation.get("GOTO_MIN_PROMPT", "Enter curve number: ") + " ");
        
        std::string input;
        if (!readLine(input) || input.empty()) return;
        
        try {
            int curveNum = std::stoi(input);
            if (curveNum < 1 || curveNum > 5) {
                print(translation.get("GOTO_MIN_INVALID", "Invalid curve number.") + "\n");
                return;
            }
            
            // Find minimum for selected curve
            size_t minIdx = 0;
            double minVal = 1e100;
            const char* curveNames[] = {"SWR", "Return Loss", "|Z|", "Reactance X", "Phase"};
            
            for (size_t i = 0; i < pts.size(); i++) {
                double val = 0.0;
                switch (curveNum) {
                    case 1: val = pts[i].swr; break;
                    case 2: val = pts[i].rl; break;
                    case 3: val = pts[i].impedance_mag; break;
                    case 4: val = pts[i].X; break;
                    case 5: val = pts[i].phase_deg; break;
                }
                
                if (val < minVal) {
                    minVal = val;
                    minIdx = i;
                }
            }
            
            analyzer.setPosition(minIdx);
            
            print(translation.format("GOTO_MIN_JUMPED", "Jumped to minimum of {0} at point {1} ({2} Hz)", curveNames[curveNum - 1], minIdx, pts[minIdx].freq) + "\n");
            
            if (logger) logger->log("UI", "Go to minimum in acoustic: curve " + std::to_string(curveNum) + " at point " + std::to_string(minIdx));
            
        } catch (...) {
            print(translation.get("GOTO_MIN_INVALID", "Invalid curve number.") + "\n");
        }
        
    } else if (key == 'x') {
        // Go to maximum
        print(translation.get("GOTO_MAX_CURVE", "Select curve for maximum:\\n(1) SWR  (2) Return Loss  (3) |Z|  (4) Reactance X  (5) Phase") + "\n");
        print(translation.get("GOTO_MAX_PROMPT", "Enter curve number: ") + " ");
        
        std::string input;
        if (!readLine(input) || input.empty()) return;
        
        try {
            int curveNum = std::stoi(input);
            if (curveNum < 1 || curveNum > 5) {
                print(translation.get("GOTO_MAX_INVALID", "Invalid curve number.") + "\n");
                return;
            }
            
            // Find maximum for selected curve
            size_t maxIdx = 0;
            double maxVal = -1e100;
            const char* curveNames[] = {"SWR", "Return Loss", "|Z|", "Reactance X", "Phase"};
            
            for (size_t i = 0; i < pts.size(); i++) {
                double val = 0.0;
                switch (curveNum) {
                    case 1: val = pts[i].swr; break;
                    case 2: val = pts[i].rl; break;
                    case 3: val = pts[i].impedance_mag; break;
                    case 4: val = pts[i].X; break;
                    case 5: val = pts[i].phase_deg; break;
                }
                
                if (val > maxVal) {
                    maxVal = val;
                    maxIdx = i;
                }
            }
            
            analyzer.setPosition(maxIdx);
            
            print(translation.format("GOTO_MAX_JUMPED", "Jumped to maximum of {0} at point {1} ({2} Hz)", curveNames[curveNum - 1], maxIdx, pts[maxIdx].freq) + "\n");
            
            if (logger) logger->log("UI", "Go to maximum in acoustic: curve " + std::to_string(curveNum) + " at point " + std::to_string(maxIdx));
            
        } catch (...) {
            print(translation.get("GOTO_MAX_INVALID", "Invalid curve number.") + "\n");
        }
        
    } else if (key == 'c') {
        // Go to cross point
        print(formatHeading(translation.get("GOTO_CROSSPOINT_TITLE", "Find Cross Points")));
        print(translation.get("GOTO_CROSSPOINT_SELECT", "Select curves to check for intersections:") + "\n");
        print("(1) SWR  (2) Return Loss  (3) |Z|  (4) Reactance X  (5) Phase\n");
        print(translation.get("GOTO_CROSSPOINT_PROMPT", "Enter curve numbers separated by spaces (e.g., '1 3'): ") + " ");
        
        std::string input;
        if (!readLine(input) || input.empty()) return;
        
        // Parse selected curves
        std::vector<int> selectedCurves;
        std::istringstream iss(input);
        int curveNum;
        while (iss >> curveNum) {
            if (curveNum >= 1 && curveNum <= 5) {
                selectedCurves.push_back(curveNum);
            }
        }
        
        if (selectedCurves.size() < 2) {
            print(translation.get("GOTO_CROSSPOINT_MIN_TWO", "At least two curves are required to find intersections.") + "\n");
            return;
        }
        
        // Find all cross points between selected curves
        struct CrossPoint {
            size_t pointIdx;
            std::vector<int> curves;  // Curves that intersect at this point
            uint64_t freq;
            std::vector<double> values;  // Values of each curve at this point
        };
        
        std::vector<CrossPoint> crossPoints;
        const char* curveNames[] = {"SWR", "Return Loss", "|Z|", "Reactance X", "Phase"};
        
        // Helper function to get curve value
        auto getCurveValue = [&](const MeasurementPoint& pt, int curveNum) -> double {
            switch (curveNum) {
                case 1: return pt.swr;
                case 2: return pt.rl;
                case 3: return pt.impedance_mag;
                case 4: return pt.X;
                case 5: return pt.phase_deg;
                default: return 0.0;
            }
        };
        
        // Check each point for intersections
        for (size_t i = 1; i < pts.size(); i++) {
            // Check all pairs of selected curves
            for (size_t c1 = 0; c1 < selectedCurves.size(); c1++) {
                for (size_t c2 = c1 + 1; c2 < selectedCurves.size(); c2++) {
                    int curve1 = selectedCurves[c1];
                    int curve2 = selectedCurves[c2];
                    
                    double val1_prev = getCurveValue(pts[i-1], curve1);
                    double val1_curr = getCurveValue(pts[i], curve1);
                    double val2_prev = getCurveValue(pts[i-1], curve2);
                    double val2_curr = getCurveValue(pts[i], curve2);
                    
                    // Check if curves crossed (sign change in difference)
                    double diff_prev = val1_prev - val2_prev;
                    double diff_curr = val1_curr - val2_curr;
                    
                    if ((diff_prev > 0 && diff_curr < 0) || (diff_prev < 0 && diff_curr > 0) || 
                        (std::abs(diff_curr) < 0.01 && std::abs(val1_curr - val2_curr) < 0.01)) {
                        // Found a crossing!
                        // Check if we already have a cross point at this location
                        bool found = false;
                        for (auto& cp : crossPoints) {
                            if (cp.pointIdx == i) {
                                // Add this curve pair to existing cross point
                                if (std::find(cp.curves.begin(), cp.curves.end(), curve1) == cp.curves.end()) {
                                    cp.curves.push_back(curve1);
                                }
                                if (std::find(cp.curves.begin(), cp.curves.end(), curve2) == cp.curves.end()) {
                                    cp.curves.push_back(curve2);
                                }
                                found = true;
                                break;
                            }
                        }
                        
                        if (!found) {
                            // New cross point
                            CrossPoint cp;
                            cp.pointIdx = i;
                            cp.freq = pts[i].freq;
                            cp.curves.push_back(curve1);
                            cp.curves.push_back(curve2);
                            for (int c : selectedCurves) {
                                cp.values.push_back(getCurveValue(pts[i], c));
                            }
                            crossPoints.push_back(cp);
                        }
                    }
                }
            }
        }
        
        if (crossPoints.empty()) {
            print(translation.get("GOTO_CROSSPOINT_NONE", "No intersections found between selected curves.") + "\n");
            return;
        }
        
        if (crossPoints.size() == 1) {
            // Only one cross point - jump directly
            analyzer.setPosition(crossPoints[0].pointIdx);
            
            print(translation.format("GOTO_CROSSPOINT_JUMPED", "Jumped to cross point at point {0} ({1} Hz)", 
                crossPoints[0].pointIdx, crossPoints[0].freq) + "\n");
            print("Curves: ");
            for (size_t i = 0; i < crossPoints[0].curves.size(); i++) {
                if (i > 0) print(", ");
                print(curveNames[crossPoints[0].curves[i] - 1]);
            }
            print("\n");
            
            if (logger) {
                logger->log("UI", "Go to cross point in acoustic: point " + std::to_string(crossPoints[0].pointIdx));
            }
        } else {
            // Multiple cross points - show menu
            print("\n" + translation.format("GOTO_CROSSPOINT_FOUND", "Found {0} cross points:", crossPoints.size()) + "\n");
            for (size_t i = 0; i < crossPoints.size(); i++) {
                print(std::to_string(i + 1) + ". Point " + std::to_string(crossPoints[i].pointIdx) + " (" + std::to_string(crossPoints[i].freq) + " Hz) - Curves: ");
                for (size_t c = 0; c < crossPoints[i].curves.size(); c++) {
                    if (c > 0) print(", ");
                    print(curveNames[crossPoints[i].curves[c] - 1]);
                }
                print("\n");
            }
            
            print(translation.get("GOTO_CROSSPOINT_CHOOSE", "Enter cross point number to jump to, or ESC to cancel: ") + " ");
            std::string cpInput;
            
            cpInput = "";
            bool inputting = true;
            while (inputting) {
                if (consoleInput->kbhit()) {
                    int ch = consoleInput->getch();
                    if (ch == 27) {  // ESC
                        print("\n[Cancelled]\n");
                        return;
                    } else if (ch == '\r' || ch == '\n') {  // Enter
                        print("\n");
                        inputting = false;
                    } else if (ch >= '0' && ch <= '9') {
                        cpInput += static_cast<char>(ch);
                        print(std::string(1, ch));
                    } else if (ch == 8 && !cpInput.empty()) {  // Backspace
                        cpInput.pop_back();
                        print("\b \b");
                    }
                }
            }
            
            if (cpInput.empty()) return;
            
            try {
                int cpNum = std::stoi(cpInput);
                if (cpNum >= 1 && cpNum <= static_cast<int>(crossPoints.size())) {
                    size_t targetIdx = crossPoints[cpNum - 1].pointIdx;
                    analyzer.setPosition(targetIdx);
                    
                    print(translation.format("GOTO_CROSSPOINT_JUMPED", "Jumped to cross point at point {0} ({1} Hz)", 
                        targetIdx, crossPoints[cpNum - 1].freq) + "\n");
                    
                    if (logger) {
                        logger->log("UI", "Go to cross point in acoustic: point " + std::to_string(targetIdx));
                    }
                } else {
                    print(translation.get("GOTO_CROSSPOINT_INVALID", "Invalid cross point number.") + "\n");
                }
            } catch (...) {
                print(translation.get("GOTO_CROSSPOINT_INVALID", "Invalid cross point number.") + "\n");
            }
        }
        
    } else if (key == 'h') {
        // Help
        print(HelpModule::getGoToMenuAcousticHelp(translation));
    } else if (key == 27) {
        // ESC - back
        return;
    } else {
        print(translation.get("ERROR_UNKNOWN_COMMAND", "Unknown command.") + "\n");
    }
    
    // Restore playback state after jumping
    // The audio context should be preserved as we used setPosition()
    if (wasFrozen) {
        analyzer.freeze();  // Re-freeze at new position
    } else if (wasPaused) {
        analyzer.pause();  // Keep paused
    }
    // If it was playing, it will continue playing at new position automatically
}

// Helper function for Braille curve selection UI
bool ConsoleUI::selectBrailleCurves(bool curveFlags[5]) {
    print(formatHeading("Braille Export - Curve Selection"));
    print(translation.get("BRAILLE_SELECT_CURVES", "Select curves to include in Braille graphics:") + "\n");
    print("  1. SWR (Standing Wave Ratio)\n");
    print("  2. Return Loss\n");
    print("  3. Impedance Magnitude |Z|\n");
    print("  4. Reactance X\n");
    print("  5. Phase\n");
    print(translation.get("BRAILLE_CURVE_PROMPT", "Enter curve numbers separated by spaces (e.g., '1 3 5') or 'a' for all:\n> "));
    
    std::string curveSelection;
    if (!readLine(curveSelection)) return false;
    
    // Initialize all to false
    for (int i = 0; i < 5; i++) {
        curveFlags[i] = false;
    }
    
    if (curveSelection == "a" || curveSelection == "A") {
        // Select all curves
        for (int i = 0; i < 5; i++) {
            curveFlags[i] = true;
        }
    } else {
        // Parse individual curve numbers
        std::istringstream iss(curveSelection);
        int curveNum;
        bool hadInvalid = false;
        while (iss >> curveNum) {
            if (curveNum >= 1 && curveNum <= 5) {
                curveFlags[curveNum - 1] = true;
            } else {
                hadInvalid = true;
            }
        }
        if (hadInvalid) {
            print("Note: Some invalid curve numbers were ignored (valid range: 1-5)\n");
        }
    }
    
    // Check if any curves were selected
    bool anyCurveSelected = false;
    for (int i = 0; i < 5; i++) {
        if (curveFlags[i]) {
            anyCurveSelected = true;
            break;
        }
    }
    
    if (!anyCurveSelected) {
        print("No curves selected. Export canceled.\n");
        return false;
    }
    
    // Show selected curves
    print("Selected curves: ");
    const char* curveNames[] = {"SWR", "Return Loss", "Impedance Mag", "Reactance", "Phase"};
    bool first = true;
    for (int i = 0; i < 5; i++) {
        if (curveFlags[i]) {
            if (!first) print(", ");
            print(curveNames[i]);
            first = false;
        }
    }
    print("\n");
    
    return true;
}

void ConsoleUI::clearScreen(bool preserve) {
    // Do not clear screen if debug flag is set
    // This allows users to copy complete error situations from the screen
    if (cfg.debug) {
        if (logger) logger->log("UI", "Screen clear skipped (debug mode)");
        return;
    }
    
    // When preserve is true, skip clearing to keep existing content visible
    // This is used e.g. at startup to preserve the version info display
    if (preserve) {
        if (logger) logger->log("UI", "Screen clear skipped (preserve mode)");
        return;
    }
    
#if defined(_WIN32)
    // On Windows, use cls command for reliable clearing
    // ANSI codes are enabled in main.cpp but cls is more reliable across Windows versions
    std::system("cls");
    // Also send ANSI clear to web interface if running (works in browsers)
    if (webServer && webServer->isRunning()) {
        webServer->sendOutput("\033[2J\033[H");
    }
#else
    // On Linux and macOS, use ANSI escape codes
    // Route through print() to ensure web interface also receives the clear
    print("\033[2J\033[H");
#endif
    
    if (logger) logger->log("UI", "Screen cleared");
}

std::string ConsoleUI::formatHeading(const std::string& title) const {
    // Use ANSI bold + cyan for headings - visible for sighted users,
    // no decorative characters that create noise for screenreaders
    return "\n\033[1;36m" + title + "\033[0m\n";
}

std::string ConsoleUI::formatSubHeading(const std::string& title) const {
    // Use ANSI bold + white for subheadings
    return "\n\033[1;37m" + title + "\033[0m\n";
}

std::string ConsoleUI::processTextForDisplay(const std::string& text) {
    // Post-process text to replace decorative separators with ANSI colors.
    // This handles translation strings that contain embedded === or ═══ patterns.
    // Processing is line-by-line for correctness.
    std::string result;
    std::istringstream stream(text);
    std::string line;
    bool firstLine = true;
    
    while (std::getline(stream, line)) {
        if (!firstLine) {
            result += '\n';
        }
        firstLine = false;
        
        // Check for ═══ separator lines (box-drawing characters) - remove entirely
        // These lines consist only of ═ characters (possibly with whitespace)
        {
            std::string trimmed = line;
            // Trim whitespace
            size_t start = trimmed.find_first_not_of(" \t");
            if (start != std::string::npos) {
                trimmed = trimmed.substr(start);
            }
            // Check if line consists entirely of ═ (UTF-8: 0xE2 0x95 0x90)
            bool allBoxDraw = !trimmed.empty();
            for (size_t i = 0; i < trimmed.size(); ) {
                if (i + 2 < trimmed.size() &&
                    static_cast<unsigned char>(trimmed[i]) == 0xE2 &&
                    static_cast<unsigned char>(trimmed[i+1]) == 0x95 &&
                    static_cast<unsigned char>(trimmed[i+2]) == 0x90) {
                    i += 3;  // Skip ═ (3-byte UTF-8)
                } else {
                    allBoxDraw = false;
                    break;
                }
            }
            if (allBoxDraw) {
                // Skip this line entirely (replace with empty line)
                continue;
            }
        }
        
        // Check for === Title === pattern (also handles ==== variants)
        {
            std::string trimmed = line;
            size_t start = trimmed.find_first_not_of(" \t\n\r");
            if (start != std::string::npos) {
                trimmed = trimmed.substr(start);
            }
            // Skip leading = characters to find the title
            size_t eqStart = 0;
            while (eqStart < trimmed.size() && trimmed[eqStart] == '=') eqStart++;
            // Need at least 3 leading = and a space after them
            if (eqStart >= 3 && eqStart < trimmed.size() && trimmed[eqStart] == ' ') {
                // Find the closing === (at least 3 = at end, preceded by space)
                size_t endPos = trimmed.rfind(" ===");
                if (endPos != std::string::npos && endPos > eqStart) {
                    std::string title = trimmed.substr(eqStart + 1, endPos - eqStart - 1);
                    // Remove any trailing = characters from title
                    while (!title.empty() && title.back() == '=') {
                        title.pop_back();
                    }
                    while (!title.empty() && title.back() == ' ') {
                        title.pop_back();
                    }
                    if (!title.empty()) {
                        result += "\033[1;36m" + title + "\033[0m";
                        continue;
                    }
                }
            }
        }
        
        // Check for --- Title --- pattern  
        {
            std::string trimmed = line;
            size_t start = trimmed.find_first_not_of(" \t\n\r");
            if (start != std::string::npos) {
                trimmed = trimmed.substr(start);
            }
            if (trimmed.size() > 8 && trimmed.substr(0, 4) == "--- ") {
                size_t endPos = trimmed.rfind(" ---");
                if (endPos != std::string::npos && endPos > 4) {
                    std::string title = trimmed.substr(4, endPos - 4);
                    result += "\033[1;37m" + title + "\033[0m";
                    continue;
                }
            }
        }
        
        // No pattern matched - keep line as-is
        result += line;
    }
    
    // If original text ended with newline, preserve it
    if (!text.empty() && text.back() == '\n') {
        result += '\n';
    }
    
    return result;
}

void ConsoleUI::setUIContext(const std::string& context, const std::vector<UIAction>& actions, const std::string& inputMode) {
    currentContext = context;
    currentActions = actions;
    currentInputMode = inputMode;
    
    // Send context update to web interface if running
    if (webServer && webServer->isRunning()) {
        webServer->sendContext(getContextJSON());
    }
}

std::string ConsoleUI::getContextJSON() const {
    // Helper lambda to escape strings for JSON output
    auto jsonEscape = [](const std::string& s) -> std::string {
        std::string result;
        result.reserve(s.size() + 10);
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        // Control character - skip
                    } else {
                        result += c;
                    }
                    break;
            }
        }
        return result;
    };
    
    std::string json = "{\"context\":\"" + jsonEscape(currentContext) + "\",\"inputMode\":\"" + jsonEscape(currentInputMode.empty() ? "menu" : currentInputMode) + "\",\"actions\":[";
    for (size_t i = 0; i < currentActions.size(); ++i) {
        if (i > 0) json += ",";
        json += "{\"key\":\"" + jsonEscape(currentActions[i].key) + "\",";
        json += "\"label\":\"" + jsonEscape(currentActions[i].label) + "\",";
        json += "\"needsEnter\":" + std::string(currentActions[i].needsEnter ? "true" : "false") + "}";
    }
    json += "]}";
    return json;
}

void ConsoleUI::print(const std::string& text) {
    // Post-process text to replace decorative separators with ANSI colors
    // This transparently handles translation strings containing === or ═══ patterns
    std::string processed = processTextForDisplay(text);
    
    // Output to console
    std::cout << processed;
    std::cout.flush();
    
    // Also send to web interface if running
    if (webServer && webServer->isRunning()) {
        webServer->sendOutput(processed);
    }
}

void ConsoleUI::output(const std::string& text) {
    print(text);
}

bool ConsoleUI::readLine(std::string& result) {
    // Phase 4: Replaced canonical mode implementation with raw mode
    // Now uses readRawLineInput() internally for consistent Escape support
    auto inputResult = readRawLineInput("");
    
    if (inputResult.cancelled) {
        return false;  // User pressed Escape
    }
    
    result = inputResult.value;
    return true;
}

void ConsoleUI::documentationMenu() {
    clearScreen();
    print(formatHeading(translation.get("DOCS_MENU_TITLE", "Manuals and Training")));
    print(translation.get("DOCS_MENU_MANUAL", "1. Open User Manual") + "\n");
    print(translation.get("DOCS_MENU_TRAINING", "2. Open Training Suite") + "\n");
    print(translation.get("DOCS_MENU_BETA", "3. Open Beta Test Instructions") + "\n");
    print(translation.get("DOCS_MENU_FEEDBACK", "4. Feedback to Developer") + "\n");
    print(translation.get("DOCS_MENU_BACK", "ESC - Back to Main Menu") + "\n\n");
    
    print(translation.get("DOCS_MENU_PROMPT", "Select an option (1-4, or ESC to go back):") + " > ");
    
    while (true) {
        if (consoleInput->kbhit()) {
            char key = static_cast<char>(consoleInput->getch());
            if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
            
            if (key == 27) {  // ESC
                print("\n");
                break;
            } else if (key == '1') {
                print("1\n");
                std::string docPath = translation.get("DOC_PATH_MANUAL", "doc/manuals/USER_MANUAL_EN.html");
                openDocumentation(docPath);
                break;
            } else if (key == '2') {
                print("2\n");
                std::string docPath = translation.get("DOC_PATH_TRAINING", "doc/training/en/Training_Index.html");
                openDocumentation(docPath);
                break;
            } else if (key == '3') {
                print("3\n");
                std::string docPath = translation.get("DOC_PATH_BETA", "doc/beta-testing/BETA_TESTING_EN.html");
                openDocumentation(docPath);
                break;
            } else if (key == '4') {
                print("4\n");
                feedbackToDeveloper();
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    clearScreen();
}

void ConsoleUI::openDocumentation(const std::string& docPath) {
    print(translation.get("DOC_OPENED", "Opening documentation in browser...") + "\n");
    
    // Check if file exists
    if (!std::filesystem::exists(docPath)) {
        print(translation.format("ERROR_DOC_NOT_FOUND", "Error: Documentation file not found: {0}", docPath) + "\n");
        if (logger) logger->log("UI", "Documentation file not found: " + docPath);
        return;
    }
    
    // Get absolute path
    std::filesystem::path absPath = std::filesystem::absolute(docPath);
    std::string pathStr = absPath.string();
    
#if defined(_WIN32)
    // On Windows, use ShellExecute to open in default browser
    HINSTANCE result = ShellExecuteA(NULL, "open", pathStr.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        print(translation.get("ERROR_BROWSER_FAILED", "Error: Could not open browser.") + "\n");
        if (logger) logger->log("UI", "Failed to open browser for: " + pathStr);
    } else {
        if (logger) logger->log("UI", "Opened documentation: " + pathStr);
    }
#else
    // On Linux/Mac, use xdg-open or open
    std::string cmd = "xdg-open \"" + pathStr + "\" 2>/dev/null || open \"" + pathStr + "\"";
    int result = system(cmd.c_str());
    if (result != 0) {
        print(translation.get("ERROR_BROWSER_FAILED", "Error: Could not open browser.") + "\n");
        if (logger) logger->log("UI", "Failed to open browser for: " + pathStr);
    } else {
        if (logger) logger->log("UI", "Opened documentation: " + pathStr);
    }
#endif
}

void ConsoleUI::feedbackToDeveloper() {
    print(formatHeading(translation.get("FEEDBACK_MENU_TITLE", "Feedback to Developer")));
    print(translation.get("FEEDBACK_ATTACH_QUESTION", "Would you like to attach logs and configuration files? (y/n):") + " ");
    
    bool attachFiles = false;
    
    while (true) {
        if (consoleInput->kbhit()) {
            char key = static_cast<char>(consoleInput->getch());
            if (key >= 'A' && key <= 'Z') key = key - 'A' + 'a';
            
            std::string yesKey = translation.get("YES_KEY", "y");
            std::string noKey = translation.get("NO_KEY", "n");
            
            if (key == yesKey[0] || key == 'y') {
                print(std::string(1, key) + "\n");
                attachFiles = true;
                break;
            } else if (key == noKey[0] || key == 'n') {
                print(std::string(1, key) + "\n");
                attachFiles = false;
                break;
            } else if (key == 27) {  // ESC
                print("\n");
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Show warning message about manual attachment BEFORE opening email
    if (attachFiles) {
        print("\n" + translation.get("FEEDBACK_ATTACHMENTS_WARNING", 
            "IMPORTANT: Email clients cannot attach files automatically.\nYou will need to manually attach the following folders after the email opens:") + "\n");
        print("  - config/*\n");
        print("  - logs/*\n");
        print("  - Export/* (if relevant)\n\n");
        print(translation.get("MSG_PRESS_ESC_BACK", "Press ESC to cancel, or any other key to continue...") + " ");
        
        while (true) {
            if (consoleInput->kbhit()) {
                char key = static_cast<char>(consoleInput->getch());
                if (key == 27) {  // ESC
                    print("\n");
                    return;
                }
                print("\n");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    print(translation.get("FEEDBACK_PREPARING", "Preparing email...") + "\n");
    
    // Prepare email
    std::string emailAddress = "do9re@hotmail.com";
    std::string subject = "NanoVNA CLI Accessible - Feedback";
    std::string body = "Hello,%0D%0A%0D%0APlease describe your feedback here:%0D%0A%0D%0A";
    
    if (attachFiles) {
        body += "%0D%0A--- Please attach the following files ----%0D%0A";
        body += "- config/*%0D%0A";
        body += "- logs/*%0D%0A";
        body += "- Export/* (if relevant)%0D%0A%0D%0A";
    }
    
    std::string mailtoUrl = "mailto:" + emailAddress + "?subject=" + subject + "&body=" + body;
    
#if defined(_WIN32)
    HINSTANCE result = ShellExecuteA(NULL, "open", mailtoUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        print(translation.get("FEEDBACK_ERROR", "Error opening email client.") + "\n");
        if (logger) logger->log("UI", "Failed to open email client");
    } else {
        print(translation.get("FEEDBACK_EMAIL_PREPARED", "Email client has been opened. Please add your message and send the email.") + "\n");
        if (attachFiles) {
            print(translation.get("FEEDBACK_ATTACHMENTS_INFO", 
                "The following files should be attached:\n  - config/*\n  - logs/*\n  - Export/*\n\nPlease manually attach these folders to your email.") + "\n");
        }
        if (logger) logger->log("UI", "Opened email client for feedback");
    }
#else
    std::string cmd = "xdg-open \"" + mailtoUrl + "\" 2>/dev/null || open \"" + mailtoUrl + "\"";
    int result = system(cmd.c_str());
    if (result != 0) {
        print(translation.get("FEEDBACK_ERROR", "Error opening email client.") + "\n");
        if (logger) logger->log("UI", "Failed to open email client");
    } else {
        print(translation.get("FEEDBACK_EMAIL_PREPARED", "Email client has been opened. Please add your message and send the email.") + "\n");
        if (attachFiles) {
            print(translation.get("FEEDBACK_ATTACHMENTS_INFO", 
                "The following files should be attached:\n  - config/*\n  - logs/*\n  - Export/*\n\nPlease manually attach these folders to your email.") + "\n");
        }
        if (logger) logger->log("UI", "Opened email client for feedback");
    }
#endif
}
