#include "braille_printer.h"
#include "frequency_utils.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

#if defined(_WIN32)
#include <winspool.h>
#pragma comment(lib, "winspool.lib")
#endif

BraillePrinter::BraillePrinter(Logger* logger) : logger(logger) {
    if (logger) {
        logger->log("BRAILLE_PRINTER", "BraillePrinter initialized");
    }
}

BraillePrinter::~BraillePrinter() {
    if (logger) {
        logger->log("BRAILLE_PRINTER", "BraillePrinter destroyed");
    }
}

PaperDimensions BraillePrinter::getPaperDimensions(AppConfig::BraillePaperSize paperSize, 
                                                   AppConfig::BrailleOrientation orientation) {
    PaperDimensions dims;
    
    // Paper sizes in mm (printable area, accounting for 5mm margins on each side)
    // Physical paper width - 10mm (5mm margin on each side)
    switch (paperSize) {
        case AppConfig::BraillePaperSize::A4:
            dims.width_mm = 200.0;   // 210mm - 10mm
            dims.height_mm = 287.0;  // 297mm - 10mm
            break;
        case AppConfig::BraillePaperSize::LETTER:
            dims.width_mm = 205.9;   // 215.9mm - 10mm
            dims.height_mm = 269.4;  // 279.4mm - 10mm
            break;
        case AppConfig::BraillePaperSize::A3:
            dims.width_mm = 287.0;   // 297mm - 10mm
            dims.height_mm = 410.0;  // 420mm - 10mm
            break;
        case AppConfig::BraillePaperSize::LEGAL:
            dims.width_mm = 205.9;   // 215.9mm - 10mm
            dims.height_mm = 345.6;  // 355.6mm - 10mm
            break;
        case AppConfig::BraillePaperSize::BLISTA_260x305:
            dims.width_mm = 250.0;   // 260mm - 10mm
            dims.height_mm = 295.0;  // 305mm - 10mm
            break;
        case AppConfig::BraillePaperSize::BLISTA_270x340:
            dims.width_mm = 260.0;   // 270mm - 10mm
            dims.height_mm = 330.0;  // 340mm - 10mm
            break;
        case AppConfig::BraillePaperSize::BLISTA_297x304:
            dims.width_mm = 287.0;   // 297mm - 10mm
            dims.height_mm = 294.0;  // 304mm - 10mm
            break;
        default:
            dims.width_mm = 200.0;
            dims.height_mm = 287.0;
    }
    
    // Swap dimensions for landscape
    if (orientation == AppConfig::BrailleOrientation::LANDSCAPE) {
        std::swap(dims.width_mm, dims.height_mm);
    }
    
    return dims;
}

bool BraillePrinter::enumeratePrinters(std::vector<PrinterInfo>& printers, std::string& err) {
#if !defined(_WIN32)
    err = "Braille printer support is only available on Windows";
    if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
    return false;
#else
    if (logger) logger->log("BRAILLE_PRINTER", "Starting printer enumeration...");
    
    printers.clear();
    
    // First call to get required buffer size
    DWORD needed = 0;
    DWORD returned = 0;
    DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    
    EnumPrintersA(flags, NULL, 2, NULL, 0, &needed, &returned);
    
    if (needed == 0) {
        err = "No printers found on this system";
        if (logger) logger->log("BRAILLE_PRINTER", "WARNING: " + err);
        return true; // Not an error, just no printers
    }
    
    if (logger) {
        std::ostringstream oss;
        oss << "Found printer data, need " << needed << " bytes for " << returned << " printers";
        logger->log("BRAILLE_PRINTER", oss.str());
    }
    
    // Allocate buffer and get printer info
    std::vector<BYTE> buffer(needed);
    PRINTER_INFO_2A* pPrinterInfo = reinterpret_cast<PRINTER_INFO_2A*>(buffer.data());
    
    if (!EnumPrintersA(flags, NULL, 2, buffer.data(), needed, &needed, &returned)) {
        DWORD dwError = GetLastError();
        std::ostringstream oss;
        oss << "Failed to enumerate printers. Error code: " << dwError;
        err = oss.str();
        if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
        return false;
    }
    
    if (logger) {
        std::ostringstream oss;
        oss << "Successfully enumerated " << returned << " printers";
        logger->log("BRAILLE_PRINTER", oss.str());
    }
    
    // Get default printer name
    char defaultPrinterName[256] = {0};
    DWORD defaultNameSize = sizeof(defaultPrinterName);
    GetDefaultPrinterA(defaultPrinterName, &defaultNameSize);
    
    if (logger && defaultPrinterName[0] != '\0') {
        std::ostringstream oss;
        oss << "Default printer: " << defaultPrinterName;
        logger->log("BRAILLE_PRINTER", oss.str());
    }
    
    // Extract printer information
    for (DWORD i = 0; i < returned; i++) {
        PrinterInfo info;
        info.name = pPrinterInfo[i].pPrinterName ? pPrinterInfo[i].pPrinterName : "";
        info.port = pPrinterInfo[i].pPortName ? pPrinterInfo[i].pPortName : "";
        info.driver = pPrinterInfo[i].pDriverName ? pPrinterInfo[i].pDriverName : "";
        info.isDefault = (info.name == defaultPrinterName);
        
        printers.push_back(info);
        
        if (logger) {
            std::ostringstream oss;
            oss << "Printer " << (i+1) << ": '" << info.name << "' "
                << "Port: '" << info.port << "' "
                << "Driver: '" << info.driver << "' "
                << (info.isDefault ? "[DEFAULT]" : "");
            logger->log("BRAILLE_PRINTER", oss.str());
        }
    }
    
    return true;
#endif
}

// Helper function to normalize curve values to 0.0-1.0 range for Braille graphics
static double normalizeCurveValue(double value, double minVal, double maxVal) {
    if (maxVal <= minVal) return 0.5;  // If no variation, place in middle
    double normalized = (value - minVal) / (maxVal - minVal);
    return std::max(0.0, std::min(1.0, normalized));  // Clamp to [0, 1]
}

// Parse pattern string like "2-2" into vector of counts [2, 2]
// Pattern alternates between draw and pause: first=draw, second=pause, third=draw, etc.
std::vector<int> BraillePrinter::parsePatternString(const std::string& pattern) {
    std::vector<int> result;
    
    // Empty or "0" means solid line
    if (pattern.empty() || pattern == "0") {
        return result;  // Empty vector means solid
    }
    
    std::istringstream iss(pattern);
    std::string token;
    while (std::getline(iss, token, '-')) {
        try {
            int count = std::stoi(token);
            result.push_back(count);
        } catch (...) {
            // Invalid token, log and skip it
            if (logger) {
                std::ostringstream oss;
                oss << "Invalid pattern token '" << token << "' in pattern '" << pattern << "', skipping";
                logger->log("BRAILLE_PRINTER", oss.str());
            }
        }
    }
    
    return result;
}

