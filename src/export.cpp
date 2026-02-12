#include "export.h"
#include "braille_printer.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cmath>
#include <algorithm>

// Task 1.14: Helper to generate CSV header from centralized schema
static std::string generateCSVHeader() {
    std::string header;
    for (size_t i = 0; i < CSVSchema::FIELD_NAMES.size(); ++i) {
        if (i > 0) header += ",";
        header += CSVSchema::FIELD_NAMES[i];
    }
    return header;
}

// Helper function to generate filename with timestamp and parameters
static std::string generateFilename(const std::string& extension,
                                   uint64_t startFreq, uint64_t endFreq, uint64_t step) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    
    std::ostringstream filename;
    filename << "Export/nanovna_"
             << std::put_time(&tm, "%Y%m%d_%H%M%S")
             << "_" << startFreq
             << "_" << endFreq
             << "_" << step
             << extension;
    
    return filename.str();
}

bool ExportModule::exportCSV(const std::vector<MeasurementPoint>& pts,
                            uint64_t startFreq, uint64_t endFreq, uint64_t step,
                            std::string& generatedFilename, std::string& err) {
    if (pts.empty()) {
        err = "No data to export";
        return false;
    }
    
    generatedFilename = generateFilename(".csv", startFreq, endFreq, step);
    
    std::ofstream ofs(generatedFilename);
    if (!ofs) { 
        err = "Cannot open file: " + generatedFilename; 
        return false; 
    }
    
    // Task 1.14: Use centralized schema for header
    ofs << generateCSVHeader() << "\n";
    
    for (auto &p : pts) {
        ofs << p.freq << ","
            << std::fixed << std::setprecision(9) << p.s11_re << ","
            << p.s11_im << ","
            << p.swr << ","
            << p.rl << ","
            << p.R << ","
            << p.X << ",";
        if (p.hasS21) ofs << p.s21_re << "," << p.s21_im;
        else ofs << ","; // keep columns
        ofs << "\n";
    }
    
    return true;
}

bool ExportModule::exportTXT(const std::vector<MeasurementPoint>& pts,
                            uint64_t startFreq, uint64_t endFreq, uint64_t step,
                            std::string& generatedFilename, std::string& err) {
    if (pts.empty()) {
        err = "No data to export";
        return false;
    }
    
    generatedFilename = generateFilename(".txt", startFreq, endFreq, step);
    
    std::ofstream ofs(generatedFilename);
    if (!ofs) { 
        err = "Cannot open file: " + generatedFilename; 
        return false; 
    }
    
    ofs << "Measurement dump\n";
    ofs << "Start: " << startFreq << " Hz, End: " << endFreq << " Hz, Step: " << step << " Hz\n";
    ofs << "Points: " << pts.size() << "\n\n";
    
    for (auto &p : pts) {
        ofs << "Freq: " << p.freq << " Hz  SWR: " << p.swr << "  RL: " << p.rl << "  R: " << p.R << "  X: " << p.X << "\n";
    }
    
    return true;
}

// Legacy functions for backward compatibility
bool ExportModule::exportCSV(const std::string& filename, const std::vector<MeasurementPoint>& pts, std::string& err) {
    std::ofstream ofs(filename);
    if (!ofs) { err = "Cannot open file"; return false; }
    // Task 1.14: Use centralized schema for header
    ofs << generateCSVHeader() << "\n";
    for (auto &p : pts) {
        ofs << p.freq << ","
            << std::fixed << std::setprecision(9) << p.s11_re << ","
            << p.s11_im << ","
            << p.swr << ","
            << p.rl << ","
            << p.R << ","
            << p.X << ",";
        if (p.hasS21) ofs << p.s21_re << "," << p.s21_im;
        else ofs << ","; // keep columns
        ofs << "\n";
    }
    return true;
}

bool ExportModule::exportTXT(const std::string& filename, const std::vector<MeasurementPoint>& pts, std::string& err) {
    std::ofstream ofs(filename);
    if (!ofs) { err = "Cannot open file"; return false; }
    ofs << "Measurement dump\n";
    for (auto &p : pts) {
        ofs << "Freq: " << p.freq << " Hz  SWR: " << p.swr << "  RL: " << p.rl << "  R: " << p.R << "  X: " << p.X << "\n";
    }
    return true;
}

bool ExportModule::exportBraille(const std::vector<MeasurementPoint>& pts,
                                 uint64_t startFreq, uint64_t endFreq, uint64_t step,
                                 const bool curveFlags[5],
                                 const AppConfig& config,
                                 std::string& generatedFilename, std::string& err) {
    if (pts.empty()) {
        err = "No data to export";
        return false;
    }
    
    // Check if at least one curve is selected
    bool anyCurveSelected = false;
    for (int i = 0; i < 5; i++) {
        if (curveFlags[i]) {
            anyCurveSelected = true;
            break;
        }
    }
    
    if (!anyCurveSelected) {
        err = "No curves selected for export";
        return false;
    }
    
    // Generate filename
    generatedFilename = generateFilename(".brl", startFreq, endFreq, step);
    
    // Use BraillePrinter to generate data with full config
    BraillePrinter printer(nullptr);  // No logger for export
    std::vector<char> brailleData;
    if (!printer.generateBrailleData(pts, startFreq, endFreq, step, curveFlags, 
                                    config, brailleData, err)) {
        return false;
    }
    
    // Write to file
    std::ofstream ofs(generatedFilename, std::ios::binary);
    if (!ofs) { 
        err = "Cannot open file: " + generatedFilename; 
        return false; 
    }
    
    ofs.write(brailleData.data(), brailleData.size());
    
    if (!ofs.good()) {
        err = "Failed to write braille data to file";
        return false;
    }
    
    return true;
}
