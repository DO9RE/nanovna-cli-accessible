#ifdef WITH_HAM_SPIRIT

#include "hamspirit_game.h"
#include "game_authority.h"
#include "acoustic_analyzer.h"
#include "synthesizer_engine.h"
#include "platform/interface/console_input.h"
#include "platform/interface/audio_backend.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <limits>
#include <sstream>
#include <atomic>
#include <array>
#include <cctype>
#include <cstring>
#include <condition_variable>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#endif

#ifdef log
#undef log
#endif

#ifndef _WIN32
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
// Forward declarations for functions defined in platform/macos/hamspirit_gui_macos.mm.
// Must be at file scope (external linkage) so the linker can resolve them.
bool startHamSpiritWindow();
void stopHamSpiritWindow();
void pushBannerText(const std::string& text);
void updateMenuOverlay(const std::string& title,
                       const std::vector<std::string>& items,
                       int selectedIndex);
void hideMenuOverlay();
void showTextOverlay(const std::string& text);
void hideTextOverlay();
void showTextInputOverlay(const std::string& label, const std::string& value);
void hideTextInputOverlay();
// Processes all pending AppKit events — must be called periodically from the
// game thread to prevent the NSWindow from becoming "not responding".
void pumpHamSpiritEvents();
// Poll a key event from the GUI window's key queue.
// Returns true if an event was dequeued.
bool pollHamSpiritKeyEvent(int& outKeyCode, bool& outPressed);
#endif // HAVE_NATIVE_GUI && __APPLE__
#endif // !_WIN32

namespace {
constexpr float kRefImpedanceOhms = 50.0f;
constexpr float kFeasibilityStepL = 10.0f;  // µH coarse step for feasibility scan
constexpr float kFeasibilityStepC = 100.0f; // pF coarse step for feasibility scan
constexpr float kFeasibilityTargetSWR = 2.0f; // Acceptable coarse target for early exit
constexpr float kFeasibilityWarnSWR = 5.0f;
constexpr float kMinCapacitancePF = 0.01f;
constexpr float kMinInductanceUH = 0.01f;
constexpr float kMaxGammaMag = 0.818f;  // Corresponds to SWR 10:1 — physical limit for the game
constexpr float kMinSWR = 1.0f;
constexpr float kMaxSWR = 10.0f;       // Realistic max: modern PAs fold back at 3:1, auto-tuners stop at 10:1
constexpr float kMinResistanceOhms = 0.1f;
constexpr float kMinComplexDenominator = 1e-10f;
constexpr float kNoisePanDebugThrottleSeconds = 1.0f;
constexpr float kKeyDebugThrottleSeconds = 1.0f;
constexpr float kConsoleFocusDebugThrottleSeconds = 1.0f;
constexpr float kInitialDebugThrottleTime = -1e6f; // Time offset far in past (seconds) for immediate logs
constexpr float kMinBandwidthForPan = 0.3f;
constexpr int kMaxMathWarningsPerCounter = 5;
constexpr int kMaxWindowTitleLength = 256;
constexpr int kMaxWindowClassLength = 128;
static_assert(kMaxWindowTitleLength > 0, "Window title length must be positive");
static_assert(kMaxWindowClassLength > 0, "Window class length must be positive");
const char* getTTSEngineLabel(TTSEngineType type) {
    switch (type) {
        case TTSEngineType::NVDA:            return "NVDA";
        case TTSEngineType::WINDOWS_SAPI:    return "Windows SAPI";
        case TTSEngineType::MACOS_SAY:       return "macOS Say";
        case TTSEngineType::MACOS_VOICEOVER: return "VoiceOver";
        case TTSEngineType::ESPEAK_NG:       return "espeak-NG";
        default:                             return "Unknown";
    }
}

const char* getTTSEngineConfigValue(TTSEngineType type) {
    switch (type) {
        case TTSEngineType::NVDA:            return "nvda";
        case TTSEngineType::WINDOWS_SAPI:    return "windows";
        case TTSEngineType::MACOS_SAY:       return "macos_say";
        case TTSEngineType::MACOS_VOICEOVER: return "macos_voiceover";
        case TTSEngineType::ESPEAK_NG:       return "espeak_ng";
        default:                             return "windows";
    }
}

#ifndef _WIN32
/**
 * Convert LogicalKey codes (from console input) to VK-style codes
 * used by KeyboardEmulatorMapping and the keyboard emulator.
 * This enables the same key mapping configuration to work across platforms.
 */
static int logicalKeyToVK(int logicalKey) {
    switch (logicalKey) {
        case KEY_LEFT:      return 0x25;  // VK_LEFT
        case KEY_UP:        return 0x26;  // VK_UP
        case KEY_RIGHT:     return 0x27;  // VK_RIGHT
        case KEY_DOWN:      return 0x28;  // VK_DOWN
        case KEY_ESCAPE:    return 0x1B;  // VK_ESCAPE
        case KEY_ENTER:     return 0x0D;  // VK_RETURN
        case KEY_TAB:       return 0x09;  // VK_TAB
        case KEY_BACKSPACE: return 0x08;  // VK_BACK
        case KEY_F1:        return 0x70;  // VK_F1
        case KEY_F2:        return 0x71;
        case KEY_F3:        return 0x72;
        case KEY_F4:        return 0x73;
        case KEY_F5:        return 0x74;
        case KEY_F6:        return 0x75;
        case KEY_F7:        return 0x76;
        case KEY_F8:        return 0x77;
        case KEY_F9:        return 0x78;
        case KEY_F10:       return 0x79;
        case KEY_F11:       return 0x7A;
        case KEY_F12:       return 0x7B;
        case KEY_DELETE:    return 0x2E;  // VK_DELETE
        case KEY_HOME:      return 0x24;  // VK_HOME
        case KEY_END:       return 0x23;  // VK_END
        default:
            // Lowercase letters → uppercase (VK codes use uppercase)
            if (logicalKey >= 'a' && logicalKey <= 'z') {
                return logicalKey - 'a' + 'A';
            }
            // Printable ASCII (space, digits, uppercase letters) pass through
            return logicalKey;
    }
}
#endif
std::atomic<Logger*> sHamSpiritMathLogger{nullptr};
std::atomic<int> sSWRMathWarningCount{0};
std::atomic<int> sTunerMathWarningCount{0};

bool tryLogMathWarning(std::atomic<int>& counter) {
    int current = counter.load();
    while (current < kMaxMathWarningsPerCounter) {
        if (counter.compare_exchange_weak(current, current + 1)) {
            return true;
        }
    }
    return false;
}

void logHamSpiritMath(const std::string& message) {
    Logger* logger = sHamSpiritMathLogger.load();
    if (logger) {
        logger->log("HAMSPIRIT_MATH", message);
    }
}

#ifdef _WIN32
std::string describeWindow(HWND hwnd) {
    if (!hwnd) {
        return "none";
    }
    std::array<char, kMaxWindowTitleLength> title{};
    std::array<char, kMaxWindowClassLength> className{};
    SetLastError(0);
    int titleLen = GetWindowTextA(hwnd, title.data(), kMaxWindowTitleLength);
    DWORD titleError = (titleLen == 0) ? GetLastError() : 0;
    SetLastError(0);
    int classLen = GetClassNameA(hwnd, className.data(), kMaxWindowClassLength);
    DWORD classError = (classLen == 0) ? GetLastError() : 0;
    std::ostringstream oss;
    if (titleLen > 0) {
        oss << "\"" << title.data() << "\"";
    } else if (classLen > 0) {
        oss << "class=" << className.data();
    } else if (titleError != 0 || classError != 0) {
        oss << "unknown (title error=0x" << std::hex << titleError
            << ", class error=0x" << classError << ")";
    } else {
        oss << "unknown";
    }
    return oss.str();
}

// Track visualization overlay data — shared between game thread and window thread.
// The game thread writes sample points each frame; WM_PAINT reads them to draw
// two curved track-boundary bars, a car rectangle, and game objects on the track.
static constexpr int TRACK_VIS_SAMPLES = 20;  // Number of look-ahead samples
static constexpr int TRACK_VIS_MAX_OBJECTS = 16;  // Max objects to render simultaneously

// Visual placeholder for any game object on the track.
// Position is given as fractional distance ahead (0 = at player, 1 = horizon)
// and lateral offset (-1 = left edge, +1 = right edge).
enum class TrackObjKind : uint8_t {
    NOISE_ENEMY = 0,    // Red bar spanning bandwidth
    MORSE_SIGNAL,       // Green diamond with character label
    POWER_UP,           // Gold circle with type initial (placeholder for PNG sprite)
    QSO_STOERER,        // Purple triangle (intruder)
};

struct TrackVisObject {
    TrackObjKind kind{TrackObjKind::NOISE_ENEMY};
    float ahead{0.0f};       // 0..1 how far ahead on screen (0 = bottom, 1 = top)
    float lateral{0.0f};     // -1..+1 lateral position on track
    float sizeParam{0.0f};   // Kind-specific size (bandwidth for noise, quality for powerup)
    float healthFrac{1.0f};  // Health fraction 0..1 (1=full, 0=nearly dead). For shootable objects.
    int  intParam{0};        // Kind-specific int (powerUpType for PU, ASCII char for morse)
    bool highlight{false};   // True if currently targeted / in collection
};

struct TrackVisualData {
    // Each sample represents a point ahead on the track (index 0 = player, last = furthest ahead).
    // curvature[i] stores the SWR-based lateral shift at that sample point.
    // Positive = track curves right, negative = track curves left.
    float curvature[TRACK_VIS_SAMPLES]{};
    // swrValue[i]: the adjusted SWR at each sample (drives boundary bar color).
    // 1.0 = perfect match (green), 2-3 = moderate (yellow), 3+ = poor (red/blue).
    float swrValue[TRACK_VIS_SAMPLES]{};
    // reactance[i]: reactance at each sample.
    // Positive = inductive (bar tints red), negative = capacitive (bar tints blue),
    // near zero with good SWR = well matched (bar is green).
    float reactance[TRACK_VIS_SAMPLES]{};
    float playerLateral{0.0f};  // Player's lateral offset (-1..+1)
    float aimAngle{0.0f};       // Weapon aim angle in radians (-π..+π), 0 = straight ahead
    bool active{false};         // True during PLAYING state
    // Game objects on the track (up to TRACK_VIS_MAX_OBJECTS)
    TrackVisObject objects[TRACK_VIS_MAX_OBJECTS]{};
    int objectCount{0};
    // HUD status bar text (same content as braille display, for sighted users)
    char statusText[256]{};
    // Scrolling TTS banner text (Bandwacht, Intruder Monitoring, Verkehrsservice announcements)
    char bannerText[512]{};
    float bannerScrollX{0.0f};       // Current scroll position (pixels from right edge)
    bool bannerActive{false};        // True while a banner message is scrolling
};

// Menu overlay data — shared between game thread and window thread.
// The game thread writes menu state; WM_PAINT reads to draw menu items
// over the wallpaper image with retro-style highlighting.
static constexpr int MENU_MAX_ITEMS = 12;
static constexpr int MENU_ITEM_TEXT_LEN = 80;
static constexpr int MENU_TITLE_LEN = 128;

struct MenuOverlayData {
    bool visible{false};
    int selectedIndex{0};
    int itemCount{0};
    char items[MENU_MAX_ITEMS][MENU_ITEM_TEXT_LEN]{};
    char title[MENU_TITLE_LEN]{};
};

// Text overlay — shows intro/controls text centered on screen (over wallpaper).
static constexpr int TEXT_OVERLAY_MAX_LEN = 512;

struct TextOverlayData {
    bool visible{false};
    char text[TEXT_OVERLAY_MAX_LEN]{};
};

// Text input overlay — shows a labeled input field with cursor.
static constexpr int TEXT_INPUT_LABEL_LEN = 128;
static constexpr int TEXT_INPUT_VALUE_LEN = 64;

struct TextInputOverlayData {
    bool visible{false};
    char label[TEXT_INPUT_LABEL_LEN]{};
    char value[TEXT_INPUT_VALUE_LEN]{};
};

struct HamSpiritWindowState {
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> ready{false};
    HWND hwnd{nullptr};
    HWND consoleHwnd{nullptr};
    bool consoleHidden{false};
    LONG_PTR originalConsoleExStyle{0};
    std::mutex mutex;
    std::condition_variable readyCv;
    DWORD threadId{0};
    // Wallpaper images (GDI+)
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken{0};
    Gdiplus::Image* imgTitle{nullptr};    // HamSpirit-1.PNG — title screen
    Gdiplus::Image* imgMenu{nullptr};     // HamSpirit-2.PNG — menu / intermissions
    Gdiplus::Image* imgRacing{nullptr};   // HamSpirit-3.PNG — gameplay
    std::atomic<int> currentImage{0};     // 0=none, 1=title, 2=menu, 3=racing
    // Track visualization overlay (written by game thread, read by WM_PAINT)
    std::mutex trackVisMtx;
    TrackVisualData trackVis;
    // Per-player track visualization for multiplayer split-screen.
    // Index 0 = player 0 (mirrors trackVis), indices 1..3 = additional players.
    TrackVisualData playerTrackVis[4];
    int multiplayerCount{1};      // 1 = singleplayer, 2-4 = multiplayer
    int splitOrientation{0};      // 0 = HORIZONTAL (side-by-side), 1 = VERTICAL (top-bottom)
    // Menu overlay (written by game thread, read by WM_PAINT)
    MenuOverlayData menuOverlay;
    // Text overlay (intro/controls text shown centered on screen)
    TextOverlayData textOverlay;
    // Text input overlay (callsign/name entry with visible text box)
    TextInputOverlayData textInputOverlay;
    // Scrolling banner state (managed by WM_PAINT/WM_TIMER in the GUI thread)
    float bannerPixelOffset{0.0f};   // Current X offset for scrolling animation
    // TODO: Replace car rectangle with a car sprite image loaded here
    // Gdiplus::Image* imgCar{nullptr};
};

HamSpiritWindowState sHamSpiritWindow;

// Push a text message to the scrolling banner overlay (thread-safe).
// Called from game thread for Bandwacht, Intruder Monitoring, and Verkehrsservice.
void pushBannerText(const std::string& text) {
    std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
    std::strncpy(sHamSpiritWindow.trackVis.bannerText, text.c_str(),
                 sizeof(sHamSpiritWindow.trackVis.bannerText) - 1);
    sHamSpiritWindow.trackVis.bannerText[sizeof(sHamSpiritWindow.trackVis.bannerText) - 1] = '\0';
    sHamSpiritWindow.trackVis.bannerActive = true;
    sHamSpiritWindow.trackVis.bannerScrollX = 0.0f;  // Reset scroll to start from right edge
    sHamSpiritWindow.bannerPixelOffset = 0.0f;
}

// Update the menu overlay displayed on the GUI window (thread-safe).
// Called from game thread when menu state changes (main menu, pause menu, config).
void updateMenuOverlay(const std::string& title,
                       const std::vector<std::string>& items,
                       int selectedIndex) {
    std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
    auto& mo = sHamSpiritWindow.menuOverlay;
    mo.visible = true;
    mo.selectedIndex = selectedIndex;
    mo.itemCount = std::min(static_cast<int>(items.size()), MENU_MAX_ITEMS);
    std::strncpy(mo.title, title.c_str(), MENU_TITLE_LEN - 1);
    mo.title[MENU_TITLE_LEN - 1] = '\0';
    for (int i = 0; i < mo.itemCount; i++) {
        std::strncpy(mo.items[i], items[i].c_str(), MENU_ITEM_TEXT_LEN - 1);
        mo.items[i][MENU_ITEM_TEXT_LEN - 1] = '\0';
    }
    // Trigger a repaint so the menu is immediately visible
    if (sHamSpiritWindow.hwnd) {
        InvalidateRect(sHamSpiritWindow.hwnd, nullptr, FALSE);
    }
}

// Hide the menu overlay (thread-safe).
void hideMenuOverlay() {
    std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
    sHamSpiritWindow.menuOverlay.visible = false;
    if (sHamSpiritWindow.hwnd) {
        InvalidateRect(sHamSpiritWindow.hwnd, nullptr, FALSE);
    }
}

// Show a centered text overlay on screen (for intro/controls narration).
void showTextOverlay(const std::string& text) {
    std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
    auto& to = sHamSpiritWindow.textOverlay;
    to.visible = true;
    std::strncpy(to.text, text.c_str(), TEXT_OVERLAY_MAX_LEN - 1);
    to.text[TEXT_OVERLAY_MAX_LEN - 1] = '\0';
    if (sHamSpiritWindow.hwnd) {
        InvalidateRect(sHamSpiritWindow.hwnd, nullptr, FALSE);
    }
}

// Hide the text overlay.
void hideTextOverlay() {
    std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
    sHamSpiritWindow.textOverlay.visible = false;
    if (sHamSpiritWindow.hwnd) {
        InvalidateRect(sHamSpiritWindow.hwnd, nullptr, FALSE);
    }
}

// Show a text input overlay (label + current input value).
void showTextInputOverlay(const std::string& label, const std::string& value) {
    std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
    auto& ti = sHamSpiritWindow.textInputOverlay;
    ti.visible = true;
    std::strncpy(ti.label, label.c_str(), TEXT_INPUT_LABEL_LEN - 1);
    ti.label[TEXT_INPUT_LABEL_LEN - 1] = '\0';
    std::strncpy(ti.value, value.c_str(), TEXT_INPUT_VALUE_LEN - 1);
    ti.value[TEXT_INPUT_VALUE_LEN - 1] = '\0';
    if (sHamSpiritWindow.hwnd) {
        InvalidateRect(sHamSpiritWindow.hwnd, nullptr, FALSE);
    }
}

// Hide the text input overlay.
void hideTextInputOverlay() {
    std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
    sHamSpiritWindow.textInputOverlay.visible = false;
    if (sHamSpiritWindow.hwnd) {
        InvalidateRect(sHamSpiritWindow.hwnd, nullptr, FALSE);
    }
}

void stopHamSpiritWindow();

struct HamSpiritWindowGuard {
    bool enabled;
    explicit HamSpiritWindowGuard(bool enabledValue) : enabled(enabledValue) {}
    HamSpiritWindowGuard(const HamSpiritWindowGuard&) = delete;
    HamSpiritWindowGuard& operator=(const HamSpiritWindowGuard&) = delete;
    ~HamSpiritWindowGuard() {
        if (enabled) {
            stopHamSpiritWindow();
        }
    }
};

LRESULT CALLBACK HamSpiritWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int w = clientRect.right - clientRect.left;
            int h = clientRect.bottom - clientRect.top;
            
            // Double-buffer to avoid flicker: draw into a memory DC, then blit
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
            
            // 1. Draw background wallpaper (or fallback)
            Gdiplus::Image* img = nullptr;
            int which = sHamSpiritWindow.currentImage.load();
            if (which == 1 && sHamSpiritWindow.imgTitle) img = sHamSpiritWindow.imgTitle;
            else if (which == 2 && sHamSpiritWindow.imgMenu) img = sHamSpiritWindow.imgMenu;
            else if (which == 3 && sHamSpiritWindow.imgRacing) img = sHamSpiritWindow.imgRacing;
            
            if (img) {
                Gdiplus::Graphics graphics(memDC);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(img, 0, 0, w, h);
            } else {
                HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(memDC, &clientRect, brush);
                DeleteObject(brush);
            }
            
            // 2. Track visualization overlay (during active gameplay)
            TrackVisualData vis;
            TrackVisualData mpVis[4];
            int mpCount = 1;
            int mpSplitOri = 0;
            {
                std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
                vis = sHamSpiritWindow.trackVis;
                mpCount = std::max(1, std::min(4, sHamSpiritWindow.multiplayerCount));
                mpSplitOri = sHamSpiritWindow.splitOrientation;
                for (int i = 0; i < mpCount; i++)
                    mpVis[i] = sHamSpiritWindow.playerTrackVis[i];
            }
            if (vis.active) {
                // --- Player 0 viewport ---
                // In split-screen mode, player 0 also gets a viewport (not full screen).
                // vx/vy/vw/vh define the rendering region for player 0.
                int vx = 0, vy = 0, vw = w, vh = h;
                HRGN p0ClipRgn = nullptr;
                if (mpCount > 1) {
                    if (mpCount == 2) {
                        if (mpSplitOri == 0) { vw = w / 2; }
                        else                 { vh = h / 2; }
                    } else if (mpCount == 3) {
                        if (mpSplitOri == 0) { vw = w / 3; }
                        else                 { vh = h / 3; }
                    } else { // 4 players: 2x2
                        vw = w / 2; vh = h / 2;
                    }
                    p0ClipRgn = CreateRectRgn(vx, vy, vx + vw, vy + vh);
                    SelectClipRgn(memDC, p0ClipRgn);
                }

                // --- Track boundary bars ---
                // The bars run vertically: bottom = player position, top = look-ahead.
                // Each sample shifts the bars horizontally based on accumulated SWR curvature.
                // Color per segment reflects impedance matching at that point:
                //   Green  = good SWR (well matched)
                //   Red    = inductive reactance (positive X)
                //   Blue   = capacitive reactance (negative X)
                //   Yellow = poor SWR with near-zero reactance (resistive mismatch)
                //
                // Track half-width in pixels: 35% of viewport width on each side of center
                const float trackHalfPx = vw * 0.35f;
                const float centerX = vx + vw * 0.5f;
                const int barWidth = std::max(4, vw / 60);  // Bar thickness
                
                // Helper: compute bar color from SWR and reactance at a sample
                auto segmentColor = [](float swr, float reactX) -> COLORREF {
                    // Good match: SWR < 1.5 → green
                    if (swr < 1.5f) {
                        // Blend from bright green (SWR=1) to slightly yellow-green (SWR=1.5)
                        float t = std::clamp((swr - 1.0f) / 0.5f, 0.0f, 1.0f);
                        return RGB(static_cast<int>(t * 180), static_cast<int>(220 - t * 40), 0);
                    }
                    // Poor match: color depends on reactance sign
                    float severity = std::clamp((swr - 1.5f) / 3.0f, 0.0f, 1.0f);
                    if (reactX > 5.0f) {
                        // Inductive → red. Brighter red with higher SWR.
                        return RGB(static_cast<int>(180 + 75 * severity), 
                                   static_cast<int>(80 * (1.0f - severity)), 0);
                    } else if (reactX < -5.0f) {
                        // Capacitive → blue. Brighter blue with higher SWR.
                        return RGB(0, static_cast<int>(80 * (1.0f - severity)),
                                   static_cast<int>(180 + 75 * severity));
                    } else {
                        // Resistive mismatch (near-zero reactance) → yellow/orange
                        return RGB(static_cast<int>(200 + 55 * severity),
                                   static_cast<int>(200 - 120 * severity), 0);
                    }
                };
                
                float cumulShift = 0.0f;
                float prevLeftX = centerX - trackHalfPx;
                float prevRightX = centerX + trackHalfPx;
                int prevY = vy + vh;  // Start at bottom of viewport
                
                for (int i = 0; i < TRACK_VIS_SAMPLES; i++) {
                    // Each sample covers one vertical strip of the viewport
                    int y = vy + vh - static_cast<int>((static_cast<float>(i + 1) / TRACK_VIS_SAMPLES) * vh * 0.85f);
                    
                    // Accumulate curvature shift (pixels). Scale so max SWR creates
                    // visible bending but the track doesn't go off-screen.
                    cumulShift += vis.curvature[i] * (vw * 0.015f);
                    cumulShift = std::clamp(cumulShift, -trackHalfPx * 0.8f, trackHalfPx * 0.8f);
                    
                    // Perspective narrowing: track gets narrower toward the top (horizon)
                    float perspFactor = 0.3f + 0.7f * (static_cast<float>(TRACK_VIS_SAMPLES - i) / TRACK_VIS_SAMPLES);
                    float halfW = trackHalfPx * perspFactor;
                    
                    float leftX = centerX + cumulShift - halfW;
                    float rightX = centerX + cumulShift + halfW;
                    
                    // Per-segment color based on impedance matching
                    COLORREF segColor = segmentColor(vis.swrValue[i], vis.reactance[i]);
                    HPEN segPen = CreatePen(PS_SOLID, barWidth, segColor);
                    HPEN oldPen = (HPEN)SelectObject(memDC, segPen);
                    
                    // Draw left boundary segment
                    MoveToEx(memDC, static_cast<int>(prevLeftX), prevY, nullptr);
                    LineTo(memDC, static_cast<int>(leftX), y);
                    
                    // Draw right boundary segment
                    MoveToEx(memDC, static_cast<int>(prevRightX), prevY, nullptr);
                    LineTo(memDC, static_cast<int>(rightX), y);
                    
                    SelectObject(memDC, oldPen);
                    DeleteObject(segPen);
                    
                    prevLeftX = leftX;
                    prevRightX = rightX;
                    prevY = y;
                }
                
                // --- Game objects on the track ---
                // Each object is positioned using its ahead (0=bottom, 1=top) and
                // lateral (-1..+1) values.  We interpolate the track center shift
                // and perspective width at the object's Y level from the curvature
                // samples already computed above.
                // A lambda computes (screenX, screenY, halfTrackW) for a given ahead/lateral.
                auto objScreenPos = [&](float ahead, float lateral,
                                        int& outX, int& outY, float& outHalfW) {
                    ahead = std::clamp(ahead, 0.0f, 1.0f);
                    float yFrac = ahead;  // 0..1
                    outY = vy + vh - static_cast<int>(yFrac * vh * 0.85f);
                    // Interpolate cumulative shift at this Y level
                    float sampleF = yFrac * (TRACK_VIS_SAMPLES - 1);
                    int si = static_cast<int>(sampleF);
                    float sf = sampleF - si;
                    si = std::clamp(si, 0, TRACK_VIS_SAMPLES - 2);
                    // Recompute cumulative shifts (small local array — cheap)
                    float shifts[TRACK_VIS_SAMPLES];
                    float accum = 0.0f;
                    for (int k = 0; k < TRACK_VIS_SAMPLES; k++) {
                        accum += vis.curvature[k] * (vw * 0.015f);
                        accum = std::clamp(accum, -trackHalfPx * 0.8f, trackHalfPx * 0.8f);
                        shifts[k] = accum;
                    }
                    float shift = shifts[si] + sf * (shifts[si + 1] - shifts[si]);
                    float perspFactor = 0.3f + 0.7f * (1.0f - yFrac);
                    outHalfW = trackHalfPx * perspFactor;
                    float cx = centerX + shift;
                    outX = static_cast<int>(cx + lateral * outHalfW);
                };
                
                for (int oi = 0; oi < vis.objectCount; oi++) {
                    const auto& obj = vis.objects[oi];
                    int ox, oy; float oHalfW;
                    objScreenPos(obj.ahead, obj.lateral, ox, oy, oHalfW);
                    // Scale objects smaller toward the horizon (perspective)
                    float scale = 0.3f + 0.7f * (1.0f - std::clamp(obj.ahead, 0.0f, 1.0f));
                    
                    switch (obj.kind) {
                        case TrackObjKind::NOISE_ENEMY: {
                            // Red horizontal bar spanning bandwidth.
                            // Color fades from bright red (full HP) → dark red/gray (low HP).
                            int barHalfW = std::max(4, static_cast<int>(obj.sizeParam * oHalfW));
                            int barH = std::max(3, static_cast<int>(6 * scale));
                            float hf = obj.healthFrac;
                            COLORREF neColor = RGB(
                                static_cast<int>(80 + 175 * hf),   // Red: 255→80
                                static_cast<int>(60 * (1.0f - hf)),// Green: 0→60 (gray tint when damaged)
                                static_cast<int>(60 * (1.0f - hf)) // Blue: 0→60
                            );
                            HBRUSH br = CreateSolidBrush(neColor);
                            RECT r = { ox - barHalfW, oy - barH / 2,
                                       ox + barHalfW, oy + barH / 2 };
                            FillRect(memDC, &r, br);
                            DeleteObject(br);
                            // Health dots above the bar
                            HBRUSH dotBr = CreateSolidBrush(neColor);
                            HBRUSH prevDotBr = (HBRUSH)SelectObject(memDC, dotBr);
                            for (int hp = 0; hp < obj.intParam && hp < 5; hp++) {
                                int dotR = std::max(2, static_cast<int>(3 * scale));
                                int dotX = ox - (obj.intParam - 1) * (dotR + 1) + hp * 2 * (dotR + 1);
                                Ellipse(memDC, dotX - dotR, oy - barH / 2 - dotR * 3,
                                        dotX + dotR, oy - barH / 2 - dotR);
                            }
                            SelectObject(memDC, prevDotBr);
                            DeleteObject(dotBr);
                            break;
                        }
                        case TrackObjKind::MORSE_SIGNAL: {
                            // Render morse code as dots and dashes instead of the letter.
                            // Each character is mapped to its ITU morse representation.
                            // Bordered rounded rectangles for dashes, bordered circles for dots.
                            static const char* morseTable[36] = {
                                ".-","-...","-.-.","-..",".","..-.","--.","....","..",   // A-I
                                ".---","-.-",".-..","--","-.","---",".--.","--.-",".-.", // J-R
                                "...","-","..-","...-",".--","-..-","-.--","--..",       // S-Z
                                "-----",".----","..---","...--","....-",".....",         // 0-5
                                "-....","--...","---..","----."                          // 6-9
                            };
                            char ch = static_cast<char>(std::toupper(obj.intParam));
                            const char* morse = nullptr;
                            if (ch >= 'A' && ch <= 'Z') morse = morseTable[ch - 'A'];
                            else if (ch >= '0' && ch <= '9') morse = morseTable[26 + ch - '0'];
                            
                            COLORREF mColor = obj.highlight ? RGB(255, 255, 100) : RGB(0, 230, 230);
                            HPEN mp = CreatePen(PS_SOLID, std::max(1, static_cast<int>(2 * scale)), mColor);
                            HBRUSH mBr = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
                            HPEN mpPrev = (HPEN)SelectObject(memDC, mp);
                            HBRUSH mBrPrev = (HBRUSH)SelectObject(memDC, mBr);
                            
                            if (morse) {
                                int len = static_cast<int>(std::strlen(morse));
                                int dotSz = std::max(3, static_cast<int>(4 * scale));
                                int dashW = dotSz * 3;
                                int gap = std::max(2, static_cast<int>(2 * scale));
                                int totalW = 0;
                                for (int m = 0; m < len; m++) {
                                    totalW += (morse[m] == '.' ? dotSz * 2 : dashW) + (m < len - 1 ? gap : 0);
                                }
                                int mx = ox - totalW / 2;
                                for (int m = 0; m < len; m++) {
                                    if (morse[m] == '.') {
                                        // Dot: small outlined circle
                                        Ellipse(memDC, mx, oy - dotSz, mx + dotSz * 2, oy + dotSz);
                                        mx += dotSz * 2 + gap;
                                    } else {
                                        // Dash: outlined rounded rectangle
                                        RoundRect(memDC, mx, oy - dotSz, mx + dashW, oy + dotSz, dotSz, dotSz);
                                        mx += dashW + gap;
                                    }
                                }
                            } else {
                                // Unknown char — draw a small outlined box as fallback
                                int sz = std::max(4, static_cast<int>(6 * scale));
                                Rectangle(memDC, ox - sz, oy - sz, ox + sz, oy + sz);
                            }
                            SelectObject(memDC, mBrPrev);
                            SelectObject(memDC, mpPrev);
                            DeleteObject(mp);
                            break;
                        }
                        case TrackObjKind::POWER_UP: {
                            // Gold circle, larger if highlighted (being collected)
                            int radius = std::max(6, static_cast<int>((obj.highlight ? 16 : 12) * scale));
                            HBRUSH pb = CreateSolidBrush(obj.highlight ? RGB(255, 255, 180) : RGB(255, 200, 40));  // Gold, white-gold when collecting
                            HBRUSH prevBr = (HBRUSH)SelectObject(memDC, pb);
                            Ellipse(memDC, ox - radius, oy - radius, ox + radius, oy + radius);
                            SelectObject(memDC, prevBr);
                            DeleteObject(pb);
                            // Label: power-up type initial (S=Speed, F=FireRate, A=AutoFire, I=Immunity, D=Duration)
                            const char* labels[] = {"S", "F", "A", "I", "D"};
                            constexpr int numLabels = sizeof(labels) / sizeof(labels[0]);
                            int ti = std::clamp(obj.intParam, 0, numLabels - 1);
                            SetBkMode(memDC, TRANSPARENT);
                            SetTextColor(memDC, RGB(0, 0, 0));
                            TextOutA(memDC, ox - 4, oy - 7, labels[ti], 1);
                            break;
                        }
                        case TrackObjKind::QSO_STOERER: {
                            // Magenta triangle — color fades from hot magenta (full HP)
                            // → dark purple/gray (low HP).
                            float hf = obj.healthFrac;
                            COLORREF stoererColor = RGB(
                                static_cast<int>(80 + 175 * hf),   // Red: 255→80
                                0,
                                static_cast<int>(60 + 140 * hf)    // Blue: 200→60
                            );
                            int sz = std::max(8, static_cast<int>(16 * scale));
                            POINT tri[3] = {
                                {ox, oy - sz}, {ox - sz, oy + sz / 2}, {ox + sz, oy + sz / 2}
                            };
                            HBRUSH tb = CreateSolidBrush(stoererColor);
                            HBRUSH tbPrev = (HBRUSH)SelectObject(memDC, tb);
                            Polygon(memDC, tri, 3);
                            SelectObject(memDC, tbPrev);
                            DeleteObject(tb);
                            // "!" warning label
                            SetBkMode(memDC, TRANSPARENT);
                            SetTextColor(memDC, RGB(255, 255, 255));
                            TextOutA(memDC, ox - 3, oy - 7, "!", 1);
                            break;
                        }
                    }
                }
                
                // --- Car rectangle ---
                // Positioned at the bottom, between the track boundaries.
                // Moves left/right based on playerLateral (-1..+1).
                {
                    int carW = std::max(20, vw / 18);   // Car width
                    int carH = std::max(30, vh / 12);   // Car height
                    float carCenterX = centerX + vis.playerLateral * (trackHalfPx - carW * 0.5f);
                    int carLeft = static_cast<int>(carCenterX - carW * 0.5f);
                    int carTop = vy + vh - carH - std::max(8, vh / 30);  // Small gap from bottom
                    
                    RECT carRect = { carLeft, carTop, carLeft + carW, carTop + carH };
                    HBRUSH carBrush = CreateSolidBrush(RGB(255, 255, 255));
                    FillRect(memDC, &carRect, carBrush);
                    DeleteObject(carBrush);
                    
                    // Outline for visibility against any background
                    HBRUSH outlineBrush = CreateSolidBrush(RGB(180, 180, 180));
                    FrameRect(memDC, &carRect, outlineBrush);
                    DeleteObject(outlineBrush);
                    
                    // --- Weapon aim pointer ---
                    {
                        int pointerLen = std::max(25, vh / 10);
                        float aimCx = carCenterX;
                        float aimCy = static_cast<float>(carTop);
                        float tipX = aimCx + std::sin(vis.aimAngle) * pointerLen;
                        float tipY = aimCy - std::cos(vis.aimAngle) * pointerLen;
                        
                        HPEN aimPen = CreatePen(PS_SOLID, std::max(2, vw / 200), RGB(255, 120, 0));
                        HPEN prevPen = (HPEN)SelectObject(memDC, aimPen);
                        MoveToEx(memDC, static_cast<int>(aimCx), static_cast<int>(aimCy), nullptr);
                        LineTo(memDC, static_cast<int>(tipX), static_cast<int>(tipY));
                        
                        int crossR = std::max(3, vw / 150);
                        Ellipse(memDC,
                                static_cast<int>(tipX) - crossR, static_cast<int>(tipY) - crossR,
                                static_cast<int>(tipX) + crossR, static_cast<int>(tipY) + crossR);
                        
                        SelectObject(memDC, prevPen);
                        DeleteObject(aimPen);
                    }
                }
                
                // Clear player 0 clipping region after rendering
                if (p0ClipRgn) {
                    SelectClipRgn(memDC, nullptr);
                    DeleteObject(p0ClipRgn);
                }
            }
            
            // --- HUD status bar at the top of the viewport ---
            // Shows the same information as the braille display: speed, frequency,
            // SWR, PA health, score, laps — readable text for sighted users.
            if (vis.active && vis.statusText[0] != '\0') {
                // When in split-screen, use player 0's viewport dimensions
                int hudVx = 0, hudVy = 0, hudVw = w, hudVh = h;
                if (mpCount > 1) {
                    if (mpCount == 2) {
                        if (mpSplitOri == 0) { hudVw = w / 2; }
                        else                 { hudVh = h / 2; }
                    } else if (mpCount == 3) {
                        if (mpSplitOri == 0) { hudVw = w / 3; }
                        else                 { hudVh = h / 3; }
                    } else {
                        hudVw = w / 2; hudVh = h / 2;
                    }
                }
                int barH = std::max(22, hudVh / 30);
                RECT barRect = { hudVx, hudVy, hudVx + hudVw, hudVy + barH };
                HBRUSH barBg = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(memDC, &barRect, barBg);
                DeleteObject(barBg);
                
                SetBkMode(memDC, TRANSPARENT);
                SetTextColor(memDC, RGB(255, 255, 255));
                HFONT hudFont = CreateFontA(
                    barH - 4, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
                HFONT oldFont = (HFONT)SelectObject(memDC, hudFont);
                RECT textRect = { hudVx + 8, hudVy + 2, hudVx + hudVw - 8, hudVy + barH - 2 };
                DrawTextA(memDC, vis.statusText, -1, &textRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(memDC, oldFont);
                DeleteObject(hudFont);
            }
            
            // --- Multiplayer split-screen viewports for players 1+ ---
            // Renders additional player views with separator lines and per-player
            // track visualization (curvature, objects, HUD) in their viewport region.
            if (vis.active && mpCount > 1) {
                // Draw separator lines between viewports
                HPEN sepPen = CreatePen(PS_SOLID, 2, RGB(200, 200, 200));
                HPEN prevSepPen = (HPEN)SelectObject(memDC, sepPen);
                
                if (mpCount == 2) {
                    if (mpSplitOri == 0) { // Horizontal: vertical line at center
                        MoveToEx(memDC, w / 2, 0, nullptr);
                        LineTo(memDC, w / 2, h);
                    } else { // Vertical: horizontal line at center
                        MoveToEx(memDC, 0, h / 2, nullptr);
                        LineTo(memDC, w, h / 2);
                    }
                } else if (mpCount == 3) {
                    if (mpSplitOri == 0) { // Three columns
                        MoveToEx(memDC, w / 3, 0, nullptr);     LineTo(memDC, w / 3, h);
                        MoveToEx(memDC, 2 * w / 3, 0, nullptr); LineTo(memDC, 2 * w / 3, h);
                    } else { // Three rows
                        MoveToEx(memDC, 0, h / 3, nullptr);     LineTo(memDC, w, h / 3);
                        MoveToEx(memDC, 0, 2 * h / 3, nullptr); LineTo(memDC, w, 2 * h / 3);
                    }
                } else { // 4 players: 2x2 grid
                    MoveToEx(memDC, w / 2, 0, nullptr); LineTo(memDC, w / 2, h);
                    MoveToEx(memDC, 0, h / 2, nullptr); LineTo(memDC, w, h / 2);
                }
                
                SelectObject(memDC, prevSepPen);
                DeleteObject(sepPen);
                
                // Render simplified track view for each additional player
                for (int p = 1; p < mpCount; p++) {
                    if (!mpVis[p].active) continue;
                    
                    // Calculate viewport rect for this player
                    int vpX, vpY, vpW, vpH;
                    if (mpCount == 2) {
                        if (mpSplitOri == 0) { vpX = w / 2; vpY = 0; vpW = w / 2; vpH = h; }
                        else                 { vpX = 0; vpY = h / 2; vpW = w; vpH = h / 2; }
                    } else if (mpCount == 3) {
                        if (mpSplitOri == 0) { vpX = p * w / 3; vpY = 0; vpW = w / 3; vpH = h; }
                        else                 { vpX = 0; vpY = p * h / 3; vpW = w; vpH = h / 3; }
                    } else { // 4 players: 2x2
                        vpX = (p % 2) * w / 2; vpY = (p / 2) * h / 2;
                        vpW = w / 2; vpH = h / 2;
                    }
                    
                    // Set clipping region for this player's viewport
                    HRGN vpRgn = CreateRectRgn(vpX, vpY, vpX + vpW, vpY + vpH);
                    SelectClipRgn(memDC, vpRgn);
                    
                    const auto& pv = mpVis[p];
                    
                    // Draw track boundary bars (simplified)
                    const float pTrackHalfPx = vpW * 0.35f;
                    const float pCenterX = vpX + vpW * 0.5f;
                    const int pBarWidth = std::max(3, vpW / 60);
                    
                    float pCumulShift = 0.0f;
                    float pPrevLeftX = pCenterX - pTrackHalfPx;
                    float pPrevRightX = pCenterX + pTrackHalfPx;
                    int pPrevY = vpY + vpH;
                    
                    for (int i = 0; i < TRACK_VIS_SAMPLES; i++) {
                        int py = vpY + vpH - static_cast<int>((static_cast<float>(i + 1) / TRACK_VIS_SAMPLES) * vpH * 0.85f);
                        pCumulShift += pv.curvature[i] * (vpW * 0.015f);
                        pCumulShift = std::max(-pTrackHalfPx * 0.8f, std::min(pTrackHalfPx * 0.8f, pCumulShift));
                        float perspFactor = 0.3f + 0.7f * (static_cast<float>(TRACK_VIS_SAMPLES - i) / TRACK_VIS_SAMPLES);
                        float halfW2 = pTrackHalfPx * perspFactor;
                        float leftX2 = pCenterX + pCumulShift - halfW2;
                        float rightX2 = pCenterX + pCumulShift + halfW2;
                        
                        // Color based on SWR
                        COLORREF col;
                        if (pv.swrValue[i] < 1.5f) {
                            col = RGB(0, 200, 0);
                        } else if (pv.reactance[i] > 5.0f) {
                            col = RGB(220, 60, 0);
                        } else if (pv.reactance[i] < -5.0f) {
                            col = RGB(0, 60, 220);
                        } else {
                            col = RGB(220, 200, 0);
                        }
                        
                        HPEN trkPen = CreatePen(PS_SOLID, pBarWidth, col);
                        HPEN oldTrkPen = (HPEN)SelectObject(memDC, trkPen);
                        MoveToEx(memDC, static_cast<int>(pPrevLeftX), pPrevY, nullptr);
                        LineTo(memDC, static_cast<int>(leftX2), py);
                        MoveToEx(memDC, static_cast<int>(pPrevRightX), pPrevY, nullptr);
                        LineTo(memDC, static_cast<int>(rightX2), py);
                        SelectObject(memDC, oldTrkPen);
                        DeleteObject(trkPen);
                        
                        pPrevLeftX = leftX2;
                        pPrevRightX = rightX2;
                        pPrevY = py;
                    }
                    
                    // Draw game objects (noise enemies, morse signals, power-ups, QSO Störer)
                    // relative to this player's position, using the same rendering as player 0.
                    {
                        auto pObjScreenPos = [&](float ahead, float lateral,
                                                  int& ox, int& oy, float& oHalfW) {
                            float perspFactor = 0.3f + 0.7f * (1.0f - std::clamp(ahead, 0.0f, 1.0f));
                            oHalfW = pTrackHalfPx * perspFactor;
                            // Re-accumulate curvature shift up to this object's ahead position
                            float shift = 0.0f;
                            int sampleIdx = static_cast<int>(ahead * (TRACK_VIS_SAMPLES - 1));
                            for (int si = 0; si <= sampleIdx && si < TRACK_VIS_SAMPLES; si++)
                                shift += pv.curvature[si] * (vpW * 0.015f);
                            shift = std::clamp(shift, -pTrackHalfPx * 0.8f, pTrackHalfPx * 0.8f);
                            ox = vpX + static_cast<int>(vpW * 0.5f + shift + lateral * oHalfW);
                            oy = vpY + vpH - static_cast<int>(ahead * vpH * 0.85f);
                        };

                        for (int oi = 0; oi < pv.objectCount; oi++) {
                            const auto& obj = pv.objects[oi];
                            int ox, oy; float oHalfW;
                            pObjScreenPos(obj.ahead, obj.lateral, ox, oy, oHalfW);
                            float scale = 0.3f + 0.7f * (1.0f - std::clamp(obj.ahead, 0.0f, 1.0f));

                            switch (obj.kind) {
                                case TrackObjKind::NOISE_ENEMY: {
                                    int barHalfW = std::max(4, static_cast<int>(obj.sizeParam * oHalfW));
                                    int barH = std::max(3, static_cast<int>(6 * scale));
                                    float hf = obj.healthFrac;
                                    COLORREF neColor = RGB(
                                        static_cast<int>(80 + 175 * hf),
                                        static_cast<int>(60 * (1.0f - hf)),
                                        static_cast<int>(60 * (1.0f - hf)));
                                    HBRUSH br = CreateSolidBrush(neColor);
                                    RECT r = { ox - barHalfW, oy - barH / 2,
                                               ox + barHalfW, oy + barH / 2 };
                                    FillRect(memDC, &r, br);
                                    DeleteObject(br);
                                    break;
                                }
                                case TrackObjKind::MORSE_SIGNAL: {
                                    static const char* morseTable[36] = {
                                        ".-","-...","-.-.","-..",".","..-.","--.","....","..",
                                        ".---","-.-",".-..","--","-.","---",".--.","--.-",".-.",
                                        "...","-","..-","...-",".--","-..-","-.--","--..",
                                        "-----",".----","..---","...--","....-",".....",
                                        "-....","--...","---..","----."
                                    };
                                    char ch = static_cast<char>(std::toupper(obj.intParam));
                                    const char* morse = nullptr;
                                    if (ch >= 'A' && ch <= 'Z') morse = morseTable[ch - 'A'];
                                    else if (ch >= '0' && ch <= '9') morse = morseTable[26 + ch - '0'];
                                    COLORREF mColor = RGB(0, 230, 230);
                                    HPEN mp = CreatePen(PS_SOLID, std::max(1, static_cast<int>(2 * scale)), mColor);
                                    HBRUSH mBr = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
                                    HPEN mpPrev = (HPEN)SelectObject(memDC, mp);
                                    HBRUSH mBrPrev = (HBRUSH)SelectObject(memDC, mBr);
                                    if (morse) {
                                        int len = static_cast<int>(std::strlen(morse));
                                        int dotSz = std::max(3, static_cast<int>(4 * scale));
                                        int dashW = dotSz * 3;
                                        int gap = std::max(2, static_cast<int>(2 * scale));
                                        int totalW = 0;
                                        for (int m = 0; m < len; m++)
                                            totalW += (morse[m] == '.' ? dotSz * 2 : dashW) + (m < len - 1 ? gap : 0);
                                        int mx = ox - totalW / 2;
                                        for (int m = 0; m < len; m++) {
                                            if (morse[m] == '.') {
                                                Ellipse(memDC, mx, oy - dotSz, mx + dotSz * 2, oy + dotSz);
                                                mx += dotSz * 2 + gap;
                                            } else {
                                                RoundRect(memDC, mx, oy - dotSz, mx + dashW, oy + dotSz, dotSz, dotSz);
                                                mx += dashW + gap;
                                            }
                                        }
                                    } else {
                                        int sz = std::max(4, static_cast<int>(6 * scale));
                                        Rectangle(memDC, ox - sz, oy - sz, ox + sz, oy + sz);
                                    }
                                    SelectObject(memDC, mBrPrev);
                                    SelectObject(memDC, mpPrev);
                                    DeleteObject(mp);
                                    break;
                                }
                                case TrackObjKind::POWER_UP: {
                                    int radius = std::max(6, static_cast<int>(12 * scale));
                                    HBRUSH pb = CreateSolidBrush(RGB(255, 200, 40));
                                    HBRUSH prevBr = (HBRUSH)SelectObject(memDC, pb);
                                    Ellipse(memDC, ox - radius, oy - radius, ox + radius, oy + radius);
                                    SelectObject(memDC, prevBr);
                                    DeleteObject(pb);
                                    const char* labels[] = {"S", "F", "A", "I", "D"};
                                    constexpr int numLabels = sizeof(labels) / sizeof(labels[0]);
                                    int ti = std::clamp(obj.intParam, 0, numLabels - 1);
                                    SetBkMode(memDC, TRANSPARENT);
                                    SetTextColor(memDC, RGB(0, 0, 0));
                                    TextOutA(memDC, ox - 4, oy - 7, labels[ti], 1);
                                    break;
                                }
                                case TrackObjKind::QSO_STOERER: {
                                    float hf = obj.healthFrac;
                                    COLORREF stoererColor = RGB(
                                        static_cast<int>(80 + 175 * hf), 0,
                                        static_cast<int>(60 + 140 * hf));
                                    int sz = std::max(8, static_cast<int>(16 * scale));
                                    POINT tri[3] = {
                                        {ox, oy - sz}, {ox - sz, oy + sz / 2}, {ox + sz, oy + sz / 2}
                                    };
                                    HBRUSH tb = CreateSolidBrush(stoererColor);
                                    HBRUSH tbPrev = (HBRUSH)SelectObject(memDC, tb);
                                    Polygon(memDC, tri, 3);
                                    SelectObject(memDC, tbPrev);
                                    DeleteObject(tb);
                                    SetBkMode(memDC, TRANSPARENT);
                                    SetTextColor(memDC, RGB(255, 255, 255));
                                    TextOutA(memDC, ox - 3, oy - 7, "!", 1);
                                    break;
                                }
                            }
                        }
                    }

                    // Draw player car indicator (small rectangle at bottom center)
                    {
                        int carW2 = std::max(6, vpW / 30);
                        int carH2 = std::max(10, vpH / 25);
                        int carX = vpX + static_cast<int>(vpW * 0.5f + pv.playerLateral * pTrackHalfPx) - carW2 / 2;
                        int carY = vpY + vpH - carH2 - std::max(4, vpH / 40);
                        HBRUSH carBr = CreateSolidBrush(RGB(0, 200, 255));
                        RECT carRect = { carX, carY, carX + carW2, carY + carH2 };
                        FillRect(memDC, &carRect, carBr);
                        DeleteObject(carBr);
                    }
                    
                    // Draw HUD for this player
                    if (pv.statusText[0] != '\0') {
                        int pBarH = std::max(16, vpH / 30);
                        RECT pBarRect = { vpX, vpY, vpX + vpW, vpY + pBarH };
                        HBRUSH pBarBg = CreateSolidBrush(RGB(0, 0, 0));
                        FillRect(memDC, &pBarRect, pBarBg);
                        DeleteObject(pBarBg);
                        
                        SetBkMode(memDC, TRANSPARENT);
                        SetTextColor(memDC, RGB(200, 255, 200));
                        HFONT pHudFont = CreateFontA(
                            pBarH - 2, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
                        HFONT prevPHud = (HFONT)SelectObject(memDC, pHudFont);
                        RECT pTxtRect = { vpX + 4, vpY + 1, vpX + vpW - 4, vpY + pBarH - 1 };
                        DrawTextA(memDC, pv.statusText, -1, &pTxtRect,
                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        SelectObject(memDC, prevPHud);
                        DeleteObject(pHudFont);
                    }
                    
                    // Clear clipping region
                    SelectClipRgn(memDC, nullptr);
                    DeleteObject(vpRgn);
                }
            }
            
            // --- Scrolling TTS banner (Bandwacht, Intruder, Verkehrsservice) ---
            // Placed above the car area so it doesn't overlap with track graphics.
            // During gameplay: above the car (~80% from top).
            // During menus: near the bottom of the screen.
            if (vis.bannerActive && vis.bannerText[0] != '\0') {
                int bannerH = std::max(28, h / 22);
                // Position: above the car area during gameplay, bottom during menus
                int bannerTop = vis.active
                    ? static_cast<int>(h * 0.72f) - bannerH  // Above car area
                    : h - bannerH;                            // Bottom during menus
                RECT bannerRect = { 0, bannerTop, w, bannerTop + bannerH };
                // Dark background strip
                HBRUSH bannerBg = CreateSolidBrush(RGB(10, 10, 40));
                FillRect(memDC, &bannerRect, bannerBg);
                DeleteObject(bannerBg);
                // Top and bottom border lines for separation
                HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 160));
                HPEN prevBorderPen = (HPEN)SelectObject(memDC, borderPen);
                MoveToEx(memDC, 0, bannerTop, nullptr);
                LineTo(memDC, w, bannerTop);
                MoveToEx(memDC, 0, bannerTop + bannerH, nullptr);
                LineTo(memDC, w, bannerTop + bannerH);
                SelectObject(memDC, prevBorderPen);
                DeleteObject(borderPen);
                
                // Yellow text on dark background for high visibility
                SetBkMode(memDC, TRANSPARENT);
                SetTextColor(memDC, RGB(255, 220, 50));
                HFONT bannerFont = CreateFontA(
                    bannerH - 6, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
                HFONT oldBannerFont = (HFONT)SelectObject(memDC, bannerFont);
                
                // Measure text width to know when scrolling is complete
                SIZE textSize;
                GetTextExtentPoint32A(memDC, vis.bannerText,
                                     static_cast<int>(std::strlen(vis.bannerText)), &textSize);
                
                // Scroll from right edge to fully off the left edge
                float scrollSpeed = 1.5f;  // Pixels per frame (~45px/sec at 30fps)
                sHamSpiritWindow.bannerPixelOffset += scrollSpeed;
                int textX = w - static_cast<int>(sHamSpiritWindow.bannerPixelOffset);
                int textY = bannerTop + (bannerH - (bannerH - 6)) / 2;
                
                // Clip to banner area
                HRGN clipRgn = CreateRectRgn(0, bannerTop, w, bannerTop + bannerH);
                SelectClipRgn(memDC, clipRgn);
                TextOutA(memDC, textX, textY, vis.bannerText,
                         static_cast<int>(std::strlen(vis.bannerText)));
                SelectClipRgn(memDC, nullptr);
                DeleteObject(clipRgn);
                
                SelectObject(memDC, oldBannerFont);
                DeleteObject(bannerFont);
                
                // Deactivate banner once it has fully scrolled off the left edge
                if (textX + textSize.cx < 0) {
                    std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
                    sHamSpiritWindow.trackVis.bannerActive = false;
                    sHamSpiritWindow.trackVis.bannerText[0] = '\0';
                    sHamSpiritWindow.bannerPixelOffset = 0.0f;
                }
            }
            
            // --- Text overlay (intro narration, controls explanation) ---
            // Shows centered text over the wallpaper in a dark box at the bottom third.
            {
                TextOverlayData txtOv;
                {
                    std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
                    txtOv = sHamSpiritWindow.textOverlay;
                }
                if (txtOv.visible && txtOv.text[0] != '\0') {
                    int fontSize = std::max(16, h / 32);
                    int padX = std::max(24, w / 20);
                    int padY = std::max(12, h / 50);
                    int boxW = w - padX * 2;
                    int boxH = std::max(fontSize * 4, h / 6);
                    int boxX = padX;
                    int boxY = h - boxH - padY;  // Near the bottom

                    HBRUSH bg = CreateSolidBrush(RGB(10, 10, 30));
                    RECT bgRect = { boxX, boxY, boxX + boxW, boxY + boxH };
                    FillRect(memDC, &bgRect, bg);
                    DeleteObject(bg);
                    // Thin cyan border
                    HPEN brd = CreatePen(PS_SOLID, 1, RGB(0, 160, 200));
                    HPEN prevBrd = (HPEN)SelectObject(memDC, brd);
                    HBRUSH hol = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
                    HBRUSH prevHol = (HBRUSH)SelectObject(memDC, hol);
                    Rectangle(memDC, boxX, boxY, boxX + boxW, boxY + boxH);
                    SelectObject(memDC, prevHol);
                    SelectObject(memDC, prevBrd);
                    DeleteObject(brd);

                    SetBkMode(memDC, TRANSPARENT);
                    SetTextColor(memDC, RGB(220, 220, 240));
                    HFONT fnt = CreateFontA(
                        fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
                    HFONT prevFnt = (HFONT)SelectObject(memDC, fnt);
                    RECT txtRect = { boxX + padX / 2, boxY + padY,
                                     boxX + boxW - padX / 2, boxY + boxH - padY };
                    DrawTextA(memDC, txtOv.text, -1, &txtRect,
                              DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
                    SelectObject(memDC, prevFnt);
                    DeleteObject(fnt);
                }
            }

            // --- Text input overlay (callsign/name entry) ---
            // Shows a labeled text input box with the current typed value and a blinking cursor.
            // Declared here (outside its rendering block) so the menu overlay block below
            // can check tiOv.visible to avoid painting over the text input box.
            TextInputOverlayData tiOv;
            {
                {
                    std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
                    tiOv = sHamSpiritWindow.textInputOverlay;
                }
                if (tiOv.visible) {
#ifdef _DEBUG
                    {
                        char dbg[256];
                        std::snprintf(dbg, sizeof(dbg), "WM_PAINT: textInput visible, label='%.40s' value='%.40s'",
                                      tiOv.label, tiOv.value);
                        OutputDebugStringA(dbg);
                    }
#endif
                    int fontSize = std::max(18, h / 28);
                    int labelH = static_cast<int>(fontSize * 1.5f);
                    int fieldH = static_cast<int>(fontSize * 2.0f);
                    int padX = std::max(24, w / 10);
                    int panelW = w - padX * 2;
                    int panelH = labelH + fieldH + std::max(20, h / 30);
                    int panelX = padX;
                    int panelY = (h - panelH) / 2;

                    // Dark background
                    HBRUSH bg = CreateSolidBrush(RGB(10, 10, 30));
                    RECT bgR = { panelX, panelY, panelX + panelW, panelY + panelH };
                    FillRect(memDC, &bgR, bg);
                    DeleteObject(bg);
                    // Cyan border
                    HPEN brd = CreatePen(PS_SOLID, 2, RGB(0, 180, 220));
                    HPEN prevBrd = (HPEN)SelectObject(memDC, brd);
                    HBRUSH hol = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
                    HBRUSH prevHol = (HBRUSH)SelectObject(memDC, hol);
                    Rectangle(memDC, panelX, panelY, panelX + panelW, panelY + panelH);
                    SelectObject(memDC, prevHol);
                    SelectObject(memDC, prevBrd);
                    DeleteObject(brd);

                    SetBkMode(memDC, TRANSPARENT);
                    // Label
                    SetTextColor(memDC, RGB(0, 220, 255));
                    HFONT lblFnt = CreateFontA(
                        fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
                    HFONT prevFnt = (HFONT)SelectObject(memDC, lblFnt);
                    int lblPad = std::max(10, h / 60);
                    RECT lblR = { panelX + padX / 2, panelY + lblPad,
                                  panelX + panelW - padX / 2, panelY + lblPad + labelH };
                    DrawTextA(memDC, tiOv.label, -1, &lblR,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    // Input field background
                    int fieldY = panelY + lblPad + labelH;
                    int fieldPadX = std::max(16, w / 20);
                    HBRUSH fieldBg = CreateSolidBrush(RGB(20, 20, 50));
                    RECT fieldR = { panelX + fieldPadX, fieldY,
                                    panelX + panelW - fieldPadX, fieldY + fieldH };
                    FillRect(memDC, &fieldR, fieldBg);
                    DeleteObject(fieldBg);
                    // Field border
                    HPEN fldBrd = CreatePen(PS_SOLID, 1, RGB(80, 80, 160));
                    HPEN prevFldBrd = (HPEN)SelectObject(memDC, fldBrd);
                    Rectangle(memDC, panelX + fieldPadX, fieldY,
                              panelX + panelW - fieldPadX, fieldY + fieldH);
                    SelectObject(memDC, prevFldBrd);
                    DeleteObject(fldBrd);

                    // Input text + blinking cursor
                    SetTextColor(memDC, RGB(255, 255, 255));
                    HFONT valFnt = CreateFontA(
                        static_cast<int>(fontSize * 1.2f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
                    SelectObject(memDC, valFnt);
                    // Build display string with blinking cursor
                    char dispBuf[TEXT_INPUT_VALUE_LEN + 4];
                    bool cursorOn = HamSpirit::isCursorVisible(static_cast<float>(GetTickCount()) / 1000.0f);
                    std::snprintf(dispBuf, sizeof(dispBuf), "%s%s", tiOv.value, cursorOn ? "_" : " ");
                    RECT valR = { panelX + fieldPadX + 8, fieldY + 4,
                                  panelX + panelW - fieldPadX - 8, fieldY + fieldH - 4 };
                    DrawTextA(memDC, dispBuf, -1, &valR,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                    SelectObject(memDC, prevFnt);
                    DeleteObject(lblFnt);
                    DeleteObject(valFnt);
                }
            }

            // --- Menu overlay (Main Menu, Pause Menu, Config) ---
            // Draws menu items over the wallpaper image with retro-style highlighting.
            // Minimalistic design: dark panel, monospace font, bracket selection indicator.
            // Skip when text input overlay is active (it occupies the same screen area).
            {
                MenuOverlayData menu;
                {
                    std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
                    menu = sHamSpiritWindow.menuOverlay;
                }
                if (menu.visible && menu.itemCount > 0 && !tiOv.visible) {
                    // Compute panel dimensions
                    int fontSize = std::max(18, h / 28);
                    int lineH = static_cast<int>(fontSize * 1.6f);
                    int titleH = static_cast<int>(fontSize * 1.3f);
                    int panelPadX = std::max(16, w / 40);
                    int panelPadY = std::max(12, h / 50);
                    int panelH = titleH + panelPadY + menu.itemCount * lineH + panelPadY * 2;
                    int panelW = std::min(w * 3 / 5, std::max(320, w / 3));
                    int panelX = (w - panelW) / 2;
                    int panelY = (h - panelH) / 2;  // Centered vertically
                    
                    // Dark semi-transparent background panel
                    HBRUSH panelBg = CreateSolidBrush(RGB(10, 10, 30));
                    RECT panelRect = { panelX, panelY, panelX + panelW, panelY + panelH };
                    FillRect(memDC, &panelRect, panelBg);
                    DeleteObject(panelBg);
                    
                    // Panel border (retro cyan glow)
                    HPEN panelBorder = CreatePen(PS_SOLID, 2, RGB(0, 180, 220));
                    HPEN prevPanelPen = (HPEN)SelectObject(memDC, panelBorder);
                    HBRUSH hollowBr = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
                    HBRUSH prevPanelBr = (HBRUSH)SelectObject(memDC, hollowBr);
                    Rectangle(memDC, panelX, panelY, panelX + panelW, panelY + panelH);
                    SelectObject(memDC, prevPanelBr);
                    SelectObject(memDC, prevPanelPen);
                    DeleteObject(panelBorder);
                    
                    SetBkMode(memDC, TRANSPARENT);
                    
                    // Title text (bright cyan, centered)
                    HFONT titleFont = CreateFontA(
                        titleH, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
                    HFONT prevFont = (HFONT)SelectObject(memDC, titleFont);
                    SetTextColor(memDC, RGB(0, 220, 255));
                    RECT titleRect = { panelX + panelPadX, panelY + panelPadY,
                                       panelX + panelW - panelPadX, panelY + panelPadY + titleH };
                    DrawTextA(memDC, menu.title, -1, &titleRect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SelectObject(memDC, prevFont);
                    DeleteObject(titleFont);
                    
                    // Separator line under title
                    HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(0, 120, 160));
                    HPEN prevSepPen = (HPEN)SelectObject(memDC, sepPen);
                    int sepY = panelY + panelPadY + titleH + panelPadY / 2;
                    MoveToEx(memDC, panelX + panelPadX, sepY, nullptr);
                    LineTo(memDC, panelX + panelW - panelPadX, sepY);
                    SelectObject(memDC, prevSepPen);
                    DeleteObject(sepPen);
                    
                    // Menu items
                    HFONT itemFont = CreateFontA(
                        fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
                    HFONT prevItemFont = (HFONT)SelectObject(memDC, itemFont);
                    
                    int itemStartY = sepY + panelPadY / 2;
                    for (int i = 0; i < menu.itemCount; i++) {
                        int itemY = itemStartY + i * lineH;
                        bool selected = (i == menu.selectedIndex);
                        
                        if (selected) {
                            // Highlight bar for selected item
                            RECT hlRect = { panelX + 4, itemY,
                                            panelX + panelW - 4, itemY + lineH };
                            HBRUSH hlBr = CreateSolidBrush(RGB(0, 60, 90));
                            FillRect(memDC, &hlRect, hlBr);
                            DeleteObject(hlBr);
                            // Bright yellow-green text for selected item
                            SetTextColor(memDC, RGB(180, 255, 60));
                        } else {
                            // Dimmer white for non-selected items
                            SetTextColor(memDC, RGB(180, 180, 200));
                        }
                        
                        // Build display string with selection indicator
                        char displayBuf[MENU_ITEM_TEXT_LEN + 8];
                        if (selected) {
                            std::snprintf(displayBuf, sizeof(displayBuf), "> %s", menu.items[i]);
                        } else {
                            std::snprintf(displayBuf, sizeof(displayBuf), "  %s", menu.items[i]);
                        }
                        
                        RECT itemRect = { panelX + panelPadX, itemY,
                                          panelX + panelW - panelPadX, itemY + lineH };
                        DrawTextA(memDC, displayBuf, -1, &itemRect,
                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    }
                    
                    SelectObject(memDC, prevItemFont);
                    DeleteObject(itemFont);
                }
            }
            
            // Blit the double-buffer to screen
            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            // Timer ID 1: fast repaint during gameplay (~30 fps)
            // Timer ID 2: slow UI repaint for menus and banners (~10 fps)
            if (wparam == 1 || wparam == 2) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;  // Prevent flicker — we paint the entire client area
        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

bool startHamSpiritWindow() {
    if (sHamSpiritWindow.running.load()) {
        return sHamSpiritWindow.hwnd != nullptr;
    }
    
    // Initialize GDI+ for image loading
    Gdiplus::GdiplusStartup(&sHamSpiritWindow.gdiplusToken, &sHamSpiritWindow.gdiplusStartupInput, nullptr);
    
    // Load wallpaper images using centralized paths
    {
        auto paths = HamSpirit::getWallpaperPaths();
        // Convert paths to wide strings for GDI+
        auto toWide = [](const std::string& s) {
            std::wstring w(s.begin(), s.end());
            return w;
        };
        if (paths.size() > 0) sHamSpiritWindow.imgTitle = Gdiplus::Image::FromFile(toWide(paths[0]).c_str());
        if (paths.size() > 1) sHamSpiritWindow.imgMenu = Gdiplus::Image::FromFile(toWide(paths[1]).c_str());
        if (paths.size() > 2) sHamSpiritWindow.imgRacing = Gdiplus::Image::FromFile(toWide(paths[2]).c_str());
    }
    // Validate loaded images (if loading failed, the Status will be non-OK)
    if (sHamSpiritWindow.imgTitle && sHamSpiritWindow.imgTitle->GetLastStatus() != Gdiplus::Ok) {
        delete sHamSpiritWindow.imgTitle; sHamSpiritWindow.imgTitle = nullptr;
    }
    if (sHamSpiritWindow.imgMenu && sHamSpiritWindow.imgMenu->GetLastStatus() != Gdiplus::Ok) {
        delete sHamSpiritWindow.imgMenu; sHamSpiritWindow.imgMenu = nullptr;
    }
    if (sHamSpiritWindow.imgRacing && sHamSpiritWindow.imgRacing->GetLastStatus() != Gdiplus::Ok) {
        delete sHamSpiritWindow.imgRacing; sHamSpiritWindow.imgRacing = nullptr;
    }
    // Start with title image
    sHamSpiritWindow.currentImage.store(sHamSpiritWindow.imgTitle ? 1 : 0);
    
    sHamSpiritWindow.consoleHwnd = GetConsoleWindow();
    sHamSpiritWindow.running.store(true);
    sHamSpiritWindow.ready.store(false);
    sHamSpiritWindow.thread = std::thread([] {
        sHamSpiritWindow.threadId = GetCurrentThreadId();
        HINSTANCE instance = GetModuleHandle(nullptr);
        const wchar_t* className = L"HamSpiritWindow";
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = HamSpiritWindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        if (!RegisterClassExW(&wc)) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                sHamSpiritWindow.ready.store(true);
                sHamSpiritWindow.running.store(false);
                sHamSpiritWindow.readyCv.notify_all();
                return;
            }
        }
        HWND hwnd = CreateWindowExW(
            0,
            className,
            L"Ham Spirit",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            960,
            540,
            nullptr,
            nullptr,
            instance,
            nullptr);
        sHamSpiritWindow.hwnd = hwnd;
        if (hwnd) {
            // Maximize the window for full-screen experience
            ShowWindow(hwnd, SW_SHOWMAXIMIZED);
            UpdateWindow(hwnd);
            // Start persistent UI timer for menu/banner repaints (~10 fps)
            // Timer ID 2 runs in all game states; Timer ID 1 is added during gameplay only
            SetTimer(hwnd, 2, 100, nullptr);
            // Use multiple techniques to ensure window gets focus.
            // Standard SetForegroundWindow may fail if our process doesn't own
            // the foreground lock (e.g., screen reader has focus). The topmost
            // toggle is a well-known workaround that forces activation on Windows
            // configurations where the standard method is blocked.
            // 1. BringWindowToTop (within our process)
            BringWindowToTop(hwnd);
            // 2. Temporarily set HWND_TOPMOST then remove — forces activation
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            // 3. SetForegroundWindow (may fail due to Windows restrictions, but try)
            SetForegroundWindow(hwnd);
            // 4. SetFocus (within our thread)
            SetFocus(hwnd);
        }
        sHamSpiritWindow.ready.store(true);
        sHamSpiritWindow.readyCv.notify_all();
        MSG msg;
        while (sHamSpiritWindow.running.load() && GetMessage(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (hwnd) {
            DestroyWindow(hwnd);
        }
        sHamSpiritWindow.hwnd = nullptr;
        sHamSpiritWindow.running.store(false);
    });
    // ready is atomic for cross-thread status; condition variable provides timed wait.
    bool ready = false;
    {
        std::unique_lock<std::mutex> lock(sHamSpiritWindow.mutex);
        ready = sHamSpiritWindow.readyCv.wait_for(
            lock,
            std::chrono::milliseconds(3000),
            [] { return sHamSpiritWindow.ready.load(); });
    }
    if (!ready) {
        sHamSpiritWindow.running.store(false);
        if (sHamSpiritWindow.threadId != 0) {
            PostThreadMessage(sHamSpiritWindow.threadId, WM_NULL, 0, 0);
        }
        if (sHamSpiritWindow.thread.joinable()) {
            sHamSpiritWindow.thread.join();
        }
        return false;
    }
    if (sHamSpiritWindow.hwnd && sHamSpiritWindow.consoleHwnd) {
        ShowWindow(sHamSpiritWindow.consoleHwnd, SW_HIDE);
        // Remove console from Alt+Tab list by adding WS_EX_TOOLWINDOW style
        LONG_PTR exStyle = GetWindowLongPtr(sHamSpiritWindow.consoleHwnd, GWL_EXSTYLE);
        if (exStyle != 0 || GetLastError() == 0) {
            sHamSpiritWindow.originalConsoleExStyle = exStyle;
            SetWindowLongPtr(sHamSpiritWindow.consoleHwnd, GWL_EXSTYLE,
                             (exStyle | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
        }
        sHamSpiritWindow.consoleHidden = true;
    }
    return sHamSpiritWindow.hwnd != nullptr;
}

void stopHamSpiritWindow() {
    if (sHamSpiritWindow.running.load()) {
        sHamSpiritWindow.running.store(false);
        if (sHamSpiritWindow.hwnd) {
            PostMessage(sHamSpiritWindow.hwnd, WM_CLOSE, 0, 0);
        }
        if (sHamSpiritWindow.threadId != 0) {
            PostThreadMessage(sHamSpiritWindow.threadId, WM_NULL, 0, 0);
        }
        if (sHamSpiritWindow.thread.joinable()) {
            sHamSpiritWindow.thread.join();
        }
    }
    if (sHamSpiritWindow.consoleHwnd && sHamSpiritWindow.consoleHidden) {
        // Restore original console window extended style
        SetWindowLongPtr(sHamSpiritWindow.consoleHwnd, GWL_EXSTYLE,
                         sHamSpiritWindow.originalConsoleExStyle);
        ShowWindow(sHamSpiritWindow.consoleHwnd, SW_SHOW);
    }
    sHamSpiritWindow.consoleHwnd = nullptr;
    sHamSpiritWindow.consoleHidden = false;
    
    // Clean up GDI+ images
    delete sHamSpiritWindow.imgTitle;  sHamSpiritWindow.imgTitle = nullptr;
    delete sHamSpiritWindow.imgMenu;   sHamSpiritWindow.imgMenu = nullptr;
    delete sHamSpiritWindow.imgRacing; sHamSpiritWindow.imgRacing = nullptr;
    if (sHamSpiritWindow.gdiplusToken) {
        Gdiplus::GdiplusShutdown(sHamSpiritWindow.gdiplusToken);
        sHamSpiritWindow.gdiplusToken = 0;
    }
}
#endif

// ============================================================================
// Non-Windows GUI implementation
// Three modes:
//   1. HAVE_NATIVE_GUI — native platform GUI (macOS AppKit via .mm file)
//   2. HAVE_SDL2       — SDL2 windowed GUI (Linux with SDL2 installed)
//   3. Fallback        — terminal-based ANSI escape code rendering
// ============================================================================
#ifndef _WIN32

#ifdef HAVE_NATIVE_GUI
// Native GUI is provided by a platform-specific source file
// (e.g., src/platform/macos/hamspirit_gui_macos.mm for macOS).
// RAII guard for the native window lifetime.
struct HamSpiritWindowGuard {
    bool enabled;
    explicit HamSpiritWindowGuard(bool enabledValue) : enabled(enabledValue) {}
    HamSpiritWindowGuard(const HamSpiritWindowGuard&) = delete;
    HamSpiritWindowGuard& operator=(const HamSpiritWindowGuard&) = delete;
    ~HamSpiritWindowGuard() {
        if (enabled) stopHamSpiritWindow();
    }
};

#elif defined(HAVE_SDL2)
// ============================================================================
// SDL2 + SDL2_ttf windowed GUI — analogous to Windows GDI+ window
// Provides menu overlays, text overlays, text input with cursor, and banners.
// ============================================================================
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

namespace {

struct SDLGuiState {
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    TTF_Font* fontNormal{nullptr};
    TTF_Font* fontLarge{nullptr};
    TTF_Font* fontSmall{nullptr};
    std::atomic<bool> active{false};
    std::thread renderThread;
    std::atomic<bool> renderRunning{false};
    std::mutex dataMtx;

    // Overlay state
    bool menuVisible{false};
    std::string menuTitle;
    std::vector<std::string> menuItems;
    int menuSelected{0};

    bool textVisible{false};
    std::string textContent;

    bool inputVisible{false};
    std::string inputLabel;
    std::string inputValue;

    std::string bannerText;
};

SDLGuiState sSDL;

// Try to load a monospace system font at the given size
TTF_Font* sdlLoadSystemFont(int size) {
    const char* paths[] = {
#ifdef __APPLE__
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/SFMono-Regular.otf",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
#endif
        nullptr
    };
    for (int i = 0; paths[i]; i++) {
        TTF_Font* f = TTF_OpenFont(paths[i], size);
        if (f) return f;
    }
    return nullptr;
}

// Render UTF-8 text to the SDL renderer
void sdlDrawText(const std::string& text, int x, int y,
                 TTF_Font* font, SDL_Color color, bool centered = false) {
    if (!sSDL.renderer || !font || text.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(sSDL.renderer, surf);
    if (tex) {
        SDL_Rect dst = {centered ? x - surf->w / 2 : x, y, surf->w, surf->h};
        SDL_RenderCopy(sSDL.renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

// Fill a rectangle with a solid color
void sdlFillRect(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(sSDL.renderer, c.r, c.g, c.b, c.a);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(sSDL.renderer, &r);
}

// Draw a rectangle outline
void sdlDrawRect(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(sSDL.renderer, c.r, c.g, c.b, c.a);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderDrawRect(sSDL.renderer, &r);
}

// Theme colors derived from centralized palette (hamspirit_game.h)
constexpr SDL_Color CLR_BG        = {HamSpirit::CLR_HS_BG.r, HamSpirit::CLR_HS_BG.g, HamSpirit::CLR_HS_BG.b, HamSpirit::CLR_HS_BG.a};
constexpr SDL_Color CLR_CYAN      = {HamSpirit::CLR_HS_CYAN.r, HamSpirit::CLR_HS_CYAN.g, HamSpirit::CLR_HS_CYAN.b, HamSpirit::CLR_HS_CYAN.a};
constexpr SDL_Color CLR_YELLOW    = {HamSpirit::CLR_HS_YELLOW.r, HamSpirit::CLR_HS_YELLOW.g, HamSpirit::CLR_HS_YELLOW.b, HamSpirit::CLR_HS_YELLOW.a};
constexpr SDL_Color CLR_WHITE     = {HamSpirit::CLR_HS_WHITE.r, HamSpirit::CLR_HS_WHITE.g, HamSpirit::CLR_HS_WHITE.b, HamSpirit::CLR_HS_WHITE.a};
constexpr SDL_Color CLR_GRAY      = {HamSpirit::CLR_HS_GRAY.r, HamSpirit::CLR_HS_GRAY.g, HamSpirit::CLR_HS_GRAY.b, HamSpirit::CLR_HS_GRAY.a};
constexpr SDL_Color CLR_DARK_GRAY = {HamSpirit::CLR_HS_DARK_GRAY.r, HamSpirit::CLR_HS_DARK_GRAY.g, HamSpirit::CLR_HS_DARK_GRAY.b, HamSpirit::CLR_HS_DARK_GRAY.a};
constexpr SDL_Color CLR_GREEN     = {HamSpirit::CLR_HS_GREEN.r, HamSpirit::CLR_HS_GREEN.g, HamSpirit::CLR_HS_GREEN.b, HamSpirit::CLR_HS_GREEN.a};
constexpr SDL_Color CLR_FIELD_BG  = {HamSpirit::CLR_HS_FIELD_BG.r, HamSpirit::CLR_HS_FIELD_BG.g, HamSpirit::CLR_HS_FIELD_BG.b, HamSpirit::CLR_HS_FIELD_BG.a};
constexpr SDL_Color CLR_PANEL_BRD = {HamSpirit::CLR_HS_PANEL_BRD.r, HamSpirit::CLR_HS_PANEL_BRD.g, HamSpirit::CLR_HS_PANEL_BRD.b, HamSpirit::CLR_HS_PANEL_BRD.a};
constexpr SDL_Color CLR_HIGHLIGHT = {HamSpirit::CLR_HS_HIGHLIGHT.r, HamSpirit::CLR_HS_HIGHLIGHT.g, HamSpirit::CLR_HS_HIGHLIGHT.b, HamSpirit::CLR_HS_HIGHLIGHT.a};
constexpr SDL_Color CLR_BANNER_BG = {HamSpirit::CLR_HS_BANNER_BG.r, HamSpirit::CLR_HS_BANNER_BG.g, HamSpirit::CLR_HS_BANNER_BG.b, HamSpirit::CLR_HS_BANNER_BG.a};

void sdlRenderFrame() {
    if (!sSDL.renderer) return;

    int w = 0, h = 0;
    SDL_GetRendererOutputSize(sSDL.renderer, &w, &h);
    if (w <= 0 || h <= 0) return;

    // Clear
    SDL_SetRenderDrawColor(sSDL.renderer, CLR_BG.r, CLR_BG.g, CLR_BG.b, 255);
    SDL_RenderClear(sSDL.renderer);

    // Read overlay state
    bool menuVis, textVis, inputVis;
    std::string mTitle, tContent, iLabel, iValue, banner;
    std::vector<std::string> mItems;
    int mSel;
    {
        std::lock_guard<std::mutex> lock(sSDL.dataMtx);
        menuVis = sSDL.menuVisible;
        mTitle  = sSDL.menuTitle;
        mItems  = sSDL.menuItems;
        mSel    = sSDL.menuSelected;
        textVis = sSDL.textVisible;
        tContent = sSDL.textContent;
        inputVis = sSDL.inputVisible;
        iLabel   = sSDL.inputLabel;
        iValue   = sSDL.inputValue;
        banner   = sSDL.bannerText;
    }

    // ---- Header ----
    SDL_SetRenderDrawColor(sSDL.renderer, CLR_CYAN.r, CLR_CYAN.g, CLR_CYAN.b, 255);
    SDL_RenderDrawLine(sSDL.renderer, 0, 3, w, 3);
    if (sSDL.fontLarge)
        sdlDrawText("=== HAM SPIRIT ===", w / 2, 10, sSDL.fontLarge, CLR_CYAN, true);
    SDL_RenderDrawLine(sSDL.renderer, 0, 45, w, 45);

    int contentY = 60;

    // ---- Text input overlay (highest priority) ----
    if (inputVis && sSDL.fontNormal) {
        int panelW = std::min(w - 80, 600);
        int panelH = 130;
        int panelX = (w - panelW) / 2;
        int panelY = (h - panelH) / 2;

        sdlFillRect(panelX, panelY, panelW, panelH, CLR_BG);
        sdlDrawRect(panelX, panelY, panelW, panelH, CLR_CYAN);

        // Label
        sdlDrawText(iLabel, w / 2, panelY + 15, sSDL.fontNormal, CLR_CYAN, true);

        // Input field
        int fX = panelX + 24, fW = panelW - 48, fY = panelY + 60, fH = 44;
        sdlFillRect(fX, fY, fW, fH, CLR_FIELD_BG);
        sdlDrawRect(fX, fY, fW, fH, CLR_PANEL_BRD);

        // Value with blinking cursor
        bool cursorOn = HamSpirit::isCursorVisible(static_cast<float>(SDL_GetTicks()) / 1000.0f);
        std::string display = iValue + (cursorOn ? "_" : " ");
        sdlDrawText(display, fX + 10, fY + 10, sSDL.fontNormal, CLR_WHITE);

    // ---- Text overlay ----
    } else if (textVis && sSDL.fontNormal) {
        // Word-wrap and center
        int maxW = std::min(w - 80, 700);
        int charW = 0, charH = 0;
        TTF_SizeUTF8(sSDL.fontNormal, "M", &charW, &charH);
        if (charW <= 0) charW = 10;
        int charsPerLine = maxW / charW;

        std::string remaining = tContent;
        int row = contentY;
        while (!remaining.empty() && row < h - 60) {
            std::string line;
            if (static_cast<int>(remaining.size()) <= charsPerLine) {
                line = remaining;
                remaining.clear();
            } else {
                size_t brk = remaining.rfind(' ', charsPerLine);
                if (brk == std::string::npos || brk == 0) brk = charsPerLine;
                line = remaining.substr(0, brk);
                // Skip past the space at break point so the next line doesn't start with one
                size_t next = brk + (brk < remaining.size() && remaining[brk] == ' ' ? 1 : 0);
                remaining = remaining.substr(next);
            }
            sdlDrawText(line, w / 2, row, sSDL.fontNormal, CLR_WHITE, true);
            row += charH + 6;
        }

    // ---- Menu overlay ----
    } else if (menuVis && !mItems.empty() && sSDL.fontNormal) {
        // Title
        if (sSDL.fontLarge)
            sdlDrawText(mTitle, w / 2, contentY, sSDL.fontLarge, CLR_CYAN, true);

        SDL_SetRenderDrawColor(sSDL.renderer, CLR_CYAN.r, CLR_CYAN.g, CLR_CYAN.b, 255);
        SDL_RenderDrawLine(sSDL.renderer, w / 2 - 160, contentY + 38, w / 2 + 160, contentY + 38);

        int charH = 0;
        TTF_SizeUTF8(sSDL.fontNormal, "M", nullptr, &charH);
        int lineH = charH + 12;
        int itemY = contentY + 52;

        for (int i = 0; i < static_cast<int>(mItems.size()); i++) {
            if (itemY >= h - 60) break;
            if (i == mSel) {
                // Highlight bar
                int textW = 0;
                TTF_SizeUTF8(sSDL.fontNormal, mItems[i].c_str(), &textW, nullptr);
                int barW = textW + 60;
                sdlFillRect(w / 2 - barW / 2, itemY - 2, barW, lineH, CLR_HIGHLIGHT);
                sdlDrawRect(w / 2 - barW / 2, itemY - 2, barW, lineH, CLR_YELLOW);
                std::string label = "> " + mItems[i] + " <";
                sdlDrawText(label, w / 2, itemY + 2, sSDL.fontNormal, CLR_YELLOW, true);
            } else {
                sdlDrawText(mItems[i], w / 2, itemY + 2, sSDL.fontNormal, CLR_GRAY, true);
            }
            itemY += lineH;
        }
    }

    // ---- Banner at bottom ----
    if (!banner.empty() && sSDL.fontSmall) {
        sdlFillRect(0, h - 38, w, 38, CLR_BANNER_BG);
        sdlDrawText(banner, 12, h - 33, sSDL.fontSmall, CLR_GREEN);
    }

    // ---- Footer ----
    if (sSDL.fontSmall)
        sdlDrawText("Arrow keys: Navigate | Enter/A: Select | Escape/B: Back",
                    12, h - 16, sSDL.fontSmall, CLR_DARK_GRAY);

    SDL_RenderPresent(sSDL.renderer);
}

// Background render thread — redraws at ~30fps and pumps SDL events
void sdlRenderLoop() {
    while (sSDL.renderRunning.load()) {
        // Pump events to keep the window responsive
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            // Ignore all events — input comes from the terminal
            if (ev.type == SDL_QUIT) {
                // Don't quit — game lifecycle is managed by the game loop
            }
        }
        sdlRenderFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

} // anonymous namespace

bool startHamSpiritWindow() {
    if (sSDL.active.load()) return sSDL.window != nullptr;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
    if (TTF_Init() < 0) { SDL_Quit(); return false; }

    sSDL.window = SDL_CreateWindow(
        "Ham Spirit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 540,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!sSDL.window) { TTF_Quit(); SDL_Quit(); return false; }

    sSDL.renderer = SDL_CreateRenderer(sSDL.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sSDL.renderer)
        sSDL.renderer = SDL_CreateRenderer(sSDL.window, -1, SDL_RENDERER_SOFTWARE);
    if (!sSDL.renderer) {
        SDL_DestroyWindow(sSDL.window); sSDL.window = nullptr;
        TTF_Quit(); SDL_Quit();
        return false;
    }

    // Load fonts at multiple sizes
    sSDL.fontNormal = sdlLoadSystemFont(18);
    sSDL.fontLarge  = sdlLoadSystemFont(26);
    sSDL.fontSmall  = sdlLoadSystemFont(13);

    if (!sSDL.fontNormal) {
        // No usable font — can't render text, give up
        SDL_DestroyRenderer(sSDL.renderer); sSDL.renderer = nullptr;
        SDL_DestroyWindow(sSDL.window); sSDL.window = nullptr;
        TTF_Quit(); SDL_Quit();
        return false;
    }

    SDL_MaximizeWindow(sSDL.window);
    sSDL.active.store(true);

    // Start background render thread
    sSDL.renderRunning.store(true);
    sSDL.renderThread = std::thread(sdlRenderLoop);

    return true;
}

void stopHamSpiritWindow() {
    sSDL.renderRunning.store(false);
    if (sSDL.renderThread.joinable()) sSDL.renderThread.join();

    if (sSDL.fontSmall)  { TTF_CloseFont(sSDL.fontSmall);  sSDL.fontSmall  = nullptr; }
    if (sSDL.fontLarge)  { TTF_CloseFont(sSDL.fontLarge);  sSDL.fontLarge  = nullptr; }
    if (sSDL.fontNormal) { TTF_CloseFont(sSDL.fontNormal); sSDL.fontNormal = nullptr; }
    if (sSDL.renderer) { SDL_DestroyRenderer(sSDL.renderer); sSDL.renderer = nullptr; }
    if (sSDL.window)   { SDL_DestroyWindow(sSDL.window);   sSDL.window   = nullptr; }
    TTF_Quit();
    SDL_Quit();
    sSDL.active.store(false);
}

struct HamSpiritWindowGuard {
    bool enabled;
    explicit HamSpiritWindowGuard(bool enabledValue) : enabled(enabledValue) {}
    HamSpiritWindowGuard(const HamSpiritWindowGuard&) = delete;
    HamSpiritWindowGuard& operator=(const HamSpiritWindowGuard&) = delete;
    ~HamSpiritWindowGuard() {
        if (enabled) stopHamSpiritWindow();
    }
};

void pushBannerText(const std::string& text) {
    std::lock_guard<std::mutex> lock(sSDL.dataMtx);
    sSDL.bannerText = text;
}

void updateMenuOverlay(const std::string& title,
                       const std::vector<std::string>& items,
                       int selectedIndex) {
    std::lock_guard<std::mutex> lock(sSDL.dataMtx);
    sSDL.menuVisible = true;
    sSDL.menuTitle = title;
    sSDL.menuItems = items;
    sSDL.menuSelected = selectedIndex;
}

void hideMenuOverlay() {
    std::lock_guard<std::mutex> lock(sSDL.dataMtx);
    sSDL.menuVisible = false;
}

void showTextOverlay(const std::string& text) {
    std::lock_guard<std::mutex> lock(sSDL.dataMtx);
    sSDL.textVisible = true;
    sSDL.textContent = text;
}

void hideTextOverlay() {
    std::lock_guard<std::mutex> lock(sSDL.dataMtx);
    sSDL.textVisible = false;
}

void showTextInputOverlay(const std::string& label, const std::string& value) {
    std::lock_guard<std::mutex> lock(sSDL.dataMtx);
    sSDL.inputVisible = true;
    sSDL.inputLabel = label;
    sSDL.inputValue = value;
}

void hideTextInputOverlay() {
    std::lock_guard<std::mutex> lock(sSDL.dataMtx);
    sSDL.inputVisible = false;
}

#else
// ============================================================================
// Terminal-based ANSI GUI fallback (when SDL2 is not available)
// ============================================================================
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

struct TermSize { int cols; int rows; };
TermSize getTermSize() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return {ws.ws_col, ws.ws_row};
    }
    return {80, 24};
}

void termClear() { std::printf("\033[2J\033[H"); }
void termMoveTo(int row, int col) { std::printf("\033[%d;%dH", row, col); }
void termResetColor() { std::printf("\033[0m"); }
void termSetColor(int fg, bool bold = false) {
    if (bold) std::printf("\033[1;%dm", fg);
    else std::printf("\033[%dm", fg);
}

void termPrintCentered(int row, const std::string& text, int cols) {
    int pad = std::max(0, (cols - static_cast<int>(text.size())) / 2);
    termMoveTo(row, pad + 1);
    std::printf("%s", text.c_str());
}

void termHLine(int row, int startCol, int width, char c = '-') {
    termMoveTo(row, startCol);
    for (int i = 0; i < width; i++) std::putchar(c);
}

struct TermOverlayState {
    std::mutex mtx;
    bool menuVisible{false};
    std::string menuTitle;
    std::vector<std::string> menuItems;
    int menuSelected{0};
    bool textVisible{false};
    std::string textContent;
    bool inputVisible{false};
    std::string inputLabel;
    std::string inputValue;
    std::string bannerText;
};

TermOverlayState sTermOverlay;

void termRenderOverlay() {
    TermSize ts = getTermSize();
    std::printf("\033[?25l");
    termClear();

    termSetColor(36, true);
    termHLine(1, 1, ts.cols, '=');
    termPrintCentered(2, "=== HAM SPIRIT ===", ts.cols);
    termHLine(3, 1, ts.cols, '=');
    termResetColor();

    int startRow = 5;

    if (sTermOverlay.inputVisible) {
        int boxWidth = std::min(ts.cols - 4, 60);
        int boxStart = (ts.cols - boxWidth) / 2;
        int boxRow = ts.rows / 2 - 2;

        termSetColor(36, true);
        termMoveTo(boxRow, boxStart);
        std::putchar('+');
        for (int i = 0; i < boxWidth - 2; i++) std::putchar('-');
        std::putchar('+');

        termMoveTo(boxRow + 1, boxStart);
        std::printf("| ");
        termSetColor(37, true);
        std::string label = sTermOverlay.inputLabel;
        if (static_cast<int>(label.size()) > boxWidth - 4) label = label.substr(0, boxWidth - 4);
        std::printf("%-*s", boxWidth - 4, label.c_str());
        termSetColor(36, true);
        std::printf(" |");

        termMoveTo(boxRow + 2, boxStart);
        std::printf("| ");
        termSetColor(33, true);
        std::string val = sTermOverlay.inputValue + "_";
        if (static_cast<int>(val.size()) > boxWidth - 4) val = val.substr(0, boxWidth - 4);
        std::printf("%-*s", boxWidth - 4, val.c_str());
        termSetColor(36, true);
        std::printf(" |");

        termMoveTo(boxRow + 3, boxStart);
        std::putchar('+');
        for (int i = 0; i < boxWidth - 2; i++) std::putchar('-');
        std::putchar('+');
        termResetColor();

    } else if (sTermOverlay.textVisible) {
        termSetColor(37, true);
        std::string text = sTermOverlay.textContent;
        int maxWidth = std::min(ts.cols - 6, 70);
        int row = startRow;
        while (!text.empty() && row < ts.rows - 2) {
            std::string line;
            if (static_cast<int>(text.size()) <= maxWidth) {
                line = text;
                text.clear();
            } else {
                size_t breakPos = text.rfind(' ', maxWidth);
                if (breakPos == std::string::npos || breakPos == 0) breakPos = maxWidth;
                line = text.substr(0, breakPos);
                text = text.substr(breakPos + (text[breakPos] == ' ' ? 1 : 0));
            }
            termPrintCentered(row++, line, ts.cols);
        }
        termResetColor();

    } else if (sTermOverlay.menuVisible && !sTermOverlay.menuItems.empty()) {
        termSetColor(36, true);
        termPrintCentered(startRow, sTermOverlay.menuTitle, ts.cols);
        termResetColor();

        termSetColor(36);
        termHLine(startRow + 1, (ts.cols - 40) / 2, 40, '-');
        termResetColor();

        for (int i = 0; i < static_cast<int>(sTermOverlay.menuItems.size()); i++) {
            int row = startRow + 3 + i;
            if (row >= ts.rows - 2) break;
            std::string item;
            if (i == sTermOverlay.menuSelected) {
                item = "  > " + sTermOverlay.menuItems[i] + " <  ";
                termSetColor(33, true);
            } else {
                item = "    " + sTermOverlay.menuItems[i];
                termSetColor(37);
            }
            termPrintCentered(row, item, ts.cols);
            termResetColor();
        }
    }

    if (!sTermOverlay.bannerText.empty()) {
        termMoveTo(ts.rows - 1, 1);
        termSetColor(32);
        std::string banner = sTermOverlay.bannerText;
        if (static_cast<int>(banner.size()) > ts.cols) banner = banner.substr(0, ts.cols);
        std::printf("%s", banner.c_str());
        termResetColor();
    }

    termMoveTo(ts.rows, 1);
    termSetColor(90);
    std::printf("Arrow keys: Navigate | Enter/A: Select | Escape/B: Back");
    termResetColor();

    std::fflush(stdout);
}

} // anonymous namespace

void pushBannerText(const std::string& text) {
    std::lock_guard<std::mutex> lock(sTermOverlay.mtx);
    sTermOverlay.bannerText = text;
    termRenderOverlay();
}

void updateMenuOverlay(const std::string& title,
                       const std::vector<std::string>& items,
                       int selectedIndex) {
    std::lock_guard<std::mutex> lock(sTermOverlay.mtx);
    sTermOverlay.menuVisible = true;
    sTermOverlay.menuTitle = title;
    sTermOverlay.menuItems = items;
    sTermOverlay.menuSelected = selectedIndex;
    termRenderOverlay();
}

void hideMenuOverlay() {
    std::lock_guard<std::mutex> lock(sTermOverlay.mtx);
    sTermOverlay.menuVisible = false;
    termRenderOverlay();
}

void showTextOverlay(const std::string& text) {
    std::lock_guard<std::mutex> lock(sTermOverlay.mtx);
    sTermOverlay.textVisible = true;
    sTermOverlay.textContent = text;
    termRenderOverlay();
}

void hideTextOverlay() {
    std::lock_guard<std::mutex> lock(sTermOverlay.mtx);
    sTermOverlay.textVisible = false;
    termRenderOverlay();
}

void showTextInputOverlay(const std::string& label, const std::string& value) {
    std::lock_guard<std::mutex> lock(sTermOverlay.mtx);
    sTermOverlay.inputVisible = true;
    sTermOverlay.inputLabel = label;
    sTermOverlay.inputValue = value;
    termRenderOverlay();
}

void hideTextInputOverlay() {
    std::lock_guard<std::mutex> lock(sTermOverlay.mtx);
    sTermOverlay.inputVisible = false;
    termRenderOverlay();
}

bool startHamSpiritWindow() {
    termClear();
    std::printf("\033[?25l");
    std::fflush(stdout);
    return true;
}

void stopHamSpiritWindow() {
    std::printf("\033[?25h");
    termClear();
    std::fflush(stdout);
}

struct HamSpiritWindowGuard {
    bool enabled;
    explicit HamSpiritWindowGuard(bool enabledValue) : enabled(enabledValue) {}
    HamSpiritWindowGuard(const HamSpiritWindowGuard&) = delete;
    HamSpiritWindowGuard& operator=(const HamSpiritWindowGuard&) = delete;
    ~HamSpiritWindowGuard() {
        if (enabled) stopHamSpiritWindow();
    }
};

#endif // HAVE_NATIVE_GUI / HAVE_SDL2 / terminal fallback
#endif // _WIN32

float adjustUnUnMagnitude(float mag, float multiplier) {
    if (multiplier <= 1.0f) {
        return mag;
    }
    return mag > kRefImpedanceOhms ? mag / multiplier : mag * multiplier;
}

const char* ununRatioLabel(HamSpirit::UnUn::Ratio ratio) {
    switch (ratio) {
        case HamSpirit::UnUn::Ratio::RATIO_1_1: return "1:1";
        case HamSpirit::UnUn::Ratio::RATIO_4_1: return "4:1";
        case HamSpirit::UnUn::Ratio::RATIO_9_1: return "9:1";
        case HamSpirit::UnUn::Ratio::RATIO_16_1: return "16:1";
        default: return "1:1";
    }
}
}

namespace HamSpirit {

// Constants
static constexpr float PI = 3.14159265359f;
static constexpr float TWO_PI = 2.0f * PI;

// Audio constants
static constexpr int GAME_SAMPLE_RATE = 44100;
static constexpr int GAME_CHANNELS = 2;
static constexpr int GAME_BITS = 16;
static constexpr int GAME_AUDIO_FRAME_MS = 20;  // 20ms audio frames (~50 fps audio, low latency)
static constexpr int GAME_AUDIO_SAMPLES = GAME_SAMPLE_RATE * GAME_AUDIO_FRAME_MS / 1000;
// Helper: convert milliseconds to audio frame count (round up to avoid zero)
static constexpr int msToFrames(int ms) { return (ms + GAME_AUDIO_FRAME_MS - 1) / GAME_AUDIO_FRAME_MS; }
// Scale factor for per-frame audio calculations (normalized so formulas stay correct
// regardless of GAME_AUDIO_FRAME_MS).  With 20ms frames this is 1.0.
static constexpr float FRAME_SCALE = 20.0f / static_cast<float>(GAME_AUDIO_FRAME_MS);
static constexpr float MOTOR_BASE_FREQ = 220.0f;   // Motor base frequency (A3)
static constexpr float MOTOR_MAX_FREQ = 1320.0f;    // Motor max frequency (E6) — higher range for 10 kHz speed cap
static constexpr float SWR_GOOD_FREQ = 880.0f;      // SWR=1.0 tone (A5)
static constexpr float SWR_BAD_FREQ = 220.0f;       // SWR=10+ tone (A3)
static constexpr float MORSE_SIGNAL_FREQ = 600.0f;   // Morse signal beep frequency (unified with sidetone)
static constexpr float MORSE_CANNON_FREQ = 600.0f;   // Morse cannon dit/dah frequency
static constexpr float MORSE_SIDETONE_FREQ = 600.0f;  // CW sidetone frequency (same for dot and dash)
static constexpr float STANDSTILL_THRESHOLD = 0.005f; // Speed below this = standing still
static constexpr int AUDIO_CONSECUTIVE_FAIL_RECOVERY = 3; // Attempt backend recovery after this many consecutive playBuffer failures (low for fast focus-change recovery)
// Psychoacoustic constants
static constexpr float PSYCHOACOUSTIC_LP_ALPHA = 0.35f;  // IIR low-pass alpha: simulates head-shadow attenuation for behind-listener sounds

// Speed and frequency constants
static constexpr float DEFAULT_KHZ_PER_RADIAN = 1.0f;
static constexpr float BASE_MAX_SPEED_KHZ = 10.0f;             // Max speed in kHz/s (was 25 — reduced for human reaction time)
// Track boundary and QSO Störer constants  
static constexpr float CRASH_SWR_GRADIENT_THRESHOLD = 1.0f;
// Xbox 360 vibration motor compensation — right motor is physically smaller/weaker
// than the left motor. Scale right motor drive value up to produce perceptually
// equivalent intensity. Empirical value based on Xbox 360 ERM motor ratio.
static constexpr float RIGHT_MOTOR_COMPENSATION = 1.4f;
static constexpr float QSO_STOERER_SPEED_MULTIPLIER = 1.05f;   // Chases at 105% player speed (was 110% — more beatable)
static constexpr int MAX_EVENT_SPAWN_DIST_POINTS = 4;
static constexpr float QSO_STOERER_BASE_INTERVAL = 120.0f;     // Base spawn interval seconds (was 90 — more breathing room)
static constexpr float QSO_STOERER_DIFFICULTY_FACTOR = 9.0f;    // Seconds reduced per difficulty level
static constexpr float QSO_STOERER_MIN_INTERVAL = 60.0f;       // Minimum spawn interval (was 45 — less pressure)
static constexpr float QSO_STOERER_COLLISION_COOLDOWN = 2.0f;   // Seconds between collision damage
static constexpr float QSO_STOERER_COLLISION_DAMAGE = 0.03f;    // PA damage per collision (3%)
static constexpr float QSO_STOERER_DESTRUCTION_THRESHOLD = 0.3f; // Health below this = destroyed
static constexpr float QSO_STOERER_HIT_MARGIN = 0.4f;           // Aim tolerance for noise blanker hit (was 0.3 — more forgiving)
static constexpr float QSO_STOERER_HEALTH_PER_HIT = 0.25f;      // Health reduction per noise blanker hit (was 20% — 25% = 3 hits to neutralize)
static constexpr float QSO_STOERER_LATERAL_SPEED = 0.4f;        // How fast Störer shifts laterally (was 0.8 — slower = easier to dodge)
static constexpr float QSO_STOERER_LATERAL_DODGE = 0.5f;        // Lateral distance to clear for overtaking (was 0.3 — wider pass window)
static constexpr float QSO_STOERER_WOBBLE_FREQ = 8.0f;         // Swerve oscillation Hz during driving error
static constexpr float QSO_STOERER_WOBBLE_AMP = 1.5f;          // Swerve amplitude during driving error
static constexpr float STEREO_NARROW_RATE = 3.0f;               // How quickly stereo field narrows with aim lock

// Power-up system constants
static constexpr float POWERUP_ZONE_HALF_WIDTH = 0.35f;         // Half-width of power-up zone (radians) — wider for better audibility
static constexpr float POWERUP_SPAWN_DISTANCE = 0.15f;          // Spawn distance ahead of player (radians) — very close, immediately audible
static constexpr float POWERUP_SPAWN_INTERVAL_MIN = 25.0f;      // Min seconds between spawns (more frequent for better pickup chances)
static constexpr float POWERUP_SPAWN_INTERVAL_MAX = 55.0f;      // Max seconds between spawns
static constexpr float POWERUP_LIFETIME = 60.0f;                // How long a power-up zone persists (seconds)
static constexpr int   POWERUP_MAX_CONCURRENT = 2;              // Max power-ups on the track at once
static constexpr float POWERUP_HEARING_RANGE_MULT = 4.0f;       // Hearing range = zoneHalfWidth * this (hear PUs from further away)
static constexpr float POWERUP_COLLECT_TIME_Q1 = 1.2f;          // Collection hold time: quality 1
static constexpr float POWERUP_COLLECT_TIME_Q2 = 2.2f;          // Collection hold time: quality 2
static constexpr float POWERUP_COLLECT_TIME_Q3 = 3.5f;          // Collection hold time: quality 3 (max)
static constexpr float POWERUP_COLLECT_TIME_MAX = 4.0f;         // Cap for effective collection time (seconds)
static constexpr float POWERUP_COLLECT_TIME_MIN = 0.1f;         // Floor for effective collection time (avoid division by zero)
static constexpr float POWERUP_SPEED_BOOST_FACTOR = 1.3f;       // 30% speed increase
static constexpr float POWERUP_FIRE_RATE_FACTOR = 0.5f;         // 50% cooldown reduction
static constexpr float POWERUP_DURATION_EXTENSION = 20.0f;      // Permanent duration extension (seconds)
static constexpr float POWERUP_EXPLOSION_MAX_DAMAGE = 0.5f;     // Max PA damage from center explosion (50%)
static constexpr float POWERUP_AIM_MARGIN = 0.4f;               // Aiming tolerance for targeting
static constexpr float POWERUP_TRIGGER_THRESHOLD = 0.3f;        // Trigger axis threshold for collection
static constexpr float POWERUP_COLLECT_DEBOUNCE = 0.3f;         // After releasing dual-trigger, ignore single trigger for this long
static constexpr float POWERUP_INVULN_TIME = 1.5f;              // Grace period after spawn before it can be shot
static constexpr float POWERUP_OUT_OF_RANGE_TTS_COOLDOWN = 5.0f;// Min seconds between "drive closer" TTS messages

// Analog stick input thresholds
// Default deadzone is now configurable via config.inputDeadzone (default 0.08f)
// These constants provide fallback values and compile-time reference only
static constexpr float DEFAULT_INPUT_DEADZONE = 0.08f;           // Default analog stick deadzone
// Maximum distance (track radians) at which other players' events are audible
static constexpr float MAX_AUDIO_EVENT_DISTANCE = 2.0f;
// Audio thread recovery
static constexpr int MAX_AUDIO_CRASH_RECOVERY = 20;             // Max auto-restarts before audio thread gives up
// SWR L/C sound effect tuning
static constexpr float WOBBLE_RESAMPLE_DEPTH = 8.0f;            // Sample offset depth for inductive pitch wobble
static constexpr float SWR_REACTANCE_THRESHOLD = 0.5f;          // Reactance (Ohms) above which L/C effect is applied

// PA thermal damage model constants
static constexpr float PA_THERMAL_CAPACITY = 5.0f;              // Seconds to reach critical temp at full reflected power
static constexpr float PA_COOLING_RATE = 0.03f;                 // Passive cooling per second (heatsink model)
static constexpr float PA_SAFE_SWR = 2.0f;                      // Below this SWR, no significant thermal stress
static constexpr float PA_DAMAGE_THERMAL_THRESHOLD = 0.3f;      // Thermal load above this causes permanent damage
static constexpr float PA_DAMAGE_RATE_FACTOR = 0.02f;           // Base damage rate multiplier for thermal runaway
static constexpr float PA_REGEN_RATE = 0.005f;                  // Very slow health regeneration when cool
static constexpr float PA_REGEN_THERMAL_THRESHOLD = 0.1f;       // Must be cooler than this for regen
static constexpr float PA_THERMAL_MAX = 2.0f;                   // Maximum thermal load cap
static constexpr float PA_DT_CLAMP = 0.1f;                      // Max delta-time per tick (100ms) to prevent damage spikes
static constexpr float PA_SWR_AVERAGE_ALPHA = 0.95f;            // Exponential average factor for SWR tracking

// PA damage stage thresholds (health fraction boundaries)
static constexpr float PA_STAGE_1_THRESHOLD = 0.9f;             // Light damage begins
static constexpr float PA_STAGE_2_THRESHOLD = 0.7f;             // Moderate damage
static constexpr float PA_STAGE_3_THRESHOLD = 0.5f;             // Heavy damage
static constexpr float PA_STAGE_4_THRESHOLD = 0.3f;             // Critical damage
static constexpr float PA_STAGE_5_THRESHOLD = 0.1f;             // Failing — imminent destruction
// PA damage speed reduction per stage
static constexpr float PA_SPEED_MULT_STAGE_1 = 0.90f;           // 90% max speed at stage 1
static constexpr float PA_SPEED_MULT_STAGE_2 = 0.75f;           // 75% at stage 2
static constexpr float PA_SPEED_MULT_STAGE_3 = 0.55f;           // 55% at stage 3
static constexpr float PA_SPEED_MULT_STAGE_4 = 0.35f;           // 35% at stage 4
static constexpr float PA_SPEED_MULT_STAGE_5 = 0.15f;           // 15% at stage 5 (barely moving)

// Border collision and warning constants
static constexpr float BORDER_WARNING_ZONE_START = 0.4f;        // Lateral offset where warnings begin (0.0-1.0)
static constexpr float BORDER_CRASH_SPEED_MULT = 0.3f;          // Speed multiplier on curve crash
static constexpr float BORDER_SCRAPE_SPEED_MULT = 0.85f;        // Speed multiplier on barrier scrape
static constexpr float BORDER_RECOVERY_SPEED_MULT = 0.5f;       // Speed multiplier during crash recovery
static constexpr float BORDER_CRASH_PA_DAMAGE = 0.05f;          // PA health damage from curve crash
static constexpr float BORDER_SCRAPE_PA_DAMAGE = 0.005f;        // PA health damage from barrier scrape
static constexpr float BORDER_CRASH_BOUNCE_OFFSET = 0.55f;      // Lateral offset after curve crash bounce
static constexpr float BORDER_SCRAPE_BOUNCE_OFFSET = 0.65f;     // Lateral offset after scrape bounce
static constexpr float BORDER_CRASH_RECOVERY_TIME = 1.0f;       // Seconds of recovery after crash
static constexpr float BORDER_CRASH_VIB_DURATION = 0.4f;        // Vibration duration for crash (seconds)
static constexpr float BORDER_SCRAPE_VIB_DURATION = 0.2f;       // Vibration duration for scrape (seconds)
static constexpr float BORDER_VIB_MIN_INTENSITY = 0.15f;        // Minimum vibration intensity in warning zone
static constexpr float BORDER_WARNING_SPEED_FACTOR = 0.3f;      // Speed factor added to warning strength
static constexpr float BORDER_WARNING_SWR_FACTOR = 0.2f;        // SWR gradient factor added to warning strength
static constexpr float BORDER_WARNING_MIN_STRENGTH = 0.05f;     // Minimum warning strength for beep/vibration

// Noise enemy gameplay constants
static constexpr float NOISE_ENEMY_BW_MIN = 0.1f;               // Minimum bandwidth (narrow/weak)
static constexpr float NOISE_ENEMY_BW_RANGE = 0.7f;             // Bandwidth random range (max = min + range)
static constexpr float NOISE_ENEMY_HEALTH_BW_SCALE = 4.0f;      // Health scaling with bandwidth
static constexpr float NOISE_ENEMY_INTENSITY_BASE = 0.3f;       // Base intensity (volume)
static constexpr float NOISE_ENEMY_INTENSITY_BW_SCALE = 0.7f;   // Intensity scaling with bandwidth
static constexpr float NOISE_ENEMY_HEARING_EXTRA = 0.5f;        // Extra radians beyond bandwidth for hearing
static constexpr float NOISE_ENEMY_INSIDE_VOLUME = 70.0f;       // Peak volume when inside bandwidth
static constexpr float NOISE_ENEMY_FADE_RANGE = 0.5f;           // Fade range beyond bandwidth edge
static constexpr float NOISE_SPAWN_BASE_INTERVAL = 25.0f;       // Base seconds between noise enemy spawns
static constexpr float NOISE_SPAWN_DIFFICULTY_REDUCTION = 2.0f;  // Seconds reduced per difficulty level
static constexpr float NOISE_SPAWN_RANDOM_RANGE = 15.0f;        // Random additional seconds between spawns
static constexpr float NOISE_ANNOUNCE_LEAD_TIME = 3.0f;         // Seconds before spawn to announce

// QSO Störer behavior constants
static constexpr float QSO_STOERER_MIN_SPEED_FRACTION = 0.15f;  // Minimum speed as fraction of max speed
static constexpr float QSO_STOERER_ERROR_SPEED_MULT = 0.4f;     // Speed multiplier during driving error
static constexpr float QSO_STOERER_AHEAD_SPEED_MULT = 0.5f;     // Speed multiplier when ahead (waiting)
static constexpr float QSO_STOERER_GIVE_UP_FACTOR = -1.5f;      // Give up when this many step-angles behind player
static constexpr float QSO_STOERER_ERROR_DURATION = 2.0f;       // Seconds of swerving during driving error
static constexpr int   QSO_STOERER_DRIVING_ERROR_CHANCE = 5;     // Percent chance of driving error per morse send
static constexpr float QSO_STOERER_ERROR_MIN_LATERAL = 0.3f;    // Min lateral offset below which error forces full swerve
static constexpr float QSO_STOERER_ERROR_SWERVE_LATERAL = 0.7f; // Lateral offset forced during driving error
static constexpr float QSO_STOERER_COLLISION_VIB_DURATION = 0.3f; // Vibration duration for Störer collision (seconds)
static constexpr float QSO_STOERER_DESTROY_SCORE = 50.0f;       // Score for destroying QSO Störer
static constexpr float QSO_STOERER_PAN_AMPLIFICATION = 4.0f;    // Angular pan amplification for audio
static constexpr float QSO_STOERER_VOLUME_MAX = 70.0f;          // Peak volume at point-blank
static constexpr float QSO_STOERER_ALONGSIDE_THRESHOLD = 0.3f;  // Step-angle fraction for "alongside" state
static constexpr float QSO_STOERER_ANNOUNCE_COOLDOWN = 2.0f;    // Min seconds between position announcements
static constexpr int   QSO_STOERER_SPAWN_DIST_MIN = 2;          // Min measurement points ahead for spawn
static constexpr int   QSO_STOERER_SPAWN_DIST_RANGE = 3;        // Range of measurement points (min to min+range-1)
static constexpr int   QSO_STOERER_RESPAWN_RANDOM = 30;         // Random additional seconds before respawn

// Morse collection gameplay constants
static constexpr float MORSE_AIM_MARGIN_NORMAL = 0.3f;          // Aim tolerance for morse collection (radians)
static constexpr float MORSE_PA_REPAIR_SHORT = 0.05f;           // PA repair for short patterns (1-2 elements)
static constexpr float MORSE_PA_REPAIR_MEDIUM = 0.08f;          // PA repair for medium patterns (3 elements)
static constexpr float MORSE_PA_REPAIR_LONG = 0.12f;            // PA repair for long patterns (4+ elements)
static constexpr int   MORSE_COLLECTION_SCORE_PER = 50;         // Score per collected morse character
static constexpr int   MORSE_HAMSPIRIT_BONUS = 500;             // Bonus for collecting all HAMSPIRIT letters
static constexpr int   MORSE_MISS_THRESHOLD = 3;                // Consecutive misses before signal is removed
static constexpr int   MORSE_MISS_PENALTY = 20;                 // Score penalty when signal removed from misses
static constexpr int   MORSE_QSO_DISMISS_SCORE = 73;            // Score for dismissing QSO Störer with "99"

// Noise blanker hit detection constants
static constexpr float NOISE_BLANKER_BW_HIT_EXTRA = 0.1f;       // Extra radians added to bandwidth for hit detection
static constexpr int   NOISE_DESTROY_BONUS_BASE = 50;           // Base score for destroying noise enemy
static constexpr int   NOISE_DESTROY_BONUS_BW_SCALE = 75;       // Bonus score scaling with inverse bandwidth
static constexpr int   NOISE_BLANKER_QSO_HIT_SCORE = 25;        // Score for hitting QSO Störer
static constexpr int   NOISE_BLANKER_MORSE_HIT_PENALTY = 25;    // Score penalty for hitting morse signal
static constexpr float NOISE_BLANKER_MORSE_AIM_MARGIN = 0.4f;   // Aim margin for morse signal hit (penalty)

// Power-up spawn type probability distribution (cumulative percentages)
static constexpr int POWERUP_CHANCE_SPEED_BOOST = 25;            // 25% chance for speed boost
static constexpr int POWERUP_CHANCE_FIRE_RATE = 50;              // 25% chance for fire rate (cumulative 50%)
static constexpr int POWERUP_CHANCE_AUTO_FIRE = 70;              // 20% chance for auto fire (cumulative 70%)
static constexpr int POWERUP_CHANCE_SWR_IMMUNITY = 85;           // 15% chance for SWR immunity (cumulative 85%)
                                                                  // remaining 15% = duration extend
// Power-up activation balance
static constexpr int   POWERUP_ACTIVATION_SCORE_PER_QUALITY = 100; // Score per quality tier on activation
static constexpr float POWERUP_SPEED_BOOST_QUALITY_STEP = 0.1f;   // Extra speed boost per quality tier above 1
static constexpr float POWERUP_FIRE_RATE_QUALITY_STEP = 0.1f;     // Extra cooldown reduction per quality tier
static constexpr float POWERUP_FIRE_RATE_MIN = 0.2f;              // Minimum fire rate factor (cap)
static constexpr float POWERUP_EXPIRE_WARNING_TIME = 5.0f;        // Seconds before expiry to warn player

// Emergency brake constants
static constexpr float EMERGENCY_BRAKE_DURATION = 0.8f;          // Total braking duration (seconds)
static constexpr float HEADING_SYNC_DECAY = 0.85f;               // Aim decay factor per frame when heading-sync active
static constexpr float HEADING_SYNC_SNAP_THRESHOLD = 0.01f;      // Aim angle below this snaps to zero
static constexpr float HEADING_SYNC_BREAK_THRESHOLD = 0.15f;     // Stick input above this breaks heading sync

// Entity spawn distance distribution
static constexpr float SPAWN_DIST_MIN_FRACTION = 0.3f;           // Minimum 30% of max distance
static constexpr float SPAWN_DIST_RANGE_FRACTION = 0.4f;         // 40% range (30%-70% of max)
static constexpr float SPAWN_DIST_ABSOLUTE_MIN = 0.05f;          // Minimum spawn distance (radians)

// ============================================================================
// TrackGenerator Implementation
// ============================================================================

std::vector<TrackPoint> TrackGenerator::generateTrack(
    const std::vector<MeasurementPoint>& measurements,
    TrackCurve curveType
) {
    std::vector<TrackPoint> track;
    
    if (measurements.empty()) {
        return track;
    }
    
    size_t numPoints = measurements.size();
    track.reserve(numPoints);
    
    // Distribute measurement points evenly around circle
    for (size_t i = 0; i < numPoints; i++) {
        TrackPoint point;
        
        // Calculate angle for this point (0 to 2π)
        point.angle = (static_cast<float>(i) / static_cast<float>(numPoints)) * TWO_PI;
        
        // Copy measurement values, clamping to physically realistic ranges
        const auto& m = measurements[i];
        point.swr = std::min(static_cast<float>(m.swr), kMaxSWR);
        point.returnLoss = static_cast<float>(m.rl);
        point.resistance = std::max(static_cast<float>(m.R), kMinResistanceOhms);
        point.impedanceMag = static_cast<float>(m.impedance_mag);
        point.reactance = static_cast<float>(m.X);
        point.phase = static_cast<float>(m.phase_deg * PI / 180.0);  // Convert to radians
        point.frequency = static_cast<float>(m.freq);
        
        track.push_back(point);
    }
    
    return track;
}

TrackPoint TrackGenerator::interpolateAt(
    const std::vector<TrackPoint>& track,
    float angle
) {
    if (track.empty()) {
        return TrackPoint();
    }
    
    if (track.size() == 1) {
        return track[0];
    }
    
    // Normalize angle to [0, 2π)
    while (angle < 0.0f) angle += TWO_PI;
    while (angle >= TWO_PI) angle -= TWO_PI;
    
    // Find the two track points surrounding this angle
    size_t idx1 = 0;
    size_t idx2 = 1;
    
    for (size_t i = 0; i < track.size(); i++) {
        if (track[i].angle <= angle) {
            idx1 = i;
            idx2 = (i + 1) % track.size();
        } else {
            break;
        }
    }
    
    // Handle wrap-around at 2π → 0
    float angle1 = track[idx1].angle;
    float angle2 = track[idx2].angle;
    
    if (idx2 == 0) {
        angle2 += TWO_PI;
    }
    
    // Linear interpolation factor
    float t = 0.0f;
    if (angle2 > angle1) {
        t = (angle - angle1) / (angle2 - angle1);
    }
    
    // Interpolate all values
    TrackPoint result;
    result.angle = angle;
    result.swr = track[idx1].swr + t * (track[idx2].swr - track[idx1].swr);
    result.returnLoss = track[idx1].returnLoss + t * (track[idx2].returnLoss - track[idx1].returnLoss);
    result.resistance = track[idx1].resistance + t * (track[idx2].resistance - track[idx1].resistance);
    result.impedanceMag = track[idx1].impedanceMag + t * (track[idx2].impedanceMag - track[idx1].impedanceMag);
    result.reactance = track[idx1].reactance + t * (track[idx2].reactance - track[idx1].reactance);
    result.phase = track[idx1].phase + t * (track[idx2].phase - track[idx1].phase);
    result.frequency = track[idx1].frequency + t * (track[idx2].frequency - track[idx1].frequency);
    
    return result;
}

float TrackGenerator::getCurveValue(
    const MeasurementPoint& point,
    TrackCurve curveType
) {
    switch (curveType) {
        case TrackCurve::SWR:
            return std::min(static_cast<float>(point.swr), kMaxSWR);
        case TrackCurve::RETURN_LOSS:
            return static_cast<float>(point.rl);
        case TrackCurve::IMPEDANCE_MAG:
            return static_cast<float>(point.impedance_mag);
        case TrackCurve::REACTANCE:
            return static_cast<float>(point.X);
        case TrackCurve::PHASE:
            return static_cast<float>(point.phase_deg);
        default:
            return 0.0f;
    }
}

// ============================================================================
// SpatialAudio Implementation
// ============================================================================

SpatialAudio::SpatialAudio() : lastPan(0.0f) {
}

float SpatialAudio::calculatePan(
    const std::vector<TrackPoint>& track,
    float playerAngle,
    float lookAheadDistance
) {
    if (track.empty()) {
        return 0.0f;
    }
    
    // Get average SWR in lookahead region
    float avgSWR = calculateAverageCurveAhead(track, playerAngle, lookAheadDistance, TrackCurve::SWR);
    
    // Map SWR to pan value
    // SWR of 1.0 (perfect) = center (0.0)
    // Higher SWR = more deviation from center
    // We'll use the curve direction to determine left/right
    
    // Get SWR at current position and ahead
    // Use current position for a quick feasibility check to avoid expensive full-track scans.
    TrackPoint currentPoint = TrackGenerator::interpolateAt(track, playerAngle);
    TrackPoint aheadPoint = TrackGenerator::interpolateAt(track, playerAngle + lookAheadDistance);
    
    float swrDiff = aheadPoint.swr - currentPoint.swr;
    
    // Map difference to pan
    // Positive diff (SWR increasing ahead) = pan right
    // Negative diff (SWR decreasing ahead) = pan left
    float pan = std::max(-1.0f, std::min(1.0f, swrDiff * 2.0f));
    
    // Smooth pan changes
    const float smoothFactor = 0.1f;
    pan = lastPan + smoothFactor * (pan - lastPan);
    lastPan = pan;
    
    return pan;
}

float SpatialAudio::calculateAverageCurveAhead(
    const std::vector<TrackPoint>& track,
    float playerAngle,
    float lookAheadDistance,
    TrackCurve curveType
) {
    if (track.empty()) {
        return 0.0f;
    }
    
    const int numSamples = 10;
    float sum = 0.0f;
    
    for (int i = 0; i < numSamples; i++) {
        float angle = playerAngle + (lookAheadDistance * static_cast<float>(i) / static_cast<float>(numSamples));
        TrackPoint point = TrackGenerator::interpolateAt(track, angle);
        
        switch (curveType) {
            case TrackCurve::SWR:
                sum += point.swr;
                break;
            case TrackCurve::RETURN_LOSS:
                sum += point.returnLoss;
                break;
            case TrackCurve::IMPEDANCE_MAG:
                sum += point.impedanceMag;
                break;
            case TrackCurve::REACTANCE:
                sum += point.reactance;
                break;
            case TrackCurve::PHASE:
                sum += point.phase;
                break;
            default:
                // ALL_CURVES is a track-generation meta-value; no single
                // measurement applies, so contribute nothing to the average.
                break;
        }
    }
    
    return sum / static_cast<float>(numSamples);
}

// ============================================================================
// LCTuner Implementation
// ============================================================================

LCTuner::LCTuner() : inductanceUH(0.0f), capacitancePF(0.0f) {
    // Start at zero — effectively bypasses the tuner
    // so a perfect antenna stays at SWR≈1.0
}

bool LCTuner::adjustInductance(float delta) {
    float oldVal = inductanceUH;
    inductanceUH += delta;
    inductanceUH = std::max(0.0f, std::min(MAX_L_UH, inductanceUH));
    return (inductanceUH == oldVal); // true = at limit, bumped
}

void LCTuner::setInductance(float value) {
    inductanceUH = std::clamp(value, 0.0f, MAX_L_UH);
}

bool LCTuner::adjustCapacitance(float delta) {
    float oldVal = capacitancePF;
    capacitancePF += delta;
    capacitancePF = std::max(0.0f, std::min(MAX_C_PF, capacitancePF));
    return (capacitancePF == oldVal); // true = at limit, bumped
}

void LCTuner::setCapacitance(float value) {
    capacitancePF = std::clamp(value, 0.0f, MAX_C_PF);
}

std::complex<float> LCTuner::calculateInputImpedance(float frequency, std::complex<float> loadZ) const {
    // Bypass mode: when both components are at zero, pass through unchanged
    if (inductanceUH < 0.01f && capacitancePF < 0.01f) {
        return loadZ;
    }
    
    // Guard against invalid frequency
    if (frequency <= 0.0f || !std::isfinite(frequency)) {
        if (tryLogMathWarning(sTunerMathWarningCount)) {
            logHamSpiritMath("Tuner invalid frequency: " + std::to_string(frequency));
        }
        return loadZ;
    }
    
    float omega = 2.0f * PI * frequency;
    std::complex<float> result = loadZ;
    
    // Apply shunt C if capacitance is set (> 0.01 pF)
    if (capacitancePF >= 0.01f) {
        float C = capacitancePF * 1e-12f; // pF to F
        float XC = 1.0f / (omega * C);    // Capacitive reactance
        std::complex<float> ZC(0.0f, -XC);
        // Parallel combination: result = result || ZC
        std::complex<float> denom = result + ZC;
        if (std::abs(denom) <= kMinComplexDenominator) {
            if (tryLogMathWarning(sTunerMathWarningCount)) {
                logHamSpiritMath("Tuner shunt denom too small: " + std::to_string(std::abs(denom)));
            }
        } else {
            result = (result * ZC) / denom;
        }
    }
    
    // Apply series L if inductance is set (> 0.01 µH)
    if (inductanceUH >= 0.01f) {
        float L = inductanceUH * 1e-6f;  // µH to H
        float XL = omega * L;             // Inductive reactance
        std::complex<float> ZL(0.0f, XL);
        result = ZL + result;
    }
    
    // Final NaN/Inf guard
    if (!std::isfinite(result.real()) || !std::isfinite(result.imag())) {
        if (tryLogMathWarning(sTunerMathWarningCount)) {
            logHamSpiritMath("Tuner impedance not finite: R=" + std::to_string(result.real()) +
                             " X=" + std::to_string(result.imag()));
        }
        return loadZ;
    }
    
    return result;
}

// ============================================================================
// UnUn Implementation
// ============================================================================

UnUn::UnUn() : currentRatio(Ratio::RATIO_1_1) {
}

void UnUn::setRatio(Ratio ratio) {
    currentRatio = ratio;
}

float UnUn::getMultiplier(Ratio ratio) {
    switch (ratio) {
        case Ratio::RATIO_1_1: return 1.0f;
        case Ratio::RATIO_4_1: return 4.0f;
        case Ratio::RATIO_9_1: return 9.0f;
        case Ratio::RATIO_16_1: return 16.0f;
        default: return 1.0f;
    }
}

std::complex<float> UnUn::transform(std::complex<float> impedance) const {
    float multiplier = getMultiplier(currentRatio);
    if (multiplier <= 1.0f) {
        return impedance;
    }
    float mag = std::abs(impedance);
    if (mag > kRefImpedanceOhms) {
        return impedance / multiplier;  // Step-down for high impedances
    }
    return impedance * multiplier;      // Step-up for low impedances
}

// ============================================================================
// AntennaNetwork Implementation
// ============================================================================

AntennaNetwork::AntennaNetwork() {
}

float AntennaNetwork::calculateAdjustedSWR(const std::vector<TrackPoint>& track, float playerAngle) const {
    if (track.empty()) {
        return 1.0f;
    }
    
    // Get current track point
    TrackPoint currentPoint = TrackGenerator::interpolateAt(track, playerAngle);
    
    // Use actual resistance (real part), not impedance magnitude
    const float Z0 = 50.0f;
    float R = currentPoint.resistance;
    float X = currentPoint.reactance;
    if (!std::isfinite(R) || !std::isfinite(X)) {
        if (tryLogMathWarning(sSWRMathWarningCount)) {
            logHamSpiritMath("Track impedance not finite: R=" + std::to_string(R) +
                             " X=" + std::to_string(X));
        }
        R = kMinResistanceOhms;
        X = 0.0f;
    } else {
        R = std::max(R, kMinResistanceOhms);   // Resistance (real part of Z)
    }
    std::complex<float> loadZ(R, X);
    
    // Apply UnUn transformation
    std::complex<float> transformedZ = unun.transform(loadZ);
    
    // Apply L-C tuner
    std::complex<float> finalZ = tuner.calculateInputImpedance(currentPoint.frequency, transformedZ);
    
    // Ensure positive real part for valid SWR calculation
    if (finalZ.real() <= 0.0f) {
        finalZ = std::complex<float>(0.1f, finalZ.imag());
    }
    
    // Calculate reflection coefficient magnitude
    std::complex<float> denom = finalZ + Z0;
    if (std::abs(denom) < kMinComplexDenominator) {
        if (tryLogMathWarning(sSWRMathWarningCount)) {
            std::string imagSign = (finalZ.imag() < 0.0f) ? "" : "+";
            logHamSpiritMath("SWR denom too small: |Z+Z0|=" + std::to_string(std::abs(denom)) +
                             " Z=" + std::to_string(finalZ.real()) + imagSign + std::to_string(finalZ.imag()) + "j");
        }
        denom = std::complex<float>(kMinComplexDenominator, 0.0f);
    }
    std::complex<float> gamma = (finalZ - Z0) / denom;
    float gammaMag = std::abs(gamma);
    
    // Prevent division by zero
    if (!std::isfinite(gammaMag)) {
        if (tryLogMathWarning(sSWRMathWarningCount)) {
            logHamSpiritMath("Gamma magnitude not finite: |Γ|=" + std::to_string(gammaMag));
        }
        gammaMag = kMaxGammaMag;
    }
    if (gammaMag >= kMaxGammaMag) gammaMag = kMaxGammaMag;
    
    // Calculate SWR
    float adjustedSWR = (1.0f + gammaMag) / (1.0f - gammaMag);
    if (!std::isfinite(adjustedSWR)) {
        if (tryLogMathWarning(sSWRMathWarningCount)) {
            logHamSpiritMath("Adjusted SWR not finite; clamping to max.");
        }
        adjustedSWR = kMaxSWR;
    }
    
    // Clamp to physically realistic range for amateur radio
    adjustedSWR = std::max(kMinSWR, std::min(kMaxSWR, adjustedSWR));
    
    return adjustedSWR;
}

float AntennaNetwork::calculateAdjustedReactance(const std::vector<TrackPoint>& track, float playerAngle) const {
    if (track.empty()) return 0.0f;
    
    TrackPoint currentPoint = TrackGenerator::interpolateAt(track, playerAngle);
    float R = currentPoint.resistance;
    float X = currentPoint.reactance;
    if (!std::isfinite(R) || !std::isfinite(X)) {
        R = kMinResistanceOhms;
        X = 0.0f;
    } else {
        R = std::max(R, kMinResistanceOhms);
    }
    std::complex<float> loadZ(R, X);
    
    // Apply UnUn transformation, then L-C tuner
    std::complex<float> transformedZ = unun.transform(loadZ);
    std::complex<float> finalZ = tuner.calculateInputImpedance(currentPoint.frequency, transformedZ);
    
    float adjustedX = finalZ.imag();
    if (!std::isfinite(adjustedX)) adjustedX = 0.0f;
    return adjustedX;
}

float AntennaNetwork::calculateSpeedFactor(float swr) const {
    // Perfect match (SWR = 1.0) = full speed
    // SWR = 2.0 = 70% speed
    // SWR = 3.0 = 40% speed
    // SWR >= 5.0 = 10% speed (crawling)
    
    if (swr <= 1.0f) {
        return 1.0f;
    } else if (swr >= 5.0f) {
        return 0.1f;
    } else {
        // Exponential falloff
        float factor = 1.0f / (1.0f + (swr - 1.0f) * 0.4f);
        return std::max(0.1f, factor);
    }
}

// ============================================================================
// MorseDatabase Implementation
// ============================================================================

MorseDatabase::MorseDatabase() {
    initializeDatabase();
}

void MorseDatabase::initializeDatabase() {
    // International Morse Code
    // Letters (A-Z) with difficulty ratings
    database['A'] = MorseChar('A', ".-", 1);
    database['B'] = MorseChar('B', "-...", 3);
    database['C'] = MorseChar('C', "-.-.", 3);
    database['D'] = MorseChar('D', "-..", 2);
    database['E'] = MorseChar('E', ".", 1);
    database['F'] = MorseChar('F', "..-.", 3);
    database['G'] = MorseChar('G', "--.", 2);
    database['H'] = MorseChar('H', "....", 2);
    database['I'] = MorseChar('I', "..", 1);
    database['J'] = MorseChar('J', ".---", 4);
    database['K'] = MorseChar('K', "-.-", 3);
    database['L'] = MorseChar('L', ".-..", 3);
    database['M'] = MorseChar('M', "--", 2);
    database['N'] = MorseChar('N', "-.", 1);
    database['O'] = MorseChar('O', "---", 2);
    database['P'] = MorseChar('P', ".--.", 4);
    database['Q'] = MorseChar('Q', "--.-", 4);
    database['R'] = MorseChar('R', ".-.", 2);
    database['S'] = MorseChar('S', "...", 1);
    database['T'] = MorseChar('T', "-", 1);
    database['U'] = MorseChar('U', "..-", 2);
    database['V'] = MorseChar('V', "...-", 3);
    database['W'] = MorseChar('W', ".--", 2);
    database['X'] = MorseChar('X', "-..-", 4);
    database['Y'] = MorseChar('Y', "-.--", 4);
    database['Z'] = MorseChar('Z', "--..", 3);
    
    // Numbers (0-9)
    database['0'] = MorseChar('0', "-----", 5);
    database['1'] = MorseChar('1', ".----", 5);
    database['2'] = MorseChar('2', "..---", 4);
    database['3'] = MorseChar('3', "...--", 4);
    database['4'] = MorseChar('4', "....-", 4);
    database['5'] = MorseChar('5', ".....", 3);
    database['6'] = MorseChar('6', "-....", 4);
    database['7'] = MorseChar('7', "--...", 4);
    database['8'] = MorseChar('8', "---..", 4);
    database['9'] = MorseChar('9', "----.", 5);
}

std::string MorseDatabase::getPattern(char c) const {
    char upper = std::toupper(c);
    auto it = database.find(upper);
    if (it != database.end()) {
        return it->second.pattern;
    }
    return "";
}

char MorseDatabase::getRandomChar(int maxDifficulty) const {
    std::vector<char> candidates;
    for (const auto& pair : database) {
        if (pair.second.difficulty <= maxDifficulty) {
            candidates.push_back(pair.first);
        }
    }
    
    if (candidates.empty()) {
        return 'E';  // Fallback
    }
    
    int index = rand() % candidates.size();
    return candidates[index];
}

std::vector<MorseChar> MorseDatabase::getCharsForString(const std::string& str) const {
    std::vector<MorseChar> result;
    for (char c : str) {
        char upper = std::toupper(c);
        auto it = database.find(upper);
        if (it != database.end()) {
            result.push_back(it->second);
        }
    }
    return result;
}

char MorseDatabase::getNextChar(char c) {
    char upper = std::toupper(c);
    if (upper >= 'A' && upper < 'Z') {
        return upper + 1;
    } else if (upper == 'Z') {
        return '0';
    } else if (upper >= '0' && upper < '9') {
        return upper + 1;
    } else if (upper == '9') {
        return 'A';
    }
    return upper;
}

char MorseDatabase::getPrevChar(char c) {
    char upper = std::toupper(c);
    if (upper > 'A' && upper <= 'Z') {
        return upper - 1;
    } else if (upper == 'A') {
        return '9';
    } else if (upper > '0' && upper <= '9') {
        return upper - 1;
    } else if (upper == '0') {
        return 'Z';
    }
    return upper;
}

// ============================================================================
// MorseCannon Implementation
// ============================================================================

MorseCannon::MorseCannon() 
    : currentMode(Mode::VERTICAL_KEY), currentPattern(""),
      lastInputTime(0.0f), characterTimeout(CHAR_TIMEOUT),
      lastSentCharacter('\0'), charWasRead(true),
      buttonWasPressed(false), buttonHeld(false), buttonHoldTime(0.0f),
      leftWasPressed(false), rightWasPressed(false), paddleSwapped(false),
      paddleActive(false), leftHoldTime(0.0f), rightHoldTime(0.0f),
      iambicSqueeze(false), iambicLastWasDot(true), iambicElementTimer(0.0f),
      iambicSpaceTimer(0.0f), iambicInSpace(false) {
}

void MorseCannon::update(const GamepadState& input, float dt, bool suppressVerticalKey) {
    lastInputTime += dt;
    
    // Check for timeout - recognize and send pattern if no input for a while
    if (lastInputTime > characterTimeout && !currentPattern.empty()) {
        char recognized = recognizePattern();
        if (recognized != '\0') {
            lastSentCharacter = recognized;
            charWasRead = false;
        }
        currentPattern.clear();
    }
    
    if (currentMode == Mode::VERTICAL_KEY && !suppressVerticalKey) {
        // Right Trigger (RT) - vertical key
        // Short press (< DASH_THRESHOLD) = dot, long press = dash
        bool buttonPressed = input.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)] > 0.3f;
        
        if (buttonPressed && !buttonWasPressed) {
            // Button just pressed — start tracking hold time
            buttonHeld = true;
            buttonHoldTime = 0.0f;
        }
        
        if (buttonPressed && buttonHeld) {
            // Button being held — accumulate hold time
            buttonHoldTime += dt;
        }
        
        if (!buttonPressed && buttonHeld) {
            // Button released — determine dot or dash based on hold duration
            if (buttonHoldTime >= DASH_THRESHOLD) {
                addDash();
            } else {
                addDot();
            }
            lastInputTime = 0.0f;
            buttonHeld = false;
            buttonHoldTime = 0.0f;
        }
        
        buttonWasPressed = buttonPressed;
    }
    
    // Iambic paddle input: ALWAYS active (LB/RB bumpers) regardless of currentMode.
    // This allows both straight key (RT) and paddle (LB/RB) to work simultaneously.
    // Implements iambic mode B: when both paddles are squeezed, alternate dots and dashes.
    {
        bool leftPressed = input.buttons[static_cast<int>(GamepadButton::LEFT_SHOULDER)];
        bool rightPressed = input.buttons[static_cast<int>(GamepadButton::RIGHT_SHOULDER)];
        
        // Determine which paddle maps to dot and which to dash
        bool dotPressed = paddleSwapped ? rightPressed : leftPressed;
        bool dashPressed = paddleSwapped ? leftPressed : rightPressed;
        
        // Track paddle active state for audio feedback
        paddleActive = dotPressed || dashPressed;
        
        // Detect iambic squeeze (both paddles pressed simultaneously)
        bool bothPressed = dotPressed && dashPressed;
        
        if (iambicInSpace) {
            // Currently in inter-element space — count down
            iambicSpaceTimer -= dt;
            if (iambicSpaceTimer <= 0.0f) {
                iambicInSpace = false;
                // After space, check if we should continue (iambic squeeze or held paddle)
                if (bothPressed) {
                    // Iambic mode: alternate dot/dash
                    if (iambicLastWasDot) {
                        addDash();
                        iambicLastWasDot = false;
                        iambicElementTimer = DASH_DURATION;
                    } else {
                        addDot();
                        iambicLastWasDot = true;
                        iambicElementTimer = DOT_DURATION;
                    }
                    lastInputTime = 0.0f;
                    iambicSqueeze = true;
                } else if (dotPressed) {
                    addDot();
                    iambicLastWasDot = true;
                    iambicElementTimer = DOT_DURATION;
                    lastInputTime = 0.0f;
                } else if (dashPressed) {
                    addDash();
                    iambicLastWasDot = false;
                    iambicElementTimer = DASH_DURATION;
                    lastInputTime = 0.0f;
                }
            }
        } else if (iambicElementTimer > 0.0f) {
            // Currently playing an element — count down
            iambicElementTimer -= dt;
            if (iambicElementTimer <= 0.0f) {
                // Element finished — enter inter-element space
                if (dotPressed || dashPressed) {
                    iambicInSpace = true;
                    iambicSpaceTimer = ELEMENT_SPACE;
                }
            }
        } else {
            // No element playing — check for new input
            if (bothPressed && !iambicSqueeze) {
                // New iambic squeeze — start with dot
                addDot();
                iambicLastWasDot = true;
                iambicElementTimer = DOT_DURATION;
                lastInputTime = 0.0f;
                iambicSqueeze = true;
            } else if (dotPressed && !leftWasPressed) {
                addDot();
                iambicLastWasDot = true;
                iambicElementTimer = DOT_DURATION;
                lastInputTime = 0.0f;
                leftHoldTime = 0.0f;
            } else if (dashPressed && !rightWasPressed) {
                addDash();
                iambicLastWasDot = false;
                iambicElementTimer = DASH_DURATION;
                lastInputTime = 0.0f;
                rightHoldTime = 0.0f;
            } else if (dotPressed && !bothPressed) {
                // Held dot paddle — auto-repeat
                leftHoldTime += dt;
                if (leftHoldTime >= PADDLE_REPEAT_DOT) {
                    leftHoldTime -= PADDLE_REPEAT_DOT;
                    addDot();
                    iambicLastWasDot = true;
                    iambicElementTimer = DOT_DURATION;
                    lastInputTime = 0.0f;
                }
            } else if (dashPressed && !bothPressed) {
                // Held dash paddle — auto-repeat
                rightHoldTime += dt;
                if (rightHoldTime >= PADDLE_REPEAT_DASH) {
                    rightHoldTime -= PADDLE_REPEAT_DASH;
                    addDash();
                    iambicLastWasDot = false;
                    iambicElementTimer = DASH_DURATION;
                    lastInputTime = 0.0f;
                }
            }
        }
        
        if (!dotPressed && !dashPressed) {
            iambicSqueeze = false;
            // Don't reset iambicElementTimer — let the current element complete its full duration.
            // This matches iambic keyer mode B behavior: elements always play their full length
            // even if the paddle is released early (prevents contact bounce from cutting elements short).
            if (iambicElementTimer <= 0.0f) {
                // Only reset inter-element state when no element is actively playing
                iambicInSpace = false;
            }
            leftHoldTime = 0.0f;
            rightHoldTime = 0.0f;
        }
        
        // Track previous dot/dash state (post-swap) for correct edge detection
        leftWasPressed = dotPressed;
        rightWasPressed = dashPressed;
    }
}

char MorseCannon::getLastSentChar() {
    if (!charWasRead && lastSentCharacter != '\0') {
        charWasRead = true;
        return lastSentCharacter;
    }
    return '\0';
}

void MorseCannon::reset() {
    currentPattern.clear();
    lastInputTime = 0.0f;
    lastSentCharacter = '\0';
    charWasRead = true;
}

bool MorseCannon::isActive() const {
    // For vertical key: audio plays while the key is held
    if (buttonHeld) return true;
    // For paddles: audio plays for the full element duration (DOT_DURATION or DASH_DURATION),
    // regardless of whether the physical bumper is still held. This ensures dots sound short
    // (100ms) and dashes sound long (300ms), matching real CW keyer behavior.
    if (iambicElementTimer > 0.0f && !iambicInSpace) return true;
    return false;
}

bool MorseCannon::isDashPaddleActive() const {
    // For vertical key: dash when held long enough
    if (buttonHeld && buttonHoldTime >= DASH_THRESHOLD) return true;
    // For paddle: check if the current iambic element is a dash (not a dot)
    // iambicLastWasDot tracks the last element type; if false, current element is a dash
    if (iambicElementTimer > 0.0f && !iambicInSpace && !iambicLastWasDot) return true;
    return false;
}

void MorseCannon::addDot() {
    currentPattern += '.';
}

void MorseCannon::addDash() {
    currentPattern += '-';
}

char MorseCannon::recognizePattern() {
    // Simple pattern matching - check common patterns
    // Full database lookup would be better but this works for prototype
    if (currentPattern == ".-") return 'A';
    if (currentPattern == "-...") return 'B';
    if (currentPattern == "-.-.") return 'C';
    if (currentPattern == "-..") return 'D';
    if (currentPattern == ".") return 'E';
    if (currentPattern == "..-.") return 'F';
    if (currentPattern == "--.") return 'G';
    if (currentPattern == "....") return 'H';
    if (currentPattern == "..") return 'I';
    if (currentPattern == ".---") return 'J';
    if (currentPattern == "-.-") return 'K';
    if (currentPattern == ".-..") return 'L';
    if (currentPattern == "--") return 'M';
    if (currentPattern == "-.") return 'N';
    if (currentPattern == "---") return 'O';
    if (currentPattern == ".--.") return 'P';
    if (currentPattern == "--.-") return 'Q';
    if (currentPattern == ".-.") return 'R';
    if (currentPattern == "...") return 'S';
    if (currentPattern == "-") return 'T';
    if (currentPattern == "..-") return 'U';
    if (currentPattern == "...-") return 'V';
    if (currentPattern == ".--") return 'W';
    if (currentPattern == "-..-") return 'X';
    if (currentPattern == "-.--") return 'Y';
    if (currentPattern == "--..") return 'Z';
    
    // Numbers
    if (currentPattern == "-----") return '0';
    if (currentPattern == ".----") return '1';
    if (currentPattern == "..---") return '2';
    if (currentPattern == "...--") return '3';
    if (currentPattern == "....-") return '4';
    if (currentPattern == ".....") return '5';
    if (currentPattern == "-....") return '6';
    if (currentPattern == "--...") return '7';
    if (currentPattern == "---..") return '8';
    if (currentPattern == "----.") return '9';
    
    return '\0';  // Pattern not recognized yet
}

// ============================================================================
// MorseSignalManager Implementation
// ============================================================================

MorseSignalManager::MorseSignalManager(MorseDatabase* db) 
    : database(db) {
}

void MorseSignalManager::update(float playerAngle, float gameTime, float dt) {
    removeExpiredSignals(gameTime);
    updateSignalPanning(playerAngle);
}

void MorseSignalManager::spawnSignal(char character, float angle, float gameTime) {
    MorseSignal signal;
    signal.character = character;
    signal.angle = angle;
    signal.spawnTime = gameTime;
    // All other fields use their default member initializers from struct definition
    
    signals.push_back(signal);
}

MorseSignal* MorseSignalManager::getTargetedSignal(float playerAngle, float aimAngle, float aimMargin) {
    float targetAngle = playerAngle + aimAngle;
    
    // Normalize to [0, 2π)
    while (targetAngle < 0.0f) targetAngle += TWO_PI;
    while (targetAngle >= TWO_PI) targetAngle -= TWO_PI;
    
    MorseSignal* closest = nullptr;
    float closestDist = aimMargin;
    
    for (auto& signal : signals) {
        if (signal.collected) continue;
        
        // Calculate angular distance
        float dist = std::abs(signal.angle - targetAngle);
        if (dist > PI) {
            dist = TWO_PI - dist;  // Wrap around
        }
        
        if (dist < closestDist) {
            closestDist = dist;
            closest = &signal;
        }
    }
    
    return closest;
}

bool MorseSignalManager::tryCollectSignal(MorseSignal* signal, char sentChar, float reactance) {
    if (!signal || signal->collected) {
        return false;
    }
    
    // Simple exact match — send the displayed character to collect it
    if (std::toupper(sentChar) == std::toupper(signal->character)) {
        signal->collected = true;
        return true;
    }
    
    return false;
}

void MorseSignalManager::clear() {
    signals.clear();
}

bool MorseSignalManager::isPositionTooClose(float angle, float minDistance) const {
    for (const auto& sig : signals) {
        if (sig.collected) continue;
        float dist = std::abs(sig.angle - angle);
        if (dist > PI) dist = TWO_PI - dist;
        if (dist < minDistance) return true;
    }
    return false;
}

void MorseSignalManager::removeExpiredSignals(float gameTime) {
    // Mark collected signals for removal so the central template handles them
    for (auto& s : signals) {
        if (s.collected) s.markedForRemoval = true;
    }
    removeExpiredEntities(signals, gameTime);
}

void MorseSignalManager::updateSignalPanning(float playerAngle) {
    for (auto& signal : signals) {
        signal.panPosition = calculatePanPosition(signal.angle, playerAngle);
    }
}

// ============================================================================
// Game Implementation
// ============================================================================

// Constructor
Game::Game(TranslationManager* trans, Logger* log, IConsoleInput* console)
    : translation(trans), logger(log), analyzer(nullptr), consoleInput(console),
      currentPan(0.0f), motorFreqHz(MOTOR_BASE_FREQ),
      swrFreqHz(SWR_GOOD_FREQ), swrAlertPhase(0.0f), audioInitialized(false),
      audioBackend(nullptr), audioRunning(false),
      currentState(GameState::INTRO), deltaTime(0.0f), shouldExit(false),
      currentMenuOption(MenuOption::RESUME), inConfigMenu(false), configCalledFromMainMenu(false),
      inSoundLearning(false), soundLearningFromMainMenu(false), soundLearningIndex(0),
      soundLearningVibTimer(0.0f),
      currentConfigCategory(ConfigCategory::TRACK), currentConfigOption(ConfigOption::TRACK_CURVE),
      inConfigSubMenu(false), currentSubOptionIndex(0),
      playerAngle(0.0f), playerSpeed(0.0f), maxSpeed(2.0f), baseMaxSpeed(2.0f),
      acceleration(0.06f), deceleration(0.4f), brakingForce(0.8f), friction(0.04f),
      steeringSpeed(1.4f), playerLateralOffset(0.0f), isBraking(false), reverseHoldTime(0.0f),
      kHzPerRadian(1.0f), freqStepKHz(1.0f), trackBorderProximity(0.0f), crashRecoveryTime(0.0f),
      borderVibrationActive(false), crashVibrationTimer(0.0f),
      nextQSOStoererSpawnTime(90.0f),
      aimAngle(0.0f), aimSpeed(2.0f), aimSyncToHeading(false), hamSpiritBonusAchieved(false), nextMorseSpawnTime(0.0f),
      announceCooldown(0.0f),
      lastAnnouncedDamageStage(0), paReflectedPowerAccum(0.0f), paThermalLoad(0.0f),
      nextTrafficReportTime(30.0f),
      lastNoisePanDebugTime(kInitialDebugThrottleTime),
      lastKeyboardUnknownKeyTime(kInitialDebugThrottleTime),
      lastConsoleFocusLogTime(kInitialDebugThrottleTime),
      lastConsoleFocused(true),
      allCurvesCurrentSection(0),
      currentVoiceIndex(0) {
    
    currentWeapon = WeaponType::NOISE_BLANKER;
    noiseBlankerCooldown = 0.0f;
    pendingHitTimer = 0.0f;
    pendingHitHealth = 0;
    pendingHitDestroyed = false;
    pendingHitBonus = 0;
    pendingHitIsQso = false;
    pendingHitQsoHealth = 0.0f;
    nextNoiseSpawnTime = 20.0f;
    nextNoiseAnnounceTime = 17.0f;
    scheduledNoiseAngle = 0.0f;
    noiseScheduled = false;
    
    // Load persistent game config (before anything else uses config)
    loadGameConfig();
    
    if (logger) {
        logger->log("HAMSPIRIT", "Game instance created");
    }
    
    // Load hamspirit translations (merge into existing language)
    if (translation) {
        std::string lang = translation->getCurrentLanguage();
        std::string hamFile = "Languages/hamspirit_" + lang + ".lng";
        std::string err;
        if (translation->loadAdditionalFile(hamFile, err)) {
            if (logger) logger->log("HAMSPIRIT", "Loaded translations from " + hamFile);
        } else {
            if (logger) logger->log("HAMSPIRIT", "No hamspirit translation file: " + err);
        }
    }
    
    // Initialize input systems
    gamepad = createGamepadInput();
    keyboard = createKeyboardEmulator();
    
    if (gamepad) {
        gamepad->initialize();
        // Apply saved controller preset (0=Auto, 1=Xbox, 2=PS)
        gamepad->setControllerPreset(config.controllerPreset);
    }
    
    // Apply loaded key mapping to the keyboard emulator
    applyKeyMapping();
    
    // Initialize TTS
#ifdef __APPLE__
    // On macOS, remap Windows-only engine types to macOS equivalents
    if (config.ttsEngine == TTSEngineType::WINDOWS_SAPI || config.ttsEngine == TTSEngineType::NVDA) {
        if (logger) logger->log("HAMSPIRIT", "Remapping Windows TTS engine to macOS Say (saved config had Windows-only type)");
        config.ttsEngine = TTSEngineType::MACOS_SAY;
    }
#endif
    tts = std::make_unique<TTSManager>(translation, config.ttsEngine);
    if (tts) {
        bool ttsAvailable = tts->initialize();
#ifdef __APPLE__
        // macOS: if VoiceOver was requested but isn't running, fall back to 'say'
        if (!ttsAvailable && config.ttsEngine == TTSEngineType::MACOS_VOICEOVER) {
            if (logger) logger->log("HAMSPIRIT", "VoiceOver not running — falling back to macOS Say");
            config.ttsEngine = TTSEngineType::MACOS_SAY;
            if (tts->setEngineType(TTSEngineType::MACOS_SAY)) {
                ttsAvailable = true;
            }
        }
#else
        bool nvdaWasRequested = (config.ttsEngine == TTSEngineType::NVDA);
        if (!ttsAvailable && nvdaWasRequested) {
            // NVDA DLL not found — fall back to SAPI first so we can speak to the user
            if (tts->setEngineType(TTSEngineType::WINDOWS_SAPI)) {
                ttsAvailable = true;
            }
        }
#endif
        if (ttsAvailable) {
            tts->setRate(static_cast<TTSRate>(config.ttsSpeed));
            // Apply saved voice if specified
            if (!config.ttsVoice.empty()) {
                ITTSEngine* engine = tts->getEngine();
                if (engine) engine->setVoice(config.ttsVoice);
            }
        }
#ifndef __APPLE__
        // If NVDA was requested but unavailable, offer interactive download
        if (ttsAvailable && nvdaWasRequested && tts->getEngineType() == TTSEngineType::WINDOWS_SAPI) {
            offerNvdaControllerDownload();
        }
#endif
        if (translation && logger) {
            logger->log("HAMSPIRIT", "TTS language from translation: " + translation->getCurrentLanguage());
        }
    }
    
    // Initialize spatial audio
    spatialAudio = std::make_unique<SpatialAudio>();
    
    // Initialize antenna network
    antennaNetwork = std::make_unique<AntennaNetwork>();
    
    // Initialize morse system
    morseDatabase = std::make_unique<MorseDatabase>();
    morseSignalManager = std::make_unique<MorseSignalManager>(morseDatabase.get());
    morseCannon = std::make_unique<MorseCannon>();
    // Single game instance is expected; last instance owns the shared math logger.
    sHamSpiritMathLogger = logger;
}

// Destructor
Game::~Game() {
    // Stop vibration on ALL controllers before any other cleanup
    try { stopAllVibration(); } catch (...) {}
    if (sHamSpiritMathLogger == logger) {
        sHamSpiritMathLogger = nullptr;
    }
    shutdown();
}

// Initialize game with measurement data
bool Game::initialize(
    const std::vector<MeasurementPoint>& measurements,
    AcousticAnalyzer* analyzer_ptr
) {
    if (measurements.empty()) {
        log("HAMSPIRIT", "ERROR: No measurement data provided");
        return false;
    }
    
    if (!analyzer_ptr) {
        log("HAMSPIRIT", "ERROR: No acoustic analyzer provided");
        return false;
    }
    
    measurementData = measurements;
    analyzer = analyzer_ptr;
    
    log("HAMSPIRIT", "Game initialized with " + std::to_string(measurements.size()) + " measurement points");
    
    // Generate track from measurement data
    track = TrackGenerator::generateTrack(measurementData, config.trackCurve);
    log("HAMSPIRIT", "Generated track with " + std::to_string(track.size()) + " points");
    
    // Pre-generate all curve tracks for ALL_CURVES mode
    allCurvesTracks.clear();
    for (int c = 0; c <= 4; c++) {
        allCurvesTracks.push_back(
            TrackGenerator::generateTrack(measurementData, static_cast<TrackCurve>(c)));
    }
    
    // Initialize player position at start of track
    playerAngle = 0.0f;
    playerSpeed = 0.0f;  // Start stationary — speed = transmitted power

    // Auto-select UnUn ratio based on scan impedance and log matching feasibility
    autoSelectUnUnRatio();
    logMatchingFeasibility();
    
    // Calculate kHz/radian conversion from frequency span
    float startFreq = measurements.front().freq;
    float endFreq = measurements.back().freq;
    float totalSpanKHz = std::abs(endFreq - startFreq) / 1000.0f;
    freqStepKHz = totalSpanKHz / std::max(1.0f, static_cast<float>(measurements.size() - 1));
    kHzPerRadian = totalSpanKHz / TWO_PI;
    // Record frequency range for band crossing sanity checking
    minTrackFreqHz = std::min(startFreq, endFreq);
    maxTrackFreqHz = std::max(startFreq, endFreq);
    if (kHzPerRadian < 0.001f) {
        kHzPerRadian = DEFAULT_KHZ_PER_RADIAN;
        log("HAMSPIRIT", "WARNING: kHzPerRadian fallback (frequency span too small)");
    }
    
    // Base max speed: ~10 kHz/s converted to rad/s
    baseMaxSpeed = BASE_MAX_SPEED_KHZ / kHzPerRadian;
    // Difficulty scaling: 0.9x at diff 1, up to 1.8x at diff 10 (was 0.2× per level)
    baseMaxSpeed *= (0.8f + 0.1f * config.difficultyLevel);
    maxSpeed = baseMaxSpeed;
    
    // Initialize timing
    gameStartTime = std::chrono::steady_clock::now();
    lastUpdateTime = gameStartTime;
    
    // Load band plan for band crossing announcements
    loadBandPlan();
    
    // Initialize audio system
    initAudio();
    
    return true;
}

// Run the game (main loop)
void Game::run() {
    log("HAMSPIRIT", "Starting game main loop");
#ifdef _WIN32
    log("HAMSPIRIT", "Input: GUI window (GetAsyncKeyState)");
#elif defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
    log("HAMSPIRIT", "Input: GUI window (NSEvent key queue)");
#else
    log("HAMSPIRIT", "Input: Console (POSIX terminal)");
#endif
    log("HAMSPIRIT", "Gamepad: " + std::string((gamepad && gamepad->isConnected()) ? "connected" : "not connected"));
    if (gamepad && gamepad->isConnected()) {
        log("HAMSPIRIT", "Gamepad name: " + gamepad->getControllerName());
    } else if (gamepad) {
        log("HAMSPIRIT", "Gamepad count: " + std::to_string(gamepad->getConnectedCount()));
    }
    log("HAMSPIRIT", "TTS: " + std::string((tts && tts->isAvailable()) ? "available" : "NOT available"));
    log("HAMSPIRIT", std::string("TTS engine: ") + getTTSEngineLabel(config.ttsEngine));

    const bool windowed = startHamSpiritWindow();
    log("HAMSPIRIT", windowed ? "Ham Spirit windowed mode enabled" : "Ham Spirit windowed mode unavailable");
    // RAII guard ensures the window is closed on exit from run().
    HamSpiritWindowGuard windowGuard(windowed);
    
    shouldExit = false;
    currentState.store(GameState::INTRO);
    
    // Run intro sequence directly (setState would skip it since currentState is already INTRO)
    runIntroSequence();
    
    if (shouldExit) {
        log("HAMSPIRIT", "Game exited during intro");
        return;
    }
    
    // Start audio thread AFTER intro so SAPI TTS and waveOut don't conflict
    if (audioInitialized && !audioRunning.load()) {
        audioRunning.store(true);
        audioThread = std::thread(&Game::audioThreadFunc, this);
    }
    
    // Reset timing AFTER intro so deltaTime doesn't include intro duration
    lastUpdateTime = std::chrono::steady_clock::now();
    
    // Main game loop — wrapped in try/catch for crash safety (vibration cleanup)
    int gameFrameCount = 0;
    try {
    while (!shouldExit) {
        auto currentTime = std::chrono::steady_clock::now();
        deltaTime = std::chrono::duration<float>(currentTime - lastUpdateTime).count();
        lastUpdateTime = currentTime;
        
        // Clamp deltaTime to prevent physics explosion after alt-tab or focus loss
        if (deltaTime > 0.25f) deltaTime = 0.016f;
        
        gameFrameCount++;
        // Heartbeat log every ~5 seconds (300 frames at ~16ms each)
        if (gameFrameCount % 300 == 0) {
            float swr = getCurrentSWR();
            log("HAMSPIRIT", "Game loop alive, frame " + std::to_string(gameFrameCount) +
                ", state: " + std::to_string(static_cast<int>(currentState.load())) +
                ", speed: " + std::to_string(playerSpeed) +
                ", maxSpeed: " + std::to_string(maxSpeed) +
                ", SWR: " + std::to_string(swr) +
                ", health: " + std::to_string(stats.paHealth));
            // Extended debug: tuner state, morse, noise enemies, aim
            if (antennaNetwork) {
                log("HAMSPIRIT_DEBUG", "Tuner L=" + std::to_string(antennaNetwork->getTuner().getInductance()) +
                    " C=" + std::to_string(antennaNetwork->getTuner().getCapacitance()) +
                    " UnUn=" + std::to_string(antennaNetwork->getUnUn().getRatioMultiplier()) +
                    " aim=" + std::to_string(aimAngle) +
                    " morse=" + std::to_string(morseSignalManager ? morseSignalManager->getActiveSignalCount() : -1) +
                    " noise=" + std::to_string(noiseEnemies.size()) +
                    " angle=" + std::to_string(playerAngle));
            }
            // Debug: gamepad input state snapshot
            if (gamepad && gamepad->isConnected()) {
                GamepadState dbgInput = getCurrentInput();
                int buttonBits = 0;
                for (int i = 0; i < static_cast<int>(GamepadButton::COUNT); i++) {
                    if (dbgInput.buttons[i]) buttonBits |= (1 << i);
                }
                char hexBuf[16]; 
                std::snprintf(hexBuf, sizeof(hexBuf), "%04x", buttonBits);
                log("HAMSPIRIT_DEBUG", "Gamepad: LX=" + std::to_string(dbgInput.axes[0]) + 
                    " LY=" + std::to_string(dbgInput.axes[1]) +
                    " RX=" + std::to_string(dbgInput.axes[2]) +
                    " RY=" + std::to_string(dbgInput.axes[3]) +
                    " LT=" + std::to_string(dbgInput.axes[4]) +
                    " RT=" + std::to_string(dbgInput.axes[5]) +
                    " buttons=0x" + std::string(hexBuf));
            }
        }
        
        // Poll keyboard input and feed to keyboard emulator
        pollKeyboard();
        
        // Update gamepad input (unconditional — update() processes device
        // connect/disconnect callbacks; skipping it when !isConnected() would
        // prevent newly plugged-in controllers from ever being detected)
        if (gamepad) {
            gamepad->update();
        }
        
        // Handle input
        handleInput();
        
        // Update game state
        updateGameState(deltaTime);
        
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
        // Pump macOS AppKit events to keep the NSWindow responsive
        pumpHamSpiritEvents();
#endif
        // Frame-time-aware sleep to maintain ~60 FPS without accumulating lag.
        // On macOS the simple 16ms sleep causes drift because it doesn't account
        // for processing time, leading to sluggish input and mis-timed morse.
        {
            auto frameEnd = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - currentTime);
            auto targetFrame = std::chrono::microseconds(16000); // 16ms target
            if (elapsed < targetFrame) {
                std::this_thread::sleep_for(targetFrame - elapsed);
            } else {
                // Frame took longer than budget — yield briefly to avoid starving OS
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        }
    }
    } catch (const std::exception& e) {
        log("HAMSPIRIT", "CRASH in game loop: " + std::string(e.what()));
        // Emergency vibration stop
        try { stopAllVibration(); } catch (...) {}
    } catch (...) {
        log("HAMSPIRIT", "UNKNOWN CRASH in game loop");
        try { stopAllVibration(); } catch (...) {}
    }
    
    // Always stop vibration when game exits normally too
    try { stopAllVibration(); } catch (...) {}
    
    log("HAMSPIRIT", "Game main loop exited");
}

// Shutdown game
void Game::shutdown() {
    log("HAMSPIRIT", "Shutting down game");
    
    shouldExit = true;
    
    // Shutdown the central authority before audio/input to ensure
    // no pending events reference destroyed resources.
    if (gameAuthority) {
        gameAuthority->shutdown();
    }
    
    shutdownAudio();
    
    // Stop vibration on ALL controllers
    stopAllVibration();
    
    if (tts) {
        tts->stop();
    }
    
    if (gamepad) {
        gamepad->shutdown();
    }
}

// Set game state
void Game::setState(GameState newState) {
    GameState oldState = currentState.load();
    
    if (oldState == newState) {
        return;
    }
    
    log("HAMSPIRIT", "State change: " + std::to_string(static_cast<int>(oldState)) + 
        " -> " + std::to_string(static_cast<int>(newState)));
    
    currentState.store(newState);
    
    // Update wallpaper image based on game state
#ifdef _WIN32
    {
        int imgIdx = 0;
        switch (newState) {
            case GameState::INTRO:
                imgIdx = 1;  // Title image
                break;
            case GameState::MAIN_MENU:
            case GameState::GAME_OVER:
                imgIdx = 2;  // Menu image (also for intermissions like game over, tutorial)
                break;
            case GameState::PAUSED:
                imgIdx = 3;  // Racing image shown during pause (moved from PLAYING)
                break;
            case GameState::PLAYING:
                imgIdx = 0;  // No wallpaper — track visualization overlay only
                break;
            case GameState::EXITING:
                imgIdx = 0;  // No image
                break;
        }
        if (sHamSpiritWindow.currentImage.load() != imgIdx) {
            sHamSpiritWindow.currentImage.store(imgIdx);
            if (sHamSpiritWindow.hwnd) {
                InvalidateRect(sHamSpiritWindow.hwnd, nullptr, FALSE);
            }
        }
        // Track visualization timer: start ~30fps repaint during gameplay, stop otherwise
        if (sHamSpiritWindow.hwnd) {
            if (newState == GameState::PLAYING) {
                SetTimer(sHamSpiritWindow.hwnd, 1, 16, nullptr);  // Timer ID 1, ~60fps — match game loop for audio-visual sync
                // Hide menu overlay when entering gameplay
                hideMenuOverlay();
            } else {
                KillTimer(sHamSpiritWindow.hwnd, 1);
                // Clear track overlay when leaving gameplay
                std::lock_guard<std::mutex> lock(sHamSpiritWindow.trackVisMtx);
                sHamSpiritWindow.trackVis.active = false;
            }
            if (newState == GameState::EXITING) {
                hideMenuOverlay();
            }
        }
    }
#else
    // Non-Windows: manage terminal overlay state on game state transitions
    if (newState == GameState::PLAYING || newState == GameState::EXITING) {
        hideMenuOverlay();
        hideTextOverlay();
    }
#endif
    
    // Title melody lifecycle management
    if (titleMelody) {
        if (newState == GameState::PLAYING) {
            // Entering active gameplay — fade out the melody quickly.
            // beginFadeOut() is safe to call even if already fading or stopped.
            titleMelody->beginFadeOut();
        }
        else if (newState == GameState::PAUSED) {
            // Entering pause menu — restart the title melody
            titleMelody->start();
        }
        else if (newState == GameState::EXITING) {
            titleMelody->stop();
        }
    }
    
    // Apply config changes immediately when returning to gameplay from pause/config
    // This ensures settings like vibration, braille, volumes etc. take effect right away
    if (newState == GameState::PLAYING && oldState == GameState::PAUSED) {
        log("HAMSPIRIT", "Applying config changes after returning from pause/config menu");
        // Vibration state is read directly from config.swrVibration each frame — no extra action needed
        // Audio volumes are read from config each frame — no extra action needed
        // Braille: if just enabled, trigger an immediate update
        if (config.brailleEnabled) {
            std::string brl = buildGameplayBrailleString();
            if (!brl.empty()) updateBrailleDisplay(brl);
        }
    }
    
    // Pause/resume racing loop sounds: stop all gameplay audio when pausing
    // to prevent distortion and hangs from un-updated sound parameters.
    // Use blocking lock — this state change is critical and must not be dropped.
    if (newState == GameState::PAUSED && oldState == GameState::PLAYING) {
        std::lock_guard<std::mutex> lock(audioStateMtx);
        // Clear all racing/gameplay sound frame counters
        audioParams.borderScrapeSoundFrames = 0;
        audioParams.borderCrashSoundFrames = 0;
        audioParams.borderWarningSoundFrames = 0;
        audioParams.borderWarningActive = false;
        audioParams.borderWarningBeepTimer = 0.0f;
        audioParams.emergencyBrakeSoundFrames = 0;
        audioParams.paDamageSoundFrames = 0;
        audioParams.trafficBeepFrames = 0;
        audioParams.noiseBlankerFireFrames = 0;
        audioParams.noiseDestroyedFrames = 0;
        audioParams.noiseHitSoundFrames = 0;
        audioParams.qsoStoererCollisionFrames = 0;
        audioParams.qsoStoererOvertakeFrames = 0;
        audioParams.qsoStoererActive = false;
        audioParams.swrAlertActive = false;
        audioParams.morseCannonActive = false;
        audioParams.morseSignals.clear();
        audioParams.noiseEnemies.clear();
        audioParams.motorVolume = 0.0f;
        audioParams.swrVolume = 0.0f;
        audioParams.aimLockStrength = 0.0f;
        audioParams.aimLockMorse = 0.0f;
        audioParams.aimLockNoise = 0.0f;
        audioParams.aimLockStoerer = 0.0f;
        audioParams.aimLockPowerUp = 0.0f;
        audioParams.powerUpCollecting = false;
        audioParams.powerUpCollectProgress = 0.0f;
        audioParams.powerUpZones.clear();
        audioParams.collectSoundFrames = 0;
        audioParams.noiseHitSoundFrames = 0;
        audioParams.bandJingleFrames = 0;
    }
    // Stop vibration on ALL controllers when entering pause menu
    if (newState == GameState::PAUSED && oldState == GameState::PLAYING) {
        stopAllVibration();
        // Reset crash vibration timer so it doesn't resume after unpausing
        crashVibrationTimer = 0.0f;
    }
    // Also clear gameplay-specific audio state for secondary players on pause
    if (newState == GameState::PAUSED && oldState == GameState::PLAYING
        && multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        int pc = multiplayerMgr->getPlayerCount();
        for (int p = 1; p < pc; p++) {
            auto* ctx = multiplayerMgr->getPlayer(p);
            if (!ctx) continue;
            std::lock_guard<std::mutex> pLock(ctx->audioStateMtx);
            ctx->audioState.powerUpZones.clear();
            ctx->audioState.powerUpCollecting = false;
            ctx->audioState.powerUpCollectProgress = 0.0f;
            ctx->audioState.motorVolume = 0.0f;
            ctx->audioState.swrAlertActive = false;
            ctx->audioState.morseSignals.clear();
            ctx->audioState.noiseEnemies.clear();
            ctx->audioState.aimLockStrength = 0.0f;
            ctx->audioState.aimLockMorse = 0.0f;
            ctx->audioState.aimLockNoise = 0.0f;
            ctx->audioState.aimLockStoerer = 0.0f;
            ctx->audioState.aimLockPowerUp = 0.0f;
            ctx->audioState.collectSoundFrames = 0;
            ctx->audioState.paRepairSoundFrames = 0;
            ctx->audioState.paDamageSoundFrames = 0;
            ctx->audioState.noiseHitSoundFrames = 0;
            ctx->audioState.noiseBlankerFireFrames = 0;
            ctx->audioState.noiseDestroyedFrames = 0;
            ctx->audioState.missMorseSoundFrames = 0;
            ctx->audioState.missAimSoundFrames = 0;
            ctx->audioState.borderWarningSoundFrames = 0;
            ctx->audioState.borderWarningActive = false;
            ctx->audioState.swrVolume = 0.0f;
            ctx->audioState.adjustSoundFrames = 0;
            ctx->audioState.bumperSoundFrames = 0;
            ctx->audioState.qsoStoererCollisionFrames = 0;
            ctx->audioState.qsoStoererOvertakeFrames = 0;
            ctx->audioState.bandJingleFrames = 0;
            ctx->audioState.trafficBeepFrames = 0;
            ctx->audioState.powerUpActivateFrames = 0;
            ctx->audioState.powerUpExpireFrames = 0;
            ctx->audioState.powerUpExplodeFrames = 0;
        }
    }
    
    // State entry actions
    switch (newState) {
        case GameState::INTRO:
            runIntroSequence();
            break;
            
        case GameState::MAIN_MENU:
            // Main menu handles its own input loop
            // Show menu overlay for visual display
            {
                std::vector<std::string> items = {"New Game", "Leaderboard", "Tutorial", "Speaker Test", "Learn Sounds", "Configuration", "Exit"};
                updateMenuOverlay("Ham Spirit", items, static_cast<int>(currentMainMenuOption));
            }
            break;
            
        case GameState::PAUSED:
            // Skip showPauseMenu() when entering PAUSED from main menu Config
            // option — the config flags (inConfigMenu, configCalledFromMainMenu)
            // are already set by handleMainMenuInput and showPauseMenu() would
            // reset them, causing the config menu to not appear and the pause
            // menu's "Resume" to be selected instead (which could trigger
            // gameplay if A button is still held).
            if (!inConfigMenu) {
                showPauseMenu();
            }
            break;
            
        case GameState::GAME_OVER:
            // Stop any currently playing melody first, then start end-game melody
            if (titleMelody) {
                titleMelody->stop();
                if (stats.paHealth > 0.0f) {
                    titleMelody->startVictory();
                } else {
                    titleMelody->startDefeat();
                }
            }
            showGameOver();
            break;
            
        case GameState::EXITING:
            shouldExit = true;
            break;
            
        default:
            break;
    }
}

// Update game state
void Game::updateGameState(float dt) {
    stats.gameTime += dt;
    
    GameState state = currentState.load();
    
    switch (state) {
        case GameState::INTRO:
            // Intro sequence handles its own state transition
            break;
            
        case GameState::MAIN_MENU:
            // Main menu handles input
            {
                GamepadState input = getCurrentInput();
                if (inSoundLearning) {
                    handleSoundLearningInput(input);
                } else {
                    handleMainMenuInput(input);
                }
            }
            break;
            
        case GameState::PLAYING:
            updatePlayingState(dt);
            break;
            
        case GameState::PAUSED:
            updatePausedState(dt);
            break;
            
        case GameState::GAME_OVER:
            // Waiting for input to exit
            break;
            
        case GameState::EXITING:
            // Should exit soon
            break;
    }
}

// Run intro sequence — just the welcome, then main menu
void Game::runIntroSequence() {
    log("HAMSPIRIT", "Running intro sequence");
    
    if (tts && tts->isAvailable()) {
        log("HAMSPIRIT", "TTS available, playing welcome");
        
        // Welcome message
        showTextOverlay("Welcome to Ham Spirit!");
        speakTranslated("HAMSPIRIT_WELCOME", "Welcome to Ham Spirit!", true);
        if (!waitForInput()) goto intro_complete;
        
        // Story part 1
        showTextOverlay("You are the Ham Spirit, a positive radio signal seeking to travel across all amateur radio bands.");
        speakTranslated("HAMSPIRIT_INTRO_1", 
            "You are the Ham Spirit, a positive radio signal seeking to travel across all amateur radio bands.",
            true);
        if (!waitForInput()) goto intro_complete;
        
        // Story part 2
        showTextOverlay("But as a signal, you face obstacles that can only be overcome with proper measurement and tuning techniques.");
        speakTranslated("HAMSPIRIT_INTRO_2",
            "But as a signal, you face obstacles that can only be overcome with proper measurement and tuning techniques.",
            true);
        if (!waitForInput()) goto intro_complete;
        
        // Story part 3
        showTextOverlay("Use your antenna tuner, matching networks, and morse cannon to complete your journey.");
        speakTranslated("HAMSPIRIT_INTRO_3",
            "Use your antenna tuner, matching networks, and morse cannon to complete your journey.",
            true);
        if (!waitForInput()) goto intro_complete;
    } else {
        log("HAMSPIRIT", "TTS not available, skipping intro narration");
    }
    
intro_complete:
    hideTextOverlay();
    if (tts) {
        tts->stop();
    }
    
    if (shouldExit) {
        log("HAMSPIRIT", "Exit requested during intro");
        setState(GameState::EXITING);
        return;
    }
    
    // Go to main menu, not directly to playing
    log("HAMSPIRIT", "Intro complete, switching to MAIN_MENU");
    currentMainMenuOption = MainMenuOption::NEW_GAME;
    speakTranslated("HAMSPIRIT_MAIN_MENU", "Main Menu", true);
    speakMainMenuOption();
    setState(GameState::MAIN_MENU);
}

// Controls explanation — runs when starting a new game from main menu
void Game::runControlsIntro() {
    log("HAMSPIRIT", "Running controls intro");
    
    if (!tts || !tts->isAvailable()) return;
    
    showTextOverlay("Game Controls");
    speakTranslated("HAMSPIRIT_CONTROLS_TITLE", "Game Controls", true);
    if (!waitForInput() || shouldExit) { hideTextOverlay(); return; }
    
    if (gamepad && gamepad->isConnected()) {
        showTextOverlay("Left analog stick: Steer left/right and adjust speed. Click for emergency brake.");
        speakTranslated("HAMSPIRIT_CONTROLS_MOVEMENT", 
            "Left analog stick: Steer left/right and adjust speed. Click for emergency brake.", true);
        if (!waitForInput() || shouldExit) { hideTextOverlay(); return; }
        
        showTextOverlay("Y: Increase inductance. X: Decrease inductance.\nB: Increase capacitance. A: Decrease capacitance.");
        tts->speak("Y: Increase inductance. X: Decrease inductance. B: Increase capacitance. A: Decrease capacitance.", shouldInterruptTts(true));
        if (!waitForInput() || shouldExit) { hideTextOverlay(); return; }
        
        showTextOverlay("D-pad up/down: Adjust UnUn impedance ratio.\nD-pad left/right: Switch weapon.");
        speakTranslated("HAMSPIRIT_CONTROLS_UNUN",
            "D-pad up/down: Adjust UnUn impedance ratio. D-pad left/right: Switch weapon.", true);
        if (!waitForInput() || shouldExit) { hideTextOverlay(); return; }
        
        showTextOverlay("Right trigger: Morse key. Left trigger: Noise blanker.\nRight stick: Aim turret. Click right stick: Reset aim.");
        tts->speak("Right trigger: Morse key. Left trigger: Noise blanker. Right stick: Aim turret. Click right stick: Reset aim.", shouldInterruptTts(true));
        if (!waitForInput() || shouldExit) { hideTextOverlay(); return; }
        
        showTextOverlay("LB/RB: Morse paddles.\nBack button: Status readout. Start: Pause.");
        tts->speak("LB/RB: Morse paddles. Back button: Status readout. Start: Pause.", shouldInterruptTts(true));
        if (!waitForInput() || shouldExit) { hideTextOverlay(); return; }
    } else {
        showTextOverlay("Arrow keys: Steer and adjust speed.\nW A S D: Aim turret.");
        speakTranslated("HAMSPIRIT_KB_MOVEMENT", "Arrow keys: Steer and adjust speed. W A S D: Aim turret.", true);
        if (!waitForInput() || shouldExit) { hideTextOverlay(); return; }
        
        showTextOverlay("Q and E: Increase or decrease inductance.\nZ and C: Increase or decrease capacitance.");
        tts->speak("Q and E: Increase or decrease inductance. Z and C: Increase or decrease capacitance.", shouldInterruptTts(true));
        if (!waitForInput() || shouldExit) { hideTextOverlay(); return; }
        
        showTextOverlay("I and K: UnUn ratio up and down.\nJ and L: Switch weapon.");
        tts->speak("I and K: UnUn ratio up and down. J and L: Switch weapon.", shouldInterruptTts(true));
        if (!waitForInput() || shouldExit) { hideTextOverlay(); return; }
        
        showTextOverlay("Space: Morse key. F: Noise blanker.\nU and O: Morse paddles.");
        speakTranslated("HAMSPIRIT_KB_MORSE", "Space: Morse key. F: Noise blanker. U and O: Morse paddles.", true);
        if (!waitForInput() || shouldExit) { hideTextOverlay(); return; }
        
        showTextOverlay("Tab: Status readout. P: Pause.");
        speakTranslated("HAMSPIRIT_KB_PAUSE", "Tab: Status readout. P: Pause", true);
        if (!waitForInput() || shouldExit) { hideTextOverlay(); return; }
    }
    
    showTextOverlay("Get ready! The game starts now. Good luck!");
    speakTranslated("HAMSPIRIT_INTRO_START", "Get ready! The game starts now. Good luck!", true);
    // Begin fading out the title melody — player confirmed last prompt before racing
    if (titleMelody && titleMelody->isPlaying()) {
        titleMelody->beginFadeOut();
    }
    waitForInput(10.0f);
    hideTextOverlay();
}

// Main menu option speech
void Game::speakMainMenuOption() {
    if (!tts || !tts->isAvailable()) return;
    switch (currentMainMenuOption) {
        case MainMenuOption::NEW_GAME:
            speakTranslated("HAMSPIRIT_MAINMENU_NEWGAME", "New Game", true);
            break;
        case MainMenuOption::LEADERBOARD:
            speakTranslated("HAMSPIRIT_MENU_LEADERBOARD", "Leaderboard", true);
            break;
        case MainMenuOption::TUTORIAL:
            speakTranslated("HAMSPIRIT_MAINMENU_TUTORIAL", "Tutorial", true);
            break;
        case MainMenuOption::SPEAKER_TEST:
            speakTranslated("HAMSPIRIT_MAINMENU_SPEAKER_TEST", "Speaker Test", true);
            break;
        case MainMenuOption::LEARN_SOUNDS:
            speakTranslated("HAMSPIRIT_MAINMENU_LEARN_SOUNDS", "Learn Sounds", true);
            break;
        case MainMenuOption::CONFIGURE:
            speakTranslated("HAMSPIRIT_MAINMENU_CONFIG", "Configuration", true);
            break;
        case MainMenuOption::EXIT:
            speakTranslated("HAMSPIRIT_MAINMENU_EXIT", "Exit", true);
            break;
    }
    // Braille output for selected menu item
    static const char* mainMenuBrl[] = {"New Game", "Leaderboard", "Tutorial", "Speaker Test", "Learn Sounds", "Config", "Exit"};
    int brlIdx = static_cast<int>(currentMainMenuOption);
    if (brlIdx >= 0 && brlIdx < static_cast<int>(sizeof(mainMenuBrl) / sizeof(mainMenuBrl[0])))
        updateBrailleDisplay(mainMenuBrl[brlIdx]);
    // Update visual menu overlay on GUI window
    {
        auto tr = [this](const std::string& key, const std::string& fb) {
            return translation ? translation->get(key, fb) : fb;
        };
        std::vector<std::string> items = {
            tr("HAMSPIRIT_MAINMENU_NEWGAME", "New Game"),
            tr("HAMSPIRIT_MENU_LEADERBOARD", "Leaderboard"),
            tr("HAMSPIRIT_MAINMENU_TUTORIAL", "Tutorial"),
            tr("HAMSPIRIT_MAINMENU_SPEAKER_TEST", "Speaker Test"),
            tr("HAMSPIRIT_MAINMENU_LEARN_SOUNDS", "Learn Sounds"),
            tr("HAMSPIRIT_MAINMENU_CONFIG", "Configuration"),
            tr("HAMSPIRIT_MAINMENU_EXIT", "Exit")
        };
        updateMenuOverlay("Ham Spirit", items, static_cast<int>(currentMainMenuOption));
    }
}

// Main menu input handling
void Game::handleMainMenuInput(const GamepadState& input) {
    // Member variables: prevMainUp, prevMainDown, prevMainA (reset in restartGame)
    
    bool rawUp = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                 || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -STICK_MENU_DEADZONE;
    bool rawDown = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                   || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > STICK_MENU_DEADZONE;
    bool rawLeft = input.buttons[static_cast<int>(GamepadButton::DPAD_LEFT)]
                   || input.axes[static_cast<int>(GamepadAxis::LEFT_X)] < -STICK_MENU_DEADZONE;
    bool rawRight = input.buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)]
                    || input.axes[static_cast<int>(GamepadAxis::LEFT_X)] > STICK_MENU_DEADZONE;
    bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
    
    // D-pad axis priority: if left/right pressed, suppress up/down to prevent
    // diagonal drift from controller bounce or imprecise input
    bool up = rawUp && !rawLeft && !rawRight;
    bool down = rawDown && !rawLeft && !rawRight;
    
    // Debounce timer: ignore new D-pad edges during cooldown
    if (dpadDebounceTimer > 0.0f) {
        dpadDebounceTimer -= deltaTime;
        up = false;
        down = false;
    }
    
    if (up && !prevMainUp) {
        int opt = static_cast<int>(currentMainMenuOption);
        if (opt > 0) {
            currentMainMenuOption = static_cast<MainMenuOption>(opt - 1);
            triggerMenuNavSound();
            speakMainMenuOption();
            dpadDebounceTimer = DPAD_DEBOUNCE_TIME;
        } else {
            triggerBumperSound();
        }
    }
    if (down && !prevMainDown) {
        int opt = static_cast<int>(currentMainMenuOption);
        if (opt < static_cast<int>(MainMenuOption::EXIT)) {
            currentMainMenuOption = static_cast<MainMenuOption>(opt + 1);
            triggerMenuNavSound();
            speakMainMenuOption();
            dpadDebounceTimer = DPAD_DEBOUNCE_TIME;
        } else {
            triggerBumperSound();  // At bottom of menu
        }
    }
    if (accept && !prevMainA) {
        triggerMenuSelectSound();
        log("HAMSPIRIT", "Main menu selected: " + std::to_string(static_cast<int>(currentMainMenuOption)));
        switch (currentMainMenuOption) {
            case MainMenuOption::NEW_GAME: {
                // Show game mode selection (Singleplayer / Multiplayer)
                int gameModeResult = showGameModeSelectionMenu();
                if (shouldExit) break;
                if (gameModeResult < 0) {
                    // User pressed B — return to main menu
                    speakTranslated("HAMSPIRIT_MAIN_MENU", "Main Menu", true);
                    speakMainMenuOption();
                    break;
                }
                
                if (gameModeResult == 1) {
                    // Run multiplayer setup menu (assigns controllers, audio, callsigns)
                    runMultiplayerSetupMenu();
                    if (shouldExit) break;
                    // Apply player 0's controller preset globally so their assigned
                    // controller uses the correct button/axis mapping (affects macOS HID).
                    // Other players rely on auto-detection (preset 0) which correctly
                    // identifies Xbox vs PS controllers by VID/PID.
                    if (gamepad) {
                        gamepad->setControllerPreset(multiplayerConfig.controllerPresets[0]);
                    }
                } else {
                    // Singleplayer: select input device
                    bool inputCancelled = false;
                    {
                        int connectedGamepads = gamepad ? gamepad->getConnectedCount() : 0;
                        if (connectedGamepads > 0 && tts && tts->isAvailable()) {
                            // Build input choice list
                            std::vector<std::string> inputItems;
                            std::vector<int> inputValues; // 0=keyboard, 1..N=gamepad index (1-based)
                            inputItems.push_back(translation ? translation->get("HAMSPIRIT_INPUT_KEYBOARD", "Keyboard") : "Keyboard");
                            inputValues.push_back(0);
                            for (int g = 0; g < MAX_PLAYERS; g++) {
                                if (gamepad && gamepad->isConnected(g)) {
                                    std::string name = gamepad->getControllerName(g);
                                    inputItems.push_back(name);
                                    inputValues.push_back(g + 1);
                                }
                            }
                            int inputIdx = 0;
                            tts->speak(translation ? translation->get("HAMSPIRIT_SELECT_INPUT_TITLE", "Select input device for Player 1.") : "Select input device for Player 1.",
                                       shouldInterruptTts(true));
                            tts->speak(inputItems[0], shouldInterruptTts(false));
                            updateMenuOverlay(translation ? translation->get("HAMSPIRIT_INPUT_TITLE", "Input Device") : "Input Device", inputItems, 0);

                            pollKeyboard();
                            if (gamepad) gamepad->update();
                            GamepadState initCtrl = getCurrentInput();
                            bool piUp = initCtrl.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                                        || initCtrl.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
                            bool piDown = initCtrl.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                                          || initCtrl.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
                            bool piAccept = initCtrl.buttons[static_cast<int>(GamepadButton::A)];
                            bool piBack = initCtrl.buttons[static_cast<int>(GamepadButton::B)];
                            bool confirmed = false;
                            while (!shouldExit) {
                                pollKeyboard();
                                if (gamepad) gamepad->update();
                                GamepadState input = getCurrentInput();

                                bool up = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                                          || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
                                bool down = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                                            || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
                                bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
                                bool back = input.buttons[static_cast<int>(GamepadButton::B)];

                                if (back && !piBack) { inputCancelled = true; break; }
                                if (up && !piUp && inputIdx > 0) {
                                    inputIdx--;
                                    triggerMenuNavSound();
                                    tts->speak(inputItems[inputIdx], shouldInterruptTts(true));
                                    updateMenuOverlay(translation ? translation->get("HAMSPIRIT_INPUT_TITLE", "Input Device") : "Input Device", inputItems, inputIdx);
                                }
                                if (down && !piDown && inputIdx < static_cast<int>(inputItems.size()) - 1) {
                                    inputIdx++;
                                    triggerMenuNavSound();
                                    tts->speak(inputItems[inputIdx], shouldInterruptTts(true));
                                    updateMenuOverlay(translation ? translation->get("HAMSPIRIT_INPUT_TITLE", "Input Device") : "Input Device", inputItems, inputIdx);
                                }
                                if (accept && !piAccept) {
                                    triggerMenuSelectSound();
                                    confirmed = true;
                                    break;
                                }
                                piUp = up; piDown = down; piAccept = accept; piBack = back;
                                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                            }
                            if (confirmed) {
                                int selectedInputValue = inputValues[inputIdx];
                                if (selectedInputValue == 0) {
                                    multiplayerConfig.inputAssignments[0].type = InputSourceType::KEYBOARD;
                                } else {
                                    multiplayerConfig.inputAssignments[0].type = InputSourceType::GAMEPAD;
                                    multiplayerConfig.inputAssignments[0].gamepadIndex = selectedInputValue - 1;
                                }
                                log("HAMSPIRIT", "Singleplayer input selected: " + inputItems[inputIdx]);
                            }
                            hideMenuOverlay();
                        } else {
                            // No gamepads connected: default to keyboard
                            multiplayerConfig.inputAssignments[0].type = InputSourceType::KEYBOARD;
                        }
                    }
                    if (inputCancelled) {
                        // B pressed → return to main menu
                        speakTranslated("HAMSPIRIT_MAIN_MENU", "Main Menu", true);
                        speakMainMenuOption();
                        break;
                    }
                    if (shouldExit) break;

                    // Singleplayer: prompt for callsign and player name
                    currentPlayerCallsign = promptTextInput(
                        translation ? translation->get("HAMSPIRIT_ENTER_CALLSIGN", "Enter your callsign (or leave empty):") 
                                    : "Enter your callsign (or leave empty):", 15);
                    if (shouldExit) break;
                    currentPlayerName = promptTextInput(
                        translation ? translation->get("HAMSPIRIT_ENTER_NAME", "Enter your name:")
                                    : "Enter your name:", 20);
                    if (shouldExit) break;
                    if (!currentPlayerCallsign.empty() && tts && tts->isAvailable()) {
                        std::string phonetic = callsignToPhonetic(currentPlayerCallsign);
                        tts->speak(phonetic, shouldInterruptTts(true));
                        waitForInput(5.0f);
                    }
                    // Initialize singleplayer multiplayer context (1 player)
                    multiplayerConfig.playerCount = 1;
                }
                runControlsIntro();
                if (!shouldExit) {
                    restartGame();
                }
                break;
            }
            case MainMenuOption::LEADERBOARD:
                showLeaderboard();
                break;
            case MainMenuOption::TUTORIAL:
                runTutorial();
                break;
            case MainMenuOption::SPEAKER_TEST:
                runSpeakerTest();
                break;
            case MainMenuOption::LEARN_SOUNDS:
                soundLearningFromMainMenu = true;
                inSoundLearning = true;
                soundLearningIndex = 0;
                runSoundLearningMenu();
                break;
            case MainMenuOption::CONFIGURE:
                // Config from main menu — set flag so title is "Configuration" not "Pause Menu"
                configCalledFromMainMenu = true;
                inConfigMenu = true;
                inConfigSubMenu = false;
                currentConfigCategory = ConfigCategory::TRACK;
                currentSubOptionIndex = 0;
                speakTranslated("HAMSPIRIT_MAINMENU_CONFIG", "Configuration", true);
                setState(GameState::PAUSED);
                break;
            case MainMenuOption::EXIT:
                setState(GameState::EXITING);
                break;
        }
    }
    
    prevMainUp = up; prevMainDown = down; prevMainA = accept;
}

// Update playing state
void Game::updatePlayingState(float dt) {
    bool isMP = multiplayerMgr && multiplayerMgr->isMultiplayer();

    // In multiplayer mode, gather input for ALL players (including Player 0)
    // through the centralized path. This ensures all players are processed
    // identically — same axes, same calibration, same features.
    // Must run BEFORE Player 0's singleplayer input path and BEFORE tick().
    if (isMP) {
        if (gamepad) gamepad->update();
        gatherMultiplayerInput(dt);
    }

    // Get current input (Player 0 for singleplayer, or for general state checks)
    GamepadState input = getCurrentInput();
    
    // ── Player 0 input processing — singleplayer only ──
    // In multiplayer, all input is processed by gatherMultiplayerInput() above.
    if (!isMP) {
    // Handle antenna network adjustments
    handleAntennaNetworkInput(input, dt);

    // ── Send current SWR to GameAuthority after any tuning change ──
    // The authority uses this to compute maxSpeed and PA damage for this player.
    if (gameAuthority && gameAuthority->isActive() && antennaNetwork && !track.empty()) {
        float currentSWR = antennaNetwork->calculateAdjustedSWR(track, playerAngle);
        PlayerAction tuneAction;
        tuneAction.type = PlayerActionType::ANTENNA_TUNE;
        tuneAction.playerId = 0;
        tuneAction.value = currentSWR;
        tuneAction.timestamp = std::chrono::steady_clock::now();
        gameAuthority->processAction(tuneAction);
    }

    // ── Send raw input to GameAuthority (physics computed centrally) ──
    // Player 0 uses the same path as all other players: send raw input,
    // authority computes speed/position/steering/maxSpeed/PA in tick().
    if (gameAuthority && gameAuthority->isActive()) {
        float rawForward = -input.axes[static_cast<int>(GamepadAxis::LEFT_Y)];
        float rawSteer = input.axes[static_cast<int>(GamepadAxis::LEFT_X)];
        float rawAim = input.axes[static_cast<int>(GamepadAxis::RIGHT_X)];

        // Sync baseMaxSpeed to authority
        auto& pws = gameAuthority->getPlayerStateMutable(0);
        pws.baseMaxSpeed = baseMaxSpeed;

        PlayerAction inputAction;
        inputAction.type = PlayerActionType::INPUT_UPDATE;
        inputAction.playerId = 0;
        inputAction.forwardInput = rawForward;
        inputAction.steerInput = rawSteer;
        inputAction.aimInput = rawAim;
        inputAction.timestamp = std::chrono::steady_clock::now();
        gameAuthority->processAction(inputAction);
    } else {
        // Fallback: compute locally if authority not active (e.g. tutorial)
        updateMaxSpeedFromMatching();
        updatePlayerSpeed(dt, input);
        updatePlayerPosition(dt);
    }
    } // end if (!isMP) — singleplayer input processing

    // ── Read authoritative state back into local variables ──
    // The authority is the single source of truth for all physics.
    if (gameAuthority && gameAuthority->isActive()) {
        const auto& pws = gameAuthority->getPlayerState(0);
        playerAngle = pws.playerAngle;
        playerSpeed = pws.playerSpeed;
        playerLateralOffset = pws.playerLateralOffset;
        aimAngle = pws.aimAngle;
        maxSpeed = pws.maxSpeed;
        stats.paHealth = pws.paHealth;
    }
    
    // Update spatial audio
    updateSpatialAudio();
    
    // Check PA health based on current SWR (using adjusted SWR if antenna network exists)
    checkPAHealth(dt);
    
    // Update morse system
    updateMorseSystem(dt);
    
    // ── Per-player action handlers — singleplayer only ──
    // In multiplayer, these are handled uniformly for all players by gatherMultiplayerInput().
    if (!isMP) {
    // Handle morse cannon input
    handleMorseCannonInput(input, dt);
    
    // Handle aiming input
    handleAimingInput(input, dt);
    
    // Emergency brake (L3 — left stick click): rapid deceleration over ~0.8 seconds
    // with continuous tire screech sound throughout the braking process
    if (config.emergencyBrakeEnabled) {
        bool leftStickBtn = input.buttons[static_cast<int>(GamepadButton::LEFT_STICK)];
        if (leftStickBtn && !prevLeftStickBtn && std::abs(playerSpeed) > STANDSTILL_THRESHOLD && emergencyBrakeTimer <= 0.0f) {
            emergencyBrakeTimer = EMERGENCY_BRAKE_DURATION;  // 0.8 seconds of hard braking
            emergencyBrakeStartSpeed = std::abs(playerSpeed);
            triggerEmergencyBrakeSound();
            log("HAMSPIRIT", "Emergency brake activated! Speed was " + std::to_string(playerSpeed));
        }
        prevLeftStickBtn = leftStickBtn;
    }
    // Process ongoing emergency brake deceleration
    if (emergencyBrakeTimer > 0.0f) {
        emergencyBrakeTimer -= dt;
        if (emergencyBrakeTimer <= 0.0f) {
            // Brake complete — full stop
            playerSpeed = 0.0f;
            emergencyBrakeTimer = 0.0f;
        } else {
            // Decelerate proportionally: fast at start, easing to stop
            float brakeFraction = emergencyBrakeTimer / EMERGENCY_BRAKE_DURATION;  // 1.0 at start, 0.0 at end
            float targetSpeed = emergencyBrakeStartSpeed * brakeFraction * brakeFraction;  // Ease-out curve
            if (playerSpeed > 0.0f) {
                playerSpeed = std::min(playerSpeed, targetSpeed);
            } else {
                playerSpeed = std::max(playerSpeed, -targetSpeed);
            }
            // Keep the screech sound alive during the entire braking process
            {
                std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
                if (lock.owns_lock() && audioParams.emergencyBrakeSoundFrames < msToFrames(80)) {
                    audioParams.emergencyBrakeSoundFrames = msToFrames(320);  // Refresh screech
                }
            }
        }
        isBraking = true;
    }
    
    // Handle weapon switching (D-pad left/right)
    handleWeaponInput(input, dt);
    } // end if (!isMP) — singleplayer action handlers
    
    // Update power-up system (zones, active effects, collection)
    updatePowerUps(dt);
    if (!isMP) {
    handlePowerUpCollection(input, dt);
    }
    
    // Update noise enemies
    updateNoiseEnemies(dt);
    
    // Border collision system — singleplayer only.
    // In multiplayer, per-player border collision is handled by gatherMultiplayerInput()
    // so each player gets independent collision detection, damage, and vibration.
    if (!isMP) {
    trackBorderProximity = std::abs(playerLateralOffset);
    
    // Count down crash vibration timer (collision rumble takes priority over everything)
    if (crashVibrationTimer > 0.0f) {
        crashVibrationTimer -= dt;
        if (crashVibrationTimer <= 0.0f) {
            crashVibrationTimer = 0.0f;
            // Rumble finished — vibration will be released by the priority logic below
        }
    }
    
    if (crashRecoveryTime > 0.0f) {
        crashRecoveryTime -= dt;
        playerSpeed *= BORDER_RECOVERY_SPEED_MULT;  // Gradual slowdown during recovery instead of full stop
    } else if (trackBorderProximity >= 1.0f) {
        // CRASH — actually hitting the barrier
        // Check if on a curve by examining SWR gradient
        float swrGradient = 0.0f;
        if (!track.empty()) {
            TrackPoint tp = TrackGenerator::interpolateAt(track, playerAngle);
            float stepAngle = TWO_PI / std::max(1.0f, static_cast<float>(track.size()));
            TrackPoint tpAhead = TrackGenerator::interpolateAt(track, playerAngle + stepAngle);
            swrGradient = std::abs(tpAhead.swr - tp.swr);
        }
        if (swrGradient > CRASH_SWR_GRADIENT_THRESHOLD) {
            // CRASH on curve — hard impact with bounce-back toward center
            playerSpeed *= BORDER_CRASH_SPEED_MULT;
            stats.paHealth = std::max(0.0f, stats.paHealth - BORDER_CRASH_PA_DAMAGE);
            crashRecoveryTime = BORDER_CRASH_RECOVERY_TIME;
            {
                std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
                if (lock.owns_lock()) {
                    audioParams.borderCrashSoundFrames = 4800;
                    audioParams.borderCollisionSide = (playerLateralOffset > 0.0f) ? 0.8f : 0.2f;
                }
            }
            if (tts && tts->isAvailable()) {
                speakTranslated("HAMSPIRIT_BORDER_CRASH", "Crash! You hit the barrier!", false);
            }
            playerLateralOffset = (playerLateralOffset > 0.0f) ? BORDER_CRASH_BOUNCE_OFFSET : -BORDER_CRASH_BOUNCE_OFFSET;
            // Strong frontal collision rumble — both motors full blast
            crashVibrationTimer = BORDER_CRASH_VIB_DURATION;  // 400ms distinct rumble
        } else {
            // SCRAPE on straight — brief contact, immediate bounce back to track
            playerSpeed *= BORDER_SCRAPE_SPEED_MULT;
            stats.paHealth = std::max(0.0f, stats.paHealth - BORDER_SCRAPE_PA_DAMAGE);
            {
                std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
                if (lock.owns_lock()) {
                    audioParams.borderScrapeSoundFrames = 240;
                    audioParams.borderCollisionSide = (playerLateralOffset > 0.0f) ? 0.8f : 0.2f;
                }
            }
            if (tts && tts->isAvailable() && announceCooldown <= 0.0f) {
                speakTranslated("HAMSPIRIT_BORDER_SCRAPE", "Scraping along the barrier!", false);
                announceCooldown = 5.0f;
            }
            playerLateralOffset = (playerLateralOffset > 0.0f) ? BORDER_SCRAPE_BOUNCE_OFFSET : -BORDER_SCRAPE_BOUNCE_OFFSET;
            // Lighter scrape rumble
            crashVibrationTimer = BORDER_SCRAPE_VIB_DURATION;  // 200ms lighter rumble
        }
    } else if (trackBorderProximity > BORDER_WARNING_ZONE_START) {
        // Warning zone (0.4 to 1.0) — graduated warning beeps start well before barrier
        if (config.borderWarningEnabled) {
            // warningStrength: 0.0 at proximity 0.4, 1.0 at proximity 1.0
            float warningStrength = (trackBorderProximity - BORDER_WARNING_ZONE_START) / (1.0f - BORDER_WARNING_ZONE_START);
            warningStrength = std::clamp(warningStrength, 0.0f, 1.0f);
            
            // Speed-reactive: higher speed = stronger warning (more time needed to react)
            float speedFactor = std::clamp(std::abs(playerSpeed) / maxSpeed, 0.0f, 1.0f);
            warningStrength = std::min(1.0f, warningStrength + speedFactor * BORDER_WARNING_SPEED_FACTOR);
            
            // Curve-reactive: check SWR gradient to detect curves
            float swrGradientWarn = 0.0f;
            if (!track.empty()) {
                TrackPoint tp = TrackGenerator::interpolateAt(track, playerAngle);
                float stepAngle = TWO_PI / std::max(1.0f, static_cast<float>(track.size()));
                TrackPoint tpAhead = TrackGenerator::interpolateAt(track, playerAngle + stepAngle);
                swrGradientWarn = std::abs(tpAhead.swr - tp.swr);
            }
            warningStrength = std::min(1.0f, warningStrength + swrGradientWarn * BORDER_WARNING_SWR_FACTOR);
            
            std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
            if (lock.owns_lock()) {
                audioParams.borderWarningActive = true;
                audioParams.borderWarningIntensity = warningStrength;
                audioParams.borderWarningSide = (playerLateralOffset > 0.0f) ? 0.8f : 0.2f;
                
                // Beep rate: increases with warning strength
                if (warningStrength > BORDER_WARNING_MIN_STRENGTH) {
                    float beepInterval = 0.6f - warningStrength * 0.52f;
                    beepInterval = std::max(beepInterval, 0.08f);
                    audioParams.borderWarningBeepTimer += dt;
                    if (audioParams.borderWarningBeepTimer >= beepInterval) {
                        audioParams.borderWarningBeepTimer = 0.0f;
                        int beepFrames = static_cast<int>((200 + warningStrength * 280) * (40.0f / GAME_AUDIO_FRAME_MS));  // Scale for frame size
                        audioParams.borderWarningSoundFrames = beepFrames;
                    }
                }
            }
            
            // Continuous progressive tactile border warning via controller vibration.
            // Xbox 360: left motor = heavy/low-freq (big weight), right motor = light/high-freq (small weight).
            // The right motor is physically weaker, so we scale its drive value up by 1.4x
            // to produce perceptually comparable intensity to the left motor.
            // Approaching LEFT barrier (negative offset) → left motor vibrates
            // Approaching RIGHT barrier (positive offset) → right motor vibrates
            // Vibration is continuous and proportional to proximity — NOT pulsed.
            // This way the player constantly feels which side is dangerous and how close they are.
            // (Crash vibration takes priority and is handled separately.)
            if (config.swrVibration && crashVibrationTimer <= 0.0f && warningStrength > BORDER_WARNING_MIN_STRENGTH) {
                // Progressive intensity: 0.15 (gentle hum) → 1.0 (strong vibration) as proximity increases
                float vibIntensity = BORDER_VIB_MIN_INTENSITY + (1.0f - BORDER_VIB_MIN_INTENSITY) * warningStrength;
                if (playerLateralOffset < 0.0f) {
                    // Approaching LEFT barrier → left motor only
                    setVibrationForPlayer(0, vibIntensity * config.vibrationIntensity, 0.0f);
                } else {
                    // Approaching RIGHT barrier → right motor only (scaled up for smaller motor)
                    setVibrationForPlayer(0, 0.0f, std::min(1.0f, vibIntensity * RIGHT_MOTOR_COMPENSATION * config.vibrationIntensity));
                }
                borderVibrationActive = true;
            }
        } else {
            // Warning disabled — no sound or vibration
            std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
            if (lock.owns_lock()) {
                audioParams.borderWarningActive = false;
                audioParams.borderWarningSoundFrames = 0;
            }
            if (borderVibrationActive && crashVibrationTimer <= 0.0f) {
                setVibrationForPlayer(0, 0.0f, 0.0f);
                borderVibrationActive = false;
            }
        }
    } else {
        // Player is in safe zone — stop border warning sounds and vibration
        std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
        if (lock.owns_lock()) {
            audioParams.borderScrapeSoundFrames = 0;
            audioParams.borderCrashSoundFrames = 0;
            audioParams.borderWarningSoundFrames = 0;
            audioParams.borderWarningActive = false;
            audioParams.borderWarningBeepTimer = 0.0f;
        }
        if (borderVibrationActive && crashVibrationTimer <= 0.0f) {
            setVibrationForPlayer(0, 0.0f, 0.0f);
            borderVibrationActive = false;
        }
    }
    
    // Crash/collision vibration — takes absolute priority over all other vibration.
    // Both motors at full power for a distinct, unmistakable rumble.
    if (crashVibrationTimer > 0.0f) {
        setVibrationForPlayer(0, config.vibrationIntensity, config.vibrationIntensity);
    }
    } // end if (!isMP) — singleplayer border collision
    
    // Update QSO Störer
    updateQSOStoerer(dt);
    
    // Check for band boundary crossings (frequency-based)
    checkBandCrossing();
    
    // Check for periodic "traffic reports" (humorous SWR warnings)
    checkTrafficReport();
    
    // Curve announcement assist system
    checkCurveAnnouncement(dt);
    
    // Gather multiplayer input is now called at the top of updatePlayingState()
    // so all players' input is processed before any game logic runs.
    
    // Run the central authority simulation tick.
    // The GameAuthority processes all world state updates deterministically:
    // entity lifecycles, weapon cooldowns, active power-ups, NPC behavior.
    // This replaces the old model where the Game class directly managed
    // world state through Player0-specific code paths.
    if (gameAuthority && gameAuthority->isActive()) {
        gameAuthority->tick(dt);
    }
    
    // Update multiplayer state (spatial audio, Doppler, collisions)
    // Sync current player state into multiplayer context before updating
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        auto* ctx0 = multiplayerMgr->getPlayer(0);
        updateMultiplayerState(dt);
        // Sync Player 0's authoritative state from the multiplayer context
        // back into the Game-level variables for compatibility with shared systems.
        if (ctx0) {
            playerAngle = ctx0->playerAngle;
            playerSpeed = ctx0->playerSpeed;
            maxSpeed = ctx0->maxSpeed;
            playerLateralOffset = ctx0->playerLateralOffset;
            aimAngle = ctx0->aimAngle;
        }
    }
    
    // Periodic braille display update during gameplay (~4 Hz)
    if (config.brailleEnabled) {
        brailleUpdateTimer += dt;
        if (brailleUpdateTimer >= 0.25f) {
            brailleUpdateTimer = 0.0f;
            std::string brl = buildGameplayBrailleString();
            if (!brl.empty()) updateBrailleDisplay(brl);
        }
    }
    
    // Update track visualization overlay for the GUI window
#ifdef _WIN32
    updateTrackVisualization();
#endif
}

// -----------------------------------------------------------------------
// Track visualization data feed — pushes current game state into the
// shared TrackVisualData structure so WM_PAINT can render the overlay.
// Called once per gameplay frame from updatePlayingState().
// -----------------------------------------------------------------------
#ifdef _WIN32
void Game::updateTrackVisualization() {
    if (!sHamSpiritWindow.hwnd) return;
    
    TrackVisualData vis;
    vis.active = true;
    vis.playerLateral = playerLateralOffset;
    vis.aimAngle = aimAngle;
    
    // --- Curvature samples ---
    // Sample SWR values at evenly spaced points ahead of the player.
    // The curvature at each sample is derived from the SWR gradient:
    //   high positive gradient (SWR rising) → track curves right
    //   high negative gradient (SWR falling) → track curves left
    // This maps the measurement data directly to visual track shape.
    if (!track.empty()) {
        float stepAngle = TWO_PI / static_cast<float>(track.size());
        float lookAheadRange = stepAngle * TRACK_VIS_SAMPLES;
        float sampleStep = lookAheadRange / TRACK_VIS_SAMPLES;
        
        for (int i = 0; i < TRACK_VIS_SAMPLES; i++) {
            float sampleAngle = playerAngle + sampleStep * (i + 1);
            while (sampleAngle >= TWO_PI) sampleAngle -= TWO_PI;
            
            TrackPoint tp = TrackGenerator::interpolateAt(track, sampleAngle);
            float prevAngle = sampleAngle - sampleStep;
            while (prevAngle < 0) prevAngle += TWO_PI;
            TrackPoint tpPrev = TrackGenerator::interpolateAt(track, prevAngle);
            
            // Gradient of SWR determines curvature direction and magnitude
            float gradient = tp.swr - tpPrev.swr;
            // Scale: SWR gradient of 1.0 = moderate curve, 3.0+ = sharp bend
            vis.curvature[i] = std::clamp(gradient * 0.6f, -2.0f, 2.0f);
            // Store ADJUSTED SWR and reactance for boundary bar coloring.
            // These reflect the player's current tuner settings (L/C/UnUn),
            // so the bar colors change dynamically as the player tunes.
            if (antennaNetwork) {
                vis.swrValue[i] = antennaNetwork->calculateAdjustedSWR(track, sampleAngle);
                vis.reactance[i] = antennaNetwork->calculateAdjustedReactance(track, sampleAngle);
            } else {
                vis.swrValue[i] = tp.swr;
                vis.reactance[i] = tp.reactance;
            }
        }
        
        // --- Game objects ---
        // Convert each object's angular position to a fractional "ahead" value
        // (0 = at player, 1 = at horizon/look-ahead limit) and a lateral value.
        int objIdx = 0;
        
        // Helper: angular distance ahead of player → fractional ahead (0..1)
        auto angleToAhead = [&](float objAngle) -> float {
            float diff = objAngle - playerAngle;
            while (diff > PI) diff -= TWO_PI;
            while (diff < -PI) diff += TWO_PI;
            if (diff < -0.01f) return -1.0f;  // Behind player — skip
            return std::clamp(diff / lookAheadRange, 0.0f, 1.0f);
        };
        
        // Noise enemies
        for (const auto& ne : noiseEnemies) {
            if (ne.destroyed || objIdx >= TRACK_VIS_MAX_OBJECTS) break;
            float ahead = angleToAhead(ne.angle);
            if (ahead < 0.0f) continue;
            auto& o = vis.objects[objIdx++];
            o.kind = TrackObjKind::NOISE_ENEMY;
            o.ahead = ahead;
            o.lateral = 0.0f;  // Noise enemies span the full track width
            o.sizeParam = ne.bandwidth;  // Width of the noise bar
            // Health fraction: compute max from spawn formula (1 + bw*4 + difficulty/3)
            int maxHP = 1 + static_cast<int>(ne.bandwidth * 4.0f) + (config.difficultyLevel / 3);
            o.healthFrac = std::clamp(static_cast<float>(ne.health) / std::max(1, maxHP), 0.0f, 1.0f);
            o.intParam = ne.health;
            o.highlight = false;
        }
        
        // Morse signals
        if (morseSignalManager) {
            for (const auto& ms : morseSignalManager->getSignals()) {
                if (ms.collected || objIdx >= TRACK_VIS_MAX_OBJECTS) continue;
                float ahead = angleToAhead(ms.angle);
                if (ahead < 0.0f) continue;
                auto& o = vis.objects[objIdx++];
                o.kind = TrackObjKind::MORSE_SIGNAL;
                o.ahead = ahead;
                // Pan position (-1..+1) maps to lateral position on screen
                o.lateral = std::clamp(ms.panPosition * 0.6f, -0.8f, 0.8f);
                o.sizeParam = 0.0f;
                o.intParam = static_cast<int>(ms.character);
                o.highlight = false;  // Could highlight when targeted
            }
        }
        
        // Power-ups
        for (const auto& pu : powerUps) {
            if (pu.collected || pu.destroyed || objIdx >= TRACK_VIS_MAX_OBJECTS) continue;
            float ahead = angleToAhead(pu.angle);
            if (ahead < 0.0f) continue;
            auto& o = vis.objects[objIdx++];
            o.kind = TrackObjKind::POWER_UP;
            o.ahead = ahead;
            o.lateral = std::clamp(pu.panPosition * 0.5f, -0.7f, 0.7f);
            o.sizeParam = static_cast<float>(pu.quality);
            o.intParam = static_cast<int>(pu.type);
            o.highlight = (pu.uid == powerUpCollectTargetUid && powerUpCollectTimer > 0.0f);
        }
        
        // QSO Störer (single instance)
        if (qsoStoerer.active && objIdx < TRACK_VIS_MAX_OBJECTS) {
            float ahead = angleToAhead(qsoStoerer.angle);
            if (ahead >= 0.0f) {
                auto& o = vis.objects[objIdx++];
                o.kind = TrackObjKind::QSO_STOERER;
                o.ahead = ahead;
                o.lateral = std::clamp(qsoStoerer.lateralOffset, -0.9f, 0.9f);
                o.healthFrac = std::clamp(qsoStoerer.health, 0.0f, 1.0f);
                o.sizeParam = 0.0f;
                o.intParam = 0;
                o.highlight = false;
            }
        }
        
        vis.objectCount = objIdx;
    }
    
    // --- HUD status text (same content as the braille display) ---
    // Reuse buildGameplayBrailleString() which respects the user's config for
    // which fields to show (speed, freq, SWR, PA, score, lap).
    {
        std::string status = buildGameplayBrailleString();
        std::strncpy(vis.statusText, status.c_str(), sizeof(vis.statusText) - 1);
        vis.statusText[sizeof(vis.statusText) - 1] = '\0';
    }
    
    // Push to shared state (non-blocking — skip frame if contended)
    {
        std::unique_lock<std::mutex> lock(sHamSpiritWindow.trackVisMtx, std::try_to_lock);
        if (lock.owns_lock()) {
            sHamSpiritWindow.trackVis = vis;
            sHamSpiritWindow.playerTrackVis[0] = vis;

            // Generate per-player views for multiplayer split-screen
            if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
                int mpCount = multiplayerMgr->getPlayerCount();
                sHamSpiritWindow.multiplayerCount = mpCount;
                sHamSpiritWindow.splitOrientation =
                    (multiplayerConfig.splitOrientation == SplitOrientation::HORIZONTAL) ? 0 : 1;

                for (int p = 1; p < mpCount; p++) {
                    auto* ctx = multiplayerMgr->getPlayer(p);
                    if (!ctx) continue;

                    TrackVisualData pVis;
                    pVis.active = true;
                    pVis.playerLateral = ctx->playerLateralOffset;
                    pVis.aimAngle = ctx->aimAngle;

                    // Sample curvature, SWR, reactance from this player's position
                    if (!track.empty()) {
                        float stepAngle = TWO_PI / static_cast<float>(track.size());
                        float lookAheadRange = stepAngle * TRACK_VIS_SAMPLES;
                        float sampleStep = lookAheadRange / TRACK_VIS_SAMPLES;

                        for (int i = 0; i < TRACK_VIS_SAMPLES; i++) {
                            float sampleAngle = ctx->playerAngle + sampleStep * (i + 1);
                            while (sampleAngle >= TWO_PI) sampleAngle -= TWO_PI;
                            TrackPoint tp = TrackGenerator::interpolateAt(track, sampleAngle);
                            float prevAngle = sampleAngle - sampleStep;
                            while (prevAngle < 0) prevAngle += TWO_PI;
                            TrackPoint tpPrev = TrackGenerator::interpolateAt(track, prevAngle);

                            float gradient = tp.swr - tpPrev.swr;
                            pVis.curvature[i] = std::clamp(gradient * 0.6f, -2.0f, 2.0f);

                            if (ctx->antennaNetwork) {
                                pVis.swrValue[i] = ctx->antennaNetwork->calculateAdjustedSWR(track, sampleAngle);
                                pVis.reactance[i] = ctx->antennaNetwork->calculateAdjustedReactance(track, sampleAngle);
                            } else {
                                pVis.swrValue[i] = tp.swr;
                                pVis.reactance[i] = tp.reactance;
                            }
                        }

                        // Game objects relative to this player's position
                        int objIdx = 0;
                        float lookAhead = stepAngle * TRACK_VIS_SAMPLES;

                        auto angleToAheadP = [&](float objAngle) -> float {
                            float diff = objAngle - ctx->playerAngle;
                            while (diff > PI) diff -= TWO_PI;
                            while (diff < -PI) diff += TWO_PI;
                            if (diff < -0.01f) return -1.0f;
                            return std::clamp(diff / lookAhead, 0.0f, 1.0f);
                        };

                        // Noise enemies
                        for (const auto& ne : noiseEnemies) {
                            if (ne.destroyed || objIdx >= TRACK_VIS_MAX_OBJECTS) break;
                            float ahead = angleToAheadP(ne.angle);
                            if (ahead < 0.0f) continue;
                            auto& o = pVis.objects[objIdx++];
                            o.kind = TrackObjKind::NOISE_ENEMY;
                            o.ahead = ahead;
                            o.lateral = 0.0f;
                            o.sizeParam = ne.bandwidth;
                            int maxHP = 1 + static_cast<int>(ne.bandwidth * 4.0f) + (config.difficultyLevel / 3);
                            o.healthFrac = std::clamp(static_cast<float>(ne.health) / std::max(1, maxHP), 0.0f, 1.0f);
                            o.intParam = ne.health;
                            o.highlight = false;
                        }

                        // Morse signals
                        if (morseSignalManager) {
                            for (const auto& ms : morseSignalManager->getSignals()) {
                                if (ms.collected || objIdx >= TRACK_VIS_MAX_OBJECTS) continue;
                                float ahead = angleToAheadP(ms.angle);
                                if (ahead < 0.0f) continue;
                                auto& o = pVis.objects[objIdx++];
                                o.kind = TrackObjKind::MORSE_SIGNAL;
                                o.ahead = ahead;
                                o.lateral = std::clamp(ms.panPosition * 0.6f, -0.8f, 0.8f);
                                o.sizeParam = 0.0f;
                                o.intParam = static_cast<int>(ms.character);
                                o.highlight = false;
                            }
                        }

                        // Power-ups
                        for (const auto& pu : powerUps) {
                            if (pu.collected || pu.destroyed || objIdx >= TRACK_VIS_MAX_OBJECTS) continue;
                            float ahead = angleToAheadP(pu.angle);
                            if (ahead < 0.0f) continue;
                            auto& o = pVis.objects[objIdx++];
                            o.kind = TrackObjKind::POWER_UP;
                            o.ahead = ahead;
                            o.lateral = std::clamp(pu.panPosition * 0.5f, -0.7f, 0.7f);
                            o.sizeParam = static_cast<float>(pu.quality);
                            o.intParam = static_cast<int>(pu.type);
                            o.highlight = false;
                        }

                        // QSO Störer
                        if (qsoStoerer.active && objIdx < TRACK_VIS_MAX_OBJECTS) {
                            float ahead = angleToAheadP(qsoStoerer.angle);
                            if (ahead >= 0.0f) {
                                auto& o = pVis.objects[objIdx++];
                                o.kind = TrackObjKind::QSO_STOERER;
                                o.ahead = ahead;
                                o.lateral = std::clamp(qsoStoerer.lateralOffset, -0.9f, 0.9f);
                                o.healthFrac = std::clamp(qsoStoerer.health, 0.0f, 1.0f);
                                o.sizeParam = 0.0f;
                                o.intParam = 0;
                                o.highlight = false;
                            }
                        }

                        pVis.objectCount = objIdx;
                    }

                    // Status text for this player
                    std::string pStatus = "P" + std::to_string(p + 1);
                    if (!ctx->callsign.empty()) pStatus += " " + ctx->callsign;
                    // Speed scaled to display percentage, PA health as percentage
                    pStatus += " Spd:" + std::to_string(static_cast<int>(ctx->playerSpeed * 100.0f));
                    pStatus += " PA:" + formatPAHealth(ctx->paHealth) + "%";
                    std::strncpy(pVis.statusText, pStatus.c_str(), sizeof(pVis.statusText) - 1);
                    pVis.statusText[sizeof(pVis.statusText) - 1] = '\0';

                    sHamSpiritWindow.playerTrackVis[p] = pVis;
                }
            } else {
                sHamSpiritWindow.multiplayerCount = 1;
            }
        }
    }
}
#endif

// Update paused state
void Game::updatePausedState(float dt) {
    // Menu is shown, waiting for input
}

// Show game over
void Game::showGameOver() {
    log("HAMSPIRIT", "Showing game over screen");
    
    bool isMultiplayer = multiplayerMgr && multiplayerMgr->isMultiplayer();
    
    if (tts && tts->isAvailable()) {
        // Game over message
        speakTranslated("HAMSPIRIT_GAME_OVER", "Game Over!", true);
        waitForInput();
        if (shouldExit) { setState(GameState::EXITING); return; }
        
        // Reason
        if (stats.paHealth <= 0.0f) {
            speakTranslated("HAMSPIRIT_PA_FAILED", "Power amplifier failed!", true);
        } else {
            speakTranslated("HAMSPIRIT_COMPLETED", "Mission completed!", true);
        }
        waitForInput();
        if (shouldExit) { setState(GameState::EXITING); return; }
        
        if (isMultiplayer) {
            // ── Multiplayer Game Over: show stats for each player, ranked ──
            int playerCount = multiplayerMgr->getPlayerCount();
            
            // Build ranked list by score
            std::vector<int> ranking;
            for (int p = 0; p < playerCount; p++) ranking.push_back(p);
            std::sort(ranking.begin(), ranking.end(), [&](int a, int b) {
                auto* sa = multiplayerMgr->getPlayerStats(a);
                auto* sb = multiplayerMgr->getPlayerStats(b);
                return (sa ? sa->score : 0) > (sb ? sb->score : 0);
            });
            
            // Announce ranking
            speakTranslated("HAMSPIRIT_STATS_TITLE", "Game Statistics", true);
            waitForInput();
            if (shouldExit) { setState(GameState::EXITING); return; }
            
            tts->speak("Multiplayer results: " + std::to_string(playerCount) + " players.", shouldInterruptTts(true));
            waitForInput();
            if (shouldExit) { setState(GameState::EXITING); return; }
            
            // Show each player's stats in rank order
            for (int rank = 0; rank < static_cast<int>(ranking.size()); rank++) {
                int p = ranking[rank];
                auto* ctx = multiplayerMgr->getPlayer(p);
                auto* pStats = multiplayerMgr->getPlayerStats(p);
                if (!ctx || !pStats) continue;
                
                std::string playerLabel = "Player " + std::to_string(p + 1);
                if (!ctx->callsign.empty()) playerLabel += " (" + ctx->callsign + ")";
                if (!ctx->playerName.empty()) playerLabel = ctx->playerName + ", " + playerLabel;
                
                tts->speak("Rank " + std::to_string(rank + 1) + ": " + playerLabel, shouldInterruptTts(true));
                waitForInput();
                if (shouldExit) { setState(GameState::EXITING); return; }
                
                tts->speak("Score: " + std::to_string(pStats->score) + " points", shouldInterruptTts(true));
                waitForInput();
                if (shouldExit) { setState(GameState::EXITING); return; }
                
                tts->speak("Morse collected: " + std::to_string(pStats->charactersCollected), shouldInterruptTts(true));
                waitForInput();
                if (shouldExit) { setState(GameState::EXITING); return; }
                
                int healthPct = static_cast<int>(pStats->paHealth * 100.0f);
                tts->speak("PA health: " + std::to_string(healthPct) + " percent", shouldInterruptTts(true));
                waitForInput();
                if (shouldExit) { setState(GameState::EXITING); return; }
                
                if (pStats->bonusAchieved) {
                    tts->speak("HAMSPIRIT bonus achieved!", shouldInterruptTts(true));
                    waitForInput();
                    if (shouldExit) { setState(GameState::EXITING); return; }
                }
                
                // Add high score entry for this player
                addHighScoreEntryForPlayer(p);
                waitForInput(2.0f);
                if (shouldExit) { setState(GameState::EXITING); return; }
            }
        } else {
            // ── Singleplayer Game Over (original logic) ──
            // Statistics
            speakTranslated("HAMSPIRIT_STATS_TITLE", "Game Statistics", true);
            waitForInput();
            if (shouldExit) { setState(GameState::EXITING); return; }
            
            // Score
            if (translation) {
                tts->speak(translation->format("HAMSPIRIT_STATS_SCORE", "Final score: {0} points", stats.score), shouldInterruptTts(true));
            } else {
                tts->speak("Final score: " + std::to_string(stats.score) + " points", shouldInterruptTts(true));
            }
            waitForInput();
            if (shouldExit) { setState(GameState::EXITING); return; }
            
            // Time played
            int minutes = static_cast<int>(stats.gameTime) / 60;
            int seconds = static_cast<int>(stats.gameTime) % 60;
            if (translation) {
                tts->speak(translation->format("HAMSPIRIT_STATS_TIME", "Time played: {0} minutes and {1} seconds", minutes, seconds), shouldInterruptTts(true));
            } else {
                tts->speak("Time played: " + std::to_string(minutes) + " minutes and " + std::to_string(seconds) + " seconds", shouldInterruptTts(true));
            }
            waitForInput();
            if (shouldExit) { setState(GameState::EXITING); return; }
            
            // Laps
            if (translation) {
                tts->speak(translation->format("HAMSPIRIT_STATS_LAPS", "Laps completed: {0}", stats.lapsCompleted), shouldInterruptTts(true));
            } else {
                tts->speak("Laps completed: " + std::to_string(stats.lapsCompleted), shouldInterruptTts(true));
            }
            waitForInput();
            if (shouldExit) { setState(GameState::EXITING); return; }
            
            // Characters collected
            if (translation) {
                tts->speak(translation->format("HAMSPIRIT_STATS_CHARS", "Morse characters collected: {0}", stats.charactersCollected), shouldInterruptTts(true));
            } else {
                tts->speak("Morse characters collected: " + std::to_string(stats.charactersCollected), shouldInterruptTts(true));
            }
            waitForInput();
            if (shouldExit) { setState(GameState::EXITING); return; }
            
            // HAMSPIRIT bonus
            if (stats.bonusAchieved) {
                speakTranslated("HAMSPIRIT_BONUS_ACHIEVED", "HAMSPIRIT bonus achieved!", true);
                waitForInput();
                if (shouldExit) { setState(GameState::EXITING); return; }
            }
            
            // Average SWR
            if (translation) {
                tts->speak(translation->format("HAMSPIRIT_STATS_AVG_SWR", "Average SWR: {0}", 
                    std::to_string(static_cast<int>(stats.averageSWR * 10.0f) / 10.0f)), shouldInterruptTts(true));
            } else {
                tts->speak("Average SWR: " + std::to_string(static_cast<int>(stats.averageSWR * 10.0f) / 10.0f), shouldInterruptTts(true));
            }
            waitForInput();
            if (shouldExit) { setState(GameState::EXITING); return; }
            
            // Final PA health
            if (translation) {
                tts->speak(translation->format("HAMSPIRIT_STATS_PA_HEALTH", "Final PA health: {0} percent",
                    static_cast<int>(stats.paHealth * 100.0f)), shouldInterruptTts(true));
            } else {
                tts->speak("Final PA health: " + formatPAHealth(stats.paHealth) + " percent", shouldInterruptTts(true));
            }
            waitForInput();
            if (shouldExit) { setState(GameState::EXITING); return; }
            
            // Save high score entry and announce rank
            addHighScoreEntry();
            waitForInput(3.0f);
            if (shouldExit) { setState(GameState::EXITING); return; }
            
            // Player info in statistics
            if (!currentPlayerCallsign.empty()) {
                tts->speak(callsignToPhonetic(currentPlayerCallsign), shouldInterruptTts(true));
                waitForInput(3.0f);
                if (shouldExit) { setState(GameState::EXITING); return; }
            }
        }
        
        // Thank you message
        speakTranslated("HAMSPIRIT_THANKS", "Thank you for playing Ham Spirit!", true);
        waitForInput();
        if (shouldExit) { setState(GameState::EXITING); return; }
        
        // Play Again prompt — A to replay, B to return to main menu
        speakTranslated("HAMSPIRIT_PLAY_AGAIN", "Play again? Press A to replay, or B to return to the main menu.", true);
    }
    
    // Wait for explicit A (replay) or B (main menu) input
    while (!shouldExit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        pollKeyboard();
        GamepadState input = getCurrentInput();
        
        bool aBtn = input.buttons[static_cast<int>(GamepadButton::A)];
        bool bBtn = input.buttons[static_cast<int>(GamepadButton::B)];
        
        // Check keyboard: Enter = replay, Escape/Q = exit
        if (shouldExit) break;
        
        if (aBtn) {
            // A button = replay
            restartGame();
            return;
        }
        if (bBtn) {
            // B button = return to main menu
            setState(GameState::MAIN_MENU);
            return;
        }
    }
    
    setState(GameState::EXITING);
}

// Handle input
void Game::handleInput() {
    GamepadState input = getCurrentInput();
    
    GameState state = currentState.load();
    
    if (state == GameState::PLAYING) {
        // Check for pause button (edge-triggered: press to pause)
        bool startBtn = input.buttons[static_cast<int>(GamepadButton::START)];
        if (startBtn && !prevStartBtn) {
            triggerPauseSound();
            setState(GameState::PAUSED);
        }
        prevStartBtn = startBtn;
        
        // Check for Back/Select/View button — full status readout
        bool backBtn = input.buttons[static_cast<int>(GamepadButton::BACK)];
        if (backBtn && !prevBackBtn) {
            if (statusReadoutActive && tts && tts->isSpeaking()) {
                // Second press while readout is active — cancel it
                tts->stop();
                statusReadoutActive = false;
                log("HAMSPIRIT", "Status readout cancelled by double-press");
            } else {
                // First press — interrupt current TTS and start readout
                if (tts && tts->isAvailable()) {
                    tts->speak("", true);  // Interrupt current speech
                }
                statusReadoutActive = true;
                announceFullStatus();
            }
        }
        prevBackBtn = backBtn;
        
        // Track when status readout finishes speaking
        if (statusReadoutActive && tts && !tts->isSpeaking()) {
            statusReadoutActive = false;
        }
    }
    else if (state == GameState::PAUSED) {
        // Check for resume via Start button (same button toggles)
        // Only allow resume when there's actually a game in progress —
        // if config was opened from the main menu, pressing Start should
        // not transition to PLAYING (there's no game to resume).
        bool startBtn = input.buttons[static_cast<int>(GamepadButton::START)];
        if (startBtn && !prevStartBtn && !configCalledFromMainMenu) {
            triggerUnpauseSound();
            setState(GameState::PLAYING);
            prevStartBtn = startBtn;
            return;
        }
        prevStartBtn = startBtn;
        
        if (inConfigMenu) {
            handleConfigInput(input);
        } else if (inSoundLearning) {
            handleSoundLearningInput(input);
        } else {
            handleMenuInput(input);
        }
    }
}

/**
 * Poll keyboard via GetAsyncKeyState and feed key events to the keyboard emulator.
 * All keyboard input is read via GetAsyncKeyState.
 * Console input (kbhit/getKey) is no longer used.
 * ESC triggers immediate game exit from any state.
 * Keys are pressed and released immediately (tap simulation).
 *
 * Keyboard events are only processed when the game GUI window or the
 * console window is in the foreground to prevent capturing input destined
 * for other applications.
 */
void Game::pollKeyboard() {
#ifdef _WIN32
    // Release any keys that were pressed last frame
    if (keyboard) {
        for (int key : lastPressedKeys) {
            keyboard->handleKeyEvent(key, false);
        }
    }
    lastPressedKeys.clear();
    
    // Only process keyboard input when our window is focused
    HWND fg = GetForegroundWindow();
    bool focused = (sHamSpiritWindow.hwnd && fg == sHamSpiritWindow.hwnd) ||
                   (sHamSpiritWindow.consoleHwnd && fg == sHamSpiritWindow.consoleHwnd);
    if (!focused) return;
    
    // Flush any pending console input so typed characters don't accumulate
    // in the console input buffer while the GUI window is focused.
    {
        HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
        if (hInput && hInput != INVALID_HANDLE_VALUE) {
            FlushConsoleInputBuffer(hInput);
        }
    }
    
    // Build VK→VK mapping from config.keyMapping (remappable)
    // GetAsyncKeyState returns negative if key is currently pressed.
    // Send VK codes directly to handleKeyEvent so they match the keyboard
    // emulator's keyStates[] indices (which use VK codes / uppercase letters).
    struct VKMapping { int vk; int logicalKey; };
    const KeyMapping& km = config.keyMapping;
    VKMapping mappings[] = {
        { VK_ESCAPE,           VK_ESCAPE },
        { km.steerLeft,        km.steerLeft },
        { km.steerRight,       km.steerRight },
        { km.accelerate,       km.accelerate },
        { km.brake,            km.brake },
        { km.aimLeft,          km.aimLeft },
        { km.aimRight,         km.aimRight },
        { km.aimUp,            km.aimUp },
        { km.aimDown,          km.aimDown },
        { km.morseKey,         km.morseKey },
        { VK_RETURN,           VK_RETURN },
        { VK_BACK,             VK_BACK },
        { km.noiseBlanker,     km.noiseBlanker },
        { km.paddleDot,        km.paddleDot },
        { km.paddleDash,       km.paddleDash },
        { km.inductanceUp,     km.inductanceUp },
        { km.inductanceDown,   km.inductanceDown },
        { km.capacitanceUp,    km.capacitanceUp },
        { km.capacitanceDown,  km.capacitanceDown },
        { km.ununUp,           km.ununUp },
        { km.ununDown,         km.ununDown },
        { km.weaponPrev,       km.weaponPrev },
        { km.weaponNext,       km.weaponNext },
        { km.pause,            km.pause },
        { km.statusReadout,    km.statusReadout },
        { VK_F1,               VK_F1 },
    };
    
    for (const auto& m : mappings) {
        if (GetAsyncKeyState(m.vk) & 0x8000) {
            // ESC exits the game from any state
            if (m.vk == VK_ESCAPE) {
                log("HAMSPIRIT", "Exit key pressed (ESC) via GUI window");
                shouldExit = true;
                setState(GameState::EXITING);
                return;
            }
            if (keyboard) {
                keyboard->handleKeyEvent(m.logicalKey, true);
                lastPressedKeys.push_back(m.logicalKey);
            }
        }
    }
#else
    // POSIX: Read key events and forward to keyboard emulator.
    // Release any keys that were pressed last frame (tap simulation).
    if (keyboard) {
        for (int key : lastPressedKeys) {
            keyboard->handleKeyEvent(key, false);
        }
    }
    lastPressedKeys.clear();
    
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
    // macOS GUI mode: read key events from the NSWindow key queue
    {
        int vk = 0;
        bool pressed = false;
        while (pollHamSpiritKeyEvent(vk, pressed)) {
            if (vk < 0) continue;
            
            // ESC exits the game from any state
            if (vk == 0x1B && pressed) {
                log("HAMSPIRIT", "Exit key pressed (ESC) via GUI window");
                shouldExit = true;
                setState(GameState::EXITING);
                return;
            }
            if (keyboard && pressed) {
                keyboard->handleKeyEvent(vk, true);
                lastPressedKeys.push_back(vk);
            }
        }
    }
#else
    if (consoleInput) {
        while (consoleInput->kbhit()) {
            int key = consoleInput->getKey();
            if (key == KEY_ERROR || key == KEY_UNKNOWN) continue;
            
            int vk = logicalKeyToVK(key);
            
            // ESC exits the game from any state
            if (vk == 0x1B) {
                log("HAMSPIRIT", "Exit key pressed (ESC) via console");
                shouldExit = true;
                setState(GameState::EXITING);
                return;
            }
            if (keyboard) {
                keyboard->handleKeyEvent(vk, true);
                lastPressedKeys.push_back(vk);
            }
        }
    }
#endif
#endif
}

/**
 * Wait for any key press or gamepad button.
 * Returns true if input received, false if timeout or exit requested.
 */
/**
 * Wait for any key press or gamepad button.
 * All keyboard input via GetAsyncKeyState (GUI window).
 * Drains buffered input first, waits for button release, then waits for NEW input.
 * Returns true if input received, false if exit requested.
 */
bool Game::waitForInput(float timeoutSeconds) {
    log("HAMSPIRIT_WAIT", "waitForInput(timeout=" + std::to_string(timeoutSeconds) + "s) entered");
    // 1. Wait for TTS to finish speaking while checking for user skip input
    if (tts && tts->isAvailable()) {
        bool skipped = false;
        while (tts->isSpeaking() && !skipped) {
            if (shouldExit) return false;
            
#ifdef _WIN32
            // Check for skip via GetAsyncKeyState (no focus check needed —
            // GetAsyncKeyState works globally and we're the only game running)
            {
                if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                    shouldExit = true;
                    tts->stop();
                    return false;
                }
                // Any key press skips TTS
                if ((GetAsyncKeyState(VK_RETURN) & 0x8000) ||
                    (GetAsyncKeyState(VK_SPACE) & 0x8000) ||
                    (GetAsyncKeyState('A') & 0x8000)) {
                    tts->stop();
                    skipped = true;
                    break;
                }
            }
#else
            // POSIX: Check for skip input
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
            // macOS GUI: check key queue from NSWindow
            {
                int vk = 0;
                bool pressed = false;
                while (pollHamSpiritKeyEvent(vk, pressed)) {
                    if (!pressed) continue;
                    if (vk == 0x1B) {
                        shouldExit = true;
                        tts->stop();
                        return false;
                    }
                    tts->stop();
                    skipped = true;
                    break;
                }
            }
#else
            if (consoleInput && consoleInput->kbhit()) {
                int key = consoleInput->getKey();
                int vk = logicalKeyToVK(key);
                if (vk == 0x1B) {
                    shouldExit = true;
                    tts->stop();
                    return false;
                }
                tts->stop();
                skipped = true;
                break;
            }
#endif
#endif
            // Check gamepad for skip
            if (gamepad) {
                gamepad->update();
                if (gamepad->isConnected()) {
                    GamepadState state = gamepad->getState();
                    for (int i = 0; i < static_cast<int>(GamepadButton::COUNT); i++) {
                        if (state.buttons[i]) {
                            tts->stop();
                            skipped = true;
                            break;
                        }
                    }
                }
            }
            if (!skipped) {
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
                pumpHamSpiritEvents();
#endif
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

    // 2. Wait for gamepad buttons to be released (prevent held-button skip of next message)
    if (gamepad && gamepad->isConnected()) {
        auto releaseStart = std::chrono::steady_clock::now();
        while (std::chrono::duration<float>(std::chrono::steady_clock::now() - releaseStart).count() < 0.5f) {
            if (shouldExit) return false;
            gamepad->update();
            GamepadState state = gamepad->getState();
            bool anyPressed = false;
            for (int i = 0; i < static_cast<int>(GamepadButton::COUNT); i++) {
                if (state.buttons[i]) { anyPressed = true; break; }
            }
            if (!anyPressed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    // 3. Small debounce delay (replaces old console input drain)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 4. Wait for NEW input to advance to next message
    auto startWait = std::chrono::steady_clock::now();
    while (std::chrono::duration<float>(std::chrono::steady_clock::now() - startWait).count() < timeoutSeconds) {
        if (shouldExit) return false;
        
#ifdef _WIN32
        // Check for key input via GetAsyncKeyState (no focus check needed)
        {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                shouldExit = true;
                return false;
            }
            if ((GetAsyncKeyState(VK_RETURN) & 0x8000) ||
                (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
                return true;
            }
        }
#else
        // POSIX: Check for key press
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
        // macOS GUI: check key queue from NSWindow
        {
            int vk = 0;
            bool pressed = false;
            while (pollHamSpiritKeyEvent(vk, pressed)) {
                if (!pressed) continue;
                if (vk == 0x1B) {
                    shouldExit = true;
                    return false;
                }
                return true;  // Any key press advances
            }
        }
#else
        if (consoleInput && consoleInput->kbhit()) {
            int key = consoleInput->getKey();
            int vk = logicalKeyToVK(key);
            if (vk == 0x1B) {
                shouldExit = true;
                return false;
            }
            if (vk == 0x0D || vk == 0x20) {
                return true;
            }
            // Any other key also advances
            return true;
        }
#endif
#endif
        // Check gamepad
        if (gamepad) {
            gamepad->update();
            if (gamepad->isConnected()) {
                GamepadState state = gamepad->getState();
                for (int i = 0; i < static_cast<int>(GamepadButton::COUNT); i++) {
                    if (state.buttons[i]) return true;
                }
            }
        }
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
        pumpHamSpiritEvents();
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;  // Timeout — continue anyway
}
// Get current input state (gamepad or keyboard)
GamepadState Game::getCurrentInput() {
    static GamepadState lastState;
    GamepadState currentInput;

    // During PLAYING state, only read from player 0's assigned input device
    // so that only the chosen controller/keyboard steers the character.
    // In menus and other states, merge ALL controllers as before so every
    // gamepad can navigate the shared UI.
    bool isPlaying = currentState == GameState::PLAYING;
    bool mpPlaying = multiplayerMgr && multiplayerMgr->isMultiplayer() && isPlaying;
    // Singleplayer with explicit input assignment
    bool spAssigned = isPlaying && !mpPlaying
                      && multiplayerConfig.playerCount == 1;

    if (gamepad) {
        gamepad->update();

        if (mpPlaying) {
            // Multiplayer gameplay: read ONLY from player 0's assigned device.
            auto* ctx0 = multiplayerMgr->getPlayer(0);
            if (ctx0 && ctx0->inputAssignment.type == InputSourceType::GAMEPAD) {
                int gIdx = ctx0->inputAssignment.gamepadIndex;
                if (gamepad->isConnected(gIdx)) {
                    currentInput = gamepad->getState(gIdx);
                    currentInput.connected = true;
                }
            }
            // If player 0 is on keyboard, do NOT merge any gamepad here —
            // the keyboard path below will provide input.
        } else if (spAssigned && multiplayerConfig.inputAssignments[0].type == InputSourceType::GAMEPAD) {
            // Singleplayer with gamepad assignment: read ONLY from assigned gamepad
            int gIdx = multiplayerConfig.inputAssignments[0].gamepadIndex;
            if (gamepad->isConnected(gIdx)) {
                currentInput = gamepad->getState(gIdx);
                currentInput.connected = true;
            }
        } else if (spAssigned && multiplayerConfig.inputAssignments[0].type == InputSourceType::KEYBOARD) {
            // Singleplayer with keyboard assignment: do NOT merge any gamepad
        } else {
            // Menus / pre-game: merge ALL connected controllers
            for (int c = 0; c < 4; c++) {
                if (!gamepad->isConnected(c)) continue;
                GamepadState gs = gamepad->getState(c);
                for (int i = 0; i < static_cast<int>(GamepadButton::COUNT); i++) {
                    if (gs.buttons[i]) currentInput.buttons[i] = true;
                }
                for (int i = 0; i < static_cast<int>(GamepadAxis::COUNT); i++) {
                    if (std::abs(gs.axes[i]) > std::abs(currentInput.axes[i])) {
                        currentInput.axes[i] = gs.axes[i];
                    }
                }
                currentInput.connected = true;
            }
        }

        // Apply stick drift calibration offsets (subtract center bias)
        currentInput.axes[static_cast<int>(GamepadAxis::LEFT_X)]  -= config.stickOffsetLX;
        currentInput.axes[static_cast<int>(GamepadAxis::LEFT_Y)]  -= config.stickOffsetLY;
        currentInput.axes[static_cast<int>(GamepadAxis::RIGHT_X)] -= config.stickOffsetRX;
        currentInput.axes[static_cast<int>(GamepadAxis::RIGHT_Y)] -= config.stickOffsetRY;
    }

    // Keyboard emulator: during gameplay, only merge if player 0
    // is assigned to keyboard; in menus, always merge.
    bool mergeKeyboard = true;
    if (mpPlaying) {
        auto* ctx0 = multiplayerMgr->getPlayer(0);
        mergeKeyboard = ctx0 && ctx0->inputAssignment.type == InputSourceType::KEYBOARD;
    } else if (spAssigned) {
        mergeKeyboard = multiplayerConfig.inputAssignments[0].type == InputSourceType::KEYBOARD;
    }

    if (keyboard && mergeKeyboard) {
        GamepadState kbState;
        keyboard->update(kbState, deltaTime);
        for (int i = 0; i < static_cast<int>(GamepadButton::COUNT); i++) {
            if (kbState.buttons[i]) currentInput.buttons[i] = true;
        }
        for (int i = 0; i < static_cast<int>(GamepadAxis::COUNT); i++) {
            if (std::abs(kbState.axes[i]) > std::abs(currentInput.axes[i])) {
                currentInput.axes[i] = kbState.axes[i];
            }
        }
        currentInput.connected = true;
    }
    
    // Store for next frame (for edge detection)
    lastState = currentInput;
    return currentInput;
}

int Game::getPlayer0GamepadIndex() const {
    return getPlayerGamepadIndex(0);
}

int Game::getPlayerGamepadIndex(int playerIndex) const {
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        auto* ctx = multiplayerMgr->getPlayer(playerIndex);
        if (ctx && ctx->inputAssignment.type == InputSourceType::GAMEPAD)
            return ctx->inputAssignment.gamepadIndex;
        return -1;
    }
    // Singleplayer: respect input assignment (only player 0)
    if (playerIndex == 0 && multiplayerConfig.inputAssignments[0].type == InputSourceType::GAMEPAD)
        return multiplayerConfig.inputAssignments[0].gamepadIndex;
    return -1;  // Keyboard only — no gamepad vibration
}

void Game::setVibrationForPlayer(int playerIndex, float leftMotor, float rightMotor) {
    if (!gamepad) return;
    int gpIdx = getPlayerGamepadIndex(playerIndex);
    if (gpIdx >= 0 && gamepad->isConnected(gpIdx)) {
        try { gamepad->setVibration(gpIdx, leftMotor, rightMotor); } catch (...) {}
    }
}

void Game::stopAllVibration() {
    if (!gamepad) return;
    // Stop vibration for the primary player (singleplayer or player 0)
    setVibrationForPlayer(0, 0.0f, 0.0f);
    // Stop vibration for all secondary players in multiplayer
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        for (int p = 1; p < multiplayerMgr->getPlayerCount(); p++) {
            setVibrationForPlayer(p, 0.0f, 0.0f);
        }
    }
}

GamepadState Game::getInputForPlayer(int playerIndex) {
    GamepadState state;

    // Determine input assignment for the requested player
    PlayerInputAssignment assignment;
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        auto* ctx = multiplayerMgr->getPlayer(playerIndex);
        if (!ctx) return state;
        assignment = ctx->inputAssignment;
    } else {
        if (playerIndex >= 0 && playerIndex < MAX_PLAYERS)
            assignment = multiplayerConfig.inputAssignments[playerIndex];
    }

    // Read raw input from the assigned device
    if (assignment.type == InputSourceType::GAMEPAD && gamepad) {
        int gIdx = assignment.gamepadIndex;
        // Apply per-player controller preset so that axis mapping matches the
        // player's physical controller type (Xbox vs PS).  Without this, all
        // players use player 0's preset which can mis-map triggers and right
        // stick on macOS when mixed controller types are connected.
        int playerPreset = 0;
        if (playerIndex >= 0 && playerIndex < MAX_PLAYERS)
            playerPreset = multiplayerConfig.controllerPresets[playerIndex];
        gamepad->setControllerPreset(playerPreset);
        if (gamepad->isConnected(gIdx)) {
            state = gamepad->getState(gIdx);
            state.connected = true;
        }
    } else if (assignment.type == InputSourceType::KEYBOARD && keyboard) {
        keyboard->update(state, deltaTime);
        state.connected = true;
    }

    // Apply stick drift calibration offsets (same for all players)
    state.axes[static_cast<int>(GamepadAxis::LEFT_X)]  -= config.stickOffsetLX;
    state.axes[static_cast<int>(GamepadAxis::LEFT_Y)]  -= config.stickOffsetLY;
    state.axes[static_cast<int>(GamepadAxis::RIGHT_X)] -= config.stickOffsetRX;
    state.axes[static_cast<int>(GamepadAxis::RIGHT_Y)] -= config.stickOffsetRY;

    return state;
}

// Show pause menu
void Game::showPauseMenu() {
    log("HAMSPIRIT", "Showing pause menu");
    inConfigMenu = false;
    inSoundLearning = false;
    configCalledFromMainMenu = false;
    currentMenuOption = MenuOption::RESUME;
    speakTranslated("HAMSPIRIT_MENU_TITLE", "Ham Spirit - Pause Menu", true);
    // Update visual menu overlay
#ifdef _WIN32
    {
        std::vector<std::string> items = {"Resume", "Configuration", "Learn Sounds", "Restart", "Main Menu"};
        updateMenuOverlay("Pause", items, static_cast<int>(currentMenuOption));
    }
#endif
}

// Speak the currently selected menu option
void Game::speakCurrentMenuOption() {
    if (!tts || !tts->isAvailable()) return;
    
    switch (currentMenuOption) {
        case MenuOption::RESUME:
            speakTranslated("HAMSPIRIT_MENU_RESUME", "Resume", true);
            break;
        case MenuOption::CONFIGURE:
            speakTranslated("HAMSPIRIT_MENU_CONFIG", "Configuration", true);
            break;
        case MenuOption::LEARN_SOUNDS:
            tts->speak("Learn Sounds", shouldInterruptTts(true));
            break;
        case MenuOption::RESTART:
            speakTranslated("HAMSPIRIT_MENU_RESTART", "Restart", true);
            break;
        case MenuOption::MAIN_MENU:
            tts->speak("Main Menu", shouldInterruptTts(true));
            break;
    }
    // Update visual menu overlay
#ifdef _WIN32
    {
        std::vector<std::string> items = {"Resume", "Configuration", "Learn Sounds", "Restart", "Main Menu"};
        updateMenuOverlay("Pause", items, static_cast<int>(currentMenuOption));
    }
#endif
}

// Handle menu input with D-pad navigation
void Game::handleMenuInput(const GamepadState& input) {
    // Member variables: prevMenuUp, prevMenuDown, prevMenuA (reset in restartGame)
    
    bool rawUp = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                 || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -STICK_MENU_DEADZONE;
    bool rawDown = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                   || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > STICK_MENU_DEADZONE;
    bool rawLeft = input.buttons[static_cast<int>(GamepadButton::DPAD_LEFT)]
                   || input.axes[static_cast<int>(GamepadAxis::LEFT_X)] < -STICK_MENU_DEADZONE;
    bool rawRight = input.buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)]
                    || input.axes[static_cast<int>(GamepadAxis::LEFT_X)] > STICK_MENU_DEADZONE;
    bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
    
    // D-pad axis priority: suppress up/down when left/right is pressed
    bool up = rawUp && !rawLeft && !rawRight;
    bool down = rawDown && !rawLeft && !rawRight;
    
    // Debounce timer
    if (dpadDebounceTimer > 0.0f) {
        dpadDebounceTimer -= deltaTime;
        up = false;
        down = false;
    }
    
    // Navigate up (edge-triggered)
    if (up && !prevMenuUp) {
        int opt = static_cast<int>(currentMenuOption);
        if (opt > 0) {
            currentMenuOption = static_cast<MenuOption>(opt - 1);
            triggerMenuNavSound();
            speakCurrentMenuOption();
            dpadDebounceTimer = DPAD_DEBOUNCE_TIME;
        } else {
            triggerBumperSound();
        }
    }
    
    // Navigate down (edge-triggered) — MAIN_MENU is the last option in pause menu
    if (down && !prevMenuDown) {
        int opt = static_cast<int>(currentMenuOption);
        if (opt < static_cast<int>(MenuOption::MAIN_MENU)) {
            currentMenuOption = static_cast<MenuOption>(opt + 1);
            triggerMenuNavSound();
            speakCurrentMenuOption();
            dpadDebounceTimer = DPAD_DEBOUNCE_TIME;
        } else {
            triggerBumperSound();
        }
    }
    
    // Select (edge-triggered)
    if (accept && !prevMenuA) {
        triggerMenuSelectSound();
        log("HAMSPIRIT", "Menu selected: " + std::to_string(static_cast<int>(currentMenuOption)));
        switch (currentMenuOption) {
            case MenuOption::RESUME:
                setState(GameState::PLAYING);
                break;
            case MenuOption::CONFIGURE:
                configCalledFromMainMenu = false;
                showConfigMenu();
                break;
            case MenuOption::LEARN_SOUNDS:
                soundLearningFromMainMenu = false;
                inSoundLearning = true;
                soundLearningIndex = 0;
                runSoundLearningMenu();
                break;
            case MenuOption::RESTART:
                // Reset game state
                playerAngle = 0.0f;
                playerSpeed = 0.0f;
                stats = GameStats{};
                setState(GameState::PLAYING);
                break;
            case MenuOption::MAIN_MENU:
                // Return to main menu
                setState(GameState::MAIN_MENU);
                break;
        }
    }
    
    prevMenuUp = up;
    prevMenuDown = down;
    prevMenuA = accept;
}

// Show config menu
// Speaker test: announce and play test tones for Left, Right, Center
void Game::runSpeakerTest() {
    if (!tts || !tts->isAvailable() || !audioEngine || !audioBackend) return;
    log("HAMSPIRIT", "Running speaker test");
    
    auto playTestTone = [&](double pan, int durationMs) {
        const int samples = GAME_SAMPLE_RATE * durationMs / 1000;
        const size_t bufSize = samples * GAME_CHANNELS;
        std::vector<int16_t> buf(bufSize, 0);
        audioEngine->generateAudio(buf, samples, 1, 440.0, pan, 80);
        audioBackend->playBuffer(buf.data(), samples, GAME_SAMPLE_RATE, GAME_CHANNELS, GAME_BITS);
    };
    
    // Left speaker
    tts->speak("Left Speaker", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    playTestTone(0.0, 500);  // Pan full left
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    
    // Right speaker
    tts->speak("Right Speaker", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    playTestTone(1.0, 500);  // Pan full right
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    
    // Center
    tts->speak("Center", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    playTestTone(0.5, 500);  // Pan center
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    
    tts->speak("Speaker test complete", true);
    log("HAMSPIRIT", "Speaker test complete");
}

// ============================================================================
// Sound Learning Menu — lets the player listen to all game sounds
// ============================================================================

// Sound entry: name, description, and how to trigger it
struct SoundLearningEntry {
    const char* name;        // Short name (spoken and displayed)
    const char* description; // What happens in-game when you hear this
    bool hasVibration;       // Whether this sound has an associated vibration event
};

// All game sounds the player can learn, in logical groups
static const SoundLearningEntry sSoundEntries[] = {
    // --- Driving ---
    {"Tuner Adjustment",
     "Played when adjusting inductance or capacitance on the antenna tuner.",
     false},
    {"Border Bumper",
     "Short buzz when hitting the edge of a menu or tuner range limit.",
     false},
    {"Emergency Brake",
     "Tire screech when you press the emergency brake.",
     true},
    {"Pause",
     "Descending tone when the game is paused.",
     false},
    {"Unpause",
     "Ascending tone when the game is resumed.",
     false},
    // --- Morse & Collection ---
    {"Morse Collected",
     "Success chime when you collect a morse character.",
     false},
    {"Wrong Aim",
     "Descending buzz — correct morse sent, but wrong aim direction.",
     false},
    {"Wrong Morse",
     "Error tone — wrong morse character sent.",
     false},
    // --- PA Health ---
    {"PA Damage",
     "Crackle and pop — your power amplifier took damage from bad SWR.",
     true},
    {"PA Repair",
     "Ascending chime — your PA is being repaired by good SWR.",
     false},
    // --- Weapons ---
    {"Noise Blanker Fire",
     "Laser-like zap when firing the noise blanker.",
     false},
    {"Noise Hit",
     "Metallic impact when the noise blanker hits an interference source.",
     false},
    {"Noise Destroyed",
     "Explosion when an interference source is eliminated.",
     false},
    {"Aim Reset",
     "Swoosh sound when resetting your turret aim to center.",
     false},
    // --- Power-Ups ---
    {"Power-Up Activate",
     "Fanfare when a power-up is collected and activated.",
     true},
    {"Power-Up Expire",
     "Sound when an active power-up effect expires.",
     false},
    {"Power-Up Explosion",
     "Explosion when a power-up is shot and destroyed.",
     true},
    // --- QSO Störer ---
    {"QSO Störer Collision",
     "Harsh noise when colliding with the QSO interferer.",
     true},
    // --- UI ---
    {"Menu Navigate",
     "Short click when moving between menu items.",
     false},
    {"Menu Select",
     "Confirmation beep when selecting a menu item.",
     false},
    {"Key Click",
     "Soft click played for each keystroke during text input.",
     false},
    {"Status Readout Start",
     "Two-tone chime when starting a status readout.",
     false},
    {"Status Readout Done",
     "Confirmation ding when the status readout is finished.",
     false},
    // --- End ---
    {"Back",
     "Return to the previous menu.",
     false},
};

static constexpr int SOUND_ENTRY_COUNT = sizeof(sSoundEntries) / sizeof(sSoundEntries[0]);

void Game::runSoundLearningMenu() {
    log("HAMSPIRIT", "Opening sound learning menu");
    inSoundLearning = true;
    soundLearningIndex = 0;
    if (tts && tts->isAvailable()) {
        tts->speak("Learn Sounds", shouldInterruptTts(true));
    }
    speakCurrentSoundEntry();
}

void Game::speakCurrentSoundEntry() {
    if (soundLearningIndex < 0 || soundLearningIndex >= SOUND_ENTRY_COUNT) return;
    const auto& entry = sSoundEntries[soundLearningIndex];
    if (tts && tts->isAvailable()) {
        std::string msg = std::string(entry.name) + ". " + entry.description;
        tts->speak(msg, shouldInterruptTts(true));
    }
    updateBrailleDisplay(entry.name);
    // Update visual menu overlay
#ifdef _WIN32
    {
        std::vector<std::string> items;
        items.reserve(SOUND_ENTRY_COUNT);
        for (int i = 0; i < SOUND_ENTRY_COUNT; i++) {
            items.push_back(sSoundEntries[i].name);
        }
        updateMenuOverlay("Learn Sounds", items, soundLearningIndex);
    }
#endif
}

void Game::handleSoundLearningInput(const GamepadState& input) {
    // Count down vibration demo timer (non-blocking)
    if (soundLearningVibTimer > 0.0f) {
        soundLearningVibTimer -= deltaTime;
        if (soundLearningVibTimer <= 0.0f) {
            soundLearningVibTimer = 0.0f;
            setVibrationForPlayer(0, 0.0f, 0.0f);
        }
    }
    
    bool rawUp = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                 || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -STICK_MENU_DEADZONE;
    bool rawDown = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                   || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > STICK_MENU_DEADZONE;
    bool rawLeft = input.buttons[static_cast<int>(GamepadButton::DPAD_LEFT)]
                   || input.axes[static_cast<int>(GamepadAxis::LEFT_X)] < -STICK_MENU_DEADZONE;
    bool rawRight = input.buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)]
                    || input.axes[static_cast<int>(GamepadAxis::LEFT_X)] > STICK_MENU_DEADZONE;
    bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
    bool back = input.buttons[static_cast<int>(GamepadButton::B)];
    
    bool up = rawUp && !rawLeft && !rawRight;
    bool down = rawDown && !rawLeft && !rawRight;
    
    if (dpadDebounceTimer > 0.0f) {
        dpadDebounceTimer -= deltaTime;
        up = false;
        down = false;
    }
    
    // Navigate up
    if (up && !prevMenuUp) {
        if (soundLearningIndex > 0) {
            soundLearningIndex--;
            triggerMenuNavSound();
            speakCurrentSoundEntry();
            dpadDebounceTimer = DPAD_DEBOUNCE_TIME;
        } else {
            triggerBumperSound();
        }
    }
    
    // Navigate down
    if (down && !prevMenuDown) {
        if (soundLearningIndex < SOUND_ENTRY_COUNT - 1) {
            soundLearningIndex++;
            triggerMenuNavSound();
            speakCurrentSoundEntry();
            dpadDebounceTimer = DPAD_DEBOUNCE_TIME;
        } else {
            triggerBumperSound();
        }
    }
    
    // Accept: play the selected sound (and vibration if applicable)
    if (accept && !prevMenuA) {
        const auto& entry = sSoundEntries[soundLearningIndex];
        
        // Check if this is the "Back" entry (last entry)
        if (soundLearningIndex == SOUND_ENTRY_COUNT - 1) {
            triggerMenuSelectSound();
            inSoundLearning = false;
            if (soundLearningFromMainMenu) {
                speakMainMenuOption();
            } else {
                showPauseMenu();
            }
            prevMenuUp = up; prevMenuDown = down; prevMenuA = accept;
            return;
        }
        
        log("HAMSPIRIT", "Sound learning: playing " + std::string(entry.name));
        
        // Play the corresponding sound
        // Map index to trigger function (same order as sSoundEntries)
        switch (soundLearningIndex) {
            case 0:  triggerAdjustmentSound(true); break;      // Tuner Adjustment
            case 1:  triggerBumperSound(); break;               // Border Bumper
            case 2:  triggerEmergencyBrakeSound(); break;       // Emergency Brake
            case 3:  triggerPauseSound(); break;                // Pause
            case 4:  triggerUnpauseSound(); break;              // Unpause
            case 5:  triggerCollectSound(); break;              // Morse Collected
            case 6:  triggerMissAimSound(); break;              // Wrong Aim
            case 7:  triggerMissMorseSound(); break;            // Wrong Morse
            case 8:  triggerPaDamageSound(); break;             // PA Damage
            case 9:  triggerPaRepairSound(); break;             // PA Repair
            case 10: triggerNoiseBlankerFireSound(); break;     // Noise Blanker Fire
            case 11: triggerNoiseHitSound(2); break;            // Noise Hit
            case 12: triggerNoiseDestroyedSound(); break;       // Noise Destroyed
            case 13: triggerAimResetSound(); break;             // Aim Reset
            case 14: triggerPowerUpActivateSound(PowerUpType::SPEED_BOOST); break; // PU Activate
            case 15: triggerPowerUpExpireSound(); break;        // PU Expire
            case 16: triggerPowerUpExplodeSound(0.5f, 0.8f); break; // PU Explosion
            case 17: triggerQSOStoererCollisionSound(); break;  // QSO Störer Collision
            case 18: triggerMenuNavSound(); break;              // Menu Navigate
            case 19: triggerMenuSelectSound(); break;           // Menu Select
            case 20: triggerKeyClickSound(); break;             // Key Click
            case 21: triggerStatusStartSound(); break;          // Status Readout Start
            case 22: triggerStatusDoneSound(); break;           // Status Readout Done
            default: break;
        }
        
        // Simulate vibration for sounds that have it.
        // Use crashVibrationTimer mechanism: set vibration and schedule stop
        // via a dedicated timer that the game loop will count down.
        if (entry.hasVibration && gamepad && gamepad->isConnected() && config.swrVibration) {
            float leftMotor = 0.0f, rightMotor = 0.0f;
            switch (soundLearningIndex) {
                case 2:  // Emergency Brake — strong rumble
                    leftMotor = 0.8f; rightMotor = 0.3f;
                    break;
                case 8:  // PA Damage — sharp impact
                    leftMotor = 0.6f; rightMotor = 0.6f;
                    break;
                case 14: // Power-Up Activate — celebratory pulse
                    leftMotor = 0.3f; rightMotor = 0.7f;
                    break;
                case 16: // Power-Up Explosion — heavy boom
                    leftMotor = 1.0f; rightMotor = 1.0f;
                    break;
                case 17: // QSO Störer Collision — harsh impact
                    leftMotor = 1.0f; rightMotor = 0.5f;
                    break;
                default:
                    leftMotor = 0.4f; rightMotor = 0.4f;
                    break;
            }
            setVibrationForPlayer(0, leftMotor * config.vibrationIntensity,
                                    rightMotor * config.vibrationIntensity);
            // Use crashVibrationTimer to auto-stop vibration after 400ms.
            // The timer is counted down in updatePlayingState, but we also
            // count it down here in the sound learning loop via deltaTime.
            soundLearningVibTimer = 0.4f;
        }
    }
    
    // B button: go back to previous menu
    if (back && !prevMenuA) {
        triggerMenuSelectSound();
        inSoundLearning = false;
        if (soundLearningFromMainMenu) {
            speakMainMenuOption();
        } else {
            showPauseMenu();
        }
    }
    
    prevMenuUp = up; prevMenuDown = down; prevMenuA = accept;
}

void Game::showConfigMenu() {
    log("HAMSPIRIT", "Showing config menu");
    inConfigMenu = true;
    inConfigSubMenu = false;
    currentConfigCategory = ConfigCategory::TRACK;
    currentSubOptionIndex = 0;
    speakCurrentConfigCategory();
}

// Get options for a category
std::vector<Game::ConfigOption> Game::getOptionsForCategory(ConfigCategory cat) const {
    switch (cat) {
        case ConfigCategory::TRACK:
            return { ConfigOption::TRACK_CURVE, ConfigOption::DIFFICULTY, ConfigOption::LAP_COUNT, ConfigOption::SUB_BACK };
        case ConfigCategory::CONTROLS:
            return { ConfigOption::STEERING_SENS, ConfigOption::ACCEL_SENS, ConfigOption::AIM_SENS, ConfigOption::INPUT_DEADZONE, ConfigOption::PADDLE_SWAP, ConfigOption::SUB_BACK };
        case ConfigCategory::AUDIO:
            return { ConfigOption::MOTOR_VOLUME, ConfigOption::SWR_VOLUME, ConfigOption::MORSE_VOLUME, ConfigOption::WARNING_VOLUME, ConfigOption::COLLISION_VOLUME, ConfigOption::ENEMY_VOLUME, ConfigOption::UI_VOLUME, ConfigOption::SUB_BACK };
        case ConfigCategory::VIBRATION:
            return { ConfigOption::VIBRATION_ENABLED, ConfigOption::VIBRATION_INTENSITY, ConfigOption::SUB_BACK };
        case ConfigCategory::SPEECH:
            return { ConfigOption::TTS_ENGINE, ConfigOption::TTS_SPEED, ConfigOption::TTS_VOICE, ConfigOption::SUB_BACK };
        case ConfigCategory::ASSIST:
            return { ConfigOption::AIM_ASSIST, ConfigOption::TRAFFIC_REPORTS, ConfigOption::EMERGENCY_BRAKE, ConfigOption::NOISE_ALERTS, ConfigOption::INTRUDER_MONITORING, ConfigOption::BORDER_WARNING, ConfigOption::CURVE_ANNOUNCEMENT, ConfigOption::CURVE_ANNOUNCE_DIST, ConfigOption::SUB_BACK };
        case ConfigCategory::WEAPONS:
            return { ConfigOption::NOISE_BLANKER, ConfigOption::SUB_BACK };
        case ConfigCategory::BRAILLE:
            return { ConfigOption::BRAILLE_ENABLED, ConfigOption::BRAILLE_SPEED, ConfigOption::BRAILLE_FREQ,
                     ConfigOption::BRAILLE_SWR, ConfigOption::BRAILLE_PA, ConfigOption::BRAILLE_SCORE,
                     ConfigOption::BRAILLE_LAP, ConfigOption::BRAILLE_TUNER, ConfigOption::SUB_BACK };
        case ConfigCategory::STATUS_READOUT:
            return { ConfigOption::STATUS_SPEED, ConfigOption::STATUS_FREQ, ConfigOption::STATUS_SWR,
                     ConfigOption::STATUS_PA, ConfigOption::STATUS_TUNER, ConfigOption::STATUS_SCORE,
                     ConfigOption::STATUS_LAPS, ConfigOption::STATUS_TIME, ConfigOption::SUB_BACK };
        case ConfigCategory::KEY_MAPPING:
            return { ConfigOption::CONTROLLER_PRESET, ConfigOption::CALIBRATE_CONTROLLER, ConfigOption::REMAP_KEYBOARD, ConfigOption::REMAP_CONTROLLER, ConfigOption::SUB_BACK };
        case ConfigCategory::GAME_ELEMENTS:
            return { ConfigOption::ELEM_MORSE_SIGNALS, ConfigOption::ELEM_SWR_DAMAGE,
                     ConfigOption::ELEM_NOISE_ENEMIES, ConfigOption::ELEM_QSO_STOERER,
                     ConfigOption::ELEM_POWER_UPS, ConfigOption::ELEM_AUTO_STEERING,
                     ConfigOption::ELEM_AUTO_AIM, ConfigOption::MORSE_DIFFICULTY,
                     ConfigOption::SUB_BACK };
        default:
            return { ConfigOption::SUB_BACK };
    }
}

// Speak current category name
void Game::speakCurrentConfigCategory() {
    if (!tts || !tts->isAvailable()) return;
    static const char* configCatNames[] = {
        "Track", "Controls", "Audio", "Vibration", "Speech",
        "Assist", "Weapons", "Braille", "Status Readout", "Key Mapping", "Game Elements", "Back"
    };
    switch (currentConfigCategory) {
        case ConfigCategory::TRACK:
            tts->speak("Track", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config category: Track");
            break;
        case ConfigCategory::CONTROLS:
            tts->speak("Controls", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config category: Controls");
            break;
        case ConfigCategory::AUDIO:
            tts->speak("Audio", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config category: Audio");
            break;
        case ConfigCategory::VIBRATION:
            tts->speak("Vibration", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config category: Vibration");
            break;
        case ConfigCategory::SPEECH:
            tts->speak("Speech", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config category: Speech");
            break;
        case ConfigCategory::ASSIST:
            tts->speak("Assist", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config category: Assist");
            break;
        case ConfigCategory::WEAPONS:
            tts->speak("Weapons", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config category: Weapons");
            break;
        case ConfigCategory::BRAILLE:
            tts->speak("Braille", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config category: Braille");
            break;
        case ConfigCategory::STATUS_READOUT: {
            bool isGerman = translation && 
                (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
            tts->speak(isGerman ? "Statusansage" : "Status Readout", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config category: Status Readout");
            break;
        }
        case ConfigCategory::KEY_MAPPING:
            tts->speak("Key Mapping", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config category: Key Mapping");
            break;
        case ConfigCategory::GAME_ELEMENTS:
            tts->speak("Game Elements", shouldInterruptTts(true));
            break;
        case ConfigCategory::BACK:
            speakTranslated("HAMSPIRIT_CONFIG_BACK", "Back", true);
            log("HAMSPIRIT", "Config category: Back");
            break;
    }
    // Update visual config category overlay
#ifdef _WIN32
    {
        std::vector<std::string> items;
        for (int i = 0; i <= static_cast<int>(ConfigCategory::BACK); i++) {
            items.push_back(configCatNames[i]);
        }
        updateMenuOverlay("Configuration", items, static_cast<int>(currentConfigCategory));
    }
#endif
}

// Speak current config option value
void Game::speakCurrentConfigOption() {
    if (!tts || !tts->isAvailable()) return;
    
    // Compute language flag once for all cases that need it
    bool isGerman = translation && 
        (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
    
    switch (currentConfigOption) {
        case ConfigOption::TRACK_CURVE:
            if (translation) {
                tts->speak(translation->format("HAMSPIRIT_CFG_TRACK_CURVE", 
                    "Track curve: {0}", getTrackCurveName(config.trackCurve)), shouldInterruptTts(true));
            } else {
                tts->speak("Track curve: " + getTrackCurveName(config.trackCurve), shouldInterruptTts(true));
            }
            log("HAMSPIRIT", "Config option: Track curve = " + getTrackCurveName(config.trackCurve));
            break;
        case ConfigOption::DIFFICULTY:
            if (translation) {
                tts->speak(translation->format("HAMSPIRIT_CFG_DIFFICULTY",
                    "Difficulty: {0}", std::to_string(config.difficultyLevel)), shouldInterruptTts(true));
            } else {
                tts->speak("Difficulty: " + std::to_string(config.difficultyLevel), shouldInterruptTts(true));
            }
            log("HAMSPIRIT", "Config option: Difficulty = " + std::to_string(config.difficultyLevel));
            break;
        case ConfigOption::LAP_COUNT:
            if (config.targetLaps == 0) {
                tts->speak("Laps: Infinite", shouldInterruptTts(true));
            } else {
                tts->speak("Laps: " + std::to_string(config.targetLaps), shouldInterruptTts(true));
            }
            log("HAMSPIRIT", "Config option: Laps = " + std::to_string(config.targetLaps));
            break;
        case ConfigOption::STEERING_SENS: {
            int pct = static_cast<int>(std::round(config.steeringSensitivity * 100.0f));
            tts->speak("Steering: " + std::to_string(pct) + "%", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config option: Steering = " + std::to_string(pct) + "%");
            break;
        }
        case ConfigOption::ACCEL_SENS: {
            int pct = static_cast<int>(std::round(config.accelerationSensitivity * 100.0f));
            tts->speak("Acceleration: " + std::to_string(pct) + "%", shouldInterruptTts(true));
            log("HAMSPIRIT", "Config option: Acceleration = " + std::to_string(pct) + "%");
            break;
        }
        case ConfigOption::AIM_SENS: {
            int pct = static_cast<int>(std::round(config.aimSensitivity * 100.0f));
            speakTranslated("HAMSPIRIT_AIM_SENS", "Aim sensitivity", true);
            if (tts && tts->isAvailable()) tts->speak(std::to_string(pct) + "%", false);
            break;
        }
        case ConfigOption::INPUT_DEADZONE:
            tts->speak("Deadzone: " + std::to_string(static_cast<int>(config.inputDeadzone * 100)) + " percent", shouldInterruptTts(true));
            break;
        case ConfigOption::PADDLE_SWAP:
            tts->speak(std::string("Paddle: ") + (config.paddleSwap ? "LB=Dash RB=Dot" : "LB=Dot RB=Dash"), shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: Paddle = ") + (config.paddleSwap ? "LB=Dash RB=Dot" : "LB=Dot RB=Dash"));
            break;
        case ConfigOption::TTS_ENGINE: {
            const char* engineName = getTTSEngineLabel(config.ttsEngine);
            tts->speak(std::string("Speech engine: ") + engineName, shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: TTS engine = ") + engineName);
            break;
        }
        case ConfigOption::TTS_SPEED: {
            const char* speedNames[] = { "Very slow", "Slow", "Normal", "Fast", "Very fast" };
            int idx = config.ttsSpeed + 2;  // -2..2 → 0..4
            idx = std::clamp(idx, 0, 4);
            tts->speak(std::string("Speech speed: ") + speedNames[idx], shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: TTS speed = ") + speedNames[idx]);
            break;
        }
        case ConfigOption::TTS_VOICE: {
            if (availableVoices.empty()) refreshAvailableVoices();
            if (availableVoices.empty()) {
                tts->speak("Voice: default", shouldInterruptTts(true));
            } else {
                currentVoiceIndex = std::clamp(currentVoiceIndex, 0, static_cast<int>(availableVoices.size()) - 1);
                tts->speak("Voice: " + availableVoices[currentVoiceIndex], shouldInterruptTts(true));
            }
            break;
        }
        case ConfigOption::MOTOR_VOLUME:
            tts->speak("Motor Volume: " + std::to_string(static_cast<int>(config.motorVolume * 100)) + " percent", shouldInterruptTts(true));
            break;
        case ConfigOption::SWR_VOLUME:
            tts->speak("SWR Volume: " + std::to_string(static_cast<int>(config.swrVolume * 100)) + " percent", shouldInterruptTts(true));
            break;
        case ConfigOption::MORSE_VOLUME:
            tts->speak("Morse Volume: " + std::to_string(static_cast<int>(config.morseVolume * 100)) + " percent", shouldInterruptTts(true));
            break;
        case ConfigOption::WARNING_VOLUME:
            tts->speak("Warning Volume: " + std::to_string(static_cast<int>(config.warningVolume * 100)) + " percent", shouldInterruptTts(true));
            break;
        case ConfigOption::COLLISION_VOLUME:
            tts->speak("Collision Volume: " + std::to_string(static_cast<int>(config.collisionVolume * 100)) + " percent", shouldInterruptTts(true));
            break;
        case ConfigOption::ENEMY_VOLUME:
            tts->speak("Enemy Volume: " + std::to_string(static_cast<int>(config.enemyVolume * 100)) + " percent", shouldInterruptTts(true));
            break;
        case ConfigOption::UI_VOLUME:
            tts->speak("UI Volume: " + std::to_string(static_cast<int>(config.uiVolume * 100)) + " percent", shouldInterruptTts(true));
            break;
        case ConfigOption::VIBRATION_ENABLED:
            tts->speak(std::string("Vibration: ") + (config.swrVibration ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::VIBRATION_INTENSITY:
            tts->speak("Vibration Intensity: " + std::to_string(static_cast<int>(config.vibrationIntensity * 100)) + " percent", shouldInterruptTts(true));
            break;
        case ConfigOption::AIM_ASSIST:
            tts->speak(std::string("Aim Assist: ") + (config.aimAssist ? "On" : "Off"), shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: Aim Assist = ") + (config.aimAssist ? "On" : "Off"));
            break;
        case ConfigOption::TRAFFIC_REPORTS:
            tts->speak(std::string("Traffic Reports: ") + (config.trafficReports ? "On" : "Off"), shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: Traffic Reports = ") + (config.trafficReports ? "On" : "Off"));
            break;
        case ConfigOption::NOISE_BLANKER:
            tts->speak(std::string("Noise Blanker: ") + (config.noiseBlankerEnabled ? "On" : "Off"), shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: Noise Blanker = ") + (config.noiseBlankerEnabled ? "On" : "Off"));
            break;
        case ConfigOption::EMERGENCY_BRAKE:
            tts->speak(std::string("Emergency Brake: ") + (config.emergencyBrakeEnabled ? "On" : "Off"), shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: Emergency Brake = ") + (config.emergencyBrakeEnabled ? "On" : "Off"));
            break;
        case ConfigOption::NOISE_ALERTS:
            tts->speak(std::string("Noise Alerts: ") + (config.noiseAlerts ? "On" : "Off"), shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: Noise Alerts = ") + (config.noiseAlerts ? "On" : "Off"));
            break;
        case ConfigOption::INTRUDER_MONITORING:
            if (translation) {
                tts->speak(translation->format("HAMSPIRIT_CFG_INTRUDER_MONITORING",
                    "Intruder Monitoring: {0}", std::string(config.intruderMonitoring ? "On" : "Off")), shouldInterruptTts(true));
            } else {
                tts->speak(std::string("Intruder Monitoring: ") + (config.intruderMonitoring ? "On" : "Off"), shouldInterruptTts(true));
            }
            log("HAMSPIRIT", std::string("Config option: Intruder Monitoring = ") + (config.intruderMonitoring ? "On" : "Off"));
            break;
        case ConfigOption::BORDER_WARNING:
            tts->speak(std::string("Border Warning: ") + (config.borderWarningEnabled ? "On" : "Off"), shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: Border Warning = ") + (config.borderWarningEnabled ? "On" : "Off"));
            break;
        case ConfigOption::CURVE_ANNOUNCEMENT:
            tts->speak(std::string("Curve Announcement: ") + (config.curveAnnouncementEnabled ? "On" : "Off"), shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: Curve Announcement = ") + (config.curveAnnouncementEnabled ? "On" : "Off"));
            break;
        case ConfigOption::CURVE_ANNOUNCE_DIST: {
            char distBuf[32];
            std::snprintf(distBuf, sizeof(distBuf), "%.1f", static_cast<double>(config.curveAnnouncementDistance));
            tts->speak(std::string("Curve Warning Distance: ") + distBuf + " kHz", shouldInterruptTts(true));
            break;
        }
        case ConfigOption::BRAILLE_ENABLED:
            tts->speak(std::string("Braille Display: ") + (config.brailleEnabled ? "On" : "Off"), shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: Braille = ") + (config.brailleEnabled ? "On" : "Off"));
            break;
        case ConfigOption::BRAILLE_SPEED:
            tts->speak(std::string("Show Speed: ") + (config.brailleShowSpeed ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::BRAILLE_FREQ:
            tts->speak(std::string("Show Frequency: ") + (config.brailleShowFreq ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::BRAILLE_SWR:
            tts->speak(std::string("Show SWR: ") + (config.brailleShowSWR ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::BRAILLE_PA:
            tts->speak(std::string("Show PA Health: ") + (config.brailleShowPA ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::BRAILLE_SCORE:
            tts->speak(std::string("Show Score: ") + (config.brailleShowScore ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::BRAILLE_LAP:
            tts->speak(std::string("Show Lap: ") + (config.brailleShowLap ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::BRAILLE_TUNER:
            tts->speak(std::string("Show Tuner: ") + (config.brailleShowTuner ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::STATUS_SPEED:
            tts->speak(std::string(isGerman ? "Geschwindigkeit ansagen: " : "Announce Speed: ") + (config.statusShowSpeed ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::STATUS_FREQ:
            tts->speak(std::string(isGerman ? "Frequenz ansagen: " : "Announce Frequency: ") + (config.statusShowFreq ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::STATUS_SWR:
            tts->speak(std::string(isGerman ? "SWR ansagen: " : "Announce SWR: ") + (config.statusShowSWR ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::STATUS_PA:
            tts->speak(std::string(isGerman ? "Endstufe ansagen: " : "Announce PA: ") + (config.statusShowPA ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::STATUS_TUNER:
            tts->speak(std::string(isGerman ? "Tuner ansagen: " : "Announce Tuner: ") + (config.statusShowTuner ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::STATUS_SCORE:
            tts->speak(std::string(isGerman ? "Punkte ansagen: " : "Announce Score: ") + (config.statusShowScore ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::STATUS_LAPS:
            tts->speak(std::string(isGerman ? "Runden ansagen: " : "Announce Laps: ") + (config.statusShowLaps ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::STATUS_TIME:
            tts->speak(std::string(isGerman ? "Spielzeit ansagen: " : "Announce Time: ") + (config.statusShowTime ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::REMAP_KEYBOARD:
            tts->speak("Remap Keyboard — Press A to start", shouldInterruptTts(true));
            break;
        case ConfigOption::REMAP_CONTROLLER:
            tts->speak("Remap Controller — Press A to start", shouldInterruptTts(true));
            break;
        case ConfigOption::CONTROLLER_PRESET: {
            const char* presetNames[] = { "Auto-detect", "Xbox", "PlayStation" };
            int idx = std::clamp(config.controllerPreset, 0, 2);
            tts->speak(std::string("Controller Preset: ") + presetNames[idx], shouldInterruptTts(true));
            log("HAMSPIRIT", std::string("Config option: Controller Preset = ") + presetNames[idx]);
            break;
        }
        case ConfigOption::CALIBRATE_CONTROLLER: {
            bool hasCalibration = (config.stickOffsetLX != 0.0f || config.stickOffsetLY != 0.0f ||
                                   config.stickOffsetRX != 0.0f || config.stickOffsetRY != 0.0f);
            if (hasCalibration) {
                tts->speak("Calibrate Controller — Calibrated. Press A to recalibrate", shouldInterruptTts(true));
            } else {
                tts->speak("Calibrate Controller — Press A to start", shouldInterruptTts(true));
            }
            break;
        }
        case ConfigOption::ELEM_MORSE_SIGNALS:
            tts->speak(std::string("Morse Signals: ") + (config.elemMorseSignals ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::ELEM_SWR_DAMAGE:
            tts->speak(std::string("SWR Damage: ") + (config.elemSwrDamage ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::ELEM_NOISE_ENEMIES:
            tts->speak(std::string("Noise Enemies: ") + (config.elemNoiseEnemies ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::ELEM_QSO_STOERER:
            tts->speak(std::string("QSO Interferer: ") + (config.elemQsoStoerer ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::ELEM_POWER_UPS:
            tts->speak(std::string("Power-Ups: ") + (config.elemPowerUps ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::ELEM_AUTO_STEERING:
            tts->speak(std::string("Auto Steering: ") + (config.elemAutoSteering ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::ELEM_AUTO_AIM:
            tts->speak(std::string("Auto Aim: ") + (config.elemAutoAim ? "On" : "Off"), shouldInterruptTts(true));
            break;
        case ConfigOption::MORSE_DIFFICULTY:
            tts->speak("Morse Difficulty: " + std::to_string(config.morseDifficulty), shouldInterruptTts(true));
            break;
        case ConfigOption::SUB_BACK:
            speakTranslated("HAMSPIRIT_CONFIG_BACK", "Back", true);
            log("HAMSPIRIT", "Config option: Back");
            break;
        default:
            break;
    }
    
    // Braille: send compact config value representation
    std::string brl;
    switch (currentConfigOption) {
        case ConfigOption::TRACK_CURVE: brl = "Trk:" + getTrackCurveName(config.trackCurve); break;
        case ConfigOption::DIFFICULTY: brl = "Diff:" + std::to_string(config.difficultyLevel); break;
        case ConfigOption::LAP_COUNT: brl = "Laps:" + std::to_string(config.targetLaps); break;
        case ConfigOption::STEERING_SENS: brl = "Str:" + std::to_string(static_cast<int>(config.steeringSensitivity * 100)) + "%"; break;
        case ConfigOption::ACCEL_SENS: brl = "Acc:" + std::to_string(static_cast<int>(config.accelerationSensitivity * 100)) + "%"; break;
        case ConfigOption::AIM_SENS: brl = "Aim:" + std::to_string(static_cast<int>(config.aimSensitivity * 100)) + "%"; break;
        case ConfigOption::PADDLE_SWAP: brl = std::string("Pad:") + (config.paddleSwap ? "Swap" : "Norm"); break;
        case ConfigOption::TTS_ENGINE: brl = std::string("TTS:") + getTTSEngineLabel(config.ttsEngine); break;
        case ConfigOption::TTS_SPEED: brl = "Spd:" + std::to_string(config.ttsSpeed); break;
        case ConfigOption::TTS_VOICE:
            if (!availableVoices.empty() && currentVoiceIndex < static_cast<int>(availableVoices.size()))
                brl = "Vce:" + availableVoices[currentVoiceIndex];
            else brl = "Vce:default";
            break;
        case ConfigOption::VIBRATION_ENABLED: brl = std::string("Vib:") + (config.swrVibration ? "On" : "Off"); break;
        case ConfigOption::AIM_ASSIST: brl = std::string("AAst:") + (config.aimAssist ? "On" : "Off"); break;
        case ConfigOption::BORDER_WARNING: brl = std::string("BWrn:") + (config.borderWarningEnabled ? "On" : "Off"); break;
        case ConfigOption::CURVE_ANNOUNCEMENT: brl = std::string("CrvA:") + (config.curveAnnouncementEnabled ? "On" : "Off"); break;
        case ConfigOption::CURVE_ANNOUNCE_DIST: {
            char db[16]; std::snprintf(db, sizeof(db), "%.1f", static_cast<double>(config.curveAnnouncementDistance));
            brl = std::string("CrvD:") + db; break;
        }
        case ConfigOption::REMAP_KEYBOARD: brl = "KeyRemap"; break;
        case ConfigOption::REMAP_CONTROLLER: brl = "CtrlRemap"; break;
        case ConfigOption::CONTROLLER_PRESET: {
            const char* presetShort[] = { "Auto", "Xbox", "PS" };
            brl = std::string("Preset:") + presetShort[std::clamp(config.controllerPreset, 0, 2)];
            break;
        }
        case ConfigOption::CALIBRATE_CONTROLLER: brl = "StickCal"; break;
        case ConfigOption::ELEM_MORSE_SIGNALS: brl = std::string("Morse:") + (config.elemMorseSignals ? "On" : "Off"); break;
        case ConfigOption::ELEM_SWR_DAMAGE: brl = std::string("SWRDmg:") + (config.elemSwrDamage ? "On" : "Off"); break;
        case ConfigOption::ELEM_NOISE_ENEMIES: brl = std::string("Noise:") + (config.elemNoiseEnemies ? "On" : "Off"); break;
        case ConfigOption::ELEM_QSO_STOERER: brl = std::string("QSO:") + (config.elemQsoStoerer ? "On" : "Off"); break;
        case ConfigOption::ELEM_POWER_UPS: brl = std::string("PwrUp:") + (config.elemPowerUps ? "On" : "Off"); break;
        case ConfigOption::ELEM_AUTO_STEERING: brl = std::string("AutoStr:") + (config.elemAutoSteering ? "On" : "Off"); break;
        case ConfigOption::ELEM_AUTO_AIM: brl = std::string("AutoAim:") + (config.elemAutoAim ? "On" : "Off"); break;
        case ConfigOption::MORSE_DIFFICULTY: brl = "MorseDiff:" + std::to_string(config.morseDifficulty); break;
        case ConfigOption::STATUS_SPEED: brl = std::string("SSpd:") + (config.statusShowSpeed ? "On" : "Off"); break;
        case ConfigOption::STATUS_FREQ: brl = std::string("SFrq:") + (config.statusShowFreq ? "On" : "Off"); break;
        case ConfigOption::STATUS_SWR: brl = std::string("SSWR:") + (config.statusShowSWR ? "On" : "Off"); break;
        case ConfigOption::STATUS_PA: brl = std::string("SPA:") + (config.statusShowPA ? "On" : "Off"); break;
        case ConfigOption::STATUS_TUNER: brl = std::string("STun:") + (config.statusShowTuner ? "On" : "Off"); break;
        case ConfigOption::STATUS_SCORE: brl = std::string("SSco:") + (config.statusShowScore ? "On" : "Off"); break;
        case ConfigOption::STATUS_LAPS: brl = std::string("SLap:") + (config.statusShowLaps ? "On" : "Off"); break;
        case ConfigOption::STATUS_TIME: brl = std::string("STim:") + (config.statusShowTime ? "On" : "Off"); break;
        case ConfigOption::SUB_BACK: brl = "Back"; break;
        default: break;
    }
    if (!brl.empty()) updateBrailleDisplay(brl);
}

// Handle config input — two-level menu
void Game::handleConfigInput(const GamepadState& input) {
    // Member variables prevCfgUp, etc. used instead of statics (reset properly on restart)
    
    bool rawUp = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                 || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -STICK_MENU_DEADZONE;
    bool rawDown = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                   || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > STICK_MENU_DEADZONE;
    bool rawLeft = input.buttons[static_cast<int>(GamepadButton::DPAD_LEFT)]
                   || input.axes[static_cast<int>(GamepadAxis::LEFT_X)] < -STICK_MENU_DEADZONE;
    bool rawRight = input.buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)]
                    || input.axes[static_cast<int>(GamepadAxis::LEFT_X)] > STICK_MENU_DEADZONE;
    bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
    bool back = input.buttons[static_cast<int>(GamepadButton::B)];
    
    // D-pad axis separation: when left/right is pressed, suppress up/down
    // to prevent diagonal bounce from triggering unwanted navigation
    bool up = rawUp && !rawLeft && !rawRight;
    bool down = rawDown && !rawLeft && !rawRight;
    // When up/down is pressed, suppress left/right to prevent value changes while navigating
    bool left = rawLeft && !rawUp && !rawDown;
    bool right = rawRight && !rawUp && !rawDown;
    
    // Debounce timer for up/down navigation
    if (dpadDebounceTimer > 0.0f) {
        dpadDebounceTimer -= deltaTime;
        up = false;
        down = false;
    }
    
    if (!inConfigSubMenu) {
        // === Level 0: Category navigation ===
        // Helper: check if a screen reader with braille support is active
        bool brailleEngineActive = false;
        if (tts && tts->isAvailable()) {
            brailleEngineActive = (config.ttsEngine == TTSEngineType::NVDA ||
                                   config.ttsEngine == TTSEngineType::MACOS_VOICEOVER);
        }
        
        if (up && !prevCfgUp) {
            int cat = static_cast<int>(currentConfigCategory);
            if (cat > 0) {
                currentConfigCategory = static_cast<ConfigCategory>(cat - 1);
                // Skip BRAILLE category if no screen reader with braille support is active
                if (!brailleEngineActive && currentConfigCategory == ConfigCategory::BRAILLE) {
                    cat = static_cast<int>(currentConfigCategory);
                    if (cat > 0) currentConfigCategory = static_cast<ConfigCategory>(cat - 1);
                }
                triggerMenuNavSound();
                dpadDebounceTimer = DPAD_DEBOUNCE_TIME;
                log("HAMSPIRIT_MENU", "Config category navigate up");
                speakCurrentConfigCategory();
            } else {
                triggerBumperSound();
            }
        }
        if (down && !prevCfgDown) {
            int cat = static_cast<int>(currentConfigCategory);
            if (cat < static_cast<int>(ConfigCategory::BACK)) {
                currentConfigCategory = static_cast<ConfigCategory>(cat + 1);
                // Skip BRAILLE category if no screen reader with braille support is active
                if (!brailleEngineActive && currentConfigCategory == ConfigCategory::BRAILLE) {
                    cat = static_cast<int>(currentConfigCategory);
                    if (cat < static_cast<int>(ConfigCategory::BACK))
                        currentConfigCategory = static_cast<ConfigCategory>(cat + 1);
                }
                triggerMenuNavSound();
                dpadDebounceTimer = DPAD_DEBOUNCE_TIME;
                log("HAMSPIRIT_MENU", "Config category navigate down");
                speakCurrentConfigCategory();
            } else {
                triggerBumperSound();
            }
        }
        // Enter category or go back
        if (accept && !prevCfgA) {
            if (currentConfigCategory == ConfigCategory::BACK) {
                triggerMenuNavSound();
                log("HAMSPIRIT_MENU", "Config back to pause menu");
                inConfigMenu = false;
                if (configCalledFromMainMenu) {
                    configCalledFromMainMenu = false;
                    setState(GameState::MAIN_MENU);
                } else {
                    showPauseMenu();
                }
            } else {
                triggerMenuSelectSound();
                inConfigSubMenu = true;
                currentSubOptionIndex = 0;
                auto opts = getOptionsForCategory(currentConfigCategory);
                if (!opts.empty()) {
                    currentConfigOption = opts[0];
                    speakCurrentConfigOption();
                }
            }
        }
        if (back && !prevCfgB) {
            triggerMenuNavSound();
            log("HAMSPIRIT_MENU", "Config back (B) to pause menu");
            inConfigMenu = false;
            if (configCalledFromMainMenu) {
                configCalledFromMainMenu = false;
                setState(GameState::MAIN_MENU);
            } else {
                showPauseMenu();
            }
        }
    } else {
        // === Level 1: Option navigation within category ===
        auto opts = getOptionsForCategory(currentConfigCategory);
        int numOpts = static_cast<int>(opts.size());
        
        if (up && !prevCfgUp) {
            if (currentSubOptionIndex > 0) {
                currentSubOptionIndex--;
                currentConfigOption = opts[currentSubOptionIndex];
                triggerMenuNavSound();
                dpadDebounceTimer = DPAD_DEBOUNCE_TIME;
                log("HAMSPIRIT_MENU", "Config option navigate up");
                speakCurrentConfigOption();
            } else {
                triggerBumperSound();
            }
        }
        if (down && !prevCfgDown) {
            if (currentSubOptionIndex < numOpts - 1) {
                currentSubOptionIndex++;
                currentConfigOption = opts[currentSubOptionIndex];
                triggerMenuNavSound();
                dpadDebounceTimer = DPAD_DEBOUNCE_TIME;
                log("HAMSPIRIT_MENU", "Config option navigate down");
                speakCurrentConfigOption();
            } else {
                triggerBumperSound();
            }
        }
        
        // Change values with left/right
        bool changed = false;
        if (currentConfigOption == ConfigOption::TRACK_CURVE) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                int curve = static_cast<int>(config.trackCurve);
                if (right && !prevCfgRight) curve = (curve + 1) % 6;
                if (left && !prevCfgLeft) curve = (curve + 5) % 6;
                config.trackCurve = static_cast<TrackCurve>(curve);
                track = TrackGenerator::generateTrack(measurementData, config.trackCurve);
                changed = true;
            }
        } else if (currentConfigOption == ConfigOption::DIFFICULTY) {
            if (right && !prevCfgRight && config.difficultyLevel < 5) {
                config.difficultyLevel++; changed = true;
            }
            if (left && !prevCfgLeft && config.difficultyLevel > 1) {
                config.difficultyLevel--; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::LAP_COUNT) {
            if (right && !prevCfgRight) {
                config.targetLaps = std::min(99, config.targetLaps + 1); changed = true;
            }
            if (left && !prevCfgLeft && config.targetLaps > 0) {
                config.targetLaps--; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::STEERING_SENS) {
            // Helper: round to nearest 5% step (0.05 increments)
            auto step5 = [](float v, float delta) {
                return std::round((v + delta) * 20.0f) / 20.0f;
            };
            if (right && !prevCfgRight && config.steeringSensitivity < 2.0f) {
                config.steeringSensitivity = std::min(2.0f, step5(config.steeringSensitivity, 0.05f)); changed = true;
            }
            if (left && !prevCfgLeft && config.steeringSensitivity > 0.1f) {
                config.steeringSensitivity = std::max(0.1f, step5(config.steeringSensitivity, -0.05f)); changed = true;
            }
        } else if (currentConfigOption == ConfigOption::ACCEL_SENS) {
            auto step5 = [](float v, float delta) {
                return std::round((v + delta) * 20.0f) / 20.0f;
            };
            if (right && !prevCfgRight && config.accelerationSensitivity < 2.0f) {
                config.accelerationSensitivity = std::min(2.0f, step5(config.accelerationSensitivity, 0.05f)); changed = true;
            }
            if (left && !prevCfgLeft && config.accelerationSensitivity > 0.1f) {
                config.accelerationSensitivity = std::max(0.1f, step5(config.accelerationSensitivity, -0.05f)); changed = true;
            }
        } else if (currentConfigOption == ConfigOption::AIM_SENS) {
            auto step5 = [](float v, float delta) {
                return std::round((v + delta) * 20.0f) / 20.0f;
            };
            if (right && !prevCfgRight) {
                config.aimSensitivity = std::min(2.0f, step5(config.aimSensitivity, 0.05f));
                changed = true;
            }
            if (left && !prevCfgLeft) {
                config.aimSensitivity = std::max(0.1f, step5(config.aimSensitivity, -0.05f));
                changed = true;
            }
        } else if (currentConfigOption == ConfigOption::PADDLE_SWAP) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.paddleSwap = !config.paddleSwap;
                if (morseCannon) morseCannon->setPaddleSwap(config.paddleSwap);
                changed = true;
            }
        } else if (currentConfigOption == ConfigOption::TTS_ENGINE) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                TTSEngineType previous = config.ttsEngine;
#ifdef __APPLE__
                // macOS: cycle between 'say' → VoiceOver → espeak-NG
                TTSEngineType attempted;
                if (right && !prevCfgRight) {
                    // Cycle forward
                    if (config.ttsEngine == TTSEngineType::MACOS_SAY)
                        attempted = TTSEngineType::MACOS_VOICEOVER;
                    else if (config.ttsEngine == TTSEngineType::MACOS_VOICEOVER)
                        attempted = TTSEngineType::ESPEAK_NG;
                    else
                        attempted = TTSEngineType::MACOS_SAY;
                } else {
                    // Cycle backward
                    if (config.ttsEngine == TTSEngineType::MACOS_SAY)
                        attempted = TTSEngineType::ESPEAK_NG;
                    else if (config.ttsEngine == TTSEngineType::ESPEAK_NG)
                        attempted = TTSEngineType::MACOS_VOICEOVER;
                    else
                        attempted = TTSEngineType::MACOS_SAY;
                }
                config.ttsEngine = attempted;
                bool switched = tts && tts->setEngineType(config.ttsEngine);
                if (!switched) {
                    config.ttsEngine = previous;
                    if (tts) tts->setEngineType(previous);
                    if (attempted == TTSEngineType::MACOS_VOICEOVER) {
                        log("HAMSPIRIT", "VoiceOver engine switch failed — VoiceOver may not be running or accessibility permissions are not granted");
                        if (tts && tts->isAvailable()) {
                            tts->speak("VoiceOver not available. Please enable VoiceOver and grant accessibility permissions in System Settings.", true);
                        }
                    } else if (attempted == TTSEngineType::ESPEAK_NG) {
                        log("HAMSPIRIT", "espeak-NG not available");
                        if (tts && tts->isAvailable()) {
                            tts->speak("espeak-NG not installed. Install via: brew install espeak-ng", true);
                        }
                    } else {
                        log("HAMSPIRIT", "TTS engine switch failed, reverted to " +
                            std::string(getTTSEngineLabel(previous)));
                    }
                }
#else
                // Windows: cycle SAPI → NVDA → espeak-NG
                TTSEngineType attempted;
                if (right && !prevCfgRight) {
                    // Cycle forward
                    if (config.ttsEngine == TTSEngineType::WINDOWS_SAPI)
                        attempted = TTSEngineType::NVDA;
                    else if (config.ttsEngine == TTSEngineType::NVDA)
                        attempted = TTSEngineType::ESPEAK_NG;
                    else
                        attempted = TTSEngineType::WINDOWS_SAPI;
                } else {
                    // Cycle backward
                    if (config.ttsEngine == TTSEngineType::WINDOWS_SAPI)
                        attempted = TTSEngineType::ESPEAK_NG;
                    else if (config.ttsEngine == TTSEngineType::ESPEAK_NG)
                        attempted = TTSEngineType::NVDA;
                    else
                        attempted = TTSEngineType::WINDOWS_SAPI;
                }
                config.ttsEngine = attempted;
                bool switched = tts && tts->setEngineType(config.ttsEngine);
                if (!switched) {
                    config.ttsEngine = previous;
                    if (tts) tts->setEngineType(previous);
                    if (attempted == TTSEngineType::NVDA) {
                        offerNvdaControllerDownload();
                    } else if (attempted == TTSEngineType::ESPEAK_NG) {
                        log("HAMSPIRIT", "espeak-NG not available on Windows");
                        if (tts && tts->isAvailable()) {
                            tts->speak("espeak-NG not found. Opening download page.", true);
                        }
                        // Open espeak-NG releases page in the browser
                        #ifdef _WIN32
                        ShellExecuteA(nullptr, "open",
                                      "https://github.com/espeak-ng/espeak-ng/releases",
                                      nullptr, nullptr, SW_SHOWNORMAL);
                        #endif
                    }
                }
#endif
                changed = true;
            }
        } else if (currentConfigOption == ConfigOption::TTS_SPEED) {
            if (right && !prevCfgRight && config.ttsSpeed < 2) {
                config.ttsSpeed++; changed = true;
            }
            if (left && !prevCfgLeft && config.ttsSpeed > -2) {
                config.ttsSpeed--; changed = true;
            }
            if (changed && tts) {
                tts->setRate(static_cast<TTSRate>(config.ttsSpeed));
            }
        } else if (currentConfigOption == ConfigOption::TTS_VOICE) {
            if (availableVoices.empty()) refreshAvailableVoices();
            if (!availableVoices.empty()) {
                if (right && !prevCfgRight) {
                    if (currentVoiceIndex < static_cast<int>(availableVoices.size()) - 1) {
                        currentVoiceIndex++; changed = true;
                    }
                }
                if (left && !prevCfgLeft) {
                    if (currentVoiceIndex > 0) {
                        currentVoiceIndex--; changed = true;
                    }
                }
                if (changed && tts) {
                    config.ttsVoice = availableVoices[currentVoiceIndex];
                    ITTSEngine* engine = tts->getEngine();
                    if (engine) engine->setVoice(config.ttsVoice);
                }
            }
        } else if (currentConfigOption == ConfigOption::MOTOR_VOLUME) {
            if (right && !prevCfgRight) { config.motorVolume = std::min(1.0f, config.motorVolume + 0.1f); changed = true; }
            if (left && !prevCfgLeft) { config.motorVolume = std::max(0.0f, config.motorVolume - 0.1f); changed = true; }
        } else if (currentConfigOption == ConfigOption::SWR_VOLUME) {
            if (right && !prevCfgRight) { config.swrVolume = std::min(1.0f, config.swrVolume + 0.1f); changed = true; }
            if (left && !prevCfgLeft) { config.swrVolume = std::max(0.0f, config.swrVolume - 0.1f); changed = true; }
        } else if (currentConfigOption == ConfigOption::MORSE_VOLUME) {
            if (right && !prevCfgRight) { config.morseVolume = std::min(1.0f, config.morseVolume + 0.1f); changed = true; }
            if (left && !prevCfgLeft) { config.morseVolume = std::max(0.0f, config.morseVolume - 0.1f); changed = true; }
        } else if (currentConfigOption == ConfigOption::WARNING_VOLUME) {
            if (right && !prevCfgRight) { config.warningVolume = std::min(1.0f, config.warningVolume + 0.1f); changed = true; }
            if (left && !prevCfgLeft) { config.warningVolume = std::max(0.0f, config.warningVolume - 0.1f); changed = true; }
        } else if (currentConfigOption == ConfigOption::COLLISION_VOLUME) {
            if (right && !prevCfgRight) { config.collisionVolume = std::min(1.0f, config.collisionVolume + 0.1f); changed = true; }
            if (left && !prevCfgLeft) { config.collisionVolume = std::max(0.0f, config.collisionVolume - 0.1f); changed = true; }
        } else if (currentConfigOption == ConfigOption::ENEMY_VOLUME) {
            if (right && !prevCfgRight) { config.enemyVolume = std::min(1.0f, config.enemyVolume + 0.1f); changed = true; }
            if (left && !prevCfgLeft) { config.enemyVolume = std::max(0.0f, config.enemyVolume - 0.1f); changed = true; }
        } else if (currentConfigOption == ConfigOption::UI_VOLUME) {
            if (right && !prevCfgRight) { config.uiVolume = std::min(1.0f, config.uiVolume + 0.1f); changed = true; }
            if (left && !prevCfgLeft) { config.uiVolume = std::max(0.0f, config.uiVolume - 0.1f); changed = true; }
        } else if (currentConfigOption == ConfigOption::VIBRATION_ENABLED) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.swrVibration = !config.swrVibration;
                changed = true;
                // Strong vibration pulse as confirmation when turning vibration ON
                if (config.swrVibration) {
                    float vibIntensity = config.vibrationIntensity;
                    setVibrationForPlayer(0, vibIntensity, vibIntensity);
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    setVibrationForPlayer(0, 0.0f, 0.0f);
                }
            }
        } else if (currentConfigOption == ConfigOption::VIBRATION_INTENSITY) {
            if (right && !prevCfgRight) { config.vibrationIntensity = std::min(1.0f, config.vibrationIntensity + 0.1f); changed = true; }
            if (left && !prevCfgLeft) { config.vibrationIntensity = std::max(0.0f, config.vibrationIntensity - 0.1f); changed = true; }
            if (changed && config.swrVibration) {
                // Brief pulse at new intensity so user can feel it
                float vibIntensity = config.vibrationIntensity;
                setVibrationForPlayer(0, vibIntensity, vibIntensity);
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                setVibrationForPlayer(0, 0.0f, 0.0f);
            }
        } else if (currentConfigOption == ConfigOption::INPUT_DEADZONE) {
            if (right && !prevCfgRight) { config.inputDeadzone = std::min(0.30f, config.inputDeadzone + 0.02f); changed = true; }
            if (left && !prevCfgLeft) { config.inputDeadzone = std::max(0.02f, config.inputDeadzone - 0.02f); changed = true; }
        } else if (currentConfigOption == ConfigOption::AIM_ASSIST) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.aimAssist = !config.aimAssist;
                changed = true;
            }
        } else if (currentConfigOption == ConfigOption::TRAFFIC_REPORTS) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.trafficReports = !config.trafficReports;
                changed = true;
            }
        } else if (currentConfigOption == ConfigOption::NOISE_BLANKER) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.noiseBlankerEnabled = !config.noiseBlankerEnabled;
                changed = true;
                log("HAMSPIRIT", std::string("Config: Noise Blanker = ") + (config.noiseBlankerEnabled ? "On" : "Off"));
            }
        } else if (currentConfigOption == ConfigOption::EMERGENCY_BRAKE) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.emergencyBrakeEnabled = !config.emergencyBrakeEnabled;
                changed = true;
                log("HAMSPIRIT", std::string("Config: Emergency Brake = ") + (config.emergencyBrakeEnabled ? "On" : "Off"));
            }
        } else if (currentConfigOption == ConfigOption::NOISE_ALERTS) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.noiseAlerts = !config.noiseAlerts;
                changed = true;
                log("HAMSPIRIT", std::string("Config: Noise Alerts = ") + (config.noiseAlerts ? "On" : "Off"));
            }
        } else if (currentConfigOption == ConfigOption::INTRUDER_MONITORING) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.intruderMonitoring = !config.intruderMonitoring;
                changed = true;
                log("HAMSPIRIT", std::string("Config: Intruder Monitoring = ") + (config.intruderMonitoring ? "On" : "Off"));
            }
        } else if (currentConfigOption == ConfigOption::BORDER_WARNING) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.borderWarningEnabled = !config.borderWarningEnabled;
                changed = true;
                log("HAMSPIRIT", std::string("Config: Border Warning = ") + (config.borderWarningEnabled ? "On" : "Off"));
            }
        } else if (currentConfigOption == ConfigOption::CURVE_ANNOUNCEMENT) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.curveAnnouncementEnabled = !config.curveAnnouncementEnabled;
                changed = true;
                log("HAMSPIRIT", std::string("Config: Curve Announcement = ") + (config.curveAnnouncementEnabled ? "On" : "Off"));
            }
        } else if (currentConfigOption == ConfigOption::CURVE_ANNOUNCE_DIST) {
            // Adjustable warning distance: 1.0 to 20.0 kHz in 0.5 steps
            if (right && !prevCfgRight) { config.curveAnnouncementDistance = std::min(20.0f, config.curveAnnouncementDistance + 0.5f); changed = true; }
            if (left && !prevCfgLeft) { config.curveAnnouncementDistance = std::max(1.0f, config.curveAnnouncementDistance - 0.5f); changed = true; }
        } else if (currentConfigOption == ConfigOption::BRAILLE_ENABLED) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.brailleEnabled = !config.brailleEnabled;
                // Master toggle: when disabling, turn off all individual values too
                // When enabling, turn all individual values on
                config.brailleShowSpeed = config.brailleEnabled;
                config.brailleShowFreq = config.brailleEnabled;
                config.brailleShowSWR = config.brailleEnabled;
                config.brailleShowPA = config.brailleEnabled;
                config.brailleShowScore = config.brailleEnabled;
                config.brailleShowLap = config.brailleEnabled;
                config.brailleShowTuner = config.brailleEnabled;
                changed = true;
            }
        } else if (currentConfigOption == ConfigOption::BRAILLE_SPEED) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.brailleShowSpeed = !config.brailleShowSpeed; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::BRAILLE_FREQ) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.brailleShowFreq = !config.brailleShowFreq; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::BRAILLE_SWR) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.brailleShowSWR = !config.brailleShowSWR; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::BRAILLE_PA) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.brailleShowPA = !config.brailleShowPA; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::BRAILLE_SCORE) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.brailleShowScore = !config.brailleShowScore; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::BRAILLE_LAP) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.brailleShowLap = !config.brailleShowLap; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::BRAILLE_TUNER) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.brailleShowTuner = !config.brailleShowTuner; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::STATUS_SPEED) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.statusShowSpeed = !config.statusShowSpeed; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::STATUS_FREQ) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.statusShowFreq = !config.statusShowFreq; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::STATUS_SWR) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.statusShowSWR = !config.statusShowSWR; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::STATUS_PA) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.statusShowPA = !config.statusShowPA; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::STATUS_TUNER) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.statusShowTuner = !config.statusShowTuner; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::STATUS_SCORE) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.statusShowScore = !config.statusShowScore; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::STATUS_LAPS) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.statusShowLaps = !config.statusShowLaps; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::STATUS_TIME) {
            if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
                config.statusShowTime = !config.statusShowTime; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::CONTROLLER_PRESET) {
            if (right && !prevCfgRight) {
                config.controllerPreset = (config.controllerPreset + 1) % 3;  // 0=Auto, 1=Xbox, 2=PS
                changed = true;
            }
            if (left && !prevCfgLeft) {
                config.controllerPreset = (config.controllerPreset + 2) % 3;  // wrap backwards
                changed = true;
            }
            if (changed && gamepad) {
                gamepad->setControllerPreset(config.controllerPreset);
            }
        } else if (currentConfigOption == ConfigOption::REMAP_KEYBOARD) {
            if (accept && !prevCfgA) {
                prevCfgA = true;  // Prevent re-trigger on return
                runKeyRemappingDialog();
                return;  // Dialog handles its own input
            }
        } else if (currentConfigOption == ConfigOption::REMAP_CONTROLLER) {
            if (accept && !prevCfgA) {
                prevCfgA = true;  // Prevent re-trigger on return
                runControllerRemappingDialog();
                return;  // Dialog handles its own input
            }
        } else if (currentConfigOption == ConfigOption::CALIBRATE_CONTROLLER) {
            if (accept && !prevCfgA) {
                prevCfgA = true;  // Prevent re-trigger on return
                runControllerCalibration();
                return;  // Wizard handles its own input
            }
        } else if (currentConfigOption == ConfigOption::ELEM_MORSE_SIGNALS) {
            if ((right && !prevCfgRight) || (left && !prevCfgLeft)) {
                config.elemMorseSignals = !config.elemMorseSignals; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::ELEM_SWR_DAMAGE) {
            if ((right && !prevCfgRight) || (left && !prevCfgLeft)) {
                config.elemSwrDamage = !config.elemSwrDamage; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::ELEM_NOISE_ENEMIES) {
            if ((right && !prevCfgRight) || (left && !prevCfgLeft)) {
                config.elemNoiseEnemies = !config.elemNoiseEnemies; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::ELEM_QSO_STOERER) {
            if ((right && !prevCfgRight) || (left && !prevCfgLeft)) {
                config.elemQsoStoerer = !config.elemQsoStoerer; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::ELEM_POWER_UPS) {
            if ((right && !prevCfgRight) || (left && !prevCfgLeft)) {
                config.elemPowerUps = !config.elemPowerUps; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::ELEM_AUTO_STEERING) {
            if ((right && !prevCfgRight) || (left && !prevCfgLeft)) {
                config.elemAutoSteering = !config.elemAutoSteering; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::ELEM_AUTO_AIM) {
            if ((right && !prevCfgRight) || (left && !prevCfgLeft)) {
                config.elemAutoAim = !config.elemAutoAim; changed = true;
            }
        } else if (currentConfigOption == ConfigOption::MORSE_DIFFICULTY) {
            if (right && !prevCfgRight && config.morseDifficulty < 5) {
                config.morseDifficulty++; changed = true;
            }
            if (left && !prevCfgLeft && config.morseDifficulty > 1) {
                config.morseDifficulty--; changed = true;
            }
        }
        
        if (changed) {
            // Calculate panning based on value position in its range for stereo feedback
            float valuePan = 0.5f;  // Default: center
            if (currentConfigOption == ConfigOption::STEERING_SENS) {
                valuePan = (config.steeringSensitivity - 0.1f) / 1.9f;  // 0.1-2.0 → 0-1
            } else if (currentConfigOption == ConfigOption::ACCEL_SENS) {
                valuePan = (config.accelerationSensitivity - 0.1f) / 1.9f;
            } else if (currentConfigOption == ConfigOption::AIM_SENS) {
                valuePan = (config.aimSensitivity - 0.1f) / 1.9f;
            } else if (currentConfigOption == ConfigOption::DIFFICULTY) {
                valuePan = (config.difficultyLevel - 1) / 4.0f;
            } else if (currentConfigOption == ConfigOption::MORSE_DIFFICULTY) {
                valuePan = (config.morseDifficulty - 1) / 4.0f;
            } else if (currentConfigOption == ConfigOption::TTS_SPEED) {
                valuePan = (config.ttsSpeed + 2) / 4.0f;
            }
            valuePan = std::clamp(valuePan, 0.0f, 1.0f);
            // Trigger panned adjustment sound
            {
                std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
                if (lock.owns_lock()) {
                    audioParams.adjustSoundFrames = msToFrames(160);
                    audioParams.adjustSoundUp = (right && !prevCfgRight);
                    audioParams.adjustSoundPan = valuePan;
                }
            }
            speakCurrentConfigOption();
            saveGameConfig();  // Persist config changes
        } else if ((left && !prevCfgLeft) || (right && !prevCfgRight)) {
            // Value at boundary — horizontal bump sound
            triggerBumperSound();
        }
        
        // Back from submenu: A on SUB_BACK, or B button
        if ((accept && !prevCfgA && currentConfigOption == ConfigOption::SUB_BACK) || (back && !prevCfgB)) {
            triggerMenuNavSound();
            log("HAMSPIRIT_MENU", "Config back to category menu");
            inConfigSubMenu = false;
            speakCurrentConfigCategory();
        }
    }
    
    prevCfgUp = up; prevCfgDown = down; prevCfgLeft = left; prevCfgRight = right;
    prevCfgA = accept; prevCfgB = back;
}

bool Game::shouldInterruptTts(bool requested) const {
    if (!requested) return false;
    // Only suppress interrupt during PLAYING state where SAPI's
    // SPF_PURGEBEFORESPEAK can deadlock with waveOut for WAVE_MAPPER.
    // In all other states (menus, pause, game over) interrupt is safe
    // and essential for responsive menu navigation.
    if (currentState == GameState::PLAYING && audioRunning.load()) return false;
    return true;
}

// Offer interactive NVDA controller client DLL download via TTS
void Game::offerNvdaControllerDownload() {
    if (!tts || !tts->isAvailable()) return;
    
    // Check if NVDA is actually running — only then does a download make sense
    if (!isNvdaScreenReaderRunning()) {
        log("HAMSPIRIT", "NVDA not running, skipping controller download offer");
        tts->speak("NVDA is not running. Using Windows speech.", true);
        config.ttsEngine = TTSEngineType::WINDOWS_SAPI;
        saveGameConfig();
        return;
    }
    
    log("HAMSPIRIT", "NVDA running but controller DLL missing — offering download");
    
    // Speak the prompt via SAPI (which is already active as fallback)
    tts->speak("NVDA screen reader detected, but the controller component is missing. "
               "Would you like to download it now? "
               "Press Enter or A button for yes. Press Escape or B button for no.", true);
    
    // Wait for user response with keyboard or gamepad
    bool userAccepted = false;
    bool gotResponse = false;
    auto startTime = std::chrono::steady_clock::now();
    // Allow enough time for the TTS prompt to be spoken and the user to react
    constexpr float PROMPT_TIMEOUT_SECONDS = 30.0f;
    
    // Wait for any held gamepad buttons to be released before accepting input
    if (gamepad && gamepad->isConnected()) {
        auto releaseStart = std::chrono::steady_clock::now();
        while (std::chrono::duration<float>(std::chrono::steady_clock::now() - releaseStart).count() < 0.5f) {
            gamepad->update();
            GamepadState st = gamepad->getState();
            bool anyPressed = false;
            for (int i = 0; i < static_cast<int>(GamepadButton::COUNT); i++) {
                if (st.buttons[i]) { anyPressed = true; break; }
            }
            if (!anyPressed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    while (!gotResponse && !shouldExit) {
        // Timeout check
        float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsed >= PROMPT_TIMEOUT_SECONDS) {
            tts->speak("No response. Continuing with Windows speech.", true);
            break;
        }
        
        // Check keyboard via GetAsyncKeyState (GUI window)
#ifdef _WIN32
        {
            HWND fg = GetForegroundWindow();
            bool focused = (sHamSpiritWindow.hwnd && fg == sHamSpiritWindow.hwnd) ||
                           (GetConsoleWindow() && fg == GetConsoleWindow());
            if (focused) {
                // Accept: Enter, Space, Y, J (German "Ja")
                if ((GetAsyncKeyState(VK_RETURN) & 0x8000) ||
                    (GetAsyncKeyState(VK_SPACE) & 0x8000) ||
                    (GetAsyncKeyState('Y') & 0x8000) ||
                    (GetAsyncKeyState('J') & 0x8000)) {
                    userAccepted = true;
                    gotResponse = true;
                } else if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) ||
                           (GetAsyncKeyState('N') & 0x8000)) {
                    userAccepted = false;
                    gotResponse = true;
                }
            }
        }
#else
        // POSIX: Check for yes/no response
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
        {
            int vk = 0;
            bool pressed = false;
            while (pollHamSpiritKeyEvent(vk, pressed)) {
                if (!pressed) continue;
                if (vk == 0x0D || vk == 0x20 || vk == 'Y' || vk == 'J') {
                    userAccepted = true;
                    gotResponse = true;
                } else if (vk == 0x1B || vk == 'N') {
                    userAccepted = false;
                    gotResponse = true;
                }
            }
        }
#else
        if (consoleInput && consoleInput->kbhit()) {
            int key = consoleInput->getKey();
            int vk = logicalKeyToVK(key);
            if (vk == 0x0D || vk == 0x20 || vk == 'Y' || vk == 'J') {
                userAccepted = true;
                gotResponse = true;
            } else if (vk == 0x1B || vk == 'N') {
                userAccepted = false;
                gotResponse = true;
            }
        }
#endif
#endif
        
        // Check gamepad (button release already handled above)
        if (!gotResponse && gamepad && gamepad->isConnected()) {
            gamepad->update();
            GamepadState state = gamepad->getState();
            if (state.buttons[static_cast<int>(GamepadButton::A)]) {
                userAccepted = true;
                gotResponse = true;
            } else if (state.buttons[static_cast<int>(GamepadButton::B)]) {
                userAccepted = false;
                gotResponse = true;
            }
        }
        
        // 50ms polling: balances responsiveness with low CPU usage
        if (!gotResponse) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    if (!userAccepted) {
        tts->speak("Download skipped. Using Windows speech.", true);
        config.ttsEngine = TTSEngineType::WINDOWS_SAPI;
        saveGameConfig();
        log("HAMSPIRIT", "User declined NVDA controller download");
        // Wait briefly for TTS to finish
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        return;
    }
    
    // User accepted — start download with TTS status feedback
    tts->stop();
    tts->speak("Downloading NVDA controller component. Please wait.", true);
    log("HAMSPIRIT", "Starting NVDA controller DLL download");
    
    // Wait a moment for the TTS to be heard before the download blocks
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    bool success = downloadNvdaControllerClientDll();
    
    if (success) {
        tts->speak("Download complete. Switching to NVDA.", true);
        log("HAMSPIRIT", "NVDA controller DLL downloaded successfully");
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        
        // Try to switch to NVDA now
        if (tts->setEngineType(TTSEngineType::NVDA)) {
            config.ttsEngine = TTSEngineType::NVDA;
            saveGameConfig();
            tts->setRate(static_cast<TTSRate>(config.ttsSpeed));
            log("HAMSPIRIT", "Switched TTS to NVDA successfully");
        } else {
            tts->speak("NVDA component loaded but could not connect. Using Windows speech.", true);
            config.ttsEngine = TTSEngineType::WINDOWS_SAPI;
            saveGameConfig();
            log("HAMSPIRIT", "NVDA DLL loaded but engine init failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    } else {
        // Download failed — offer browser fallback
        log("HAMSPIRIT", "NVDA controller DLL download failed — offering browser fallback");
        
        std::string libDir = getNvdaDllTargetDirectory();
        
        tts->speak("Automatic download failed. "
                   "Would you like to open the download page in your browser? "
                   "Press Enter or A for yes, Escape or B for no.", true);
        
        // Wait for user response
        bool openBrowser = false;
        bool gotBrowserResponse = false;
        auto browserStart = std::chrono::steady_clock::now();
        
        // Wait for held buttons to release
        if (gamepad && gamepad->isConnected()) {
            auto releaseStart = std::chrono::steady_clock::now();
            while (std::chrono::duration<float>(std::chrono::steady_clock::now() - releaseStart).count() < 0.5f) {
                gamepad->update();
                GamepadState st = gamepad->getState();
                bool anyPressed = false;
                for (int i = 0; i < static_cast<int>(GamepadButton::COUNT); i++) {
                    if (st.buttons[i]) { anyPressed = true; break; }
                }
                if (!anyPressed) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        
        while (!gotBrowserResponse && !shouldExit) {
            float elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - browserStart).count();
            if (elapsed >= 20.0f) {
                tts->speak("No response. Using Windows speech.", true);
                break;
            }
            
            // Check keyboard via GetAsyncKeyState (GUI window)
#ifdef _WIN32
            {
                HWND fg = GetForegroundWindow();
                bool focused = (sHamSpiritWindow.hwnd && fg == sHamSpiritWindow.hwnd) ||
                               (GetConsoleWindow() && fg == GetConsoleWindow());
                if (focused) {
                    if ((GetAsyncKeyState(VK_RETURN) & 0x8000) ||
                        (GetAsyncKeyState(VK_SPACE) & 0x8000) ||
                        (GetAsyncKeyState('Y') & 0x8000) ||
                        (GetAsyncKeyState('J') & 0x8000)) {
                        openBrowser = true;
                        gotBrowserResponse = true;
                    } else if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) ||
                               (GetAsyncKeyState('N') & 0x8000)) {
                        gotBrowserResponse = true;
                    }
                }
            }
#else
            // POSIX: Check for yes/no response
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
            {
                int vk = 0;
                bool pressed = false;
                while (pollHamSpiritKeyEvent(vk, pressed)) {
                    if (!pressed) continue;
                    if (vk == 0x0D || vk == 0x20 || vk == 'Y' || vk == 'J') {
                        openBrowser = true;
                        gotBrowserResponse = true;
                    } else if (vk == 0x1B || vk == 'N') {
                        gotBrowserResponse = true;
                    }
                }
            }
#else
            if (consoleInput && consoleInput->kbhit()) {
                int key = consoleInput->getKey();
                int vk = logicalKeyToVK(key);
                if (vk == 0x0D || vk == 0x20 || vk == 'Y' || vk == 'J') {
                    openBrowser = true;
                    gotBrowserResponse = true;
                } else if (vk == 0x1B || vk == 'N') {
                    gotBrowserResponse = true;
                }
            }
#endif
#endif
            
            if (!gotBrowserResponse && gamepad && gamepad->isConnected()) {
                gamepad->update();
                GamepadState state = gamepad->getState();
                if (state.buttons[static_cast<int>(GamepadButton::A)]) {
                    openBrowser = true;
                    gotBrowserResponse = true;
                } else if (state.buttons[static_cast<int>(GamepadButton::B)]) {
                    gotBrowserResponse = true;
                }
            }
            
            if (!gotBrowserResponse) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        
        if (openBrowser) {
            // Open the download page
            openNvdaDllDownloadPage();
            log("HAMSPIRIT", "Opened NVDA controller download page in browser");
            
            // Tell the user where to put the DLL
#if defined(_WIN64)
            const char* dllFileName = "nvdaControllerClient64.dll";
#else
            const char* dllFileName = "nvdaControllerClient32.dll";
#endif
            std::string instruction = 
                "Your browser is opening the NVDA download page. "
                "Please download the controller client ZIP file. "
                "Extract it and copy the file " + std::string(dllFileName) + " ";
            if (!libDir.empty()) {
                instruction += "into the folder: " + libDir + ". ";
            } else {
                instruction += "into the lib folder next to the program. ";
            }
            instruction += "Then restart the program to use NVDA.";
            
            tts->speak(instruction, true);
            log("HAMSPIRIT", "TTS instruction: " + instruction);
            
            // Wait for TTS to finish speaking the instructions
            std::this_thread::sleep_for(std::chrono::milliseconds(8000));
        }
        
        config.ttsEngine = TTSEngineType::WINDOWS_SAPI;
        saveGameConfig();
    }
}

// Speak translated text
void Game::speakTranslated(const std::string& key, const std::string& fallback, bool interrupt) {
    if (tts && tts->isAvailable()) {
        // NEVER use interrupt/purge during PLAYING state — SAPI's SPF_PURGEBEFORESPEAK
        // can deadlock when competing with waveOutWrite for WAVE_MAPPER.
        // Instead: skip non-critical messages if TTS is already speaking.
        if (currentState == GameState::PLAYING) {
            if (tts->isSpeaking()) {
                log("HAMSPIRIT_TTS", "SKIPPED key=" + key + " (already speaking)");
                return;  // Drop message to prevent queue buildup
            }
            log("HAMSPIRIT_TTS", "speak key=" + key + " async");
            tts->speakTranslated(key, fallback, false);  // Always async, never interrupt
        } else {
            bool safeInterrupt = shouldInterruptTts(interrupt);
            if (interrupt && !safeInterrupt) {
                log("HAMSPIRIT_TTS", "soft interrupt key=" + key);
                tts->stop();
                tts->speakTranslated(key, fallback, false);
            } else {
                log("HAMSPIRIT_TTS", "speak key=" + key + " interrupt=" + std::to_string(safeInterrupt));
                tts->speakTranslated(key, fallback, safeInterrupt);
            }
        }
    }
}

void Game::speakText(const std::string& text, bool interrupt) {
    if (tts && tts->isAvailable()) {
        if (currentState == GameState::PLAYING) {
            if (tts->isSpeaking()) {
                log("HAMSPIRIT_TTS", "SKIPPED text=\"" + text.substr(0, 60) + "\" (already speaking)");
                return;
            }
            log("HAMSPIRIT_TTS", "speak text=\"" + text.substr(0, 60) + "\" async");
            tts->speak(text, false);  // Always async, never interrupt
        } else {
            bool safeInterrupt = shouldInterruptTts(interrupt);
            if (interrupt && !safeInterrupt) {
                log("HAMSPIRIT_TTS", "soft interrupt text=\"" + text.substr(0, 60) + "\"");
                tts->stop();
                tts->speak(text, false);
            } else {
                log("HAMSPIRIT_TTS", "speak text=\"" + text.substr(0, 60) + "\" interrupt=" + std::to_string(safeInterrupt));
                tts->speak(text, safeInterrupt);
            }
        }
    }
}

// Log message
void Game::log(const std::string& category, const std::string& message) {
    if (logger) {
        logger->log(category, message);
    }
}

// Get track curve name
std::string Game::getTrackCurveName(TrackCurve curve) const {
    switch (curve) {
        case TrackCurve::SWR: return "SWR";
        case TrackCurve::RETURN_LOSS: return "Return Loss";
        case TrackCurve::IMPEDANCE_MAG: return "Impedance Magnitude";
        case TrackCurve::REACTANCE: return "Reactance";
        case TrackCurve::PHASE: return "Phase";
        case TrackCurve::ALL_CURVES: return "All Curves";
        default: return "Unknown";
    }
}

// Get data source name
std::string Game::getDataSourceName(bool useAll) const {
    return useAll ? "All Points" : "Time Window";
}

// Curve announcement assist system
// Scans ahead on the track for upcoming SWR gradient changes (curves).
// Announces direction (left/right), severity (gentle/moderate/hard), and distance in kHz.
// Only announces events reachable within 60 seconds at current speed (or minimum speed if stopped).
void Game::checkCurveAnnouncement(float dt) {
    if (!config.curveAnnouncementEnabled || !tts || !tts->isAvailable()) return;
    if (track.size() < 3) return;  // Need at least 3 points to detect curves
    
    // Cooldown between announcements (minimum 2 seconds between announcements)
    if (curveAnnounceCooldown > 0.0f) {
        curveAnnounceCooldown -= dt;
        return;
    }
    
    // Determine reference speed for reachability calculation:
    // - If moving: use actual speed
    // - If stopped: use minimum possible driving speed (5% of maxSpeed)
    //   so the player still gets announcements for nearby events
    float absSpeed = std::abs(playerSpeed);
    float referenceSpeed = absSpeed;
    if (absSpeed < STANDSTILL_THRESHOLD) {
        referenceSpeed = maxSpeed * 0.05f;
    }
    
    // Maximum lookahead: events reachable within 60 seconds at reference speed
    static constexpr float MAX_REACHABLE_TIME = 60.0f;
    float maxLookaheadRad = referenceSpeed * MAX_REACHABLE_TIME;
    
    // Also clamp by the configured warning distance (user preference)
    float warningDistRad = config.curveAnnouncementDistance / kHzPerRadian;
    float effectiveLookahead = std::min(warningDistRad, maxLookaheadRad);
    
    // Don't scan more than half the track to avoid wrap-around confusion
    effectiveLookahead = std::min(effectiveLookahead, PI);
    
    // Scan the track ahead in small steps looking for a significant SWR gradient
    float stepAngle = TWO_PI / static_cast<float>(track.size());
    int numSteps = static_cast<int>(effectiveLookahead / stepAngle) + 1;
    numSteps = std::min(numSteps, static_cast<int>(track.size()) / 2);
    
    // Severity thresholds (absolute SWR change per measurement step):
    // These represent the SWR difference between adjacent measurement points.
    // Typical VNA measurement sets have SWR values from 1.0 (perfect match) to ~10+ (bad match).
    // A GENTLE curve might see SWR change 0.1 per step (e.g., 1.5 → 1.6).
    // A MODERATE curve sees ~0.4 change per step (e.g., 1.5 → 1.9).
    // A HARD curve sees 1.0+ change per step (e.g., 2.0 → 3.0, steep resonance slope).
    static constexpr float GENTLE_THRESHOLD = 0.1f;
    static constexpr float MODERATE_THRESHOLD = 0.4f;
    static constexpr float HARD_THRESHOLD = 1.0f;
    
    for (int i = 1; i <= numSteps; i++) {
        float probeAngle = playerAngle + i * stepAngle;
        while (probeAngle >= TWO_PI) probeAngle -= TWO_PI;
        while (probeAngle < 0.0f) probeAngle += TWO_PI;
        
        TrackPoint tpBefore = TrackGenerator::interpolateAt(track, probeAngle - stepAngle * 0.5f);
        TrackPoint tpAfter = TrackGenerator::interpolateAt(track, probeAngle + stepAngle * 0.5f);
        
        float swrDiff = tpAfter.swr - tpBefore.swr;
        float absGradient = std::abs(swrDiff);
        
        if (absGradient < GENTLE_THRESHOLD) continue;
        
        // Don't re-announce the same curve
        float angleDiffToLast = std::abs(probeAngle - lastAnnouncedCurveAngle);
        if (angleDiffToLast < stepAngle * 1.5f) continue;
        
        // Calculate distance and time-to-reach
        float distAngle = i * stepAngle;
        float distKHz = distAngle * kHzPerRadian;
        float timeToReach = distAngle / std::max(referenceSpeed, 0.001f);
        
        // Skip events not reachable within 60 seconds
        if (timeToReach > MAX_REACHABLE_TIME) continue;
        
        // Direction: positive SWR diff = curve pushes right, negative = left
        bool curveRight = (swrDiff > 0.0f);
        
        // Severity classification
        std::string severityEn, severityDe;
        if (absGradient >= HARD_THRESHOLD) {
            severityEn = "Hard";
            severityDe = "Scharfe";
        } else if (absGradient >= MODERATE_THRESHOLD) {
            severityEn = "Moderate";
            severityDe = "Moderate";
        } else {
            severityEn = "Gentle";
            severityDe = "Leichte";
        }
        
        // Build announcement: "Moderate Right in 3.5 kHz"
        bool isGerman = translation &&
            (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
        
        char distBuf[16];
        std::snprintf(distBuf, sizeof(distBuf), "%.1f", static_cast<double>(distKHz));
        
        std::string announcement;
        if (isGerman) {
            std::string dirDe = curveRight ? "Rechts" : "Links";
            announcement = severityDe + " " + dirDe + " in " + distBuf + " kHz";
        } else {
            std::string dirEn = curveRight ? "Right" : "Left";
            announcement = severityEn + " " + dirEn + " in " + distBuf + " kHz";
        }
        
        tts->speak(announcement, false);
        log("HAMSPIRIT", "Curve announcement: " + announcement +
            " (gradient=" + std::to_string(absGradient) +
            ", ETA=" + std::to_string(timeToReach) + "s)");
        
        lastAnnouncedCurveAngle = probeAngle;
        
        // Scale cooldown by speed: faster = shorter cooldown (more frequent updates needed)
        // Use referenceSpeed (not absSpeed) to handle the stopped case consistently
        float speedFraction = std::clamp(referenceSpeed / maxSpeed, 0.1f, 1.0f);
        curveAnnounceCooldown = 3.0f / speedFraction;
        curveAnnounceCooldown = std::clamp(curveAnnounceCooldown, 2.0f, 10.0f);
        
        return;  // One announcement at a time
    }
}

// Update player position on track
void Game::updatePlayerPosition(float dt) {
    // Move player based on speed (positive = forward, negative = reverse)
    playerAngle += playerSpeed * dt;
    
    // Wrap around at 2π (forward lap)
    while (playerAngle >= TWO_PI) {
        playerAngle -= TWO_PI;
        if (playerSpeed > 0.0f) {  // Only count forward laps
            stats.lapsCompleted++;
            stats.score += 100;
            
            // Check if target laps reached
            if (config.targetLaps > 0 && stats.lapsCompleted >= config.targetLaps) {
                setState(GameState::GAME_OVER);
                return;
            }
            
            if (tts && tts->isAvailable()) {
                speakTranslated("HAMSPIRIT_LAP_COMPLETE", "Lap complete!", false);
            }
            
            // In ALL_CURVES mode, advance to next curve section
            if (config.trackCurve == TrackCurve::ALL_CURVES && !allCurvesTracks.empty()) {
                allCurvesCurrentSection = (allCurvesCurrentSection + 1) % static_cast<int>(allCurvesTracks.size());
                track = allCurvesTracks[allCurvesCurrentSection];
                std::string curveName;
                switch (allCurvesCurrentSection) {
                    case 0: curveName = "SWR"; break;
                    case 1: curveName = "Return Loss"; break;
                    case 2: curveName = "Impedance"; break;
                    case 3: curveName = "Reactance"; break;
                    case 4: curveName = "Phase"; break;
                }
                if (tts && tts->isAvailable()) {
                    tts->speak(curveName, false);
                }
                log("HAMSPIRIT", "ALL_CURVES: Switched to section " + std::to_string(allCurvesCurrentSection) + " (" + curveName + ")");
            }
        }
    }
    // Wrap around at 0 (reverse)
    while (playerAngle < 0.0f) {
        playerAngle += TWO_PI;
    }
}

// Update player speed based on input — racing physics with braking
void Game::updatePlayerSpeed(float dt, const GamepadState& input) {
    // Get raw forward/backward input from left stick Y axis
    float rawForward = -input.axes[static_cast<int>(GamepadAxis::LEFT_Y)];
    
    // Smooth the input to reduce twitchiness (low-pass filter, ~25ms settling time)
    // Make throttle more responsive: faster smoothing while retaining stability
    float smoothRate = std::min(80.0f * dt, 1.0f);  // ~12ms time constant (more responsive)
    smoothedForwardInput += smoothRate * (rawForward - smoothedForwardInput);
    float forwardInput = smoothedForwardInput;
    
    // Get left/right steering input from left stick X axis
    float steerInput = input.axes[static_cast<int>(GamepadAxis::LEFT_X)];
    
    // Apply configurable sensitivity to steering
    float effectiveSteering = steeringSpeed * config.steeringSensitivity;
    
    // Steering only works while the vehicle is in motion.
    // Steering effectiveness scales with speed (realistic: no steering at standstill).
    // Use a minimum speed threshold based on baseMaxSpeed to ensure steering remains
    // effective even when SWR-based speed reduction brings maxSpeed very low.
    float absSpeed = std::abs(playerSpeed);
    float minSteeringThreshold = baseMaxSpeed * 0.05f;  // Minimum denominator for speed factor calc
    float speedFactor = std::clamp(absSpeed / std::max(minSteeringThreshold, maxSpeed * 0.15f), 0.0f, 1.0f);
    // Allow limited steering even from standstill so the player can line up before moving
    speedFactor = std::max(speedFactor, 0.2f);
    effectiveSteering *= speedFactor;
    
    // Update lateral offset: steer to compensate for track curves
    // Positive steerInput = steer right, negative = steer left
    if (std::abs(steerInput) > config.inputDeadzone) {
        playerLateralOffset += steerInput * effectiveSteering * dt;
        playerLateralOffset = std::clamp(playerLateralOffset, -1.0f, 1.0f);
    } else {
        // Slowly center when no steering input (auto-center tendency)
        float centerRate = 0.2f * dt;
        if (playerLateralOffset > 0.0f) {
            playerLateralOffset -= centerRate;
            if (playerLateralOffset < 0.0f) playerLateralOffset = 0.0f;
        } else if (playerLateralOffset < 0.0f) {
            playerLateralOffset += centerRate;
            if (playerLateralOffset > 0.0f) playerLateralOffset = 0.0f;
        }
    }
    // Auto-steering: automatically center the car to avoid border crashes
    if (config.elemAutoSteering) {
        float correction = -playerLateralOffset * 2.0f * dt;
        playerLateralOffset += correction;
        crashRecoveryTime = 0.01f;  // Always in "recovery" so no crash damage applied
    }
    
    // Apply rolling friction: speed-dependent coasting model
    // At high speed, vehicle coasts for a long time (low drag relative to speed)
    // Below standstill threshold, speed is zeroed for a clean stop
    absSpeed = std::abs(playerSpeed);
    if (absSpeed > STANDSTILL_THRESHOLD) {
        // Drag = constant rolling resistance + small quadratic aero drag
        // Rolling resistance is weak so the vehicle coasts realistically at high speed
        float rollingResistance = 0.008f * maxSpeed;  // Very gentle constant drag
        float aeroDrag = 0.015f * absSpeed * absSpeed / std::max(maxSpeed, 0.01f);
        float dragForce = rollingResistance + aeroDrag;
        if (playerSpeed > 0.0f) {
            playerSpeed -= dragForce * dt;
            if (playerSpeed < 0.0f) playerSpeed = 0.0f;
        } else if (playerSpeed < 0.0f) {
            playerSpeed += dragForce * dt;
            if (playerSpeed > 0.0f) playerSpeed = 0.0f;
        }
    }
    
    // Acceleration as fraction of maxSpeed per second:
    // acceleration=0.06 means 6% of maxSpeed per second at full throttle.
    // At 100% sensitivity: ~17 seconds 0-to-max. At 10%: ~170 seconds.
    // This stays consistent regardless of track frequency span or kHzPerRadian.
    float effectiveAccel = acceleration * maxSpeed * config.accelerationSensitivity;
    
    isBraking = false;
    
    if (forwardInput > config.inputDeadzone) {
        reverseHoldTime = 0.0f;  // Reset reverse timer
        if (playerSpeed < -STANDSTILL_THRESHOLD) {
            // Moving backward but stick forward — brake first (same realistic model)
            isBraking = true;
            float baseBrakeDecel = 0.35f * maxSpeed;
            playerSpeed += baseBrakeDecel * dt;
            if (playerSpeed > 0.0f) playerSpeed = 0.0f;  // Stop at zero
        } else {
            // Progressive throttle: squared input curve like real racing games
            // 50% stick = 25% power, 70% stick = 49% power, 100% stick = 100% power
            float normalizedInput = (forwardInput - config.inputDeadzone) / (1.0f - config.inputDeadzone);  // Remap to 0..1
            float throttle = normalizedInput * normalizedInput;     // Squared curve
            
            if (playerSpeed < 0.0f) playerSpeed = 0.0f;
            playerSpeed += effectiveAccel * throttle * dt;
            playerSpeed = std::min(playerSpeed, maxSpeed);
        }
    } else if (forwardInput < -config.inputDeadzone) {
        if (playerSpeed > STANDSTILL_THRESHOLD) {
            // Moving forward but stick backward — BRAKE progressively
            // Physically realistic: brake force scales with maxSpeed so stopping distance
            // is proportional. Full brake at max speed stops in ~3 seconds.
            // Squared input curve: gentle braking at partial stick, strong at full pull.
            isBraking = true;
            reverseHoldTime = 0.0f;
            float brakeInput = (std::abs(forwardInput) - config.inputDeadzone) / (1.0f - config.inputDeadzone);
            brakeInput = std::clamp(brakeInput, 0.0f, 1.0f);
            float brakeThrottle = brakeInput * brakeInput;  // Squared curve for progressive feel
            // Base deceleration = 35% of maxSpeed per second at full brake
            // This means from maxSpeed, full brake stops in ~2.9 seconds
            // At half stick deflection (25% power), stopping takes ~11.4 seconds
            float baseBrakeDecel = 0.35f * maxSpeed;
            float actualBrakeForce = baseBrakeDecel * brakeThrottle;
            playerSpeed -= actualBrakeForce * dt;
            if (playerSpeed < 0.0f) playerSpeed = 0.0f;  // Stop at zero, don't overshoot
        } else if (playerSpeed > -STANDSTILL_THRESHOLD) {
            // At or near standstill — hold at zero, no reverse allowed
            // (bands should always be driven bottom-to-top / low→high frequency)
            playerSpeed = 0.0f;
            reverseHoldTime = 0.0f;
        } else {
            // If somehow in reverse, brake to stop (realistic force)
            float baseBrakeDecel = 0.35f * maxSpeed;
            playerSpeed += baseBrakeDecel * dt;
            if (playerSpeed > 0.0f) playerSpeed = 0.0f;
        }
    } else {
        // No input — reset reverse timer
        reverseHoldTime = 0.0f;
        // Apply static friction near zero to reach a clean stop
        if (std::abs(playerSpeed) < STANDSTILL_THRESHOLD) {
            playerSpeed = 0.0f;
        }
    }
    
    // Hard clamp: vehicle must never go in reverse (bands driven bottom-to-top only)
    if (playerSpeed < 0.0f) playerSpeed = 0.0f;
}

// Update spatial audio based on track
void Game::updateSpatialAudio() {
    if (!spatialAudio || track.empty()) {
        return;
    }
    
    // In multiplayer, player 0's tuner adjustments go to ctx0->antennaNetwork
    // (not the Game-level antennaNetwork).  Use the correct per-player instance
    // so SWR-derived audio (roughness, alert, pan, reactance) matches the
    // player's actual tuning state.
    AntennaNetwork* effectiveAntenna = antennaNetwork.get();
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        auto* ctx0 = multiplayerMgr->getPlayer(0);
        if (ctx0 && ctx0->antennaNetwork)
            effectiveAntenna = ctx0->antennaNetwork.get();
    }

    // Calculate pan based on ADJUSTED SWR (smoothed by antenna network tuning)
    // When user tunes the antenna, the adjusted SWR at each point becomes smoother,
    // which makes the track feel smoother (less pan deviation = easier driving)
    float lookDir = (playerSpeed >= 0.0f) ? 1.0f : -1.0f;
    const float lookAheadDistance = 0.5f * lookDir;
    
    if (effectiveAntenna && !track.empty()) {
        // Use adjusted SWR difference for pan — tuning smooths the track
        float currentAdjSWR = effectiveAntenna->calculateAdjustedSWR(track, playerAngle);
        float aheadAdjSWR = effectiveAntenna->calculateAdjustedSWR(track, playerAngle + lookAheadDistance);
        float swrDiff = aheadAdjSWR - currentAdjSWR;
        float rawPan = std::clamp(swrDiff * 2.0f, -1.0f, 1.0f);
        // Smooth pan changes
        currentPan = currentPan + 0.1f * (rawPan - currentPan);
    } else {
        // Fallback to raw track data
        currentPan = spatialAudio->calculatePan(track, playerAngle, lookAheadDistance);
    }

    // Pan is proportional to track position: left position = left audio, right position = right audio
    // No inversion — direct mapping for intuitive spatial feedback
    
    if (playerSpeed < 0.0f) {
        currentPan = -currentPan;  // Invert pan for reverse driving
    }
    
    // Motor sound pan reflects player's lateral position on the track
    // Left position (negative offset) = sound from left, right position = sound from right
    // This tells the player where they are so they can steer back to center
    currentPan = playerLateralOffset;
    currentPan = std::clamp(currentPan, -1.0f, 1.0f);
    
    // Motor frequency scales with speed, with slight variance to prevent sensory fatigue
    // At high speeds, a subtle LFO modulates the frequency, depth scaling with speed²
    // for natural hearing (gentle at low speed, noticeable only at higher speeds)
    float speedFraction = (maxSpeed > 0.0f) ? std::abs(playerSpeed) / maxSpeed : 0.0f;
    speedFraction = std::min(speedFraction, 1.0f);
    float baseMotorFreq = MOTOR_BASE_FREQ + (MOTOR_MAX_FREQ - MOTOR_BASE_FREQ) * speedFraction;
    // LFO: ~1.5 Hz modulation, depth uses speed² curve
    // ±1.5% max deviation — enough to prevent perceptual "frozen tone" at max speed
    // while being subtle enough to not feel like an engine misfire.
    static constexpr float MAX_LFO_DEPTH = 0.015f;
    float lfoDepth = speedFraction * speedFraction * MAX_LFO_DEPTH;
    float lfoPhase = std::fmod(stats.gameTime * 1.5f * TWO_PI, TWO_PI);
    float lfo = std::sin(lfoPhase);
    // Secondary slower LFO (~0.3 Hz) for organic variation
    float lfo2Phase = std::fmod(stats.gameTime * 0.3f * TWO_PI, TWO_PI);
    float lfo2 = std::sin(lfo2Phase) * 0.4f;
    float combinedLfo = (lfo + lfo2) * 0.5f;  // Blend both oscillators
    motorFreqHz = baseMotorFreq * (1.0f + lfoDepth * combinedLfo);
    
    // Motor roughness from SWR: good match = smooth road, bad match = rough road
    // SWR 1.0 = 0.0 roughness (perfectly smooth)
    // SWR 2.0 = slight roughness
    // SWR 5.0+ = maximum roughness
    // Use effectiveAntenna (per-player in MP) so roughness matches actual tuning.
    float swr = 1.0f;
    if (effectiveAntenna && !track.empty()) {
        swr = effectiveAntenna->calculateAdjustedSWR(track, playerAngle);
    } else if (!track.empty()) {
        TrackPoint currentPoint = TrackGenerator::interpolateAt(track, playerAngle);
        swr = std::max(kMinSWR, std::min(kMaxSWR, currentPoint.swr));
    }
    float motorRoughness = 0.0f;
    if (swr > 1.1f) {
        motorRoughness = std::min((swr - 1.0f) / 4.0f, 1.0f);  // 0 at SWR=1, 1 at SWR=5+
    }
    const float swrAlertThreshold = 2.0f;
    bool swrAlert = (swr > swrAlertThreshold);
    float swrAlertRate = 0.0f;
    int swrVol = 0;
    
    if (swrAlert) {
        // Normalize: 0.0 at SWR=2, 1.0 at SWR=10+
        swrAlertRate = std::min((swr - swrAlertThreshold) / 8.0f, 1.0f);
        // Beep frequency: worse SWR = higher pitch (440-1200 Hz)
        swrFreqHz = 440.0f + 760.0f * swrAlertRate;
        // Volume scales with severity, scaled by config
        swrVol = static_cast<int>((60 + 80 * swrAlertRate) * config.swrVolume);
    } else {
        swrFreqHz = 440.0f;
    }
    
    // Controller vibration for SWR warning — intensity proportional to SWR severity.
    // Priority: crash rumble > border proximity warning > SWR warning.
    // Works even at standstill (use mild floor intensity when speed is very low).
    if (config.swrVibration && crashVibrationTimer <= 0.0f && !borderVibrationActive) {
        if (swrAlert) {
            float vibIntensity = 0.3f + 0.7f * swrAlertRate;
            // If barely moving, still provide a tactile cue at a reduced floor
            float speedScale = (std::abs(playerSpeed) < STANDSTILL_THRESHOLD) ? 0.4f : 1.0f;
            vibIntensity *= speedScale;
            setVibrationForPlayer(0, vibIntensity * config.vibrationIntensity, vibIntensity * 0.8f * config.vibrationIntensity);
        } else {
            // Only zero motors if no other vibration system is active
            setVibrationForPlayer(0, 0.0f, 0.0f);
        }
        // When crash or border vibration is active, don't touch the motors — let them handle it
    }
    
    // Update shared audio parameters for the audio thread
    // Motor volume: silent at standstill, ramps up with speed, scaled by config
    int motorVol = 0;
    if (speedFraction > 0.01f) {
        motorVol = static_cast<int>((80 + 120 * speedFraction) * config.motorVolume);
    }
    
    // Build morse signal audio data — panned relative to AIM direction (turret model)
    // The player aims with the right analog stick independently of driving direction
    std::vector<AudioParams::MorseSignalAudio> morseAudio;
    // Per-target-type aim lock for distinct audio cues through the crosshair
    float aimLockMorse = 0.0f;
    float aimLockNoise = 0.0f;
    float aimLockStoerer = 0.0f;
    float aimLockPowerUp = 0.0f;
    if (morseSignalManager) {
        float aimDirection = playerAngle + aimAngle;  // Turret points here
        const float AIM_MARGIN = 0.3f;  // Same as collection tolerance
        auto& signals = morseSignalManager->getSignals();
        for (auto& sig : signals) {
            if (sig.collected) continue;
            // Calculate angle difference from turret direction, not player position
            float angleDiff = sig.angle - aimDirection;
            while (angleDiff > PI) angleDiff -= TWO_PI;
            while (angleDiff < -PI) angleDiff += TWO_PI;
            float distance = std::abs(angleDiff);
            
            // Compute aim lock via centralized utility
            float lock = calculateAimLock(aimDirection, sig.angle, AIM_MARGIN);
            if (lock > aimLockMorse) aimLockMorse = lock;
            
            if (distance > 1.5f) continue;  // Wider hearing range (1.5 rad ≈ 86°)
            float signalVolume = std::max(0.0f, 1.0f - distance) * config.morseVolume;
            // Pan: 0.0=left, 0.5=center, 1.0=right (relative to aim direction)
            float signalPan = std::clamp((angleDiff + 1.5f) / 3.0f, 0.0f, 1.0f);
            float panSigned = signalPan * 2.0f - 1.0f;
            panSigned = std::clamp(panSigned - playerLateralOffset, -1.0f, 1.0f);
            signalPan = (panSigned + 1.0f) * 0.5f;
            std::string pattern;
            if (morseDatabase) pattern = morseDatabase->getPattern(sig.character);
            morseAudio.push_back({signalPan, static_cast<int>(signalVolume * 100), pattern});
        }
    }
    
    // Aim crosshair for noise enemies (separate lock from morse)
    {
        float aimDirection = playerAngle + aimAngle;
        const float AIM_MARGIN = 0.3f;
        for (const auto& enemy : noiseEnemies) {
            if (enemy.destroyed) continue;
            float lock = calculateAimLock(aimDirection, enemy.angle, AIM_MARGIN);
            if (lock > aimLockNoise) aimLockNoise = lock;
        }
    }
    
    // Aim crosshair for QSO Störer (separate lock)
    if (qsoStoerer.active) {
        float aimDirection = playerAngle + aimAngle;
        const float AIM_MARGIN = 0.3f;
        float lock = calculateAimLock(aimDirection, qsoStoerer.angle, AIM_MARGIN);
        if (lock > aimLockStoerer) aimLockStoerer = lock;
    }
    
    // Aim crosshair for power-ups (separate lock)
    {
        float aimDirection = playerAngle + aimAngle;
        const float AIM_MARGIN = 0.4f;  // Same as POWERUP_AIM_MARGIN
        for (const auto& pu : powerUps) {
            if (pu.collected || pu.destroyed) continue;
            float lock = calculateAimLock(aimDirection, pu.angle, AIM_MARGIN);
            if (lock > aimLockPowerUp) aimLockPowerUp = lock;
        }
    }
    
    // Combined aim lock (max of all types) for stereo narrowing
    float aimLock = std::max({aimLockMorse, aimLockNoise, aimLockStoerer, aimLockPowerUp});
    
    // Cache aim lock for power-ups so handlePowerUpCollection can use it
    cachedAimLockPowerUp = aimLockPowerUp;
    
    bool cannonActive = morseCannon && morseCannon->isActive();
    bool cannonIsDash = morseCannon && morseCannon->isDashPaddleActive();
    
    // 6.4: Push immediate audio events for morse cannon state changes.
    // Detect transitions (key-down / key-up) and push to the lock-free queue
    // so the audio thread can start/stop the tone within the current frame.
    {
        if (cannonActive && !prevMorseCannonActive) {
            feedbackOrchestrator.triggerMorseKeyDown(cannonIsDash, currentPan);
        } else if (!cannonActive && prevMorseCannonActive) {
            feedbackOrchestrator.triggerMorseKeyUp();
        }
        prevMorseCannonActive = cannonActive;
    }
    
    // Lock and update shared params
    {
        std::lock_guard<std::mutex> lock(audioStateMtx);
        // 6.5: Stamp the audio state with the current time for sample-accurate rendering
        audioParams.eventTimestamp = FeedbackClock::now();
        audioParams.pan = currentPan;
        audioParams.motorFreq = motorFreqHz;
        audioParams.motorVolume = static_cast<float>(motorVol);
        audioParams.motorRoughness = motorRoughness;
        audioParams.warningVolume = config.warningVolume;
        audioParams.collisionVolume = config.collisionVolume;
        audioParams.enemyVolume = config.enemyVolume;
        audioParams.uiVolume = config.uiVolume;
        audioParams.swrFreq = swrFreqHz;
        audioParams.swrVolume = static_cast<float>(swrVol);
        audioParams.swrAlertActive = swrAlert;
        audioParams.swrAlertRate = swrAlertRate;
        // Pass reactance for L/C indication in SWR alert tone.
        // Use ADJUSTED reactance (after tuner/UnUn) when antenna network exists,
        // so the wobble/reverb reflects the remaining mismatch the player needs to fix.
        if (effectiveAntenna) {
            audioParams.reactanceAtPlayer = effectiveAntenna->calculateAdjustedReactance(track, playerAngle);
        } else {
            TrackPoint tp = TrackGenerator::interpolateAt(track, playerAngle);
            audioParams.reactanceAtPlayer = tp.reactance;
        }
        audioParams.morseCannonActive = cannonActive;
        audioParams.morseCannonIsDash = cannonIsDash;
        audioParams.morseSignals = std::move(morseAudio);
        audioParams.aimLockStrength = config.aimAssist ? aimLock : 0.0f;
        audioParams.aimLockMorse = config.aimAssist ? aimLockMorse : 0.0f;
        audioParams.aimLockNoise = config.aimAssist ? aimLockNoise : 0.0f;
        audioParams.aimLockStoerer = config.aimAssist ? aimLockStoerer : 0.0f;
        audioParams.aimLockPowerUp = config.aimAssist ? aimLockPowerUp : 0.0f;
    }
}

// ============================================================================
// TitleMelody Implementation — F harmonic-minor chiptune with tempo changes
// ============================================================================

// F harmonic minor: F  G  Ab  Bb  C  Db  E
// Octave 2 (sub-bass / bass)
static constexpr double NF2  =  87.31;
static constexpr double NG2  =  98.00;
static constexpr double NAb2 = 103.83;
static constexpr double NBb2 = 116.54;
static constexpr double NC3  = 130.81;
static constexpr double NDb3 = 138.59;
static constexpr double NE3  = 164.81;
// Octave 3
static constexpr double NF3  = 174.61;
static constexpr double NG3  = 196.00;
static constexpr double NAb3 = 207.65;
static constexpr double NBb3 = 233.08;
static constexpr double NC4  = 261.63;
static constexpr double NDb4 = 277.18;
static constexpr double NE4  = 329.63;
// Octave 4 (melody)
static constexpr double NF4  = 349.23;
static constexpr double NG4  = 392.00;
static constexpr double NAb4 = 415.30;
static constexpr double NBb4 = 466.16;
static constexpr double NC5  = 523.25;
static constexpr double NDb5 = 554.37;
static constexpr double NE5  = 659.26;
// Octave 5 (high accents)
static constexpr double NF5  = 698.46;
static constexpr double NG5  = 783.99;
static constexpr double NAb5 = 830.61;

// Drum synthesis constants
static constexpr float KICK_AMPLITUDE    = 9000.0f;  // PCM scale for kick drum
static constexpr float SNARE_AMPLITUDE   = 7000.0f;  // PCM scale for snare drum
static constexpr float HIHAT_AMPLITUDE   = 6000.0f;  // PCM scale for hi-hat
static constexpr float KICK_PITCH_SWEEP  = 1500.0f;  // Hz/s pitch drop rate for kick
static constexpr float KICK_START_FREQ   = 150.0f;   // Kick start frequency (Hz)
static constexpr float KICK_MIN_FREQ     = 45.0f;    // Kick minimum frequency (Hz)
static constexpr float SNARE_BODY_FREQ   = 180.0f;   // Snare tonal body frequency (Hz)
// Volume scaling: keeps melody well below menu sounds and TTS for audibility
static constexpr float TITLE_GLOBAL_VOLUME_SCALE = 0.55f;
// Standard glibc LCG constants used for noise generation (hi-hat, snare)
static constexpr unsigned int LCG_MULTIPLIER = 1103515245;
static constexpr unsigned int LCG_INCREMENT  = 12345;

TitleMelody::TitleMelody()
    : playing(false), fadingOut(false), playbackPos(0.0),
      masterVolume(1.0f), fadeSpeed(1.0f / FADE_DURATION),
      totalLength(TOTAL_LENGTH),
      kickPhase(0.0), snarePhase(0.0), hihatSeed(42)
{
    buildComposition();
}

void TitleMelody::start() {
    buildComposition();
    totalLength = TOTAL_LENGTH;
    playbackPos = 0.0;
    masterVolume = 1.0f;
    fadingOut = false;
    playing = true;
    kickPhase = 0.0;
    snarePhase = 0.0;
    hihatSeed = 42;
}

void TitleMelody::startVictory() {
    buildVictoryComposition();
    totalLength = VICTORY_LENGTH;
    playbackPos = 0.0;
    masterVolume = 1.0f;
    fadingOut = false;
    playing = true;
    kickPhase = 0.0;
    snarePhase = 0.0;
    hihatSeed = 42;
}

void TitleMelody::startDefeat() {
    buildDefeatComposition();
    totalLength = DEFEAT_LENGTH;
    playbackPos = 0.0;
    masterVolume = 1.0f;
    fadingOut = false;
    playing = true;
    kickPhase = 0.0;
    snarePhase = 0.0;
    hihatSeed = 42;
}

void TitleMelody::beginFadeOut() {
    if (playing) fadingOut = true;
}

void TitleMelody::stop() {
    playing = false;
    fadingOut = false;
    masterVolume = 0.0f;
}

// Piecewise tempo map (BPM at a given position in seconds)
double TitleMelody::getBPM(double pos) {
    if (pos <  8.0) return 90.0;                           // Intro
    if (pos < 24.0) return 140.0;                          // Section A
    if (pos < 36.0) return 150.0;                          // Section B
    if (pos < 44.0) return 110.0;                          // Bridge
    if (pos < 58.0) return 160.0;                          // Climax C
    // Outro: decelerate 160 → 80 over 15 seconds
    double t = (pos - 58.0) / 15.0;
    if (t > 1.0) t = 1.0;
    return 160.0 - 80.0 * t;
}

// ---- composition -----------------------------------------------------------
void TitleMelody::buildComposition() {
    melodyEvents.clear();
    bassEvents.clear();
    padEvents.clear();
    drumEvents.clear();

    // Shorthand lambdas
    auto mel = [&](double t, double d, double f, float v, float p = 0.5f) {
        melodyEvents.push_back({t, d, f, 3, v, p});
    };
    auto bas = [&](double t, double d, double f, float v, float p = 0.5f) {
        bassEvents.push_back({t, d, f, 0, v, p});
    };
    auto pad = [&](double t, double d, double f, float v, float p = 0.5f) {
        padEvents.push_back({t, d, f, 4, v, p});
    };
    auto drm = [&](double t, int type, float v) {
        drumEvents.push_back({t, type, v});
    };

    // Helper: beat length at a given BPM
    auto bl = [](double bpm) { return 60.0 / bpm; };

    // ======================================================================
    // INTRO (0–8 s) — 90 BPM — atmospheric rise, radio-tuning feel
    // ======================================================================
    {
        double bpm = 90.0;
        double b = bl(bpm);   // ~0.667 s

        // Rising pad sweep: F3 → C4 → E4 stacked, slow attack
        pad(0.0,  4.0, NF3,  PAD_VOLUME * 0.5f, 0.50f);
        pad(0.0,  4.0, NC4,  PAD_VOLUME * 0.3f, 0.55f);
        pad(2.0,  3.0, NAb3, PAD_VOLUME * 0.4f, 0.45f);
        pad(4.0,  4.0, NE4,  PAD_VOLUME * 0.6f, 0.50f);
        pad(4.0,  4.0, NC4,  PAD_VOLUME * 0.4f, 0.55f);
        pad(4.0,  4.0, NF3,  PAD_VOLUME * 0.3f, 0.45f);

        // Sparse melody — like a radio signal being tuned in
        mel(1.0,  b,   NF4,  MELODY_VOLUME * 0.3f, 0.50f);
        mel(2.5,  b*0.5, NC5, MELODY_VOLUME * 0.25f, 0.55f);
        mel(4.0,  b*1.5, NE5, MELODY_VOLUME * 0.4f, 0.50f);
        mel(5.5,  b,   NDb5, MELODY_VOLUME * 0.35f, 0.48f);
        mel(6.5,  b*0.5, NC5, MELODY_VOLUME * 0.30f, 0.52f);

        // Pulsing sub-bass heartbeat — every 2 beats
        for (int i = 0; i < 6; i++) {
            double t = 1.0 + i * b * 2;
            if (t >= 8.0) break;
            float vol = BASS_VOLUME * (0.3f + 0.1f * i);
            bas(t, b * 0.8, NF2, vol);
        }

        // Sparse kick buildup in last 2 s of intro
        drm(6.0, 0, DRUM_VOLUME * 0.4f);
        drm(6.5, 0, DRUM_VOLUME * 0.5f);
        drm(7.0, 0, DRUM_VOLUME * 0.6f);
        drm(7.25, 0, DRUM_VOLUME * 0.7f);
        drm(7.5,  0, DRUM_VOLUME * 0.8f);
        drm(7.75, 0, DRUM_VOLUME * 0.9f);

        // Transition sweep: rising sawtooth sweep into Section A
        pad(7.0, 1.5, NC4,  PAD_VOLUME * 0.8f, 0.50f);
        pad(7.0, 1.5, NE4,  PAD_VOLUME * 0.6f, 0.55f);
        pad(7.0, 1.5, NAb4, PAD_VOLUME * 0.4f, 0.45f);
    }

    // ======================================================================
    // SECTION A (8–24 s) — 140 BPM — driving main theme
    // ======================================================================
    {
        double bpm = 140.0;
        double b = bl(bpm);   // ~0.4286 s
        double e = b * 0.5;   // eighth
        double s16 = b * 0.25; // sixteenth

        // --- Melody: aggressive F-minor arpeggio motif ---
        // Phrase A1 (8–12 s): the "Ham Spirit" hook
        double t = 8.0;
        mel(t,       e,   NF4,  MELODY_VOLUME, 0.45f);
        mel(t+e,     e,   NAb4, MELODY_VOLUME, 0.48f);
        mel(t+2*e,   e,   NC5,  MELODY_VOLUME, 0.52f);
        mel(t+3*e,   b,   NE5,  MELODY_VOLUME * 1.15f, 0.55f);
        mel(t+3*e+b, e,   NDb5, MELODY_VOLUME, 0.52f);
        mel(t+3*e+b+e, b*1.5, NC5, MELODY_VOLUME, 0.50f);
        // Bar 2
        t = 8.0 + b * 4;
        mel(t,       e,   NBb4, MELODY_VOLUME, 0.48f);
        mel(t+e,     s16, NAb4, MELODY_VOLUME * 0.9f, 0.50f);
        mel(t+e+s16, s16, NBb4, MELODY_VOLUME * 0.9f, 0.50f);
        mel(t+2*e,   b,   NC5,  MELODY_VOLUME, 0.52f);
        mel(t+2*e+b, b*2, NAb4, MELODY_VOLUME * 0.9f, 0.50f);

        // Phrase A2 (12–16 s): variation with higher reach
        t = 12.0;
        mel(t,       e,   NC5,  MELODY_VOLUME, 0.50f);
        mel(t+e,     e,   NE5,  MELODY_VOLUME * 1.1f, 0.53f);
        mel(t+2*e,   e,   NF5,  MELODY_VOLUME * 1.2f, 0.55f);
        mel(t+3*e,   b,   NE5,  MELODY_VOLUME * 1.1f, 0.53f);
        mel(t+3*e+b, b,   NDb5, MELODY_VOLUME, 0.50f);
        mel(t+3*e+2*b, b*2, NC5, MELODY_VOLUME, 0.50f);

        // Phrase A3 (16–20 s): descending resolution
        t = 16.0;
        mel(t,       b,   NE5,  MELODY_VOLUME * 1.1f, 0.55f);
        mel(t+b,     e,   NDb5, MELODY_VOLUME, 0.52f);
        mel(t+b+e,   e,   NC5,  MELODY_VOLUME, 0.50f);
        mel(t+2*b,   e,   NBb4, MELODY_VOLUME, 0.48f);
        mel(t+2*b+e, e,   NAb4, MELODY_VOLUME * 0.9f, 0.45f);
        mel(t+3*b,   b*2, NG4,  MELODY_VOLUME * 0.85f, 0.48f);
        // Second half: quick ornament
        t = 18.0;
        mel(t,       s16, NF4,  MELODY_VOLUME * 0.8f, 0.50f);
        mel(t+s16,   s16, NG4,  MELODY_VOLUME * 0.85f, 0.50f);
        mel(t+2*s16, s16, NAb4, MELODY_VOLUME * 0.9f, 0.50f);
        mel(t+3*s16, b*3, NF4,  MELODY_VOLUME, 0.50f);

        // Phrase A4 (20–24 s): rhythmic drive into transition
        t = 20.0;
        mel(t,       e,   NAb4, MELODY_VOLUME, 0.48f);
        mel(t+e,     e,   NC5,  MELODY_VOLUME, 0.50f);
        mel(t+2*e,   b,   NE5,  MELODY_VOLUME * 1.1f, 0.55f);
        mel(t+2*e+b, e,   NC5,  MELODY_VOLUME, 0.52f);
        mel(t+2*e+b+e, e, NDb5, MELODY_VOLUME, 0.53f);
        mel(t+4*e+b, b*2, NE5,  MELODY_VOLUME * 1.15f, 0.55f);
        // Rhythmic staccato tail
        t = 22.0;
        for (int i = 0; i < 4; i++) {
            double f = (i % 2 == 0) ? NC5 : NAb4;
            mel(t + i * e, s16, f, MELODY_VOLUME * 0.8f, 0.50f);
        }
        mel(22.0 + 4*e, b*2, NF4, MELODY_VOLUME, 0.50f);

        // --- Bass: pulsing eighth-note root pattern ---
        double roots_a[] = { NF2, NDb3, NAb2, NC3, NF2, NBb2, NDb3, NE3 };
        for (int bar = 0; bar < 8; bar++) {
            double bt = 8.0 + bar * b * 4;
            double root = roots_a[bar];
            for (int i = 0; i < 8; i++) {
                double freq = (i % 2 == 0) ? root : root * 2.0;
                float vol = BASS_VOLUME * ((i == 0 || i == 4) ? 1.0f : 0.75f);
                bas(bt + i * e, e * 0.85, freq, vol);
            }
        }

        // --- Drums: 4-on-the-floor with hats and snare on 2 & 4 ---
        for (double dt = 8.0; dt < 24.0; dt += b) {
            int beatInBar = static_cast<int>(std::round((dt - 8.0) / b)) % 4;
            drm(dt, 0, DRUM_VOLUME);                       // kick every beat
            if (beatInBar == 1 || beatInBar == 3) {
                drm(dt, 1, DRUM_VOLUME * 0.85f);           // snare on 2 & 4
            }
            drm(dt,       2, DRUM_VOLUME * 0.5f);          // closed hat on beat
            drm(dt + e,   2, DRUM_VOLUME * 0.35f);         // closed hat off-beat
        }

        // --- Transition sweep A→B ---
        pad(23.0, 1.5, NE4,  PAD_VOLUME * 0.9f, 0.50f);
        pad(23.0, 1.5, NAb4, PAD_VOLUME * 0.6f, 0.55f);
        pad(23.0, 1.5, NC5,  PAD_VOLUME * 0.4f, 0.45f);
    }

    // ======================================================================
    // SECTION B (24–36 s) — 150 BPM — syncopated, call-and-response
    // ======================================================================
    {
        double bpm = 150.0;
        double b = bl(bpm);
        double e = b * 0.5;
        double s16 = b * 0.25;

        // --- Melody: syncopated call-and-response ---
        // Call phrase (24–28)
        double t = 24.0;
        mel(t,         e,    NE5,  MELODY_VOLUME * 1.1f, 0.55f);
        mel(t+e*1.5,   e,    NF5,  MELODY_VOLUME * 1.2f, 0.55f);
        mel(t+3*e,     b,    NE5,  MELODY_VOLUME * 1.0f, 0.52f);
        mel(t+3*e+b,   b*2,  NDb5, MELODY_VOLUME * 0.9f, 0.50f);
        // Response (26–28) — echo-like, panned slightly left
        t = 26.0;
        mel(t,         e,    NC5,  MELODY_VOLUME * 0.7f, 0.38f);
        mel(t+e*1.5,   e,    NDb5, MELODY_VOLUME * 0.7f, 0.38f);
        mel(t+3*e,     b,    NC5,  MELODY_VOLUME * 0.65f, 0.40f);
        mel(t+3*e+b,   b*2,  NBb4, MELODY_VOLUME * 0.6f, 0.42f);

        // Call 2 (28–30)
        t = 28.0;
        mel(t,       e,   NF4,  MELODY_VOLUME, 0.50f);
        mel(t+e,     e,   NAb4, MELODY_VOLUME, 0.50f);
        mel(t+2*e,   e,   NBb4, MELODY_VOLUME, 0.52f);
        mel(t+3*e,   e,   NC5,  MELODY_VOLUME, 0.53f);
        mel(t+4*e,   b,   NDb5, MELODY_VOLUME * 1.05f, 0.55f);
        mel(t+4*e+b, b,   NE5,  MELODY_VOLUME * 1.15f, 0.55f);
        // Response 2 (30–32) — panned right
        t = 30.0;
        mel(t,       e,   NDb5, MELODY_VOLUME * 0.7f, 0.62f);
        mel(t+e,     e,   NC5,  MELODY_VOLUME * 0.65f, 0.62f);
        mel(t+2*e,   e,   NBb4, MELODY_VOLUME * 0.6f, 0.60f);
        mel(t+3*e,   b*3, NAb4, MELODY_VOLUME * 0.55f, 0.58f);

        // Driving tail (32–36): accelerating sixteenths
        t = 32.0;
        double scaleB[] = { NF4, NG4, NAb4, NBb4, NC5, NDb5, NE5, NF5 };
        for (int i = 0; i < 8; i++) {
            mel(t + i * s16 * 2, s16 * 1.8, scaleB[i],
                MELODY_VOLUME * (0.7f + 0.05f * i), 0.45f + 0.015f * i);
        }
        t = 34.0;
        // Descending with staccato
        for (int i = 0; i < 8; i++) {
            mel(t + i * s16 * 2, s16, scaleB[7 - i],
                MELODY_VOLUME * (0.9f - 0.04f * i), 0.55f - 0.015f * i);
        }

        // --- Bass: syncopated push ---
        double roots_b[] = { NF2, NBb2, NDb3, NC3, NAb2, NBb2, NF2, NE3 };
        for (int bar = 0; bar < 6; bar++) {
            double bt = 24.0 + bar * b * 4;
            double root = roots_b[bar % 8];
            // Syncopated: skip beat 3, accent the & of 2
            bas(bt,         e*0.85, root,       BASS_VOLUME);
            bas(bt+e,       e*0.85, root*2.0,   BASS_VOLUME * 0.7f);
            bas(bt+2*e,     e*0.85, root,       BASS_VOLUME * 0.85f);
            bas(bt+3.5*e,   e*0.85, root*2.0,   BASS_VOLUME * 0.9f);  // pushed
            bas(bt+5*e,     e*0.85, root,       BASS_VOLUME * 0.8f);
            bas(bt+6*e,     e*0.85, root*2.0,   BASS_VOLUME * 0.65f);
            bas(bt+7*e,     e*0.85, root,       BASS_VOLUME * 0.75f);
        }

        // --- Drums: half-time snare feel with double-speed hats ---
        for (double dt = 24.0; dt < 36.0; dt += b) {
            int beatInBar = static_cast<int>(std::round((dt - 24.0) / b)) % 4;
            drm(dt, 0, DRUM_VOLUME);
            if (beatInBar == 2) {
                drm(dt, 1, DRUM_VOLUME * 0.9f);            // snare only on 3 (half-time)
            }
            drm(dt,       2, DRUM_VOLUME * 0.45f);
            drm(dt + e*0.5, 2, DRUM_VOLUME * 0.30f);       // double-speed closed hat
            drm(dt + e,   2, DRUM_VOLUME * 0.40f);
            drm(dt + e*1.5, 2, DRUM_VOLUME * 0.25f);
        }

        // --- Pad accent at phrase boundaries ---
        pad(24.0, 2.0, NF3,  PAD_VOLUME * 0.7f, 0.50f);
        pad(24.0, 2.0, NAb3, PAD_VOLUME * 0.5f, 0.45f);
        pad(28.0, 1.0, NE4,  PAD_VOLUME * 0.6f, 0.55f);
        // Transition sweep B→Bridge
        pad(35.0, 1.5, NDb4, PAD_VOLUME * 0.9f, 0.50f);
        pad(35.0, 1.5, NF3,  PAD_VOLUME * 0.6f, 0.55f);
        pad(35.0, 1.5, NAb3, PAD_VOLUME * 0.4f, 0.45f);
    }

    // ======================================================================
    // BRIDGE (36–44 s) — 110 BPM — breakdown, bass solo, tension sweep
    // ======================================================================
    {
        double bpm = 110.0;
        double b = bl(bpm);
        double e = b * 0.5;

        // --- Melody: sparse, mysterious fragments ---
        mel(36.5, b*2, NF4,  MELODY_VOLUME * 0.5f, 0.50f);
        mel(39.0, b,   NAb4, MELODY_VOLUME * 0.4f, 0.48f);
        mel(40.0, b*2, NC5,  MELODY_VOLUME * 0.5f, 0.52f);
        // Rising tension motif (42-44)
        mel(42.0, e,   NDb5, MELODY_VOLUME * 0.6f, 0.50f);
        mel(42.0+e, e, NE5,  MELODY_VOLUME * 0.7f, 0.52f);
        mel(42.0+2*e, b*2, NF5, MELODY_VOLUME * 0.8f, 0.55f);

        // --- Bass solo: walking bass line ---
        double walkBass[] = { NF2, NG2, NAb2, NBb2, NC3, NDb3, NE3, NF3,
                              NE3, NDb3, NC3, NBb2, NAb2, NG2, NF2, NF2 };
        for (int i = 0; i < 16; i++) {
            double bt = 36.0 + i * b;
            if (bt >= 44.0) break;
            bas(bt, b * 0.85, walkBass[i], BASS_VOLUME * 0.9f);
        }

        // --- Drums: stripped back — kick only, sparse hat ---
        for (double dt = 36.0; dt < 44.0; dt += b) {
            int beatInBar = static_cast<int>(std::round((dt - 36.0) / b)) % 4;
            if (beatInBar == 0 || beatInBar == 2) {
                drm(dt, 0, DRUM_VOLUME * 0.7f);
            }
            if (beatInBar == 0) {
                drm(dt, 2, DRUM_VOLUME * 0.3f);
            }
        }
        // Fill at end of bridge — snare roll building into climax
        for (int i = 0; i < 8; i++) {
            double ft = 43.0 + i * 0.125;
            drm(ft, 1, DRUM_VOLUME * (0.4f + 0.08f * i));
        }

        // --- Tension sweep: long rising pad ---
        pad(36.0, 4.0, NF3,  PAD_VOLUME * 0.5f, 0.50f);
        pad(36.0, 4.0, NAb3, PAD_VOLUME * 0.3f, 0.45f);
        // Building tension sweep (40-44)
        pad(40.0, 4.0, NC4,  PAD_VOLUME * 0.7f, 0.50f);
        pad(40.0, 4.0, NE4,  PAD_VOLUME * 0.5f, 0.55f);
        pad(40.0, 4.0, NAb4, PAD_VOLUME * 0.4f, 0.45f);
        // Crash sweep into climax
        pad(43.0, 2.0, NE5,  PAD_VOLUME * 1.0f, 0.50f);
        pad(43.0, 2.0, NC5,  PAD_VOLUME * 0.7f, 0.55f);
        pad(43.0, 2.0, NAb4, PAD_VOLUME * 0.5f, 0.45f);
    }

    // ======================================================================
    // SECTION C / CLIMAX (44–58 s) — 160 BPM — full energy
    // ======================================================================
    {
        double bpm = 160.0;
        double b = bl(bpm);   // 0.375 s
        double e = b * 0.5;
        double s16 = b * 0.25;

        // --- Melody: soaring, rhythmically intense ---
        // Phrase C1 (44–48): triumphant ascending
        double t = 44.0;
        mel(t,       e,   NF5,  MELODY_VOLUME * 1.2f, 0.55f);
        mel(t+e,     e,   NE5,  MELODY_VOLUME * 1.1f, 0.53f);
        mel(t+2*e,   e,   NF5,  MELODY_VOLUME * 1.2f, 0.55f);
        mel(t+3*e,   b,   NE5,  MELODY_VOLUME * 1.1f, 0.52f);
        mel(t+3*e+b, e,   NDb5, MELODY_VOLUME, 0.50f);
        mel(t+4*e+b, b*2, NC5,  MELODY_VOLUME, 0.50f);
        t = 46.0;
        mel(t,       e,   NAb4, MELODY_VOLUME * 0.9f, 0.48f);
        mel(t+e,     e,   NBb4, MELODY_VOLUME * 0.95f, 0.50f);
        mel(t+2*e,   e,   NC5,  MELODY_VOLUME, 0.52f);
        mel(t+3*e,   e,   NDb5, MELODY_VOLUME * 1.05f, 0.53f);
        mel(t+4*e,   b,   NE5,  MELODY_VOLUME * 1.15f, 0.55f);
        mel(t+4*e+b, b,   NF5,  MELODY_VOLUME * 1.25f, 0.55f);

        // Phrase C2 (48–52): rhythmic stab pattern
        t = 48.0;
        for (int i = 0; i < 4; i++) {
            double freq = (i == 0) ? NE5 : (i == 1) ? NC5 : (i == 2) ? NE5 : NF5;
            float pan = 0.50f + (i % 2 == 0 ? -0.05f : 0.05f);
            mel(t + i * b, s16 * 3, freq, MELODY_VOLUME * 1.1f, pan);
        }
        t = 49.5;
        mel(t,     e,   NDb5, MELODY_VOLUME, 0.50f);
        mel(t+e,   e,   NC5,  MELODY_VOLUME, 0.48f);
        mel(t+2*e, b*2, NBb4, MELODY_VOLUME * 0.95f, 0.50f);
        // Rapid arpeggio (50.5–52)
        t = 50.5;
        double arp[] = { NF4, NAb4, NC5, NE5, NF5, NE5, NC5, NAb4 };
        for (int i = 0; i < 8; i++) {
            mel(t + i * s16 * 1.5, s16, arp[i],
                MELODY_VOLUME * (0.8f + 0.05f * (i < 5 ? i : 8-i)), 0.50f);
        }

        // Phrase C3 (52–56): peak intensity with held high notes
        t = 52.0;
        mel(t,     b,     NF5,  MELODY_VOLUME * 1.3f, 0.55f);
        mel(t+b,   e,     NE5,  MELODY_VOLUME * 1.2f, 0.53f);
        mel(t+b+e, b*1.5, NDb5, MELODY_VOLUME * 1.1f, 0.50f);
        t = 53.5;
        mel(t,     e,   NC5,  MELODY_VOLUME, 0.50f);
        mel(t+e,   e,   NBb4, MELODY_VOLUME * 0.95f, 0.48f);
        mel(t+2*e, e,   NAb4, MELODY_VOLUME * 0.9f, 0.45f);
        mel(t+3*e, b*3, NG4,  MELODY_VOLUME * 0.85f, 0.48f);
        t = 55.0;
        mel(t,     e,   NAb4, MELODY_VOLUME * 0.9f, 0.50f);
        mel(t+e,   e,   NBb4, MELODY_VOLUME * 0.95f, 0.52f);
        mel(t+2*e, b*4, NF4,  MELODY_VOLUME, 0.50f);

        // Phrase C4 (56–58): triumphant final statement
        t = 56.5;
        mel(t,     e,   NC5,  MELODY_VOLUME * 1.1f, 0.52f);
        mel(t+e,   e,   NE5,  MELODY_VOLUME * 1.2f, 0.55f);
        mel(t+2*e, b*3, NF5,  MELODY_VOLUME * 1.3f, 0.55f);

        // --- Bass: driving double-time ---
        double roots_c[] = { NF2, NAb2, NDb3, NE3, NF2, NC3, NAb2, NBb2,
                             NF2, NDb3, NE3, NC3, NAb2, NBb2 };
        for (int bar = 0; bar < 14; bar++) {
            double bt = 44.0 + bar * b * 2;
            if (bt >= 58.0) break;
            double root = roots_c[bar % 14];
            // Fast eighth-note pattern
            for (int i = 0; i < 4; i++) {
                double freq = (i % 2 == 0) ? root : root * 2.0;
                float vol = BASS_VOLUME * 1.1f * ((i == 0) ? 1.0f : 0.75f);
                bas(bt + i * e, e * 0.8, freq, vol);
            }
        }

        // --- Drums: double-time hat, powerful kick/snare ---
        for (double dt = 44.0; dt < 58.0; dt += b) {
            int beatInBar = static_cast<int>(std::round((dt - 44.0) / b)) % 4;
            drm(dt, 0, DRUM_VOLUME * 1.1f);               // kick every beat
            if (beatInBar == 1 || beatInBar == 3) {
                drm(dt, 1, DRUM_VOLUME);                   // snare 2 & 4
            }
            // Double-time hats (sixteenth notes)
            drm(dt,           2, DRUM_VOLUME * 0.5f);
            drm(dt + s16,     2, DRUM_VOLUME * 0.30f);
            drm(dt + 2*s16,   2, DRUM_VOLUME * 0.40f);
            drm(dt + 3*s16,   2, DRUM_VOLUME * 0.25f);
        }

        // --- Pad stabs at structural points ---
        pad(44.0, 1.5, NF3,  PAD_VOLUME * 0.8f, 0.50f);
        pad(44.0, 1.5, NC4,  PAD_VOLUME * 0.5f, 0.55f);
        pad(44.0, 1.5, NE4,  PAD_VOLUME * 0.4f, 0.45f);
        // Mid-section accent
        pad(48.0, 1.0, NAb3, PAD_VOLUME * 0.7f, 0.50f);
        pad(48.0, 1.0, NDb4, PAD_VOLUME * 0.5f, 0.55f);
        // Pre-phrase stab
        pad(52.0, 1.0, NF3,  PAD_VOLUME * 0.9f, 0.50f);
        pad(52.0, 1.0, NAb3, PAD_VOLUME * 0.6f, 0.45f);
        pad(52.0, 1.0, NC4,  PAD_VOLUME * 0.5f, 0.55f);
        // Transition sweep C→Outro
        pad(57.0, 2.0, NE4,  PAD_VOLUME * 0.8f, 0.50f);
        pad(57.0, 2.0, NC4,  PAD_VOLUME * 0.5f, 0.55f);
        pad(57.0, 2.0, NAb3, PAD_VOLUME * 0.4f, 0.45f);
    }

    // ======================================================================
    // OUTRO (58–73 s) — 160→80 BPM — decelerating, melody dissolves
    // ======================================================================
    {
        // Use the instantaneous tempo for accurate beat placement
        // We pre-compute beat positions by integrating the tempo curve

        // Melody fragments becoming sparser
        mel(58.5, 0.8, NE5,  MELODY_VOLUME * 0.9f, 0.52f);
        mel(59.5, 0.6, NDb5, MELODY_VOLUME * 0.75f, 0.50f);
        mel(60.5, 1.0, NC5,  MELODY_VOLUME * 0.65f, 0.48f);
        mel(62.0, 1.2, NAb4, MELODY_VOLUME * 0.55f, 0.50f);
        mel(64.0, 1.5, NG4,  MELODY_VOLUME * 0.45f, 0.48f);
        mel(66.0, 2.0, NF4,  MELODY_VOLUME * 0.40f, 0.50f);
        // Final ghost note
        mel(69.0, 3.0, NF4,  MELODY_VOLUME * 0.25f, 0.50f);

        // Bass: long notes, fading
        bas(58.0, 2.0, NF2,  BASS_VOLUME * 0.8f);
        bas(60.0, 2.0, NAb2, BASS_VOLUME * 0.65f);
        bas(62.0, 2.5, NC3,  BASS_VOLUME * 0.55f);
        bas(64.5, 3.0, NF2,  BASS_VOLUME * 0.45f);
        bas(68.0, 4.0, NF2,  BASS_VOLUME * 0.30f);

        // Drums: decelerating, getting quieter
        {
            // Place beats at roughly decelerating intervals
            double beatTimes[] = { 58.0, 58.38, 58.75, 59.13, 59.5, 59.9,
                                   60.3, 60.75, 61.2, 61.7, 62.2, 62.8,
                                   63.4, 64.1, 64.9, 65.8, 66.8 };
            for (int i = 0; i < 17; i++) {
                float vol = DRUM_VOLUME * (1.0f - i * 0.05f);
                if (vol < 0.05f) vol = 0.05f;
                drm(beatTimes[i], 0, vol * 0.7f);
                if (i % 4 == 2) drm(beatTimes[i], 1, vol * 0.6f);
                if (i < 12) drm(beatTimes[i], 2, vol * 0.3f);
            }
        }

        // Sweeps: long dissolving pad
        pad(58.0, 5.0, NF3,  PAD_VOLUME * 0.6f, 0.50f);
        pad(58.0, 5.0, NAb3, PAD_VOLUME * 0.4f, 0.45f);
        pad(58.0, 5.0, NC4,  PAD_VOLUME * 0.3f, 0.55f);
        // Final ethereal sweep
        pad(64.0, 8.0, NF3,  PAD_VOLUME * 0.4f, 0.50f);
        pad(64.0, 8.0, NC4,  PAD_VOLUME * 0.25f, 0.55f);
        pad(64.0, 8.0, NE4,  PAD_VOLUME * 0.2f, 0.45f);
        // Very last breath
        pad(70.0, 3.0, NF3,  PAD_VOLUME * 0.15f, 0.50f);
    }
}

// ---- Victory composition (F harmonic minor — upbeat, triumphant) --------
// ~25 seconds, loops. Despite harmonic minor, achieves brightness through
// major-feel techniques: raised 7th (E natural), bright arpeggios, fast tempo,
// high register, and rhythmic energy.
void TitleMelody::buildVictoryComposition() {
    melodyEvents.clear();
    bassEvents.clear();
    padEvents.clear();
    drumEvents.clear();

    auto mel = [&](double t, double d, double f, float v, float p = 0.5f) {
        melodyEvents.push_back({t, d, f, 3, v, p});
    };
    auto bas = [&](double t, double d, double f, float v, float p = 0.5f) {
        bassEvents.push_back({t, d, f, 0, v, p});
    };
    auto pad = [&](double t, double d, double f, float v, float p = 0.5f) {
        padEvents.push_back({t, d, f, 4, v, p});
    };
    auto drm = [&](double t, int type, float v) {
        drumEvents.push_back({t, type, v});
    };

    double bpm = 155.0;
    double b = 60.0 / bpm;
    double e = b * 0.5;
    double s16 = b * 0.25;

    // Triumphant fanfare opening (0-4s)
    // Rising arpeggio: F4 → Ab4 → C5 → E5 → F5 (bright harmonic minor!)
    mel(0.0,    e,   NF4,  MELODY_VOLUME * 1.0f, 0.48f);
    mel(0.0+e,  e,   NAb4, MELODY_VOLUME * 1.0f, 0.50f);
    mel(0.0+2*e, e,  NC5,  MELODY_VOLUME * 1.1f, 0.52f);
    mel(0.0+3*e, b,  NE5,  MELODY_VOLUME * 1.2f, 0.55f);
    mel(0.0+3*e+b, b*2, NF5, MELODY_VOLUME * 1.3f, 0.55f);
    // Pad chord: F major-ish feel from the raised 7th
    pad(0.0, 4.0, NF3,  PAD_VOLUME * 0.7f, 0.50f);
    pad(0.0, 4.0, NAb3, PAD_VOLUME * 0.5f, 0.45f);
    pad(0.0, 4.0, NC4,  PAD_VOLUME * 0.6f, 0.55f);
    // Bass: steady root
    bas(0.0, b*2, NF2, BASS_VOLUME * 0.9f);
    bas(b*2, b*2, NC3, BASS_VOLUME * 0.8f);
    // Drums: kick on every beat, snare on 2&4
    for (double dt = 0.0; dt < 4.0; dt += b) {
        int beat = static_cast<int>(std::round(dt / b)) % 4;
        drm(dt, 0, DRUM_VOLUME);
        if (beat == 1 || beat == 3) drm(dt, 1, DRUM_VOLUME * 0.85f);
        drm(dt, 2, DRUM_VOLUME * 0.4f);
        drm(dt + e, 2, DRUM_VOLUME * 0.3f);
    }

    // Joyful theme (4-12s) — bouncy, rhythmic, using brightness of E natural
    double t = 4.0;
    mel(t,     e,   NC5,  MELODY_VOLUME, 0.50f);
    mel(t+e,   e,   NE5,  MELODY_VOLUME * 1.1f, 0.53f);
    mel(t+2*e, b,   NF5,  MELODY_VOLUME * 1.2f, 0.55f);
    mel(t+2*e+b, e, NE5,  MELODY_VOLUME * 1.1f, 0.53f);
    mel(t+3*e+b, e, NDb5, MELODY_VOLUME, 0.50f);
    mel(t+4*e+b, b*2, NC5, MELODY_VOLUME, 0.50f);
    t = 6.0;
    mel(t,     e,   NAb4, MELODY_VOLUME * 0.9f, 0.48f);
    mel(t+e,   e,   NBb4, MELODY_VOLUME * 0.95f, 0.50f);
    mel(t+2*e, e,   NC5,  MELODY_VOLUME, 0.52f);
    mel(t+3*e, b,   NE5,  MELODY_VOLUME * 1.15f, 0.55f);
    mel(t+3*e+b, b*2, NF5, MELODY_VOLUME * 1.2f, 0.55f);
    t = 8.0;
    // Descending resolution with energy
    mel(t,     e,   NF5,  MELODY_VOLUME * 1.2f, 0.55f);
    mel(t+e,   e,   NE5,  MELODY_VOLUME * 1.1f, 0.53f);
    mel(t+2*e, e,   NDb5, MELODY_VOLUME, 0.50f);
    mel(t+3*e, e,   NC5,  MELODY_VOLUME, 0.50f);
    mel(t+4*e, b*2, NAb4, MELODY_VOLUME * 0.9f, 0.48f);
    t = 10.0;
    // Quick ornamental run
    for (int i = 0; i < 8; i++) {
        double scaleV[] = { NF4, NG4, NAb4, NBb4, NC5, NDb5, NE5, NF5 };
        mel(t + i * s16, s16 * 0.9, scaleV[i], MELODY_VOLUME * (0.7f + 0.06f * i), 0.50f);
    }
    mel(t + 8*s16, b*2, NF5, MELODY_VOLUME * 1.3f, 0.55f);

    // Bass line (4-12s)
    double rootsV[] = { NF2, NAb2, NDb3, NC3, NF2, NBb2, NE3, NF2 };
    for (int bar = 0; bar < 8; bar++) {
        double bt = 4.0 + bar * b * 2;
        bas(bt, b * 0.85, rootsV[bar % 8], BASS_VOLUME);
        bas(bt + b, b * 0.85, rootsV[bar % 8] * 2.0, BASS_VOLUME * 0.7f);
    }
    // Drums (4-12s)
    for (double dt = 4.0; dt < 12.0; dt += b) {
        int beat = static_cast<int>(std::round((dt - 4.0) / b)) % 4;
        drm(dt, 0, DRUM_VOLUME * 1.1f);
        if (beat == 1 || beat == 3) drm(dt, 1, DRUM_VOLUME * 0.9f);
        drm(dt, 2, DRUM_VOLUME * 0.45f);
        drm(dt + s16*2, 2, DRUM_VOLUME * 0.3f);
    }
    pad(4.0, 4.0, NF3, PAD_VOLUME * 0.6f, 0.50f);
    pad(4.0, 4.0, NC4, PAD_VOLUME * 0.4f, 0.55f);
    pad(8.0, 4.0, NAb3, PAD_VOLUME * 0.5f, 0.45f);
    pad(8.0, 4.0, NE4, PAD_VOLUME * 0.4f, 0.55f);

    // Triumphant climax (12-20s) — high energy celebration
    t = 12.0;
    mel(t,       e,   NF5,  MELODY_VOLUME * 1.3f, 0.55f);
    mel(t+e,     e,   NE5,  MELODY_VOLUME * 1.2f, 0.53f);
    mel(t+2*e,   e,   NF5,  MELODY_VOLUME * 1.3f, 0.55f);
    mel(t+3*e,   b,   NG5,  MELODY_VOLUME * 1.3f, 0.55f);
    mel(t+3*e+b, b*2, NF5,  MELODY_VOLUME * 1.2f, 0.55f);
    t = 14.0;
    mel(t,     e,   NE5,  MELODY_VOLUME * 1.1f, 0.53f);
    mel(t+e,   e,   NDb5, MELODY_VOLUME, 0.50f);
    mel(t+2*e, b,   NC5,  MELODY_VOLUME, 0.50f);
    mel(t+2*e+b, e, NAb4, MELODY_VOLUME * 0.9f, 0.48f);
    mel(t+3*e+b, b*2, NF4, MELODY_VOLUME * 0.85f, 0.50f);
    // Repeat fanfare motif
    t = 16.0;
    mel(t,     s16, NF4,  MELODY_VOLUME * 0.9f, 0.50f);
    mel(t+s16, s16, NAb4, MELODY_VOLUME * 0.95f, 0.50f);
    mel(t+2*s16, s16, NC5, MELODY_VOLUME, 0.52f);
    mel(t+3*s16, b*2, NE5, MELODY_VOLUME * 1.2f, 0.55f);
    t = 18.0;
    mel(t,     b*2, NF5, MELODY_VOLUME * 1.3f, 0.55f);
    mel(t+b*2, b*3, NC5, MELODY_VOLUME * 0.9f, 0.50f);
    // Bass + drums (12-20s)
    for (int bar = 0; bar < 8; bar++) {
        double bt = 12.0 + bar * b * 2;
        bas(bt, b*1.5, rootsV[bar % 8], BASS_VOLUME * 1.1f);
    }
    for (double dt = 12.0; dt < 20.0; dt += b) {
        drm(dt, 0, DRUM_VOLUME * 1.1f);
        int beat = static_cast<int>(std::round((dt - 12.0) / b)) % 4;
        if (beat == 1 || beat == 3) drm(dt, 1, DRUM_VOLUME);
        drm(dt, 2, DRUM_VOLUME * 0.5f);
        drm(dt + s16*2, 2, DRUM_VOLUME * 0.35f);
    }
    pad(12.0, 4.0, NF3, PAD_VOLUME * 0.8f, 0.50f);
    pad(12.0, 4.0, NC4, PAD_VOLUME * 0.5f, 0.55f);
    pad(16.0, 4.0, NAb3, PAD_VOLUME * 0.6f, 0.45f);
    pad(16.0, 4.0, NE4, PAD_VOLUME * 0.5f, 0.55f);

    // Gentle landing (20-25s)
    mel(20.0, b*2, NF5, MELODY_VOLUME * 0.8f, 0.52f);
    mel(21.5, b*2, NC5, MELODY_VOLUME * 0.6f, 0.50f);
    mel(23.0, b*3, NF4, MELODY_VOLUME * 0.4f, 0.50f);
    bas(20.0, 3.0, NF2, BASS_VOLUME * 0.5f);
    bas(23.0, 2.0, NF2, BASS_VOLUME * 0.3f);
    pad(20.0, 5.0, NF3, PAD_VOLUME * 0.4f, 0.50f);
    pad(20.0, 5.0, NC4, PAD_VOLUME * 0.25f, 0.55f);
}

// ---- Defeat composition (F harmonic minor — melancholic, slow) ----------
// ~25 seconds, loops. Somber mood: slow tempo, low register, sparse rhythm.
void TitleMelody::buildDefeatComposition() {
    melodyEvents.clear();
    bassEvents.clear();
    padEvents.clear();
    drumEvents.clear();

    auto mel = [&](double t, double d, double f, float v, float p = 0.5f) {
        melodyEvents.push_back({t, d, f, 3, v, p});
    };
    auto bas = [&](double t, double d, double f, float v, float p = 0.5f) {
        bassEvents.push_back({t, d, f, 0, v, p});
    };
    auto pad = [&](double t, double d, double f, float v, float p = 0.5f) {
        padEvents.push_back({t, d, f, 4, v, p});
    };
    auto drm = [&](double t, int type, float v) {
        drumEvents.push_back({t, type, v});
    };

    double bpm = 70.0;
    double b = 60.0 / bpm;
    double e = b * 0.5;

    // Desolate opening pad (0-6s) — minor chord, hollow sound
    pad(0.0, 6.0, NF3,  PAD_VOLUME * 0.5f, 0.50f);
    pad(0.0, 6.0, NAb3, PAD_VOLUME * 0.35f, 0.45f);
    pad(0.0, 6.0, NC4,  PAD_VOLUME * 0.3f, 0.55f);

    // Slow, descending melody — lament (0-8s)
    mel(1.0,  b*2, NC5,  MELODY_VOLUME * 0.5f, 0.50f);
    mel(3.0,  b*2, NBb4, MELODY_VOLUME * 0.45f, 0.48f);
    mel(5.0,  b*2, NAb4, MELODY_VOLUME * 0.4f, 0.50f);
    mel(7.0,  b*3, NF4,  MELODY_VOLUME * 0.35f, 0.50f);

    // Deep bass: slow heartbeat-like pulse
    bas(0.0, b*2, NF2, BASS_VOLUME * 0.5f);
    bas(2.5, b*2, NAb2, BASS_VOLUME * 0.4f);
    bas(5.0, b*2, NDb3, BASS_VOLUME * 0.45f);
    bas(7.5, b*3, NF2, BASS_VOLUME * 0.4f);

    // Sparse kick only — funeral march feel
    drm(0.0, 0, DRUM_VOLUME * 0.5f);
    drm(2.5, 0, DRUM_VOLUME * 0.45f);
    drm(5.0, 0, DRUM_VOLUME * 0.4f);
    drm(7.5, 0, DRUM_VOLUME * 0.35f);

    // Middle section (8-16s) — slightly more motion but still sad
    pad(8.0, 4.0, NAb3, PAD_VOLUME * 0.4f, 0.45f);
    pad(8.0, 4.0, NDb4, PAD_VOLUME * 0.3f, 0.55f);
    pad(12.0, 4.0, NF3,  PAD_VOLUME * 0.45f, 0.50f);
    pad(12.0, 4.0, NAb3, PAD_VOLUME * 0.3f, 0.45f);

    // Melody: ascending attempt that falls back — hope denied
    mel(8.5,   b*1.5, NF4,  MELODY_VOLUME * 0.45f, 0.50f);
    mel(10.0,  b*1.5, NAb4, MELODY_VOLUME * 0.5f, 0.48f);
    mel(11.5,  b*1.5, NBb4, MELODY_VOLUME * 0.55f, 0.50f);
    mel(13.0,  b,     NC5,  MELODY_VOLUME * 0.6f, 0.52f);
    // Falls back down
    mel(14.0,  b,     NBb4, MELODY_VOLUME * 0.5f, 0.50f);
    mel(15.0,  b*2,   NAb4, MELODY_VOLUME * 0.4f, 0.48f);

    // Bass walking down
    bas(8.0,  b*2, NAb2, BASS_VOLUME * 0.45f);
    bas(10.0, b*2, NDb3, BASS_VOLUME * 0.4f);
    bas(12.0, b*2, NC3,  BASS_VOLUME * 0.45f);
    bas(14.0, b*3, NF2,  BASS_VOLUME * 0.4f);

    // Sparse drums with occasional snare (ghost notes)
    for (double dt = 8.0; dt < 16.0; dt += b * 2) {
        drm(dt, 0, DRUM_VOLUME * 0.4f);
    }
    drm(11.0, 1, DRUM_VOLUME * 0.25f);
    drm(15.0, 1, DRUM_VOLUME * 0.3f);

    // Final section (16-25s) — fading into silence
    pad(16.0, 5.0, NF3,  PAD_VOLUME * 0.35f, 0.50f);
    pad(16.0, 5.0, NAb3, PAD_VOLUME * 0.25f, 0.45f);

    // Very sparse, ghostly melody fragments
    mel(17.0, b*2, NE4,  MELODY_VOLUME * 0.3f, 0.52f);
    mel(19.5, b*3, NF4,  MELODY_VOLUME * 0.25f, 0.50f);
    mel(22.0, b*3, NF4,  MELODY_VOLUME * 0.15f, 0.50f);

    bas(16.0, 4.0, NF2, BASS_VOLUME * 0.3f);
    bas(20.0, 5.0, NF2, BASS_VOLUME * 0.2f);

    // Final lonely kick
    drm(16.0, 0, DRUM_VOLUME * 0.3f);
    drm(20.0, 0, DRUM_VOLUME * 0.2f);
    // Ghost hi-hat
    pad(21.0, 4.0, NF3, PAD_VOLUME * 0.15f, 0.50f);
}
void TitleMelody::renderFrame(std::vector<int16_t>& buffer,
                               int samples,
                               int sampleRate,
                               SynthesizerEngine* engine,
                               std::vector<int16_t>& mixBuf) {
    if (!playing || !engine) return;

    double frameDuration = static_cast<double>(samples) / sampleRate;
    double frameStart = playbackPos;
    size_t bufSize = static_cast<size_t>(samples) * 2;

    // Advance playback position
    playbackPos += frameDuration;

    // Handle fade-out
    if (fadingOut) {
        masterVolume -= static_cast<float>(frameDuration) * fadeSpeed;
        if (masterVolume <= 0.0f) {
            stop();
            return;
        }
    }

    // Loop after total length
    if (playbackPos >= totalLength) {
        playbackPos -= totalLength;
    }

    // Global scale keeps melody well below menu/TTS volume
    float globalScale = masterVolume * TITLE_GLOBAL_VOLUME_SCALE;

    // --- Helper: render a NoteEvent list into buffer --------------------------
    auto renderEvents = [&](const std::vector<NoteEvent>& events,
                            double attackS, double releaseRatio) {
        for (const auto& ev : events) {
            if (frameStart >= ev.time && frameStart < ev.time + ev.duration) {
                std::fill(mixBuf.begin(), mixBuf.end(), static_cast<int16_t>(0));
                int vol = static_cast<int>(ev.volume * globalScale * 100.0f);
                engine->generateAudio(mixBuf, samples, ev.curveIndex,
                                      ev.freqHz, static_cast<double>(ev.pan), vol);
                // Per-note envelope
                double notePos = frameStart - ev.time;
                double noteRatio = notePos / ev.duration;
                float envGain = 1.0f;
                if (notePos < attackS)
                    envGain = static_cast<float>(notePos / attackS);
                if (noteRatio > (1.0 - releaseRatio))
                    envGain *= static_cast<float>((1.0 - noteRatio) / releaseRatio);
                for (size_t i = 0; i < bufSize; i++) {
                    int32_t mixed = static_cast<int32_t>(buffer[i]) +
                                    static_cast<int32_t>(static_cast<float>(mixBuf[i]) * envGain);
                    buffer[i] = static_cast<int16_t>(
                        std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
                }
            }
        }
    };

    // Melody: short attack, moderate release
    renderEvents(melodyEvents, 0.005, 0.10);
    // Bass: very short attack, longer release
    renderEvents(bassEvents, 0.003, 0.15);
    // Pads/sweeps: slow attack, long release
    renderEvents(padEvents, 0.08, 0.30);

    // --- Drum rendering (procedural from event list) -------------------------
    for (const auto& dh : drumEvents) {
        // Only render hits that overlap the current frame window
        double hitEnd;
        switch (dh.type) {
            case 0:  hitEnd = dh.time + 0.07;  break;  // kick
            case 1:  hitEnd = dh.time + 0.06;  break;  // snare
            default: hitEnd = dh.time + 0.02;  break;  // hihat
        }
        if (frameStart < dh.time || frameStart >= hitEnd) continue;

        double hitPos = frameStart - dh.time;
        float gain = dh.volume * globalScale;

        for (int i = 0; i < samples; i++) {
            float t = static_cast<float>(hitPos) + static_cast<float>(i) / sampleRate;
            float s = 0.0f;

            if (dh.type == 0) {
                // Kick: pitch-dropping sine
                float env = std::max(0.0f, 1.0f - t / 0.07f);
                env *= env;
                float freq = KICK_START_FREQ - KICK_PITCH_SWEEP * t;
                if (freq < KICK_MIN_FREQ) freq = KICK_MIN_FREQ;
                kickPhase += static_cast<double>(freq) / sampleRate;
                if (kickPhase > 1.0) kickPhase -= static_cast<int>(kickPhase);
                s = std::sin(static_cast<float>(kickPhase * 2.0 * PI)) * env * gain;
                int16_t sample = static_cast<int16_t>(std::clamp(s * KICK_AMPLITUDE, -32768.0f, 32767.0f));
                int32_t mL = static_cast<int32_t>(buffer[i*2])   + sample;
                int32_t mR = static_cast<int32_t>(buffer[i*2+1]) + sample;
                buffer[i*2]   = static_cast<int16_t>(std::clamp(mL, (int32_t)-32768, (int32_t)32767));
                buffer[i*2+1] = static_cast<int16_t>(std::clamp(mR, (int32_t)-32768, (int32_t)32767));
            }
            else if (dh.type == 1) {
                // Snare: short noise burst + tonal body
                float env = std::max(0.0f, 1.0f - t / 0.06f);
                env *= env;
                // Noise component (glibc LCG)
                unsigned int seed = hihatSeed + static_cast<unsigned int>(dh.time * 10000) + i;
                seed = seed * LCG_MULTIPLIER + LCG_INCREMENT;
                float noise = ((seed >> 16) & 0x7FFF) / 32768.0f * 2.0f - 1.0f;
                // Tonal component
                snarePhase += static_cast<double>(SNARE_BODY_FREQ) / sampleRate;
                if (snarePhase > 1.0) snarePhase -= 1.0;
                float tone = std::sin(static_cast<float>(snarePhase * 2.0 * PI));
                s = (noise * 0.7f + tone * 0.3f) * env * gain;
                int16_t sample = static_cast<int16_t>(std::clamp(s * SNARE_AMPLITUDE, -32768.0f, 32767.0f));
                // Snare slightly left
                int32_t mL = static_cast<int32_t>(buffer[i*2])   + static_cast<int32_t>(sample * 0.55f);
                int32_t mR = static_cast<int32_t>(buffer[i*2+1]) + static_cast<int32_t>(sample * 0.45f);
                buffer[i*2]   = static_cast<int16_t>(std::clamp(mL, (int32_t)-32768, (int32_t)32767));
                buffer[i*2+1] = static_cast<int16_t>(std::clamp(mR, (int32_t)-32768, (int32_t)32767));
            }
            else {
                // Hi-hat: very short noise burst (closed=type 2, open=type 3)
                float duration = (dh.type == 3) ? 0.04f : 0.015f;
                float env = std::max(0.0f, 1.0f - t / duration);
                env *= env;
                // Noise via glibc LCG
                unsigned int seed = hihatSeed + static_cast<unsigned int>(dh.time * 10000) + i * 7;
                seed = seed * LCG_MULTIPLIER + LCG_INCREMENT;
                float noise = ((seed >> 16) & 0x7FFF) / 32768.0f * 2.0f - 1.0f;
                s = noise * env * gain * 0.5f;
                int16_t sample = static_cast<int16_t>(std::clamp(s * HIHAT_AMPLITUDE, -32768.0f, 32767.0f));
                // Hat slightly right
                int32_t mL = static_cast<int32_t>(buffer[i*2])   + static_cast<int32_t>(sample * 0.4f);
                int32_t mR = static_cast<int32_t>(buffer[i*2+1]) + static_cast<int32_t>(sample * 0.6f);
                buffer[i*2]   = static_cast<int16_t>(std::clamp(mL, (int32_t)-32768, (int32_t)32767));
                buffer[i*2+1] = static_cast<int16_t>(std::clamp(mR, (int32_t)-32768, (int32_t)32767));
            }
        }
    }
}

// Initialize audio engine and backend
void Game::initAudio() {
    if (audioInitialized) return;
    
    log("HAMSPIRIT", "Initializing game audio...");
    
    // Create synthesizer engine
    audioEngine = std::make_unique<SynthesizerEngine>();
    if (!audioEngine->open()) {
        log("HAMSPIRIT", "WARNING: Failed to open audio engine");
        return;
    }
    
    // Set waveforms: curve 0 = motor (Triangle), curve 1 = SWR (Sine), 
    // curve 2 = morse signal (Sine), curve 3 = morse cannon (Square),
    // curve 4 = UI sounds/adjustment/bumper (Sawtooth)
    audioEngine->setCurveWaveform(0, Waveform::TRIANGLE);
    audioEngine->setCurveWaveform(1, Waveform::SINE);
    audioEngine->setCurveWaveform(2, Waveform::SINE);
    audioEngine->setCurveWaveform(3, Waveform::SQUARE);
    audioEngine->setCurveWaveform(4, Waveform::SAWTOOTH);
    
    // Release the synth engine's internal audio backend — we only use generateAudio()
    // for computation and play via our own dedicated backend.
    audioEngine->releaseBackend();
    
    // Create independent audio backend for the game (completely separate from analyzer).
    // This prevents audio conflicts during window focus changes and ensures the game
    // audio continues working regardless of the parent application's audio state.
    ownedAudioBackend.reset(createAudioBackend());
    if (ownedAudioBackend && ownedAudioBackend->initialize()) {
        audioBackend = ownedAudioBackend.get();
        audioBackend->setLogger(logger);
        log("HAMSPIRIT", "Created independent audio backend for game");
    } else {
        // Fallback: try to borrow from analyzer
        log("HAMSPIRIT", "Independent audio backend failed, falling back to analyzer backend");
        ownedAudioBackend.reset();
        if (analyzer) {
            audioBackend = analyzer->getAudioBackend();
        }
    }
    if (!audioBackend) {
        log("HAMSPIRIT", "WARNING: No audio backend available");
        return;
    }
    
    // Reset abort state so playBuffer() works
    audioBackend->resetAbort();
    
    // Pre-allocate audio buffers (stereo interleaved)
    audioBuffer.resize(GAME_AUDIO_SAMPLES * GAME_CHANNELS, 0);
    mixBuffer.resize(GAME_AUDIO_SAMPLES * GAME_CHANNELS, 0);
    
    // Create title melody (chiptune background music for menus)
    titleMelody = std::make_unique<TitleMelody>();
    titleMelody->start();
    
    audioInitialized = true;
    
    // 6.1 / 6.4: Wire up the feedback orchestrator with the immediate audio queue
    // and latency tracker.  This must happen after audio init but before the
    // audio thread starts consuming events.
    feedbackOrchestrator.setImmediateQueue(&immediateAudioQueue);
    feedbackOrchestrator.setLatencyTracker(&latencyTracker);
    
    log("HAMSPIRIT", "Game audio initialized successfully");
}

// Abort the primary audio backend and all multiplayer per-player backends
// so any blocking playBuffer() calls unblock immediately.
void Game::abortAllAudioBackends() {
    if (audioBackend) {
        audioBackend->abort();
    }
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        for (int p = 1; p < multiplayerMgr->getPlayerCount(); p++) {
            auto* ctx = multiplayerMgr->getPlayer(p);
            if (ctx && ctx->audioBackend) {
                ctx->audioBackend->abort();
            }
        }
    }
}

// Shutdown audio
void Game::shutdownAudio() {
    if (!audioInitialized) return;
    
    log("HAMSPIRIT", "Shutting down game audio...");
    
    // Signal audio thread to stop FIRST (before abort)
    audioRunning.store(false);
    
    // Abort all backends so blocking playBuffer() calls return
    abortAllAudioBackends();
    
    // Wait for audio thread to exit using the audioThreadExited flag
    if (audioThread.joinable()) {
        // Poll for up to 3 seconds, re-aborting periodically
        for (int i = 0; i < 150 && !audioThreadExited.load(); i++) {
            abortAllAudioBackends();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        
        if (audioThreadExited.load()) {
            audioThread.join();  // Thread exited — join returns immediately
        } else {
            log("HAMSPIRIT", "Audio thread join timed out after 3s — detaching");
            audioThread.detach();
        }
    }
    
    // Shut down owned audio backend (if we created one)
    if (ownedAudioBackend) {
        audioBackend = nullptr;
        ownedAudioBackend->shutdown();
        ownedAudioBackend.reset();
    } else if (audioBackend) {
        // Borrowed backend — just reset abort and release reference
        audioBackend->resetAbort();
        audioBackend = nullptr;
    }
    
    if (audioEngine) {
        audioEngine->close();
        audioEngine.reset();
    }
    
    audioInitialized = false;
}

// Audio thread — runs independently, generates and plays audio continuously
// Self-healing: if an exception kills the inner loop, it restarts automatically
void Game::audioThreadFunc() {
    audioThreadExited.store(false);
    log("HAMSPIRIT", "Audio thread started");
    
    const size_t bufSize = GAME_AUDIO_SAMPLES * GAME_CHANNELS;
    int frameCount = 0;
    int failCount = 0;
    int consecutiveFailCount = 0;     // Track consecutive failures for recovery
    float roughnessPhase = 0.0f;   // Motor roughness oscillator phase
    float rattlePhase = 0.0f;      // Motor gravel rattle oscillator phase
    float morsePatternPhase = 0.0f; // Morse signal pattern timing
    float smoothedPan = 0.0f;      // Smoothed pan for anti-crackle
    int crashCount = 0;            // Number of times the audio loop crashed and restarted
    
    // 6.4: Immediate morse cannon state tracked by audio thread
    // These are updated from the lock-free queue for sub-frame latency.
    bool immediateMorseActive = false;
    FeedbackTimePoint immediateMorseTimestamp{};
    float immediateMorsePhase = 0.0f;  // Phase accumulator for immediate morse tone
    
    // Ensure audio buffers are generously sized before entering the loop.
    if (audioBuffer.size() < bufSize) {
        audioBuffer.resize(bufSize, 0);
    }
    if (mixBuffer.size() < bufSize) {
        mixBuffer.resize(bufSize, 0);
    }
    
    // Self-healing outer loop: if the inner loop throws, we restart it
    while (audioRunning.load()) {
    try {
    while (audioRunning.load()) {
        frameCount++;
        
        // Heartbeat log every ~5 seconds (250 frames at 20ms each)
        if (frameCount % 250 == 0) {
            log("HAMSPIRIT_AUDIO", "Audio thread alive, frame " + std::to_string(frameCount) +
                ", failures: " + std::to_string(failCount));
        }
        
        // Safety: bail out early if audio backend disappeared
        if (!audioBackend) {
            std::this_thread::sleep_for(std::chrono::milliseconds(GAME_AUDIO_FRAME_MS));
            continue;
        }
        
        // Read current audio parameters
        AudioParams params;
        {
            std::lock_guard<std::mutex> lock(audioStateMtx);
            params = audioParams;
        }
        
        // 6.3: Record audio frame start time for sample-offset calculation
        audioFrameStartTime = FeedbackClock::now();
        
        // 6.4: Process immediate audio events from the lock-free queue.
        // These events carry timestamps and allow sub-frame latency for
        // critical sounds (P0), especially morse sidetone key-on/key-off.
        {
            ImmediateAudioEvent evt;
            while (immediateAudioQueue.pop(evt)) {
                switch (evt.type) {
                    case ImmediateAudioEvent::Type::MORSE_KEY_DOWN:
                        immediateMorseActive = true;
                        immediateMorseTimestamp = evt.timestamp;
                        break;
                    case ImmediateAudioEvent::Type::MORSE_KEY_UP:
                        immediateMorseActive = false;
                        immediateMorseTimestamp = evt.timestamp;
                        break;
                }
            }
        }
        
        // Only generate audio while in PLAYING state (or PAUSED/MAIN_MENU/INTRO for menu sounds + melody)
        GameState state = currentState.load();
        if (state != GameState::PLAYING && state != GameState::PAUSED && 
            state != GameState::MAIN_MENU && state != GameState::INTRO) {
            // Complete silence when not playing/paused/menu
            std::fill(audioBuffer.begin(), audioBuffer.end(), 0);
            // Still render title melody during non-gameplay states if it's playing
            try {
                if (titleMelody && titleMelody->isPlaying()) {
                    titleMelody->renderFrame(audioBuffer, GAME_AUDIO_SAMPLES,
                                             GAME_SAMPLE_RATE, audioEngine.get(), mixBuffer);
                }
            } catch (...) {
                // Don't let title melody crash kill audio thread
            }
            if (!audioBackend->playBuffer(audioBuffer.data(), GAME_AUDIO_SAMPLES,
                                      GAME_SAMPLE_RATE, GAME_CHANNELS, GAME_BITS)) {
                // Apply same recovery logic as gameplay path
                consecutiveFailCount++;
                if (consecutiveFailCount >= AUDIO_CONSECUTIVE_FAIL_RECOVERY && audioRunning.load()) {
                    log("HAMSPIRIT_AUDIO", "Audio recovery (non-gameplay) after " +
                        std::to_string(consecutiveFailCount) + " consecutive failures");
                    audioBackend->resetAbort();
                    consecutiveFailCount = 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(GAME_AUDIO_FRAME_MS));
            } else {
                consecutiveFailCount = 0;
                // Distribute menu/title audio to other players' backends (multiplayer)
                if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
                    try {
                        multiplayerMgr->distributeAudioToPlayers(
                            audioBuffer.data(), GAME_AUDIO_SAMPLES,
                            GAME_SAMPLE_RATE, GAME_CHANNELS, GAME_BITS);
                    } catch (...) {}
                }
            }
            continue;
        }
        
        // Clear main buffer
        std::fill(audioBuffer.begin(), audioBuffer.end(), 0);
        
        // In PAUSED state, only play menu sounds (no motor/SWR/morse)
        bool isPlaying = (state == GameState::PLAYING);
        
        // Convert pan from (-1..+1) to (0..1) for the engine, with smoothing to prevent crackle
        smoothedPan += 0.3f * (params.pan - smoothedPan);  // Low-pass filter
        double panFraction = static_cast<double>(smoothedPan + 1.0f) * 0.5;
        panFraction = std::clamp(panFraction, 0.0, 1.0);
        
        // Gameplay audio layers (only during PLAYING state)
        if (isPlaying) {
        try {
        
        // Layer 1: Motor sound (with road roughness from SWR quality)
        if (params.motorVolume > 0.0f) {
            audioEngine->generateAudio(audioBuffer, GAME_AUDIO_SAMPLES, 0,
                                        static_cast<double>(params.motorFreq), panFraction,
                                        static_cast<int>(params.motorVolume));
            
            // Apply roughness: amplitude modulation simulating road surface quality
            // Good SWR = smooth road (no modulation)
            // Bad SWR = rough road (noise-like AM + slight pitch instability)
            if (params.motorRoughness > 0.05f) {
                float roughness = params.motorRoughness;
                // Modulation rate: 15-60 Hz (faster = rougher, like gravel)
                float modRate = 15.0f + 45.0f * roughness;
                float phaseInc = modRate * TWO_PI / static_cast<float>(GAME_SAMPLE_RATE);
                // Modulation depth: 0-70% amplitude variation
                float modDepth = 0.7f * roughness;
                // Secondary "gravel" rattle at higher frequency
                float rattle = roughness * roughness;  // Only significant at high roughness
                float rattleRate = 80.0f + 120.0f * roughness;
                float rattleInc = rattleRate * TWO_PI / static_cast<float>(GAME_SAMPLE_RATE);
                
                for (int i = 0; i < GAME_AUDIO_SAMPLES; i++) {
                    // Primary roughness: low-freq AM (road bumps)
                    float mod = 1.0f - modDepth * (0.5f + 0.5f * std::sin(roughnessPhase));
                    roughnessPhase += phaseInc;
                    
                    // Secondary rattle: higher-freq AM (gravel/stones) 
                    float rattleMod = 1.0f - rattle * 0.3f * (0.5f + 0.5f * std::sin(rattlePhase));
                    rattlePhase += rattleInc;
                    
                    float combinedMod = mod * rattleMod;
                    
                    // Apply to both stereo channels
                    int idx = i * GAME_CHANNELS;
                    audioBuffer[idx] = static_cast<int16_t>(audioBuffer[idx] * combinedMod);
                    if (GAME_CHANNELS > 1) {
                        audioBuffer[idx + 1] = static_cast<int16_t>(audioBuffer[idx + 1] * combinedMod);
                    }
                }
                if (roughnessPhase > TWO_PI) roughnessPhase -= TWO_PI;
                if (rattlePhase > TWO_PI) rattlePhase -= TWO_PI;
            }
        }
        
        // Layer 2: SWR alert tone (with L/C indication)
        if (params.swrAlertActive && params.swrVolume > 1.0f) {
            float beepHz = 1.0f + 5.0f * params.swrAlertRate;
            float frameDuration = static_cast<float>(GAME_AUDIO_FRAME_MS) / 1000.0f;
            swrAlertPhase += beepHz * frameDuration * TWO_PI;
            if (swrAlertPhase > TWO_PI) swrAlertPhase -= TWO_PI;
            
            bool beepOn = (std::sin(swrAlertPhase) > 0.0f);
            
            if (beepOn) {
                std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
                audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                            static_cast<double>(params.swrFreq), 0.5,
                                            static_cast<int>(params.swrVolume));
                
                // Reactance indication: 
                // Inductive (reactance > 0): frequency wobble ("eiern") — pitch modulation
                // Capacitive (reactance < 0): reverb/echo — metallic resonant tail
                float absReactance = std::abs(params.reactanceAtPlayer);
                // Scale effect intensity with reactance magnitude (more reactance = stronger effect)
                float effectIntensity = std::min(absReactance / 10.0f, 1.0f);
                
                if (params.reactanceAtPlayer > SWR_REACTANCE_THRESHOLD) {
                    // INDUCTIVE wobble ("eiern"): frequency modulation makes the pitch oscillate
                    // Like a warped record or a motor struggling — the pitch itself moves up and down
                    float wobbleDepth = 0.15f + 0.25f * effectIntensity;  // 15-40% pitch deviation
                    float wobbleRate = 4.0f + 3.0f * effectIntensity;     // 4-7 Hz warble rate
                    for (size_t i = 0; i < bufSize; i += 2) {
                        float t = static_cast<float>(i/2) / static_cast<float>(GAME_AUDIO_SAMPLES);
                        // FM: phase-shift the samples by resampling with oscillating offset
                        // This creates genuine pitch wobble, not just volume wobble
                        float wobble = wobbleDepth * std::sin(t * wobbleRate * TWO_PI);
                        // AM component: volume dip synchronized with pitch wobble for richer "eiern"
                        float amMod = 0.5f + 0.5f * (1.0f - wobbleDepth * std::abs(std::sin(t * wobbleRate * TWO_PI)));
                        // Combine: the AM gives a pulsing quality, the pitch shift is simulated
                        // by mixing in a slightly detuned version
                        int srcIdx = static_cast<int>((i/2) + wobble * WOBBLE_RESAMPLE_DEPTH) * 2;
                        srcIdx = std::clamp(srcIdx, 0, static_cast<int>(bufSize) - 2);
                        int32_t wobbled_l = static_cast<int32_t>(mixBuffer[srcIdx] * amMod);
                        int32_t wobbled_r = static_cast<int32_t>(mixBuffer[srcIdx + 1] * amMod);
                        mixBuffer[i] = static_cast<int16_t>(std::clamp(wobbled_l, (int32_t)-32768, (int32_t)32767));
                        mixBuffer[i+1] = static_cast<int16_t>(std::clamp(wobbled_r, (int32_t)-32768, (int32_t)32767));
                    }
                } else if (params.reactanceAtPlayer < -SWR_REACTANCE_THRESHOLD) {
                    // CAPACITIVE reverb: multi-tap echo with longer delays for audible metallic resonance
                    // Like sound bouncing inside a capacitor housing — ringing, shimmering tail
                    float echoMix = 0.3f + 0.3f * effectIntensity;  // 30-60% echo level
                    // Multiple echo taps at different delays for rich reverb
                    int tap1 = static_cast<int>(0.015f * GAME_SAMPLE_RATE) * 2;  // 15ms
                    int tap2 = static_cast<int>(0.035f * GAME_SAMPLE_RATE) * 2;  // 35ms  
                    int tap3 = static_cast<int>(0.055f * GAME_SAMPLE_RATE) * 2;  // 55ms
                    for (int i = static_cast<int>(bufSize) - 1; i >= 0; i--) {
                        int32_t original = static_cast<int32_t>(mixBuffer[i]);
                        int32_t echo = 0;
                        if (i >= tap1) echo += static_cast<int32_t>(mixBuffer[i - tap1]) * 5 / 10;
                        if (i >= tap2) echo += static_cast<int32_t>(mixBuffer[i - tap2]) * 3 / 10;
                        if (i >= tap3) echo += static_cast<int32_t>(mixBuffer[i - tap3]) * 2 / 10;
                        int32_t result = original + static_cast<int32_t>(echo * echoMix);
                        mixBuffer[i] = static_cast<int16_t>(std::clamp(result, (int32_t)-32768, (int32_t)32767));
                    }
                }
                
                for (size_t i = 0; i < bufSize; i++) {
                    int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                    audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
                }
            }
        }
        
        // Layer 3: Morse signal beeps (repeating pattern)
        {
            // Use phase counter to cycle through patterns
            float frameSec = static_cast<float>(GAME_AUDIO_FRAME_MS) / 1000.0f;
            morsePatternPhase += frameSec;
            // Publish to shared global phase so secondary players use the
            // same time reference (prevents inter-player pattern offset).
            globalMorsePatternPhase = morsePatternPhase;
            
            for (auto& sig : params.morseSignals) {
                if (sig.volume < 1 || sig.pattern.empty()) continue;
                
                // Calculate which element of the pattern we're on
                // Use the same timing as MorseCannon for consistency between
                // what the player hears and what they need to reproduce
                float dotLen = MorseCannon::DOT_DURATION;    // 100ms
                float dashLen = MorseCannon::DASH_DURATION;  // 300ms
                float gapLen = MorseCannon::ELEMENT_SPACE;   // 100ms
                float patternLen = 0.0f;
                for (char c : sig.pattern) {
                    patternLen += (c == '.') ? dotLen : dashLen;
                    patternLen += gapLen;
                }
                patternLen += MorseCannon::PATTERN_REPEAT_PAUSE; // pause between repeats
                
                float posInPattern = std::fmod(morsePatternPhase, patternLen);
                
                // Determine if we're in a "sound on" portion
                bool soundOn = false;
                float pos = 0.0f;
                for (char c : sig.pattern) {
                    float elemLen = (c == '.') ? dotLen : dashLen;
                    if (posInPattern >= pos && posInPattern < pos + elemLen) {
                        soundOn = true;
                        break;
                    }
                    pos += elemLen + gapLen;
                }
                
                if (soundOn) {
                    std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
                    audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 2,
                                                static_cast<double>(MORSE_SIGNAL_FREQ),
                                                static_cast<double>(sig.pan), sig.volume);
                    for (size_t i = 0; i < bufSize; i++) {
                        int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                        audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
                    }
                }
            }
        }
        
        // Layer 4: Morse cannon/paddle sidetone — single frequency CW tone (real keyer behavior)
        // Uses same sidetone frequency for both dots and dashes, differentiated only by duration.
        // 6.4: Audio-Immediate-Path — uses lock-free queue state (immediateMorseActive)
        // for sub-frame latency.  Falls back to frame-based params.morseCannonActive
        // if no immediate events were received (backward compatibility).
        {
            bool morseActive = immediateMorseActive || params.morseCannonActive;
            if (morseActive) {
                // Calculate sample offset for sub-frame start when using immediate path.
                // If the morse key-down happened mid-frame, start the tone at that exact sample.
                int startSample = 0;
                if (immediateMorseActive && immediateMorseTimestamp != FeedbackTimePoint{}) {
                    startSample = calculateSampleOffset(
                        immediateMorseTimestamp, audioFrameStartTime,
                        GAME_SAMPLE_RATE, GAME_AUDIO_SAMPLES);
                }
                
                // Generate sidetone with sample-accurate phase continuity
                float invSR = 1.0f / static_cast<float>(GAME_SAMPLE_RATE);
                for (int i = 0; i < GAME_AUDIO_SAMPLES; i++) {
                    if (i < startSample) continue;  // Silence before event timestamp
                    
                    immediateMorsePhase += MORSE_SIDETONE_FREQ * invSR;
                    if (immediateMorsePhase >= 1.0f) immediateMorsePhase -= 1.0f;
                    // Square wave for authentic CW sidetone
                    float sample = (immediateMorsePhase < 0.5f) ? 1.0f : -1.0f;
                    int16_t mixSample = static_cast<int16_t>(sample * 3000.0f);  // 6000 * 0.5
                    
                    int idx = i * GAME_CHANNELS;
                    int32_t mixL = static_cast<int32_t>(audioBuffer[idx])     + mixSample;
                    int32_t mixR = static_cast<int32_t>(audioBuffer[idx + 1]) + mixSample;
                    audioBuffer[idx]     = static_cast<int16_t>(std::clamp(mixL, (int32_t)-32768, (int32_t)32767));
                    audioBuffer[idx + 1] = static_cast<int16_t>(std::clamp(mixR, (int32_t)-32768, (int32_t)32767));
                }
            } else {
                // Reset phase when tone stops for clean restart
                immediateMorsePhase = 0.0f;
            }
        }
        
        } catch (const std::exception& e) {
            log("HAMSPIRIT_AUDIO", "Exception in gameplay audio layers: " + std::string(e.what()));
        } catch (...) {
            log("HAMSPIRIT_AUDIO", "Unknown exception in gameplay audio layers");
        }
        } // end isPlaying block
        
        // UI sound layers (play during PLAYING and PAUSED states)
        try {
        
        // Layer 5: Adjustment sound (ascending/descending tone for L/C/UNUN changes)
        // Pan position reflects value position in the stereo field
        if (params.adjustSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Create a quick ascending or descending pitch sweep
            float baseFreq = params.adjustSoundUp ? 400.0f : 800.0f;
            float step = params.adjustSoundUp ? 80.0f : -80.0f;
            float freq = baseFreq + step * static_cast<float>(6 - params.adjustSoundFrames);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        static_cast<double>(freq), static_cast<double>(params.adjustSoundPan), 80);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            // Decrement remaining frames (thread-safe via audioStateMtx)
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.adjustSoundFrames > 0)
                    audioParams.adjustSoundFrames--;
            }
        }
        
        // Layer 6: Bumper sound (harsh buzz when tuner hits limit)
        if (params.bumperSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Low harsh buzz at 150 Hz (sawtooth wave)
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        150.0, 0.5, 100);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.bumperSoundFrames > 0)
                    audioParams.bumperSoundFrames--;
            }
        }
        
        // Layer 7: Collection success chime (ascending major triad)
        if (params.collectSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Ascending: C5, E5, G5 spread over 8 frames
            float chimeFreqs[] = {523.25f, 659.25f, 783.99f, 1046.5f};
            int idx = std::min(3, (msToFrames(160) - params.collectSoundFrames) / msToFrames(40));
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        static_cast<double>(chimeFreqs[idx]), 0.5, 90);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.collectSoundFrames > 0)
                    audioParams.collectSoundFrames--;
            }
        }
        
        // Layer 8: Miss aim sound (correct character, wrong aim — descending tone)
        if (params.missAimSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float freq = 600.0f - 80.0f * (5 - params.missAimSoundFrames);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        static_cast<double>(freq), 0.5, 70);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.missAimSoundFrames > 0)
                    audioParams.missAimSoundFrames--;
            }
        }
        
        // Layer 9: Miss morse sound (wrong character — low buzz)
        if (params.missMorseSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        200.0, 0.5, 80);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.missMorseSoundFrames > 0)
                    audioParams.missMorseSoundFrames--;
            }
        }
        
        // Layer 10: Menu navigation beep (short click)
        if (params.menuNavSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        1000.0, 0.5, 50);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.menuNavSoundFrames > 0)
                    audioParams.menuNavSoundFrames--;
            }
        }
        
        // Layer 11: Menu selection confirmation (higher pitched)
        if (params.menuSelectSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        1200.0, 0.5, 60);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.menuSelectSoundFrames > 0)
                    audioParams.menuSelectSoundFrames--;
            }
        }
        
        // Layer 11b: Keyboard/text input click (short high tick)
        if (params.keyClickSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        1800.0, 0.5, 50);
            // Short envelope: only first 30% audible (click)
            size_t fadeStart = static_cast<size_t>(bufSize * 0.3);
            for (size_t i = fadeStart; i < bufSize; i++) mixBuffer[i] = 0;
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.keyClickSoundFrames > 0)
                    audioParams.keyClickSoundFrames--;
            }
        }
        
        // Layer 12: PA damage crackle/pop (harsh noise burst on damage event)
        if (params.paDamageSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Harsh crackling: alternating high/low freq bursts
            float freq = (params.paDamageSoundFrames % 2 == 0) ? 100.0f : 2000.0f;
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        static_cast<double>(freq), 0.5, 120);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.paDamageSoundFrames > 0)
                    audioParams.paDamageSoundFrames--;
            }
        }
        
        // Layer 13: Continuous PA distortion overlay (damaged PA adds noise to all audio)
        if (isPlaying && params.paDamageLevel > 0.1f) {
            // Damage creates intermittent crackle on the motor/output sound
            // More damage = more frequent and louder crackle
            float damageIntensity = (params.paDamageLevel - 0.1f) / 0.9f;  // 0..1
            int distortionVol = static_cast<int>(30 * damageIntensity);
            // Random-ish crackle: use frame count to create pseudo-random pattern
            if ((frameCount % std::max(1, static_cast<int>((20 - 18 * damageIntensity) * FRAME_SCALE))) == 0) {
                std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
                // Harsh sawtooth burst at random-ish frequency
                float crackleFreq = 80.0f + (frameCount % 7) * 200.0f;
                audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                            static_cast<double>(crackleFreq), 0.5, distortionVol);
                for (size_t i = 0; i < bufSize; i++) {
                    int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                    audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
                }
            }
        }
        
        // Layer 14: PA repair chime (ascending warm tones when morse collection heals PA)
        if (params.paRepairSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Warm ascending: G4→B4→D5→G5 spread over 8 frames
            float repairFreqs[] = {392.0f, 493.9f, 587.3f, 784.0f};
            int idx = std::min(3, (msToFrames(160) - params.paRepairSoundFrames) / msToFrames(40));
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        static_cast<double>(repairFreqs[idx]), 0.5, 70);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.paRepairSoundFrames > 0)
                    audioParams.paRepairSoundFrames--;
            }
        }
        
        // Layer 15: Pause sound (descending two-tone: C5→G4)
        if (params.pauseSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float freq = (params.pauseSoundFrames > msToFrames(60)) ? 523.25f : 392.0f;
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        static_cast<double>(freq), 0.5, 80);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.pauseSoundFrames > 0)
                    audioParams.pauseSoundFrames--;
            }
        }
        
        // Layer 16: Unpause/resume sound (ascending two-tone: G4→C5)
        if (params.unpauseSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float freq = (params.unpauseSoundFrames > msToFrames(60)) ? 392.0f : 523.25f;
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        static_cast<double>(freq), 0.5, 80);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.unpauseSoundFrames > 0)
                    audioParams.unpauseSoundFrames--;
            }
        }
        
        // Layer 17: Traffic report whistle/beep (3-tone ascending fanfare)
        if (params.trafficBeepFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Classic radio traffic jingle: 3 ascending tones
            float trafficFreqs[] = {880.0f, 1100.0f, 1320.0f};
            int phase = (msToFrames(300) - params.trafficBeepFrames) / msToFrames(100);
            phase = std::clamp(phase, 0, 2);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        static_cast<double>(trafficFreqs[phase]), 0.5, 90);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.trafficBeepFrames > 0)
                    audioParams.trafficBeepFrames--;
            }
        }
        
        // Layer 18: Status readout start chime (ascending E5→A5→C#6)
        if (params.statusStartSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float startFreqs[] = {659.25f, 880.0f, 1108.73f};
            int idx = std::min(2, (msToFrames(120) - params.statusStartSoundFrames) / msToFrames(40));
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        static_cast<double>(startFreqs[idx]), 0.5, 70);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.statusStartSoundFrames > 0)
                    audioParams.statusStartSoundFrames--;
            }
        }
        
        // Layer 19: Status readout done chime (single high ding: E6)
        if (params.statusDoneSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        1318.51, 0.5, 60);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                if (audioParams.statusDoneSoundFrames > 0)
                    audioParams.statusDoneSoundFrames--;
            }
        }
        
        // Layer 20: Per-target-type aim lock indicators
        // Each target type gets a distinct audio cue so the player can identify
        // what they're aiming at even when multiple targets overlap.
        //   Morse signals:  Geiger-counter clicks (sawtooth, 2500-3500 Hz)
        //   Noise enemies:  Pulsing low buzz (square wave, 400-800 Hz)
        //   QSO Störer:     Alarm-like rapid pulse (triangle, 1000-1500 Hz)
        //   Power-ups:      Shimmering chime (sine, 800-1200 Hz)
        if (isPlaying) {
            // 20a: Morse aim lock — scratchy Geiger clicks (original behavior)
            if (params.aimLockMorse > 0.01f) {
                float strength = params.aimLockMorse;
                int clickInterval = std::max(1, static_cast<int>(12.0f - 11.0f * strength));
                if (frameCount % clickInterval == 0) {
                    std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
                    float aimFreq = 2500.0f + 1000.0f * strength;
                    int aimVol = static_cast<int>(15 + 45 * strength);
                    audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                                static_cast<double>(aimFreq), 0.5, aimVol);
                    size_t fadeStart = static_cast<size_t>(bufSize * 0.4);
                    for (size_t i = fadeStart; i < bufSize; i++) mixBuffer[i] = 0;
                    for (size_t i = 0; i < bufSize; i++) {
                        int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                        audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
                    }
                }
            }
            // 20b: Noise enemy aim lock — pulsing low buzz (square wave)
            if (params.aimLockNoise > 0.01f) {
                float strength = params.aimLockNoise;
                int clickInterval = std::max(1, static_cast<int>(10.0f - 9.0f * strength));
                if (frameCount % clickInterval == 0) {
                    std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
                    float noiseAimFreq = 400.0f + 400.0f * strength;
                    int noiseAimVol = static_cast<int>(20 + 40 * strength);
                    audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 2,
                                                static_cast<double>(noiseAimFreq), 0.5, noiseAimVol);
                    size_t fadeStart = static_cast<size_t>(bufSize * 0.5);
                    for (size_t i = fadeStart; i < bufSize; i++) mixBuffer[i] = 0;
                    for (size_t i = 0; i < bufSize; i++) {
                        int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                        audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
                    }
                }
            }
            // 20c: QSO Störer aim lock — alarm-like rapid pulse (triangle wave)
            if (params.aimLockStoerer > 0.01f) {
                float strength = params.aimLockStoerer;
                int clickInterval = std::max(1, static_cast<int>(8.0f - 7.0f * strength));
                if (frameCount % clickInterval == 0) {
                    std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
                    float stoererAimFreq = 1000.0f + 500.0f * strength;
                    int stoererAimVol = static_cast<int>(20 + 50 * strength);
                    audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 3,
                                                static_cast<double>(stoererAimFreq), 0.5, stoererAimVol);
                    size_t fadeStart = static_cast<size_t>(bufSize * 0.35);
                    for (size_t i = fadeStart; i < bufSize; i++) mixBuffer[i] = 0;
                    for (size_t i = 0; i < bufSize; i++) {
                        int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                        audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
                    }
                }
            }
            // 20d: Power-up aim lock — shimmering chime (sine wave)
            if (params.aimLockPowerUp > 0.01f) {
                float strength = params.aimLockPowerUp;
                int clickInterval = std::max(1, static_cast<int>(10.0f - 9.0f * strength));
                if (frameCount % clickInterval == 0) {
                    std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
                    float puAimFreq = 800.0f + 400.0f * strength;
                    int puAimVol = static_cast<int>(15 + 40 * strength);
                    audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                                static_cast<double>(puAimFreq), 0.5, puAimVol);
                    size_t fadeStart = static_cast<size_t>(bufSize * 0.6);
                    for (size_t i = fadeStart; i < bufSize; i++) mixBuffer[i] = 0;
                    for (size_t i = 0; i < bufSize; i++) {
                        int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                        audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
                    }
                }
            }
        }
        
        // Layer 21: Noise enemy ambient noise (static/hum from nearby enemies)
        // Narrow stereo field when aimLockStrength > 0.8 (focused aiming)
        float stereoNarrow = (params.aimLockStrength > 0.8f) ? (1.0f - (params.aimLockStrength - 0.8f) * STEREO_NARROW_RATE) : 1.0f;
        stereoNarrow = std::max(0.3f, stereoNarrow);
        for (const auto& ne : params.noiseEnemies) {
            if (ne.volume > 0) {
                std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
                // Random-ish static noise using rapidly alternating frequencies
                float noiseFreq = 60.0f + (frameCount % 5) * 40.0f + ne.intensity * 200.0f;
                float narrowedPan = 0.5f + (ne.pan - 0.5f) * stereoNarrow;
                audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                            static_cast<double>(noiseFreq), narrowedPan, ne.volume);
                for (size_t i = 0; i < bufSize; i++) {
                    int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                    audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
                }
            }
        }
        
        // Layer 22: Weapon switch click sound (short mechanical click)
        if (params.weaponSwitchSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        800.0, 0.5, 50);
            size_t clickEnd = static_cast<size_t>(bufSize * 0.3);
            for (size_t i = clickEnd; i < bufSize; i++) mixBuffer[i] = 0;
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.weaponSwitchSoundFrames > 0) audioParams.weaponSwitchSoundFrames--; }
        }
        
        // Layer 23: Weapon equip sound (unique per weapon)
        if (params.weaponEquipSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float eqFreq = (params.equippedWeaponType == WeaponType::NOISE_BLANKER) ? 1500.0f : 600.0f;
            int curve = (params.equippedWeaponType == WeaponType::NOISE_BLANKER) ? 3 : 2;
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, curve,
                                        static_cast<double>(eqFreq), 0.5, 70);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.weaponEquipSoundFrames > 0) audioParams.weaponEquipSoundFrames--; }
        }
        
        // Layer 24: Noise blanker fire — laser-like descending zap
        if (params.noiseBlankerFireFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Descending frequency sweep: 3000→500 Hz over 8 frames
            float zapFreq = 3000.0f - 312.5f * (8 - params.noiseBlankerFireFrames);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 3,
                                        static_cast<double>(zapFreq), 0.5, 100);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.noiseBlankerFireFrames > 0) audioParams.noiseBlankerFireFrames--; }
        }
        
        // Layer 25: Noise enemy destroyed explosion (burst of wide-band noise)
        if (params.noiseDestroyedFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float explFreq = 150.0f + (10 - params.noiseDestroyedFrames) * 80.0f;
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        static_cast<double>(explFreq), 0.5, 160);  // vol 160 (was 90)
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.noiseDestroyedFrames > 0) audioParams.noiseDestroyedFrames--; }
        }
        
        // Layer 25b: Noise enemy HIT sound (metallic impact with pitch variation)
        if (params.noiseHitSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Metallic clang: square wave for sharp attack, base freq varies with hit
            float hitBaseFreq = 1000.0f + params.noiseHitVariation * 300.0f;
            // Rising frequency for each subsequent hit (more urgent as enemy weakens)
            float hitFreq = hitBaseFreq + (8 - params.noiseHitSoundFrames) * 200.0f;
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 2,  // square wave (sharp, distinct)
                                        static_cast<double>(hitFreq), 0.5, 150);  // vol 150 (was 80)
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.noiseHitSoundFrames > 0) audioParams.noiseHitSoundFrames--; }
        }
        
        // Layer 26: Emergency brake tire screech (descending noise + resonant squeal)
        if (params.emergencyBrakeSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Screech: descending sawtooth simulating tire squeal
            float screechFreq = 1500.0f + 400.0f * FRAME_SCALE * params.emergencyBrakeSoundFrames;
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        static_cast<double>(screechFreq), 0.5, 160);
            // Add noise component for tire-on-asphalt realism
            unsigned int noiseSeed = 54321 + params.emergencyBrakeSoundFrames * 7;
            for (size_t i = 0; i < bufSize; i++) {
                noiseSeed = noiseSeed * 1103515245 + 12345;
                int16_t noise = static_cast<int16_t>((noiseSeed >> 16) & 0x7F) - 64;
                mixBuffer[i] = static_cast<int16_t>(std::clamp(
                    static_cast<int32_t>(mixBuffer[i]) + noise * 3, (int32_t)-32768, (int32_t)32767));
            }
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.emergencyBrakeSoundFrames > 0) audioParams.emergencyBrakeSoundFrames--; }
        }
        
        // Layer 27: Aim reset swoosh (descending sine sweep for turret re-center)
        if (params.aimResetSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Quick descending tone: 1200 Hz down to 400 Hz over 120ms
            float swooshFreq = 400.0f + params.aimResetSoundFrames * 150.0f * FRAME_SCALE;
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 0,  // sine
                                        static_cast<double>(swooshFreq), 0.5, 100);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.aimResetSoundFrames > 0) audioParams.aimResetSoundFrames--; }
        }
        
        // Layer 28: Border warning beep (100Hz sawtooth, panned to side)
        // Volume scales with warning intensity for graduated feedback
        if (params.borderWarningSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            int warnVol = static_cast<int>((40 + params.borderWarningIntensity * 80) * params.warningVolume);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        100.0, static_cast<double>(params.borderWarningSide), warnVol);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.borderWarningSoundFrames > 0) audioParams.borderWarningSoundFrames--; }
        }
        
        // Layer 29: Border scrape (800-1200Hz alternating sawtooth, panned to collision side)
        if (params.borderScrapeSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float scrapeFreq = (frameCount % 2 == 0) ? 800.0f : 1200.0f;
            int scrapeVol = static_cast<int>(30 * params.collisionVolume);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        static_cast<double>(scrapeFreq),
                                        static_cast<double>(params.borderCollisionSide), scrapeVol);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.borderScrapeSoundFrames > 0) audioParams.borderScrapeSoundFrames--; }
        }
        
        // Layer 30: Border crash (80Hz kick + noise burst, panned to collision side)
        if (params.borderCrashSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float crashFreq = 80.0f + (params.borderCrashSoundFrames > 2400 ? 200.0f : 0.0f);
            int crashVol = static_cast<int>(50 * params.collisionVolume);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        static_cast<double>(crashFreq),
                                        static_cast<double>(params.borderCollisionSide), crashVol);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.borderCrashSoundFrames > 0) audioParams.borderCrashSoundFrames--; }
        }
        
        // Layer 31: QSO Störer hornet buzz (180-280Hz modulated sawtooth)
        // Psychoacoustic front/back: when behind player, apply simple low-pass
        // filter to simulate head-shadow effect (high frequencies attenuated from behind)
        if (isPlaying && params.qsoStoererActive && params.qsoStoererVolume > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        static_cast<double>(params.qsoStoererBuzzFreq),
                                        static_cast<double>(0.5f + params.qsoStoererPan * 0.5f),
                                        params.qsoStoererVolume);
            // Apply psychoacoustic low-pass when Störer is behind the player
            // This simulates the head-shadow effect for binaural front/back distinction
            if (params.qsoStoererBehind) {
                // Simple IIR low-pass filter (one-pole): y[n] = alpha * x[n] + (1-alpha) * y[n-1]
                // Lower alpha = more filtering. Behind = muffled, losing high frequencies.
                int16_t prevL = 0, prevR = 0;
                for (int i = 0; i < GAME_AUDIO_SAMPLES; i++) {
                    int idx = i * GAME_CHANNELS;
                    int16_t filtL = static_cast<int16_t>(PSYCHOACOUSTIC_LP_ALPHA * mixBuffer[idx] + (1.0f - PSYCHOACOUSTIC_LP_ALPHA) * prevL);
                    prevL = filtL;
                    mixBuffer[idx] = filtL;
                    if (GAME_CHANNELS > 1) {
                        int16_t filtR = static_cast<int16_t>(PSYCHOACOUSTIC_LP_ALPHA * mixBuffer[idx+1] + (1.0f - PSYCHOACOUSTIC_LP_ALPHA) * prevR);
                        prevR = filtR;
                        mixBuffer[idx+1] = filtR;
                    }
                }
            }
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
        }
        
        // Layer 32: QSO Störer collision impact (descending thud)
        if (params.qsoStoererCollisionFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float impactFreq = 120.0f + params.qsoStoererCollisionFrames * 0.05f * FRAME_SCALE;
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        static_cast<double>(impactFreq), 0.5, 90);
            size_t fadeStart = static_cast<size_t>(bufSize * 0.6);
            for (size_t i = fadeStart; i < bufSize; i++) {
                float fade = 1.0f - static_cast<float>(i - fadeStart) / static_cast<float>(bufSize - fadeStart);
                mixBuffer[i] = static_cast<int16_t>(mixBuffer[i] * fade);
            }
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.qsoStoererCollisionFrames > 0) audioParams.qsoStoererCollisionFrames--; }
        }
        
        // Layer 33: QSO Störer overtake sweep (psychoacoustic front→behind transition)
        // A descending frequency sweep panned from front to back, simulating the Doppler
        // effect of passing a sound source. Combined with low-pass onset for behind cue.
        if (params.qsoStoererOvertakeFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Sweep from high to low frequency (Doppler-like)
            float progress = 1.0f - static_cast<float>(params.qsoStoererOvertakeFrames) / static_cast<float>(msToFrames(300));
            float sweepFreq = 400.0f - 200.0f * progress;  // 400 → 200 Hz
            // Pan sweeps from center-right to center-left (passing effect)
            float sweepPan = 0.7f - 0.4f * progress;  // 0.7 → 0.3
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        static_cast<double>(sweepFreq), static_cast<double>(sweepPan), 60);
            // Apply progressive low-pass as sound moves "behind" (second half)
            if (progress > 0.5f) {
                float lpAlpha = 0.5f - 0.3f * (progress - 0.5f) * 2.0f;  // 0.5 → 0.2
                int16_t prev = 0;
                for (size_t i = 0; i < bufSize; i++) {
                    int16_t filt = static_cast<int16_t>(lpAlpha * mixBuffer[i] + (1.0f - lpAlpha) * prev);
                    prev = filt;
                    mixBuffer[i] = filt;
                }
            }
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.qsoStoererOvertakeFrames > 0) audioParams.qsoStoererOvertakeFrames--; }
        }
        
        // Layer 34: Band crossing jingle (ascending = entering band, descending = leaving)
        // A short 3-tone arpeggio that plays before the TTS band announcement
        // 8 frames × 20ms per frame = 160ms total jingle duration
        static constexpr int BAND_JINGLE_TOTAL_FRAMES = msToFrames(160);
        if (params.bandJingleFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float jingleFreqs[] = {523.25f, 659.25f, 783.99f, 1046.5f};  // C5, E5, G5, C6
            int idx = std::min(3, (BAND_JINGLE_TOTAL_FRAMES - params.bandJingleFrames) / msToFrames(40));
            float freq;
            if (params.bandJingleAscending) {
                freq = jingleFreqs[idx];  // C5 → E5 → G5 → C6 (entering)
            } else {
                freq = jingleFreqs[3 - idx];  // C6 → G5 → E5 → C5 (leaving)
            }
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        static_cast<double>(freq), 0.5,
                                        static_cast<int>(70 * params.uiVolume));
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.bandJingleFrames > 0) audioParams.bandJingleFrames--; }
        }
        
        // Layer 35: Power-up zone ambient (ascending positive tone sequence, repeating)
        // Each power-up zone generates a shimmering ascending arpeggio
        // puZonePhase is local to the audio thread (single-threaded access only)
        {
            static int puZonePhase = 0;
            std::lock_guard<std::mutex> lock(audioStateMtx);
            bool anyActive = false;
            for (const auto& pz : params.powerUpZones) {
                if (pz.volume <= 0) continue;
                anyActive = true;
                std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
                // Distinct timbre per power-up type to disambiguate targets in the crosshair
                int wave = 1; // sine default
                float baseFreq = 523.25f;
                switch (pz.type) {
                    case PowerUpType::SPEED_BOOST:   wave = 1; baseFreq = 523.25f; break; // bright sine arpeggio
                    case PowerUpType::FIRE_RATE:     wave = 2; baseFreq = 880.0f;  break; // square, urgent
                    case PowerUpType::AUTO_FIRE:     wave = 3; baseFreq = 660.0f;  break; // triangle, steady
                    case PowerUpType::SWR_IMMUNITY:  wave = 4; baseFreq = 440.0f;  break; // saw, gritty
                    case PowerUpType::DURATION_EXTEND: wave = 1; baseFreq = 392.0f; break; // warm lower
                    default: break;
                }
                float puFreqs[] = {baseFreq, baseFreq * 1.25f, baseFreq * 1.5f, baseFreq * 2.0f};
                float freq = puFreqs[(puZonePhase / 4) % 4];
                audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, wave,
                                            static_cast<double>(freq), pz.pan,
                                            pz.volume);
                for (size_t i = 0; i < bufSize; i++) {
                    int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                    audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
                }
            }
            if (anyActive) puZonePhase++;
        }
        
        // Layer 36: Power-up collection progress (ascending tone proportional to progress)
        if (params.powerUpCollecting && params.powerUpCollectProgress > 0.0f) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Rising tone: 300 Hz at start → 1200 Hz at completion
            float collectFreq = 300.0f + 900.0f * params.powerUpCollectProgress;
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        static_cast<double>(collectFreq), 0.5,
                                        static_cast<int>(60 * params.uiVolume));
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
        }
        
        // Layer 36b: Power-up miss cue (short descending buzz)
        if (params.powerUpMissSoundFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float freq = 500.0f - 70.0f * (4 - params.powerUpMissSoundFrames);
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 4,
                                        static_cast<double>(freq), 0.5, 70);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock(audioStateMtx);
              if (audioParams.powerUpMissSoundFrames > 0) audioParams.powerUpMissSoundFrames--; }
        }
        
        // Layer 37: Power-up activation fanfare (unique per type)
        if (params.powerUpActivateFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float actFreq = 800.0f;
            float actPan = 0.5f;
            // Different tonal character per type
            switch (params.powerUpActivateType) {
                case PowerUpType::SPEED_BOOST: actFreq = 600.0f + (msToFrames(300) - params.powerUpActivateFrames) * 80.0f * FRAME_SCALE; break;  // Rising whoosh
                case PowerUpType::FIRE_RATE: actFreq = 1000.0f + std::sin(params.powerUpActivateFrames * 0.8f * FRAME_SCALE) * 300.0f; break;  // Rapid oscillation
                case PowerUpType::AUTO_FIRE: actFreq = 400.0f + (msToFrames(300) - params.powerUpActivateFrames) * 60.0f * FRAME_SCALE; break;  // Military rising
                case PowerUpType::SWR_IMMUNITY: actFreq = 700.0f; break;  // Steady protective hum
                case PowerUpType::DURATION_EXTEND: actFreq = 523.25f * std::pow(2.0f, static_cast<float>(msToFrames(300) - params.powerUpActivateFrames) / static_cast<float>(msToFrames(300))); break;  // Octave sweep
                default: break;
            }
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        static_cast<double>(actFreq), actPan,
                                        static_cast<int>(80 * params.uiVolume));
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock2(audioStateMtx);
              if (audioParams.powerUpActivateFrames > 0) audioParams.powerUpActivateFrames--; }
        }
        
        // Layer 38: Power-up expiration (descending deflating tone)
        if (params.powerUpExpireFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            float expireFreq = 800.0f * (static_cast<float>(params.powerUpExpireFrames) / static_cast<float>(msToFrames(200)));  // Descending from 800 to ~80
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 1,
                                        static_cast<double>(std::max(80.0f, expireFreq)), 0.5,
                                        static_cast<int>(60 * params.uiVolume));
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock2(audioStateMtx);
              if (audioParams.powerUpExpireFrames > 0) audioParams.powerUpExpireFrames--; }
        }
        
        // Layer 39: Power-up explosion (wide-band noise burst with intensity)
        if (params.powerUpExplodeFrames > 0) {
            std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
            // Explosive noise: mix of low kick + white noise
            float explodeFreq = 80.0f + (params.powerUpExplodeFrames > msToFrames(200) ? 0.0f : (msToFrames(200) - params.powerUpExplodeFrames) * 30.0f * FRAME_SCALE);
            int explodeVol = static_cast<int>(100 * params.powerUpExplodeIntensity * params.collisionVolume);
            explodeVol = std::max(explodeVol, 30);  // Always audible
            audioEngine->generateAudio(mixBuffer, GAME_AUDIO_SAMPLES, 3,  // Waveform 3 = noise-like
                                        static_cast<double>(explodeFreq), params.powerUpExplodePan,
                                        explodeVol);
            for (size_t i = 0; i < bufSize; i++) {
                int32_t mixed = static_cast<int32_t>(audioBuffer[i]) + static_cast<int32_t>(mixBuffer[i]);
                audioBuffer[i] = static_cast<int16_t>(std::clamp(mixed, (int32_t)-32768, (int32_t)32767));
            }
            { std::lock_guard<std::mutex> lock2(audioStateMtx);
              if (audioParams.powerUpExplodeFrames > 0) audioParams.powerUpExplodeFrames--; }
        }
        
        } catch (const std::exception& e) {
            log("HAMSPIRIT_AUDIO", "Exception in UI sound layers: " + std::string(e.what()));
        } catch (...) {
            log("HAMSPIRIT_AUDIO", "Unknown exception in UI sound layers");
        }
        
        // Title melody layer (plays during MAIN_MENU / PAUSED / INTRO, silent during PLAYING)
        try {
            if (titleMelody && titleMelody->isPlaying()) {
                titleMelody->renderFrame(audioBuffer, GAME_AUDIO_SAMPLES,
                                         GAME_SAMPLE_RATE, audioEngine.get(), mixBuffer);
            }
        } catch (const std::exception& e) {
            log("HAMSPIRIT_AUDIO", "Exception in title melody: " + std::string(e.what()));
        } catch (...) {
            log("HAMSPIRIT_AUDIO", "Unknown exception in title melody");
        }
        
        // Multiplayer spatial audio: mix other players' engine sounds, Morse cannon
        // sidetones, and collision sounds into player 0's audio buffer.
        // Each sound is spatialized based on the relative position between
        // player 0 (listener) and the source player (distance, bearing, Doppler).
        if (isPlaying && multiplayerMgr && multiplayerMgr->isMultiplayer()) {
            try {
                int mpCount = multiplayerMgr->getPlayerCount();
                for (int src = 1; src < mpCount; src++) {
                    auto rel = multiplayerMgr->calculateSpatialRelation(0, src);
                    const auto* srcCtx = multiplayerMgr->getPlayer(src);
                    if (!srcCtx || rel.volume < 0.001f) continue;

                    // Other player's engine sound (with Doppler)
                    if (srcCtx->playerSpeed > 0.001f) {
                        renderOtherPlayerEngine(audioBuffer, GAME_AUDIO_SAMPLES,
                                                GAME_SAMPLE_RATE, rel,
                                                srcCtx->playerSpeed, MOTOR_BASE_FREQ,
                                                src);
                    }

                    // Other player's Morse cannon sidetone
                    // 6.4/6.5: Uses morseCannonTimestamp for sample-accurate start
                    // when the other player just started keying within this frame.
                    if (srcCtx->morseCannonActive) {
                        float morseFreq = MORSE_SIDETONE_FREQ;
                        float dopplerMorse = SpatialPlayerAudio::applyDoppler(morseFreq, rel.dopplerFactor);

                        float angle = (rel.pan + 1.0f) * 0.25f * static_cast<float>(M_PI);
                        float leftGain  = rel.volume * std::cos(angle) * 0.4f;
                        float rightGain = rel.volume * std::sin(angle) * 0.4f;

                        float invSR = 1.0f / static_cast<float>(GAME_SAMPLE_RATE);
                        // Phase accumulators per source player, only accessed from audio thread
                        static thread_local float morsePhaseP0[MAX_PLAYERS] = {};
                        float& mPhase = morsePhaseP0[src];
                        
                        // Calculate sample offset for sub-frame start using timestamp
                        int startSample = 0;
                        if (srcCtx->morseCannonTimestamp != FeedbackTimePoint{}) {
                            startSample = calculateSampleOffset(
                                srcCtx->morseCannonTimestamp, audioFrameStartTime,
                                GAME_SAMPLE_RATE, GAME_AUDIO_SAMPLES);
                        }

                        for (int i = 0; i < GAME_AUDIO_SAMPLES; i++) {
                            if (i < startSample) continue;  // Silence before event
                            mPhase += dopplerMorse * invSR;
                            if (mPhase >= 1.0f) mPhase -= 1.0f;
                            float sample = (mPhase < 0.5f) ? 1.0f : -1.0f;

                            int idx = i * 2;
                            int32_t mixL = static_cast<int32_t>(audioBuffer[idx])     + static_cast<int16_t>(sample * leftGain  * 6000.0f);
                            int32_t mixR = static_cast<int32_t>(audioBuffer[idx + 1]) + static_cast<int16_t>(sample * rightGain * 6000.0f);
                            audioBuffer[idx]     = static_cast<int16_t>(std::clamp(mixL, (int32_t)-32768, (int32_t)32767));
                            audioBuffer[idx + 1] = static_cast<int16_t>(std::clamp(mixR, (int32_t)-32768, (int32_t)32767));
                        }
                    }

                    // Other player's collision sound
                    if (srcCtx->collisionSoundFrames > 0) {
                        // Render collision at the spatial position of the other player
                        float spatialPan = rel.pan;
                        float spatialIntensity = srcCtx->collisionIntensity * rel.volume;
                        // Use a local copy of frames so we don't decrement the source's counter
                        int localFrames = std::min(srcCtx->collisionSoundFrames, GAME_AUDIO_SAMPLES);
                        renderCollisionSound(audioBuffer, GAME_AUDIO_SAMPLES,
                                             GAME_SAMPLE_RATE, spatialIntensity,
                                             spatialPan, localFrames);
                    }
                }
            } catch (...) {
                // Don't let multiplayer audio errors crash the audio thread
            }
        }
        
        // Play the mixed buffer
        if (!audioBackend->playBuffer(audioBuffer.data(), GAME_AUDIO_SAMPLES,
                                      GAME_SAMPLE_RATE, GAME_CHANNELS, GAME_BITS)) {
            failCount++;
            consecutiveFailCount++;
            if (failCount <= 5 || failCount % 100 == 0) {
                log("HAMSPIRIT_AUDIO", "playBuffer failed (count: " + std::to_string(failCount) +
                    ", consecutive: " + std::to_string(consecutiveFailCount) + ")");
            }
            // After many consecutive failures, attempt backend recovery by
            // resetting the abort flag.  This handles the case where a prior
            // abort was signalled and never properly cleared (e.g. race during
            // pause/unpause transitions), leaving playBuffer permanently refusing data.
            // IMPORTANT: Never resetAbort if shutdown is in progress (audioRunning=false),
            // otherwise we undo the abort that shutdownAudio() set to unblock us,
            // causing audioThread.join() to hang forever.
            if (consecutiveFailCount >= AUDIO_CONSECUTIVE_FAIL_RECOVERY && audioRunning.load()) {
                log("HAMSPIRIT_AUDIO", "Attempting audio backend recovery after " +
                    std::to_string(consecutiveFailCount) + " consecutive failures");
                audioBackend->resetAbort();
                // If backend is persistently failing (e.g. after Alt+Tab), try re-initializing it
                if (consecutiveFailCount >= AUDIO_CONSECUTIVE_FAIL_RECOVERY * 3) {
                    log("HAMSPIRIT_AUDIO", "Extended failure — attempting backend reinitialization");
                    try {
                        audioBackend->initialize();
                    } catch (...) {
                        log("HAMSPIRIT_AUDIO", "Backend reinitialization threw exception");
                    }
                }
                consecutiveFailCount = 0;
            }
            // Sleep to prevent busy-loop when playBuffer consistently fails
            std::this_thread::sleep_for(std::chrono::milliseconds(GAME_AUDIO_FRAME_MS));
        } else {
            consecutiveFailCount = 0;
            // Per-player independent audio for multiplayer:
            // Each player gets their own fully rendered audio stream from their
            // own perspective (their own AudioParams computed in the game loop).
            if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
                try {
                    multiplayerMgr->renderAllPlayerAudio(
                        audioEngine.get(), GAME_SAMPLE_RATE,
                        GAME_AUDIO_SAMPLES, GAME_CHANNELS, GAME_BITS,
                        globalMorsePatternPhase, audioFrameStartTime,
                        isPlaying);
                } catch (...) {
                    // Don't let secondary audio output errors affect primary audio
                }
            }
        }
    }
    } catch (const std::exception& e) {
        crashCount++;
        log("HAMSPIRIT_AUDIO", "EXCEPTION in audio thread (crash #" + std::to_string(crashCount) + 
            "): " + std::string(e.what()));
        consecutiveFailCount = 0;
        // Only resetAbort if we're still supposed to be running
        if (audioBackend && audioRunning.load()) audioBackend->resetAbort();
        std::fill(audioBuffer.begin(), audioBuffer.end(), 0);
        std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (crashCount > MAX_AUDIO_CRASH_RECOVERY) {
            log("HAMSPIRIT_AUDIO", "Too many audio crashes (" + std::to_string(crashCount) + "), giving up");
            break;
        }
        continue;
    } catch (...) {
        crashCount++;
        log("HAMSPIRIT_AUDIO", "UNKNOWN EXCEPTION in audio thread (crash #" + std::to_string(crashCount) + ")");
        consecutiveFailCount = 0;
        if (audioBackend && audioRunning.load()) audioBackend->resetAbort();
        std::fill(audioBuffer.begin(), audioBuffer.end(), 0);
        std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (crashCount > MAX_AUDIO_CRASH_RECOVERY) break;
        continue;
    }
    break;  // Normal exit (audioRunning == false)
    } // end self-healing outer loop
    
    log("HAMSPIRIT", "Audio thread stopped (frames: " + std::to_string(frameCount) + 
        ", failures: " + std::to_string(failCount) + ", crashes: " + std::to_string(crashCount) + ")");
    audioThreadExited.store(true);
}

// generateAndPlayAudio is now handled by audioThreadFunc
void Game::generateAndPlayAudio() {
    // No-op: audio runs on its own thread now
}

// Get current SWR at player position
float Game::getCurrentSWR() const {
    if (track.empty()) {
        return 1.0f;
    }
    
    // In multiplayer, player 0's tuner adjustments go to ctx0->antennaNetwork.
    // Use the per-player instance so SWR queries match the actual tuning state.
    const AntennaNetwork* effective = antennaNetwork.get();
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        auto* ctx0 = multiplayerMgr->getPlayer(0);
        if (ctx0 && ctx0->antennaNetwork)
            effective = ctx0->antennaNetwork.get();
    }

    // If antenna network exists, use adjusted SWR
    if (effective) {
        return effective->calculateAdjustedSWR(track, playerAngle);
    }
    
    // Otherwise use raw SWR from track (clamped to game limits)
    TrackPoint currentPoint = TrackGenerator::interpolateAt(track, playerAngle);
    return std::max(kMinSWR, std::min(kMaxSWR, currentPoint.swr));
}

// Check PA health based on SWR
void Game::checkPAHealth(float dt) {
    float currentSWR = getCurrentSWR();
    if (!std::isfinite(currentSWR)) {
        if (tryLogMathWarning(sSWRMathWarningCount)) {
            logHamSpiritMath("Current SWR not finite; clamping to max.");
        }
        currentSWR = kMaxSWR;
    }
    if (currentSWR < kMinSWR) {
        if (tryLogMathWarning(sSWRMathWarningCount)) {
            logHamSpiritMath("Current SWR below minimum; clamping to 1.0.");
        }
        currentSWR = kMinSWR;
    } else if (currentSWR > kMaxSWR) {
        if (tryLogMathWarning(sSWRMathWarningCount)) {
            logHamSpiritMath("Current SWR above maximum; clamping to max.");
        }
        currentSWR = kMaxSWR;
    }
    
    // Update average SWR
    stats.averageSWR = stats.averageSWR * PA_SWR_AVERAGE_ALPHA + currentSWR * (1.0f - PA_SWR_AVERAGE_ALPHA);
    
    // Clamp dt to avoid huge damage spikes from frame time accumulation
    float clampedDt = std::min(dt, PA_DT_CLAMP);  // Max 100ms per tick
    
    // Speed represents transmitted power: no speed = no power = no PA damage
    float speedFactor = (maxSpeed > 0.0f) ? std::abs(playerSpeed) / maxSpeed : 0.0f;
    speedFactor = std::min(speedFactor, 1.0f);
    
    // === REALISTIC PA DAMAGE MODEL ===
    // Based on reflected power: P_reflected = P_forward * ((SWR-1)/(SWR+1))^2
    // PA transistors are rated for a max reflected power over time.
    // At SWR 1.0: 0% reflected → no damage
    // At SWR 2.0: 11% reflected → light stress
    // At SWR 3.0: 25% reflected → moderate stress  
    // At SWR 5.0: 44% reflected → heavy stress
    // At SWR 10.0: 67% reflected → critical, rapid destruction
    
    float reflectionCoeff = (currentSWR > 1.0f) ? (currentSWR - 1.0f) / (currentSWR + 1.0f) : 0.0f;
    float reflectedPowerFraction = reflectionCoeff * reflectionCoeff;  // Gamma^2
    
    // Thermal model: PA heats up from reflected power, cools down passively
    const float thermalCapacity = PA_THERMAL_CAPACITY;    // Seconds to reach critical temp at full reflected power
    const float coolingRate = PA_COOLING_RATE;       // Passive cooling per second (slow — heatsink model)
    const float safeSWR = PA_SAFE_SWR;            // Below this, no significant thermal stress
    
    if (currentSWR > safeSWR && speedFactor > 0.01f && !swrImmunityActive) {
        // Heat accumulates from reflected power * transmitted power (speed)
        float heatInput = reflectedPowerFraction * speedFactor / thermalCapacity;
        paThermalLoad += heatInput * clampedDt;
        
        // Accumulated reflected energy (for permanent damage calculation)
        paReflectedPowerAccum += reflectedPowerFraction * speedFactor * clampedDt;
        
        // Permanent damage: PA health degrades based on thermal load exceeding safe zone
        // Only permanent damage when thermal load is above 0.3 (transistors start degrading)
        if (paThermalLoad > PA_DAMAGE_THERMAL_THRESHOLD) {
            float excessHeat = paThermalLoad - PA_DAMAGE_THERMAL_THRESHOLD;
            // Nonlinear: damage accelerates at higher temperatures (thermal runaway)
            float damageRate = PA_DAMAGE_RATE_FACTOR * excessHeat * excessHeat;
            if (!config.elemSwrDamage) damageRate = 0.0f;  // SWR damage disabled
            stats.paHealth -= damageRate * speedFactor * clampedDt;
        }
        
        // Log damage events
        static int damageLogCount = 0;
        if (damageLogCount < 10 && paThermalLoad > PA_DAMAGE_THERMAL_THRESHOLD) {
            log("HAMSPIRIT", "PA stress: SWR=" + std::to_string(currentSWR) + 
                " reflected=" + std::to_string(static_cast<int>(reflectedPowerFraction * 100)) + "%" +
                " thermal=" + std::to_string(paThermalLoad) +
                " health=" + std::to_string(stats.paHealth));
            damageLogCount++;
        }
    } else {
        // Cooling: thermal load decreases passively
        paThermalLoad -= coolingRate * clampedDt;
        if (paThermalLoad < 0.0f) paThermalLoad = 0.0f;
        
        // Slow health regeneration only when cool and SWR is good (PA recovers slightly)
        if (paThermalLoad < PA_REGEN_THERMAL_THRESHOLD && stats.paHealth > 0.0f && stats.paHealth < 1.0f) {
            stats.paHealth += PA_REGEN_RATE * clampedDt;  // Very slow — permanent damage is mostly permanent
            stats.paHealth = std::min(1.0f, stats.paHealth);
        }
    }
    
    // Clamp thermal load (can overshoot but cap to prevent infinity)
    paThermalLoad = std::min(paThermalLoad, PA_THERMAL_MAX);
    
    // === STAGED DAMAGE ANNOUNCEMENTS ===
    // Stage 0: 100-90% — healthy
    // Stage 1: 90-70% — light damage (slight performance loss)
    // Stage 2: 70-50% — moderate damage (noticeable performance loss)
    // Stage 3: 50-30% — heavy damage (significant performance loss + distortion)
    // Stage 4: 30-10% — critical (severe performance loss + heavy distortion)
    // Stage 5: 10-0% — failing (imminent destruction)
    int currentStage = 0;
    if (stats.paHealth <= PA_STAGE_5_THRESHOLD) currentStage = 5;
    else if (stats.paHealth <= PA_STAGE_4_THRESHOLD) currentStage = 4;
    else if (stats.paHealth <= PA_STAGE_3_THRESHOLD) currentStage = 3;
    else if (stats.paHealth <= PA_STAGE_2_THRESHOLD) currentStage = 2;
    else if (stats.paHealth <= PA_STAGE_1_THRESHOLD) currentStage = 1;
    
    if (currentStage > lastAnnouncedDamageStage) {
        lastAnnouncedDamageStage = currentStage;
        triggerPaDamageSound();
        
        if (tts && tts->isAvailable()) {
            int healthPct = static_cast<int>(stats.paHealth * 100.0f);
            switch (currentStage) {
                case 1:
                    tts->speak("PA: " + std::to_string(healthPct) + "%. Light damage.", false);
                    break;
                case 2:
                    tts->speak("PA: " + std::to_string(healthPct) + "%. Moderate damage. Reduce power.", false);
                    break;
                case 3:
                    tts->speak("Warning! PA: " + std::to_string(healthPct) + "%. Heavy damage. Match antenna!", false);
                    break;
                case 4:
                    tts->speak("Critical! PA: " + std::to_string(healthPct) + "%. Severe damage!", false);
                    break;
                case 5:
                    tts->speak("Emergency! PA failing! " + std::to_string(healthPct) + "%!", false);
                    break;
            }
        }
    }
    
    // === SPEED RESTRICTION FROM DAMAGE ===
    // Damaged PA can't deliver full power → reduced max speed
    // Apply as multiplier on current maxSpeed (which was already set by updateMaxSpeedFromMatching)
    // Stage 0: 100% speed, Stage 1: 90%, Stage 2: 75%, Stage 3: 55%, Stage 4: 35%, Stage 5: 15%
    float damageSpeedMultiplier = 1.0f;
    switch (currentStage) {
        case 1: damageSpeedMultiplier = PA_SPEED_MULT_STAGE_1; break;
        case 2: damageSpeedMultiplier = PA_SPEED_MULT_STAGE_2; break;
        case 3: damageSpeedMultiplier = PA_SPEED_MULT_STAGE_3; break;
        case 4: damageSpeedMultiplier = PA_SPEED_MULT_STAGE_4; break;
        case 5: damageSpeedMultiplier = PA_SPEED_MULT_STAGE_5; break;
    }
    // Apply to maxSpeed (derived from baseMaxSpeed * matchingFactor, set each frame)
    maxSpeed *= damageSpeedMultiplier;
    
    // Update audio damage level for distortion sounds
    {
        std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
        if (lock.owns_lock()) {
            audioParams.paDamageLevel = 1.0f - stats.paHealth;  // 0=healthy, 1=destroyed
        }
    }
    
    // Game over if PA completely fails
    if (stats.paHealth <= 0.0f) {
        stats.paHealth = 0.0f;
        setState(GameState::GAME_OVER);
    }
}

// Handle antenna network input (tuner and UnUn adjustments)
void Game::handleAntennaNetworkInput(const GamepadState& input, float dt) {
    if (!antennaNetwork) return;
    
    if (announceCooldown > 0.0f) announceCooldown -= dt;
    if (tunerSoundCooldown > 0.0f) tunerSoundCooldown -= dt;
    
    // Face buttons for L/C tuning with debounced hold-to-repeat:
    // First press: immediate action. Hold: delay then repeat.
    // Y (North) = Increase L, X (West) = Decrease L
    // B (East) = Increase C, A (South) = Decrease C
    bool yBtn = input.buttons[static_cast<int>(GamepadButton::Y)];
    bool xBtn = input.buttons[static_cast<int>(GamepadButton::X)];
    bool bBtn = input.buttons[static_cast<int>(GamepadButton::B)];
    bool aBtn = input.buttons[static_cast<int>(GamepadButton::A)];

    bool cheatPressed = yBtn && xBtn && bBtn && aBtn;
    if (ENABLE_TUNER_CHEAT && cheatPressed) {
        if (!prevTunerCheat) {
            applyPerfectTunerCheat();
        }
        tunerHoldTimerY = tunerHoldTimerX = 0.0f;
        tunerHoldTimerB = tunerHoldTimerA = 0.0f;
        prevTunerCheat = true;
        return;
    }
    prevTunerCheat = false;
    
    // Helper lambda: handle a tuner button with debounced hold-to-repeat
    auto handleTunerButton = [&](bool btn, bool& prevBtn, float& holdTimer, 
                                  auto adjustFunc, bool isUp, TunerParam param) {
        if (btn) {
            if (!prevBtn) {
                // First press — immediate action
                bool bumped = adjustFunc();
                if (bumped) triggerBumperSound(); else triggerAdjustmentSound(isUp);
                tunerSoundCooldown = 0.15f;
                announceTunerParam(param);
                holdTimer = 0.0f;
            } else {
                // Held — accumulate hold time for repeat
                holdTimer += dt;
                if (holdTimer >= TUNER_HOLD_INITIAL_DELAY) {
                    // Repeat phase: fire at TUNER_HOLD_REPEAT_RATE
                    holdTimer -= TUNER_HOLD_REPEAT_RATE;
                    bool bumped = adjustFunc();
                    if (bumped && tunerSoundCooldown <= 0.0f) {
                        triggerBumperSound();
                        tunerSoundCooldown = 0.15f;
                    }
                }
            }
        } else {
            holdTimer = 0.0f;
        }
        prevBtn = btn;
    };
    
    float stepL = 1.0f;  // Fixed step per press (not dt-scaled for debounced mode)
    float stepC = 10.0f;
    
    handleTunerButton(yBtn, prevTunerY, tunerHoldTimerY,
        [&]() { return antennaNetwork->getTuner().adjustInductance(stepL); }, true, TunerParam::L);
    handleTunerButton(xBtn, prevTunerX, tunerHoldTimerX,
        [&]() { return antennaNetwork->getTuner().adjustInductance(-stepL); }, false, TunerParam::L);
    handleTunerButton(bBtn, prevTunerB, tunerHoldTimerB,
        [&]() { return antennaNetwork->getTuner().adjustCapacitance(stepC); }, true, TunerParam::C);
    handleTunerButton(aBtn, prevTunerA, tunerHoldTimerA,
        [&]() { return antennaNetwork->getTuner().adjustCapacitance(-stepC); }, false, TunerParam::C);
    
    // D-pad up: Increase UnUn ratio
    bool dpadUpPressed = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)];
    if (dpadUpPressed && !prevDpadUp) {
        int currentRatio = static_cast<int>(antennaNetwork->getUnUn().getRatio());
        if (currentRatio < 3) {
            antennaNetwork->getUnUn().setRatio(static_cast<UnUn::Ratio>(currentRatio + 1));
            triggerAdjustmentSound(true);
        } else {
            triggerBumperSound();
        }
        announceTunerParam(TunerParam::UNUN);
    }
    prevDpadUp = dpadUpPressed;
    
    // D-pad down: Decrease UnUn ratio
    bool dpadDownPressed = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)];
    if (dpadDownPressed && !prevDpadDown) {
        int currentRatio = static_cast<int>(antennaNetwork->getUnUn().getRatio());
        if (currentRatio > 0) {
            antennaNetwork->getUnUn().setRatio(static_cast<UnUn::Ratio>(currentRatio - 1));
            triggerAdjustmentSound(false);
        } else {
            triggerBumperSound();
        }
        announceTunerParam(TunerParam::UNUN);
    }
    prevDpadDown = dpadDownPressed;
}

// Update max speed based on matching quality
void Game::updateMaxSpeedFromMatching() {
    if (!antennaNetwork || track.empty()) {
        maxSpeed = baseMaxSpeed;
        return;
    }
    
    // Get adjusted SWR with current matching network
    float adjustedSWR = antennaNetwork->calculateAdjustedSWR(track, playerAngle);
    
    // Calculate speed factor
    float speedFactor = antennaNetwork->calculateSpeedFactor(adjustedSWR);
    
    // Apply to max speed
    maxSpeed = baseMaxSpeed * speedFactor;
    
    // If speed is very restricted, warn the player
    if (speedFactor < 0.3f && std::abs(playerSpeed) > 0.01f && announceCooldown <= 0.0f) {
        if (tts && tts->isAvailable()) {
            speakTranslated("HAMSPIRIT_SWR_WARNING", "Warning: High SWR detected!", false);
        }
        announceCooldown = 15.0f;  // Long cooldown to prevent TTS spam
    }
}

// Announce matching status with L, C, UNUN values and current SWR
void Game::announceMatchingStatus() {
    if (!antennaNetwork || !tts || !tts->isAvailable()) {
        return;
    }
    
    // Get current values
    float L = antennaNetwork->getTuner().getInductance();
    float C = antennaNetwork->getTuner().getCapacitance();
    float ratio = antennaNetwork->getUnUn().getRatioMultiplier();
    
    // Build simplified announcement — just the changed value + symbol
    std::string text;
    // Format L with one decimal: e.g. "5.3"
    char lBuf[16];
    std::snprintf(lBuf, sizeof(lBuf), "%.1f", static_cast<double>(L));
    text = std::string("L: ") + lBuf + " µH, "
         + "C: " + std::to_string(static_cast<int>(C)) + " pF, "
         + "UnUn " + std::to_string(static_cast<int>(ratio)) + ":1";
    
    tts->speak(text, false);  // Non-interrupt to avoid SAPI deadlock
}

void Game::autoSelectUnUnRatio() {
    if (!antennaNetwork || measurementData.empty()) {
        return;
    }
    auto& unun = antennaNetwork->getUnUn();
    float sumMag = 0.0f;
    int count = 0;
    for (const auto& point : measurementData) {
        if (std::isfinite(point.impedance_mag) && point.impedance_mag > 0.0) {
            sumMag += static_cast<float>(point.impedance_mag);
            count++;
        }
    }
    if (count == 0) {
        return;
    }

    float avgMag = sumMag / static_cast<float>(count);
    struct RatioChoice {
        UnUn::Ratio ratio;
        float multiplier;
    };
    const RatioChoice choices[] = {
        {UnUn::Ratio::RATIO_1_1, UnUn::getMultiplier(UnUn::Ratio::RATIO_1_1)},
        {UnUn::Ratio::RATIO_4_1, UnUn::getMultiplier(UnUn::Ratio::RATIO_4_1)},
        {UnUn::Ratio::RATIO_9_1, UnUn::getMultiplier(UnUn::Ratio::RATIO_9_1)},
        {UnUn::Ratio::RATIO_16_1, UnUn::getMultiplier(UnUn::Ratio::RATIO_16_1)}
    };

    float bestError = std::numeric_limits<float>::max();
    UnUn::Ratio bestRatio = unun.getRatio();
    for (const auto& choice : choices) {
        float adjusted = adjustUnUnMagnitude(avgMag, choice.multiplier);
        float error = std::abs(adjusted - kRefImpedanceOhms);
        if (error < bestError) {
            bestError = error;
            bestRatio = choice.ratio;
        }
    }

    if (bestRatio != unun.getRatio()) {
        unun.setRatio(bestRatio);
        std::ostringstream message;
        message << "Auto-selected UnUn ratio: " << ununRatioLabel(bestRatio)
                << " based on avg |Z|=" << avgMag;
        log("HAMSPIRIT", message.str());
    }
}

void Game::logMatchingFeasibility() {
    if (!antennaNetwork || track.empty()) {
        return;
    }
    auto& tuner = antennaNetwork->getTuner();
    auto& unun = antennaNetwork->getUnUn();
    float originalL = tuner.getInductance();
    float originalC = tuner.getCapacitance();
    auto originalRatio = unun.getRatio();

    float maxL = tuner.getMaxInductance();
    float maxC = tuner.getMaxCapacitance();
    // Coarse scan: ~((maxL/stepL)+1)*((maxC/stepC)+1)*ratios iterations (few thousand).
    constexpr float stepL = kFeasibilityStepL;
    constexpr float stepC = kFeasibilityStepC;
    constexpr float targetSWR = kFeasibilityTargetSWR;

    float bestSWR = std::numeric_limits<float>::max();
    float bestL = originalL;
    float bestC = originalC;
    UnUn::Ratio bestRatio = originalRatio;
    const UnUn::Ratio ratios[] = {
        UnUn::Ratio::RATIO_1_1,
        UnUn::Ratio::RATIO_4_1,
        UnUn::Ratio::RATIO_9_1,
        UnUn::Ratio::RATIO_16_1
    };
    int maxLSteps = static_cast<int>(std::ceil(maxL / stepL));
    int maxCSteps = static_cast<int>(std::ceil(maxC / stepC));
    TrackPoint currentPoint = TrackGenerator::interpolateAt(track, playerAngle);
    auto applyUnUn = [](std::complex<float> z, float multiplier) {
        if (multiplier <= 1.0f) {
            return z;
        }
        float mag = std::abs(z);
        return mag > kRefImpedanceOhms ? z / multiplier : z * multiplier;
    };
    auto computeSWR = [&](float l, float c, float multiplier) {
        std::complex<float> loadZ(std::max(currentPoint.resistance, kMinResistanceOhms), currentPoint.reactance);
        std::complex<float> transformedZ = applyUnUn(loadZ, multiplier);
        std::complex<float> finalZ = transformedZ;
        if (currentPoint.frequency > 0.0f && std::isfinite(currentPoint.frequency)) {
            float omega = 2.0f * PI * currentPoint.frequency;
            if (c >= kMinCapacitancePF) {
                float capF = c * 1e-12f;
                float XC = 1.0f / (omega * capF);
                std::complex<float> ZC(0.0f, -XC);
                std::complex<float> denom = finalZ + ZC;
                if (std::abs(denom) > kMinComplexDenominator) {
                    finalZ = (finalZ * ZC) / denom;
                }
            }
            if (l >= kMinInductanceUH) {
                float indH = l * 1e-6f;
                float XL = omega * indH;
                finalZ += std::complex<float>(0.0f, XL);
            }
        }
        if (!std::isfinite(finalZ.real()) || !std::isfinite(finalZ.imag())) {
            finalZ = transformedZ;
        }
        if (finalZ.real() <= 0.0f) {
            finalZ = std::complex<float>(0.1f, finalZ.imag());
        }
        std::complex<float> gammaDenom = finalZ + kRefImpedanceOhms;
        if (std::abs(gammaDenom) < kMinComplexDenominator) {
            gammaDenom = std::complex<float>(kMinComplexDenominator, 0.0f);
        }
        std::complex<float> gamma = (finalZ - kRefImpedanceOhms) / gammaDenom;
        float gammaMag = std::abs(gamma);
        if (!std::isfinite(gammaMag)) gammaMag = kMaxGammaMag;
        if (gammaMag >= kMaxGammaMag) gammaMag = kMaxGammaMag;
        float swr = (1.0f + gammaMag) / (1.0f - gammaMag);
        return std::max(kMinSWR, std::min(kMaxSWR, swr));
    };
    auto scanRatio = [&](UnUn::Ratio ratio) {
        float multiplier = UnUn::getMultiplier(ratio);
        for (int lStep = 0; lStep <= maxLSteps; ++lStep) {
            float l = lStep * stepL;
            for (int cStep = 0; cStep <= maxCSteps; ++cStep) {
                float c = cStep * stepC;
                float swr = computeSWR(l, c, multiplier);
                if (swr < bestSWR) {
                    bestSWR = swr;
                    bestL = l;
                    bestC = c;
                    bestRatio = ratio;
                }
            }
        }
    };

    for (auto ratio : ratios) {
        scanRatio(ratio);
    }

    tuner.setInductance(originalL);
    tuner.setCapacitance(originalC);
    unun.setRatio(originalRatio);

    std::ostringstream message;
    message << "Matching feasibility (coarse): best SWR=" << bestSWR
            << " at L=" << bestL << " C=" << bestC
            << " UnUn " << ununRatioLabel(bestRatio);
    log("HAMSPIRIT", message.str());
    if (bestSWR > kFeasibilityWarnSWR) {
        log("HAMSPIRIT", "WARNING: Matching remains high even with coarse L/C/UnUn search.");
    } else if (bestSWR <= targetSWR) {
        log("HAMSPIRIT", "Matching feasibility: coarse scan reached target SWR.");
    }
}

// Announce all current important values — triggered by Back/Select button
// Respects per-element verbosity settings from config.statusShow* flags
void Game::announceFullStatus() {
    if (!tts || !tts->isAvailable()) return;
    
    triggerStatusStartSound();  // Start chime
    
    bool isGerman = translation && 
        (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
    
    // Build status string based on verbosity settings
    std::string status;
    char buf[256];
    
    // 1. Speed in kHz/s
    if (config.statusShowSpeed) {
        float speedKHz = std::abs(playerSpeed) * kHzPerRadian;
        std::snprintf(buf, sizeof(buf), isGerman ? "Geschwindigkeit: %.1f Kilohertz pro Sekunde. " : "Speed: %.1f kHz per second. ",
            static_cast<double>(speedKHz));
        status += buf;
    }
    
    // 2. Current frequency and band
    if (config.statusShowFreq) {
        if (!track.empty()) {
            TrackPoint tp = TrackGenerator::interpolateAt(track, playerAngle);
            if (tp.frequency > 0.0f) {
                float freqMHz = tp.frequency / 1e6f;
                std::snprintf(buf, sizeof(buf), isGerman ? "Frequenz: %.2f MHz. " : "Frequency: %.2f MHz. ",
                    static_cast<double>(freqMHz));
                status += buf;
            }
        }
        if (!currentBandName.empty()) {
            status += isGerman ? ("Band: " + currentBandName + ". ") : ("Band: " + currentBandName + ". ");
        }
    }
    
    // 3. SWR
    if (config.statusShowSWR) {
        float swr = getCurrentSWR();
        std::snprintf(buf, sizeof(buf), "SWR: %.1f. ", static_cast<double>(swr));
        status += buf;
    }
    
    // 4. PA Health
    if (config.statusShowPA) {
        int healthPct = static_cast<int>(stats.paHealth * 100.0f);
        if (isGerman) {
            status += "Endstufe: " + std::to_string(healthPct) + "%. ";
        } else {
            status += "PA: " + std::to_string(healthPct) + "%. ";
        }
    }
    
    // 5. Tuner values (if antenna network exists)
    if (config.statusShowTuner && antennaNetwork) {
        float L = antennaNetwork->getTuner().getInductance();
        float C = antennaNetwork->getTuner().getCapacitance();
        float ratio = antennaNetwork->getUnUn().getRatioMultiplier();
        
        if (L < 0.01f && C < 0.01f) {
            status += isGerman ? "Tuner: Aus. " : "Tuner: Off. ";
        } else {
            char lBuf[16];
            std::snprintf(lBuf, sizeof(lBuf), "%.1f", static_cast<double>(L));
            status += std::string("L: ") + lBuf + ", C: " + std::to_string(static_cast<int>(C)) + 
                ", UnUn " + std::to_string(static_cast<int>(ratio)) + ":1. ";
        }
    }
    
    // 6. Score, laps, characters
    if (config.statusShowScore) {
        if (isGerman) {
            status += "Punkte: " + std::to_string(stats.score) + ". ";
            status += "Morsezeichen: " + std::to_string(stats.charactersCollected) + ". ";
        } else {
            status += "Score: " + std::to_string(stats.score) + ". ";
            status += "Morse: " + std::to_string(stats.charactersCollected) + ". ";
        }
    }
    if (config.statusShowLaps) {
        if (isGerman) {
            status += "Runden: " + std::to_string(stats.lapsCompleted) + ". ";
        } else {
            status += "Laps: " + std::to_string(stats.lapsCompleted) + ". ";
        }
    }
    
    // 7. Game time
    if (config.statusShowTime) {
        int minutes = static_cast<int>(stats.gameTime) / 60;
        int seconds = static_cast<int>(stats.gameTime) % 60;
        if (isGerman) {
            status += std::to_string(minutes) + " Minuten " + std::to_string(seconds) + " Sekunden.";
        } else {
            status += std::to_string(minutes) + " minutes " + std::to_string(seconds) + " seconds.";
        }
    }
    
    if (status.empty()) {
        status = isGerman ? "Keine Statuselemente ausgewählt." : "No status elements selected.";
    }
    
    // Append end marker to status text so it's spoken as one utterance.
    // Previously this was a separate speak() call, but that caused two issues:
    // - NVDA: second speak(interrupt=false) would queue after the first but
    //   if the game called stop() before the queue drained, only "Ende" was heard
    // - eSpeak: each speak() spawns a new process, so the second process ran
    //   concurrently with the first, causing overlapping speech
    status += " ";
    status += isGerman ? "Ende." : "End.";

    // Stop any ongoing speech first, then speak without interrupt flag.
    // Using stop() + speak(false) instead of speak(true) avoids a potential
    // SAPI deadlock where SPF_PURGEBEFORESPEAK can block on WAVE_MAPPER
    // during active gameplay audio.
    tts->stop();
    log("HAMSPIRIT", "Status readout: " + status);
    tts->speak(status, false);
    triggerStatusDoneSound();
}

// Trigger adjustment sound feedback (ascending or descending short tone)
void Game::triggerAdjustmentSound(bool ascending) {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::ADJUST, 120, 0.5f, ascending);
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.adjustSoundFrames = msToFrames(120);
        audioParams.adjustSoundUp = ascending;
    }
}

void Game::triggerBumperSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::BUMPER, 80);
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.bumperSoundFrames = msToFrames(80);
    }
}

void Game::triggerCollectSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::COLLECT, 160);
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.collectSoundFrames = msToFrames(160);
    }
}

void Game::triggerMissAimSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::MISS_AIM, 100);
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.missAimSoundFrames = msToFrames(100);
    }
}

void Game::triggerMissMorseSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::MISS_MORSE, 120);
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.missMorseSoundFrames = msToFrames(120);
    }
}

void Game::triggerMenuNavSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::MENU_NAV, 40);
    std::lock_guard<std::mutex> lock(audioStateMtx);
    audioParams.menuNavSoundFrames = msToFrames(40);
}

void Game::triggerMenuSelectSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::MENU_SELECT, 80);
    std::lock_guard<std::mutex> lock(audioStateMtx);
    audioParams.menuSelectSoundFrames = msToFrames(80);
}

void Game::triggerKeyClickSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::KEY_CLICK, 40);
    std::lock_guard<std::mutex> lock(audioStateMtx);
    audioParams.keyClickSoundFrames = msToFrames(40);
}

void Game::triggerPauseSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::PAUSE, 120);
    std::lock_guard<std::mutex> lock(audioStateMtx);
    audioParams.pauseSoundFrames = msToFrames(120);
}

void Game::triggerUnpauseSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::UNPAUSE, 120);
    std::lock_guard<std::mutex> lock(audioStateMtx);
    audioParams.unpauseSoundFrames = msToFrames(120);
}

void Game::triggerPaDamageSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::PA_DAMAGE, 200);
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.paDamageSoundFrames = msToFrames(200);
    }
}

void Game::triggerPaRepairSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::PA_REPAIR, 160);
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.paRepairSoundFrames = msToFrames(160);
    }
}

void Game::triggerStatusStartSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::STATUS_START, 120);
    std::lock_guard<std::mutex> lock(audioStateMtx);
    audioParams.statusStartSoundFrames = msToFrames(120);
}

void Game::triggerStatusDoneSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::STATUS_DONE, 80);
    std::lock_guard<std::mutex> lock(audioStateMtx);
    audioParams.statusDoneSoundFrames = msToFrames(80);
}

void Game::announceTunerParam(TunerParam param) {
    if (!antennaNetwork || !tts || !tts->isAvailable()) return;
    if (announceCooldown > 0.0f) return;
    
    // Don't call TTS with interrupt during gameplay — SAPI purge can deadlock
    // with the audio thread's waveOut calls. Use non-interrupt (queued) mode.
    std::string text;
    switch (param) {
        case TunerParam::L: {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(antennaNetwork->getTuner().getInductance()));
            text = std::string("L: ") + buf + " µH";
            break;
        }
        case TunerParam::C:
            text = "C: " + std::to_string(static_cast<int>(antennaNetwork->getTuner().getCapacitance())) + " pF";
            break;
        case TunerParam::UNUN:
            text = "UnUn " + std::to_string(static_cast<int>(antennaNetwork->getUnUn().getRatioMultiplier())) + ":1";
            break;
    }
    
    tts->speak(text, false);  // Non-interrupt: avoids SAPI purge deadlock with audio thread
    announceCooldown = 3.0f;
}

void Game::applyPerfectTunerCheat() {
    if (!ENABLE_TUNER_CHEAT || !antennaNetwork || track.empty()) {
        return;
    }

    constexpr float coarseStepL = 5.0f;
    constexpr float coarseStepC = 50.0f;
    constexpr float fineStepL = 1.0f;
    constexpr float fineStepC = 10.0f;
    constexpr float targetSWR = 1.05f;  // Near-perfect tuning threshold

    auto& tuner = antennaNetwork->getTuner();
    float maxL = tuner.getMaxInductance();
    float maxC = tuner.getMaxCapacitance();
    float originalL = tuner.getInductance();
    float originalC = tuner.getCapacitance();
    float bestL = originalL;
    float bestC = originalC;
    float bestSWR = antennaNetwork->calculateAdjustedSWR(track, playerAngle);

    auto scanRange = [&](float startL, float endL, float stepL, float startC, float endC, float stepC) {
        for (float l = startL; l <= endL; l += stepL) {
            tuner.setInductance(l);
            for (float c = startC; c <= endC; c += stepC) {
                tuner.setCapacitance(c);
                float swr = antennaNetwork->calculateAdjustedSWR(track, playerAngle);
                if (swr < bestSWR) {
                    bestSWR = swr;
                    bestL = l;
                    bestC = c;
                }
                if (bestSWR <= targetSWR) {
                    return true;
                }
            }
        }
        return false;
    };

    bool reachedTarget = scanRange(0.0f, maxL, coarseStepL, 0.0f, maxC, coarseStepC);
    if (!reachedTarget && bestSWR > targetSWR) {
        auto adjustRange = [](float& start, float& end, float max) {
            float width = end - start;
            if (start < 0.0f) {
                start = 0.0f;
                end = std::min(max, start + width);
            }
            if (end > max) {
                end = max;
                start = std::max(0.0f, end - width);
            }
        };
        float fineLStart = bestL - coarseStepL;
        float fineLEnd = bestL + coarseStepL;
        adjustRange(fineLStart, fineLEnd, maxL);
        float fineCStart = bestC - coarseStepC;
        float fineCEnd = bestC + coarseStepC;
        adjustRange(fineCStart, fineCEnd, maxC);
        scanRange(fineLStart, fineLEnd, fineStepL, fineCStart, fineCEnd, fineStepC);
    }

    tuner.setInductance(bestL);
    tuner.setCapacitance(bestC);
    triggerAdjustmentSound(true);
    log("HAMSPIRIT", "Tuner cheat applied: L=" + std::to_string(bestL) + " C=" + std::to_string(bestC) +
        " SWR=" + std::to_string(bestSWR));
    announceMatchingStatus();
}

// Restart game for replay
void Game::restartGame() {
    log("HAMSPIRIT", "Restarting game");
    
    // Stop audio thread before reset to avoid race conditions.
    // Use a longer timeout and never detach — wait until the thread exits.
    if (audioRunning.load()) {
        audioRunning.store(false);
        if (audioBackend) audioBackend->abort();
        if (audioThread.joinable()) {
            // Poll for up to 3 seconds, re-aborting periodically
            for (int i = 0; i < 150 && !audioThreadExited.load(); i++) {
                if (audioBackend) audioBackend->abort();
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (audioThreadExited.load()) {
                audioThread.join();
            } else {
                // Last resort: detach, but do NOT touch buffers that the
                // detached thread may still be using.  Log a warning.
                log("HAMSPIRIT", "WARNING: Audio thread did not exit in 3s — detaching");
                audioThread.detach();
            }
        }
        if (audioBackend) audioBackend->resetAbort();
    }
    
    playerAngle = 0.0f;
    playerSpeed = 0.0f;
    playerLateralOffset = 0.0f;
    isBraking = false;
    reverseHoldTime = 0.0f;
    aimAngle = 0.0f;
    announceCooldown = 0.0f;
    tunerSoundCooldown = 0.0f;
    prevTunerY = prevTunerX = prevTunerB = prevTunerA = false;
    prevTunerCheat = false;
    prevDpadUp = prevDpadDown = false;
    prevStartBtn = false;
    prevBackBtn = false;
    tunerHoldTimerY = tunerHoldTimerX = tunerHoldTimerB = tunerHoldTimerA = 0.0f;
    swrAlertPhase = 0.0f;
    lastAnnouncedDamageStage = 0;
    paReflectedPowerAccum = 0.0f;
    paThermalLoad = 0.0f;
    stats = GameStats{};
    hamSpiritBonusAchieved = false;
    collectedChars.clear();
    nextMorseSpawnTime = 5.0f;
    nextTrafficReportTime = 30.0f;
    lastNoisePanDebugTime = kInitialDebugThrottleTime;
    lastKeyboardUnknownKeyTime = kInitialDebugThrottleTime;
    lastConsoleFocusLogTime = kInitialDebugThrottleTime;
    lastConsoleFocused = true;
    currentBandName.clear();
    brailleUpdateTimer = 0.0f;
    statusReadoutActive = false;
    curveAnnounceCooldown = 0.0f;
    lastAnnouncedCurveAngle = -1.0f;
    
    currentWeapon = WeaponType::NOISE_BLANKER;
    noiseBlankerCooldown = 0.0f;
    pendingHitTimer = 0.0f;
    pendingHitDestroyed = false;
    prevDpadLeft = prevDpadRight = false;
    prevLeftTrigger = false;
    prevRightStickBtn = false;
    prevLeftStickBtn = false;
    emergencyBrakeTimer = 0.0f;
    emergencyBrakeStartSpeed = 0.0f;
    smoothedForwardInput = 0.0f;
    prevCfgUp = prevCfgDown = prevCfgLeft = prevCfgRight = prevCfgA = prevCfgB = false;
    prevMainUp = prevMainDown = prevMainA = false;
    prevMenuUp = prevMenuDown = prevMenuA = false;
    inSoundLearning = false;
    soundLearningFromMainMenu = false;
    soundLearningIndex = 0;
    nextNoiseSpawnTime = 20.0f;
    nextNoiseAnnounceTime = 17.0f;
    scheduledNoiseAngle = 0.0f;
    noiseScheduled = false;
    noiseEnemies.clear();
    
    // Reset border collision and QSO Störer state
    crashRecoveryTime = 0.0f;
    trackBorderProximity = 0.0f;
    borderVibrationActive = false;
    crashVibrationTimer = 0.0f;
    qsoStoerer = QSOStoerer();
    nextQSOStoererSpawnTime = 90.0f;
    
    // Reset power-up system
    powerUps.clear();
    activePowerUps.clear();
    powerUpDurationBonus = 0.0f;
    nextPowerUpSpawnTime = 30.0f;
    powerUpCollectTimer = 0.0f;
    powerUpCollectTargetUid = 0;
    powerUpCollectLastCountdown = -1;
    powerUpCollectCountdownTimer = 0.0f;
    powerUpNoAimCooldown = 0.0f;
    prevBothTriggersHeld = false;
    autoFireActive = false;
    swrImmunityActive = false;
    savedMaxSpeedBeforeBoost = 0.0f;
    savedCooldownBeforeBoost = 0.0f;
    
    if (morseSignalManager) morseSignalManager->clear();
    if (morseCannon) morseCannon->reset();
    if (antennaNetwork) {
        antennaNetwork->getTuner().reset();
        antennaNetwork->getUnUn().setRatio(UnUn::Ratio::RATIO_1_1);
    }
    
    allCurvesCurrentSection = 0;
    
    // Stop vibration on ALL controllers
    stopAllVibration();
    
    // Regenerate track
    track = TrackGenerator::generateTrack(measurementData, config.trackCurve);
    
    // Initialize the central GameAuthority (server-authoritative architecture).
    // The authority manages all world state and treats all players identically.
    // This replaces the old Player0-centric model where the Game class
    // directly managed world state through global variables.
    gameAuthority = std::make_unique<GameAuthority>();
    gameAuthority->initialize(track, config);
    
    // Register the primary player with the authority.
    // In singleplayer mode, there is exactly one client entity.
    // In multiplayer, additional players are registered by updateMultiplayerState().
    // No player has special privileges — all go through the same registration path.
    PlayerInfo primaryPlayer;
    primaryPlayer.callsign = multiplayerConfig.playerCallsigns[0];
    primaryPlayer.playerName = multiplayerConfig.playerNames[0];
    gameAuthority->registerPlayer(primaryPlayer);
    
    // Register additional multiplayer players if applicable
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        int playerCount = multiplayerMgr->getPlayerCount();
        for (int i = 1; i < playerCount; i++) {
            PlayerInfo info;
            info.callsign = multiplayerConfig.playerCallsigns[i];
            info.playerName = multiplayerConfig.playerNames[i];
            gameAuthority->registerPlayer(info);
        }
    }
    
    // Reset timing
    lastUpdateTime = std::chrono::steady_clock::now();
    gameStartTime = lastUpdateTime;
    
    // Restart audio thread
    if (audioInitialized) {
        // Reset audio state to clean defaults before restarting
        {
            std::lock_guard<std::mutex> lock(audioStateMtx);
            audioParams = AudioParams{};
        }
        // Reset secondary players' audio state to prevent stale sounds
        // (e.g. motor tone, SWR alert) from a previous game leaking into
        // the new race start.  Player 0's audioParams are reset above;
        // secondary players use their own PlayerAudioParams stored in ctx.
        if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
            for (int p = 0; p < multiplayerMgr->getPlayerCount(); p++) {
                auto* ctx = multiplayerMgr->getPlayer(p);
                if (!ctx) continue;
                std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                ctx->audioState = PlayerAudioParams{};
            }
        }
        // Only re-allocate buffers if the audio thread has truly exited
        // (not detached and still running), to avoid data races.
        if (audioThreadExited.load()) {
            const size_t bufSize = GAME_AUDIO_SAMPLES * GAME_CHANNELS;
            audioBuffer.assign(bufSize, 0);
            mixBuffer.assign(bufSize, 0);
        }
        
        // Clear abort flag again (belt-and-suspenders) so playBuffer works
        if (audioBackend) {
            audioBackend->resetAbort();
        }
        
        audioRunning.store(true);
        audioThread = std::thread(&Game::audioThreadFunc, this);
    }
    
    setState(GameState::PLAYING);
}

// Update morse system
// Load band plan based on translation language
void Game::loadBandPlan() {
    // Map language code to bandplan filename
    std::string bandplanCode = "deu";  // Default
    if (translation) {
        std::string lang = translation->getCurrentLanguage();
        if (lang == "eng" || lang == "en") bandplanCode = "usa";
        else if (lang == "deu" || lang == "de") bandplanCode = "deu";
        else bandplanCode = lang;  // Try language code directly
    }
    
    std::string error;
    bandPlan = getAmateurBands(bandplanCode);
    if (bandPlan.empty()) {
        // Fallback to deu
        bandPlan = getAmateurBands("deu");
    }
    
    if (!bandPlan.empty()) {
        log("HAMSPIRIT", "Loaded band plan: " + bandplanCode + " with " + 
            std::to_string(bandPlan.size()) + " bands");
    } else {
        log("HAMSPIRIT", "WARNING: No band plan loaded");
    }
    
    currentBandName.clear();
}

// Check if player crossed a band boundary — announce like highway border signs
void Game::checkBandCrossing() {
    if (bandPlan.empty() || track.empty()) return;
    
    // Get frequency at current player position
    TrackPoint tp = TrackGenerator::interpolateAt(track, playerAngle);
    float freq = tp.frequency;
    if (freq <= 0.0f) return;
    
    uint64_t freqHz = static_cast<uint64_t>(freq);
    
    // Sanity check: only check bands if the frequency is within the measurement data range.
    // 5% tolerance avoids false triggers from interpolation overshoot at the edges of the data,
    // where frequency values can slightly exceed the measurement start/end points.
    static constexpr float BAND_CHECK_TOLERANCE_FRACTION = 0.05f;
    float tolerance = (maxTrackFreqHz - minTrackFreqHz) * BAND_CHECK_TOLERANCE_FRACTION;
    if (freq < (minTrackFreqHz - tolerance) || freq > (maxTrackFreqHz + tolerance)) {
        return;  // Frequency is outside the measurement range — skip band check
    }
    
    // Find which band we're in (if any)
    // Only consider bands that actually overlap with the measurement frequency range
    std::string newBandName;
    for (const auto& band : bandPlan) {
        // Skip bands that don't overlap with the measurement frequency range at all
        if (band.end_hz < static_cast<uint64_t>(minTrackFreqHz) || 
            band.start_hz > static_cast<uint64_t>(maxTrackFreqHz)) {
            continue;
        }
        if (freqHz >= band.start_hz && freqHz <= band.end_hz) {
            newBandName = band.name;
            break;
        }
    }
    
    if (newBandName != currentBandName) {
        // Band changed!
        if (!currentBandName.empty() && tts && tts->isAvailable()) {
            // Leaving a band — play descending jingle first
            triggerBandJingle(false);
            if (translation) {
                tts->speak(translation->format("HAMSPIRIT_BAND_LEAVE",
                    "You are now leaving the {0} band.", currentBandName), false);
            } else {
                tts->speak("You are now leaving the " + currentBandName + " band.", false);
            }
            log("HAMSPIRIT", "Left band: " + currentBandName);
        }
        
        if (!newBandName.empty() && tts && tts->isAvailable()) {
            // Entering a band — play ascending jingle first
            triggerBandJingle(true);
            if (translation) {
                tts->speak(translation->format("HAMSPIRIT_BAND_ENTER",
                    "Welcome to the {0} band!", newBandName), false);
            } else {
                tts->speak("Welcome to the " + newBandName + " band!", false);
            }
            log("HAMSPIRIT", "Entered band: " + newBandName);
        }
        
        currentBandName = newBandName;
    }
}

void Game::triggerBandJingle(bool entering) {
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.bandJingleFrames = msToFrames(160);  // ~160ms jingle
        audioParams.bandJingleAscending = entering;
    }
}

// Check if it's time for a traffic report
void Game::checkTrafficReport() {
    if (!config.trafficReports) return;
    if (stats.gameTime < nextTrafficReportTime) return;
    if (!tts || !tts->isAvailable()) return;
    if (track.empty()) return;
    
    generateTrafficReport();
    
    // Schedule next report in 35-65 seconds (was 20-40 — less TTS overload)
    nextTrafficReportTime = stats.gameTime + 35.0f + (static_cast<float>(rand()) / RAND_MAX) * 30.0f;
}

// Generate and speak a humorous traffic report about RF conditions ahead
void Game::generateTrafficReport() {
    if (track.empty()) return;
    
    // Scan track ahead for SWR problems
    float lookAheadStart = playerAngle + 0.2f;
    float lookAheadEnd = playerAngle + 2.0f;
    
    float worstSWR = 1.0f;
    float worstAngle = 0.0f;
    float avgReactance = 0.0f;
    int sampleCount = 0;
    
    for (float a = lookAheadStart; a < lookAheadEnd; a += 0.1f) {
        float normalizedAngle = a;
        while (normalizedAngle >= TWO_PI) normalizedAngle -= TWO_PI;
        TrackPoint tp = TrackGenerator::interpolateAt(track, normalizedAngle);
        
        float swr = tp.swr;
        if (antennaNetwork) {
            swr = antennaNetwork->calculateAdjustedSWR(track, normalizedAngle);
        }
        
        if (swr > worstSWR) {
            worstSWR = swr;
            worstAngle = a;
        }
        avgReactance += tp.reactance;
        sampleCount++;
    }
    if (sampleCount > 0) avgReactance /= sampleCount;
    
    // Calculate "distance" in MHz (using frequency difference)
    TrackPoint currentTP = TrackGenerator::interpolateAt(track, playerAngle);
    TrackPoint worstTP = TrackGenerator::interpolateAt(track, worstAngle);
    float freqDiffMHz = std::abs(worstTP.frequency - currentTP.frequency) / 1e6f;
    
    // Play traffic beep first
    {
        std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
        if (lock.owns_lock()) {
            audioParams.trafficBeepFrames = msToFrames(300);  // ~300ms whistle tone
        }
    }
    
    // Build the report message
    std::string report;
    bool isGerman = translation && 
        (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
    
    if (worstSWR > 5.0f) {
        // Severe SWR problem — multiple variants
        int variant = rand() % 4;
        if (isGerman) {
            char buf[192];
            switch (variant) {
                case 0:
                    std::snprintf(buf, sizeof(buf), "Achtung Verkehrsmeldung: Starke Fehlanpassung in %.1f Megahertz. SWR %.1f zu 1. Bitte Tuner vorbereiten!", 
                        static_cast<double>(freqDiffMHz), static_cast<double>(worstSWR));
                    break;
                case 1:
                    std::snprintf(buf, sizeof(buf), "Eilmeldung: Vollsperrung in %.1f Megahertz! SWR %.1f zu 1. Endstufe in Gefahr!",
                        static_cast<double>(freqDiffMHz), static_cast<double>(worstSWR));
                    break;
                case 2:
                    std::snprintf(buf, sizeof(buf), "Stauwarnung: Massiver Reflexionsstau in %.1f Megahertz. SWR %.1f zu 1. Umleitung empfohlen!",
                        static_cast<double>(freqDiffMHz), static_cast<double>(worstSWR));
                    break;
                default:
                    std::snprintf(buf, sizeof(buf), "Katastrophenalarm: SWR %.1f zu 1 in %.1f Megahertz! Die Endstufe bittet um Erbarmen!",
                        static_cast<double>(worstSWR), static_cast<double>(freqDiffMHz));
                    break;
            }
            report = buf;
        } else {
            char buf[192];
            switch (variant) {
                case 0:
                    std::snprintf(buf, sizeof(buf), "Traffic alert: Severe mismatch in %.1f megahertz. SWR %.1f to 1. Prepare your tuner!",
                        static_cast<double>(freqDiffMHz), static_cast<double>(worstSWR));
                    break;
                case 1:
                    std::snprintf(buf, sizeof(buf), "Breaking: Total roadblock in %.1f megahertz! SWR %.1f to 1. PA in danger!",
                        static_cast<double>(freqDiffMHz), static_cast<double>(worstSWR));
                    break;
                case 2:
                    std::snprintf(buf, sizeof(buf), "Warning: Massive reflection pileup in %.1f megahertz. SWR %.1f to 1. Detour recommended!",
                        static_cast<double>(freqDiffMHz), static_cast<double>(worstSWR));
                    break;
                default:
                    std::snprintf(buf, sizeof(buf), "Emergency: SWR %.1f to 1 at %.1f megahertz! Your PA is begging for mercy!",
                        static_cast<double>(worstSWR), static_cast<double>(freqDiffMHz));
                    break;
            }
            report = buf;
        }
    } else if (worstSWR > 3.0f) {
        // Moderate SWR problem — multiple variants
        int variant = rand() % 3;
        if (isGerman) {
            char buf[192];
            switch (variant) {
                case 0:
                    std::snprintf(buf, sizeof(buf), "Verkehrshinweis: Erhöhtes SWR von %.1f zu 1 in %.1f Megahertz. Langsamfahrstelle.",
                        static_cast<double>(worstSWR), static_cast<double>(freqDiffMHz));
                    break;
                case 1:
                    std::snprintf(buf, sizeof(buf), "Verkehrsmeldung: Leichte Fehlanpassung voraus. SWR %.1f zu 1 in %.1f Megahertz. Tuner-Einsatz erwägen.",
                        static_cast<double>(worstSWR), static_cast<double>(freqDiffMHz));
                    break;
                default:
                    std::snprintf(buf, sizeof(buf), "Achtung: Holprige Strecke in %.1f Megahertz. SWR %.1f zu 1. Festhalten!",
                        static_cast<double>(freqDiffMHz), static_cast<double>(worstSWR));
                    break;
            }
            report = buf;
        } else {
            char buf[192];
            switch (variant) {
                case 0:
                    std::snprintf(buf, sizeof(buf), "Traffic advisory: Elevated SWR of %.1f to 1 in %.1f megahertz. Slow zone ahead.",
                        static_cast<double>(worstSWR), static_cast<double>(freqDiffMHz));
                    break;
                case 1:
                    std::snprintf(buf, sizeof(buf), "Traffic report: Mild mismatch ahead. SWR %.1f to 1 at %.1f megahertz. Consider tuner adjustment.",
                        static_cast<double>(worstSWR), static_cast<double>(freqDiffMHz));
                    break;
                default:
                    std::snprintf(buf, sizeof(buf), "Advisory: Bumpy road at %.1f megahertz. SWR %.1f to 1. Hold tight!",
                        static_cast<double>(freqDiffMHz), static_cast<double>(worstSWR));
                    break;
            }
            report = buf;
        }
    } else if (std::abs(avgReactance) > 50.0f) {
        // Reactance warning (fun version)
        if (avgReactance > 0) {
            if (isGerman) {
                report = "Verkehrsmeldung: Induktive Baustelle voraus. Blindwiderstand blockiert die Überholspur.";
            } else {
                report = "Traffic report: Inductive construction zone ahead. Reactance is blocking the fast lane.";
            }
        } else {
            if (isGerman) {
                report = "Verkehrsmeldung: Kapazitiver Gegenverkehr voraus. Vorsicht, Phasenverschiebung!";
            } else {
                report = "Traffic report: Capacitive oncoming traffic ahead. Watch out for phase shift!";
            }
        }
    } else {
        // All clear — fun message
        int variety = rand() % 4;
        if (isGerman) {
            switch (variety) {
                case 0: report = "Verkehrsmeldung: Freie Fahrt auf allen Frequenzen. SWR traumhaft."; break;
                case 1: report = "Verkehrshinweis: Die Strecke voraus ist gut angepasst. Gute Reise!"; break;
                case 2: report = "Verkehrsmeldung: Keine besonderen Vorkommnisse. 50 Ohm reine Freude."; break;
                case 3: report = "Hinweis: Der Funkwetterdienst meldet optimale Impedanzverhältnisse. Weiterhin gute Fahrt!"; break;
            }
        } else {
            switch (variety) {
                case 0: report = "Traffic report: Clear frequencies ahead. SWR is looking beautiful."; break;
                case 1: report = "Traffic advisory: Well-matched road ahead. Happy travels!"; break;
                case 2: report = "Traffic report: No incidents to report. Pure 50 ohm bliss."; break;
                case 3: report = "Advisory: Optimal impedance conditions ahead. Carry on!"; break;
            }
        }
    }
    
    if (!report.empty()) {
        log("HAMSPIRIT", "Traffic report: " + report);
        tts->speak(report, false);
        pushBannerText(report);  // Show Verkehrsservice report on scrolling banner
    }
}

void Game::updateMorseSystem(float dt) {
    if (!config.elemMorseSignals) return;  // Morse signals disabled
    if (!morseSignalManager) {
        return;
    }
    
    // Update all morse signals
    morseSignalManager->update(playerAngle, stats.gameTime, dt);
    
    // Spawn new signals periodically
    spawnMorseSignals(stats.gameTime);
    
    // Update morse cannon
    if (morseCannon) {
        GamepadState input = getCurrentInput();
        // Suppress vertical key (RT) when both triggers are held for power-up collection.
        // Applies to both gamepad and keyboard input to enable keyboard power-up collection.
        float ltVal = input.axes[static_cast<int>(GamepadAxis::LEFT_TRIGGER)];
        float rtVal = input.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)];
        bool bothTriggersHeld = ltVal > POWERUP_TRIGGER_THRESHOLD && rtVal > POWERUP_TRIGGER_THRESHOLD;
        morseCannon->update(input, dt, bothTriggersHeld);
    }
}

// Handle morse cannon input and collection
void Game::handleMorseCannonInput(const GamepadState& input, float dt) {
    if (!morseCannon || !morseSignalManager) {
        return;
    }
    
    // Check if a character was sent
    char sentChar = morseCannon->getLastSentChar();
    if (sentChar != '\0') {
        log("HAMSPIRIT_MORSE", std::string("Sent morse character: ") + sentChar);
        
        // Try to collect targeted signal
        if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
            checkMorseCollectionForPlayer(sentChar, 0);
        } else {
            checkMorseCollection(sentChar);
        }
        
        // Morse interaction with QSO Störer
        if (qsoStoerer.active) {
            // "99" cheat: morse '9' twice in a row to dismiss the Störer
            if (sentChar == '9' && qsoStoerer.lastMorseChar == '9') {
                qsoStoerer.active = false;
                qsoStoerer.lastMorseChar = '\0';
                {
                    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
                    if (lock.owns_lock()) audioParams.qsoStoererActive = false;
                }
                stats.score += MORSE_QSO_DISMISS_SCORE;  // Ham radio lucky number
                log("HAMSPIRIT", "QSO Störer dismissed by 99 cheat!");
                if (tts && tts->isAvailable()) {
                    speakTranslated("HAMSPIRIT_QSO_STOERER_99", "99! QSO interferer signs off. 73!", false);
                }
            } else {
                qsoStoerer.lastMorseChar = sentChar;
                
                // 5% chance the Störer makes a driving error when any morse is sent
                if ((rand() % 100) < QSO_STOERER_DRIVING_ERROR_CHANCE) {
                    qsoStoerer.drivingErrorTimer = QSO_STOERER_ERROR_DURATION;  // 2 seconds of swerving/slowing
                    // Swerve to opposite side of track
                    qsoStoerer.lateralOffset = -qsoStoerer.lateralOffset;
                    if (std::abs(qsoStoerer.lateralOffset) < QSO_STOERER_ERROR_MIN_LATERAL) {
                        qsoStoerer.lateralOffset = (rand() % 2 == 0) ? QSO_STOERER_ERROR_SWERVE_LATERAL : -QSO_STOERER_ERROR_SWERVE_LATERAL;
                    }
                    log("HAMSPIRIT", "QSO Störer driving error triggered by morse!");
                    if (tts && tts->isAvailable()) {
                        speakTranslated("HAMSPIRIT_QSO_STOERER_DRIVING_ERROR", "Interferer driving error!", false);
                    }
                }
            }
        }
    }
}

// Handle aiming input
void Game::handleAimingInput(const GamepadState& input, float dt) {
    // Right analog stick controls aiming
    float aimX = input.axes[static_cast<int>(GamepadAxis::RIGHT_X)];
    
    float prevAimAngle = aimAngle;
    
    // If right stick is moved, break heading sync and return to free aiming
    if (std::abs(aimX) > HEADING_SYNC_BREAK_THRESHOLD) {
        if (aimSyncToHeading) {
            aimSyncToHeading = false;
            log("HAMSPIRIT", "Aim sync broken — free aiming");
        }
        // Update aim angle in free mode
        aimAngle += aimX * aimSpeed * config.aimSensitivity * dt;
    }
    
    // In heading-sync mode, weapon follows vehicle heading (aimAngle stays at 0)
    if (aimSyncToHeading) {
        // Smoothly bring aim angle to 0 (forward/vehicle heading)
        aimAngle *= HEADING_SYNC_DECAY;  // Ease toward zero
        if (std::abs(aimAngle) < HEADING_SYNC_SNAP_THRESHOLD) aimAngle = 0.0f;
    }
    
    // Keep aim angle in full rotation range [-π, π]
    while (aimAngle > PI) aimAngle -= TWO_PI;
    while (aimAngle < -PI) aimAngle += TWO_PI;
    
    // Confirmation tone: play a short click when the aim crosses the forward
    // heading (aimAngle crosses zero) during free aiming.
    // Skip when heading sync is active (the weapon is already locked forward).
    if (!aimSyncToHeading && std::abs(aimX) > HEADING_SYNC_BREAK_THRESHOLD) {
        // Check for zero crossing: previous and current aim angle have different signs
        bool crossed = (prevAimAngle > 0.05f && aimAngle <= 0.05f) ||
                       (prevAimAngle < -0.05f && aimAngle >= -0.05f);
        if (crossed) {
            triggerAimResetSound();  // Short confirmation click
        }
    }
    
    // Right stick click (R3): toggle heading sync
    // When synced, weapon tracks vehicle heading — useful when target is ahead
    bool rightStickBtn = input.buttons[static_cast<int>(GamepadButton::RIGHT_STICK)];
    if (rightStickBtn && !prevRightStickBtn) {
        aimSyncToHeading = !aimSyncToHeading;
        if (aimSyncToHeading) {
            aimAngle = 0.0f;
            triggerAimResetSound();
            log("HAMSPIRIT", "Aim synced to vehicle heading");
        } else {
            triggerAimResetSound();
            log("HAMSPIRIT", "Aim sync released — free aiming");
        }
    }
    prevRightStickBtn = rightStickBtn;
}

    // Spawn morse signals
    void Game::spawnMorseSignals(float gameTime) {
        if (!morseSignalManager || !morseDatabase) {
            return;
        }
        
        // Check if it's time to spawn a new signal
        if (gameTime < nextMorseSpawnTime) {
            return;
        }
    
    // Morse character difficulty from dedicated config (independent of game difficulty)
    int difficulty = config.morseDifficulty;
    
    // Get random character
    char c = morseDatabase->getRandomChar(difficulty);
    
    // Spawn ahead of a randomly selected player (globalizes spawning for multiplayer).
    // In singleplayer or if no multiplayer manager, uses player 0's angle.
    float referenceAngle = playerAngle;
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        int pc = multiplayerMgr->getPlayerCount();
        int chosenPlayer = rand() % pc;
        auto* ctx = multiplayerMgr->getPlayer(chosenPlayer);
        if (ctx) referenceAngle = ctx->playerAngle;
    }
    
    float maxSpawnDist = MAX_EVENT_SPAWN_DIST_POINTS * (TWO_PI / std::max(1.0f, static_cast<float>(track.size())));
    float spawnAngle = referenceAngle + std::min(1.0f + (static_cast<float>(rand()) / RAND_MAX) * 1.0f, maxSpawnDist);
    while (spawnAngle >= TWO_PI) spawnAngle -= TWO_PI;
    
    // Only one signal allowed in audible range (1.5 rad) — prevents acoustic confusion
    if (morseSignalManager->isPositionTooClose(spawnAngle, 1.5f)) {
        return;  // Skip this spawn, try again later
    }
    
    morseSignalManager->spawnSignal(c, spawnAngle, gameTime);
    log("HAMSPIRIT_MORSE", std::string("Spawned morse signal: ") + c);
    
    // Spawn rate scales with difficulty: easier = longer intervals
    // 14s at diff 1, 9s at diff 5 (was 7s/3s — more time to process each signal)
    float baseInterval = 14.0f - config.difficultyLevel * 1.0f;
    float randomRange = 8.0f - config.difficultyLevel * 0.5f;
    nextMorseSpawnTime = gameTime + baseInterval + (static_cast<float>(rand()) / RAND_MAX) * randomRange;
}

// Check morse collection
void Game::checkMorseCollection(char sentChar) {
    if (!morseSignalManager || !antennaNetwork || track.empty()) {
        return;
    }
    
    if (sentChar == '\0') {
        return;
    }
    
    // Route through the central authority when available.
    // The authority validates morse collection for ALL players identically.
    if (gameAuthority && gameAuthority->isActive()) {
        PlayerAction action;
        action.type = PlayerActionType::MORSE_CANNON_FIRE;
        action.playerId = 0;
        action.angle = playerAngle;
        action.morseChar = sentChar;
        action.timestamp = std::chrono::steady_clock::now();
        gameAuthority->processAction(action);
    }
    
    // Get targeted signal
    const float AIM_MARGIN = config.elemAutoAim ? static_cast<float>(PI) : 0.3f;  // Auto-aim: accept any direction
    MorseSignal* target = morseSignalManager->getTargetedSignal(playerAngle, aimAngle, AIM_MARGIN);
    
    if (!target) {
        // No signal in aim — check if character would have matched any nearby signal
        log("HAMSPIRIT_MORSE", "No signal in aim");
        triggerMissMorseSound();
        if (tts && tts->isAvailable()) {
            tts->speak("Miss", false);
        }
        return;
    }
    
    // Get current reactance for adaptive collection
    TrackPoint currentPoint = TrackGenerator::interpolateAt(track, playerAngle);
    float reactance = currentPoint.reactance;
    
    // Try to collect — exact character match only
    bool collected = morseSignalManager->tryCollectSignal(target, sentChar, reactance);
    
    if (collected) {
        morseMissCount = 0;
        log("HAMSPIRIT_MORSE", std::string("Collected character: ") + target->character);
        
        // Add to collected characters
        collectedChars.push_back(target->character);
        stats.charactersCollected++;
        stats.score += 50;  // Points for collecting

        // Push event for multiplayer spatial audio propagation
        if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
            multiplayerMgr->pushEvent(GameEvent(
                GameEventType::MORSE_COLLECTED, 0, playerAngle, playerLateralOffset));
        }
        
        // Check for HAMSPIRIT bonus
        checkHamSpiritBonus();
        
        // === PA REPAIR from collected morse characters ===
        // Each collected character repairs 5% PA health
        // Harder characters (longer patterns) repair more
        float repairAmount = 0.05f;
        std::string pattern;
        if (morseDatabase) pattern = morseDatabase->getPattern(target->character);
        if (pattern.length() >= 3) repairAmount = 0.08f;  // Complex chars repair more
        if (pattern.length() >= 4) repairAmount = 0.12f;
        
        float oldHealth = stats.paHealth;
        stats.paHealth = std::min(1.0f, stats.paHealth + repairAmount);
        paThermalLoad = std::max(0.0f, paThermalLoad - 0.1f);  // Also cool PA slightly
        
        // Recalculate damage stage (may improve)
        int newStage = 0;
        if (stats.paHealth <= 0.1f) newStage = 5;
        else if (stats.paHealth <= 0.3f) newStage = 4;
        else if (stats.paHealth <= 0.5f) newStage = 3;
        else if (stats.paHealth <= 0.7f) newStage = 2;
        else if (stats.paHealth <= 0.9f) newStage = 1;
        if (newStage < lastAnnouncedDamageStage) {
            lastAnnouncedDamageStage = newStage;  // Allow re-announcement if it worsens again
        }
        
        // Success + repair sound and announcement
        triggerCollectSound();
        bool repaired = (stats.paHealth > oldHealth && oldHealth < 1.0f);
        if (repaired) {
            triggerPaRepairSound();
        }
        
        if (tts && tts->isAvailable()) {
            int healthPct = static_cast<int>(stats.paHealth * 100.0f);
            if (repaired) {
                tts->speak(std::string("Collected: ") + target->character + 
                    ". PA repaired to " + std::to_string(healthPct) + "%.", false);
            } else {
                tts->speak(std::string("Collected: ") + target->character, false);
            }
        }
    } else {
        // Signal was aimed at but wrong character sent — staged feedback
        morseMissCount++;
        log("HAMSPIRIT_MORSE", std::string("Wrong character. Sent: ") + sentChar + 
            " Expected: " + target->character + " (miss count " + std::to_string(morseMissCount) + ")");
        triggerMissAimSound();
        if (tts && tts->isAvailable()) {
            std::string msg = std::string("Wrong. Expected: ") + target->character +
                              ". You sent: " + sentChar + ".";
            tts->speak(msg, false);
        }
        
        if (morseMissCount == 2) {
            // Teach: describe the correct pattern verbally
            if (morseDatabase && tts && tts->isAvailable()) {
                std::string pattern = morseDatabase->getPattern(target->character);
                std::string verbal;
                for (char p : pattern) {
                    if (!verbal.empty()) verbal += " ";
                    verbal += (p == '.') ? "dit" : "dah";
                }
                tts->speak("Correct code: " + verbal, false);
            }
        } else if (morseMissCount >= 3) {
            // Remove the signal with a penalty and disgust sound
            auto& signals = morseSignalManager->getSignalsMutable();
            for (auto it = signals.begin(); it != signals.end(); ++it) {
                if (&(*it) == target) { signals.erase(it); break; }
            }
            stats.score = std::max(0, stats.score - 20);
            std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
            if (lock.owns_lock()) {
                audioParams.missAimSoundFrames = msToFrames(200);  // longer buzz as “annoyed” cue
            }
            morseMissCount = 0;
        }
    }
}

// Check HAMSPIRIT bonus
void Game::checkHamSpiritBonus() {
    if (hamSpiritBonusAchieved) {
        return;
    }
    
    // Check if we have all letters for HAMSPIRIT
    std::string target = "HAMSPIRIT";
    std::vector<char> needed;
    
    for (char c : target) {
        bool found = false;
        for (char collected : collectedChars) {
            if (std::toupper(collected) == c) {
                found = true;
                break;
            }
        }
        if (found) {
            needed.push_back(c);
        }
    }
    
    if (needed.size() == target.size()) {
        hamSpiritBonusAchieved = true;
        stats.bonusAchieved = true;
        stats.score += 500;  // Big bonus!
        
        log("HAMSPIRIT_MORSE", "HAMSPIRIT bonus achieved!");
        
        if (tts && tts->isAvailable()) {
            tts->speak("BONUS! HAM SPIRIT completed!", false);
        }
    }
}

// Update noise enemies
void Game::updateNoiseEnemies(float dt) {
    if (!config.elemNoiseEnemies) { noiseEnemies.clear(); return; }  // Noise enemies disabled
    if (!config.noiseBlankerEnabled) return;
    
    // Decrease noise blanker cooldown
    if (noiseBlankerCooldown > 0.0f) noiseBlankerCooldown -= dt;
    
    // Process pending hit sound (delayed after fire sound for acoustic clarity)
    if (pendingHitTimer > 0.0f) {
        pendingHitTimer -= dt;
        if (pendingHitTimer <= 0.0f) {
            pendingHitTimer = 0.0f;
            if (pendingHitDestroyed) {
                triggerNoiseDestroyedSound();
            } else {
                triggerNoiseHitSound(pendingHitHealth);
            }
        }
    }
    
    // Remove destroyed/expired enemies
    for (auto& e : noiseEnemies) {
        if (e.destroyed) e.markedForRemoval = true;
    }
    removeExpiredEntities(noiseEnemies, stats.gameTime);
    
    // Pre-schedule: announce noise enemies before they spawn
    if (config.noiseAlerts && noiseScheduled && stats.gameTime >= nextNoiseAnnounceTime) {
        // Announce this upcoming noise
        TrackPoint tp = TrackGenerator::interpolateAt(track, scheduledNoiseAngle);
        float freqMHz = tp.frequency / 1e6f;
        announceUpcomingNoise(freqMHz);
        noiseScheduled = false;
    }
    
    // Spawn new noise enemies (pre-scheduled ones arrive)
    spawnNoiseEnemy(stats.gameTime);
    
    // Update noise enemy audio params — bandwidth determines audible range
    {
        std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
        if (lock.owns_lock()) {
            audioParams.noiseEnemies.clear();
            for (const auto& enemy : noiseEnemies) {
                if (enemy.destroyed) continue;
                // Calculate if player is within the enemy's bandwidth
                float angleDiff = (playerAngle + aimAngle) - enemy.angle;
                while (angleDiff > PI) angleDiff -= TWO_PI;
                while (angleDiff < -PI) angleDiff += TWO_PI;
                float absAngle = std::abs(angleDiff);
                float halfBw = enemy.bandwidth / 2.0f;
                // Audible if within bandwidth + hearing margin
                float hearingRange = halfBw + NOISE_ENEMY_HEARING_EXTRA;
                if (absAngle < hearingRange) {
                    AudioParams::NoiseEnemyAudio nea;
                    float halfPi = PI * 0.5f;
                    float panBase = angleDiff / std::max(halfBw, kMinBandwidthForPan);
                    float panValue = std::clamp(panBase, -1.0f, 1.0f);
                    if (absAngle > halfPi) {
                        float behindWeight = std::clamp((absAngle - halfPi) / halfPi, 0.0f, 1.0f);
                        float behindPan = std::sin(angleDiff);
                        panValue = (1.0f - behindWeight) * panValue + behindWeight * behindPan;
                    }
                    nea.pan = std::clamp(panValue - playerLateralOffset, -1.0f, 1.0f);
                    bool logBehind = absAngle > halfPi;
                    if (logBehind &&
                        stats.gameTime - lastNoisePanDebugTime > kNoisePanDebugThrottleSeconds) {
                        log("HAMSPIRIT_DEBUG", "Noise pan behind: diff=" + std::to_string(angleDiff) +
                            " abs=" + std::to_string(absAngle) +
                            " pan=" + std::to_string(nea.pan) +
                            " base=" + std::to_string(panBase) +
                            " bw=" + std::to_string(enemy.bandwidth) +
                            " aim=" + std::to_string(aimAngle) +
                            " offset=" + std::to_string(playerLateralOffset));
                        lastNoisePanDebugTime = stats.gameTime;
                    }
                    // Full volume inside bandwidth, fade outside
                    float insideFactor = (absAngle <= halfBw) ? 1.0f : 
                        std::max(0.0f, 1.0f - (absAngle - halfBw) / NOISE_ENEMY_FADE_RANGE);
                    nea.volume = static_cast<int>(NOISE_ENEMY_INSIDE_VOLUME * enemy.intensity * insideFactor);
                    nea.intensity = enemy.intensity;
                    audioParams.noiseEnemies.push_back(nea);
                }
            }
        }
    }
}

// Random spawn distance within 30-70% of maxDist (minimum 0.05 rad)
static float randomSpawnDistance(float maxDist) {
    float minDist = maxDist * SPAWN_DIST_MIN_FRACTION;
    float range = std::max(maxDist * SPAWN_DIST_RANGE_FRACTION, 0.01f);  // 70%-30% = 40% of max
    float dist = minDist + (static_cast<float>(rand()) / RAND_MAX) * range;
    return std::clamp(dist, SPAWN_DIST_ABSOLUTE_MIN, maxDist);
}

// Spawn noise enemy — pre-scheduled with traffic report announcement
void Game::spawnNoiseEnemy(float gameTime) {
    if (gameTime < nextNoiseSpawnTime) return;
    
    // Random bandwidth: 0.1 (narrow/weak) to 0.8 (wideband/strong)
    float bw = NOISE_ENEMY_BW_MIN + (static_cast<float>(rand()) / RAND_MAX) * NOISE_ENEMY_BW_RANGE;
    
    NoiseEnemy enemy;
    // Spawn within a reasonable distance ahead of a randomly selected player
    // (globalizes spawning for multiplayer)
    float referenceAngle = playerAngle;
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        int pc = multiplayerMgr->getPlayerCount();
        int chosenPlayer = rand() % pc;
        auto* ctx = multiplayerMgr->getPlayer(chosenPlayer);
        if (ctx) referenceAngle = ctx->playerAngle;
    }
    
    float maxSpawnDist = MAX_EVENT_SPAWN_DIST_POINTS * (TWO_PI / std::max(1.0f, static_cast<float>(track.size())));
    float spawnDist = randomSpawnDistance(maxSpawnDist);
    enemy.angle = referenceAngle + spawnDist;
    while (enemy.angle >= TWO_PI) enemy.angle -= TWO_PI;
    enemy.bandwidth = bw;
    enemy.spawnTime = gameTime;
    enemy.lifetime = NOISE_ENEMY_BASE_LIFETIME + bw * NOISE_ENEMY_BW_LIFETIME_SCALE;  // Wider = lasts longer
    // Health scales with bandwidth: wide = tough, narrow = fragile
    enemy.health = 1 + static_cast<int>(bw * NOISE_ENEMY_HEALTH_BW_SCALE) + (config.difficultyLevel / 3);
    enemy.intensity = NOISE_ENEMY_INTENSITY_BASE + bw * NOISE_ENEMY_INTENSITY_BW_SCALE;  // Wider = louder
    
    noiseEnemies.push_back(enemy);
    log("HAMSPIRIT", "Spawned noise enemy bw=" + std::to_string(bw) + 
        " health=" + std::to_string(enemy.health)
        + " dist=" + std::to_string(spawnDist)
        + " maxDist=" + std::to_string(maxSpawnDist)
        + " angle=" + std::to_string(enemy.angle));
    
    // Schedule next spawn — and pre-schedule the one after that for traffic report
    // Base interval 25s at diff 1, 15s at diff 5
    float baseInterval = NOISE_SPAWN_BASE_INTERVAL - config.difficultyLevel * NOISE_SPAWN_DIFFICULTY_REDUCTION;
    float interval = baseInterval + (static_cast<float>(rand()) / RAND_MAX) * NOISE_SPAWN_RANDOM_RANGE;
    nextNoiseSpawnTime = gameTime + interval;
    
    // Pre-schedule announcement 3 seconds before next spawn.
    // Use the same distance logic as the actual spawn so the announced
    // frequency matches what will actually appear on the track.
    if (config.noiseAlerts) {
        nextNoiseAnnounceTime = nextNoiseSpawnTime - NOISE_ANNOUNCE_LEAD_TIME;
        scheduledNoiseAngle = playerAngle + randomSpawnDistance(maxSpawnDist);
        while (scheduledNoiseAngle >= TWO_PI) scheduledNoiseAngle -= TWO_PI;
        noiseScheduled = true;
    }
}

// Spawn QSO Störer — aggressive interference that chases the player
void Game::spawnQSOStoerer() {
    if (qsoStoerer.active) return;
    
    // Spawn ahead of a randomly selected player (globalizes spawning)
    float referenceAngle = playerAngle;
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        int pc = multiplayerMgr->getPlayerCount();
        int chosenPlayer = rand() % pc;
        auto* ctx = multiplayerMgr->getPlayer(chosenPlayer);
        if (ctx) referenceAngle = ctx->playerAngle;
    }
    
    // Spawn 2-4 measurement points ahead
    float spawnDist = (static_cast<float>(QSO_STOERER_SPAWN_DIST_MIN) + static_cast<float>(rand() % QSO_STOERER_SPAWN_DIST_RANGE)) * (TWO_PI / std::max(1.0f, static_cast<float>(track.size())));
    qsoStoerer.angle = referenceAngle + spawnDist;
    while (qsoStoerer.angle >= TWO_PI) qsoStoerer.angle -= TWO_PI;
    qsoStoerer.speed = 0.0f;
    qsoStoerer.lateralOffset = 0.0f;  // Start centered on track
    qsoStoerer.active = true;
    qsoStoerer.health = 1.0f;
    qsoStoerer.spawnTime = stats.gameTime;
    qsoStoerer.lastCollisionTime = -10.0f;
    qsoStoerer.hornissPhase = 0.0f;
    
    if (config.intruderMonitoring && tts && tts->isAvailable()) {
        speakTranslated("HAMSPIRIT_QSO_STOERER_DETECTED", "Intruder Monitoring: QSO interferer detected ahead!", false);
        pushBannerText("Intruder Monitoring: QSO interferer detected ahead!");
    }
    log("HAMSPIRIT", "QSO Störer spawned at angle=" + std::to_string(qsoStoerer.angle));
}

// Trigger QSO Störer collision sound
void Game::triggerQSOStoererCollisionSound() {
    audioParams.qsoStoererCollisionFrames = msToFrames(800);
}

// Update QSO Störer behavior
void Game::updateQSOStoerer(float dt) {
    if (!config.elemQsoStoerer) { qsoStoerer.active = false; return; }  // QSO Störer disabled
    // Check spawn timer
    if (!qsoStoerer.active) {
        if (stats.gameTime >= nextQSOStoererSpawnTime) {
            spawnQSOStoerer();
            float baseInterval = QSO_STOERER_BASE_INTERVAL - config.difficultyLevel * QSO_STOERER_DIFFICULTY_FACTOR;
            baseInterval = std::max(baseInterval, QSO_STOERER_MIN_INTERVAL);
            nextQSOStoererSpawnTime = stats.gameTime + baseInterval + static_cast<float>(rand() % QSO_STOERER_RESPAWN_RANDOM);
        }
        {
            std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
            if (lock.owns_lock()) audioParams.qsoStoererActive = false;
        }
        return;
    }
    
    // Calculate angular distance to player (positive = Störer is ahead)
    float diff = qsoStoerer.angle - playerAngle;
    while (diff > PI) diff -= TWO_PI;
    while (diff < -PI) diff += TWO_PI;
    
    float absDiff = std::abs(diff);
    float stepAngle = TWO_PI / std::max(1.0f, static_cast<float>(track.size()));
    
    // Give up if more than 1.5 measurement points behind player (was 2 — easier to shake off)
    if (diff < QSO_STOERER_GIVE_UP_FACTOR * stepAngle && absDiff > stepAngle) {
        qsoStoerer.active = false;
        {
            std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
            if (lock.owns_lock()) audioParams.qsoStoererActive = false;
        }
        log("HAMSPIRIT", "QSO Störer gave up (behind player)");
        return;
    }
    
    // Chase behavior: always active when spawned (Störer is aggressive)
    {
        // Speed scales with health: full health = 110% player speed, damaged = slower
        // Minimum speed ensures Störer always moves even when player is standing still
        float minStoererSpeed = maxSpeed * QSO_STOERER_MIN_SPEED_FRACTION;  // Minimum 15% of max speed
        float targetSpeed = std::max(std::abs(playerSpeed) * QSO_STOERER_SPEED_MULTIPLIER * qsoStoerer.health, minStoererSpeed);
        
        // Driving error: Störer swerves and slows down, creating overtaking opportunity
        if (qsoStoerer.drivingErrorTimer > 0.0f) {
            qsoStoerer.drivingErrorTimer -= dt;
            // During error: only 40% speed, no lateral tracking
            targetSpeed *= QSO_STOERER_ERROR_SPEED_MULT;
            qsoStoerer.speed = std::min(qsoStoerer.speed, targetSpeed);
            // Wobble laterally (simulating swerving/loss of control)
            qsoStoerer.lateralOffset += std::sin(stats.gameTime * QSO_STOERER_WOBBLE_FREQ) * QSO_STOERER_WOBBLE_AMP * dt;
            qsoStoerer.lateralOffset = std::clamp(qsoStoerer.lateralOffset, -1.0f, 1.0f);
        } else if (diff > stepAngle) {
            // Störer is well ahead — slow down to let player catch up
            qsoStoerer.speed = std::min(targetSpeed, std::abs(playerSpeed) * QSO_STOERER_AHEAD_SPEED_MULT);
        } else if (diff > 0.0f) {
            // Störer is slightly ahead — match player speed to block
            qsoStoerer.speed = std::abs(playerSpeed);
        } else {
            // Störer is behind — chase at full target speed to catch up
            qsoStoerer.speed = targetSpeed;
        }
        
        // Move forward along track
        qsoStoerer.angle += qsoStoerer.speed * dt;
        while (qsoStoerer.angle >= TWO_PI) qsoStoerer.angle -= TWO_PI;
        while (qsoStoerer.angle < 0.0f) qsoStoerer.angle += TWO_PI;
        
        // Lateral movement: Störer drifts toward player's lateral position to block
        // (only when NOT in driving error — error overrides lateral behavior above)
        if (qsoStoerer.drivingErrorTimer <= 0.0f) {
            float lateralDiff = playerLateralOffset - qsoStoerer.lateralOffset;
            qsoStoerer.lateralOffset += lateralDiff * QSO_STOERER_LATERAL_SPEED * dt;
            qsoStoerer.lateralOffset = std::clamp(qsoStoerer.lateralOffset, -1.0f, 1.0f);
        }
    }
    
    // Collision detection — requires both angular AND lateral proximity
    float collisionThreshold = stepAngle * 0.5f;
    float lateralDist = std::abs(playerLateralOffset - qsoStoerer.lateralOffset);
    bool angularClose = absDiff < collisionThreshold;
    bool lateralClose = lateralDist < QSO_STOERER_LATERAL_DODGE;
    
    if (angularClose && lateralClose && 
        (stats.gameTime - qsoStoerer.lastCollisionTime) > QSO_STOERER_COLLISION_COOLDOWN) {
        stats.paHealth = std::max(0.0f, stats.paHealth - QSO_STOERER_COLLISION_DAMAGE);
        qsoStoerer.lastCollisionTime = stats.gameTime;
        {
            std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
            if (lock.owns_lock()) triggerQSOStoererCollisionSound();
        }
        if (tts && tts->isAvailable()) {
            speakTranslated("HAMSPIRIT_QSO_STOERER_COLLISION", "QSO interferer collision!", false);
        }
        // Störer collision rumble — distinct from barrier crash
        crashVibrationTimer = QSO_STOERER_COLLISION_VIB_DURATION;  // 300ms rumble for vehicle-to-vehicle contact
        log("HAMSPIRIT", "QSO Störer collision! PA health=" + std::to_string(stats.paHealth));
    }
    
    // Check destruction threshold (health reduced by noise blanker hits)
    if (qsoStoerer.health <= QSO_STOERER_DESTRUCTION_THRESHOLD) {
        qsoStoerer.active = false;
        {
            std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
            if (lock.owns_lock()) audioParams.qsoStoererActive = false;
        }
        stats.score += static_cast<int>(QSO_STOERER_DESTROY_SCORE);
        if (tts && tts->isAvailable()) {
            speakTranslated("HAMSPIRIT_QSO_STOERER_HIT", "Interferer neutralized!", false);
        }
        log("HAMSPIRIT", "QSO Störer destroyed by accumulated hits");
        return;
    }
    
    // Update audio params (mutex-protected)
    // Panning: compute angular difference between Störer and the player's AIM direction
    // so the player can hear where the Störer is relative to their weapon crosshair
    float aimDirection = playerAngle + aimAngle;
    float aimDiff = qsoStoerer.angle - aimDirection;
    while (aimDiff > PI) aimDiff -= TWO_PI;
    while (aimDiff < -PI) aimDiff += TWO_PI;
    float angularPan = std::clamp(aimDiff * QSO_STOERER_PAN_AMPLIFICATION, -1.0f, 1.0f);  // 4x amplification for strong panning
    float lateralPan = std::clamp((qsoStoerer.lateralOffset - playerLateralOffset) * 2.0f, -1.0f, 1.0f);
    // Blend: at close range, lateral panning dominates; far away, angular dominates
    float closeness = std::max(0.0f, 1.0f - absDiff / (3.0f * stepAngle));
    float panValue = angularPan * (1.0f - closeness * 0.5f) + lateralPan * closeness * 0.5f;
    panValue = std::clamp(panValue, -1.0f, 1.0f);
    
    // Volume: louder when closer, max 70 at point-blank
    int volume = static_cast<int>(QSO_STOERER_VOLUME_MAX * std::max(0.0f, 1.0f - absDiff / (4.0f * stepAngle)));
    
    // Buzz frequency: modulated hornet sound, faster when health is lower (angrier)
    float buzzMod = 1.0f + (1.0f - qsoStoerer.health) * 2.0f;  // Angrier when damaged
    qsoStoerer.hornissPhase += dt * TWO_PI * (200.0f + 40.0f * buzzMod * std::sin(stats.gameTime * 3.0f));
    while (qsoStoerer.hornissPhase >= TWO_PI) qsoStoerer.hornissPhase -= TWO_PI;
    float buzzFreq = 180.0f + 100.0f * (0.5f + 0.5f * std::sin(stats.gameTime * 5.0f * buzzMod));
    
    {
        std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
        if (lock.owns_lock()) {
            audioParams.qsoStoererActive = true;
            audioParams.qsoStoererPan = panValue;
            audioParams.qsoStoererVolume = volume;
            audioParams.qsoStoererBuzzFreq = buzzFreq;
            audioParams.qsoStoererBehind = (diff < 0.0f);  // Behind when angular diff is negative
        }
    }
    
    // Position tracking: announce transitions (ahead/alongside/behind)
    // and trigger psychoacoustic overtake sweep sound
    {
        int newState = 0;
        float stepAngleSafe = std::max(stepAngle, 0.01f);
        if (absDiff < stepAngleSafe * QSO_STOERER_ALONGSIDE_THRESHOLD) {
            newState = 2;  // alongside
        } else if (diff > 0.0f) {
            newState = 1;  // ahead
        } else {
            newState = 3;  // behind
        }
        
        if (newState != qsoStoerer.positionState && qsoStoerer.positionState != 0) {
            // Position changed — announce via TTS
            if (tts && tts->isAvailable() && announceCooldown <= 0.0f) {
                if (newState == 1) {
                    speakTranslated("HAMSPIRIT_QSO_STOERER_AHEAD", "Ahead!", false);
                } else if (newState == 2) {
                    speakTranslated("HAMSPIRIT_QSO_STOERER_ALONGSIDE", "Alongside!", false);
                } else if (newState == 3) {
                    speakTranslated("HAMSPIRIT_QSO_STOERER_BEHIND", "Behind!", false);
                }
                announceCooldown = QSO_STOERER_ANNOUNCE_COOLDOWN;
            }
            // Overtake transition: player passes Störer (was ahead, now behind)
            // Trigger psychoacoustic sweep sound
            if ((qsoStoerer.positionState == 1 && newState == 3) ||
                (qsoStoerer.positionState == 1 && newState == 2) ||
                (qsoStoerer.positionState == 2 && newState == 3)) {
                std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
                if (lock.owns_lock()) {
                    audioParams.qsoStoererOvertakeFrames = msToFrames(300);  // ~300ms sweep
                }
            }
        }
        qsoStoerer.positionState = newState;
    }
}

// ===== POWER-UP SYSTEM =====

// Helper: get human-readable power-up type name
static std::string powerUpTypeName(PowerUpType type, bool isGerman) {
    switch (type) {
        case PowerUpType::SPEED_BOOST:    return isGerman ? "Geschwindigkeitsboost" : "Speed boost";
        case PowerUpType::FIRE_RATE:      return isGerman ? "Feuerrate" : "Fire rate";
        case PowerUpType::AUTO_FIRE:      return isGerman ? "Automatisches Feuer" : "Auto fire";
        case PowerUpType::SWR_IMMUNITY:   return isGerman ? "SWR Immunität" : "SWR immunity";
        case PowerUpType::DURATION_EXTEND:return isGerman ? "Zeitverlängerung" : "Duration extend";
        default:                          return isGerman ? "Power-Up" : "Power-up";
    }
}

int Game::findPowerUpByUid(uint32_t uid) const {
    for (size_t i = 0; i < powerUps.size(); i++) {
        if (powerUps[i].uid == uid) return static_cast<int>(i);
    }
    return -1;
}

void Game::spawnPowerUp() {
    PowerUp pu;
    // Random type (balanced distribution)
    int roll = rand() % 100;
    if (roll < POWERUP_CHANCE_SPEED_BOOST) pu.type = PowerUpType::SPEED_BOOST;
    else if (roll < POWERUP_CHANCE_FIRE_RATE) pu.type = PowerUpType::FIRE_RATE;
    else if (roll < POWERUP_CHANCE_AUTO_FIRE) pu.type = PowerUpType::AUTO_FIRE;
    else if (roll < POWERUP_CHANCE_SWR_IMMUNITY) pu.type = PowerUpType::SWR_IMMUNITY;
    else pu.type = PowerUpType::DURATION_EXTEND;
    
    // Quality tier (1-3, higher = better effect but longer collection)
    pu.quality = 1 + (rand() % 3);
    
    // Collection time based on quality
    switch (pu.quality) {
        case 1: pu.collectionTime = POWERUP_COLLECT_TIME_Q1; break;
        case 2: pu.collectionTime = POWERUP_COLLECT_TIME_Q2; break;
        case 3: pu.collectionTime = POWERUP_COLLECT_TIME_Q3; break;
        default: pu.collectionTime = POWERUP_COLLECT_TIME_Q1; break;
    }
    
    // Spawn ahead of a randomly selected player (globalizes spawning for multiplayer)
    float referenceAngle = playerAngle;
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        int pc = multiplayerMgr->getPlayerCount();
        int chosenPlayer = rand() % pc;
        auto* ctx = multiplayerMgr->getPlayer(chosenPlayer);
        if (ctx) referenceAngle = ctx->playerAngle;
    }
    
    float maxSpawnDist = MAX_EVENT_SPAWN_DIST_POINTS * (TWO_PI / std::max(1.0f, static_cast<float>(track.size())));
    float spawnDist = std::clamp(POWERUP_SPAWN_DISTANCE, SPAWN_DIST_ABSOLUTE_MIN, maxSpawnDist) + (rand() % 100) * 0.001f;
    pu.angle = referenceAngle + spawnDist;
    while (pu.angle >= TWO_PI) pu.angle -= TWO_PI;
    pu.zoneHalfWidth = POWERUP_ZONE_HALF_WIDTH;
    pu.spawnTime = stats.gameTime;
    pu.lifetime = POWERUP_LIFETIME;
    pu.collected = false;
    pu.destroyed = false;
    pu.collectionProgress = 0.0f;
    pu.uid = nextPowerUpUid++;
    
    powerUps.push_back(pu);
    log("HAMSPIRIT", "Power-up spawned: type=" + std::to_string(static_cast<int>(pu.type)) 
        + " quality=" + std::to_string(pu.quality)
        + " uid=" + std::to_string(pu.uid)
        + " angle=" + std::to_string(pu.angle)
        + " collectTime=" + std::to_string(pu.collectionTime)
        + " zoneHW=" + std::to_string(pu.zoneHalfWidth)
        + " playerAngle=" + std::to_string(playerAngle));
}

void Game::updatePowerUps(float dt) {
    if (!config.elemPowerUps) { powerUps.clear(); return; }  // Power-ups disabled
    // Spawn new power-ups periodically (respect max concurrent limit)
    nextPowerUpSpawnTime -= dt;
    if (nextPowerUpSpawnTime <= 0.0f) {
        int activeCount = 0;
        for (const auto& pu : powerUps) {
            if (!pu.collected && !pu.destroyed) activeCount++;
        }
        if (activeCount < POWERUP_MAX_CONCURRENT) {
            spawnPowerUp();
        }
        nextPowerUpSpawnTime = POWERUP_SPAWN_INTERVAL_MIN + 
            (rand() % static_cast<int>((POWERUP_SPAWN_INTERVAL_MAX - POWERUP_SPAWN_INTERVAL_MIN) * 10)) * 0.1f;
    }
    
    // Remove expired/collected/destroyed power-ups
    for (auto& pu : powerUps) {
        if (pu.collected || pu.destroyed) pu.markedForRemoval = true;
    }
    removeExpiredEntities(powerUps, stats.gameTime);
    
    // Update power-up zone panning (relative to player aim, like morse signals)
    for (auto& pu : powerUps) {
        float angleDiff = pu.angle - (playerAngle + aimAngle);
        while (angleDiff > PI) angleDiff -= TWO_PI;
        while (angleDiff < -PI) angleDiff += TWO_PI;
        pu.panPosition = std::clamp(0.5f + angleDiff * 2.0f, 0.0f, 1.0f);
    }
    
    // Update audio params for power-up zones
    {
        std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
        if (lock.owns_lock()) {
            audioParams.powerUpZones.resize(powerUps.size());
            for (size_t i = 0; i < powerUps.size(); i++) {
                auto& pu = powerUps[i];
                auto& pa = audioParams.powerUpZones[i];
                
                // Calculate distance from player to power-up center
                float dist = pu.angle - playerAngle;
                while (dist > PI) dist -= TWO_PI;
                while (dist < -PI) dist += TWO_PI;
                float absDist = std::abs(dist);
                
                // Zone depth: 1.0 at center, 0.0 at edge, negative outside
                if (absDist < pu.zoneHalfWidth) {
                    pa.inZone = true;
                    pa.zoneDepth = 1.0f - (absDist / pu.zoneHalfWidth);
                    pa.volume = static_cast<int>(40 + 60 * pa.zoneDepth);
                } else {
                    pa.inZone = false;
                    pa.zoneDepth = 0.0f;
                    // Audible up to POWERUP_HEARING_RANGE_MULT * zone half-width away.
                    // Squared falloff gives a smoother volume approach curve so
                    // the player can better judge how far the power-up still is.
                    float hearingRange = pu.zoneHalfWidth * POWERUP_HEARING_RANGE_MULT;
                    if (absDist < hearingRange) {
                        float t = 1.0f - (absDist - pu.zoneHalfWidth) / (hearingRange - pu.zoneHalfWidth);
                        float fadeFactor = t * t;  // Squared: volume rises steeply as you get close
                        pa.volume = static_cast<int>(50 * fadeFactor);  // Up to 50 at zone edge
                    } else {
                        pa.volume = 0;
                    }
                }
                pa.pan = pu.panPosition;
                pa.type = pu.type;
            }
        }
    }
    
    // Update active power-up timers
    for (auto it = activePowerUps.begin(); it != activePowerUps.end(); ) {
        it->remainingTime -= dt;
        if (it->remainingTime <= 0.0f) {
            // Power-up expired — deactivate and restore state
            size_t idx = it - activePowerUps.begin();
            deactivatePowerUp(idx);
            it = activePowerUps.erase(it);
        } else {
            // Warn when 5 seconds remaining
            if (it->remainingTime < POWERUP_EXPIRE_WARNING_TIME && it->remainingTime + dt >= POWERUP_EXPIRE_WARNING_TIME) {
                if (tts && tts->isAvailable()) {
                    bool isGerman = translation && 
                        (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
                    std::string typeName = powerUpTypeName(it->type, isGerman);
                    std::string msg = isGerman 
                        ? (typeName + " läuft in 5 Sekunden ab.")
                        : (typeName + " expires in 5 seconds.");
                    tts->speak(msg, false);
                }
            }
            ++it;
        }
    }
}

void Game::handlePowerUpCollection(const GamepadState& input, float dt) {
    float ltValue = input.axes[static_cast<int>(GamepadAxis::LEFT_TRIGGER)];
    float rtValue = input.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)];
    // Both triggers held: works for both gamepad (analog triggers) and keyboard
    // (SPACE=RT and F=LT are discrete keys that the emulator maps to trigger axes).
    bool bothHeld = ltValue > POWERUP_TRIGGER_THRESHOLD && rtValue > POWERUP_TRIGGER_THRESHOLD;
    
    // Throttled debug logging for trigger state
    static float lastTriggerLogTime = -10.0f;
    bool triggerStateChanged = (bothHeld != prevBothTriggersHeld);
    if (triggerStateChanged || (bothHeld && stats.gameTime - lastTriggerLogTime > 2.0f)) {
        log("HAMSPIRIT_DEBUG", "PU triggers: LT=" + std::to_string(ltValue) 
            + " RT=" + std::to_string(rtValue) 
            + " bothHeld=" + std::to_string(bothHeld)
            + " holdTime=" + std::to_string(powerUpTriggerHoldTime)
            + " threshold=" + std::to_string(POWERUP_TRIGGER_THRESHOLD)
            + " powerUps.size=" + std::to_string(powerUps.size())
            + " playerAngle=" + std::to_string(playerAngle)
            + " aimAngle=" + std::to_string(aimAngle)
            + " cachedAimLock=" + std::to_string(cachedAimLockPowerUp));
        lastTriggerLogTime = stats.gameTime;
    }
    
    // Debounce: require both triggers to be held together for a short grace period
    if (bothHeld) {
        powerUpTriggerHoldTime += dt;
    } else {
        powerUpTriggerHoldTime = 0.0f;
    }
    
    // Decrement "not aimed" cooldown
    if (powerUpNoAimCooldown > 0.0f) {
        powerUpNoAimCooldown -= dt;
    }
    
    if (bothHeld && powerUpTriggerHoldTime >= 0.15f) {
        // Find targeted power-up (same aim mechanics as morse signals)
        int targetIdx = -1;
        float bestDist = 999.0f;
        float aimDirection = playerAngle + aimAngle;
        
        // Debug: log all power-ups and their distances (throttled)
        static float lastSearchLogTime = -10.0f;
        bool doSearchLog = (stats.gameTime - lastSearchLogTime > 1.0f);
        if (doSearchLog) {
            log("HAMSPIRIT_DEBUG", "PU search: aimDir=" + std::to_string(aimDirection)
                + " playerAngle=" + std::to_string(playerAngle)
                + " aimAngle=" + std::to_string(aimAngle)
                + " AIM_MARGIN=" + std::to_string(POWERUP_AIM_MARGIN)
                + " count=" + std::to_string(powerUps.size()));
        }
        
        for (size_t i = 0; i < powerUps.size(); i++) {
            if (powerUps[i].collected || powerUps[i].destroyed) continue;
            float angleDiff = powerUps[i].angle - aimDirection;
            while (angleDiff > PI) angleDiff -= TWO_PI;
            while (angleDiff < -PI) angleDiff += TWO_PI;
            float dist = std::abs(angleDiff);
            
            if (doSearchLog) {
                log("HAMSPIRIT_DEBUG", "  PU[" + std::to_string(i) + "] uid=" + std::to_string(powerUps[i].uid)
                    + " angle=" + std::to_string(powerUps[i].angle)
                    + " dist=" + std::to_string(dist)
                    + " zoneHW=" + std::to_string(powerUps[i].zoneHalfWidth)
                    + " inAimMargin=" + std::to_string(dist < POWERUP_AIM_MARGIN)
                    + " zoneDist=" + std::to_string(std::abs(powerUps[i].angle - playerAngle)));
            }
            
            if (dist < POWERUP_AIM_MARGIN && dist < bestDist) {
                bestDist = dist;
                targetIdx = static_cast<int>(i);
            }
        }
        if (doSearchLog) {
            lastSearchLogTime = stats.gameTime;
            log("HAMSPIRIT_DEBUG", "PU search result: targetIdx=" + std::to_string(targetIdx)
                + " bestDist=" + std::to_string(bestDist));
        }
        
        if (targetIdx >= 0) {
            // Check if player is within the zone
            float zoneDist = powerUps[targetIdx].angle - playerAngle;
            while (zoneDist > PI) zoneDist -= TWO_PI;
            while (zoneDist < -PI) zoneDist += TWO_PI;
            float absZoneDist = std::abs(zoneDist);
            bool inZone = absZoneDist < powerUps[targetIdx].zoneHalfWidth;
            
            if (inZone) {
                // Use UID-based tracking to survive vector index shifts
                uint32_t targetUid = powerUps[targetIdx].uid;
                if (powerUpCollectTargetUid != targetUid) {
                    // Switched target — reset progress
                    powerUpCollectTimer = 0.0f;
                    powerUpCollectTargetUid = targetUid;
                    powerUpCollectLastCountdown = -1;
                    log("HAMSPIRIT", "PU collection: NEW target UID=" + std::to_string(targetUid)
                        + " type=" + std::to_string(static_cast<int>(powerUps[targetIdx].type))
                        + " quality=" + std::to_string(powerUps[targetIdx].quality)
                        + " collectTime=" + std::to_string(powerUps[targetIdx].collectionTime)
                        + " zoneDist=" + std::to_string(absZoneDist));
                    // TTS: announce what the player is collecting
                    if (tts && tts->isAvailable()) {
                        bool isGerman = translation && 
                            (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
                        std::string typeName = powerUpTypeName(powerUps[targetIdx].type, isGerman);
                        int remainSec = static_cast<int>(std::ceil(powerUps[targetIdx].collectionTime));
                        std::string secWord = (isGerman ? (remainSec == 1 ? "Sekunde" : "Sekunden")
                                                        : (remainSec == 1 ? "second" : "seconds"));
                        std::string msg = isGerman 
                            ? (typeName + ", " + std::to_string(remainSec) + " " + secWord + " halten.") 
                            : (typeName + ", hold " + std::to_string(remainSec) + " " + secWord + ".");
                        tts->stop();
                        tts->speak(msg, false);
                    }
                }
                
                // Advance collection timer while in zone + aimed + both triggers held.
                // Better aim reduces collection time: perfect aim (1.0) = 50% time.
                float currentAimLock = cachedAimLockPowerUp;
                float aimBonus = std::clamp(currentAimLock, 0.0f, 1.0f);
                // aimBonus in [0,1] → multiplier in [1.0, 0.5]: better aim = less time
                float aimTimeMultiplier = 1.0f - 0.5f * aimBonus;
                
                powerUpCollectTimer += dt;
                float baseCollectTime = std::clamp(powerUps[targetIdx].collectionTime, POWERUP_COLLECT_TIME_MIN, POWERUP_COLLECT_TIME_MAX);
                float effectiveCollectTime = baseCollectTime * aimTimeMultiplier;
                float progress = powerUpCollectTimer / effectiveCollectTime;
                progress = std::clamp(progress, 0.0f, 1.0f);
                powerUps[targetIdx].collectionProgress = progress;
                
                // Debug log every ~0.25 seconds during collection
                static float lastCollectLogTime = -1.0f;
                if (stats.gameTime - lastCollectLogTime > 0.25f) {
                    log("HAMSPIRIT_DEBUG", "PU COLLECTING: timer=" + std::to_string(powerUpCollectTimer) 
                        + "/" + std::to_string(effectiveCollectTime)
                        + " progress=" + std::to_string(progress)
                        + " aimLock=" + std::to_string(currentAimLock)
                        + " aimMult=" + std::to_string(aimTimeMultiplier)
                        + " baseCT=" + std::to_string(baseCollectTime)
                        + " dt=" + std::to_string(dt)
                        + " UID=" + std::to_string(powerUpCollectTargetUid));
                    lastCollectLogTime = stats.gameTime;
                }
                
                // TTS countdown: announce remaining seconds (interrupt to ensure audibility)
                float remaining = effectiveCollectTime - powerUpCollectTimer;
                int remainingSec = static_cast<int>(std::ceil(remaining));
                if (remainingSec > 0 && remainingSec != powerUpCollectLastCountdown && remainingSec <= 4) {
                    powerUpCollectLastCountdown = remainingSec;
                    log("HAMSPIRIT", "PU countdown: " + std::to_string(remainingSec) + "s remaining");
                    if (tts && tts->isAvailable()) {
                        tts->stop();
                        tts->speak(std::to_string(remainingSec), false);
                    }
                }
                
                // Gradual vibration: intensity increases with progress
                if (config.swrVibration) {
                    float vibIntensity = progress * config.vibrationIntensity;
                    setVibrationForPlayer(0, vibIntensity * 0.3f, vibIntensity * 0.8f);
                }
                
                // Update audio for collection progress (blocking lock)
                {
                    std::lock_guard<std::mutex> lock(audioStateMtx);
                    audioParams.powerUpCollecting = true;
                    audioParams.powerUpCollectProgress = progress;
                }
                
                if (progress >= 1.0f) {
                    // Collection complete!
                    log("HAMSPIRIT", "PU COLLECTED! UID=" + std::to_string(targetUid)
                        + " type=" + std::to_string(static_cast<int>(powerUps[targetIdx].type))
                        + " quality=" + std::to_string(powerUps[targetIdx].quality)
                        + " totalTime=" + std::to_string(powerUpCollectTimer));
                    powerUps[targetIdx].collected = true;
                    activatePowerUp(powerUps[targetIdx].type, powerUps[targetIdx].quality);
                    powerUpCollectTimer = 0.0f;
                    powerUpCollectTargetUid = 0;
                    powerUpCollectLastCountdown = -1;
                    setVibrationForPlayer(0, 0.0f, 0.0f);
                    if (tts && tts->isAvailable()) {
                        tts->stop();
                        bool isGerman = translation && 
                            (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
                        std::string typeName = powerUpTypeName(powerUps[targetIdx].type, isGerman);
                        std::string msg = isGerman 
                            ? (typeName + " eingesammelt!") 
                            : (typeName + " collected!");
                        tts->speak(msg, false);
                    }
                    {
                        std::lock_guard<std::mutex> lock(audioStateMtx);
                        audioParams.powerUpCollecting = false;
                        audioParams.powerUpCollectProgress = 0.0f;
                    }
                }
            }
            else {
                // Aimed power-up but not inside its zone — PAUSE collection, stop sound
                static float lastOutZoneLogTime = -10.0f;
                if (stats.gameTime - lastOutZoneLogTime > 1.0f) {
                    log("HAMSPIRIT_DEBUG", "PU aimed but OUT OF ZONE: zoneDist=" + std::to_string(absZoneDist)
                        + " zoneHW=" + std::to_string(powerUps[targetIdx].zoneHalfWidth)
                        + " collectUID=" + std::to_string(powerUpCollectTargetUid));
                    lastOutZoneLogTime = stats.gameTime;
                }
                // Stop collection sound while out of zone (timer stays paused, not reset)
                {
                    std::lock_guard<std::mutex> lock(audioStateMtx);
                    audioParams.powerUpCollecting = false;
                    // Keep powerUpCollectProgress as-is so it resumes at the right pitch
                }
                // Stop vibration while paused
                setVibrationForPlayer(0, 0.0f, 0.0f);
                // TTS: inform player to drive closer (throttled)
                static float lastOutOfRangeTtsTime = -99.0f;
                if (tts && tts->isAvailable() && (stats.gameTime - lastOutOfRangeTtsTime > POWERUP_OUT_OF_RANGE_TTS_COOLDOWN)) {
                    lastOutOfRangeTtsTime = stats.gameTime;
                    bool isGerman = translation && 
                        (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
                    std::string typeName = powerUpTypeName(powerUps[targetIdx].type, isGerman);
                    std::string msg = isGerman 
                        ? (typeName + " voraus. Näher heranfahren!") 
                        : (typeName + " ahead. Drive closer!");
                    tts->speak(msg, false);
                }
            }
        } else {
            // No target found — reset collection
            if (powerUpCollectTargetUid != 0) {
                log("HAMSPIRIT", "PU collection RESET: no target found (was UID=" 
                    + std::to_string(powerUpCollectTargetUid) + ")");
                int prevIdx = findPowerUpByUid(powerUpCollectTargetUid);
                if (prevIdx >= 0 && static_cast<size_t>(prevIdx) < powerUps.size()) {
                    powerUps[prevIdx].collectionProgress = 0.0f;
                }
            }
            powerUpCollectTimer = 0.0f;
            powerUpCollectTargetUid = 0;
            powerUpCollectLastCountdown = -1;
            setVibrationForPlayer(0, 0.0f, 0.0f);
            {
                std::lock_guard<std::mutex> lock(audioStateMtx);
                audioParams.powerUpCollecting = false;
                audioParams.powerUpCollectProgress = 0.0f;
            }
        }
    } else {
        // Triggers released or debounce not met — reset
        if (powerUpCollectTargetUid != 0) {
            log("HAMSPIRIT", "PU collection RESET: triggers released (was UID=" 
                + std::to_string(powerUpCollectTargetUid)
                + " timer=" + std::to_string(powerUpCollectTimer) + ")");
            int prevIdx = findPowerUpByUid(powerUpCollectTargetUid);
            if (prevIdx >= 0 && static_cast<size_t>(prevIdx) < powerUps.size()) {
                powerUps[prevIdx].collectionProgress = 0.0f;
            }
        }
        powerUpCollectTimer = 0.0f;
        powerUpCollectTargetUid = 0;
        powerUpCollectLastCountdown = -1;
        setVibrationForPlayer(0, 0.0f, 0.0f);
        {
            std::lock_guard<std::mutex> lock(audioStateMtx);
            audioParams.powerUpCollecting = false;
            audioParams.powerUpCollectProgress = 0.0f;
        }
    }
    prevBothTriggersHeld = bothHeld;
}

void Game::activatePowerUp(PowerUpType type, int quality) {
    bool isGerman = translation && 
        (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
    float duration = powerUpBaseDuration + powerUpDurationBonus;
    std::string msg;
    
    // Check if a power-up of the same type is already active — if so, extend its timer
    for (auto& ap : activePowerUps) {
        if (ap.type == type) {
            ap.remainingTime += duration;
            msg = isGerman 
                ? "Power-up verlängert!"
                : "Power-up extended!";
            triggerPowerUpActivateSound(type);
            stats.score += POWERUP_ACTIVATION_SCORE_PER_QUALITY * quality;
            if (tts && tts->isAvailable() && !msg.empty()) {
                tts->speak(msg, false);
            }
            log("HAMSPIRIT", "Power-up extended: type=" + std::to_string(static_cast<int>(type)) + 
                " quality=" + std::to_string(quality) + " newRemaining=" + std::to_string(ap.remainingTime));
            return;
        }
    }
    
    switch (type) {
        case PowerUpType::SPEED_BOOST: {
            float boost = POWERUP_SPEED_BOOST_FACTOR + (quality - 1) * POWERUP_SPEED_BOOST_QUALITY_STEP;
            ActivePowerUp ap(type, duration, baseMaxSpeed);  // Save baseMaxSpeed (not maxSpeed) for correct restoration
            activePowerUps.push_back(ap);
            savedMaxSpeedBeforeBoost = baseMaxSpeed;
            baseMaxSpeed *= boost;
            msg = isGerman ? "Geschwindigkeitsboost aktiviert!" : "Speed boost activated!";
            break;
        }
        case PowerUpType::FIRE_RATE: {
            float factor = POWERUP_FIRE_RATE_FACTOR - (quality - 1) * POWERUP_FIRE_RATE_QUALITY_STEP;
            factor = std::max(factor, POWERUP_FIRE_RATE_MIN);
            ActivePowerUp ap(type, duration, NOISE_BLANKER_COOLDOWN);
            activePowerUps.push_back(ap);
            savedCooldownBeforeBoost = NOISE_BLANKER_COOLDOWN;
            // The cooldown is a constexpr, so we apply the factor when checking
            msg = isGerman ? "Feuerrate erhöht!" : "Fire rate increased!";
            break;
        }
        case PowerUpType::AUTO_FIRE: {
            ActivePowerUp ap(type, duration);
            activePowerUps.push_back(ap);
            autoFireActive = true;
            msg = isGerman ? "Automatisches Feuer aktiviert!" : "Auto fire activated!";
            break;
        }
        case PowerUpType::SWR_IMMUNITY: {
            ActivePowerUp ap(type, duration);
            activePowerUps.push_back(ap);
            swrImmunityActive = true;
            msg = isGerman ? "SWR Immunität aktiviert! Endstufe geschützt." : "SWR immunity activated! PA protected.";
            break;
        }
        case PowerUpType::DURATION_EXTEND: {
            powerUpDurationBonus += POWERUP_DURATION_EXTENSION;
            // This is permanent — no timer needed
            msg = isGerman 
                ? ("Power-up Dauer permanent erhöht auf " + std::to_string(static_cast<int>(powerUpBaseDuration + powerUpDurationBonus)) + " Sekunden!")
                : ("Power-up duration permanently increased to " + std::to_string(static_cast<int>(powerUpBaseDuration + powerUpDurationBonus)) + " seconds!");
            break;
        }
        default: break;
    }
    
    triggerPowerUpActivateSound(type);
    stats.score += POWERUP_ACTIVATION_SCORE_PER_QUALITY * quality;
    
    if (tts && tts->isAvailable() && !msg.empty()) {
        tts->speak(msg, false);
    }
    log("HAMSPIRIT", "Power-up activated: type=" + std::to_string(static_cast<int>(type)) + " quality=" + std::to_string(quality));
}

void Game::deactivatePowerUp(size_t index) {
    if (index >= activePowerUps.size()) return;
    auto& ap = activePowerUps[index];
    
    bool isGerman = translation && 
        (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
    std::string msg;
    
    switch (ap.type) {
        case PowerUpType::SPEED_BOOST:
            baseMaxSpeed = ap.savedValue > 0.0f ? ap.savedValue : baseMaxSpeed / POWERUP_SPEED_BOOST_FACTOR;
            msg = isGerman ? "Geschwindigkeitsboost abgelaufen." : "Speed boost expired.";
            break;
        case PowerUpType::FIRE_RATE:
            msg = isGerman ? "Feuerrate normalisiert." : "Fire rate normalized.";
            break;
        case PowerUpType::AUTO_FIRE:
            autoFireActive = false;
            msg = isGerman ? "Automatisches Feuer deaktiviert." : "Auto fire deactivated.";
            break;
        case PowerUpType::SWR_IMMUNITY:
            swrImmunityActive = false;
            msg = isGerman ? "SWR Immunität abgelaufen." : "SWR immunity expired.";
            break;
        default: break;
    }
    
    triggerPowerUpExpireSound();
    
    if (tts && tts->isAvailable() && !msg.empty()) {
        tts->speak(msg, false);
    }
    log("HAMSPIRIT", "Power-up deactivated: type=" + std::to_string(static_cast<int>(ap.type)));
}

void Game::handlePowerUpExplosion(int powerUpIdx) {
    if (powerUpIdx < 0 || static_cast<size_t>(powerUpIdx) >= powerUps.size()) return;
    auto& pu = powerUps[powerUpIdx];
    // Grace period: don't allow destruction before audio cue is heard
    if ((stats.gameTime - pu.spawnTime) < POWERUP_INVULN_TIME) {
        return;
    }
    pu.destroyed = true;
    
    // Calculate player distance from power-up center
    float distToPlayer = pu.angle - playerAngle;
    while (distToPlayer > PI) distToPlayer -= TWO_PI;
    while (distToPlayer < -PI) distToPlayer += TWO_PI;
    float absDistToPlayer = std::abs(distToPlayer);
    
    // Explosion intensity: 1.0 at center, 0.0 at edge of zone
    float explosionIntensity = 0.0f;
    if (absDistToPlayer < pu.zoneHalfWidth) {
        explosionIntensity = 1.0f - (absDistToPlayer / pu.zoneHalfWidth);
    }
    
    // Damage to player (linear: center = 50% PA health, edge = 0%)
    if (explosionIntensity > 0.0f) {
        float playerDamage = POWERUP_EXPLOSION_MAX_DAMAGE * explosionIntensity;
        stats.paHealth = std::max(0.0f, stats.paHealth - playerDamage);
        if (tts && tts->isAvailable()) {
            bool isGerman = translation && 
                (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
            int dmgPct = static_cast<int>(playerDamage * 100.0f);
            std::string msg = isGerman 
                ? ("Power-up Explosion! " + std::to_string(dmgPct) + " Prozent Schaden!")
                : ("Power-up explosion! " + std::to_string(dmgPct) + " percent damage!");
            tts->speak(msg, true);
        }
        // Vibration feedback for explosion
        if (config.swrVibration && gamepad && gamepad->isConnected()) {
            crashVibrationTimer = 0.3f;
        }
    } else {
        if (tts && tts->isAvailable()) {
            bool isGerman = translation && 
                (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
            tts->speak(isGerman ? "Power-up explodiert!" : "Power-up exploded!", false);
        }
    }
    
    // Damage to QSO Störer if in zone
    if (qsoStoerer.active) {
        float distToStoerer = pu.angle - qsoStoerer.angle;
        while (distToStoerer > PI) distToStoerer -= TWO_PI;
        while (distToStoerer < -PI) distToStoerer += TWO_PI;
        if (std::abs(distToStoerer) < pu.zoneHalfWidth) {
            float stoererDamage = 0.3f * (1.0f - std::abs(distToStoerer) / pu.zoneHalfWidth);
            qsoStoerer.health = std::max(QSO_STOERER_DESTRUCTION_THRESHOLD, qsoStoerer.health - stoererDamage);
            stats.score += 50;
        }
    }
    
    // Damage to morse signals in zone
    if (morseSignalManager) {
        for (auto& sig : morseSignalManager->getSignalsMutable()) {
            if (sig.collected) continue;
            float distToSig = pu.angle - sig.angle;
            while (distToSig > PI) distToSig -= TWO_PI;
            while (distToSig < -PI) distToSig += TWO_PI;
            if (std::abs(distToSig) < pu.zoneHalfWidth) {
                sig.collected = true;  // Destroyed by explosion
                stats.score -= 25;     // Same penalty as shooting morse signals
            }
        }
    }
    
    // Damage to noise enemies in zone
    for (auto& enemy : noiseEnemies) {
        if (enemy.destroyed) continue;
        float distToEnemy = pu.angle - enemy.angle;
        while (distToEnemy > PI) distToEnemy -= TWO_PI;
        while (distToEnemy < -PI) distToEnemy += TWO_PI;
        if (std::abs(distToEnemy) < pu.zoneHalfWidth) {
            float enemyDamage = 1.0f - std::abs(distToEnemy) / pu.zoneHalfWidth;
            enemy.health -= static_cast<int>(std::ceil(enemyDamage * 2));
            if (enemy.health <= 0) {
                enemy.destroyed = true;
                stats.score += 30;  // Bonus for collateral damage
            }
        }
    }
    
    triggerPowerUpExplodeSound(pu.panPosition, explosionIntensity);
    log("HAMSPIRIT", "Power-up explosion: intensity=" + std::to_string(explosionIntensity));
}

void Game::triggerPowerUpActivateSound(PowerUpType type) {
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.powerUpActivateFrames = msToFrames(300);  // ~300ms activation fanfare
        audioParams.powerUpActivateType = type;
    }
}

void Game::triggerPowerUpExpireSound() {
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.powerUpExpireFrames = msToFrames(200);  // ~200ms expiration sound
    }
}

void Game::triggerPowerUpExplodeSound(float pan, float intensity) {
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.powerUpExplodeFrames = msToFrames(400);  // ~400ms explosion
        audioParams.powerUpExplodePan = pan;
        audioParams.powerUpExplodeIntensity = intensity;
    }
}

// Handle weapon system input
void Game::handleWeaponInput(const GamepadState& input, float dt) {
    // D-pad left/right: weapon menu (currently only Noise Blanker)
    bool dLeft = input.buttons[static_cast<int>(GamepadButton::DPAD_LEFT)];
    bool dRight = input.buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)];
    
    if ((dLeft && !prevDpadLeft) || (dRight && !prevDpadRight)) {
        // Only one weapon — play boundary bumper and announce current weapon
        triggerBumperSound();
        log("HAMSPIRIT", "Weapon menu: boundary (only Noise Blanker available)");
        if (tts && tts->isAvailable()) {
            bool isGerman = translation && 
                (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
            tts->speak(isGerman ? "Noise Blanker" : "Noise Blanker", shouldInterruptTts(true));
        }
    }
    prevDpadLeft = dLeft;
    prevDpadRight = dRight;
    
    // Left Trigger fires Noise Blanker (suppressed during power-up collection)
    float ltValue = input.axes[static_cast<int>(GamepadAxis::LEFT_TRIGGER)];
    float rtValue = input.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)];
    bool ltPressed = ltValue > 0.3f;
    bool bothTriggersHeld = ltValue > POWERUP_TRIGGER_THRESHOLD && rtValue > POWERUP_TRIGGER_THRESHOLD;
    
    // Auto-fire mode: holding LT fires repeatedly with cooldown
    if (autoFireActive && ltPressed && !bothTriggersHeld && noiseBlankerCooldown <= 0.0f) {
        handleNoiseBlankerFire();
    } else if (ltPressed && !prevLeftTrigger && noiseBlankerCooldown <= 0.0f && !bothTriggersHeld) {
        handleNoiseBlankerFire();
    }
    prevLeftTrigger = ltPressed;
}

// Fire noise blanker
void Game::handleNoiseBlankerFire() {
    // Route through the central authority when available.
    // In the server-authoritative model, ALL players (including the former
    // "Player 0") send action requests through the same interface.
    if (gameAuthority && gameAuthority->isActive()) {
        PlayerAction action;
        action.type = PlayerActionType::NOISE_BLANKER_FIRE;
        action.playerId = 0;
        action.angle = playerAngle;
        action.aimAngle = aimAngle;
        action.timestamp = std::chrono::steady_clock::now();
        gameAuthority->processAction(action);
    }
    
    // Apply fire rate power-up: check if any FIRE_RATE power-up is active
    float cooldown = NOISE_BLANKER_COOLDOWN;
    for (const auto& ap : activePowerUps) {
        if (ap.type == PowerUpType::FIRE_RATE) {
            cooldown *= POWERUP_FIRE_RATE_FACTOR;
            break;
        }
    }
    noiseBlankerCooldown = cooldown;
    triggerNoiseBlankerFireSound();
    log("HAMSPIRIT", "Noise blanker fired!");

    // Push event for multiplayer spatial audio propagation so other
    // players can hear this noise blanker fire from their perspective.
    if (multiplayerMgr && multiplayerMgr->isMultiplayer()) {
        auto* ctx0 = multiplayerMgr->getPlayer(0);
        if (ctx0) {
            ctx0->noiseBlankerCooldown = cooldown;
            multiplayerMgr->pushEvent(GameEvent(
                GameEventType::NOISE_BLANKER_FIRE, 0, playerAngle, playerLateralOffset));
        }
    }
    
    // Check if we hit the QSO Störer — same aim mechanics as noise enemies
    if (qsoStoerer.active) {
        float stoererDiff = qsoStoerer.angle - (playerAngle + aimAngle);
        while (stoererDiff > PI) stoererDiff -= TWO_PI;
        while (stoererDiff < -PI) stoererDiff += TWO_PI;
        float stoererDist = std::abs(stoererDiff);
        
        if (stoererDist < QSO_STOERER_HIT_MARGIN) {
            // Hit! Reduce health (which reduces speed)
            qsoStoerer.health = std::max(QSO_STOERER_DESTRUCTION_THRESHOLD, 
                                          qsoStoerer.health - QSO_STOERER_HEALTH_PER_HIT);
            // Delay hit sound based on distance — fire sound plays first, then impact
            float hitDelay = 0.15f + stoererDist * 0.4f;  // 150-400ms based on distance
            pendingHitTimer = hitDelay;
            pendingHitHealth = static_cast<int>(qsoStoerer.health * 5.0f);
            pendingHitDestroyed = false;
            pendingHitIsQso = true;
            pendingHitQsoHealth = qsoStoerer.health;
            stats.score += 25;
            
            bool isGerman = translation && 
                (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
            if (tts && tts->isAvailable()) {
                int healthPct = static_cast<int>(qsoStoerer.health * 100.0f);
                std::string msg = isGerman 
                    ? ("Störer getroffen! " + std::to_string(healthPct) + " Prozent.")
                    : ("Interferer hit! " + std::to_string(healthPct) + " percent.");
                tts->speak(msg, false);
            }
            log("HAMSPIRIT", "QSO Störer hit! Health now " + std::to_string(qsoStoerer.health));
            return;
        }
    }
    
    // Check if we hit a noise enemy — hit if aim is within enemy's bandwidth
    NoiseEnemy* bestTarget = nullptr;
    float bestAngleDist = 999.0f;
    
    for (auto& enemy : noiseEnemies) {
        if (enemy.destroyed) continue;
        float angleDiff = enemy.angle - (playerAngle + aimAngle);
        while (angleDiff > PI) angleDiff -= TWO_PI;
        while (angleDiff < -PI) angleDiff += TWO_PI;
        float dist = std::abs(angleDiff);
        // Hit if within half-bandwidth (wider enemies are easier to hit)
        float hitMargin = enemy.bandwidth / 2.0f + 0.1f;  // +0.1 base margin
        if (dist < hitMargin && dist < bestAngleDist) {
            bestAngleDist = dist;
            bestTarget = &enemy;
        }
    }
    
    if (bestTarget) {
        bestTarget->health--;
        // Delay hit/destroy sound based on distance — fire sound plays first
        float hitDelay = 0.15f + bestAngleDist * 0.4f;  // 150-400ms based on distance
        if (bestTarget->health <= 0) {
            bestTarget->destroyed = true;
            int bonus = 50 + static_cast<int>(75.0f * (1.0f - bestTarget->bandwidth));  // Narrow = more points
            stats.score += bonus;
            pendingHitTimer = hitDelay;
            pendingHitDestroyed = true;
            pendingHitBonus = bonus;
            pendingHitIsQso = false;
            log("HAMSPIRIT", "Noise enemy destroyed! +" + std::to_string(bonus) + " points");
            if (tts && tts->isAvailable()) {
                tts->speak("Noise eliminated! +" + std::to_string(bonus), false);
            }
        } else {
            // Hit but not destroyed — schedule delayed hit sound
            pendingHitTimer = hitDelay;
            pendingHitHealth = bestTarget->health;
            pendingHitDestroyed = false;
            pendingHitIsQso = false;
            log("HAMSPIRIT", "Noise enemy hit, health=" + std::to_string(bestTarget->health));
        }
        return;
    }
    
    // Check if we hit a power-up (triggers explosion!)
    for (size_t i = 0; i < powerUps.size(); i++) {
        if (powerUps[i].collected || powerUps[i].destroyed) continue;
        float angleDiff = powerUps[i].angle - (playerAngle + aimAngle);
        while (angleDiff > PI) angleDiff -= TWO_PI;
        while (angleDiff < -PI) angleDiff += TWO_PI;
        if (std::abs(angleDiff) < POWERUP_AIM_MARGIN) {
            handlePowerUpExplosion(static_cast<int>(i));
            return;
        }
    }
    
    // Check if we accidentally hit a morse signal (penalty!)
    const float AIM_MARGIN = 0.4f;
    if (morseSignalManager) {
        MorseSignal* morseTarget = morseSignalManager->getTargetedSignal(playerAngle, aimAngle, AIM_MARGIN);
        if (morseTarget && !morseTarget->collected) {
            stats.score -= 25;
            log("HAMSPIRIT", "Noise blanker hit morse signal! -25 penalty");
            triggerMissMorseSound();
            if (tts && tts->isAvailable()) {
                tts->speak("Penalty! Hit morse signal.", false);
            }
        }
    }
}

// Weapon system sound triggers
void Game::triggerWeaponSwitchSound() {
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.weaponSwitchSoundFrames = msToFrames(60);  // ~60ms click
    }
}

void Game::triggerWeaponEquipSound() {
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.weaponEquipSoundFrames = msToFrames(100);  // ~100ms equip
        audioParams.equippedWeaponType = currentWeapon;
    }
}

void Game::triggerNoiseBlankerFireSound() {
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.noiseBlankerFireFrames = msToFrames(160);  // ~160ms laser-like zap
    }
}

void Game::triggerNoiseDestroyedSound() {
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.noiseDestroyedFrames = msToFrames(200);  // ~200ms explosion
    }
}

void Game::triggerNoiseHitSound(int remainingHealth) {
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.noiseHitSoundFrames = msToFrames(160);  // ~160ms metallic impact
        audioParams.noiseHitVariation = remainingHealth % 4;  // Vary pitch per hit
    }
}

void Game::triggerEmergencyBrakeSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::EMERGENCY_BRAKE, 400);
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.emergencyBrakeSoundFrames = msToFrames(400);  // ~400ms tire screech
    }
}

void Game::triggerAimResetSound() {
    feedbackOrchestrator.triggerSound(ImmediateAudioEvent::Type::AIM_RESET, 120);
    std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
    if (lock.owns_lock()) {
        audioParams.aimResetSoundFrames = msToFrames(120);  // ~120ms descending swoosh
    }
}

// Announce upcoming noise enemy via traffic service ("Bandwacht")
void Game::announceUpcomingNoise(float freqMHz) {
    if (!tts || !tts->isAvailable()) return;
    if (!config.noiseAlerts) return;
    
    bool isGerman = translation && 
        (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
    
    char buf[128];
    if (freqMHz >= 1.0f) {
        if (isGerman) {
            std::snprintf(buf, sizeof(buf), "Die Bandwacht meldet ein Störgeräusch bei %.1f Megahertz.", 
                static_cast<double>(freqMHz));
        } else {
            std::snprintf(buf, sizeof(buf), "Band patrol reports interference at %.1f megahertz.", 
                static_cast<double>(freqMHz));
        }
    } else {
        float freqKHz = freqMHz * 1000.0f;
        if (isGerman) {
            std::snprintf(buf, sizeof(buf), "Die Bandwacht meldet ein Störgeräusch bei %.0f Kilohertz.", 
                static_cast<double>(freqKHz));
        } else {
            std::snprintf(buf, sizeof(buf), "Band patrol reports interference at %.0f kilohertz.", 
                static_cast<double>(freqKHz));
        }
    }
    
    // Play traffic beep before announcement
    {
        std::unique_lock<std::mutex> lock(audioStateMtx, std::try_to_lock);
        if (lock.owns_lock()) {
            audioParams.trafficBeepFrames = msToFrames(300);
        }
    }
    
    tts->speak(buf, false);
    pushBannerText(buf);  // Show Bandwacht announcement on scrolling banner
    log("HAMSPIRIT", std::string("Noise alert: ") + buf);
}

// Apply current key mapping to keyboard emulator
// The pollKeyboard() function reads config.keyMapping directly each frame,
// so no additional action is needed for GUI-focused input.
// For console-mode keyboard emulator, the mapping is already handled via
// handleKeyEvent which feeds logical keys directly.
void Game::applyKeyMapping() {
    if (!keyboard) return;
    KeyboardEmulatorMapping km;
    km.steerLeft = config.keyMapping.steerLeft;
    km.steerRight = config.keyMapping.steerRight;
    km.accelerate = config.keyMapping.accelerate;
    km.brake = config.keyMapping.brake;
    km.aimLeft = config.keyMapping.aimLeft;
    km.aimRight = config.keyMapping.aimRight;
    km.aimUp = config.keyMapping.aimUp;
    km.aimDown = config.keyMapping.aimDown;
    km.morseKey = config.keyMapping.morseKey;
    km.paddleDot = config.keyMapping.paddleDot;
    km.paddleDash = config.keyMapping.paddleDash;
    km.noiseBlanker = config.keyMapping.noiseBlanker;
    km.inductanceUp = config.keyMapping.inductanceUp;
    km.inductanceDown = config.keyMapping.inductanceDown;
    km.capacitanceUp = config.keyMapping.capacitanceUp;
    km.capacitanceDown = config.keyMapping.capacitanceDown;
    km.ununUp = config.keyMapping.ununUp;
    km.ununDown = config.keyMapping.ununDown;
    km.weaponPrev = config.keyMapping.weaponPrev;
    km.weaponNext = config.keyMapping.weaponNext;
    km.pause = config.keyMapping.pause;
    km.statusReadout = config.keyMapping.statusReadout;
    keyboard->setKeyMapping(km);
    log("HAMSPIRIT", "Key mapping applied to keyboard emulator");
}

// Helper: convert a VK code to a human-readable name
static std::string vkCodeToName(int vk) {
    switch (vk) {
        case 0x25: return "Left Arrow";
        case 0x27: return "Right Arrow";
        case 0x26: return "Up Arrow";
        case 0x28: return "Down Arrow";
        case 0x20: return "Space";
        case 0x09: return "Tab";
        case 0x0D: return "Enter";
        case 0x08: return "Backspace";
        case 0x1B: return "Escape";
        default:
            if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
            if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
            if (vk >= 0x70 && vk <= 0x7B) return "F" + std::to_string(vk - 0x70 + 1);
            return "Key " + std::to_string(vk);
    }
}

// Interactive keyboard remapping dialog
void Game::runKeyRemappingDialog() {
    if (!tts || !tts->isAvailable()) return;
    log("HAMSPIRIT", "Starting keyboard remapping dialog");
    
    // Define all remappable actions with their current bindings
    struct RemapAction {
        const char* name;       // Action name (spoken)
        int* keyRef;            // Pointer to the key in config.keyMapping
    };
    
    KeyMapping& km = config.keyMapping;
    RemapAction actions[] = {
        {"Steer Left",       &km.steerLeft},
        {"Steer Right",      &km.steerRight},
        {"Accelerate",       &km.accelerate},
        {"Brake",            &km.brake},
        {"Aim Left",         &km.aimLeft},
        {"Aim Right",        &km.aimRight},
        {"Aim Up",           &km.aimUp},
        {"Aim Down",         &km.aimDown},
        {"Morse Key",        &km.morseKey},
        {"Paddle Dot",       &km.paddleDot},
        {"Paddle Dash",      &km.paddleDash},
        {"Noise Blanker",    &km.noiseBlanker},
        {"Inductance Up",    &km.inductanceUp},
        {"Inductance Down",  &km.inductanceDown},
        {"Capacitance Up",   &km.capacitanceUp},
        {"Capacitance Down", &km.capacitanceDown},
        {"UnUn Up",          &km.ununUp},
        {"UnUn Down",        &km.ununDown},
        {"Weapon Previous",  &km.weaponPrev},
        {"Weapon Next",      &km.weaponNext},
        {"Pause",            &km.pause},
        {"Status Readout",   &km.statusReadout},
    };
    const int numActions = sizeof(actions) / sizeof(actions[0]);
    int currentAction = 0;
    bool waitingForKey = false;
    
    tts->speak("Keyboard Remapping. Use D-pad up and down to select an action, A to assign a new key, B to finish.", shouldInterruptTts(true));
    
    // Edge detection variables — declared before the loop so they persist across iterations
    // (NOT static, which would persist across separate dialog invocations and cause hangs)
    bool prevRemapUp = false, prevRemapDown = false, prevRemapA = false, prevRemapB = false;
    
    // Simple blocking loop for remapping dialog
    while (!shouldExit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        // When waiting for a key assignment, skip pollKeyboard() so that
        // key events are NOT consumed before the platform-specific capture
        // code below can read them.  On POSIX (Linux console / macOS GUI),
        // pollKeyboard() drains the event queue; on Windows it uses
        // GetAsyncKeyState which is stateless and unaffected.
        if (!waitingForKey) {
            pollKeyboard();
        }
        
        GamepadState input = getCurrentInput();
        bool up = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                  || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -STICK_MENU_DEADZONE;
        bool down = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                    || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > STICK_MENU_DEADZONE;
        bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
        bool back = input.buttons[static_cast<int>(GamepadButton::B)];
        
        if (waitingForKey) {
#ifdef _WIN32
            // Scan all possible VK codes for a key press
            for (int vk = 0x08; vk <= 0xFE; vk++) {
                // Skip modifier keys and system keys
                if (vk == 0x10 || vk == 0x11 || vk == 0x12) continue;  // Shift/Ctrl/Alt
                if (vk == 0x5B || vk == 0x5C) continue;  // Windows keys
                if (!(GetAsyncKeyState(vk) & 0x8000)) continue;  // Not pressed
                
                if (vk == 0x1B) {  // ESC pressed — cancel remapping
                    tts->speak("Cancelled", shouldInterruptTts(true));
                    waitingForKey = false;
                    break;
                }
                // Assign the key
                *actions[currentAction].keyRef = vk;
                std::string msg = std::string(actions[currentAction].name) + " mapped to " + vkCodeToName(vk);
                tts->speak(msg, shouldInterruptTts(true));
                log("HAMSPIRIT", msg);
                waitingForKey = false;
                // Save immediately after each assignment and apply to emulator
                saveGameConfig();
                applyKeyMapping();
                // Wait for key release to avoid re-triggering
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                break;
            }
#else
            // POSIX: Capture next key press
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
            {
                int vk = 0;
                bool pressed = false;
                while (pollHamSpiritKeyEvent(vk, pressed)) {
                    if (!pressed || vk < 0) continue;
                    if (vk == 0x1B) {  // ESC pressed — cancel remapping
                        tts->speak("Cancelled", shouldInterruptTts(true));
                        waitingForKey = false;
                    } else {
                        *actions[currentAction].keyRef = vk;
                        std::string msg = std::string(actions[currentAction].name) + " mapped to " + vkCodeToName(vk);
                        tts->speak(msg, shouldInterruptTts(true));
                        log("HAMSPIRIT", msg);
                        waitingForKey = false;
                        saveGameConfig();
                        applyKeyMapping();
                        std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    }
                    break;
                }
            }
#else
            if (consoleInput && consoleInput->kbhit()) {
                int key = consoleInput->getKey();
                if (key == KEY_ERROR || key == KEY_UNKNOWN) {
                    // Ignore invalid keys
                } else {
                    int vk = logicalKeyToVK(key);
                    if (vk == 0x1B) {  // ESC pressed — cancel remapping
                        tts->speak("Cancelled", shouldInterruptTts(true));
                        waitingForKey = false;
                    } else {
                        *actions[currentAction].keyRef = vk;
                        std::string msg = std::string(actions[currentAction].name) + " mapped to " + vkCodeToName(vk);
                        tts->speak(msg, shouldInterruptTts(true));
                        log("HAMSPIRIT", msg);
                        waitingForKey = false;
                        saveGameConfig();
                        applyKeyMapping();
                        std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    }
                }
            }
#endif
#endif
        } else {
            if (up && !prevRemapUp) {
                if (currentAction > 0) {
                    currentAction--;
                    triggerMenuNavSound();
                    std::string msg = std::string(actions[currentAction].name) + ": " + vkCodeToName(*actions[currentAction].keyRef);
                    tts->speak(msg, shouldInterruptTts(true));
                } else {
                    triggerBumperSound();
                }
            }
            if (down && !prevRemapDown) {
                if (currentAction < numActions - 1) {
                    currentAction++;
                    triggerMenuNavSound();
                    std::string msg = std::string(actions[currentAction].name) + ": " + vkCodeToName(*actions[currentAction].keyRef);
                    tts->speak(msg, shouldInterruptTts(true));
                } else {
                    triggerBumperSound();
                }
            }
            if (accept && !prevRemapA) {
                tts->speak("Press the key you want to assign to " + std::string(actions[currentAction].name), shouldInterruptTts(true));
                waitingForKey = true;
                // Small delay to not read the A button itself
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            if (back && !prevRemapB) {
                triggerMenuSelectSound();
                tts->speak("Key mapping saved", shouldInterruptTts(true));
                saveGameConfig();
                // Apply the new key mapping to the keyboard emulator
                applyKeyMapping();
                break;
            }
        }
        
        prevRemapUp = up; prevRemapDown = down; prevRemapA = accept; prevRemapB = back;
    }
}

void Game::runControllerRemappingDialog() {
    if (!tts || !tts->isAvailable()) return;
    if (!gamepad || !gamepad->isConnected()) {
        tts->speak("No controller connected. Please connect a controller and try again.", shouldInterruptTts(true));
        return;
    }
    log("HAMSPIRIT", "Starting controller remapping dialog");
    
    // Controller button names for reference
    struct CtrlAction {
        const char* name;
        const char* currentButton;
    };
    
    // Show current controller layout (non-remappable but informative)
    CtrlAction layout[] = {
        {"Accelerate / Brake",   "Left Stick Y-Axis"},
        {"Steer Left / Right",   "Left Stick X-Axis"},
        {"Aim Weapon",           "Right Stick"},
        {"Morse Vertical Key",   "Right Trigger"},
        {"Paddle Dot",           "Left Bumper"},
        {"Paddle Dash",          "Right Bumper"},
        {"Noise Blanker Fire",   "Left Trigger"},
        {"Pause / Resume",       "Start"},
        {"Status Readout",       "Back / Select"},
        {"Menu Confirm",         "A Button"},
        {"Menu Cancel / Back",   "B Button"},
        {"Inductance Up",        "Y Button"},
        {"Inductance Down",      "X Button"},
        {"Capacitance Up",       "B Button (in game)"},
        {"Capacitance Down",     "A Button (in game)"},
    };
    const int numActions = sizeof(layout) / sizeof(layout[0]);
    int currentAction = 0;
    
    tts->speak(translation ? translation->get("HAMSPIRIT_CONTROLLER_LAYOUT_INFO",
        "Controller Layout. Use D-pad up and down to browse actions and their assigned buttons. "
        "Controller layout uses standard Xbox mapping and is currently not remappable. "
        "Press B to return.") :
        "Controller Layout. Use D-pad up and down to browse actions and their assigned buttons. "
        "Controller layout uses standard Xbox mapping and is currently not remappable. "
        "Press B to return.", shouldInterruptTts(true));
    
    bool prevCtrlUp = false, prevCtrlDown = false, prevCtrlB = false;
    
    while (!shouldExit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        pollKeyboard();
        
        GamepadState input = getCurrentInput();
        bool up = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                  || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -STICK_MENU_DEADZONE;
        bool down = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                    || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > STICK_MENU_DEADZONE;
        bool back = input.buttons[static_cast<int>(GamepadButton::B)];
        
        if (up && !prevCtrlUp) {
            if (currentAction > 0) {
                currentAction--;
                triggerMenuNavSound();
                std::string msg = std::string(layout[currentAction].name) + ": " + layout[currentAction].currentButton;
                tts->speak(msg, shouldInterruptTts(true));
            } else {
                triggerBumperSound();
            }
        }
        if (down && !prevCtrlDown) {
            if (currentAction < numActions - 1) {
                currentAction++;
                triggerMenuNavSound();
                std::string msg = std::string(layout[currentAction].name) + ": " + layout[currentAction].currentButton;
                tts->speak(msg, shouldInterruptTts(true));
            } else {
                triggerBumperSound();
            }
        }
        if (back && !prevCtrlB) {
            triggerMenuSelectSound();
            break;
        }
        
        prevCtrlUp = up; prevCtrlDown = down; prevCtrlB = back;
    }
}

// TTS-guided controller stick drift calibration wizard.
// Samples stick rest positions over a settling period, computes center offsets,
// then optionally lets the user verify the correction by moving the sticks.
void Game::runControllerCalibration() {
    if (!tts || !tts->isAvailable()) return;
    if (!gamepad || !gamepad->isConnected()) {
        tts->speak("No controller connected. Please connect a controller and try again.", true);
        return;
    }
    log("HAMSPIRIT", "Starting controller calibration wizard");

    bool isGerman = translation &&
        (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");

    // Step 1: Instruct user
    tts->speak(isGerman
        ? "Controller Kalibrierung. Legen Sie den Controller auf eine ebene Fläche und lassen Sie beide Sticks los. Drücken Sie A, wenn bereit, oder B zum Abbrechen."
        : "Controller Calibration. Place the controller on a flat surface and release both sticks. Press A when ready, or B to cancel.",
        true);

    // Wait for A or B
    while (!shouldExit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        pollKeyboard();
        if (gamepad) gamepad->update();
        GamepadState input = getCurrentInput();
        bool a = input.buttons[static_cast<int>(GamepadButton::A)];
        bool b = input.buttons[static_cast<int>(GamepadButton::B)];
        if (b) {
            tts->speak(isGerman ? "Kalibrierung abgebrochen." : "Calibration cancelled.", true);
            return;
        }
        if (a) break;
    }
    if (shouldExit) return;

    // Wait for A release
    while (!shouldExit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        pollKeyboard();
        if (gamepad) gamepad->update();
        GamepadState input = getCurrentInput();
        if (!input.buttons[static_cast<int>(GamepadButton::A)]) break;
    }
    if (shouldExit) return;

    // Step 2: Sample stick positions over 2 seconds (120 frames at ~60fps)
    tts->speak(isGerman
        ? "Messung läuft. Bitte Sticks nicht berühren."
        : "Sampling in progress. Please do not touch the sticks.",
        true);

    // Temporarily clear offsets so we read raw values during sampling
    float prevLX = config.stickOffsetLX, prevLY = config.stickOffsetLY;
    float prevRX = config.stickOffsetRX, prevRY = config.stickOffsetRY;
    config.stickOffsetLX = 0.0f; config.stickOffsetLY = 0.0f;
    config.stickOffsetRX = 0.0f; config.stickOffsetRY = 0.0f;

    constexpr int SAMPLE_COUNT = 120;
    float sumLX = 0, sumLY = 0, sumRX = 0, sumRY = 0;
    for (int i = 0; i < SAMPLE_COUNT && !shouldExit; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        if (gamepad) gamepad->update();
        GamepadState raw = gamepad->getState();
        sumLX += raw.axes[static_cast<int>(GamepadAxis::LEFT_X)];
        sumLY += raw.axes[static_cast<int>(GamepadAxis::LEFT_Y)];
        sumRX += raw.axes[static_cast<int>(GamepadAxis::RIGHT_X)];
        sumRY += raw.axes[static_cast<int>(GamepadAxis::RIGHT_Y)];
    }
    if (shouldExit) {
        config.stickOffsetLX = prevLX; config.stickOffsetLY = prevLY;
        config.stickOffsetRX = prevRX; config.stickOffsetRY = prevRY;
        return;
    }

    float avgLX = sumLX / SAMPLE_COUNT;
    float avgLY = sumLY / SAMPLE_COUNT;
    float avgRX = sumRX / SAMPLE_COUNT;
    float avgRY = sumRY / SAMPLE_COUNT;

    // Clamp offsets to ±0.5 to prevent extreme corrections
    config.stickOffsetLX = std::clamp(avgLX, -0.5f, 0.5f);
    config.stickOffsetLY = std::clamp(avgLY, -0.5f, 0.5f);
    config.stickOffsetRX = std::clamp(avgRX, -0.5f, 0.5f);
    config.stickOffsetRY = std::clamp(avgRY, -0.5f, 0.5f);

    log("HAMSPIRIT", "Calibration offsets: LX=" + std::to_string(config.stickOffsetLX) +
        " LY=" + std::to_string(config.stickOffsetLY) +
        " RX=" + std::to_string(config.stickOffsetRX) +
        " RY=" + std::to_string(config.stickOffsetRY));

    // Step 3: Report results
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        isGerman
            ? "Kalibrierung abgeschlossen. Versatz links: X %.3f, Y %.3f. Versatz rechts: X %.3f, Y %.3f. Drücken Sie A zum Speichern oder B zum Verwerfen."
            : "Calibration complete. Left offset: X %.3f, Y %.3f. Right offset: X %.3f, Y %.3f. Press A to save, or B to discard.",
        static_cast<double>(config.stickOffsetLX), static_cast<double>(config.stickOffsetLY),
        static_cast<double>(config.stickOffsetRX), static_cast<double>(config.stickOffsetRY));
    tts->speak(std::string(buf), true);

    // Wait for A (save) or B (discard)
    while (!shouldExit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        pollKeyboard();
        if (gamepad) gamepad->update();
        GamepadState input = getCurrentInput();
        bool a = input.buttons[static_cast<int>(GamepadButton::A)];
        bool b = input.buttons[static_cast<int>(GamepadButton::B)];
        if (b) {
            config.stickOffsetLX = prevLX; config.stickOffsetLY = prevLY;
            config.stickOffsetRX = prevRX; config.stickOffsetRY = prevRY;
            tts->speak(isGerman ? "Kalibrierung verworfen." : "Calibration discarded.", true);
            return;
        }
        if (a) break;
    }
    if (shouldExit) return;

    // Save calibration persistently
    saveGameConfig();
    tts->speak(isGerman ? "Kalibrierung gespeichert." : "Calibration saved.", true);
    log("HAMSPIRIT", "Controller calibration saved");
}

// Save game configuration to file
void Game::saveGameConfig() {
    std::string path = "config/hamspirit.cfg";
    try {
        std::filesystem::create_directories("config");
        std::ofstream ofs(path);
        if (!ofs) {
            log("HAMSPIRIT", "Failed to save config to " + path);
            return;
        }
        ofs << "# Ham Spirit Game Configuration\n";
        ofs << "track_curve=" << static_cast<int>(config.trackCurve) << "\n";
        ofs << "difficulty=" << config.difficultyLevel << "\n";
        ofs << "target_laps=" << config.targetLaps << "\n";
        ofs << "steering_sensitivity=" << config.steeringSensitivity << "\n";
        ofs << "acceleration_sensitivity=" << config.accelerationSensitivity << "\n";
        ofs << "aim_sensitivity=" << config.aimSensitivity << "\n";
        ofs << "paddle_swap=" << (config.paddleSwap ? "1" : "0") << "\n";
        ofs << "controller_preset=" << config.controllerPreset << "\n";
        ofs << "tts_engine=" << getTTSEngineConfigValue(config.ttsEngine) << "\n";
        ofs << "tts_speed=" << config.ttsSpeed << "\n";
        ofs << "swr_vibration=" << (config.swrVibration ? "1" : "0") << "\n";
        ofs << "aim_assist=" << (config.aimAssist ? "1" : "0") << "\n";
        ofs << "traffic_reports=" << (config.trafficReports ? "1" : "0") << "\n";
        ofs << "noise_blanker=" << (config.noiseBlankerEnabled ? "1" : "0") << "\n";
        ofs << "emergency_brake=" << (config.emergencyBrakeEnabled ? "1" : "0") << "\n";
        ofs << "noise_alerts=" << (config.noiseAlerts ? "1" : "0") << "\n";
        ofs << "motor_volume=" << config.motorVolume << "\n";
        ofs << "swr_volume=" << config.swrVolume << "\n";
        ofs << "morse_volume=" << config.morseVolume << "\n";
        ofs << "warning_volume=" << config.warningVolume << "\n";
        ofs << "collision_volume=" << config.collisionVolume << "\n";
        ofs << "enemy_volume=" << config.enemyVolume << "\n";
        ofs << "ui_volume=" << config.uiVolume << "\n";
        ofs << "input_deadzone=" << config.inputDeadzone << "\n";
        ofs << "vibration_intensity=" << config.vibrationIntensity << "\n";
        ofs << "intruder_monitoring=" << (config.intruderMonitoring ? "1" : "0") << "\n";
        ofs << "border_warning=" << (config.borderWarningEnabled ? "1" : "0") << "\n";
        ofs << "curve_announcement=" << (config.curveAnnouncementEnabled ? "1" : "0") << "\n";
        ofs << "curve_announce_distance=" << config.curveAnnouncementDistance << "\n";
        ofs << "elem_morse_signals=" << (config.elemMorseSignals ? "1" : "0") << "\n";
        ofs << "elem_swr_damage=" << (config.elemSwrDamage ? "1" : "0") << "\n";
        ofs << "elem_noise_enemies=" << (config.elemNoiseEnemies ? "1" : "0") << "\n";
        ofs << "elem_qso_stoerer=" << (config.elemQsoStoerer ? "1" : "0") << "\n";
        ofs << "elem_power_ups=" << (config.elemPowerUps ? "1" : "0") << "\n";
        ofs << "elem_auto_steering=" << (config.elemAutoSteering ? "1" : "0") << "\n";
        ofs << "elem_auto_aim=" << (config.elemAutoAim ? "1" : "0") << "\n";
        ofs << "morse_difficulty=" << config.morseDifficulty << "\n";
        ofs << "braille_enabled=" << (config.brailleEnabled ? "1" : "0") << "\n";
        ofs << "braille_speed=" << (config.brailleShowSpeed ? "1" : "0") << "\n";
        ofs << "braille_freq=" << (config.brailleShowFreq ? "1" : "0") << "\n";
        ofs << "braille_swr=" << (config.brailleShowSWR ? "1" : "0") << "\n";
        ofs << "braille_pa=" << (config.brailleShowPA ? "1" : "0") << "\n";
        ofs << "braille_score=" << (config.brailleShowScore ? "1" : "0") << "\n";
        ofs << "braille_lap=" << (config.brailleShowLap ? "1" : "0") << "\n";
        ofs << "braille_tuner=" << (config.brailleShowTuner ? "1" : "0") << "\n";
        // Status readout verbosity
        ofs << "status_speed=" << (config.statusShowSpeed ? "1" : "0") << "\n";
        ofs << "status_freq=" << (config.statusShowFreq ? "1" : "0") << "\n";
        ofs << "status_swr=" << (config.statusShowSWR ? "1" : "0") << "\n";
        ofs << "status_pa=" << (config.statusShowPA ? "1" : "0") << "\n";
        ofs << "status_tuner=" << (config.statusShowTuner ? "1" : "0") << "\n";
        ofs << "status_score=" << (config.statusShowScore ? "1" : "0") << "\n";
        ofs << "status_laps=" << (config.statusShowLaps ? "1" : "0") << "\n";
        ofs << "status_time=" << (config.statusShowTime ? "1" : "0") << "\n";
        if (!config.ttsVoice.empty()) {
            ofs << "tts_voice=" << config.ttsVoice << "\n";
        }
        // Key mapping
        const KeyMapping& km = config.keyMapping;
        ofs << "key_steer_left=" << km.steerLeft << "\n";
        ofs << "key_steer_right=" << km.steerRight << "\n";
        ofs << "key_accelerate=" << km.accelerate << "\n";
        ofs << "key_brake=" << km.brake << "\n";
        ofs << "key_aim_left=" << km.aimLeft << "\n";
        ofs << "key_aim_right=" << km.aimRight << "\n";
        ofs << "key_aim_up=" << km.aimUp << "\n";
        ofs << "key_aim_down=" << km.aimDown << "\n";
        ofs << "key_morse=" << km.morseKey << "\n";
        ofs << "key_paddle_dot=" << km.paddleDot << "\n";
        ofs << "key_paddle_dash=" << km.paddleDash << "\n";
        ofs << "key_noise_blanker=" << km.noiseBlanker << "\n";
        ofs << "key_inductance_up=" << km.inductanceUp << "\n";
        ofs << "key_inductance_down=" << km.inductanceDown << "\n";
        ofs << "key_capacitance_up=" << km.capacitanceUp << "\n";
        ofs << "key_capacitance_down=" << km.capacitanceDown << "\n";
        ofs << "key_unun_up=" << km.ununUp << "\n";
        ofs << "key_unun_down=" << km.ununDown << "\n";
        ofs << "key_weapon_prev=" << km.weaponPrev << "\n";
        ofs << "key_weapon_next=" << km.weaponNext << "\n";
        ofs << "key_pause=" << km.pause << "\n";
        ofs << "key_status=" << km.statusReadout << "\n";
        // Stick drift calibration offsets
        ofs << "stick_offset_lx=" << config.stickOffsetLX << "\n";
        ofs << "stick_offset_ly=" << config.stickOffsetLY << "\n";
        ofs << "stick_offset_rx=" << config.stickOffsetRX << "\n";
        ofs << "stick_offset_ry=" << config.stickOffsetRY << "\n";
        ofs.close();
        log("HAMSPIRIT", "Config saved to " + path);
    } catch (const std::exception& e) {
        log("HAMSPIRIT", std::string("Config save error: ") + e.what());
    }
}

// Load game configuration from file
void Game::loadGameConfig() {
    std::string path = "config/hamspirit.cfg";
    std::ifstream ifs(path);
    if (!ifs) return;  // No saved config — use defaults
    
    std::string line;
    while (std::getline(ifs, line)) {
        // Trim leading/trailing whitespace
        auto start = line.find_first_not_of(" \t\r\n");
        auto end = line.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start, end - start + 1);
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0, pos);
        std::string v = line.substr(pos + 1);
        auto kEnd = k.find_last_not_of(" \t");
        if (kEnd != std::string::npos) k = k.substr(0, kEnd + 1);
        auto vStart = v.find_first_not_of(" \t");
        if (vStart != std::string::npos) v = v.substr(vStart);
        
        try {
            if (k == "track_curve") config.trackCurve = static_cast<TrackCurve>(std::clamp(std::stoi(v), 0, 5));
            else if (k == "difficulty") config.difficultyLevel = std::clamp(std::stoi(v), 1, 5);
            else if (k == "target_laps") config.targetLaps = std::clamp(std::stoi(v), 0, 99);
            else if (k == "steering_sensitivity") config.steeringSensitivity = std::clamp(std::stof(v), 0.1f, 2.0f);
            else if (k == "acceleration_sensitivity") config.accelerationSensitivity = std::clamp(std::stof(v), 0.1f, 2.0f);
            else if (k == "aim_sensitivity") config.aimSensitivity = std::clamp(std::stof(v), 0.1f, 2.0f);
            else if (k == "paddle_swap") config.paddleSwap = (v == "1");
            else if (k == "controller_preset") config.controllerPreset = std::clamp(std::stoi(v), 0, 2);
            else if (k == "tts_engine") {
                std::string vLower = v;
                std::transform(vLower.begin(), vLower.end(), vLower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (vLower == "nvda") {
                    config.ttsEngine = TTSEngineType::NVDA;
                } else if (vLower == "macos_say") {
                    config.ttsEngine = TTSEngineType::MACOS_SAY;
                } else if (vLower == "macos_voiceover") {
                    config.ttsEngine = TTSEngineType::MACOS_VOICEOVER;
                } else if (vLower == "espeak_ng") {
                    config.ttsEngine = TTSEngineType::ESPEAK_NG;
                } else {
                    config.ttsEngine = TTSEngineType::WINDOWS_SAPI;
                }
            }
            else if (k == "tts_speed") config.ttsSpeed = std::clamp(std::stoi(v), -2, 2);
            else if (k == "swr_vibration") config.swrVibration = (v == "1");
            else if (k == "aim_assist") config.aimAssist = (v == "1");
            else if (k == "traffic_reports") config.trafficReports = (v == "1");
            else if (k == "noise_blanker") config.noiseBlankerEnabled = (v == "1");
            else if (k == "emergency_brake") config.emergencyBrakeEnabled = (v == "1");
            else if (k == "noise_alerts") config.noiseAlerts = (v == "1");
            else if (k == "motor_volume") config.motorVolume = std::clamp(std::stof(v), 0.0f, 1.0f);
            else if (k == "swr_volume") config.swrVolume = std::clamp(std::stof(v), 0.0f, 1.0f);
            else if (k == "morse_volume") config.morseVolume = std::clamp(std::stof(v), 0.0f, 1.0f);
            else if (k == "warning_volume") config.warningVolume = std::clamp(std::stof(v), 0.0f, 1.0f);
            else if (k == "collision_volume") config.collisionVolume = std::clamp(std::stof(v), 0.0f, 1.0f);
            else if (k == "enemy_volume") config.enemyVolume = std::clamp(std::stof(v), 0.0f, 1.0f);
            else if (k == "ui_volume") config.uiVolume = std::clamp(std::stof(v), 0.0f, 1.0f);
            else if (k == "input_deadzone") config.inputDeadzone = std::clamp(std::stof(v), 0.02f, 0.30f);
            else if (k == "vibration_intensity") config.vibrationIntensity = std::clamp(std::stof(v), 0.0f, 1.0f);
            else if (k == "intruder_monitoring") config.intruderMonitoring = (v == "1");
            else if (k == "border_warning") config.borderWarningEnabled = (v == "1");
            else if (k == "curve_announcement") config.curveAnnouncementEnabled = (v == "1");
            else if (k == "curve_announce_distance") config.curveAnnouncementDistance = std::clamp(std::stof(v), 1.0f, 20.0f);
            else if (k == "elem_morse_signals") config.elemMorseSignals = (v == "1");
            else if (k == "elem_swr_damage") config.elemSwrDamage = (v == "1");
            else if (k == "elem_noise_enemies") config.elemNoiseEnemies = (v == "1");
            else if (k == "elem_qso_stoerer") config.elemQsoStoerer = (v == "1");
            else if (k == "elem_power_ups") config.elemPowerUps = (v == "1");
            else if (k == "elem_auto_steering") config.elemAutoSteering = (v == "1");
            else if (k == "elem_auto_aim") config.elemAutoAim = (v == "1");
            else if (k == "morse_difficulty") config.morseDifficulty = std::clamp(std::stoi(v), 1, 5);
            else if (k == "braille_enabled") config.brailleEnabled = (v == "1");
            else if (k == "braille_speed") config.brailleShowSpeed = (v == "1");
            else if (k == "braille_freq") config.brailleShowFreq = (v == "1");
            else if (k == "braille_swr") config.brailleShowSWR = (v == "1");
            else if (k == "braille_pa") config.brailleShowPA = (v == "1");
            else if (k == "braille_score") config.brailleShowScore = (v == "1");
            else if (k == "braille_lap") config.brailleShowLap = (v == "1");
            else if (k == "braille_tuner") config.brailleShowTuner = (v == "1");
            // Status readout verbosity
            else if (k == "status_speed") config.statusShowSpeed = (v == "1");
            else if (k == "status_freq") config.statusShowFreq = (v == "1");
            else if (k == "status_swr") config.statusShowSWR = (v == "1");
            else if (k == "status_pa") config.statusShowPA = (v == "1");
            else if (k == "status_tuner") config.statusShowTuner = (v == "1");
            else if (k == "status_score") config.statusShowScore = (v == "1");
            else if (k == "status_laps") config.statusShowLaps = (v == "1");
            else if (k == "status_time") config.statusShowTime = (v == "1");
            else if (k == "tts_voice") config.ttsVoice = v;
            // Key mapping
            else if (k == "key_steer_left") config.keyMapping.steerLeft = std::stoi(v);
            else if (k == "key_steer_right") config.keyMapping.steerRight = std::stoi(v);
            else if (k == "key_accelerate") config.keyMapping.accelerate = std::stoi(v);
            else if (k == "key_brake") config.keyMapping.brake = std::stoi(v);
            else if (k == "key_aim_left") config.keyMapping.aimLeft = std::stoi(v);
            else if (k == "key_aim_right") config.keyMapping.aimRight = std::stoi(v);
            else if (k == "key_aim_up") config.keyMapping.aimUp = std::stoi(v);
            else if (k == "key_aim_down") config.keyMapping.aimDown = std::stoi(v);
            else if (k == "key_morse") config.keyMapping.morseKey = std::stoi(v);
            else if (k == "key_paddle_dot") config.keyMapping.paddleDot = std::stoi(v);
            else if (k == "key_paddle_dash") config.keyMapping.paddleDash = std::stoi(v);
            else if (k == "key_noise_blanker") config.keyMapping.noiseBlanker = std::stoi(v);
            else if (k == "key_inductance_up") config.keyMapping.inductanceUp = std::stoi(v);
            else if (k == "key_inductance_down") config.keyMapping.inductanceDown = std::stoi(v);
            else if (k == "key_capacitance_up") config.keyMapping.capacitanceUp = std::stoi(v);
            else if (k == "key_capacitance_down") config.keyMapping.capacitanceDown = std::stoi(v);
            else if (k == "key_unun_up") config.keyMapping.ununUp = std::stoi(v);
            else if (k == "key_unun_down") config.keyMapping.ununDown = std::stoi(v);
            else if (k == "key_weapon_prev") config.keyMapping.weaponPrev = std::stoi(v);
            else if (k == "key_weapon_next") config.keyMapping.weaponNext = std::stoi(v);
            else if (k == "key_pause") config.keyMapping.pause = std::stoi(v);
            else if (k == "key_status") config.keyMapping.statusReadout = std::stoi(v);
            // Stick drift calibration offsets
            else if (k == "stick_offset_lx") config.stickOffsetLX = std::clamp(std::stof(v), -0.5f, 0.5f);
            else if (k == "stick_offset_ly") config.stickOffsetLY = std::clamp(std::stof(v), -0.5f, 0.5f);
            else if (k == "stick_offset_rx") config.stickOffsetRX = std::clamp(std::stof(v), -0.5f, 0.5f);
            else if (k == "stick_offset_ry") config.stickOffsetRY = std::clamp(std::stof(v), -0.5f, 0.5f);
        } catch (...) {
            // Skip malformed lines
        }
    }
    log("HAMSPIRIT", "Config loaded from " + path);
}

// Interactive tutorial mode
void Game::runTutorial() {
    log("HAMSPIRIT", "Starting tutorial mode");
    
    if (!tts || !tts->isAvailable()) {
        setState(GameState::PLAYING);
        return;
    }
    
    bool isGerman = translation && 
        (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
    bool usingGamepad = gamepad && gamepad->isConnected();
    
    // Helper: speak and wait for skip
    auto tutorialSpeak = [&](const std::string& textDe, const std::string& textEn) -> bool {
        tts->speak(isGerman ? textDe : textEn, shouldInterruptTts(true));
        if (!waitForInput(30.0f) || shouldExit) return false;
        return true;
    };
    
    // === Section 1: Driving ===
    if (!tutorialSpeak(
        "Willkommen zum Tutorial! Drücke eine beliebige Taste, um zum nächsten Abschnitt zu gelangen.",
        "Welcome to the tutorial! Press any button to advance to the next section.")) return;
    
    if (!tutorialSpeak(
        usingGamepad
            ? "Abschnitt 1: Fahren. Benutze den linken Analogstick nach vorne, um zu beschleunigen. Nach hinten zum Bremsen. Das Motorgeräusch wird höher bei mehr Geschwindigkeit."
            : "Abschnitt 1: Fahren. Benutze die Pfeiltasten hoch und runter zum Beschleunigen und Bremsen. Das Motorgeräusch wird höher bei mehr Geschwindigkeit.",
        usingGamepad
            ? "Section 1: Driving. Push the left analog stick forward to accelerate. Pull back to brake. The motor sound gets higher with more speed."
            : "Section 1: Driving. Use the arrow keys up and down to accelerate and brake. The motor sound gets higher with more speed.")) return;
    
    if (!tutorialSpeak(
        usingGamepad
            ? "Der Stick nach links und rechts lenkt dein Fahrzeug. Du hörst den Motorsound im Stereofeld wandern."
            : "Pfeiltasten links und rechts lenken dein Fahrzeug. Du hörst den Motorsound im Stereofeld wandern.",
        usingGamepad
            ? "Push the stick left or right to steer. You'll hear the motor sound pan in the stereo field."
            : "Use the left and right arrow keys to steer. You'll hear the motor sound pan in the stereo field.")) return;

    if (usingGamepad) {
        if (!tutorialSpeak(
            "Drücke den linken Stick rein für eine Vollbremsung. Das bringt dich sofort zum Stehen.",
            "Click the left stick for an emergency brake. This stops you instantly.")) return;
    }
    
    // === Section 2: Antenna Tuning ===
    if (!tutorialSpeak(
        "Abschnitt 2: Antennenanpassung. Die Strecke basiert auf echten Messdaten. Schlechtes SWR macht die Straße rau, du hörst es am Motor.",
        "Section 2: Antenna tuning. The track is based on real measurement data. Bad SWR makes the road rough, you hear it in the motor sound.")) return;
    
    if (!tutorialSpeak(
        usingGamepad
            ? "Y-Taste erhöht die Induktivität. X-Taste verringert sie. B-Taste erhöht die Kapazität. A-Taste verringert sie. Steuerkreuz hoch und runter ändert das UnUn Verhältnis."
            : "Q und E ändern die Induktivität. Z und C ändern die Kapazität. I und K ändern das UnUn Verhältnis.",
        usingGamepad
            ? "Y button increases inductance. X decreases it. B increases capacitance. A decreases it. D-pad up and down changes the UnUn ratio."
            : "Q and E adjust inductance. Z and C adjust capacitance. I and K change the UnUn ratio.")) return;
    
    if (!tutorialSpeak(
        "Das SWR-Warnpiepen wird schneller und höher, je schlechter die Anpassung ist. Eiert der Ton, muss die Induktivität angepasst werden. Hallt er, die Kapazität.",
        "The SWR warning beep gets faster and higher with worse matching. A wobbling tone means adjust inductance. An echo means adjust capacitance.")) return;
    
    // === Section 3: Morse Code ===
    if (!tutorialSpeak(
        "Abschnitt 3: Morsezeichen. Auf der Strecke tauchen Morsezeichen auf. Du hörst ihr Muster im Stereofeld.",
        "Section 3: Morse code. Morse signals appear on the track. You hear their pattern in the stereo field.")) return;
    
    if (!tutorialSpeak(
        usingGamepad
            ? "Mit dem rechten Analogstick zielst du wie mit einem Panzerturm. Das Fadenkreuz-Geräusch zeigt, ob du triffst. Drücke den rechten Stick rein, um das Ziel wieder geradeaus zu richten."
            : "Mit W A S D zielst du wie mit einem Panzerturm. Das Fadenkreuz-Geräusch zeigt, ob du triffst.",
        usingGamepad
            ? "Use the right analog stick to aim like a tank turret. The crosshair sound shows your aim. Click the right stick to reset aim forward."
            : "Use W A S D to aim like a tank turret. The crosshair sound shows your aim.")) return;
    
    if (!tutorialSpeak(
        usingGamepad
            ? "Der rechte Trigger ist die Morsetaste. Kurz drücken für einen Punkt, lang für einen Strich. Wiederhole das Muster des Zeichens, um es einzusammeln."
            : "Leertaste ist die Morsetaste. Kurz drücken für einen Punkt, lang für einen Strich. Wiederhole das Muster des Zeichens, um es einzusammeln.",
        usingGamepad
            ? "The right trigger is the morse key. Short press for a dot, long for a dash. Repeat the signal's pattern to collect it."
            : "Space is the morse key. Short press for a dot, long for a dash. Repeat the signal's pattern to collect it.")) return;
    
    if (!tutorialSpeak(
        usingGamepad
            ? "Alternativ kannst du die Schultertasten als Paddle benutzen. Linke Schultertaste für Punkt, rechte für Strich."
            : "Alternativ kannst du U und O als Paddle benutzen. U für Punkt, O für Strich.",
        usingGamepad
            ? "Alternatively, use the shoulder buttons as paddles. Left shoulder for dot, right for dash."
            : "Alternatively, use U and O as paddles. U for dot, O for dash.")) return;
    
    // === Section 4: Weapons ===
    if (!tutorialSpeak(
        "Abschnitt 4: Waffen und Störgeräusche. Neben Morsezeichen gibt es Störgeräusche. Sie rauschen und brummen auf der Frequenz.",
        "Section 4: Weapons and noise. Besides morse signals, there are noise enemies. They produce static and hum on the frequency.")) return;
    
    if (!tutorialSpeak(
        usingGamepad
            ? "Der linke Trigger feuert den Noise Blanker. Er klingt wie eine Laserwaffe. Ziel auf das Störgeräusch und feuere! Breitere Störungen sind leichter zu treffen, aber zäher."
            : "F feuert den Noise Blanker. Er klingt wie eine Laserwaffe. Ziel auf das Störgeräusch und feuere! Breitere Störungen sind leichter zu treffen, aber zäher.",
        usingGamepad
            ? "The left trigger fires the noise blanker. It sounds like a laser. Aim at the noise and fire! Wider interference is easier to hit but tougher."
            : "F fires the noise blanker. It sounds like a laser. Aim at the noise and fire! Wider interference is easier to hit but tougher.")) return;
    
    if (!tutorialSpeak(
        usingGamepad
            ? "Steuerkreuz links und rechts wechselt zwischen Morsekanone und Noise Blanker. Vorsicht: Den Noise Blanker auf ein Morsezeichen zu feuern gibt Strafpunkte!"
            : "J und L wechseln zwischen Morsekanone und Noise Blanker. Vorsicht: Den Noise Blanker auf ein Morsezeichen zu feuern gibt Strafpunkte!",
        usingGamepad
            ? "D-pad left and right switches between morse cannon and noise blanker. Caution: Firing the noise blanker at a morse signal gives penalty points!"
            : "J and L switch between morse cannon and noise blanker. Caution: Firing the noise blanker at a morse signal gives penalty points!")) return;
    
    // === Section 5: PA Health ===
    if (!tutorialSpeak(
        "Abschnitt 5: Transceiver Gesundheit. Fahren mit schlechtem SWR beschädigt deine Endstufe. Die Leistung sinkt und du wirst langsamer.",
        "Section 5: Transceiver health. Driving with bad SWR damages your power amplifier. Power drops and you slow down.")) return;
    
    if (!tutorialSpeak(
        "Morsezeichen sammeln repariert die Endstufe! Schwierigere Zeichen reparieren mehr. Im Stehen wird kein Schaden verursacht.",
        "Collecting morse characters repairs the PA! Harder characters repair more. Standing still causes no damage.")) return;
    
    // === Section 6: Assist functions ===
    if (!tutorialSpeak(
        "Abschnitt 6: Assistenzfunktionen. Drücke die Zurück-Taste links neben der Xbox-Taste für eine Statusansage aller aktuellen Werte.",
        "Section 6: Assist functions. Press the Back button left of the Xbox button for a status readout of all current values.")) return;
    
    if (!tutorialSpeak(
        "Im Menü findest du unter Assistenz Optionen wie Zielhilfe, Verkehrsservice, Vollbremsung und Störgeräusch-Warnungen.",
        "In the menu under Assist you'll find options like aim assist, traffic reports, emergency brake, and noise alerts.")) return;
    
    if (!tutorialSpeak(
        "Tutorial abgeschlossen! Viel Spaß beim Spielen. Drücke Start zum Pausieren.",
        "Tutorial complete! Have fun playing. Press Start to pause.")) return;
    
    // Return to wherever we came from
    log("HAMSPIRIT", "Tutorial complete");
    if (currentState.load() == GameState::MAIN_MENU || currentState.load() == GameState::INTRO) {
        // Came from main menu — go back to main menu
        currentMainMenuOption = MainMenuOption::NEW_GAME;
        speakTranslated("HAMSPIRIT_MAIN_MENU", "Main Menu", true);
        speakMainMenuOption();
        setState(GameState::MAIN_MENU);
    } else {
        // Came from pause menu — go back to pause
        showPauseMenu();
    }
}
bool launchGame(
    const std::vector<MeasurementPoint>& measurements,
    AcousticAnalyzer* analyzer,
    TranslationManager* translation,
    Logger* logger,
    IConsoleInput* consoleInput
) {
    if (logger) {
        logger->log("HAMSPIRIT", "Launching Ham Spirit game...");
    }
    
    try {
        Game game(translation, logger, consoleInput);
        
        if (!game.initialize(measurements, analyzer)) {
            if (logger) {
                logger->log("HAMSPIRIT", "Failed to initialize game");
            }
            return false;
        }
        
        game.run();
        
        if (logger) {
            logger->log("HAMSPIRIT", "Game completed successfully");
        }
        
        return true;
    }
    catch (const std::exception& e) {
        if (logger) {
            logger->log("HAMSPIRIT", std::string("Exception in game: ") + e.what());
        }
        // Ensure vibration is stopped even on crash
        try {
            auto gp = createGamepadInput();
            if (gp) {
                gp->initialize();
                if (gp->isConnected()) gp->setVibration(0, 0.0f, 0.0f);
                gp->shutdown();
            }
        } catch (...) {}
        return false;
    }
}

// ============================================================================
// Voice selection helpers
// ============================================================================

void Game::refreshAvailableVoices() {
    availableVoices.clear();
    currentVoiceIndex = 0;
    if (!tts || !tts->isAvailable()) return;
    ITTSEngine* engine = tts->getEngine();
    if (!engine) return;
    availableVoices = engine->getAvailableVoices();
    // Find current voice in list
    if (!config.ttsVoice.empty()) {
        for (int i = 0; i < static_cast<int>(availableVoices.size()); i++) {
            if (availableVoices[i] == config.ttsVoice) {
                currentVoiceIndex = i;
                break;
            }
        }
    }
    log("HAMSPIRIT", "Available voices: " + std::to_string(availableVoices.size()));
}

// ============================================================================
// NATO / Amateur Radio Phonetic Alphabet
// ============================================================================
std::string Game::callsignToPhonetic(const std::string& callsign) {
    static const char* phonetic[] = {
        "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot",
        "Golf", "Hotel", "India", "Juliet", "Kilo", "Lima",
        "Mike", "November", "Oscar", "Papa", "Quebec", "Romeo",
        "Sierra", "Tango", "Uniform", "Victor", "Whiskey",
        "X-Ray", "Yankee", "Zulu"
    };
    bool useGerman = translation && (translation->getCurrentLanguage() == "deu" || translation->getCurrentLanguage() == "de");
    static const char* digitsEn[] = {
        "Zero", "One", "Two", "Three", "Four",
        "Five", "Six", "Seven", "Eight", "Niner"
    };
    static const char* digitsDe[] = {
        "Null", "Eins", "Zwei", "Drei", "Vier",
        "Fuenf", "Sechs", "Sieben", "Acht", "Neuner"
    };
    std::string result;
    for (char c : callsign) {
        if (!result.empty()) result += ", ";
        char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper >= 'A' && upper <= 'Z') {
            result += phonetic[upper - 'A'];
        } else if (upper >= '0' && upper <= '9') {
            result += useGerman ? digitsDe[upper - '0'] : digitsEn[upper - '0'];
        } else if (c == '/') {
            result += "Stroke";
        } else if (c == '-') {
            result += "Dash";
        } else {
            result += c;
        }
    }
    return result;
}

// ============================================================================
// Text input via keyboard with click feedback
// ============================================================================
std::string Game::promptTextInput(const std::string& promptMsg, int maxLen) {
    if (tts && tts->isAvailable()) {
        tts->speak(promptMsg, shouldInterruptTts(true));
    }
    log("HAMSPIRIT", "Text input prompt: " + promptMsg);
    
    std::string input;
    bool done = false;
    
    // Pre-seed gamepad button state for edge detection so that buttons
    // still held from a prior menu selection (e.g. A to confirm the audio
    // device) don't immediately submit the text input with an empty string.
    bool prevGamepadA = false;
    bool prevGamepadB = false;
    if (gamepad && gamepad->isConnected()) {
        gamepad->update();
        GamepadState gs = gamepad->getState();
        prevGamepadA = gs.buttons[static_cast<int>(GamepadButton::A)];
        prevGamepadB = gs.buttons[static_cast<int>(GamepadButton::B)];
    }
    
    // Hide menu overlay so it doesn't compete with the text input overlay.
    // showGameModeSelectionMenu() leaves the menu visible when it returns.
    hideMenuOverlay();
    
    // Show graphical text input overlay
    showTextInputOverlay(promptMsg, "");
    
#ifdef _WIN32
    // Track previous key state for edge detection in GUI mode
    bool prevKeyState[256] = {};
    
    // Pre-seed prevKeyState with current physical key state so we don't
    // fire edge events for keys that are already held from a prior prompt.
    // This prevents Enter held from callsign prompt immediately submitting the name prompt.
    for (int vk = VK_BACK; vk <= 'Z'; vk++) {
        prevKeyState[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    prevKeyState[VK_SPACE] = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    for (int vk = VK_OEM_1; vk <= VK_OEM_102; vk++) {
        if (vk < 256) prevKeyState[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
#else
    // POSIX: Drain any buffered input before starting
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
    {
        int vk = 0;
        bool pressed = false;
        while (pollHamSpiritKeyEvent(vk, pressed)) { /* drain */ }
    }
#else
    if (consoleInput) {
        while (consoleInput->kbhit()) {
            consoleInput->getKey();
        }
    }
#endif
#endif
    
    while (!done && !shouldExit) {
#ifdef _WIN32
        // Always scan keyboard input for text entry — GetAsyncKeyState works
        // regardless of which window is focused, and during text prompts the user
        // should always be able to type.
        {
            // Scan printable characters and control keys via GetAsyncKeyState
            // Edge detection: only process newly pressed keys
            for (int vk = VK_BACK; vk <= 'Z'; vk++) {
                bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
                if (pressed && !prevKeyState[vk]) {
                    if (vk == VK_ESCAPE) {
                        input.clear();
                        done = true;
                        break;
                    } else if (vk == VK_RETURN) {
                        done = true;
                        break;
                    } else if (vk == VK_BACK) {
                        if (!input.empty()) {
                            input.pop_back();
                            triggerKeyClickSound();
                            if (tts && tts->isAvailable()) {
                                tts->speak(input.empty() ? "Empty" : input, shouldInterruptTts(true));
                            }
                        } else {
                            triggerBumperSound();
                        }
                    } else if (vk >= 0x30 && vk <= 0x39 && static_cast<int>(input.size()) < maxLen) {
                        // Digits 0-9
                        input += static_cast<char>(vk);
                        triggerKeyClickSound();
                        if (tts && tts->isAvailable()) {
                            std::string ch(1, static_cast<char>(vk));
                            tts->speak(ch, shouldInterruptTts(true));
                        }
                    } else if (vk >= 'A' && vk <= 'Z' && static_cast<int>(input.size()) < maxLen) {
                        // Letters — check shift for uppercase
                        bool shifted = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                        char c = shifted ? static_cast<char>(vk) : static_cast<char>(std::tolower(vk));
                        input += c;
                        triggerKeyClickSound();
                        if (tts && tts->isAvailable()) {
                            std::string ch(1, c);
                            tts->speak(ch, shouldInterruptTts(true));
                        }
                    } else if (vk == VK_SPACE && static_cast<int>(input.size()) < maxLen) {
                        input += ' ';
                        triggerKeyClickSound();
                        if (tts && tts->isAvailable()) {
                            tts->speak("Space", shouldInterruptTts(true));
                        }
                    } else if (static_cast<int>(input.size()) >= maxLen) {
                        triggerBumperSound();
                    }
                    // Also handle special characters: / and -
                    if (vk == VK_OEM_MINUS && static_cast<int>(input.size()) < maxLen) {
                        input += '-';
                        triggerKeyClickSound();
                    }
                }
                prevKeyState[vk] = pressed;
            }
            // Also check OEM keys above 'Z'
            for (int vk = VK_OEM_1; vk <= VK_OEM_102; vk++) {
                bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
                if (pressed && !prevKeyState[vk]) {
                    if (vk == VK_OEM_2 && static_cast<int>(input.size()) < maxLen) { // / key
                        input += '/';
                        triggerKeyClickSound();
                    }
                }
                if (vk < 256) prevKeyState[vk] = pressed;
            }
        }
#else
        // POSIX: Read key events for text entry
#if defined(HAVE_NATIVE_GUI) && defined(__APPLE__)
        // macOS GUI: read key events from the NSWindow key queue
        {
            int vk = 0;
            bool pressed = false;
            while (pollHamSpiritKeyEvent(vk, pressed)) {
                if (!pressed || vk < 0) continue;
                if (vk == 0x1B) {  // ESC
                    input.clear();
                    done = true;
                } else if (vk == 0x0D) {  // Enter
                    done = true;
                } else if (vk == 0x08) {  // Backspace
                    if (!input.empty()) {
                        input.pop_back();
                        triggerKeyClickSound();
                        if (tts && tts->isAvailable()) {
                            tts->speak(input.empty() ? "Empty" : input, shouldInterruptTts(true));
                        }
                    } else {
                        triggerBumperSound();
                    }
                } else if (vk >= 'A' && vk <= 'Z' && static_cast<int>(input.size()) < maxLen) {
                    // VK codes are uppercase; store as lowercase for callsigns
                    char c = static_cast<char>(std::tolower(vk));
                    input += c;
                    triggerKeyClickSound();
                    if (tts && tts->isAvailable()) {
                        std::string ch(1, c);
                        tts->speak(ch, shouldInterruptTts(true));
                    }
                } else if (vk >= '0' && vk <= '9' && static_cast<int>(input.size()) < maxLen) {
                    input += static_cast<char>(vk);
                    triggerKeyClickSound();
                    if (tts && tts->isAvailable()) {
                        std::string ch(1, static_cast<char>(vk));
                        tts->speak(ch, shouldInterruptTts(true));
                    }
                } else if (vk == 0x20 && static_cast<int>(input.size()) < maxLen) {
                    input += ' ';
                    triggerKeyClickSound();
                    if (tts && tts->isAvailable()) {
                        tts->speak("Space", shouldInterruptTts(true));
                    }
                } else if (static_cast<int>(input.size()) >= maxLen) {
                    triggerBumperSound();
                }
            }
        }
#else
        if (consoleInput && consoleInput->kbhit()) {
            int key = consoleInput->getKey();
            if (key != KEY_ERROR && key != KEY_UNKNOWN) {
                int vk = logicalKeyToVK(key);
                if (vk == 0x1B) {  // ESC
                    input.clear();
                    done = true;
                } else if (vk == 0x0D) {  // Enter
                    done = true;
                } else if (vk == 0x08 || key == 0x7F) {  // VK_BACK or Unix DEL char
                    if (!input.empty()) {
                        input.pop_back();
                        triggerKeyClickSound();
                        if (tts && tts->isAvailable()) {
                            tts->speak(input.empty() ? "Empty" : input, shouldInterruptTts(true));
                        }
                    } else {
                        triggerBumperSound();
                    }
                } else if (key >= 'a' && key <= 'z' && static_cast<int>(input.size()) < maxLen) {
                    // Lowercase letter (terminal delivers lowercase in raw mode)
                    input += static_cast<char>(key);
                    triggerKeyClickSound();
                    if (tts && tts->isAvailable()) {
                        std::string ch(1, static_cast<char>(key));
                        tts->speak(ch, shouldInterruptTts(true));
                    }
                } else if (key >= 'A' && key <= 'Z' && static_cast<int>(input.size()) < maxLen) {
                    input += static_cast<char>(key);
                    triggerKeyClickSound();
                    if (tts && tts->isAvailable()) {
                        std::string ch(1, static_cast<char>(key));
                        tts->speak(ch, shouldInterruptTts(true));
                    }
                } else if (key >= '0' && key <= '9' && static_cast<int>(input.size()) < maxLen) {
                    input += static_cast<char>(key);
                    triggerKeyClickSound();
                    if (tts && tts->isAvailable()) {
                        std::string ch(1, static_cast<char>(key));
                        tts->speak(ch, shouldInterruptTts(true));
                    }
                } else if (key == ' ' && static_cast<int>(input.size()) < maxLen) {
                    input += ' ';
                    triggerKeyClickSound();
                    if (tts && tts->isAvailable()) {
                        tts->speak("Space", shouldInterruptTts(true));
                    }
                } else if ((key == '-' || key == '/') && static_cast<int>(input.size()) < maxLen) {
                    input += static_cast<char>(key);
                    triggerKeyClickSound();
                } else if (static_cast<int>(input.size()) >= maxLen) {
                    triggerBumperSound();
                }
            }
        }
#endif
#endif
        
        // Also accept gamepad A to submit (like Enter) and B to cancel (like Escape)
        // — edge-triggered to prevent buttons held from a prior menu from instantly acting.
        if (gamepad && gamepad->isConnected()) {
            gamepad->update();
            GamepadState gs = gamepad->getState();
            bool curA = gs.buttons[static_cast<int>(GamepadButton::A)];
            bool curB = gs.buttons[static_cast<int>(GamepadButton::B)];
            if (curA && !prevGamepadA) {
                done = true;  // A = confirm/submit
            }
            if (curB && !prevGamepadB) {
                input.clear();  // B = cancel/back
                done = true;
            }
            prevGamepadA = curA;
            prevGamepadB = curB;
        }
        
        // Update visual text input overlay with current input
        showTextInputOverlay(promptMsg, input);
        
        // Poll at 8ms (~125 Hz) to reliably capture fast typing and transient key presses
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    
    hideTextInputOverlay();
    // Brief delay so any held Enter/A key from this confirmation can be released
    // before the next prompt starts (prevents carry-over to callsign→name transition)
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    log("HAMSPIRIT", "Text input result: " + input);
    return input;
}

// ============================================================================
// High Score / Leaderboard
// ============================================================================
void Game::loadHighScores() {
    highScores.clear();
    std::string path = "config/hamspirit_highscores.dat";
    std::ifstream ifs(path);
    if (!ifs) return;
    
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        // Format: callsign|name|score|laps|time|paHealth|bonus[|gameMode|playerCount]
        std::istringstream ss(line);
        HighScoreEntry entry;
        std::string token;
        try {
            if (!std::getline(ss, entry.callsign, '|')) continue;
            if (!std::getline(ss, entry.playerName, '|')) continue;
            if (!std::getline(ss, token, '|')) continue;
            entry.score = std::stoi(token);
            if (!std::getline(ss, token, '|')) continue;
            entry.lapsCompleted = std::stoi(token);
            if (!std::getline(ss, token, '|')) continue;
            entry.gameTime = std::stof(token);
            if (!std::getline(ss, token, '|')) continue;
            entry.paHealth = std::stof(token);
            if (!std::getline(ss, token, '|')) continue;
            entry.bonusAchieved = (token == "1");
            // Optional new fields (backward compatible)
            if (std::getline(ss, token, '|')) {
                entry.gameMode = token;
            }
            if (std::getline(ss, token, '|')) {
                entry.playerCount = std::stoi(token);
            }
            highScores.push_back(entry);
        } catch (...) {
            continue;  // Skip malformed entries
        }
    }
    // Sort descending by score
    std::sort(highScores.begin(), highScores.end(),
        [](const HighScoreEntry& a, const HighScoreEntry& b) { return a.score > b.score; });
    // Keep top 20
    if (highScores.size() > 20) highScores.resize(20);
    log("HAMSPIRIT", "Loaded " + std::to_string(highScores.size()) + " high scores");
}

void Game::saveHighScores() {
    std::string path = "config/hamspirit_highscores.dat";
    try {
        std::filesystem::create_directories("config");
        std::ofstream ofs(path);
        if (!ofs) {
            log("HAMSPIRIT", "Failed to save high scores to " + path);
            return;
        }
        ofs << "# Ham Spirit High Scores\n";
        for (const auto& e : highScores) {
            ofs << e.callsign << "|" << e.playerName << "|" << e.score << "|"
                << e.lapsCompleted << "|" << e.gameTime << "|" << e.paHealth << "|"
                << (e.bonusAchieved ? "1" : "0") << "|"
                << e.gameMode << "|" << e.playerCount << "\n";
        }
        ofs.close();
        log("HAMSPIRIT", "Saved " + std::to_string(highScores.size()) + " high scores");
    } catch (const std::exception& e) {
        log("HAMSPIRIT", std::string("High score save error: ") + e.what());
    }
}

void Game::addHighScoreEntry() {
    HighScoreEntry entry;
    entry.callsign = currentPlayerCallsign;
    entry.playerName = currentPlayerName;
    entry.score = stats.score;
    entry.lapsCompleted = stats.lapsCompleted;
    entry.gameTime = stats.gameTime;
    entry.paHealth = stats.paHealth;
    entry.bonusAchieved = stats.bonusAchieved;
    entry.gameMode = "SP";
    entry.playerCount = 1;
    
    loadHighScores();
    highScores.push_back(entry);
    std::sort(highScores.begin(), highScores.end(),
        [](const HighScoreEntry& a, const HighScoreEntry& b) { return a.score > b.score; });
    if (highScores.size() > 20) highScores.resize(20);
    saveHighScores();
    
    // Find rank by score (entry's position after sort)
    int rank = 1;
    for (const auto& e : highScores) {
        if (e.score <= entry.score) break;
        rank++;
    }
    
    if (tts && tts->isAvailable()) {
        std::string msg = "Rank " + std::to_string(rank) + " on the leaderboard.";
        tts->speak(msg, shouldInterruptTts(false));
    }
}

void Game::showLeaderboard() {
    loadHighScores();
    log("HAMSPIRIT", "Showing leaderboard");
    
    if (highScores.empty()) {
        if (tts && tts->isAvailable()) {
            speakTranslated("HAMSPIRIT_LEADERBOARD_EMPTY", "No scores yet. Play a game first!", true);
        }
        showTextOverlay("No scores yet. Play a game first!");
        waitForInput(5.0f);
        hideTextOverlay();
        return;
    }
    
    // Build menu items from high scores
    int shown = std::min(static_cast<int>(highScores.size()), 10);
    std::vector<std::string> menuItems;
    std::vector<std::string> speechItems;  // TTS-friendly versions with phonetic callsigns
    for (int i = 0; i < shown; i++) {
        const auto& e = highScores[i];
        // Display version (for GUI menu overlay)
        std::string display = std::to_string(i + 1) + ". ";
        if (!e.callsign.empty()) {
            display += e.callsign + ", ";
        }
        if (!e.playerName.empty()) {
            display += e.playerName + ": ";
        }
        display += std::to_string(e.score) + " pts, ";
        int mins = static_cast<int>(e.gameTime) / 60;
        int secs = static_cast<int>(e.gameTime) % 60;
        display += std::to_string(mins) + "m " + std::to_string(secs) + "s";
        if (e.bonusAchieved) display += " [HAMSPIRIT]";
        if (e.gameMode == "MP") display += " [MP" + std::to_string(e.playerCount) + "P]";
        menuItems.push_back(display);
        
        // TTS version (phonetic callsign)
        std::string speech = std::to_string(i + 1) + ". ";
        if (!e.callsign.empty()) {
            speech += callsignToPhonetic(e.callsign) + ", ";
        }
        if (!e.playerName.empty()) {
            speech += e.playerName + ": ";
        }
        speech += std::to_string(e.score) + " points, ";
        speech += std::to_string(mins) + " minutes " + std::to_string(secs) + " seconds";
        if (e.bonusAchieved) speech += ", HAMSPIRIT bonus";
        if (e.gameMode == "MP") speech += ", multiplayer " + std::to_string(e.playerCount) + " players";
        speechItems.push_back(speech);
    }
    // Add "Back" item at the end
    std::string backLabel = translation ? translation->get("HAMSPIRIT_CONFIG_BACK", "Back") : "Back";
    menuItems.push_back(backLabel);
    speechItems.push_back(backLabel);
    
    int currentIndex = 0;
    bool prevUp = false, prevDown = false, prevAccept = false, prevBack = false;
    
    // Initial announcement and menu display
    if (tts && tts->isAvailable()) {
        speakTranslated("HAMSPIRIT_LEADERBOARD_TITLE", "Leaderboard", true);
    }
    updateMenuOverlay(translation ? translation->get("HAMSPIRIT_LEADERBOARD_TITLE", "Leaderboard") : "Leaderboard",
                      menuItems, currentIndex);
    
    // Speak first entry after a short delay for the title
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    if (tts && tts->isAvailable() && !shouldExit) {
        tts->speak(speechItems[currentIndex], shouldInterruptTts(true));
    }
    updateBrailleDisplay(menuItems[currentIndex]);
    
    // Navigation loop
    float localDebounce = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();
    
    while (!shouldExit) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        if (localDebounce > 0.0f) localDebounce -= dt;
        
        pollKeyboard();
        GamepadState input = getCurrentInput();
        
        bool rawUp = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                     || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -STICK_MENU_DEADZONE;
        bool rawDown = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                       || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > STICK_MENU_DEADZONE;
        bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
        bool back = input.buttons[static_cast<int>(GamepadButton::B)];
        
        bool up = rawUp && localDebounce <= 0.0f;
        bool down = rawDown && localDebounce <= 0.0f;
        
        if (up && !prevUp) {
            if (currentIndex > 0) {
                currentIndex--;
                triggerMenuNavSound();
                if (tts && tts->isAvailable()) {
                    tts->speak(speechItems[currentIndex], shouldInterruptTts(true));
                }
                updateMenuOverlay(translation ? translation->get("HAMSPIRIT_LEADERBOARD_TITLE", "Leaderboard") : "Leaderboard",
                                  menuItems, currentIndex);
                updateBrailleDisplay(menuItems[currentIndex]);
                localDebounce = DPAD_DEBOUNCE_TIME;
            } else {
                triggerBumperSound();
            }
        }
        if (down && !prevDown) {
            if (currentIndex < static_cast<int>(menuItems.size()) - 1) {
                currentIndex++;
                triggerMenuNavSound();
                if (tts && tts->isAvailable()) {
                    tts->speak(speechItems[currentIndex], shouldInterruptTts(true));
                }
                updateMenuOverlay(translation ? translation->get("HAMSPIRIT_LEADERBOARD_TITLE", "Leaderboard") : "Leaderboard",
                                  menuItems, currentIndex);
                updateBrailleDisplay(menuItems[currentIndex]);
                localDebounce = DPAD_DEBOUNCE_TIME;
            } else {
                triggerBumperSound();
            }
        }
        // Accept or Back both return to main menu
        if ((accept && !prevAccept) || (back && !prevBack)) {
            triggerMenuSelectSound();
            break;
        }
        
        prevUp = rawUp; prevDown = rawDown; prevAccept = accept; prevBack = back;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    hideMenuOverlay();
}

// ============================================================================
// Braille display output via NVDA
// ============================================================================
// Build localized braille status string for gameplay display
std::string Game::buildGameplayBrailleString() const {
    std::string brl;
    float speedKHz = std::abs(playerSpeed) * kHzPerRadian;
    float swr = getCurrentSWR();
    // Use translated labels for braille (German: GES/SWR/PA/PK/RD, English: SPD/SWR/PA/SC/LP)
    std::string lblSpeed = translation ? translation->get("HAMSPIRIT_BRL_SPEED", "SPD") : "SPD";
    std::string lblSWR = translation ? translation->get("HAMSPIRIT_BRL_SWR", "SWR") : "SWR";
    std::string lblPA = translation ? translation->get("HAMSPIRIT_BRL_PA", "PA") : "PA";
    std::string lblScore = translation ? translation->get("HAMSPIRIT_BRL_SCORE", "SC") : "SC";
    std::string lblLap = translation ? translation->get("HAMSPIRIT_BRL_LAP", "LP") : "LP";
    if (config.brailleShowSpeed) brl += lblSpeed + ":" + std::to_string(static_cast<int>(speedKHz)) + " ";
    // Frequency display: interpolated MHz at current track position
    if (config.brailleShowFreq && !track.empty()) {
        std::string lblFreq = translation ? translation->get("HAMSPIRIT_BRL_FREQ", "FRQ") : "FRQ";
        TrackPoint tp = TrackGenerator::interpolateAt(track, playerAngle);
        if (tp.frequency > 0.0f) {
            char freqBuf[16];
            std::snprintf(freqBuf, sizeof(freqBuf), "%.2f", tp.frequency / 1e6f);
            brl += lblFreq + ":" + std::string(freqBuf) + " ";
        }
    }
    if (config.brailleShowSWR) {
        brl += lblSWR + ":" + formatSWR(swr) + " ";
    }
    if (config.brailleShowPA) brl += lblPA + ":" + formatPAHealth(stats.paHealth) + "% ";
    if (config.brailleShowScore) brl += lblScore + ":" + formatScore(stats.score) + " ";
    if (config.brailleShowLap) brl += lblLap + ":" + std::to_string(stats.lapsCompleted) + " ";
    return brl;
}

void Game::updateBrailleDisplay(const std::string& text) {
    if (!config.brailleEnabled) return;
    
    // Send to NVDA braille display (works regardless of TTS engine selection)
    sendNvdaBrailleMessage(text);
    
    // NOTE: We intentionally do NOT update the GUI window title with braille text.
    // Changing the window title triggers accessibility notifications that can
    // override the braille display content with window title fragments like "H",
    // defeating the purpose of showing game-relevant braille content.
}

// ══════════════════════════════════════════════════════════════════════════════
// Multiplayer implementation
// ══════════════════════════════════════════════════════════════════════════════

int Game::showGameModeSelectionMenu() {
    log("HAMSPIRIT", "showGameModeSelectionMenu() entered");
    if (!tts || !tts->isAvailable()) {
        log("HAMSPIRIT", "showGameModeSelectionMenu: TTS not available, returning 0 (singleplayer)");
        return 0;
    }

    int selection = 0; // 0=Singleplayer, 1=Multiplayer

    auto tr = [this](const std::string& key, const std::string& fb) {
        return translation ? translation->get(key, fb) : fb;
    };
    std::vector<std::string> items = {
        tr("HAMSPIRIT_GAMEMODE_SINGLE", "Singleplayer"),
        tr("HAMSPIRIT_GAMEMODE_MULTI", "Multiplayer")
    };
    updateMenuOverlay(tr("HAMSPIRIT_GAMEMODE_TITLE", "Game Mode"), items, 0);

    tts->speak(tr("HAMSPIRIT_GAMEMODE_PROMPT",
        "Game mode. Use up and down to select, then confirm.") + " " + items[0] + ".",
               shouldInterruptTts(true));

    // Pre-seed previous button state from current input to avoid
    // immediately triggering an action if a key is still held from
    // the main menu (e.g. Enter held from selecting "New Game").
    pollKeyboard();
    if (gamepad) gamepad->update();
    GamepadState initInput = getCurrentInput();
    bool prevUp = initInput.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                  || initInput.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
    bool prevDown = initInput.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                    || initInput.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
    bool prevAccept = initInput.buttons[static_cast<int>(GamepadButton::A)];
    bool prevBack = initInput.buttons[static_cast<int>(GamepadButton::B)];

    while (!shouldExit) {
        pollKeyboard();
        if (gamepad) gamepad->update();
        GamepadState input = getCurrentInput();

        bool up = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                  || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
        bool down = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                    || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
        bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
        bool back = input.buttons[static_cast<int>(GamepadButton::B)];

        if (back && !prevBack) {
            hideMenuOverlay();
            return -1; // Back to main menu (cancel)
        }

        if (up && !prevUp && selection > 0) {
            selection--;
            triggerMenuNavSound();
            tts->speak(items[selection], shouldInterruptTts(true));
            updateMenuOverlay(tr("HAMSPIRIT_GAMEMODE_TITLE", "Game Mode"), items, selection);
        }
        if (down && !prevDown && selection < 1) {
            selection++;
            triggerMenuNavSound();

            // Check if multiplayer is possible (need ≥2 audio devices)
            if (!MultiplayerManager::hasEnoughAudioDevices(2)) {
                tts->speak(items[selection] + ". " + tr("HAMSPIRIT_GAMEMODE_MP_WARNING",
                           "Warning: Multiplayer requires at least 2 audio output devices. "
                           "Only 1 detected. Please connect additional sound cards."),
                           shouldInterruptTts(true));
            } else {
                tts->speak(items[selection], shouldInterruptTts(true));
            }
            updateMenuOverlay(tr("HAMSPIRIT_GAMEMODE_TITLE", "Game Mode"), items, selection);
        }
        if (accept && !prevAccept) {
            triggerMenuSelectSound();
            if (selection == 1) {
                if (!MultiplayerManager::hasEnoughAudioDevices(2)) {
                    tts->speak("Cannot start multiplayer. At least 2 audio output devices are required.",
                               shouldInterruptTts(true));
                    selection = 0;
                    updateMenuOverlay("Game Mode", items, selection);
                    prevUp = up; prevDown = down; prevAccept = accept; prevBack = back;
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                    continue;
                }
                return 1; // Multiplayer selected
            }
            log("HAMSPIRIT", "showGameModeSelectionMenu: Singleplayer selected");
            return 0; // Singleplayer selected
        }

        prevUp = up; prevDown = down; prevAccept = accept; prevBack = back;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return -1;  // shouldExit — treat as cancel
}

void Game::runMultiplayerSetupMenu() {
    log("HAMSPIRIT_SETUP", "runMultiplayerSetupMenu() entered");
    if (!tts || !tts->isAvailable()) {
        log("HAMSPIRIT_SETUP", "TTS not available, aborting setup");
        return;
    }

    log("HAMSPIRIT_SETUP", "Enumerating audio devices...");
    auto audioDevices = MultiplayerManager::enumerateAudioDevices();
    int numDevices = static_cast<int>(audioDevices.size());
    log("HAMSPIRIT_SETUP", "Found " + std::to_string(numDevices) + " audio devices");
    for (int i = 0; i < numDevices; i++) {
        log("HAMSPIRIT_SETUP", "  Device " + std::to_string(i) + ": " + audioDevices[i].name +
            " (index=" + std::to_string(audioDevices[i].deviceIndex) + ")");
    }

    auto tr = [this](const std::string& key, const std::string& fb) {
        return translation ? translation->get(key, fb) : fb;
    };

    // Step 1: Select number of players (2-4)
    log("HAMSPIRIT_SETUP", "Step 1: Player count selection");
    int playerCount = 2;
    tts->speak(tr("HAMSPIRIT_MP_SELECT_PLAYERS", "Select number of players.") + " 2 " + tr("HAMSPIRIT_MP_PLAYERS", "Players") + ".", shouldInterruptTts(true));
    {
        std::vector<std::string> items;
        for (int i = 2; i <= std::min(4, numDevices); i++)
            items.push_back(std::to_string(i) + " " + tr("HAMSPIRIT_MP_PLAYERS", "Players"));
        updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER_COUNT_TITLE", "Multiplayer — Player Count"), items, 0);

        // Pre-seed prev state from current input to avoid instant-trigger
        pollKeyboard();
        if (gamepad) gamepad->update();
        GamepadState initInput = getCurrentInput();
        bool prevUp = initInput.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                      || initInput.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
        bool prevDown = initInput.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                        || initInput.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
        bool prevAccept = initInput.buttons[static_cast<int>(GamepadButton::A)];
        bool prevBack = initInput.buttons[static_cast<int>(GamepadButton::B)];
        while (!shouldExit) {
            pollKeyboard();
            if (gamepad) gamepad->update();
            GamepadState input = getCurrentInput();

            bool up = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                      || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
            bool down = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                        || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
            bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
            bool back = input.buttons[static_cast<int>(GamepadButton::B)];

            if (back && !prevBack) {
                // Cancel — return to game mode selection
                return;
            }
            if (up && !prevUp && playerCount > 2) {
                playerCount--;
                triggerMenuNavSound();
                tts->speak(std::to_string(playerCount) + " " + tr("HAMSPIRIT_MP_PLAYERS", "Players"), shouldInterruptTts(true));
                updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER_COUNT_TITLE", "Multiplayer — Player Count"), items, playerCount - 2);
            }
            if (down && !prevDown && playerCount < std::min(4, numDevices)) {
                playerCount++;
                triggerMenuNavSound();
                tts->speak(std::to_string(playerCount) + " " + tr("HAMSPIRIT_MP_PLAYERS", "Players"), shouldInterruptTts(true));
                updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER_COUNT_TITLE", "Multiplayer — Player Count"), items, playerCount - 2);
            }
            if (accept && !prevAccept) {
                triggerMenuSelectSound();
                break;
            }
            prevUp = up; prevDown = down; prevAccept = accept; prevBack = back;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    multiplayerConfig.playerCount = playerCount;
    log("HAMSPIRIT_SETUP", "Player count selected: " + std::to_string(playerCount));

    // Step 2: Split orientation (only for 2-3 players; 4 players = 2×2 grid, no choice)
    log("HAMSPIRIT_SETUP", "Step 2: Split orientation selection");
    if (playerCount >= 2 && playerCount <= 3) {
        int splitSel = 0; // 0=horizontal, 1=vertical
        tts->speak(tr("HAMSPIRIT_MP_SCREEN_SPLIT", "Screen split.") + " " + tr("HAMSPIRIT_MP_HORIZONTAL", "Horizontal, side by side."), shouldInterruptTts(true));
        std::vector<std::string> items = {tr("HAMSPIRIT_MP_HORIZONTAL", "Horizontal (side by side)"), tr("HAMSPIRIT_MP_VERTICAL", "Vertical (top and bottom)")};
        updateMenuOverlay(tr("HAMSPIRIT_MP_SPLIT_TITLE", "Multiplayer — Screen Split"), items, 0);

        // Pre-seed prev state from current input to avoid instant-trigger
        pollKeyboard();
        if (gamepad) gamepad->update();
        GamepadState initSplit = getCurrentInput();
        bool prevUp = initSplit.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                      || initSplit.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
        bool prevDown = initSplit.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                        || initSplit.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
        bool prevAccept = initSplit.buttons[static_cast<int>(GamepadButton::A)];
        bool prevBack = initSplit.buttons[static_cast<int>(GamepadButton::B)];
        while (!shouldExit) {
            pollKeyboard();
            if (gamepad) gamepad->update();
            GamepadState input = getCurrentInput();

            bool up = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                      || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
            bool down = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                        || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
            bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
            bool back = input.buttons[static_cast<int>(GamepadButton::B)];

            if (back && !prevBack) return;  // Cancel
            if ((up && !prevUp) || (down && !prevDown)) {
                splitSel = 1 - splitSel;
                triggerMenuNavSound();
                tts->speak(splitSel == 0 ? tr("HAMSPIRIT_MP_HORIZONTAL", "Horizontal, side by side") : tr("HAMSPIRIT_MP_VERTICAL", "Vertical, top and bottom"),
                           shouldInterruptTts(true));
                updateMenuOverlay(tr("HAMSPIRIT_MP_SPLIT_TITLE", "Multiplayer — Screen Split"), items, splitSel);
            }
            if (accept && !prevAccept) {
                triggerMenuSelectSound();
                break;
            }
            prevUp = up; prevDown = down; prevAccept = accept; prevBack = back;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        multiplayerConfig.splitOrientation = splitSel == 0
            ? SplitOrientation::HORIZONTAL : SplitOrientation::VERTICAL;
    }

    // Step 3: Assign controllers and audio devices per player
    log("HAMSPIRIT_SETUP", "Step 3: Per-player controller and audio device assignment");
    int connectedGamepads = gamepad ? gamepad->getConnectedCount() : 0;
    log("HAMSPIRIT_SETUP", "Connected gamepads: " + std::to_string(connectedGamepads));

    for (int p = 0; p < playerCount && !shouldExit; p++) {
        log("HAMSPIRIT_SETUP", "--- Player " + std::to_string(p + 1) + " setup ---");
        // ── Controller assignment ──
        log("HAMSPIRIT_SETUP", "Player " + std::to_string(p + 1) + ": controller assignment");
        tts->speak(tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + ". " + tr("HAMSPIRIT_MP_SELECT_INPUT", "Select input device."),
                    shouldInterruptTts(true));

        int inputSel = 0; // 0=keyboard, 1..N=gamepad index
        int maxInput = connectedGamepads; // 0=keyboard only

        // Build list of available inputs, skipping already-assigned ones
        std::vector<std::string> inputItems;
        std::vector<int> inputValues; // 0=keyboard, 1..N=gamepad index

        // Check if keyboard is already assigned to a previous player
        bool keyboardTaken = false;
        for (int prev = 0; prev < p; prev++) {
            if (multiplayerConfig.inputAssignments[prev].type == InputSourceType::KEYBOARD)
                keyboardTaken = true;
        }
        if (!keyboardTaken) {
            inputItems.push_back(tr("HAMSPIRIT_INPUT_KEYBOARD", "Keyboard"));
            inputValues.push_back(0);
        }

        // Add gamepads that are not already assigned to previous players.
        // Iterate all 4 possible controller slots (not just 0..count-1) because
        // controllers may occupy non-contiguous slots (e.g. Xbox at slot 0, PS4 at slot 2).
        for (int g = 0; g < 4; g++) {
            if (gamepad && !gamepad->isConnected(g)) continue;
            bool alreadyAssigned = false;
            for (int prev = 0; prev < p; prev++) {
                if (multiplayerConfig.inputAssignments[prev].type == InputSourceType::GAMEPAD &&
                    multiplayerConfig.inputAssignments[prev].gamepadIndex == g) {
                    alreadyAssigned = true;
                    break;
                }
            }
            if (!alreadyAssigned) {
                std::string name = gamepad ? gamepad->getControllerName(g) : ("Gamepad " + std::to_string(g + 1));
                inputItems.push_back(name);
                inputValues.push_back(g + 1); // 1-based (0=keyboard)
            }
        }

        if (inputItems.empty()) {
            tts->speak("No more input devices available. Please connect more controllers.",
                        shouldInterruptTts(true));
            return;
        }

        int inputIdx = 0;
        tts->speak(inputItems[0], shouldInterruptTts(true));
        updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + " — " + tr("HAMSPIRIT_MP_INPUT", "Input"), inputItems, 0);

        {
            // Pre-seed prev state from current input to avoid instant-trigger
            pollKeyboard();
            if (gamepad) gamepad->update();
            GamepadState initCtrl = getCurrentInput();
            bool prevUp = initCtrl.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                          || initCtrl.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
            bool prevDown = initCtrl.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                            || initCtrl.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
            bool prevAccept = initCtrl.buttons[static_cast<int>(GamepadButton::A)];
            bool prevBack = initCtrl.buttons[static_cast<int>(GamepadButton::B)];
            while (!shouldExit) {
                pollKeyboard();
                if (gamepad) gamepad->update();
                GamepadState input = getCurrentInput();

                bool up = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                          || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
                bool down = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                            || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
                bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
                bool back = input.buttons[static_cast<int>(GamepadButton::B)];

                if (back && !prevBack) return;  // Cancel
                if (up && !prevUp && inputIdx > 0) {
                    inputIdx--;
                    triggerMenuNavSound();
                    tts->speak(inputItems[inputIdx], shouldInterruptTts(true));
                    updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + " — " + tr("HAMSPIRIT_MP_INPUT", "Input"), inputItems, inputIdx);
                }
                if (down && !prevDown && inputIdx < static_cast<int>(inputItems.size()) - 1) {
                    inputIdx++;
                    triggerMenuNavSound();
                    tts->speak(inputItems[inputIdx], shouldInterruptTts(true));
                    updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + " — " + tr("HAMSPIRIT_MP_INPUT", "Input"), inputItems, inputIdx);
                }
                if (accept && !prevAccept) {
                    triggerMenuSelectSound();
                    break;
                }
                prevUp = up; prevDown = down; prevAccept = accept; prevBack = back;
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        }

        int selectedInputValue = inputValues[inputIdx];
        log("HAMSPIRIT_SETUP", "Player " + std::to_string(p + 1) + ": selected input index " +
            std::to_string(selectedInputValue) + " (" + inputItems[inputIdx] + ")");
        if (selectedInputValue == 0) {
            multiplayerConfig.inputAssignments[p].type = InputSourceType::KEYBOARD;
        } else {
            multiplayerConfig.inputAssignments[p].type = InputSourceType::GAMEPAD;
            multiplayerConfig.inputAssignments[p].gamepadIndex = selectedInputValue - 1;

            // ── Controller profile selection (per-player) ──
            log("HAMSPIRIT_SETUP", "Player " + std::to_string(p + 1) + ": controller profile selection");
            const char* presetNames[] = {"Auto-detect", "Xbox", "PlayStation"};
            std::vector<std::string> presetItems = {"Auto-detect", "Xbox", "PlayStation"};
            int presetIdx = 0;
            tts->speak(tr("HAMSPIRIT_MP_SELECT_CONTROLLER", "Select controller profile for") + " " + tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + ". Auto-detect.",
                        shouldInterruptTts(true));
            updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + " — " + tr("HAMSPIRIT_MP_CONTROLLER_PROFILE", "Controller Profile"), presetItems, 0);
            {
                pollKeyboard();
                if (gamepad) gamepad->update();
                GamepadState initPreset = getCurrentInput();
                bool prevUp = initPreset.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                              || initPreset.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -STICK_MENU_DEADZONE;
                bool prevDown = initPreset.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                                || initPreset.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > STICK_MENU_DEADZONE;
                bool prevAccept = initPreset.buttons[static_cast<int>(GamepadButton::A)];
                bool prevBack = initPreset.buttons[static_cast<int>(GamepadButton::B)];
                while (!shouldExit) {
                    pollKeyboard();
                    if (gamepad) gamepad->update();
                    GamepadState input = getCurrentInput();

                    bool up = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                              || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -STICK_MENU_DEADZONE;
                    bool down = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                                || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > STICK_MENU_DEADZONE;
                    bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
                    bool back = input.buttons[static_cast<int>(GamepadButton::B)];

                    if (back && !prevBack) return;  // Cancel
                    if (up && !prevUp && presetIdx > 0) {
                        presetIdx--;
                        triggerMenuNavSound();
                        tts->speak(presetNames[presetIdx], shouldInterruptTts(true));
                        updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + " — " + tr("HAMSPIRIT_MP_CONTROLLER_PROFILE", "Controller Profile"), presetItems, presetIdx);
                    }
                    if (down && !prevDown && presetIdx < 2) {
                        presetIdx++;
                        triggerMenuNavSound();
                        tts->speak(presetNames[presetIdx], shouldInterruptTts(true));
                        updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + " — " + tr("HAMSPIRIT_MP_CONTROLLER_PROFILE", "Controller Profile"), presetItems, presetIdx);
                    }
                    if (accept && !prevAccept) {
                        triggerMenuSelectSound();
                        break;
                    }
                    prevUp = up; prevDown = down; prevAccept = accept; prevBack = back;
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
            }
            multiplayerConfig.controllerPresets[p] = presetIdx;
            log("HAMSPIRIT_SETUP", "Player " + std::to_string(p + 1) + ": controller profile = " +
                std::string(presetNames[presetIdx]));
        }

        // ── Audio device assignment ──
        log("HAMSPIRIT_SETUP", "Player " + std::to_string(p + 1) + ": audio device assignment");
        // Build list of available audio devices, skipping already-assigned ones
        std::vector<std::string> deviceItems;
        std::vector<int> deviceValues;
        for (int d = 0; d < numDevices; d++) {
            bool alreadyAssigned = false;
            for (int prev = 0; prev < p; prev++) {
                if (multiplayerConfig.audioDeviceIndices[prev] == audioDevices[d].deviceIndex) {
                    alreadyAssigned = true;
                    break;
                }
            }
            if (!alreadyAssigned) {
                deviceItems.push_back(audioDevices[d].name);
                deviceValues.push_back(audioDevices[d].deviceIndex);
            }
        }

        if (deviceItems.empty()) {
            tts->speak("No more audio output devices available.",
                        shouldInterruptTts(true));
            return;
        }

        tts->speak(tr("HAMSPIRIT_MP_SELECT_AUDIO", "Select audio output for") + " " + tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + ". " + deviceItems[0],
                    shouldInterruptTts(true));
        int deviceIdx = 0;
        updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + " — " + tr("HAMSPIRIT_MP_AUDIO_OUTPUT", "Audio Output"), deviceItems, 0);

        {
            // Pre-seed prev state from current input to avoid instant-trigger
            pollKeyboard();
            if (gamepad) gamepad->update();
            GamepadState initDev = getCurrentInput();
            bool prevUp = initDev.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                          || initDev.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
            bool prevDown = initDev.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                            || initDev.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
            bool prevAccept = initDev.buttons[static_cast<int>(GamepadButton::A)];
            bool prevBack = initDev.buttons[static_cast<int>(GamepadButton::B)];
            while (!shouldExit) {
                pollKeyboard();
                if (gamepad) gamepad->update();
                GamepadState input = getCurrentInput();

                bool up = input.buttons[static_cast<int>(GamepadButton::DPAD_UP)]
                          || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] < -0.5f;
                bool down = input.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]
                            || input.axes[static_cast<int>(GamepadAxis::LEFT_Y)] > 0.5f;
                bool accept = input.buttons[static_cast<int>(GamepadButton::A)];
                bool back = input.buttons[static_cast<int>(GamepadButton::B)];

                if (back && !prevBack) return;  // Cancel
                if (up && !prevUp && deviceIdx > 0) {
                    deviceIdx--;
                    triggerMenuNavSound();
                    tts->speak(deviceItems[deviceIdx], shouldInterruptTts(true));
                    updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + " — " + tr("HAMSPIRIT_MP_AUDIO_OUTPUT", "Audio Output"), deviceItems, deviceIdx);
                }
                if (down && !prevDown && deviceIdx < static_cast<int>(deviceItems.size()) - 1) {
                    deviceIdx++;
                    triggerMenuNavSound();
                    tts->speak(deviceItems[deviceIdx], shouldInterruptTts(true));
                    updateMenuOverlay(tr("HAMSPIRIT_MP_PLAYER", "Player") + " " + std::to_string(p + 1) + " — " + tr("HAMSPIRIT_MP_AUDIO_OUTPUT", "Audio Output"), deviceItems, deviceIdx);
                }
                if (accept && !prevAccept) {
                    triggerMenuSelectSound();
                    break;
                }
                prevUp = up; prevDown = down; prevAccept = accept; prevBack = back;
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        }
        multiplayerConfig.audioDeviceIndices[p] = deviceValues[deviceIdx];
        log("HAMSPIRIT_SETUP", "Player " + std::to_string(p + 1) + ": selected audio device index " +
            std::to_string(deviceValues[deviceIdx]) + " (" + deviceItems[deviceIdx] + ")");

        // ── Callsign and name (per player) ──
        log("HAMSPIRIT_SETUP", "Player " + std::to_string(p + 1) + ": callsign/name input");
        std::string callsign = promptTextInput(
            "Player " + std::to_string(p + 1) + " callsign (or leave empty):", 15);
        if (shouldExit) break;
        std::string name = promptTextInput(
            "Player " + std::to_string(p + 1) + " name:", 20);
        if (shouldExit) break;

        // Store callsign and name for ALL players
        multiplayerConfig.playerCallsigns[p] = callsign;
        multiplayerConfig.playerNames[p] = name;
        log("HAMSPIRIT_SETUP", "Player " + std::to_string(p + 1) + ": callsign='" + callsign +
            "' name='" + name + "'");

        // Also set the legacy single-player fields from player 0
        if (p == 0) {
            currentPlayerCallsign = callsign;
            currentPlayerName = name;
        }

        // Phonetic readback of callsign
        if (!callsign.empty() && tts && tts->isAvailable()) {
            std::string phonetic = callsignToPhonetic(callsign);
            log("HAMSPIRIT_SETUP", "Player " + std::to_string(p + 1) + ": phonetic readback: " + phonetic);
            tts->speak("Player " + std::to_string(p + 1) + ": " + phonetic,
                        shouldInterruptTts(true));
            log("HAMSPIRIT_SETUP", "Player " + std::to_string(p + 1) + ": waiting for input after phonetic readback");
            waitForInput(3.0f);
            log("HAMSPIRIT_SETUP", "Player " + std::to_string(p + 1) + ": phonetic readback wait complete");
        }
    }
    log("HAMSPIRIT_SETUP", "Per-player loop complete, shouldExit=" + std::to_string(shouldExit.load()));

    // Step 4: Braille context selection
    // Auto-assign player 0 as braille recipient.  The braille player index
    // can be changed later from the in-game configuration menu.  Showing a
    // blocking selection menu here caused the setup flow to appear frozen
    // because most users don't have a braille display connected even though
    // brailleEnabled defaults to true.
    multiplayerConfig.braillePlayerIndex = 0;

    // Step 5: Check TTS multi-output compatibility
    log("HAMSPIRIT", "Checking TTS multi-output for engine: " +
        std::string(getTTSEngineLabel(config.ttsEngine)));
    if (!ttsSupportsMultiOutput(static_cast<int>(config.ttsEngine))) {
        log("HAMSPIRIT", "TTS engine does not support multi-output — showing warning");
        tts->speak("Note: The current speech engine does not support independent output "
                    "to multiple audio devices. All players will share a single TTS output. "
                    "Consider switching to espeak-NG or macOS Say for independent per-player speech.",
                    shouldInterruptTts(true));
        waitForInput(8.0f);
    } else {
        log("HAMSPIRIT", "TTS engine supports multi-output");
    }

    // Route the game's primary audio backend to player 0's configured device
    // BEFORE initializing the multiplayer manager.  On Windows the waveOut API
    // only allows one handle per device, so the game backend must already hold
    // the correct device before the multiplayer manager creates backends for
    // players 1+ (which open their own devices).
    //
    // IMPORTANT: Stop the audio thread before calling selectDevice().
    // The audio thread's recovery loop (resetAbort + continuous playBuffer retries)
    // races with waveOutReset inside selectDevice, causing some Windows audio
    // drivers (notably Realtek) to block waveOutReset indefinitely.
    // Stopping the thread eliminates all concurrent access during device switching.
    log("HAMSPIRIT", "Routing player 0 audio to device " +
        std::to_string(multiplayerConfig.audioDeviceIndices[0]));
    if (audioBackend && multiplayerConfig.audioDeviceIndices[0] >= 0) {
        // Stop audio thread
        if (audioRunning.load()) {
            log("HAMSPIRIT", "Stopping audio thread for device switch");
            audioRunning.store(false);
            if (audioBackend) audioBackend->abort();
            if (audioThread.joinable()) {
                for (int i = 0; i < 150 && !audioThreadExited.load(); i++) {
                    if (audioBackend) audioBackend->abort();
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                if (audioThreadExited.load()) {
                    audioThread.join();
                } else {
                    log("HAMSPIRIT", "WARNING: Audio thread did not exit in 3s — detaching");
                    audioThread.detach();
                }
            }
            log("HAMSPIRIT", "Audio thread stopped for device switch");
        }

        // Now switch device with no concurrent audio thread
        if (audioBackend) audioBackend->resetAbort();
        if (!audioBackend->selectDevice(multiplayerConfig.audioDeviceIndices[0])) {
            log("HAMSPIRIT", "WARNING: Failed to select audio device " +
                std::to_string(multiplayerConfig.audioDeviceIndices[0]) + " for player 0");
        } else {
            log("HAMSPIRIT", "Player 0 audio device selected successfully");
        }

        // Restart audio thread
        if (audioInitialized) {
            log("HAMSPIRIT", "Restarting audio thread after device switch");
            if (audioBackend) audioBackend->resetAbort();
            audioThreadExited.store(false);
            audioRunning.store(true);
            audioThread = std::thread(&Game::audioThreadFunc, this);
            log("HAMSPIRIT", "Audio thread restarted");
        }
    }

    // Initialize multiplayer manager (creates backends for players 1+)
    log("HAMSPIRIT", "Initializing multiplayer manager for " +
        std::to_string(playerCount) + " players");
    multiplayerMgr = std::make_unique<MultiplayerManager>();
    multiplayerMgr->setLogger(logger);
    multiplayerMgr->initialize(multiplayerConfig, track);
    log("HAMSPIRIT", "Multiplayer manager initialized");

    // Set callsigns and names on the player contexts
    for (int p = 0; p < playerCount; p++) {
        auto* ctx = multiplayerMgr->getPlayer(p);
        if (ctx) {
            ctx->callsign = multiplayerConfig.playerCallsigns[p];
            ctx->playerName = multiplayerConfig.playerNames[p];
        }
    }

    log("HAMSPIRIT", "Multiplayer setup complete, announcing to user");
    tts->speak("Multiplayer setup complete. " + std::to_string(playerCount) + " players ready.",
               shouldInterruptTts(true));
    waitForInput(3.0f);
    log("HAMSPIRIT", "Multiplayer setup menu finished");
}

// ── Multiplayer-aware interaction methods ─────────────────────────────────────
// These generalized methods accept a playerIndex parameter so that secondary
// players (1+) can interact with game entities identically to player 0.
// For player 0, they delegate to the existing single-player logic.

void Game::checkMorseCollectionForPlayer(char sentChar, int playerIndex) {
    if (!morseSignalManager || !morseDatabase || track.empty()) return;
    if (sentChar == '\0') return;
    
    PlayerContext* ctx = multiplayerMgr ? multiplayerMgr->getPlayer(playerIndex) : nullptr;
    if (!ctx) {
        // Fallback for singleplayer without multiplayer manager
        if (playerIndex == 0) { checkMorseCollection(sentChar); return; }
        return;
    }
    
    // Route through the central authority when available.
    // The authority validates proximity, prevents duplicate claims,
    // and generates collection events for ALL players identically.
    if (gameAuthority && gameAuthority->isActive()) {
        PlayerAction action;
        action.type = PlayerActionType::MORSE_CANNON_FIRE;
        action.playerId = playerIndex;
        action.angle = ctx->playerAngle;
        action.morseChar = sentChar;
        action.timestamp = std::chrono::steady_clock::now();
        gameAuthority->processAction(action);
    }
    
    // Get player-specific position and aim — all players use ctx, no Player0 special case
    float pAngle = ctx->playerAngle;
    float pAimAngle = ctx->aimAngle;
    GameStats* pStats = ctx->stats ? ctx->stats : &stats;
    
    const float AIM_MARGIN = config.elemAutoAim ? static_cast<float>(PI) : MORSE_AIM_MARGIN_NORMAL;
    MorseSignal* target = morseSignalManager->getTargetedSignal(pAngle, pAimAngle, AIM_MARGIN);
    
    if (!target) {
        log("HAMSPIRIT_MORSE", "Player " + std::to_string(playerIndex) + ": No signal in aim");
        // Audio feedback via player's own audio params
        if (playerIndex == 0) {
            triggerMissMorseSound();
        } else {
            std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
            ctx->audioState.missMorseSoundFrames = msToFrames(120);
        }
        return;
    }
    
    // Atomic claim check — prevent race conditions
    if (target->collected) return;
    
    TrackPoint currentPoint = TrackGenerator::interpolateAt(track, pAngle);
    float reactance = currentPoint.reactance;
    bool collected = morseSignalManager->tryCollectSignal(target, sentChar, reactance);
    
    if (collected) {
        target->collectedByPlayer = playerIndex;
        ctx->morseMissCount = 0;
        ctx->collectedChars.push_back(target->character);
        pStats->charactersCollected++;
        pStats->score += MORSE_COLLECTION_SCORE_PER;
        
        // PA repair
        float repairAmount = MORSE_PA_REPAIR_SHORT;
        std::string pattern = morseDatabase->getPattern(target->character);
        if (pattern.length() >= 3) repairAmount = MORSE_PA_REPAIR_MEDIUM;
        if (pattern.length() >= 4) repairAmount = MORSE_PA_REPAIR_LONG;
        float oldHealth = ctx->paHealth;
        ctx->paHealth = std::min(1.0f, ctx->paHealth + repairAmount);
        pStats->paHealth = ctx->paHealth;
        
        // Audio feedback
        if (playerIndex == 0) {
            triggerCollectSound();
            if (ctx->paHealth > oldHealth) triggerPaRepairSound();
        } else {
            std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
            ctx->audioState.collectSoundFrames = msToFrames(160);
            if (ctx->paHealth > oldHealth) ctx->audioState.paRepairSoundFrames = msToFrames(160);
        }
        
        // Push event for other players to hear
        if (multiplayerMgr) {
            multiplayerMgr->pushEvent(GameEvent(
                GameEventType::MORSE_COLLECTED, playerIndex, pAngle, ctx->playerLateralOffset));
        }
        
        log("HAMSPIRIT_MORSE", "Player " + std::to_string(playerIndex) + 
            " collected: " + std::string(1, target->character));
        
        // Check HAMSPIRIT bonus for this player
        std::string hamTarget = "HAMSPIRIT";
        bool allFound = true;
        for (char c : hamTarget) {
            bool found = false;
            for (char cc : ctx->collectedChars) {
                if (std::toupper(cc) == c) { found = true; break; }
            }
            if (!found) { allFound = false; break; }
        }
        if (allFound && !ctx->hamSpiritBonusAchieved) {
            ctx->hamSpiritBonusAchieved = true;
            pStats->bonusAchieved = true;
            pStats->score += MORSE_HAMSPIRIT_BONUS;
            log("HAMSPIRIT_MORSE", "Player " + std::to_string(playerIndex) + " HAMSPIRIT bonus!");
        }
    } else {
        ctx->morseMissCount++;
        if (playerIndex == 0) {
            triggerMissAimSound();
        } else {
            std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
            ctx->audioState.missAimSoundFrames = msToFrames(100);
        }
        
        if (ctx->morseMissCount >= MORSE_MISS_THRESHOLD) {
            auto& signals = morseSignalManager->getSignalsMutable();
            for (auto it = signals.begin(); it != signals.end(); ++it) {
                if (&(*it) == target) { signals.erase(it); break; }
            }
            pStats->score = std::max(0, pStats->score - MORSE_MISS_PENALTY);
            ctx->morseMissCount = 0;
        }
    }
}

void Game::handleNoiseBlankerFireForPlayer(int playerIndex) {
    PlayerContext* ctx = multiplayerMgr ? multiplayerMgr->getPlayer(playerIndex) : nullptr;
    if (!ctx) {
        // Fallback for singleplayer without multiplayer manager
        if (playerIndex == 0) { handleNoiseBlankerFire(); return; }
        return;
    }
    
    // Route through the central authority when available.
    // The authority validates cooldowns, calculates hits, and generates events
    // identically for ALL players — no Player0 special logic.
    if (gameAuthority && gameAuthority->isActive()) {
        PlayerAction action;
        action.type = PlayerActionType::NOISE_BLANKER_FIRE;
        action.playerId = playerIndex;
        action.angle = ctx->playerAngle;
        action.aimAngle = ctx->aimAngle;
        action.timestamp = std::chrono::steady_clock::now();
        gameAuthority->processAction(action);
    }
    
    // All players use the same code path for game logic.
    // The playerIndex == 0 checks below are ONLY for audio routing
    // (player 0 uses the main audio thread, others use per-player threads).
    float pAngle = ctx->playerAngle;
    float pAimAngle = ctx->aimAngle;
    GameStats* pStats = ctx->stats ? ctx->stats : &stats;
    
    // Apply cooldown
    float cooldown = NOISE_BLANKER_COOLDOWN;
    ctx->noiseBlankerCooldown = cooldown;
    
    // Audio: fire sound
    if (playerIndex == 0) {
        triggerNoiseBlankerFireSound();
    } else {
        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
        ctx->audioState.noiseBlankerFireFrames = msToFrames(160);
    }
    
    // Push event for spatial audio propagation
    if (multiplayerMgr) {
        multiplayerMgr->pushEvent(GameEvent(
            GameEventType::NOISE_BLANKER_FIRE, playerIndex, pAngle, ctx->playerLateralOffset));
    }
    
    log("HAMSPIRIT", "Player " + std::to_string(playerIndex) + " fired noise blanker");
    
    // Check QSO Störer hit
    if (qsoStoerer.active) {
        float stoererDiff = qsoStoerer.angle - (pAngle + pAimAngle);
        while (stoererDiff > PI) stoererDiff -= TWO_PI;
        while (stoererDiff < -PI) stoererDiff += TWO_PI;
        float stoererDist = std::abs(stoererDiff);
        
        if (stoererDist < QSO_STOERER_HIT_MARGIN) {
            qsoStoerer.health = std::max(QSO_STOERER_DESTRUCTION_THRESHOLD,
                                          qsoStoerer.health - QSO_STOERER_HEALTH_PER_HIT);
            pStats->score += NOISE_BLANKER_QSO_HIT_SCORE;
            if (multiplayerMgr) {
                multiplayerMgr->pushEvent(GameEvent(
                    GameEventType::QSO_STOERER_HIT, playerIndex, qsoStoerer.angle));
            }
            log("HAMSPIRIT", "Player " + std::to_string(playerIndex) + " hit QSO Störer");
            return;
        }
    }
    
    // Check noise enemy hit
    NoiseEnemy* bestTarget = nullptr;
    float bestAngleDist = 999.0f;
    for (auto& enemy : noiseEnemies) {
        if (enemy.destroyed) continue;
        float angleDiff = enemy.angle - (pAngle + pAimAngle);
        while (angleDiff > PI) angleDiff -= TWO_PI;
        while (angleDiff < -PI) angleDiff += TWO_PI;
        float dist = std::abs(angleDiff);
        float hitMargin = enemy.bandwidth / 2.0f + NOISE_BLANKER_BW_HIT_EXTRA;
        if (dist < hitMargin && dist < bestAngleDist) {
            bestAngleDist = dist;
            bestTarget = &enemy;
        }
    }
    
    if (bestTarget) {
        bestTarget->health--;
        if (bestTarget->health <= 0) {
            bestTarget->destroyed = true;
            bestTarget->destroyedByPlayer = playerIndex;
            int bonus = NOISE_DESTROY_BONUS_BASE + static_cast<int>(static_cast<float>(NOISE_DESTROY_BONUS_BW_SCALE) * (1.0f - bestTarget->bandwidth));
            pStats->score += bonus;
            if (multiplayerMgr) {
                multiplayerMgr->pushEvent(GameEvent(
                    GameEventType::NOISE_ENEMY_DESTROYED, playerIndex, bestTarget->angle,
                    0.0f, static_cast<float>(bonus)));
            }
        } else {
            if (multiplayerMgr) {
                multiplayerMgr->pushEvent(GameEvent(
                    GameEventType::NOISE_ENEMY_HIT, playerIndex, bestTarget->angle));
            }
        }
        
        // Hit sound for players using per-player audio pipeline.
        // Player 0's hit sound is triggered by the main audio thread.
        // This is a rendering distinction, not a game logic privilege.
        if (playerIndex != 0) {
            std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
            ctx->audioState.noiseHitSoundFrames = msToFrames(160);
            ctx->audioState.noiseHitVariation = bestTarget->health % 4;
        }
        return;
    }
    
    // Check power-up hit
    for (size_t i = 0; i < powerUps.size(); i++) {
        if (powerUps[i].collected || powerUps[i].destroyed) continue;
        float angleDiff = powerUps[i].angle - (pAngle + pAimAngle);
        while (angleDiff > PI) angleDiff -= TWO_PI;
        while (angleDiff < -PI) angleDiff += TWO_PI;
        if (std::abs(angleDiff) < POWERUP_AIM_MARGIN) {
            handlePowerUpExplosion(static_cast<int>(i));
            return;
        }
    }
    
    // Check morse signal hit (penalty)
    const float AIM_MARGIN = NOISE_BLANKER_MORSE_AIM_MARGIN;
    if (morseSignalManager) {
        MorseSignal* morseTarget = morseSignalManager->getTargetedSignal(pAngle, pAimAngle, AIM_MARGIN);
        if (morseTarget && !morseTarget->collected) {
            pStats->score -= NOISE_BLANKER_MORSE_HIT_PENALTY;
        }
    }
}

void Game::handlePowerUpCollectionForPlayer(const GamepadState& input, float dt, int playerIndex) {
    // For player 0 in singleplayer, delegate to existing method
    if (playerIndex == 0 && (!multiplayerMgr || !multiplayerMgr->isMultiplayer())) {
        handlePowerUpCollection(input, dt);
        return;
    }
    
    PlayerContext* ctx = multiplayerMgr ? multiplayerMgr->getPlayer(playerIndex) : nullptr;
    if (!ctx) return;
    
    // All players read from their context — no Player0 special case
    float pAngle = ctx->playerAngle;
    float pAimAngle = ctx->aimAngle;
    GameStats* pStats = ctx->stats ? ctx->stats : &stats;
    
    float ltValue = input.axes[static_cast<int>(GamepadAxis::LEFT_TRIGGER)];
    float rtValue = input.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)];
    // Both triggers held: works for both gamepad and keyboard players.
    bool bothHeld = ltValue > POWERUP_TRIGGER_THRESHOLD && rtValue > POWERUP_TRIGGER_THRESHOLD;
    
    if (!bothHeld) return;
    
    // Find targeted power-up from this player's perspective
    float aimDirection = pAngle + pAimAngle;
    int targetIdx = -1;
    float bestDist = 999.0f;
    
    for (size_t i = 0; i < powerUps.size(); i++) {
        if (powerUps[i].collected || powerUps[i].destroyed) continue;
        float angleDiff = powerUps[i].angle - aimDirection;
        while (angleDiff > PI) angleDiff -= TWO_PI;
        while (angleDiff < -PI) angleDiff += TWO_PI;
        float dist = std::abs(angleDiff);
        if (dist < POWERUP_AIM_MARGIN && dist < bestDist) {
            bestDist = dist;
            targetIdx = static_cast<int>(i);
        }
    }
    
    if (targetIdx >= 0) {
        // Check if in zone
        float zoneDist = powerUps[targetIdx].angle - pAngle;
        while (zoneDist > PI) zoneDist -= TWO_PI;
        while (zoneDist < -PI) zoneDist += TWO_PI;
        bool inZone = std::abs(zoneDist) < powerUps[targetIdx].zoneHalfWidth;
        
        if (inZone && !powerUps[targetIdx].collected) {
            // Advance collection progress
            powerUps[targetIdx].collectionProgress += dt / powerUps[targetIdx].collectionTime;
            
            if (powerUps[targetIdx].collectionProgress >= 1.0f) {
                // Collected!
                powerUps[targetIdx].collected = true;
                powerUps[targetIdx].collectedByPlayer = playerIndex;
                activatePowerUp(powerUps[targetIdx].type, powerUps[targetIdx].quality);
                
                pStats->score += 100;
                
                // Trigger collect and activation sounds for the collecting player
                if (playerIndex != 0) {
                    std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                    ctx->audioState.collectSoundFrames = msToFrames(160);
                    ctx->audioState.powerUpActivateFrames = msToFrames(300);
                    ctx->audioState.powerUpActivationType = static_cast<int>(powerUps[targetIdx].type);
                }
                
                if (multiplayerMgr) {
                    multiplayerMgr->pushEvent(GameEvent(
                        GameEventType::POWERUP_COLLECTED, playerIndex,
                        powerUps[targetIdx].angle, 0.0f,
                        static_cast<float>(powerUps[targetIdx].type)));
                }
                
                log("HAMSPIRIT", "Player " + std::to_string(playerIndex) + 
                    " collected power-up type=" + std::to_string(static_cast<int>(powerUps[targetIdx].type)));
            }
        }
    }
}

void Game::addHighScoreEntryForPlayer(int playerIndex) {
    HighScoreEntry entry;
    bool isMultiplayer = multiplayerMgr && multiplayerMgr->isMultiplayer();
    
    if (isMultiplayer) {
        PlayerContext* ctx = multiplayerMgr->getPlayer(playerIndex);
        if (!ctx) return;
        GameStats* pStats = ctx->stats ? ctx->stats : &stats;
        entry.callsign = ctx->callsign;
        entry.playerName = ctx->playerName;
        entry.score = pStats->score;
        entry.lapsCompleted = pStats->lapsCompleted;
        entry.gameTime = pStats->gameTime;
        entry.paHealth = pStats->paHealth;
        entry.bonusAchieved = pStats->bonusAchieved;
        entry.gameMode = "MP";
        entry.playerCount = multiplayerMgr->getPlayerCount();
    } else {
        entry.callsign = currentPlayerCallsign;
        entry.playerName = currentPlayerName;
        entry.score = stats.score;
        entry.lapsCompleted = stats.lapsCompleted;
        entry.gameTime = stats.gameTime;
        entry.paHealth = stats.paHealth;
        entry.bonusAchieved = stats.bonusAchieved;
        entry.gameMode = "SP";
        entry.playerCount = 1;
    }
    
    loadHighScores();
    highScores.push_back(entry);
    std::sort(highScores.begin(), highScores.end(),
        [](const HighScoreEntry& a, const HighScoreEntry& b) { return a.score > b.score; });
    if (highScores.size() > 20) highScores.resize(20);
    saveHighScores();
    
    int rank = 1;
    for (const auto& e : highScores) {
        if (e.score <= entry.score) break;
        rank++;
    }
    
    if (tts && tts->isAvailable()) {
        std::string who = isMultiplayer ? ("Player " + std::to_string(playerIndex + 1) + ": ") : "";
        std::string msg = who + "Rank " + std::to_string(rank) + " on the leaderboard.";
        tts->speak(msg, shouldInterruptTts(false));
    }
}

void Game::gatherMultiplayerInput(float dt) {
    if (!multiplayerMgr || !multiplayerMgr->isMultiplayer()) return;

    int playerCount = multiplayerMgr->getPlayerCount();

    // Gather input for ALL players (0 through N-1) through the same code path.
    // All players are processed identically — no special Player 0 logic.
    for (int p = 0; p < playerCount; p++) {
        auto* ctx = multiplayerMgr->getPlayer(p);
        if (!ctx) continue;

        // Use the centralized input retrieval that applies stick drift calibration
        GamepadState state = getInputForPlayer(p);
        bool hasInput = state.connected;

        // Sync baseMaxSpeed to authority unconditionally so physics are never
        // blocked by a zero base speed even if the controller is momentarily lost.
        // Always use the original track-derived baseMaxSpeed — identical to the
        // singleplayer path.  The authority computes the effective maxSpeed by
        // applying SWR and PA-damage multipliers on top of this base each tick.
        // Using ctx->maxSpeed or the Game's maxSpeed here would feed the
        // *already-multiplied* value back as the base, causing exponential
        // speed decay because the authority would apply the multipliers again.
        if (gameAuthority && gameAuthority->isActive()) {
            auto& pws = gameAuthority->getPlayerStateMutable(p);
            pws.baseMaxSpeed = baseMaxSpeed;
        }

        // ── Send raw input to GameAuthority every frame ──
        // Do not gate this on state.connected: controller backends can report
        // transient connection flags while still delivering valid axis values.
        // The authority must always receive the latest raw input (including 0)
        // so movement/aiming stays responsive and deterministic for all players.
        float rawForward = -state.axes[static_cast<int>(GamepadAxis::LEFT_Y)];
        float rawSteer = state.axes[static_cast<int>(GamepadAxis::LEFT_X)];
        float rawAim = state.axes[static_cast<int>(GamepadAxis::RIGHT_X)];
        if (gameAuthority && gameAuthority->isActive()) {
            PlayerAction inputAction;
            inputAction.type = PlayerActionType::INPUT_UPDATE;
            inputAction.playerId = p;
            inputAction.forwardInput = rawForward;
            inputAction.steerInput = rawSteer;
            inputAction.aimInput = rawAim;
            inputAction.timestamp = std::chrono::steady_clock::now();
            gameAuthority->processAction(inputAction);
        }

        if (hasInput) {
            // ── Send current SWR to GameAuthority for this player ──
            AntennaNetwork* playerAntenna = ctx->antennaNetwork.get();
            if (gameAuthority && gameAuthority->isActive() && playerAntenna && !track.empty()) {
                float currentSWR = playerAntenna->calculateAdjustedSWR(track, ctx->playerAngle);
                PlayerAction tuneAction;
                tuneAction.type = PlayerActionType::ANTENNA_TUNE;
                tuneAction.playerId = p;
                tuneAction.value = currentSWR;
                tuneAction.timestamp = std::chrono::steady_clock::now();
                gameAuthority->processAction(tuneAction);
            }

            // ── Antenna tuner: Y/X face buttons = inductance up/down, B/A = capacitance up/down ──
            if (playerAntenna) {
                bool yBtn = state.buttons[static_cast<int>(GamepadButton::Y)];
                bool xBtn = state.buttons[static_cast<int>(GamepadButton::X)];
                bool bBtn = state.buttons[static_cast<int>(GamepadButton::B)];
                bool aBtn = state.buttons[static_cast<int>(GamepadButton::A)];

                // Skip individual button processing when all 4 are held simultaneously
                // (cheat gesture — suppress normal processing)
                bool cheatPressed = yBtn && xBtn && bBtn && aBtn;
                if (!cheatPressed) {
                    ctx->prevTunerCheat = false;
                    constexpr float stepL = 1.0f;
                    constexpr float stepC = 10.0f;

                    // Tuner button handler with audio feedback (matching singleplayer)
                    auto handleTunerBtn = [&](bool btn, bool& prevBtn, float& holdTimer, auto adjustFunc, bool isUp) {
                        if (btn) {
                            if (!prevBtn) {
                                bool bumped = adjustFunc();
                                // Audio feedback: adjustment click or bumper sound
                                {
                                    std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                                    if (bumped) {
                                        ctx->audioState.bumperSoundFrames = msToFrames(80);
                                    } else {
                                        ctx->audioState.adjustSoundFrames = msToFrames(120);
                                        ctx->audioState.adjustSoundUp = isUp;
                                    }
                                }
                                holdTimer = 0.0f;
                            } else {
                                holdTimer += dt;
                                if (holdTimer >= TUNER_HOLD_INITIAL_DELAY) {
                                    holdTimer -= TUNER_HOLD_REPEAT_RATE;
                                    bool bumped = adjustFunc();
                                    if (bumped) {
                                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                                        ctx->audioState.bumperSoundFrames = msToFrames(80);
                                    }
                                }
                            }
                        } else {
                            holdTimer = 0.0f;
                        }
                        prevBtn = btn;
                    };

                    handleTunerBtn(yBtn, ctx->prevTunerY, ctx->tunerHoldTimerY,
                        [&]() { return playerAntenna->getTuner().adjustInductance(stepL); }, true);
                    handleTunerBtn(xBtn, ctx->prevTunerX, ctx->tunerHoldTimerX,
                        [&]() { return playerAntenna->getTuner().adjustInductance(-stepL); }, false);
                    handleTunerBtn(bBtn, ctx->prevTunerB, ctx->tunerHoldTimerB,
                        [&]() { return playerAntenna->getTuner().adjustCapacitance(stepC); }, true);
                    handleTunerBtn(aBtn, ctx->prevTunerA, ctx->tunerHoldTimerA,
                        [&]() { return playerAntenna->getTuner().adjustCapacitance(-stepC); }, false);
                } else {
                    ctx->tunerHoldTimerY = ctx->tunerHoldTimerX = ctx->tunerHoldTimerB = ctx->tunerHoldTimerA = 0.0f;
                    ctx->prevTunerCheat = cheatPressed;
                }

                // ── D-pad: up/down = UnUn ratio, left/right = weapon switch ──
                bool dpadUp    = state.buttons[static_cast<int>(GamepadButton::DPAD_UP)];
                bool dpadDown  = state.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)];
                bool dpadLeft  = state.buttons[static_cast<int>(GamepadButton::DPAD_LEFT)];
                bool dpadRight = state.buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)];

                if (dpadUp && !ctx->prevDpadUp) {
                    int ratio = static_cast<int>(playerAntenna->getUnUn().getRatio());
                    if (ratio < 3) {
                        playerAntenna->getUnUn().setRatio(static_cast<UnUn::Ratio>(ratio + 1));
                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                        ctx->audioState.adjustSoundFrames = msToFrames(120);
                        ctx->audioState.adjustSoundUp = true;
                    } else {
                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                        ctx->audioState.bumperSoundFrames = msToFrames(80);
                    }
                }
                if (dpadDown && !ctx->prevDpadDown) {
                    int ratio = static_cast<int>(playerAntenna->getUnUn().getRatio());
                    if (ratio > 0) {
                        playerAntenna->getUnUn().setRatio(static_cast<UnUn::Ratio>(ratio - 1));
                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                        ctx->audioState.adjustSoundFrames = msToFrames(120);
                        ctx->audioState.adjustSoundUp = false;
                    } else {
                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                        ctx->audioState.bumperSoundFrames = msToFrames(80);
                    }
                }
                // D-pad left/right: weapon switch (announce boundary since there is only one weapon)
                if ((dpadLeft && !ctx->prevDpadLeft) || (dpadRight && !ctx->prevDpadRight)) {
                    log("HAMSPIRIT", "Player " + std::to_string(p) + " weapon menu: boundary (only Noise Blanker)");
                }
                ctx->prevDpadUp    = dpadUp;
                ctx->prevDpadDown  = dpadDown;
                ctx->prevDpadLeft  = dpadLeft;
                ctx->prevDpadRight = dpadRight;

                // Re-send updated SWR to authority after any tuner change
                if (gameAuthority && gameAuthority->isActive() && !track.empty()) {
                    float updatedSWR = playerAntenna->calculateAdjustedSWR(track, ctx->playerAngle);
                    PlayerAction tuneAction;
                    tuneAction.type = PlayerActionType::ANTENNA_TUNE;
                    tuneAction.playerId = p;
                    tuneAction.value = updatedSWR;
                    tuneAction.timestamp = std::chrono::steady_clock::now();
                    gameAuthority->processAction(tuneAction);
                }
            }

            // Morse cannon: use per-player MorseCannon instance for all players
            if (!playerMorseCannons[p]) {
                playerMorseCannons[p] = std::make_unique<MorseCannon>();
                playerMorseCannons[p]->setPaddleSwap(config.paddleSwap);
            }
            // Suppress vertical key when both triggers held (power-up collection)
            float mcLt = state.axes[static_cast<int>(GamepadAxis::LEFT_TRIGGER)];
            float mcRt = state.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)];
            bool mcBothHeld = mcLt > POWERUP_TRIGGER_THRESHOLD && mcRt > POWERUP_TRIGGER_THRESHOLD;
            playerMorseCannons[p]->update(state, dt, mcBothHeld);
            bool wasActive = ctx->morseCannonActive;
            ctx->morseCannonActive = playerMorseCannons[p]->isActive();
            ctx->morseCannonIsDash = playerMorseCannons[p]->isDashPaddleActive();
            if (ctx->morseCannonActive != wasActive) {
                ctx->morseCannonTimestamp = std::chrono::steady_clock::now();
                // For player 0, push immediate audio events so the main audio
                // thread renders the sidetone with sub-frame latency — matching
                // the singleplayer path in updateSpatialAudio().
                if (p == 0) {
                    if (ctx->morseCannonActive) {
                        feedbackOrchestrator.triggerMorseKeyDown(ctx->morseCannonIsDash, currentPan);
                    } else {
                        feedbackOrchestrator.triggerMorseKeyUp();
                    }
                }
            }

            // Check for collected morse characters
            char sentChar = playerMorseCannons[p]->getLastSentChar();
            if (sentChar != '\0') {
                checkMorseCollectionForPlayer(sentChar, p);
            }

            // Noise Blanker: Left Trigger fires (suppressed during power-up collection)
            // Auto-fire mode works identically for all players.
            float nbLt = state.axes[static_cast<int>(GamepadAxis::LEFT_TRIGGER)];
            float nbRt = state.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)];
            bool nbLtPressed = nbLt > 0.3f;
            bool nbBothTriggersHeld = nbLt > POWERUP_TRIGGER_THRESHOLD && nbRt > POWERUP_TRIGGER_THRESHOLD;
            if (ctx->noiseBlankerCooldown > 0.0f) {
                ctx->noiseBlankerCooldown -= dt;
            }
            if (autoFireActive && nbLtPressed && !nbBothTriggersHeld && ctx->noiseBlankerCooldown <= 0.0f) {
                handleNoiseBlankerFireForPlayer(p);
            } else if (nbLtPressed && !ctx->prevNoiseBlankerBtn && ctx->noiseBlankerCooldown <= 0.0f && !nbBothTriggersHeld) {
                handleNoiseBlankerFireForPlayer(p);
            }
            ctx->prevNoiseBlankerBtn = nbLtPressed;

            // Emergency brake (L3 — left stick click): rapid deceleration
            // Guarded by config.emergencyBrakeEnabled to match single-player behavior.
            if (config.emergencyBrakeEnabled) {
                bool leftStickBtn = state.buttons[static_cast<int>(GamepadButton::LEFT_STICK)];
                if (leftStickBtn && !ctx->prevLeftStickBtn && ctx->emergencyBrakeTimer <= 0.0f) {
                    // Only activate if player has speed
                    if (gameAuthority && gameAuthority->isActive()) {
                        const auto& pws = gameAuthority->getPlayerState(p);
                        if (std::abs(pws.playerSpeed) > STANDSTILL_THRESHOLD) {
                            ctx->emergencyBrakeTimer = EMERGENCY_BRAKE_DURATION;
                            ctx->emergencyBrakeStartSpeed = std::abs(pws.playerSpeed);
                            // Trigger emergency brake sound for this player
                            {
                                std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                                ctx->audioState.emergencyBrakeSoundFrames = msToFrames(400);
                            }
                            log("HAMSPIRIT", "Player " + std::to_string(p) + " emergency brake activated!");
                        }
                    }
                }
                ctx->prevLeftStickBtn = leftStickBtn;
            }

            // Process ongoing emergency brake deceleration for this player
            if (ctx->emergencyBrakeTimer > 0.0f && gameAuthority && gameAuthority->isActive()) {
                ctx->emergencyBrakeTimer -= dt;
                auto& pws = gameAuthority->getPlayerStateMutable(p);
                if (ctx->emergencyBrakeTimer <= 0.0f) {
                    pws.playerSpeed = 0.0f;
                    ctx->emergencyBrakeTimer = 0.0f;
                } else {
                    float brakeFraction = ctx->emergencyBrakeTimer / EMERGENCY_BRAKE_DURATION;
                    float targetSpeed = ctx->emergencyBrakeStartSpeed * brakeFraction * brakeFraction;
                    if (pws.playerSpeed > 0.0f) {
                        pws.playerSpeed = std::min(pws.playerSpeed, targetSpeed);
                    } else {
                        pws.playerSpeed = std::max(pws.playerSpeed, -targetSpeed);
                    }
                    // Keep the screech sound alive during braking
                    {
                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                        if (ctx->audioState.emergencyBrakeSoundFrames < msToFrames(80)) {
                            ctx->audioState.emergencyBrakeSoundFrames = msToFrames(320);
                        }
                    }
                }
            }

            // Right stick click (R3): toggle heading sync (aim centering)
            {
                bool rightStickBtn = state.buttons[static_cast<int>(GamepadButton::RIGHT_STICK)];
                if (rightStickBtn && !ctx->prevRightStickBtn) {
                    ctx->aimSyncToHeading = !ctx->aimSyncToHeading;
                    // Trigger aim sync confirmation sound (matching single-player)
                    {
                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                        ctx->audioState.aimResetSoundFrames = msToFrames(120);
                    }
                    if (ctx->aimSyncToHeading) {
                        // Set aim angle to 0 (forward) via authority
                        if (gameAuthority && gameAuthority->isActive()) {
                            auto& pws = gameAuthority->getPlayerStateMutable(p);
                            pws.aimAngle = 0.0f;
                        }
                        log("HAMSPIRIT", "Player " + std::to_string(p) + " aim synced to vehicle heading");
                    } else {
                        log("HAMSPIRIT", "Player " + std::to_string(p) + " aim sync released — free aiming");
                    }
                }
                ctx->prevRightStickBtn = rightStickBtn;
            }

            // When heading sync is active, force aim to zero each frame
            if (ctx->aimSyncToHeading && gameAuthority && gameAuthority->isActive()) {
                auto& pws = gameAuthority->getPlayerStateMutable(p);
                pws.aimAngle *= HEADING_SYNC_DECAY;
                if (std::abs(pws.aimAngle) < HEADING_SYNC_SNAP_THRESHOLD) pws.aimAngle = 0.0f;
                // Break sync if player moves right stick
                float aimX = state.axes[static_cast<int>(GamepadAxis::RIGHT_X)];
                if (std::abs(aimX) > HEADING_SYNC_BREAK_THRESHOLD) {
                    ctx->aimSyncToHeading = false;
                    log("HAMSPIRIT", "Player " + std::to_string(p) + " aim sync broken — free aiming");
                }
            }

            // Aim zero-crossing confirmation sound (matching single-player handleAimingInput)
            if (!ctx->aimSyncToHeading && gameAuthority && gameAuthority->isActive()) {
                float aimX = state.axes[static_cast<int>(GamepadAxis::RIGHT_X)];
                if (std::abs(aimX) > HEADING_SYNC_BREAK_THRESHOLD) {
                    const auto& pws = gameAuthority->getPlayerState(p);
                    float prevAim = pws.aimAngle;
                    float newAim = prevAim + aimX * aimSpeed * config.aimSensitivity * dt;
                    bool crossed = (prevAim > 0.05f && newAim <= 0.05f) ||
                                   (prevAim < -0.05f && newAim >= -0.05f);
                    if (crossed) {
                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                        ctx->audioState.aimResetSoundFrames = msToFrames(120);
                    }
                }
            }

            // Power-up collection: both triggers held
            handlePowerUpCollectionForPlayer(state, dt, p);
        }

        // ── Per-player border collision handling ──
        // Uses the authoritative state so all players get identical collision
        // detection, damage, vibration, and audio — no Player 0 special logic.
        if (gameAuthority && gameAuthority->isActive()) {
            auto& pws = gameAuthority->getPlayerStateMutable(p);
            ctx->trackBorderProximity = std::abs(pws.playerLateralOffset);

            // Count down crash vibration timer
            if (ctx->crashVibrationTimer > 0.0f) {
                ctx->crashVibrationTimer -= dt;
                if (ctx->crashVibrationTimer <= 0.0f) ctx->crashVibrationTimer = 0.0f;
            }

            if (ctx->crashRecoveryTime > 0.0f) {
                ctx->crashRecoveryTime -= dt;
                pws.playerSpeed *= BORDER_RECOVERY_SPEED_MULT;
            } else if (ctx->trackBorderProximity >= 1.0f) {
                // CRASH — player hitting the barrier
                float swrGradient = 0.0f;
                if (!track.empty()) {
                    TrackPoint tp = TrackGenerator::interpolateAt(track, pws.playerAngle);
                    float stepAngle = TWO_PI / std::max(1.0f, static_cast<float>(track.size()));
                    TrackPoint tpAhead = TrackGenerator::interpolateAt(track, pws.playerAngle + stepAngle);
                    swrGradient = std::abs(tpAhead.swr - tp.swr);
                }
                if (swrGradient > CRASH_SWR_GRADIENT_THRESHOLD) {
                    // Curve crash
                    pws.playerSpeed *= BORDER_CRASH_SPEED_MULT;
                    ctx->paHealth = std::max(0.0f, ctx->paHealth - BORDER_CRASH_PA_DAMAGE);
                    pws.paHealth = ctx->paHealth;
                    ctx->crashRecoveryTime = BORDER_CRASH_RECOVERY_TIME;
                    {
                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                        ctx->audioState.borderCrashSoundFrames = 4800;
                        ctx->audioState.borderCollisionSide = (pws.playerLateralOffset > 0.0f) ? 0.8f : 0.2f;
                    }
                    if (tts && tts->isAvailable()) {
                        speakTranslated("HAMSPIRIT_BORDER_CRASH", "Crash! You hit the barrier!", false);
                    }
                    pws.playerLateralOffset = (pws.playerLateralOffset > 0.0f) ? BORDER_CRASH_BOUNCE_OFFSET : -BORDER_CRASH_BOUNCE_OFFSET;
                    ctx->crashVibrationTimer = BORDER_CRASH_VIB_DURATION;
                } else {
                    // Scrape
                    pws.playerSpeed *= BORDER_SCRAPE_SPEED_MULT;
                    ctx->paHealth = std::max(0.0f, ctx->paHealth - BORDER_SCRAPE_PA_DAMAGE);
                    pws.paHealth = ctx->paHealth;
                    {
                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                        ctx->audioState.borderScrapeSoundFrames = 240;
                        ctx->audioState.borderCollisionSide = (pws.playerLateralOffset > 0.0f) ? 0.8f : 0.2f;
                    }
                    if (tts && tts->isAvailable() && announceCooldown <= 0.0f) {
                        speakTranslated("HAMSPIRIT_BORDER_SCRAPE", "Scraping along the barrier!", false);
                        announceCooldown = 5.0f;
                    }
                    pws.playerLateralOffset = (pws.playerLateralOffset > 0.0f) ? BORDER_SCRAPE_BOUNCE_OFFSET : -BORDER_SCRAPE_BOUNCE_OFFSET;
                    ctx->crashVibrationTimer = BORDER_SCRAPE_VIB_DURATION;
                }
            } else if (ctx->trackBorderProximity > BORDER_WARNING_ZONE_START) {
                // Warning zone — progressive audio and vibration feedback
                // Matching singleplayer: audio beep generation + vibration + SWR gradient
                if (config.borderWarningEnabled) {
                    float warningStrength = (ctx->trackBorderProximity - BORDER_WARNING_ZONE_START) / (1.0f - BORDER_WARNING_ZONE_START);
                    warningStrength = std::clamp(warningStrength, 0.0f, 1.0f);
                    float effectiveMax = ctx->maxSpeed > 0.0f ? ctx->maxSpeed : maxSpeed;
                    float speedFactor = (effectiveMax > 0.0f) ? std::clamp(std::abs(pws.playerSpeed) / effectiveMax, 0.0f, 1.0f) : 0.0f;
                    warningStrength = std::min(1.0f, warningStrength + speedFactor * BORDER_WARNING_SPEED_FACTOR);

                    // SWR gradient factor — stronger warnings on curves (matching singleplayer)
                    float swrGradientWarn = 0.0f;
                    if (!track.empty()) {
                        TrackPoint tp = TrackGenerator::interpolateAt(track, pws.playerAngle);
                        float stepAngle = TWO_PI / std::max(1.0f, static_cast<float>(track.size()));
                        TrackPoint tpAhead = TrackGenerator::interpolateAt(track, pws.playerAngle + stepAngle);
                        swrGradientWarn = std::abs(tpAhead.swr - tp.swr);
                    }
                    warningStrength = std::min(1.0f, warningStrength + swrGradientWarn * BORDER_WARNING_SWR_FACTOR);

                    // Audio warning beeps (matching singleplayer border warning audio)
                    {
                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                        ctx->audioState.borderWarningActive = true;
                        ctx->audioState.borderWarningIntensity = warningStrength;
                        ctx->audioState.borderWarningSide = (pws.playerLateralOffset > 0.0f) ? 0.8f : 0.2f;

                        if (warningStrength > BORDER_WARNING_MIN_STRENGTH) {
                            float beepInterval = 0.6f - warningStrength * 0.52f;
                            beepInterval = std::max(beepInterval, 0.08f);
                            ctx->borderWarningBeepTimer += dt;
                            if (ctx->borderWarningBeepTimer >= beepInterval) {
                                ctx->borderWarningBeepTimer = 0.0f;
                                int beepFrames = static_cast<int>((200 + warningStrength * 280) * (40.0f / GAME_AUDIO_FRAME_MS));
                                ctx->audioState.borderWarningSoundFrames = beepFrames;
                            }
                        }
                    }

                    // Vibration feedback (matching singleplayer)
                    if (config.swrVibration && ctx->crashVibrationTimer <= 0.0f && warningStrength > BORDER_WARNING_MIN_STRENGTH) {
                        float vibIntensity = BORDER_VIB_MIN_INTENSITY + (1.0f - BORDER_VIB_MIN_INTENSITY) * warningStrength;
                        if (pws.playerLateralOffset < 0.0f) {
                            setVibrationForPlayer(p, vibIntensity * config.vibrationIntensity, 0.0f);
                        } else {
                            setVibrationForPlayer(p, 0.0f, std::min(1.0f, vibIntensity * RIGHT_MOTOR_COMPENSATION * config.vibrationIntensity));
                        }
                        ctx->borderVibrationActive = true;
                    }
                } else {
                    // Warning disabled — reset audio and vibration
                    {
                        std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                        ctx->audioState.borderWarningActive = false;
                        ctx->audioState.borderWarningSoundFrames = 0;
                    }
                    if (ctx->borderVibrationActive && ctx->crashVibrationTimer <= 0.0f) {
                        setVibrationForPlayer(p, 0.0f, 0.0f);
                        ctx->borderVibrationActive = false;
                    }
                }
            } else {
                // Safe zone — stop border warning sounds and vibration (matching singleplayer)
                {
                    std::lock_guard<std::mutex> lock(ctx->audioStateMtx);
                    ctx->audioState.borderScrapeSoundFrames = 0;
                    ctx->audioState.borderCrashSoundFrames = 0;
                    ctx->audioState.borderWarningSoundFrames = 0;
                    ctx->audioState.borderWarningActive = false;
                }
                ctx->borderWarningBeepTimer = 0.0f;
                if (ctx->borderVibrationActive && ctx->crashVibrationTimer <= 0.0f) {
                    setVibrationForPlayer(p, 0.0f, 0.0f);
                    ctx->borderVibrationActive = false;
                }
            }

            // Crash vibration — full power for distinct rumble
            if (ctx->crashVibrationTimer > 0.0f) {
                setVibrationForPlayer(p, config.vibrationIntensity, config.vibrationIntensity);
            }
        }
    }
}

void Game::updateMultiplayerState(float dt) {
    if (!multiplayerMgr || !multiplayerMgr->isMultiplayer()) return;

    int playerCount = multiplayerMgr->getPlayerCount();

    // Player 0's Morse cannon state is now handled by gatherMultiplayerInput()
    // via playerMorseCannons[0], same as all other players. No special sync needed.

    // Read authoritative state back into PlayerContext for ALL players.
    // Input was already gathered and sent to authority by gatherMultiplayerInput()
    // before tick(), so the authority has processed all players' input by now.
    for (int p = 0; p < playerCount; p++) {
        auto* ctx = multiplayerMgr->getPlayer(p);
        if (!ctx) continue;

        // ── Read authoritative state back into PlayerContext ──
        if (gameAuthority && gameAuthority->isActive()) {
            const auto& pws = gameAuthority->getPlayerState(p);
            ctx->playerAngle = pws.playerAngle;
            ctx->playerSpeed = pws.playerSpeed;
            ctx->playerLateralOffset = pws.playerLateralOffset;
            ctx->aimAngle = pws.aimAngle;
            ctx->maxSpeed = pws.maxSpeed;
        }
    }

    // Calculate track radius in meters (approximate from frequency range)
    float trackRadiusMeters = 100.0f; // Default 100m radius
    if (!track.empty() && kHzPerRadian > 0.0f) {
        // Use the frequency range to estimate a reasonable physical track size
        trackRadiusMeters = std::max(50.0f, (maxTrackFreqHz - minTrackFreqHz) / 1000.0f * 10.0f);
    }

    // Update spatial audio between all players
    multiplayerMgr->updateSpatialAudio(trackRadiusMeters);

    // Check for player-to-player collisions
    auto collisions = multiplayerMgr->checkPlayerCollisions(trackRadiusMeters, dt);

    // Process collision results (TTS announcements, vibration feedback)
    for (const auto& col : collisions) {
        if (!col.collided) continue;

        // Vibration feedback for both players involved
        if (config.swrVibration && gamepad) {
            float vib = col.intensity * config.vibrationIntensity;
            // Find which players collided and vibrate their controllers
            for (int p = 0; p < playerCount; p++) {
                auto* ctxP = multiplayerMgr->getPlayer(p);
                if (ctxP && ctxP->collisionSoundFrames > 0 &&
                    ctxP->inputAssignment.type == InputSourceType::GAMEPAD) {
                    gamepad->setVibration(ctxP->inputAssignment.gamepadIndex, vib, vib * 0.7f);
                }
            }
        }
    }

    // Sync player states back from multiplayer contexts
    for (int p = 0; p < playerCount; p++) {
        auto* ctx = multiplayerMgr->getPlayer(p);
        if (!ctx) continue;

        // Update track position for spatial audio
        ctx->trackPositionMeters = ctx->playerAngle * trackRadiusMeters;
        ctx->lateralPositionMeters = ctx->playerLateralOffset *
                                      SpatialPlayerAudio::TRACK_WIDTH_METERS * 0.5f;

        // Sync per-player stats (game time, PA health, laps)
        if (ctx->stats) {
            ctx->stats->gameTime = stats.gameTime;  // Shared game clock
            ctx->stats->paHealth = ctx->paHealth;
            ctx->stats->averageSWR = stats.averageSWR;  // Shared track SWR
            ctx->stats->lapsCompleted = ctx->lapsCompleted;
        }

        // Compute per-player audio params for ALL players.
        // In the authoritative architecture every player—including player 0—
        // gets audio computed from their own independent position and state.
        computePlayerAudioParams(*ctx);
    }

    // Sync player 0's event audio state to Game::audioParams.
    // Player 0's main audio thread (audioThreadFunc) reads from Game::audioParams,
    // not from ctx->audioState.  Without this sync, sounds set by
    // gatherMultiplayerInput into ctx->audioState would be inaudible
    // for player 0 in multiplayer mode.
    auto* ctx0 = multiplayerMgr->getPlayer(0);
    if (ctx0) {
        std::lock_guard<std::mutex> pLock(ctx0->audioStateMtx);
        std::lock_guard<std::mutex> aLock(audioStateMtx);
        // Border collision/warning
        audioParams.borderCrashSoundFrames  = std::max(audioParams.borderCrashSoundFrames,
                                                        ctx0->audioState.borderCrashSoundFrames);
        audioParams.borderScrapeSoundFrames = std::max(audioParams.borderScrapeSoundFrames,
                                                        ctx0->audioState.borderScrapeSoundFrames);
        audioParams.borderWarningSoundFrames = std::max(audioParams.borderWarningSoundFrames,
                                                         ctx0->audioState.borderWarningSoundFrames);
        audioParams.borderWarningActive     = ctx0->audioState.borderWarningActive;
        audioParams.borderWarningIntensity  = ctx0->audioState.borderWarningIntensity;
        audioParams.borderWarningSide       = ctx0->audioState.borderWarningSide;
        if (ctx0->audioState.borderCrashSoundFrames > 0 || ctx0->audioState.borderScrapeSoundFrames > 0)
            audioParams.borderCollisionSide = ctx0->audioState.borderCollisionSide;
        // Tuner feedback
        audioParams.adjustSoundFrames       = std::max(audioParams.adjustSoundFrames,
                                                        ctx0->audioState.adjustSoundFrames);
        if (ctx0->audioState.adjustSoundFrames > 0)
            audioParams.adjustSoundUp = ctx0->audioState.adjustSoundUp;
        audioParams.bumperSoundFrames       = std::max(audioParams.bumperSoundFrames,
                                                        ctx0->audioState.bumperSoundFrames);
        // QSO Störer events
        audioParams.qsoStoererCollisionFrames = std::max(audioParams.qsoStoererCollisionFrames,
                                                          ctx0->audioState.qsoStoererCollisionFrames);
        audioParams.qsoStoererOvertakeFrames = std::max(audioParams.qsoStoererOvertakeFrames,
                                                         ctx0->audioState.qsoStoererOvertakeFrames);
        // Band crossing / traffic
        audioParams.bandJingleFrames        = std::max(audioParams.bandJingleFrames,
                                                        ctx0->audioState.bandJingleFrames);
        if (ctx0->audioState.bandJingleFrames > 0)
            audioParams.bandJingleAscending = ctx0->audioState.bandJingleAscending;
        audioParams.trafficBeepFrames       = std::max(audioParams.trafficBeepFrames,
                                                        ctx0->audioState.trafficBeepFrames);
        // Power-up events
        audioParams.powerUpActivateFrames = std::max(audioParams.powerUpActivateFrames,
                                                        ctx0->audioState.powerUpActivateFrames);
        audioParams.powerUpExpireFrames = std::max(audioParams.powerUpExpireFrames,
                                                        ctx0->audioState.powerUpExpireFrames);
        audioParams.powerUpExplodeFrames    = std::max(audioParams.powerUpExplodeFrames,
                                                        ctx0->audioState.powerUpExplodeFrames);
        if (ctx0->audioState.powerUpExplodeFrames > 0) {
            audioParams.powerUpExplodePan = ctx0->audioState.powerUpExplodePan;
            audioParams.powerUpExplodeIntensity = ctx0->audioState.powerUpExplodeIntensity;
        }
        // Morse collection / miss / PA events
        audioParams.collectSoundFrames      = std::max(audioParams.collectSoundFrames,
                                                         ctx0->audioState.collectSoundFrames);
        audioParams.missMorseSoundFrames    = std::max(audioParams.missMorseSoundFrames,
                                                         ctx0->audioState.missMorseSoundFrames);
        audioParams.missAimSoundFrames      = std::max(audioParams.missAimSoundFrames,
                                                         ctx0->audioState.missAimSoundFrames);
        audioParams.paDamageSoundFrames     = std::max(audioParams.paDamageSoundFrames,
                                                         ctx0->audioState.paDamageSoundFrames);
        audioParams.paRepairSoundFrames     = std::max(audioParams.paRepairSoundFrames,
                                                         ctx0->audioState.paRepairSoundFrames);
        // Weapon / combat sounds
        audioParams.noiseBlankerFireFrames  = std::max(audioParams.noiseBlankerFireFrames,
                                                         ctx0->audioState.noiseBlankerFireFrames);
        audioParams.noiseHitSoundFrames     = std::max(audioParams.noiseHitSoundFrames,
                                                         ctx0->audioState.noiseHitSoundFrames);
        if (ctx0->audioState.noiseHitSoundFrames > 0)
            audioParams.noiseHitVariation   = ctx0->audioState.noiseHitVariation;
        audioParams.noiseDestroyedFrames    = std::max(audioParams.noiseDestroyedFrames,
                                                         ctx0->audioState.noiseDestroyedFrames);
        // Emergency brake / aim reset
        audioParams.emergencyBrakeSoundFrames = std::max(audioParams.emergencyBrakeSoundFrames,
                                                          ctx0->audioState.emergencyBrakeSoundFrames);
        audioParams.aimResetSoundFrames     = std::max(audioParams.aimResetSoundFrames,
                                                         ctx0->audioState.aimResetSoundFrames);
        // Morse cannon sidetone (in MP, handled by playerMorseCannons[0]
        // via gatherMultiplayerInput → computePlayerAudioParams; the SP
        // morseCannon instance is inactive, so updateSpatialAudio writes
        // false — override with the actual multiplayer state here)
        audioParams.morseCannonActive       = ctx0->audioState.morseCannonActive;
        audioParams.morseCannonIsDash       = ctx0->audioState.morseCannonIsDash;
    }
}

// ── Compute per-player audio parameters ─────────────────────────────────────
//
// For every player, compute the same audio params: motor frequency, SWR alerts,
// morse signals, noise enemies, QSO Störer, power-ups — all from this player's
// position.  This makes each player hear the game world from their own
// perspective, just like independent clients in an online game.

void Game::computePlayerAudioParams(PlayerContext& ctx) {
    PlayerAudioParams params;
    
    // 6.5: Timestamp the audio state for this player
    params.eventTimestamp = std::chrono::steady_clock::now();

    // Pan: from player's lateral position
    params.pan = ctx.playerLateralOffset;
    params.pan = std::clamp(params.pan, -1.0f, 1.0f);

    // Motor frequency from speed
    float effectiveMaxSpeed = ctx.maxSpeed > 0.0f ? ctx.maxSpeed : maxSpeed;
    float speedFraction = (effectiveMaxSpeed > 0.0f) ? std::abs(ctx.playerSpeed) / effectiveMaxSpeed : 0.0f;
    speedFraction = std::min(speedFraction, 1.0f);
    params.motorFreq = MOTOR_BASE_FREQ + (MOTOR_MAX_FREQ - MOTOR_BASE_FREQ) * speedFraction;

    // Motor volume: silent at standstill, ramps up with speed
    if (speedFraction > 0.01f) {
        params.motorVolume = (80 + 120 * speedFraction) * config.motorVolume;
    }

    // SWR and roughness at this player's position
    float swr = 1.0f;
    float reactance = 0.0f;
    if (!track.empty()) {
        TrackPoint tp = TrackGenerator::interpolateAt(track, ctx.playerAngle);
        if (ctx.antennaNetwork) {
            swr = ctx.antennaNetwork->calculateAdjustedSWR(track, ctx.playerAngle);
            reactance = ctx.antennaNetwork->calculateAdjustedReactance(track, ctx.playerAngle);
        } else {
            swr = tp.swr;
            reactance = tp.reactance;
        }
    }

    // Motor roughness from SWR
    if (swr > 1.1f) {
        params.motorRoughness = std::min((swr - 1.0f) / 4.0f, 1.0f);
    }
    // Store roughness in context so other players can hear it spatially
    ctx.currentMotorRoughness = params.motorRoughness;

    // SWR alert
    params.reactanceAtPlayer = reactance;
    const float swrAlertThreshold = 2.0f;
    if (swr > swrAlertThreshold) {
        params.swrAlertActive = true;
        params.swrAlertRate = std::min((swr - swrAlertThreshold) / 8.0f, 1.0f);
        params.swrFreq = 440.0f + 760.0f * params.swrAlertRate;
        params.swrVolume = (60 + 80 * params.swrAlertRate) * config.swrVolume;
    }

    // PA damage
    params.paDamageLevel = 1.0f - ctx.paHealth;

    // Morse cannon (this player's own keying)
    params.morseCannonActive = ctx.morseCannonActive;
    params.morseCannonIsDash = ctx.morseCannonIsDash;

    // Volume factors from config
    params.warningVolume = config.warningVolume;
    params.collisionVolume = config.collisionVolume;
    params.enemyVolume = config.enemyVolume;
    params.uiVolume = config.uiVolume;

    // Border warning from this player's position — uses the same threshold
    // as singleplayer (BORDER_WARNING_ZONE_START = 0.4) for consistent behavior.
    float borderProx = std::abs(ctx.playerLateralOffset);
    if (borderProx > BORDER_WARNING_ZONE_START) {
        params.borderWarningActive = true;
        params.borderWarningIntensity = (borderProx - BORDER_WARNING_ZONE_START) / (1.0f - BORDER_WARNING_ZONE_START);
        params.borderWarningIntensity = std::clamp(params.borderWarningIntensity, 0.0f, 1.0f);
        params.borderWarningSide = (ctx.playerLateralOffset > 0.0f) ? 0.8f : 0.2f;
    }

    // Per-player band crossing detection (matching singleplayer checkBandCrossing)
    if (!bandPlan.empty() && !track.empty()) {
        TrackPoint tp = TrackGenerator::interpolateAt(track, ctx.playerAngle);
        float freq = tp.frequency;
        if (freq > 0.0f) {
            uint64_t freqHz = static_cast<uint64_t>(freq);
            std::string newBandName;
            for (const auto& band : bandPlan) {
                if (band.end_hz < static_cast<uint64_t>(minTrackFreqHz) ||
                    band.start_hz > static_cast<uint64_t>(maxTrackFreqHz)) continue;
                if (freqHz >= band.start_hz && freqHz <= band.end_hz) {
                    newBandName = band.name;
                    break;
                }
            }
            if (newBandName != ctx.currentBandName) {
                if (!ctx.currentBandName.empty()) {
                    params.bandJingleFrames = msToFrames(160);
                    params.bandJingleAscending = false;
                }
                if (!newBandName.empty()) {
                    params.bandJingleFrames = msToFrames(160);
                    params.bandJingleAscending = true;
                }
                ctx.currentBandName = newBandName;
            }
        }
    }

    // Morse signals from this player's perspective
    if (morseSignalManager) {
        float aimDirection = ctx.playerAngle + ctx.aimAngle;
        auto& signals = morseSignalManager->getSignals();
        for (const auto& sig : signals) {
            if (sig.collected) continue;
            float angleDiff = sig.angle - aimDirection;
            while (angleDiff > PI) angleDiff -= TWO_PI;
            while (angleDiff < -PI) angleDiff += TWO_PI;
            float distance = std::abs(angleDiff);
            if (distance > 1.5f) continue;

            float signalVolume = std::max(0.0f, 1.0f - distance) * config.morseVolume;
            float signalPan = std::clamp((angleDiff + 1.5f) / 3.0f, 0.0f, 1.0f);
            std::string pattern;
            if (morseDatabase) pattern = morseDatabase->getPattern(sig.character);
            params.morseSignals.push_back({signalPan, static_cast<int>(signalVolume * 100), pattern});

            // Aim lock
            const float AIM_MARGIN = 0.3f;
            float lock = calculateAimLock(aimDirection, sig.angle, AIM_MARGIN);
            if (lock > params.aimLockMorse) params.aimLockMorse = lock;
        }
    }

    // Noise enemies from this player's perspective
    for (const auto& enemy : noiseEnemies) {
        if (enemy.destroyed) continue;
        float angleDiff = enemy.angle - ctx.playerAngle;
        while (angleDiff > PI) angleDiff -= TWO_PI;
        while (angleDiff < -PI) angleDiff += TWO_PI;
        float distance = std::abs(angleDiff);
        if (distance > 1.5f) continue;

        float vol = std::max(0.0f, 1.0f - distance / 1.5f) * enemy.bandwidth;
        float pan = std::clamp((angleDiff + 1.5f) / 3.0f, 0.0f, 1.0f);
        params.noiseEnemies.push_back({pan, static_cast<int>(vol * 80), enemy.bandwidth});

        // Aim lock for noise
        float aimDirection = ctx.playerAngle + ctx.aimAngle;
        const float AIM_MARGIN = 0.3f;
        float lock = calculateAimLock(aimDirection, enemy.angle, AIM_MARGIN);
        if (lock > params.aimLockNoise) params.aimLockNoise = lock;
    }

    // QSO Störer from this player's perspective
    if (qsoStoerer.active) {
        float angleDiff = qsoStoerer.angle - ctx.playerAngle;
        while (angleDiff > PI) angleDiff -= TWO_PI;
        while (angleDiff < -PI) angleDiff += TWO_PI;
        float distance = std::abs(angleDiff);
        if (distance < 2.0f) {
            params.qsoStoererActive = true;
            float vol = std::max(0.0f, 1.0f - distance / 2.0f);
            params.qsoStoererVolume = static_cast<int>(vol * 100);
            params.qsoStoererPan = std::clamp((angleDiff + 2.0f) / 4.0f, 0.0f, 1.0f);
            params.qsoStoererBuzzFreq = 350.0f;  // Default hornet buzz frequency
            params.qsoStoererBehind = (angleDiff < 0.0f);
        }
    }

    // Power-up zones from this player's perspective
    for (const auto& pu : powerUps) {
        if (pu.collected || pu.destroyed) continue;
        float angleDiff = pu.angle - ctx.playerAngle;
        while (angleDiff > PI) angleDiff -= TWO_PI;
        while (angleDiff < -PI) angleDiff += TWO_PI;
        float distance = std::abs(angleDiff);
        float zoneRadius = pu.zoneHalfWidth > 0.0f ? pu.zoneHalfWidth : 0.15f;
        if (distance < zoneRadius) {
            float pan = std::clamp((angleDiff + zoneRadius) / (2.0f * zoneRadius), 0.0f, 1.0f);
            float depth = 1.0f - (distance / zoneRadius);
            int vol = static_cast<int>(depth * 60);
            params.powerUpZones.push_back({pan, vol, true, depth, static_cast<int>(pu.type)});
        }
    }

    // Aim lock max — only pass non-zero values when aim assist is enabled
    if (config.aimAssist) {
        params.aimLockStrength = std::max({params.aimLockMorse, params.aimLockNoise,
                                           params.aimLockStoerer, params.aimLockPowerUp});
    } else {
        params.aimLockStrength = 0.0f;
        params.aimLockMorse = 0.0f;
        params.aimLockNoise = 0.0f;
        params.aimLockStoerer = 0.0f;
        params.aimLockPowerUp = 0.0f;
    }

    // Propagate recent game events as spatial audio for this player
    if (multiplayerMgr) {
        int playerCount = multiplayerMgr->getPlayerCount();
        for (int other = 0; other < playerCount; other++) {
            if (other == ctx.playerIndex) continue;
            auto* otherCtx = multiplayerMgr->getPlayer(other);
            if (!otherCtx) continue;
            
            // Calculate spatial relationship to other player
            float angleDiff = otherCtx->playerAngle - ctx.playerAngle;
            while (angleDiff > PI) angleDiff -= TWO_PI;
            while (angleDiff < -PI) angleDiff += TWO_PI;
            float distance = std::abs(angleDiff);
            if (distance >= MAX_AUDIO_EVENT_DISTANCE) continue;  // Too far to hear
            
            float volume = std::max(0.0f, 1.0f - distance / MAX_AUDIO_EVENT_DISTANCE);
            float evtPan = std::clamp(angleDiff / MAX_AUDIO_EVENT_DISTANCE, -1.0f, 1.0f);

            // Check if other player has active noise blanker fire
            if (otherCtx->noiseBlankerCooldown > 0.45f) {
                PlayerAudioParams::OtherPlayerEventAudio evtAudio;
                evtAudio.type = GameEventType::NOISE_BLANKER_FIRE;
                evtAudio.pan = evtPan;
                evtAudio.volume = volume;
                evtAudio.dopplerFactor = 1.0f;
                evtAudio.playerIndex = other;
                params.otherPlayerEvents.push_back(evtAudio);
            }
            
            // Check for recent morse collection sound
            {
                std::lock_guard<std::mutex> lock(otherCtx->audioStateMtx);
                if (otherCtx->audioState.collectSoundFrames > 0) {
                    PlayerAudioParams::OtherPlayerEventAudio evtAudio;
                    evtAudio.type = GameEventType::MORSE_COLLECTED;
                    evtAudio.pan = evtPan;
                    evtAudio.volume = volume;
                    evtAudio.dopplerFactor = 1.0f;
                    evtAudio.playerIndex = other;
                    params.otherPlayerEvents.push_back(evtAudio);
                }
                // Check for recent noise blanker fire sound
                if (otherCtx->audioState.noiseBlankerFireFrames > 0) {
                    PlayerAudioParams::OtherPlayerEventAudio evtAudio;
                    evtAudio.type = GameEventType::NOISE_BLANKER_FIRE;
                    evtAudio.pan = evtPan;
                    evtAudio.volume = volume;
                    evtAudio.dopplerFactor = 1.0f;
                    evtAudio.playerIndex = other;
                    params.otherPlayerEvents.push_back(evtAudio);
                }
            }
        }
    }

    // Write to player's audio state (read by audio thread).
    // Preserve ALL transient fire-event frame counters that were set externally
    // (e.g. by handleNoiseBlankerFireForPlayer, checkMorseCollectionForPlayer, etc.)
    // before this write, so the audio thread can still render those one-shot sounds.
    {
        std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
        params.noiseBlankerFireFrames  = std::max(params.noiseBlankerFireFrames,
                                                   ctx.audioState.noiseBlankerFireFrames);
        params.collectSoundFrames      = std::max(params.collectSoundFrames,
                                                   ctx.audioState.collectSoundFrames);
        params.paRepairSoundFrames     = std::max(params.paRepairSoundFrames,
                                                   ctx.audioState.paRepairSoundFrames);
        params.paDamageSoundFrames     = std::max(params.paDamageSoundFrames,
                                                   ctx.audioState.paDamageSoundFrames);
        params.noiseHitSoundFrames     = std::max(params.noiseHitSoundFrames,
                                                   ctx.audioState.noiseHitSoundFrames);
        params.noiseHitVariation       = ctx.audioState.noiseHitVariation;
        params.noiseDestroyedFrames    = std::max(params.noiseDestroyedFrames,
                                                   ctx.audioState.noiseDestroyedFrames);
        params.missMorseSoundFrames    = std::max(params.missMorseSoundFrames,
                                                   ctx.audioState.missMorseSoundFrames);
        params.missAimSoundFrames      = std::max(params.missAimSoundFrames,
                                                   ctx.audioState.missAimSoundFrames);
        params.emergencyBrakeSoundFrames = std::max(params.emergencyBrakeSoundFrames,
                                                   ctx.audioState.emergencyBrakeSoundFrames);
        params.aimResetSoundFrames     = std::max(params.aimResetSoundFrames,
                                                   ctx.audioState.aimResetSoundFrames);
        params.borderScrapeSoundFrames = std::max(params.borderScrapeSoundFrames,
                                                   ctx.audioState.borderScrapeSoundFrames);
        params.borderCrashSoundFrames  = std::max(params.borderCrashSoundFrames,
                                                   ctx.audioState.borderCrashSoundFrames);
        params.borderWarningSoundFrames = std::max(params.borderWarningSoundFrames,
                                                   ctx.audioState.borderWarningSoundFrames);
        if (ctx.audioState.borderCrashSoundFrames > 0 || ctx.audioState.borderScrapeSoundFrames > 0)
            params.borderCollisionSide = ctx.audioState.borderCollisionSide;
        // Preserve tuner feedback sounds
        params.adjustSoundFrames       = std::max(params.adjustSoundFrames,
                                                   ctx.audioState.adjustSoundFrames);
        if (ctx.audioState.adjustSoundFrames > 0) {
            params.adjustSoundUp = ctx.audioState.adjustSoundUp;
            params.adjustSoundPan = ctx.audioState.adjustSoundPan;
        }
        params.bumperSoundFrames       = std::max(params.bumperSoundFrames,
                                                   ctx.audioState.bumperSoundFrames);
        // Preserve QSO Störer event sounds
        params.qsoStoererCollisionFrames = std::max(params.qsoStoererCollisionFrames,
                                                     ctx.audioState.qsoStoererCollisionFrames);
        params.qsoStoererOvertakeFrames = std::max(params.qsoStoererOvertakeFrames,
                                                    ctx.audioState.qsoStoererOvertakeFrames);
        // Preserve band crossing and traffic sounds
        params.bandJingleFrames        = std::max(params.bandJingleFrames,
                                                   ctx.audioState.bandJingleFrames);
        if (ctx.audioState.bandJingleFrames > 0)
            params.bandJingleAscending = ctx.audioState.bandJingleAscending;
        params.trafficBeepFrames       = std::max(params.trafficBeepFrames,
                                                   ctx.audioState.trafficBeepFrames);
        // Preserve power-up event sounds
        params.powerUpActivateFrames = std::max(params.powerUpActivateFrames,
                                                   ctx.audioState.powerUpActivateFrames);
        if (ctx.audioState.powerUpActivateFrames > 0)
            params.powerUpActivationType = ctx.audioState.powerUpActivationType;
        params.powerUpExpireFrames = std::max(params.powerUpExpireFrames,
                                                   ctx.audioState.powerUpExpireFrames);
        params.powerUpExplodeFrames    = std::max(params.powerUpExplodeFrames,
                                                   ctx.audioState.powerUpExplodeFrames);
        if (ctx.audioState.powerUpExplodeFrames > 0) {
            params.powerUpExplodePan = ctx.audioState.powerUpExplodePan;
            params.powerUpExplodeIntensity = ctx.audioState.powerUpExplodeIntensity;
        }
        ctx.audioState = std::move(params);
    }
}

void Game::renderCollisionSound(std::vector<int16_t>& buffer, int samples,
                                 int sampleRate, float intensity, float pan,
                                 int& frames) {
    if (frames <= 0) return;

    // Metallic crunch/impact sound:
    //  - Short burst of filtered noise (metal-on-metal)
    //  - Low-frequency thud (body impact)
    //  - Envelope: fast attack, medium decay

    int framesToRender = std::min(frames, samples);
    float invSR = 1.0f / static_cast<float>(sampleRate);

    // Pan to left/right gain (equal power)
    float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
    float leftGain  = intensity * std::cos(angle) * 0.8f;
    float rightGain = intensity * std::sin(angle) * 0.8f;

    static unsigned int noiseSeed = 12345;
    float envelopeStart = static_cast<float>(frames) / static_cast<float>(sampleRate);

    for (int i = 0; i < framesToRender; i++) {
        float t = static_cast<float>(i) * invSR;
        float remaining = static_cast<float>(frames - i) / static_cast<float>(sampleRate);

        // Envelope: fast attack (first 5ms), then exponential decay
        float env = 1.0f;
        if (t < 0.005f) {
            env = t / 0.005f;
        }
        env *= std::exp(remaining > 0 ? -(envelopeStart - remaining) * 8.0f : 0.0f);

        // Noise component (metallic crunch)
        noiseSeed = noiseSeed * 1103515245 + 12345;
        float noise = (static_cast<float>(noiseSeed & 0x7FFF) / 16383.5f - 1.0f);

        // Low-frequency thud (60 Hz sine)
        float thud = std::sin(2.0f * 3.14159265f * 60.0f * t) * std::exp(-t * 20.0f);

        // High-frequency metallic ring (2500 Hz damped sine)
        float ring = std::sin(2.0f * 3.14159265f * 2500.0f * t) * std::exp(-t * 30.0f) * 0.3f;

        float sample = (noise * 0.4f + thud * 0.4f + ring * 0.2f) * env;

        int16_t left  = static_cast<int16_t>(sample * leftGain * 20000.0f);
        int16_t right = static_cast<int16_t>(sample * rightGain * 20000.0f);

        int idx = i * 2;
        // Mix into existing buffer (additive)
        int32_t mixL = static_cast<int32_t>(buffer[idx])     + left;
        int32_t mixR = static_cast<int32_t>(buffer[idx + 1]) + right;
        buffer[idx]     = static_cast<int16_t>(std::max(-32768, std::min(32767, static_cast<int>(mixL))));
        buffer[idx + 1] = static_cast<int16_t>(std::max(-32768, std::min(32767, static_cast<int>(mixR))));
    }

    frames -= framesToRender;
}

void Game::renderOtherPlayerEngine(std::vector<int16_t>& buffer, int samples,
                                    int sampleRate,
                                    const SpatialRelation& relation,
                                    float sourceSpeed, float baseMotorFreq,
                                    int sourceIndex) {
    if (relation.volume < 0.001f || samples <= 0) return;

    float invSR = 1.0f / static_cast<float>(sampleRate);

    // Apply Doppler to the motor frequency
    float dopplerFreq = SpatialPlayerAudio::applyDoppler(baseMotorFreq, relation.dopplerFactor);

    // Speed-dependent motor frequency (higher speed = higher pitch)
    float motorFreq = dopplerFreq * (1.0f + sourceSpeed * 0.5f);

    // Pan to left/right gain (equal power)
    float angle = (relation.pan + 1.0f) * 0.25f * 3.14159265f;
    float leftGain  = relation.volume * std::cos(angle) * 0.3f;
    float rightGain = relation.volume * std::sin(angle) * 0.3f;

    // Per-source-player phase accumulator so each opponent's engine sound
    // is independent and doesn't cause phase cancellation artefacts.
    static thread_local float enginePhaseP0[MAX_PLAYERS] = {};
    float& phase = enginePhaseP0[std::clamp(sourceIndex, 0, MAX_PLAYERS - 1)];

    for (int i = 0; i < samples; i++) {
        // Generate motor-like waveform (sawtooth with slight modulation)
        phase += motorFreq * invSR;
        if (phase >= 1.0f) phase -= 1.0f;

        float sample = (phase * 2.0f - 1.0f); // Sawtooth
        // Add slight roughness
        sample += std::sin(phase * 2.0f * 3.14159265f * 3.0f) * 0.2f;

        int16_t left  = static_cast<int16_t>(sample * leftGain * 8000.0f);
        int16_t right = static_cast<int16_t>(sample * rightGain * 8000.0f);

        int idx = i * 2;
        int32_t mixL = static_cast<int32_t>(buffer[idx])     + left;
        int32_t mixR = static_cast<int32_t>(buffer[idx + 1]) + right;
        buffer[idx]     = static_cast<int16_t>(std::max(-32768, std::min(32767, static_cast<int>(mixL))));
        buffer[idx + 1] = static_cast<int16_t>(std::max(-32768, std::min(32767, static_cast<int>(mixR))));
    }
}

} // namespace HamSpirit

#endif // WITH_HAM_SPIRIT