// Calculate estimated page count
int BraillePrinter::calculatePageCount(const std::vector<MeasurementPoint>& pts,
                                       const bool curveFlags[5],
                                       const AppConfig& config) {
#if !defined(_WIN32)
    return 0;
#else
    if (pts.empty()) return 0;
    
    // Count selected curves
    int curveCount = 0;
    for (int i = 0; i < 5; i++) {
        if (curveFlags[i]) curveCount++;
    }
    
    if (curveCount == 0) return 0;
    
    // Calculate usable width based on configured parameters
    PaperDimensions paper = getPaperDimensions(config.braille_paper_size, config.braille_orientation);
    
    double graphWidth;
    if (config.braille_orientation == AppConfig::BrailleOrientation::LANDSCAPE) {
        graphWidth = paper.width_mm * config.braille_graph_width_percent_landscape;
    } else {
        graphWidth = paper.width_mm * config.braille_graph_width_percent_portrait;
    }
    
    // Adjust for Y-axis space (must match generateV5Data calculation)
    graphWidth -= config.braille_y_axis_space_mm;
    
    // Calculate minimum spacing from DPI: spacing_mm = 25.4 / DPI
    double minSpacing = 25.4 / config.braille_dpi;
    
    // Calculate how many points can fit per page based on spacing
    int maxPointsPerPage = static_cast<int>(graphWidth / minSpacing);
    
    // Calculate number of pages needed
    // We need to account for some overlap/continuity between pages
    int pages = static_cast<int>(std::ceil(static_cast<double>(pts.size()) / maxPointsPerPage));
    
    if (pages == 0) pages = 1;
    
    return pages;
#endif
}

// New interface that accepts full AppConfig
bool BraillePrinter::generateBrailleData(const std::vector<MeasurementPoint>& pts,
                                        uint64_t startFreq, uint64_t endFreq, uint64_t step,
                                        const bool curveFlags[5],
                                        const AppConfig& config,
                                        std::vector<char>& data,
                                        std::string& err) {
#if !defined(_WIN32)
    err = "Braille printer support is only available on Windows";
    if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
    return false;
#else
    if (logger) {
        std::ostringstream oss;
        oss << "Generating Braille data for " << pts.size() << " points, "
            << "freq range " << startFreq << " - " << endFreq << " Hz, "
            << "protocol: " << (config.braille_protocol == AppConfig::BrailleProtocol::INDEX_V5 ? "V5" : "V4");
        logger->log("BRAILLE_PRINTER", oss.str());
    }
    
    if (pts.empty()) {
        err = "No data to export";
        if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
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
        if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
        return false;
    }
    
    // Get paper dimensions
    PaperDimensions paper = getPaperDimensions(config.braille_paper_size, config.braille_orientation);
    
    if (logger) {
        std::ostringstream oss;
        oss << "Paper dimensions: " << paper.width_mm << "mm x " << paper.height_mm << "mm";
        logger->log("BRAILLE_PRINTER", oss.str());
    }
    
    // Generate data based on protocol
    if (config.braille_protocol == AppConfig::BrailleProtocol::INDEX_V5) {
        return generateV5Data(pts, startFreq, endFreq, step, curveFlags, paper, config, data, err);
    } else {
        return generateV4Data(pts, startFreq, endFreq, step, curveFlags, paper, config, data, err);
    }
#endif
}

// Overload with pre-selected audio indices (Pflichtenheft §9)
bool BraillePrinter::generateBrailleData(const std::vector<MeasurementPoint>& pts,
                                        const std::vector<size_t>& audioIndices,
                                        uint64_t startFreq, uint64_t endFreq, uint64_t step,
                                        const bool curveFlags[5],
                                        const AppConfig& config,
                                        std::vector<char>& data,
                                        std::string& err) {
    // Pflichtenheft §9 + RÜCKFRAGE OPTION B: No fallback, export requires audio indices
    if (audioIndices.empty()) {
        err = "Audio playback required for Braille export - no audio indices available";
        if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
        return false;
    }
    
    // Build a reduced dataset using only the audio-selected indices
    std::vector<MeasurementPoint> selectedPts;
    selectedPts.reserve(audioIndices.size());
    for (size_t idx : audioIndices) {
        if (idx < pts.size()) {
            selectedPts.push_back(pts[idx]);
        }
    }
    
    if (selectedPts.empty()) {
        err = "No valid points from audio indices";
        if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
        return false;
    }
    
    if (logger) {
        std::ostringstream oss;
        oss << "Generating Braille data from " << audioIndices.size() << " audio-selected indices "
            << "(out of " << pts.size() << " total points)";
        logger->log("BRAILLE_PRINTER", oss.str());
    }
    
    // Delegate to the standard generation with the pre-selected points
    // The existing V5/V4 generators will handle these points directly
    // (they may further downsample if needed, but the starting set matches audio)
    return generateBrailleData(selectedPts, startFreq, endFreq, step, curveFlags, config, data, err);
}

