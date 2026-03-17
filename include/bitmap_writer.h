#pragma once
#include <vector>
#include <string>
#include <cstdint>

/**
 * Platform-independent 24-bit BMP writer.
 * Pure C++17 implementation — no external libraries required.
 * Produces uncompressed Windows Bitmap files (BMP) that can be
 * opened on any operating system (Windows, macOS, Linux).
 *
 * BMP format: 14-byte file header + 40-byte DIB header + pixel data (BGR, row-padded to 4 bytes).
 */
class BitmapWriter {
public:
    BitmapWriter(int width, int height);

    // Pixel operations
    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
    void fillBackground(uint8_t r, uint8_t g, uint8_t b);

    // Drawing primitives (Bresenham line, midpoint circle)
    void drawLine(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b);
    void drawThickLine(int x1, int y1, int x2, int y2, int thickness, uint8_t r, uint8_t g, uint8_t b);
    void drawCircle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b, bool filled = true);
    void drawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, bool filled = false);

    // Simple 8×8 pixel font text rendering
    void drawText(int x, int y, const std::string& text, uint8_t r, uint8_t g, uint8_t b);

    // Save as uncompressed 24-bit BMP file
    bool saveBMP(const std::string& filename, std::string& err) const;

    // Accessors
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

private:
    int width_;
    int height_;
    std::vector<uint8_t> pixels_;  // RGB, 3 bytes per pixel, row-major (top-to-bottom)

    // Internal helpers
    void drawHorizontalLine(int x1, int x2, int y, uint8_t r, uint8_t g, uint8_t b);
    static void renderGlyph(const uint8_t glyph[8], int startX, int startY,
                            uint8_t r, uint8_t g, uint8_t b,
                            BitmapWriter& bmp);
    static const uint8_t FONT_8X8[][8];
};
