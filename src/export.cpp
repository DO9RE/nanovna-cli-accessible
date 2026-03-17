#include "export.h"
#include "braille_printer.h"
#include "bitmap_writer.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <limits>

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

// Helper to format a TXT measurement line with consistent fixed-point notation
static void writeTxtMeasurementLine(std::ostream& ofs, const MeasurementPoint& p) {
    ofs << "Freq: " << p.freq << " Hz  SWR: "
        << std::fixed << std::setprecision(1) << p.swr
        << "  RL: " << std::setprecision(2) << p.rl
        << "  R: " << p.R << "  X: " << p.X << "\n";
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
        writeTxtMeasurementLine(ofs, p);
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
        writeTxtMeasurementLine(ofs, p);
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

bool ExportModule::exportBraille(const std::vector<MeasurementPoint>& pts,
                                 const std::vector<size_t>& audioIndices,
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
    
    // Use BraillePrinter with audio indices (Pflichtenheft §9)
    BraillePrinter printer(nullptr);
    std::vector<char> brailleData;
    if (!printer.generateBrailleData(pts, audioIndices, startFreq, endFreq, step,
                                    curveFlags, config, brailleData, err)) {
        return false;
    }
    
    // Write to file
    std::ofstream ofsBraille(generatedFilename, std::ios::binary);
    if (!ofsBraille) { 
        err = "Cannot open file: " + generatedFilename; 
        return false; 
    }
    
    ofsBraille.write(brailleData.data(), brailleData.size());
    
    if (!ofsBraille.good()) {
        err = "Failed to write braille data to file";
        return false;
    }
    
    return true;
}

// ============================================================
// Bitmap Export: Acoustic Curves as Image
// ============================================================

// Curve colors: SWR=Red, RL=Blue, |Z|=Green, X=Orange, Phase=Violet
static const struct { uint8_t r, g, b; const char* name; } CURVE_COLORS[5] = {
    {255,   0,   0, "SWR"},
    {  0,   0, 255, "RL (dB)"},
    {  0, 160,   0, "|Z| (Ohm)"},
    {255, 128,   0, "X (Ohm)"},
    {128,   0, 255, "Phase (deg)"}
};

// Extract curve value from a MeasurementPoint by curve index
static double getCurveValue(const MeasurementPoint& pt, int curveIdx) {
    switch (curveIdx) {
        case 0: return pt.swr;
        case 1: return pt.rl;
        case 2: return pt.impedance_mag;
        case 3: return pt.X;
        case 4: return pt.phase_deg;
        default: return 0.0;
    }
}

// Format a number for axis labels (compact representation)
static std::string formatAxisValue(double val) {
    std::ostringstream oss;
    if (std::abs(val) >= 1000.0) {
        oss << std::fixed << std::setprecision(0) << val;
    } else if (std::abs(val) >= 10.0) {
        oss << std::fixed << std::setprecision(1) << val;
    } else {
        oss << std::fixed << std::setprecision(2) << val;
    }
    return oss.str();
}

// Format frequency for display (Hz, kHz, MHz)
static std::string formatFrequency(uint64_t freqHz) {
    std::ostringstream oss;
    if (freqHz >= 1000000) {
        oss << std::fixed << std::setprecision(3) << (freqHz / 1000000.0) << " MHz";
    } else if (freqHz >= 1000) {
        oss << std::fixed << std::setprecision(1) << (freqHz / 1000.0) << " kHz";
    } else {
        oss << freqHz << " Hz";
    }
    return oss.str();
}

bool ExportModule::exportAcousticBitmap(
    const std::vector<MeasurementPoint>& pts,
    const std::vector<size_t>& audioIndices,
    uint64_t startFreq, uint64_t endFreq, uint64_t step,
    const bool curveFlags[5],
    const AppConfig& config,
    std::string& generatedFilename, std::string& err)
{
    if (pts.empty()) {
        err = "No data to export";
        return false;
    }
    
    bool anyCurveSelected = false;
    for (int i = 0; i < 5; i++) {
        if (curveFlags[i]) { anyCurveSelected = true; break; }
    }
    if (!anyCurveSelected) {
        err = "No curves selected for export";
        return false;
    }
    
    generatedFilename = generateFilename("_acoustic.bmp", startFreq, endFreq, step);
    
    const int imgW = std::max(200, std::min(config.bitmap_acoustic_width, 4000));
    const int imgH = std::max(150, std::min(config.bitmap_acoustic_height, 3000));
    
    // Margins
    const int marginLeft   = 80;
    const int marginRight  = 30;
    const int marginTop    = 50;
    const int marginBottom = 60;
    const int legendHeight = 20;
    
    const int graphLeft   = marginLeft;
    const int graphRight  = imgW - marginRight;
    const int graphTop    = marginTop;
    const int graphBottom = imgH - marginBottom - legendHeight;
    const int graphW      = graphRight - graphLeft;
    const int graphH      = graphBottom - graphTop;
    
    if (graphW < 50 || graphH < 50) {
        err = "Image size too small for rendering";
        return false;
    }
    
    BitmapWriter bmp(imgW, imgH);
    bmp.fillBackground(255, 255, 255);
    
    // --- Build the point list to render ---
    // If audioIndices is non-empty, use only those points; otherwise use all pts
    std::vector<const MeasurementPoint*> renderPts;
    if (!audioIndices.empty()) {
        for (size_t idx : audioIndices) {
            if (idx < pts.size()) {
                renderPts.push_back(&pts[idx]);
            }
        }
    } else {
        for (auto& p : pts) {
            renderPts.push_back(&p);
        }
    }
    
    if (renderPts.empty()) {
        err = "No valid data points to render";
        return false;
    }
    
    // --- Determine min/max per active curve ---
    struct CurveRange { double minVal, maxVal; };
    CurveRange ranges[5];
    for (int c = 0; c < 5; c++) {
        ranges[c].minVal =  std::numeric_limits<double>::max();
        ranges[c].maxVal =  std::numeric_limits<double>::lowest();
    }
    for (auto* pt : renderPts) {
        for (int c = 0; c < 5; c++) {
            if (!curveFlags[c]) continue;
            double v = getCurveValue(*pt, c);
            if (v < ranges[c].minVal) ranges[c].minVal = v;
            if (v > ranges[c].maxVal) ranges[c].maxVal = v;
        }
    }
    // Add 5% padding to ranges
    for (int c = 0; c < 5; c++) {
        if (!curveFlags[c]) continue;
        double span = ranges[c].maxVal - ranges[c].minVal;
        if (span < 1e-9) span = 1.0; // avoid zero-span
        ranges[c].minVal -= span * 0.05;
        ranges[c].maxVal += span * 0.05;
    }
    
    // --- Draw coordinate grid ---
    // Grid lines (light gray)
    const int gridLinesH = 5;
    const int gridLinesV = 8;
    for (int i = 0; i <= gridLinesH; i++) {
        int y = graphTop + (i * graphH) / gridLinesH;
        for (int x = graphLeft; x <= graphRight; x += 3) { // dashed
            bmp.setPixel(x, y, 200, 200, 200);
        }
    }
    for (int i = 0; i <= gridLinesV; i++) {
        int x = graphLeft + (i * graphW) / gridLinesV;
        for (int y = graphTop; y <= graphBottom; y += 3) { // dashed
            bmp.setPixel(x, y, 200, 200, 200);
        }
    }
    
    // Graph border (dark gray)
    bmp.drawRect(graphLeft, graphTop, graphW + 1, graphH + 1, 80, 80, 80, false);
    
    // --- Draw curves ---
    for (int c = 0; c < 5; c++) {
        if (!curveFlags[c]) continue;
        double span = ranges[c].maxVal - ranges[c].minVal;
        
        int prevX = -1, prevY = -1;
        for (size_t i = 0; i < renderPts.size(); i++) {
            double val = getCurveValue(*renderPts[i], c);
            double normalized = (val - ranges[c].minVal) / span;
            
            double normalizedX = static_cast<double>(i) / std::max(1.0, static_cast<double>(renderPts.size() - 1));
            int px = graphLeft + static_cast<int>(normalizedX * graphW);
            int py = graphBottom - static_cast<int>(normalized * graphH);
            
            // Clamp to graph area
            py = std::max(graphTop, std::min(graphBottom, py));
            
            if (prevX >= 0 && prevY >= 0) {
                bmp.drawLine(prevX, prevY, px, py,
                             CURVE_COLORS[c].r, CURVE_COLORS[c].g, CURVE_COLORS[c].b);
            }
            prevX = px;
            prevY = py;
        }
    }
    
    // --- Title ---
    bmp.drawText(graphLeft, 10, "Acoustic Curve Visualization", 0, 0, 0);
    std::string freqRange = formatFrequency(startFreq) + " - " + formatFrequency(endFreq);
    bmp.drawText(graphLeft, 24, freqRange, 80, 80, 80);
    
    // --- X-axis labels (frequency) ---
    for (int i = 0; i <= gridLinesV; i++) {
        int x = graphLeft + (i * graphW) / gridLinesV;
        uint64_t freq = startFreq + static_cast<uint64_t>((static_cast<double>(i) / gridLinesV) * (endFreq - startFreq));
        std::string label = formatFrequency(freq);
        // Center the label
        int textX = x - static_cast<int>(label.size()) * 4;
        bmp.drawText(textX, graphBottom + 8, label, 0, 0, 0);
    }
    
    // --- Y-axis labels (use first active curve's range) ---
    int primaryCurve = -1;
    for (int c = 0; c < 5; c++) {
        if (curveFlags[c]) { primaryCurve = c; break; }
    }
    if (primaryCurve >= 0) {
        double span = ranges[primaryCurve].maxVal - ranges[primaryCurve].minVal;
        for (int i = 0; i <= gridLinesH; i++) {
            int y = graphTop + (i * graphH) / gridLinesH;
            double val = ranges[primaryCurve].maxVal - (static_cast<double>(i) / gridLinesH) * span;
            std::string label = formatAxisValue(val);
            int textX = graphLeft - static_cast<int>(label.size()) * 8 - 4;
            if (textX < 2) textX = 2;
            bmp.drawText(textX, y - 4, label, 0, 0, 0);
        }
    }
    
    // --- Legend ---
    int legendY = imgH - legendHeight - 10;
    int legendX = graphLeft;
    for (int c = 0; c < 5; c++) {
        if (!curveFlags[c]) continue;
        // Color box
        bmp.drawRect(legendX, legendY, 12, 10, CURVE_COLORS[c].r, CURVE_COLORS[c].g, CURVE_COLORS[c].b, true);
        bmp.drawText(legendX + 16, legendY + 1, CURVE_COLORS[c].name, 0, 0, 0);
        legendX += 16 + static_cast<int>(std::string(CURVE_COLORS[c].name).size()) * 8 + 16;
    }
    
    // --- Save ---
    return bmp.saveBMP(generatedFilename, err);
}

// ============================================================
// Bitmap Export: Braille Preview as Image
// ============================================================

// Braille-specific curve pattern definitions (in mm along arc-length):
// Each pattern is {draw_mm, gap_mm, draw_mm, gap_mm, ...}
// These represent tactile patterns for differentiation by touch
static const std::vector<std::vector<double>> BRAILLE_PATTERNS = {
    {},                       // Curve 0 (SWR): solid line (no gaps)
    {3.0, 1.5},              // Curve 1 (RL): long dash  ——— ——— ———
    {1.5, 1.0},              // Curve 2 (|Z|): short dash  —— —— ——
    {0.6, 1.0},              // Curve 3 (X): dots  · · · ·
    {2.0, 0.8, 0.6, 0.8},   // Curve 4 (Phase): dash-dot  —·—·—·
};

// Curve colors for braille bitmap (high-contrast, distinct)
static const struct { uint8_t r, g, b; const char* name; } BRAILLE_CURVE_COLORS[5] = {
    {  0,   0,   0, "SWR"},        // Black (solid)
    {  0,   0, 180, "RL (dB)"},    // Dark blue (long dash)
    {180,   0,   0, "|Z| (Ohm)"},  // Dark red (short dash)
    {  0, 140,   0, "X (Ohm)"},    // Dark green (dotted)
    {140,   0, 140, "Phase (deg)"} // Purple (dash-dot)
};

bool ExportModule::exportBrailleBitmap(
    const std::vector<MeasurementPoint>& pts,
    const std::vector<size_t>& audioIndices,
    uint64_t startFreq, uint64_t endFreq, uint64_t step,
    const bool curveFlags[5],
    const AppConfig& config,
    std::string& generatedFilename, std::string& err)
{
    if (pts.empty()) {
        err = "No data to export";
        return false;
    }
    
    bool anyCurveSelected = false;
    for (int i = 0; i < 5; i++) {
        if (curveFlags[i]) { anyCurveSelected = true; break; }
    }
    if (!anyCurveSelected) {
        err = "No curves selected for export";
        return false;
    }
    
    generatedFilename = generateFilename("_braille.bmp", startFreq, endFreq, step);
    
    // Get paper dimensions for sizing
    PaperDimensions paper = BraillePrinter::getPaperDimensions(
        config.braille_paper_size, config.braille_orientation);
    
    const int pxPerMm = config.bitmap_braille_px_per_mm;
    const int borderPx = 20;
    
    // Compute layout in mm, then convert to pixels
    double clampedW = std::min(paper.width_mm, 500.0);
    double clampedH = std::min(paper.height_mm, 500.0);
    int paperW = static_cast<int>(clampedW * pxPerMm);
    int paperH = static_cast<int>(clampedH * pxPerMm);
    int imgW = paperW + 2 * borderPx;
    int imgH = paperH + 2 * borderPx + 60; // +60 for title + legend
    
    BitmapWriter bmp(imgW, imgH);
    bmp.fillBackground(220, 220, 220);
    
    // Paper area (white)
    int paperX0 = borderPx;
    int paperY0 = borderPx + 30;
    bmp.drawRect(paperX0, paperY0, paperW, paperH, 255, 255, 255, true);
    bmp.drawRect(paperX0, paperY0, paperW, paperH, 150, 150, 150, false);
    
    // Title
    bmp.drawText(borderPx, 8, "Braille Preview (Tactile Patterns)", 0, 0, 0);
    std::string paperStr = std::to_string(static_cast<int>(paper.width_mm)) + "x"
                         + std::to_string(static_cast<int>(paper.height_mm)) + " mm";
    bmp.drawText(borderPx, 20, paperStr, 80, 80, 80);
    
    // Graph area within the paper (matching braille_printer V5 layout)
    double graphWidthMm, graphHeightMm;
    if (config.braille_orientation == AppConfig::BrailleOrientation::LANDSCAPE) {
        graphWidthMm = clampedW * config.braille_graph_width_percent_landscape;
        graphHeightMm = clampedH * config.braille_graph_height_percent_landscape;
    } else {
        graphWidthMm = clampedW * config.braille_graph_width_percent_portrait;
        graphHeightMm = clampedH * config.braille_graph_height_percent_portrait;
    }
    double originXMm = config.braille_origin_x_mm + config.braille_y_axis_space_mm;
    double originYMm = config.braille_origin_y_mm;
    
    // Convert graph area to pixels (relative to paper origin)
    int graphLeft = paperX0 + static_cast<int>(originXMm * pxPerMm);
    int graphTop = paperY0 + static_cast<int>(originYMm * pxPerMm);
    int graphW = static_cast<int>(graphWidthMm * pxPerMm);
    int graphH = static_cast<int>(graphHeightMm * pxPerMm);
    int graphRight = graphLeft + graphW;
    int graphBottom = graphTop + graphH;
    
    if (graphW < 20 || graphH < 20) {
        err = "Graph area too small for rendering";
        return false;
    }
    
    // --- Draw coordinate grid (subtle, reduced intensity) ---
    if (config.braille_coordinate_grid != AppConfig::BrailleCoordinateGrid::NONE) {
        int gridDotRadius = std::max(1, pxPerMm / 5);  // Small: ~0.2mm radius
        uint8_t gridGray = 210;  // Very light gray — subtle, won't compete with curves
        
        if (config.braille_coordinate_grid == AppConfig::BrailleCoordinateGrid::DOTS) {
            for (int gx = 0; gx <= 10; gx++) {
                for (int gy = 0; gy <= 10; gy++) {
                    int px = graphLeft + (gx * graphW) / 10;
                    int py = graphTop + (gy * graphH) / 10;
                    bmp.drawCircle(px, py, gridDotRadius, gridGray, gridGray, gridGray, true);
                }
            }
        } else if (config.braille_coordinate_grid == AppConfig::BrailleCoordinateGrid::GRID_LINES) {
            for (int gx = 0; gx <= 10; gx++) {
                int px = graphLeft + (gx * graphW) / 10;
                for (int py = graphTop; py <= graphBottom; py += 4) {
                    bmp.setPixel(px, py, gridGray, gridGray, gridGray);
                }
            }
            for (int gy = 0; gy <= 10; gy++) {
                int py = graphTop + (gy * graphH) / 10;
                for (int px = graphLeft; px <= graphRight; px += 4) {
                    bmp.setPixel(px, py, gridGray, gridGray, gridGray);
                }
            }
        }
    }
    
    // --- Draw axes (thin, dark gray) ---
    // X-axis (bottom)
    bmp.drawLine(graphLeft, graphBottom, graphRight, graphBottom, 100, 100, 100);
    // Y-axis (left)
    bmp.drawLine(graphLeft, graphTop, graphLeft, graphBottom, 100, 100, 100);
    
    // --- Build the point list ---
    std::vector<const MeasurementPoint*> renderPts;
    if (!audioIndices.empty()) {
        for (size_t idx : audioIndices) {
            if (idx < pts.size()) renderPts.push_back(&pts[idx]);
        }
    } else {
        for (auto& p : pts) renderPts.push_back(&p);
    }
    if (renderPts.empty()) {
        err = "No valid data points to render";
        return false;
    }
    
    // --- Determine min/max per active curve ---
    struct CurveRange { double minVal, maxVal; };
    CurveRange ranges[5];
    for (int c = 0; c < 5; c++) {
        ranges[c].minVal = std::numeric_limits<double>::max();
        ranges[c].maxVal = std::numeric_limits<double>::lowest();
    }
    for (auto* pt : renderPts) {
        for (int c = 0; c < 5; c++) {
            if (!curveFlags[c]) continue;
            double v = getCurveValue(*pt, c);
            if (v < ranges[c].minVal) ranges[c].minVal = v;
            if (v > ranges[c].maxVal) ranges[c].maxVal = v;
        }
    }
    for (int c = 0; c < 5; c++) {
        if (!curveFlags[c]) continue;
        double span = ranges[c].maxVal - ranges[c].minVal;
        if (span < 1e-9) span = 1.0;
        ranges[c].minVal -= span * 0.05;
        ranges[c].maxVal += span * 0.05;
    }
    
    // --- Tactile line rendering parameters ---
    // Line thickness in pixels (represents tactile dot diameter, ~0.5mm)
    int lineThickness = std::max(2, static_cast<int>(0.5 * pxPerMm));
    // Dot radius for dotted patterns
    int dotRadius = std::max(2, static_cast<int>(0.4 * pxPerMm));
    
    // --- Draw each curve with arc-length-parametrized dash patterns ---
    for (int c = 0; c < 5; c++) {
        if (!curveFlags[c]) continue;
        double span = ranges[c].maxVal - ranges[c].minVal;
        
        // Compute pixel coordinates for all data points
        struct PixelPt { double x, y; };
        std::vector<PixelPt> pixelCoords;
        pixelCoords.reserve(renderPts.size());
        
        for (size_t i = 0; i < renderPts.size(); i++) {
            double val = getCurveValue(*renderPts[i], c);
            double normX = static_cast<double>(i) / std::max(1.0, static_cast<double>(renderPts.size() - 1));
            double normY = (val - ranges[c].minVal) / span;
            
            double px = graphLeft + normX * graphW;
            double py = graphBottom - normY * graphH;
            // Clamp to graph area
            py = std::max(static_cast<double>(graphTop), std::min(static_cast<double>(graphBottom), py));
            pixelCoords.push_back({px, py});
        }
        
        if (pixelCoords.size() < 2) continue;
        
        uint8_t cr = BRAILLE_CURVE_COLORS[c].r;
        uint8_t cg = BRAILLE_CURVE_COLORS[c].g;
        uint8_t cb = BRAILLE_CURVE_COLORS[c].b;
        
        const auto& pattern = (c >= 0 && c < static_cast<int>(BRAILLE_PATTERNS.size())) ? BRAILLE_PATTERNS[c] : BRAILLE_PATTERNS[0];
        
        if (pattern.empty()) {
            // Solid line — draw thick connected line segments
            for (size_t i = 1; i < pixelCoords.size(); i++) {
                int x1 = static_cast<int>(pixelCoords[i-1].x);
                int y1 = static_cast<int>(pixelCoords[i-1].y);
                int x2 = static_cast<int>(pixelCoords[i].x);
                int y2 = static_cast<int>(pixelCoords[i].y);
                bmp.drawThickLine(x1, y1, x2, y2, lineThickness, cr, cg, cb);
            }
        } else {
            // Dashed/dotted pattern — arc-length parametrized
            // Walk along the polyline, tracking cumulative arc length in mm
            // Pattern is defined in mm: {draw_mm, gap_mm, draw_mm, gap_mm, ...}
            double mmPerPx = 1.0 / pxPerMm;
            size_t patIdx = 0;      // current position in pattern array
            bool drawing = true;    // start in "draw" state
            double patRemaining = pattern[0]; // mm remaining in current pattern element
            
            // Determine if this is a dot pattern (short draw segments < 0.8mm)
            bool isDotPattern = (pattern[0] < 0.8);
            
            for (size_t i = 1; i < pixelCoords.size(); i++) {
                double x1 = pixelCoords[i-1].x;
                double y1 = pixelCoords[i-1].y;
                double x2 = pixelCoords[i].x;
                double y2 = pixelCoords[i].y;
                
                double dx = x2 - x1;
                double dy = y2 - y1;
                double segLenPx = std::sqrt(dx * dx + dy * dy);
                double segLenMm = segLenPx * mmPerPx;
                
                if (segLenPx < 0.5) continue; // skip degenerate segments
                
                // Walk along this segment
                double tStart = 0.0; // parametric position on this segment [0,1]
                double segConsumedMm = 0.0;
                
                while (segConsumedMm < segLenMm - 0.001) {
                    double mmToConsume = std::min(patRemaining, segLenMm - segConsumedMm);
                    double tEnd = tStart + (mmToConsume / segLenMm);
                    if (tEnd > 1.0) tEnd = 1.0;
                    
                    if (drawing) {
                        int sx = static_cast<int>(x1 + tStart * dx);
                        int sy = static_cast<int>(y1 + tStart * dy);
                        int ex = static_cast<int>(x1 + tEnd * dx);
                        int ey = static_cast<int>(y1 + tEnd * dy);
                        
                        if (isDotPattern) {
                            // For dot patterns, draw filled circles at intervals
                            int mx = (sx + ex) / 2;
                            int my = (sy + ey) / 2;
                            bmp.drawCircle(mx, my, dotRadius, cr, cg, cb, true);
                        } else {
                            // For dash patterns, draw thick line segments
                            bmp.drawThickLine(sx, sy, ex, ey, lineThickness, cr, cg, cb);
                        }
                    }
                    
                    segConsumedMm += mmToConsume;
                    patRemaining -= mmToConsume;
                    tStart = tEnd;
                    
                    if (patRemaining <= 0.001) {
                        // Move to next pattern element
                        patIdx = (patIdx + 1) % pattern.size();
                        drawing = !drawing;
                        patRemaining = pattern[patIdx];
                    }
                }
            }
        }
    }
    
    // --- Legend at bottom ---
    int legendY = imgH - 25;
    int legendX = borderPx;
    for (int c = 0; c < 5; c++) {
        if (!curveFlags[c]) continue;
        // Color box with pattern hint
        bmp.drawRect(legendX, legendY, 16, 10,
                     BRAILLE_CURVE_COLORS[c].r, BRAILLE_CURVE_COLORS[c].g, BRAILLE_CURVE_COLORS[c].b, true);
        bmp.drawText(legendX + 20, legendY + 1, BRAILLE_CURVE_COLORS[c].name, 0, 0, 0);
        legendX += 20 + static_cast<int>(std::string(BRAILLE_CURVE_COLORS[c].name).size()) * 8 + 16;
    }
    
    // --- X-axis labels ---
    for (int i = 0; i <= 4; i++) {
        int x = graphLeft + (i * graphW) / 4;
        uint64_t freq = startFreq + static_cast<uint64_t>((static_cast<double>(i) / 4) * (endFreq - startFreq));
        std::string label = formatFrequency(freq);
        int textX = x - static_cast<int>(label.size()) * 4;
        bmp.drawText(textX, graphBottom + 4, label, 60, 60, 60);
    }
    
    return bmp.saveBMP(generatedFilename, err);
}