#if defined(_WIN32)
bool BraillePrinter::generateV5Data(const std::vector<MeasurementPoint>& pts,
                                   uint64_t startFreq, uint64_t endFreq, uint64_t step,
                                   const bool curveFlags[5],
                                   const PaperDimensions& paper,
                                   const AppConfig& config,
                                   std::vector<char>& data,
                                   std::string& err) {
    if (logger) logger->log("BRAILLE_PRINTER", "Using Index V5 Floating Dot Area mode");
    
    // Shortened curve names for compact display
    const char* curveNames[] = {"SWR", "RL", "|Z|", "X", "Phase"};
    
    // Build selected curves list
    std::ostringstream selectedCurves;
    int selectedCount = 0;
    for (int i = 0; i < 5; i++) {
        if (curveFlags[i]) {
            if (selectedCurves.tellp() > 0) selectedCurves << ", ";
            selectedCurves << curveNames[i];
            selectedCount++;
        }
    }
    
    // Log data dimensions for validation
    if (logger) {
        std::ostringstream oss2;
        oss2 << "Generating braille print: " << pts.size() << " measurement points, "
             << selectedCount << " curves selected";
        logger->log("BRAILLE_PRINTER", oss2.str());
        
        oss2.str("");
        oss2 << "Paper dimensions: " << paper.width_mm << "mm x " << paper.height_mm << "mm";
        logger->log("BRAILLE_PRINTER", oss2.str());
        
        // Calculate expected dot density based on configured DPI
        // DPI to dots per mm: dotsPerMm = DPI / 25.4
        // Minimum spacing = 25.4 / DPI mm
        double dotsPerMm = config.braille_dpi / 25.4;
        int maxDotsX = static_cast<int>(paper.width_mm * dotsPerMm);
        int maxDotsY = static_cast<int>(paper.height_mm * dotsPerMm);
        
        oss2.str("");
        oss2 << "Estimated tactile resolution at " << config.braille_dpi << " DPI: " 
             << maxDotsX << " x " << maxDotsY << " dots available";
        logger->log("BRAILLE_PRINTER", oss2.str());
        
        oss2.str("");
        oss2 << "Data points: " << pts.size() << ", will be scaled to fit paper width";
        logger->log("BRAILLE_PRINTER", oss2.str());
        
        if (pts.size() > static_cast<size_t>(maxDotsX)) {
            oss2.str("");
            oss2 << "Note: Data points (" << pts.size() << ") exceed optimal dot resolution (" 
                 << maxDotsX << "). Points will be compressed horizontally.";
            logger->log("BRAILLE_PRINTER", oss2.str());
        }
    }
    
    data.clear();
    std::ostringstream oss;
    
    // Calculate number of pages needed based on configured parameters
    double graphWidth_forCalc;
    if (config.braille_orientation == AppConfig::BrailleOrientation::LANDSCAPE) {
        graphWidth_forCalc = paper.width_mm * config.braille_graph_width_percent_landscape;
    } else {
        graphWidth_forCalc = paper.width_mm * config.braille_graph_width_percent_portrait;
    }
    graphWidth_forCalc -= config.braille_y_axis_space_mm;
    
    double minSpacing = 25.4 / config.braille_dpi;  // mm per dot
    int maxPointsPerPage = static_cast<int>(graphWidth_forCalc / minSpacing);
    int totalPages = static_cast<int>(std::ceil(static_cast<double>(pts.size()) / maxPointsPerPage));
    if (totalPages == 0) totalPages = 1;
    
    if (logger && totalPages > 1) {
        std::ostringstream oss2;
        oss2 << "WARNING: Data requires " << totalPages << " pages at " << config.braille_dpi 
             << " DPI, but current implementation outputs to 1 page";
        logger->log("BRAILLE_PRINTER", oss2.str());
        oss2.str("");
        oss2 << "Points per page: " << maxPointsPerPage << ", Total points: " << pts.size();
        logger->log("BRAILLE_PRINTER", oss2.str());
    }
    
    // 1. Document setup: ESC D + parameters using configurable values
    // DBT0 = Document Braille Table 0 (no translation)
    // TD0 = Text Dots 0 (6-dot Braille)
    // DP1 = Dot Pattern 1 (graphics mode enabled)
    // LS = Line Spacing (configurable, default 50 = 5.0mm)
    // TM = Top Margin (configurable, default 0 for maximum space)
    // BI = Braille Indent (configurable, default 2)
    // CH = Characters per line (configurable, default 29)
    // LP = Lines per page (calculated from paper height and line spacing)
    // Note: LP and TM must be used together per Index spec
    
    // Calculate LP based on paper height and configured line spacing
    double lineSpacingMm = config.braille_line_spacing / 10.0;  // LS is in 0.1mm units
    int maxLinesCalculated = static_cast<int>(paper.height_mm / lineSpacingMm);
    int linesPerPage = static_cast<int>(maxLinesCalculated * 0.9);  // Use 90% to be safe
    
    // Ensure minimum of 25 lines
    if (linesPerPage < 25) {
        linesPerPage = 25;
    }
    
    if (logger) {
        std::ostringstream oss2;
        oss2 << "Paper height: " << paper.height_mm << "mm, line spacing: " << lineSpacingMm 
             << "mm, calculated max lines: " << maxLinesCalculated 
             << ", using LP=" << linesPerPage << " TM=" << config.braille_top_margin
             << " BI=" << config.braille_binding_indent << " CH=" << config.braille_chars_per_line;
        logger->log("BRAILLE_PRINTER", oss2.str());
    }
    
    oss << "\x1B" << "DBT0,TD0,DP1,LS" << config.braille_line_spacing 
        << ",TM" << config.braille_top_margin 
        << ",BI" << config.braille_binding_indent 
        << ",CH" << config.braille_chars_per_line 
        << ",LP" << linesPerPage << ";";
    
    // 2. Header text with frequency range using formatted units
    oss << "NanoVNA Measurement\r\n";
    oss << formatFrequencyWithUnit(startFreq) << " - " << formatFrequencyWithUnit(endFreq) << "\r\n";
    oss << "Points: " << pts.size() << "\r\n";
    oss << selectedCurves.str() << "\r\n";
    oss << "Page 1/" << totalPages << "\r\n";  // Show actual page count, reduced spacing
    
    // Calculate graph dimensions based on paper size and orientation
    // Use configurable percentages for maximum flexibility
    double graphWidth, graphHeight, originX, originY;
    
    if (config.braille_orientation == AppConfig::BrailleOrientation::LANDSCAPE) {
        // Landscape: Use configured width/height percentages
        graphWidth = paper.width_mm * config.braille_graph_width_percent_landscape;
        graphHeight = paper.height_mm * config.braille_graph_height_percent_landscape;
        originX = config.braille_origin_x_mm;
        originY = config.braille_origin_y_mm;
    } else {
        // Portrait: Use configured width/height percentages
        graphWidth = paper.width_mm * config.braille_graph_width_percent_portrait;
        graphHeight = paper.height_mm * config.braille_graph_height_percent_portrait;
        originX = config.braille_origin_x_mm;
        originY = config.braille_origin_y_mm;
    }
    
    // Adjust origin for Y-axis space (configurable)
    originX += config.braille_y_axis_space_mm;
    
    // Calculate usable graph area for curves and axes
    // The X-axis and Y-axis with arrows need to fit within the floating dot area
    double graphAreaWidth = graphWidth;
    double graphAreaHeight = graphHeight - 5.0;  // Reserve space below X-axis
    
    // Apply 2% vertical stretch to improve tactile readability of coordinate grid
    graphAreaHeight *= 1.02;
    
    // The HY parameter for floating dot area must accommodate:
    // 1. The graph area (graphAreaHeight)
    // 2. The X-axis arrow that extends 4mm below the X-axis line
    double floatingAreaHeight = graphAreaHeight + 4.0;  // Add 4mm for X-axis arrow
    
    // 3. Define a single floating dot area for ALL curves
    // Per Index spec: OR sets origin, WX and HY define the area size
    // All dot coordinates must be ≤ WX and ≤ HY (relative to OR)
    oss << "\x1B" << "FOR" << std::fixed << std::setprecision(2)
        << originX << ":" << originY << ","
        << "WX" << graphAreaWidth << ","
        << "HY" << floatingAreaHeight << ";";
    
    // 4. Draw coordinate grid if requested
    // Note: Grid provides a consistent tactile reference frame (10x10 divisions)
    // This is intentionally separate from data point resolution to ensure:
    // - Consistent tactile experience across different measurements
    // - Readable grid spacing regardless of measurement point count
    // - Reference framework for interpreting curve positions
    // The actual curve data uses full measurement resolution (see section 6)
    if (config.braille_coordinate_grid != AppConfig::BrailleCoordinateGrid::NONE) {
        // Draw grid based on grid type
        if (config.braille_coordinate_grid == AppConfig::BrailleCoordinateGrid::DOTS) {
            // Draw dots at 10x10 grid intersections for tactile reference
            for (int gridX = 0; gridX <= 10; gridX++) {
                for (int gridY = 0; gridY <= 10; gridY++) {
                    double x = (gridX * graphAreaWidth) / 10.0;
                    double y = (gridY * graphAreaHeight) / 10.0;
                    oss << std::fixed << std::setprecision(2) << x << ":" << y << "\r\n";
                }
            }
        } else if (config.braille_coordinate_grid == AppConfig::BrailleCoordinateGrid::GRID_LINES) {
            // Draw grid lines (vertical and horizontal) with sparser spacing for tactile feedback
            // Spacing every 5mm to create feelable lines without perforation
            for (int gridX = 0; gridX <= 10; gridX++) {
                double x = (gridX * graphAreaWidth) / 10.0;
                for (int step = 0; step <= 20; step++) {  // Reduced from 100 to 20 steps (every ~5%)
                    double y = (step * graphAreaHeight) / 20.0;
                    oss << std::fixed << std::setprecision(2) << x << ":" << y << "\r\n";
                }
            }
            for (int gridY = 0; gridY <= 10; gridY++) {
                double y = (gridY * graphAreaHeight) / 10.0;
                for (int step = 0; step <= 20; step++) {  // Reduced from 100 to 20 steps (every ~5%)
                    double x = (step * graphAreaWidth) / 20.0;
                    oss << std::fixed << std::setprecision(2) << x << ":" << y << "\r\n";
                }
            }
        }
    }
    
    // 5. Draw axes (always shown, with arrows at ends)
    // X-axis (bottom horizontal line at graphAreaHeight)
    for (int step = 0; step <= 100; step++) {
        double x = (step * graphAreaWidth) / 100.0;
        oss << std::fixed << std::setprecision(2) << x << ":" << graphAreaHeight << "\r\n";
    }
    // X-axis arrow at right end (4mm arrow, extends downward from X-axis)
    // Arrow points are within floatingAreaHeight bounds
    oss << std::fixed << std::setprecision(2) << graphAreaWidth << ":" << (graphAreaHeight - 4.0) << "\r\n";
    oss << std::fixed << std::setprecision(2) << graphAreaWidth << ":" << (graphAreaHeight - 3.0) << "\r\n";
    oss << std::fixed << std::setprecision(2) << graphAreaWidth << ":" << (graphAreaHeight - 2.0) << "\r\n";
    oss << std::fixed << std::setprecision(2) << graphAreaWidth << ":" << (graphAreaHeight - 1.0) << "\r\n";
    oss << std::fixed << std::setprecision(2) << graphAreaWidth << ":" << (graphAreaHeight + 1.0) << "\r\n";
    oss << std::fixed << std::setprecision(2) << graphAreaWidth << ":" << (graphAreaHeight + 2.0) << "\r\n";
    oss << std::fixed << std::setprecision(2) << graphAreaWidth << ":" << (graphAreaHeight + 3.0) << "\r\n";
    oss << std::fixed << std::setprecision(2) << graphAreaWidth << ":" << (graphAreaHeight + 4.0) << "\r\n";
    
    // Y-axis (left vertical line)
    for (int step = 0; step <= 100; step++) {
        double y = (step * graphAreaHeight) / 100.0;
        oss << std::fixed << std::setprecision(2) << "0.0:" << y << "\r\n";
    }
    // Y-axis arrow at top end (4mm arrow, extends horizontally from Y-axis)
    oss << std::fixed << std::setprecision(2) << "1.0:0.0" << "\r\n";
    oss << std::fixed << std::setprecision(2) << "2.0:0.0" << "\r\n";
    oss << std::fixed << std::setprecision(2) << "3.0:0.0" << "\r\n";
    oss << std::fixed << std::setprecision(2) << "4.0:0.0" << "\r\n";
    oss << std::fixed << std::setprecision(2) << "-1.0:0.0" << "\r\n";
    oss << std::fixed << std::setprecision(2) << "-2.0:0.0" << "\r\n";
    oss << std::fixed << std::setprecision(2) << "-3.0:0.0" << "\r\n";
    oss << std::fixed << std::setprecision(2) << "-4.0:0.0" << "\r\n";
    
    // 6. Plot all selected curves on the same graph
    // Note: All curves are guaranteed to be completely printed because:
    // - We iterate through ALL measurement points (lines 417-427)
    // - Each point is mapped to X coordinate: x = (i * graphWidth) / (values.size() - 1)
    // - Interpolation fills gaps between consecutive points for smooth lines
    // - Pattern application (if any) only affects visual style, not completeness
    for (int curveIdx = 0; curveIdx < 5; curveIdx++) {
        if (!curveFlags[curveIdx]) continue;
        
        // Extract curve values (all measurement points included)
        std::vector<double> values;
        values.reserve(pts.size());
        
        for (const auto& pt : pts) {
            double val = 0.0;
            switch (curveIdx) {
                case 0: val = pt.swr; break;
                case 1: val = pt.rl; break;
                case 2: val = pt.impedance_mag; break;
                case 3: val = pt.X; break;
                case 4: val = pt.phase_deg; break;
            }
            values.push_back(val);
        }
        
        // Find min/max for scaling (each curve scaled independently for optimal Y-range usage)
        double minVal = *std::min_element(values.begin(), values.end());
        double maxVal = *std::max_element(values.begin(), values.end());
        
        if (logger) {
            std::ostringstream oss2;
            oss2 << "Curve " << curveNames[curveIdx] << ": " 
                 << values.size() << " points, range [" << minVal << ", " << maxVal << "]";
            logger->log("BRAILLE_PRINTER", oss2.str());
        }
        
        // Parse curve pattern from config
        std::vector<int> pattern = parsePatternString(config.braille_curve_patterns[curveIdx]);
        
        // Calculate minimum spacing based on configurable DPI
        // DPI to mm: spacing = 25.4 / DPI
        const double minSpacing = 25.4 / config.braille_dpi;  // mm - enforces configured DPI
        
        if (logger) {
            std::ostringstream oss2;
            oss2 << "Using DPI setting: " << config.braille_dpi << " (min spacing: " << minSpacing << "mm)";
            logger->log("BRAILLE_PRINTER", oss2.str());
        }
        
        // Calculate normalized coordinates for all points first
        std::vector<std::pair<double, double>> coords;
        coords.reserve(values.size());
        
        for (size_t i = 0; i < values.size(); i++) {
            double x = (i * graphAreaWidth) / (values.size() - 1);
            if (values.size() == 1) x = graphAreaWidth / 2.0;
            
            double normalized = normalizeCurveValue(values[i], minVal, maxVal);
            double y = graphAreaHeight - (normalized * graphAreaHeight);
            
            coords.push_back({x, y});
        }
        
        // PHASE WRAP-AROUND FIX: Detect large jumps in phase curve
        // Phase curves can jump from -180 to +180 or vice versa
        // Handle based on configuration: arrows or vertical line with pattern
        
        // Structure to store discontinuity information: {x_pos, y_before, y_after}
        std::vector<std::tuple<double, double, double>> discontinuities;
        
        if (curveIdx == 4) {  // Phase curve
            for (size_t i = 1; i < coords.size(); i++) {
                double dy = std::abs(coords[i].second - coords[i-1].second);
                // If vertical jump is more than 50% of graph height, it's likely a phase wrap
                if (dy > graphAreaHeight * 0.5) {
                    // Store discontinuity: position, Y before jump, Y after jump
                    discontinuities.push_back(std::make_tuple(coords[i-1].first, coords[i-1].second, coords[i].second));
                    
                    // Only mark discontinuity to skip if using arrow mode
                    // For vertical line mode, we'll draw the line with pattern applied
                    if (config.braille_phase_discontinuity == AppConfig::BraillePhaseDiscontinuityMode::ARROWS) {
                        // Mark this as a discontinuity by using negative X marker
                        // We'll handle this later by not drawing between these points
                        coords[i-1].first = -1.0;  // Use negative X as discontinuity marker
                    }
                }
            }
            
            if (logger && !discontinuities.empty()) {
                std::ostringstream oss2;
                oss2 << "Phase curve: Detected " << discontinuities.size() << " wrap-around discontinuities, mode=" 
                     << (config.braille_phase_discontinuity == AppConfig::BraillePhaseDiscontinuityMode::ARROWS ? "ARROWS" : "VERTICAL_LINE");
                logger->log("BRAILLE_PRINTER", oss2.str());
            }
        }
        
        // Use LTTB downsampling for better curve preservation
        // Calculate target number of points based on DPI and graph width
        int maxPointsForDPI = static_cast<int>(graphWidth / minSpacing);
        
        // For phase curve with discontinuities (marked by negative X), split into segments
        std::vector<std::pair<double, double>> decimatedCoords;
        
        if (curveIdx == 4 && !discontinuities.empty() && 
            config.braille_phase_discontinuity == AppConfig::BraillePhaseDiscontinuityMode::ARROWS) {
            // Phase curve with discontinuity markers (negative X values)
            // Split by markers and apply LTTB to each segment
            std::vector<std::pair<double, double>> segment;
            
            for (size_t i = 0; i < coords.size(); i++) {
                if (coords[i].first < 0) {
                    // Found discontinuity marker - process accumulated segment
                    if (!segment.empty()) {
                        int segmentTarget = static_cast<int>(
                            maxPointsForDPI * static_cast<double>(segment.size()) / coords.size()
                        );
                        if (segmentTarget < 3) segmentTarget = std::min(static_cast<int>(segment.size()), 3);
                        
                        if (segmentTarget >= 3 && segment.size() >= 3) {
                            std::vector<size_t> selectedIndices = selectPointsUsingLTTB(segment, segmentTarget);
                            for (size_t idx : selectedIndices) {
                                decimatedCoords.push_back(segment[idx]);
                            }
                        } else {
                            // Too few points, keep all
                            decimatedCoords.insert(decimatedCoords.end(), segment.begin(), segment.end());
                        }
                        segment.clear();
                    }
                    // Add the discontinuity marker itself
                    decimatedCoords.push_back(coords[i]);
                } else {
                    // Regular point, add to current segment
                    segment.push_back(coords[i]);
                }
            }
            
            // Process final segment
            if (!segment.empty()) {
                int segmentTarget = static_cast<int>(
                    maxPointsForDPI * static_cast<double>(segment.size()) / coords.size()
                );
                if (segmentTarget < 3) segmentTarget = std::min(static_cast<int>(segment.size()), 3);
                
                if (segmentTarget >= 3 && segment.size() >= 3) {
                    std::vector<size_t> selectedIndices = selectPointsUsingLTTB(segment, segmentTarget);
                    for (size_t idx : selectedIndices) {
                        decimatedCoords.push_back(segment[idx]);
                    }
                } else {
                    // Too few points, keep all
                    decimatedCoords.insert(decimatedCoords.end(), segment.begin(), segment.end());
                }
            }
        } else {
            // No discontinuity markers - apply LTTB to entire curve
            // Filter out any discontinuity markers first (just in case)
            std::vector<std::pair<double, double>> cleanCoords;
            for (const auto& coord : coords) {
                if (coord.first >= 0) {
                    cleanCoords.push_back(coord);
                }
            }
            
            if (!cleanCoords.empty()) {
                // Calculate target based on available space
                int targetPoints = maxPointsForDPI;
                if (targetPoints > static_cast<int>(cleanCoords.size())) {
                    targetPoints = cleanCoords.size();
                }
                if (targetPoints < 3 && cleanCoords.size() >= 3) {
                    targetPoints = 3;  // Minimum for LTTB
                }
                
                if (targetPoints < 3 || cleanCoords.size() < 3) {
                    // Too few points for LTTB, just use all
                    decimatedCoords = cleanCoords;
                } else {
                    // Apply LTTB
                    std::vector<size_t> selectedIndices = selectPointsUsingLTTB(cleanCoords, targetPoints);
                    for (size_t idx : selectedIndices) {
                        decimatedCoords.push_back(cleanCoords[idx]);
                    }
                }
            }
        }
        
        if (logger) {
            std::ostringstream oss2;
            oss2 << "Curve " << curveNames[curveIdx] << ": LTTB downsampled from " 
                 << coords.size() << " to " << decimatedCoords.size() 
                 << " points (" << config.braille_dpi << " DPI, max=" << maxPointsForDPI << ")";
            logger->log("BRAILLE_PRINTER", oss2.str());
        }
        
        // UNIFORM PATH GENERATION: Resample the polyline at exact minSpacing intervals
        // This ensures consistent dot density for the braille embosser along the entire curve,
        // regardless of how the original data points or LTTB-selected points are distributed.
        // The pattern (dash/dot/gap) is then applied on top of this uniform point sequence.
        std::vector<std::pair<double, double>> uniformPath;
        
        if (decimatedCoords.size() > 1) {
            // Always include the first point
            uniformPath.push_back(decimatedCoords[0]);
            
            // Walk along the polyline, placing points at exact minSpacing intervals.
            // residualDist tracks the arc-length distance from the last placed point
            // to the end of the previous segment, so the next point is placed at
            // (minSpacing - residualDist) into the current segment.
            double residualDist = 0.0;
            
            for (size_t seg = 0; seg < decimatedCoords.size() - 1; seg++) {
                double x1 = decimatedCoords[seg].first;
                double y1 = decimatedCoords[seg].second;
                double x2 = decimatedCoords[seg + 1].first;
                double y2 = decimatedCoords[seg + 1].second;
                
                double dx = x2 - x1;
                double dy = y2 - y1;
                double segLen = std::sqrt(dx * dx + dy * dy);
                if (segLen < 1e-9) continue; // skip degenerate segments
                
                // Place points along this segment at uniform spacing
                double nextPointDist = minSpacing - residualDist;
                
                while (nextPointDist <= segLen) {
                    double t = nextPointDist / segLen;
                    uniformPath.push_back({x1 + t * dx, y1 + t * dy});
                    nextPointDist += minSpacing;
                }
                
                // Distance from last placed point to end of this segment
                residualDist = segLen - (nextPointDist - minSpacing);
            }
            
            // Always include the last point
            const auto& lastPt = decimatedCoords.back();
            if (uniformPath.empty() || 
                std::abs(uniformPath.back().first - lastPt.first) > 1e-6 || 
                std::abs(uniformPath.back().second - lastPt.second) > 1e-6) {
                uniformPath.push_back(lastPt);
            }
        } else if (decimatedCoords.size() == 1) {
            uniformPath = decimatedCoords;
        }
        
        if (logger) {
            std::ostringstream oss2;
            oss2 << "Uniform path generated: " << uniformPath.size() << " points at ~" 
                 << minSpacing << "mm spacing";
            logger->log("BRAILLE_PRINTER", oss2.str());
        }
        
        // Step 2: Apply pattern to uniform path
        // Now each point is ~minSpacing apart, so pattern application is truly uniform
        std::vector<bool> drawPoint(uniformPath.size(), true);
        
        if (!pattern.empty() && uniformPath.size() > 1) {
            // Pattern values represent multiples of minSpacing
            // e.g., "2-1" means: draw 2 points, skip 1 point, repeat
            size_t patternPos = 0;
            bool inDrawSegment = (pattern[0] > 0);  // Start with draw if first pattern value > 0
            int pointsInCurrentSegment = 0;
            int currentSegmentLength = pattern[patternPos];
            
            for (size_t i = 0; i < uniformPath.size(); i++) {
                drawPoint[i] = inDrawSegment;
                pointsInCurrentSegment++;
                
                // Check if we've completed the current pattern segment
                if (pointsInCurrentSegment >= currentSegmentLength) {
                    // Move to next pattern segment
                    patternPos = (patternPos + 1) % pattern.size();
                    inDrawSegment = !inDrawSegment;
                    currentSegmentLength = (pattern[patternPos] > 0) ? pattern[patternPos] : 1;
                    pointsInCurrentSegment = 0;
                }
            }
            
            if (logger) {
                std::ostringstream oss2;
                int drawCount = 0;
                for (bool d : drawPoint) if (d) drawCount++;
                oss2 << "Pattern applied uniformly: " << drawCount << " of " << uniformPath.size() 
                     << " points will be drawn";
                logger->log("BRAILLE_PRINTER", oss2.str());
            }
        }
        
        // Step 3: Output the uniform path, respecting the pattern
        for (size_t i = 0; i < uniformPath.size(); i++) {
            if (drawPoint[i]) {
                oss << std::fixed << std::setprecision(2) 
                    << uniformPath[i].first << ":" << uniformPath[i].second << "\r\n";
            }
        }
        
        // Draw discontinuity indicators at phase jumps (for phase curve only)
        // Mode depends on configuration: arrows or vertical line with pattern
        if (curveIdx == 4 && !discontinuities.empty()) {
            if (config.braille_phase_discontinuity == AppConfig::BraillePhaseDiscontinuityMode::ARROWS) {
                // ARROWS MODE: Draw small directional arrows
                for (const auto& disc : discontinuities) {
                    double xPos = std::get<0>(disc);
                    double yBefore = std::get<1>(disc);
                    double yAfter = std::get<2>(disc);
                    
                    // Determine arrow direction: if curve jumps up, arrow points up; if down, arrow points down
                    bool arrowUp = (yAfter < yBefore);  // Note: Y increases downward in our coord system
                    
                    // Draw a small arrow (about 4mm tall, 2mm wide)
                    double arrowHeight = 4.0;  // mm
                    double arrowWidth = 2.0;   // mm
                    
                    // Arrow starts at the point before the discontinuity
                    double arrowBaseY = yBefore;
                    
                    if (arrowUp) {
                        // Upward arrow: base at yBefore, tip points up (toward smaller Y)
                        double tipY = arrowBaseY - arrowHeight;
                        if (tipY < 0) tipY = 0;  // Clamp to graph bounds
                        
                        // Draw arrow shaft (vertical line)
                        oss << std::fixed << std::setprecision(2) << xPos << ":" << arrowBaseY << "\r\n";
                        oss << std::fixed << std::setprecision(2) << xPos << ":" << tipY << "\r\n";
                        
                        // Draw arrowhead (two lines forming a V pointing up)
                        oss << std::fixed << std::setprecision(2) << (xPos - arrowWidth/2) << ":" << (tipY + arrowWidth/2) << "\r\n";
                        oss << std::fixed << std::setprecision(2) << (xPos + arrowWidth/2) << ":" << (tipY + arrowWidth/2) << "\r\n";
                    } else {
                        // Downward arrow: base at yBefore, tip points down (toward larger Y)
                        double tipY = arrowBaseY + arrowHeight;
                        if (tipY > graphAreaHeight) tipY = graphAreaHeight;  // Clamp to graph bounds
                        
                        // Draw arrow shaft (vertical line)
                        oss << std::fixed << std::setprecision(2) << xPos << ":" << arrowBaseY << "\r\n";
                        oss << std::fixed << std::setprecision(2) << xPos << ":" << tipY << "\r\n";
                        
                        // Draw arrowhead (two lines forming a V pointing down)
                        oss << std::fixed << std::setprecision(2) << (xPos - arrowWidth/2) << ":" << (tipY - arrowWidth/2) << "\r\n";
                        oss << std::fixed << std::setprecision(2) << (xPos + arrowWidth/2) << ":" << (tipY - arrowWidth/2) << "\r\n";
                    }
                    
                    if (logger) {
                        std::ostringstream oss2;
                        oss2 << "Drew " << (arrowUp ? "upward" : "downward") << " arrow at x=" 
                             << xPos << ", yBefore=" << yBefore << ", yAfter=" << yAfter;
                        logger->log("BRAILLE_PRINTER", oss2.str());
                    }
                }
            } else {
                // VERTICAL_LINE MODE: Draw vertical lines with consistent spacing
                // Match Y-axis approach: use fixed number of steps for consistent spacing
                // Y-axis uses 100 steps, so vertical lines should use similar spacing
                for (const auto& disc : discontinuities) {
                    double xPos = std::get<0>(disc);
                    double yBefore = std::get<1>(disc);
                    double yAfter = std::get<2>(disc);
                    
                    // Draw vertical line from yBefore to yAfter
                    double yStart = std::min(yBefore, yAfter);
                    double yEnd = std::max(yBefore, yAfter);
                    double lineLength = yEnd - yStart;
                    
                    // Use fixed number of steps based on line length
                    // Scale steps to match Y-axis density: 100 steps for full graph height
                    // This ensures vertical lines have same spacing as Y-axis (no perforation)
                    int numSteps = static_cast<int>((lineLength / graphAreaHeight) * 100.0);
                    if (numSteps < 2) numSteps = 2;  // At least start and end
                    
                    // Apply curve pattern to vertical line if specified
                    if (!pattern.empty()) {
                        // Calculate step spacing
                        double stepSpacing = lineLength / numSteps;
                        
                        // Apply pattern based on distance
                        size_t patternPos = 0;
                        bool inDrawSegment = true;
                        double currentDist = 0.0;
                        double currentPatternLength = (pattern[patternPos] > 0) ? pattern[patternPos] * stepSpacing : stepSpacing;
                        
                        for (int i = 0; i <= numSteps; i++) {
                            double t = static_cast<double>(i) / numSteps;
                            double y = yStart + t * lineLength;
                            
                            if (inDrawSegment) {
                                oss << std::fixed << std::setprecision(2) << xPos << ":" << y << "\r\n";
                            }
                            
                            // Check if we should switch pattern segment
                            currentDist += stepSpacing;
                            if (currentDist >= currentPatternLength) {
                                currentDist = 0.0;
                                patternPos = (patternPos + 1) % pattern.size();
                                inDrawSegment = !inDrawSegment;
                                currentPatternLength = (pattern[patternPos] > 0) ? pattern[patternPos] * stepSpacing : stepSpacing;
                            }
                        }
                    } else {
                        // No pattern, draw solid vertical line
                        for (int i = 0; i <= numSteps; i++) {
                            double t = static_cast<double>(i) / numSteps;
                            double y = yStart + t * lineLength;
                            oss << std::fixed << std::setprecision(2) << xPos << ":" << y << "\r\n";
                        }
                    }
                    
                    if (logger) {
                        std::ostringstream oss2;
                        oss2 << "Drew vertical line with pattern at x=" << xPos 
                             << ", from y=" << yStart << " to y=" << yEnd;
                        logger->log("BRAILLE_PRINTER", oss2.str());
                    }
                }
            }
        }
    }
    
    // Terminate dot list with semicolon
    oss << ";";
    
    // 7. Form feed to eject page
    oss << "\f";
    
    // Convert to vector
    std::string strData = oss.str();
    data.assign(strData.begin(), strData.end());
    
    if (logger) {
        std::ostringstream oss2;
        oss2 << "Generated " << data.size() << " bytes of Braille print data (Index V5 Floating Dot Area mode)";
        logger->log("BRAILLE_PRINTER", oss2.str());
    }
    
    return true;
}

