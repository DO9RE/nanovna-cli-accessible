#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "measurement.h"
#include "logger.h"
#include "config.h"

#if defined(_WIN32)
#include <windows.h>
#endif

// Structure to hold printer information
struct PrinterInfo {
    std::string name;
    std::string port;
    std::string driver;
    bool isDefault;
};

// Paper size dimensions in mm (printable area, excluding margins)
struct PaperDimensions {
    double width_mm;   // Printable width in mm
    double height_mm;  // Printable height in mm
};

class BraillePrinter {
public:
    BraillePrinter(Logger* logger = nullptr);
    ~BraillePrinter();
    
    // Enumerate available Windows printers
    // Returns list of printer names and their details
    bool enumeratePrinters(std::vector<PrinterInfo>& printers, std::string& err);
    
    // Print Braille graphics directly to selected printer
    // Uses full AppConfig for all braille settings (protocol, paper, orientation, grid, etc.)
    // curveFlags: array of 5 bools for curves (0:SWR, 1:RL, 2:|Z|, 3:X, 4:Phase)
    bool printBraille(const std::string& printerName,
                     const std::vector<MeasurementPoint>& pts,
                     uint64_t startFreq, uint64_t endFreq, uint64_t step,
                     const bool curveFlags[5],
                     const AppConfig& config,
                     std::string& err);
    
    // Generate Braille print data with full config
    // Returns the raw data that would be sent to the printer
    bool generateBrailleData(const std::vector<MeasurementPoint>& pts,
                           uint64_t startFreq, uint64_t endFreq, uint64_t step,
                           const bool curveFlags[5],
                           const AppConfig& config,
                           std::vector<char>& data,
                           std::string& err);
    
    /**
     * Generate Braille data using pre-selected audio indices (Pflichtenheft §9).
     * @param pts Full measurement data
     * @param audioIndices Pre-selected point indices from Audio-LTTB
     * @param startFreq Start frequency
     * @param endFreq End frequency
     * @param step Frequency step
     * @param curveFlags Array of 5 bools for curves (0:SWR, 1:RL, 2:|Z|, 3:X, 4:Phase)
     * @param config Application configuration
     * @param data Output data buffer
     * @param err Error string
     * @return true on success, false on failure
     */
    bool generateBrailleData(const std::vector<MeasurementPoint>& pts,
                           const std::vector<size_t>& audioIndices,
                           uint64_t startFreq, uint64_t endFreq, uint64_t step,
                           const bool curveFlags[5],
                           const AppConfig& config,
                           std::vector<char>& data,
                           std::string& err);
    
    // Calculate estimated page count for current settings
    int calculatePageCount(const std::vector<MeasurementPoint>& pts,
                          const bool curveFlags[5],
                          const AppConfig& config);
    
    // Get paper dimensions based on size and orientation
    static PaperDimensions getPaperDimensions(AppConfig::BraillePaperSize paperSize, 
                                             AppConfig::BrailleOrientation orientation);
    
private:
    Logger* logger;
    
    // Helper function to send raw data to Windows printer
    bool sendToPrinter(const std::string& printerName, 
                      const std::vector<char>& data,
                      std::string& err);
    
    // Generate data using Index V5 protocol (floating dot area)
    bool generateV5Data(const std::vector<MeasurementPoint>& pts,
                       uint64_t startFreq, uint64_t endFreq, uint64_t step,
                       const bool curveFlags[5],
                       const PaperDimensions& paper,
                       const AppConfig& config,
                       std::vector<char>& data,
                       std::string& err);
    
    // Generate data using Index V4 protocol (raster graphics)
    bool generateV4Data(const std::vector<MeasurementPoint>& pts,
                       uint64_t startFreq, uint64_t endFreq, uint64_t step,
                       const bool curveFlags[5],
                       const PaperDimensions& paper,
                       const AppConfig& config,
                       std::vector<char>& data,
                       std::string& err);
    
    // Helper to parse curve pattern string (e.g., "2-0-0" -> vector of counts)
    std::vector<int> parsePatternString(const std::string& pattern);
    
    // LTTB downsampling - preserves visual shape of curves better than simple decimation
    // Returns indices of points to keep from the original data
    std::vector<size_t> selectPointsUsingLTTB(
        const std::vector<std::pair<double, double>>& coords,
        int threshold);
};
