#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "measurement.h"
#include "config.h"

// Task 1.14: Centralized CSV schema definition
// Single source of truth for CSV field order - ensures export/import compatibility
namespace CSVSchema {
    // CSV field names in their canonical order
    // IMPORTANT: Changes to this order must be reflected in both export and import logic
    const std::vector<std::string> FIELD_NAMES = {
        "freq_hz",
        "s11_re",
        "s11_im",
        "swr",
        "return_loss_db",
        "r_ohm",
        "x_ohm",
        "s21_re",
        "s21_im"
    };
    
    // Field indices for programmatic access
    enum FieldIndex {
        FREQ_HZ = 0,
        S11_RE = 1,
        S11_IM = 2,
        SWR = 3,
        RETURN_LOSS_DB = 4,
        R_OHM = 5,
        X_OHM = 6,
        S21_RE = 7,
        S21_IM = 8
    };
}

class ExportModule {
public:
    // Export to CSV with filename generation including timestamp and parameters
    static bool exportCSV(const std::vector<MeasurementPoint>& pts, 
                         uint64_t startFreq, uint64_t endFreq, uint64_t step,
                         std::string& generatedFilename, std::string& err);
    
    // Export to TXT with filename generation including timestamp and parameters
    static bool exportTXT(const std::vector<MeasurementPoint>& pts,
                         uint64_t startFreq, uint64_t endFreq, uint64_t step,
                         std::string& generatedFilename, std::string& err);
    
    // Export to Braille (.brl) with curve selection and filename generation
    // Uses full AppConfig for all braille settings (protocol, paper, orientation, grid, etc.)
    // curveFlags: array of 5 bools for curves (0:SWR, 1:RL, 2:|Z|, 3:X, 4:Phase)
    static bool exportBraille(const std::vector<MeasurementPoint>& pts,
                             uint64_t startFreq, uint64_t endFreq, uint64_t step,
                             const bool curveFlags[5],
                             const AppConfig& config,
                             std::string& generatedFilename, std::string& err);
    
    // Legacy functions for backward compatibility (deprecated)
    static bool exportCSV(const std::string& filename, const std::vector<MeasurementPoint>& pts, std::string& err);
    static bool exportTXT(const std::string& filename, const std::vector<MeasurementPoint>& pts, std::string& err);
};