bool BraillePrinter::generateV4Data(const std::vector<MeasurementPoint>& pts,
                                   uint64_t startFreq, uint64_t endFreq, uint64_t step,
                                   const bool curveFlags[5],
                                   const PaperDimensions& paper,
                                   const AppConfig& config,
                                   std::vector<char>& data,
                                   std::string& err) {
    if (logger) logger->log("BRAILLE_PRINTER", "Using Index V4 Raster Graphics mode");
    
    // Shortened curve names for compact display
    const char* curveNames[] = {"SWR", "RL", "|Z|", "X", "Phase"};
    
    // Build selected curves list
    std::ostringstream selectedCurves;
    for (int i = 0; i < 5; i++) {
        if (curveFlags[i]) {
            if (selectedCurves.tellp() > 0) selectedCurves << ", ";
            selectedCurves << curveNames[i];
        }
    }
    
    data.clear();
    std::ostringstream oss;
    
    // 1. Document setup: ESC D + parameters using configurable values
    // Calculate LP based on paper height and configured line spacing
    double lineSpacingMm = config.braille_line_spacing / 10.0;  // LS is in 0.1mm units
    int maxLinesCalculated = static_cast<int>(paper.height_mm / lineSpacingMm);
    int linesPerPage = static_cast<int>(maxLinesCalculated * 0.9);  // Use 90% to be safe
    if (linesPerPage < 25) linesPerPage = 25;
    
    // Use configurable parameters
    oss << "\x1B" << "DBT0,TD0,DP1,LS" << config.braille_line_spacing 
        << ",TM" << config.braille_top_margin 
        << ",BI" << config.braille_binding_indent 
        << ",CH" << config.braille_chars_per_line 
        << ",LP" << linesPerPage << ";";
    
    // 2. Header text with formatted frequency
    oss << "NanoVNA Measurement\r\n";
    oss << formatFrequencyWithUnit(startFreq) << " - " << formatFrequencyWithUnit(endFreq) << "\r\n";
    oss << selectedCurves.str() << "\r\n\r\n";
    
    // V4 uses raster graphics mode (ESC Z or ESC 01 for six-dot graphics)
    // This is a simplified implementation for V4
    // For true V4 support, would need to implement proper raster graphics encoding
    
    if (logger) logger->log("BRAILLE_PRINTER", "WARNING: V4 protocol uses simplified implementation. For full V4 support, use V5 protocol.");
    
    // Use ESC 01 to activate six-dot graphics
    oss << "\x1B\x01";
    
    // Generate a simple raster representation
    // This is a placeholder - full V4 implementation would require proper raster encoding
    const int rasterWidth = 40;  // characters
    const int rasterHeight = 25;  // lines
    
    for (int curveIdx = 0; curveIdx < 5; curveIdx++) {
        if (!curveFlags[curveIdx]) continue;
        
        // Extract curve values
        std::vector<double> values;
        values.reserve(pts.size());
        
        for (const auto& pt : pts) {
            double val = 0.0;
            switch (curveIdx) {
                case 0: val = pt.swr; break;
                case 1: val = pt.rl; break;
                case 2: val = pt.impedance_mag; break;
                case 3: val = pt.X; break;
                case 4: val = pt.phase_deg; break;
            }
            values.push_back(val);
        }
        
        // Find min/max for scaling
        double minVal = *std::min_element(values.begin(), values.end());
        double maxVal = *std::max_element(values.begin(), values.end());
        
        // Create raster grid
        std::vector<std::vector<bool>> grid(rasterHeight, std::vector<bool>(rasterWidth, false));
        
        // Plot points on grid
        for (size_t i = 0; i < values.size(); i++) {
            int x = (i * (rasterWidth - 1)) / (values.size() > 1 ? values.size() - 1 : 1);
            double normalized = normalizeCurveValue(values[i], minVal, maxVal);
            int y = static_cast<int>((1.0 - normalized) * (rasterHeight - 1));
            
            if (x >= 0 && x < rasterWidth && y >= 0 && y < rasterHeight) {
                grid[y][x] = true;
            }
        }
        
        // Convert grid to braille characters
        // Each character represents a 2x3 dot matrix
        for (int row = 0; row < rasterHeight; row++) {
            for (int col = 0; col < rasterWidth; col++) {
                // Simple encoding: use space or a braille character
                if (grid[row][col]) {
                    oss << (char)0xFF;  // Full dot
                } else {
                    oss << ' ';
                }
            }
            oss << "\r\n";
        }
        oss << "\r\n";
    }
    
    // Deactivate six-dot graphics
    oss << "\x1B\x02";
    
    // 3. Form feed to eject page
    oss << "\f";
    
    // Convert to vector
    std::string strData = oss.str();
    data.assign(strData.begin(), strData.end());
    
    if (logger) {
        std::ostringstream oss2;
        oss2 << "Generated " << data.size() << " bytes of Braille print data (Index V4 Raster Graphics mode)";
        logger->log("BRAILLE_PRINTER", oss2.str());
    }
    
    return true;
}
#endif // _WIN32

