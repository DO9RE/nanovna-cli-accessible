#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "measurement.h"
#include "config.h"

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