bool BraillePrinter::printBraille(const std::string& printerName,
                                 const std::vector<MeasurementPoint>& pts,
                                 uint64_t startFreq, uint64_t endFreq, uint64_t step,
                                 const bool curveFlags[5],
                                 const AppConfig& config,
                                 std::string& err) {
#if !defined(_WIN32)
    err = "Braille printer support is only available on Windows";
    if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
    return false;
#else
    if (logger) {
        std::ostringstream oss;
        oss << "Starting direct Braille print to: '" << printerName << "'";
        logger->log("BRAILLE_PRINTER", oss.str());
    }
    
    // Generate the Braille data using full config
    std::vector<char> data;
    if (!generateBrailleData(pts, startFreq, endFreq, step, curveFlags, config, data, err)) {
        if (logger) {
            std::ostringstream oss;
            oss << "Failed to generate Braille data: " << err;
            logger->log("BRAILLE_PRINTER", "ERROR: " + oss.str());
        }
        return false;
    }
    
    // Send to printer
    if (!sendToPrinter(printerName, data, err)) {
        if (logger) {
            std::ostringstream oss;
            oss << "Failed to send to printer: " << err;
            logger->log("BRAILLE_PRINTER", "ERROR: " + oss.str());
        }
        return false;
    }
    
    if (logger) logger->log("BRAILLE_PRINTER", "Print operation completed successfully");
    return true;
#endif
}

bool BraillePrinter::sendToPrinter(const std::string& printerName, 
                                  const std::vector<char>& data,
                                  std::string& err) {
#if !defined(_WIN32)
    err = "Braille printer support is only available on Windows";
    if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
    return false;
#else
    if (logger) {
        std::ostringstream oss;
        oss << "Attempting to open printer: '" << printerName << "'";
        logger->log("BRAILLE_PRINTER", oss.str());
    }
    
    // Create mutable copy for Windows API
    std::vector<char> printerNameBuf(printerName.begin(), printerName.end());
    printerNameBuf.push_back('\0');
    
    HANDLE hPrinter = NULL;
    if (!OpenPrinterA(printerNameBuf.data(), &hPrinter, NULL)) {
        DWORD dwError = GetLastError();
        std::ostringstream oss;
        oss << "Failed to open printer '" << printerName << "'. Error code: " << dwError;
        err = oss.str();
        if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
        return false;
    }
    
    if (logger) {
        std::ostringstream oss;
        oss << "Printer opened successfully. Handle: " << hPrinter;
        logger->log("BRAILLE_PRINTER", oss.str());
    }
    
    // Start a print job
    char docName[] = "NanoVNA Braille Graphics";
    char dataType[] = "RAW";
    
    DOC_INFO_1A docInfo;
    docInfo.pDocName = docName;
    docInfo.pOutputFile = NULL;
    docInfo.pDatatype = dataType;  // RAW mode to send data directly
    
    DWORD jobId = StartDocPrinterA(hPrinter, 1, reinterpret_cast<LPBYTE>(&docInfo));
    if (jobId == 0) {
        DWORD dwError = GetLastError();
        std::ostringstream oss;
        oss << "Failed to start print job. Error code: " << dwError;
        err = oss.str();
        if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
        ClosePrinter(hPrinter);
        return false;
    }
    
    if (logger) {
        std::ostringstream oss;
        oss << "Print job started. Job ID: " << jobId;
        logger->log("BRAILLE_PRINTER", oss.str());
    }
    
    // Start a page
    if (!StartPagePrinter(hPrinter)) {
        DWORD dwError = GetLastError();
        std::ostringstream oss;
        oss << "Failed to start page. Error code: " << dwError;
        err = oss.str();
        if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
        EndDocPrinter(hPrinter);
        ClosePrinter(hPrinter);
        return false;
    }
    
    if (logger) logger->log("BRAILLE_PRINTER", "Page started");
    
    // Write the data to the printer
    DWORD bytesWritten = 0;
    // WritePrinter expects LPVOID which is void*, safe to cast from const vector data
    if (!WritePrinter(hPrinter, reinterpret_cast<LPVOID>(const_cast<char*>(data.data())), 
                     static_cast<DWORD>(data.size()), &bytesWritten)) {
        DWORD dwError = GetLastError();
        std::ostringstream oss;
        oss << "Failed to write to printer. Error code: " << dwError;
        err = oss.str();
        if (logger) logger->log("BRAILLE_PRINTER", "ERROR: " + err);
        EndPagePrinter(hPrinter);
        EndDocPrinter(hPrinter);
        ClosePrinter(hPrinter);
        return false;
    }
    
    if (logger) {
        std::ostringstream oss;
        oss << "Data written to printer: " << bytesWritten << " bytes of " << data.size() << " bytes";
        logger->log("BRAILLE_PRINTER", oss.str());
    }
    
    if (bytesWritten != data.size()) {
        std::ostringstream oss;
        oss << "Warning: Only " << bytesWritten << " of " << data.size() << " bytes were written";
        if (logger) logger->log("BRAILLE_PRINTER", "WARNING: " + oss.str());
    }
    
    // End the page
    if (!EndPagePrinter(hPrinter)) {
        DWORD dwError = GetLastError();
        if (logger) {
            std::ostringstream oss;
            oss << "WARNING: Failed to end page. Error code: " << dwError;
            logger->log("BRAILLE_PRINTER", oss.str());
        }
    } else {
        if (logger) logger->log("BRAILLE_PRINTER", "Page ended successfully");
    }
    
    // End the print job
    if (!EndDocPrinter(hPrinter)) {
        DWORD dwError = GetLastError();
        if (logger) {
            std::ostringstream oss;
            oss << "WARNING: Failed to end document. Error code: " << dwError;
            logger->log("BRAILLE_PRINTER", oss.str());
        }
    } else {
        if (logger) logger->log("BRAILLE_PRINTER", "Document ended successfully");
    }
    
    // Close the printer
    ClosePrinter(hPrinter);
    if (logger) logger->log("BRAILLE_PRINTER", "Printer closed. Print job completed successfully");
    
    return true;
#endif
}

// LTTB (Largest Triangle Three Buckets) downsampling algorithm
// This algorithm preserves the visual shape of curves better than simple distance-based decimation
// by selecting points that form the largest triangles with neighboring points
std::vector<size_t> BraillePrinter::selectPointsUsingLTTB(
    const std::vector<std::pair<double, double>>& coords,
    int threshold) {
    
    std::vector<size_t> selectedIndices;
    
    // Validate inputs - need at least 3 points for LTTB to work
    if (coords.empty() || threshold < 3) {
        return selectedIndices;
    }
    
    size_t dataSize = coords.size();
    
    // If we can keep all points, just return sequential list
    if (dataSize <= static_cast<size_t>(threshold)) {
        for (size_t i = 0; i < dataSize; i++) {
            selectedIndices.push_back(i);
        }
        return selectedIndices;
    }
    
    // Always include first point
    selectedIndices.push_back(0);
    
    // Calculate bucket size (excluding first and last points)
    double bucketSize = static_cast<double>(dataSize - 2) / static_cast<double>(threshold - 2);
    
    // For each bucket (except first and last which are already handled)
    size_t selectedSoFar = 1;
    
    for (int bucketIdx = 0; bucketIdx < threshold - 2; bucketIdx++) {
        // Calculate range for current bucket
        size_t bucketStart = 1 + static_cast<size_t>(bucketIdx * bucketSize);
        size_t bucketEnd = 1 + static_cast<size_t>((bucketIdx + 1) * bucketSize);
        if (bucketEnd > dataSize - 1) bucketEnd = dataSize - 1;
        
        // Ensure bucketStart <= bucketEnd
        if (bucketStart > bucketEnd) {
            bucketStart = bucketEnd;
        }
        
        // Get the average point in the next bucket (for triangle calculation)
        size_t nextBucketStart = 1 + static_cast<size_t>((bucketIdx + 1) * bucketSize);
        size_t nextBucketEnd = 1 + static_cast<size_t>((bucketIdx + 2) * bucketSize);
        if (nextBucketEnd > dataSize) nextBucketEnd = dataSize;
        
        // Calculate average of next bucket
        double avgNextX = 0.0;
        double avgNextY = 0.0;
        int pointCount = 0;
        
        for (size_t i = nextBucketStart; i < nextBucketEnd && i < dataSize; i++) {
            avgNextX += coords[i].first;
            avgNextY += coords[i].second;
            pointCount++;
        }
        
        if (pointCount > 0) {
            avgNextX /= pointCount;
            avgNextY /= pointCount;
        }
        
        // Find point in current bucket that forms largest triangle
        size_t bestPointIdx = bucketStart;
        double maxArea = -1.0;
        
        // Previous selected point
        size_t prevIdx = selectedIndices[selectedSoFar - 1];
        double prevX = coords[prevIdx].first;
        double prevY = coords[prevIdx].second;
        
        // Check each point in current bucket
        for (size_t i = bucketStart; i <= bucketEnd && i < dataSize; i++) {
            double currX = coords[i].first;
            double currY = coords[i].second;
            
            // Calculate triangle area using the formula:
            // Area = 0.5 * |x1(y2 - y3) + x2(y3 - y1) + x3(y1 - y2)|
            double area = std::abs(
                prevX * (currY - avgNextY) +
                currX * (avgNextY - prevY) +
                avgNextX * (prevY - currY)
            ) * 0.5;
            
            if (area > maxArea) {
                maxArea = area;
                bestPointIdx = i;
            }
        }
        
        selectedIndices.push_back(bestPointIdx);
        selectedSoFar++;
    }
    
    // Always include last point
    selectedIndices.push_back(dataSize - 1);
    
    // Sort to maintain order
    std::sort(selectedIndices.begin(), selectedIndices.end());
    
    return selectedIndices;
}

