#pragma once

#ifdef WITH_HAM_SPIRIT

#include "measurement.h"
#include "tts_manager.h"
#include "gamepad_interface.h"
#include "translation.h"
#include "logger.h"
#include "audio_engine_interface.h"
#include "synthesizer_engine.h"
#include "band_definitions.h"
#include "multiplayer.h"
#include "feedback_types.h"
#include "feedback_orchestrator.h"
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <complex>
#include <map>
#include <thread>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <cstdio>

// Forward declarations
class AcousticAnalyzer;
class IAudioBackend;
class IConsoleInput;

/**
 * @file hamspirit_game.h
 * @brief Ham Spirit - Audio-based RF matching and Morse code learning game
 * 
 * An educational Easter egg that transforms VNA measurement data into an
 * interactive racing/shooter experience teaching antenna matching and Morse code.
 */

namespace HamSpirit {

// Forward declaration for server-authoritative architecture
class GameAuthority;

/**
 * Game state enumeration
 */
enum class GameState {
    INTRO,        // Playing intro welcome sequence with TTS narration
    MAIN_MENU,    // Main menu before game starts (New Game, Tutorial, Config, Exit)
    PLAYING,      // Active gameplay
    PAUSED,       // Paused with menu
    GAME_OVER,    // Game over screen
    EXITING       // Cleaning up and returning to acoustic analyzer
};

/**
 * Track curve selection
 */
enum class TrackCurve {
    SWR = 0,           // Standing Wave Ratio
    RETURN_LOSS = 1,   // Return Loss (dB)
    IMPEDANCE_MAG = 2, // Impedance Magnitude
    REACTANCE = 3,     // Reactance (X)
    PHASE = 4,         // Phase angle
    ALL_CURVES = 5     // Chain all curves as track sections
};

/**
 * Keyboard action mapping — each action maps to a virtual key code.
 * Defaults match the original hardcoded layout; overridden by user config.
 */
struct KeyMapping {
    int steerLeft = 0x25;       // VK_LEFT
    int steerRight = 0x27;      // VK_RIGHT
    int accelerate = 0x26;      // VK_UP
    int brake = 0x28;           // VK_DOWN
    int aimLeft = 'A';
    int aimRight = 'D';
    int aimUp = 'W';
    int aimDown = 'S';
    int morseKey = 0x20;        // VK_SPACE
    int paddleDot = 'U';
    int paddleDash = 'O';
    int noiseBlanker = 'F';
    int inductanceUp = 'Q';
    int inductanceDown = 'E';
    int capacitanceUp = 'Z';
    int capacitanceDown = 'C';
    int ununUp = 'I';
    int ununDown = 'K';
    int weaponPrev = 'J';
    int weaponNext = 'L';
    int pause = 'P';
    int statusReadout = 0x09;   // VK_TAB
};

/**
 * Configuration structure
 */
struct GameConfig {
    TrackCurve trackCurve;          // Which measurement curve to use as track
    bool useAllDataPoints;          // true = all points, false = time window only
    int difficultyLevel;            // 1-5 difficulty (1=Easy, 5=Very Hard)
    float motorVolume;              // 0.0-1.0
    float swrVolume;                // 0.0-1.0
    float morseVolume;              // 0.0-1.0
    float warningVolume;            // 0.0-1.0 border warning beep volume
    float collisionVolume;          // 0.0-1.0 crash/scrape sound volume
    float enemyVolume;              // 0.0-1.0 noise enemy & Störer volume
    float uiVolume;                 // 0.0-1.0 menu/UI sound volume
    float steeringSensitivity;      // 0.1-2.0 steering sensitivity multiplier
    float accelerationSensitivity;  // 0.1-2.0 acceleration sensitivity multiplier
    float aimSensitivity;              // 0.1-2.0 aim sensitivity multiplier
    float inputDeadzone;            // 0.02-0.30 analog stick deadzone
    bool swrVibration;              // Use controller vibration (master toggle)
    float vibrationIntensity;       // 0.0-1.0 overall vibration intensity factor
    int targetLaps;                 // Number of laps to complete (0 = infinite)
    bool paddleSwap;                // Swap LB/RB dot/dash assignment
    int ttsSpeed;                   // TTS speech rate (-10 to 10, 0 = default)
    TTSEngineType ttsEngine;        // TTS engine selection
    std::string ttsVoice;           // Selected TTS voice name (empty = system default)
    bool aimAssist;                 // Acoustic aim crosshair for morse signals
    bool trafficReports;            // Periodic humorous SWR traffic reports
    bool noiseBlankerEnabled;         // Noise blanker weapon available
    bool emergencyBrakeEnabled;       // Emergency brake assist (L3 click)
    bool noiseAlerts;                 // Traffic service announces noise enemies ahead
    bool intruderMonitoring;          // QSO Störer detection and alerts
    bool borderWarningEnabled;        // Graduated border warning system (assist)
    bool curveAnnouncementEnabled;    // Announce upcoming curves with direction and severity
    float curveAnnouncementDistance;  // How far ahead to scan for curves (in kHz) — higher = more reaction time
    // Braille display settings (only active when NVDA is running)
    bool brailleEnabled;              // Master toggle: show any values on braille display
    bool brailleShowSpeed;            // Show speed on braille
    bool brailleShowFreq;             // Show frequency on braille
    bool brailleShowSWR;              // Show SWR on braille
    bool brailleShowPA;               // Show PA health on braille
    bool brailleShowScore;            // Show score on braille
    bool brailleShowLap;              // Show lap count on braille
    bool brailleShowTuner;            // Show tuner L/C/UnUn on braille
    // Game Elements (accessibility toggles — all enabled by default)
    bool elemMorseSignals;            // Morse code signals appear on the track
    bool elemSwrDamage;               // Bad SWR causes PA damage
    bool elemNoiseEnemies;            // Noise interference enemies spawn
    bool elemQsoStoerer;              // QSO Störer (intruder) spawns
    bool elemPowerUps;                // Power-up zones appear on the track
    bool elemAutoSteering;            // Auto-steering through curves (no crash damage from curves)
    bool elemAutoAim;                 // Morse signals always correctly aimed (no aiming needed)
    int morseDifficulty;              // Morse character difficulty (1-5, independent of game difficulty)
    KeyMapping keyMapping;            // Keyboard action mapping (remappable)
    // Status readout verbosity (Select button) — per-element toggles
    bool statusShowSpeed;             // Announce speed on select
    bool statusShowFreq;              // Announce frequency on select
    bool statusShowSWR;               // Announce SWR on select
    bool statusShowPA;                // Announce PA health on select
    bool statusShowTuner;             // Announce tuner values on select
    bool statusShowScore;             // Announce score on select
    bool statusShowLaps;              // Announce lap count on select
    bool statusShowTime;              // Announce game time on select
    int controllerPreset;             // 0=Auto-detect, 1=Xbox, 2=PS4/PS5
    // Controller stick drift calibration — per-axis center offsets
    // Subtracted from raw axis values before deadzone processing to compensate
    // for hardware drift.  Set via the TTS-guided calibration wizard.
    float stickOffsetLX;              // Left stick X center offset
    float stickOffsetLY;              // Left stick Y center offset
    float stickOffsetRX;              // Right stick X center offset
    float stickOffsetRY;              // Right stick Y center offset
    
    GameConfig() : 
        trackCurve(TrackCurve::SWR),
        useAllDataPoints(false),
        difficultyLevel(1),
        motorVolume(0.8f),
        swrVolume(0.6f),
        morseVolume(0.7f),
        warningVolume(0.8f),
        collisionVolume(0.8f),
        enemyVolume(0.8f),
        uiVolume(0.7f),
        steeringSensitivity(1.0f),
        accelerationSensitivity(1.0f),
        aimSensitivity(0.7f),
        inputDeadzone(0.08f),
        swrVibration(true),
        vibrationIntensity(1.0f),
        targetLaps(1),
        paddleSwap(false),
        ttsSpeed(0),
#ifdef __APPLE__
        ttsEngine(TTSEngineType::MACOS_SAY),
#elif defined(_WIN32)
        ttsEngine(TTSEngineType::NVDA),
#else
        ttsEngine(TTSEngineType::WINDOWS_SAPI),
#endif
        aimAssist(true),
        trafficReports(true),
        noiseBlankerEnabled(true),
        emergencyBrakeEnabled(true),
        noiseAlerts(true),
        intruderMonitoring(true),
        borderWarningEnabled(true),
        curveAnnouncementEnabled(true),
        curveAnnouncementDistance(5.0f),
        brailleEnabled(true),
        brailleShowSpeed(true),
        brailleShowFreq(true),
        brailleShowSWR(true),
        brailleShowPA(true),
        brailleShowScore(false),
        brailleShowLap(false),
        brailleShowTuner(false),
        elemMorseSignals(true),
        elemSwrDamage(true),
        elemNoiseEnemies(true),
        elemQsoStoerer(true),
        elemPowerUps(true),
        elemAutoSteering(false),
        elemAutoAim(false),
        morseDifficulty(1),
        statusShowSpeed(true),
        statusShowFreq(true),
        statusShowSWR(true),
        statusShowPA(true),
        statusShowTuner(true),
        statusShowScore(true),
        statusShowLaps(true),
        statusShowTime(true),
        controllerPreset(0),  // 0=Auto-detect
        stickOffsetLX(0.0f),
        stickOffsetLY(0.0f),
        stickOffsetRX(0.0f),
        stickOffsetRY(0.0f) {}
};

/**
 * Game statistics
 */
struct GameStats {
    float gameTime;              // Total game time in seconds
    int score;                   // Current score
    int charactersCollected;     // Morse characters collected
    int charactersTotal;         // Total characters that appeared
    int lapsCompleted;           // Number of laps completed
    float averageSWR;            // Average SWR during gameplay
    float paHealth;              // Power amplifier health (0.0-1.0)
    bool bonusAchieved;          // HAMSPIRIT bonus word completed
    
    GameStats() : 
        gameTime(0.0f), score(0), 
        charactersCollected(0), charactersTotal(0), lapsCompleted(0),
        averageSWR(1.0f), paHealth(1.0f), 
        bonusAchieved(false) {}
};

/**
 * High score entry for leaderboard persistence
 */
struct HighScoreEntry {
    std::string callsign;        // Amateur radio callsign (e.g. "DO9RE")
    std::string playerName;      // Player display name
    int score;                   // Final score
    int lapsCompleted;           // Laps finished
    float gameTime;              // Total time in seconds
    float paHealth;              // Final PA health (0-1)
    bool bonusAchieved;          // HAMSPIRIT bonus
    std::string gameMode;        // "SP" = singleplayer, "MP" = multiplayer
    int playerCount;             // Number of players (1 for singleplayer)
    
    HighScoreEntry() : score(0), lapsCompleted(0), gameTime(0.0f),
                       paHealth(1.0f), bonusAchieved(false),
                       gameMode("SP"), playerCount(1) {}
};

/**
 * Track point on the circular race track
 */
struct TrackPoint {
    float angle;           // Position on ring (0-2π radians)
    float swr;             // SWR value at this point
    float returnLoss;      // Return loss (dB)
    float resistance;      // Resistance R (real part of impedance)
    float impedanceMag;    // Impedance magnitude |Z|
    float reactance;       // Reactance (Ohms)
    float phase;           // Phase angle
    float frequency;       // Frequency at this point
    
    TrackPoint() : angle(0.0f), swr(1.0f), returnLoss(0.0f),
                   resistance(50.0f), impedanceMag(50.0f), reactance(0.0f),
                   phase(0.0f), frequency(0.0f) {}
};

/**
 * Track generator - transforms linear measurement curves into circular tracks
 */
class TrackGenerator {
public:
    /**
     * Generate circular track from measurement data
     * @param measurements Source VNA measurements
     * @param curveType Which curve to use for track shape
     * @return Vector of track points distributed around circle
     */
    static std::vector<TrackPoint> generateTrack(
        const std::vector<MeasurementPoint>& measurements,
        TrackCurve curveType
    );
    
    /**
     * Interpolate track point at any angle
     * @param track Track points
     * @param angle Angle in radians (0-2π)
     * @return Interpolated track point
     */
    static TrackPoint interpolateAt(
        const std::vector<TrackPoint>& track,
        float angle
    );
    
    /**
     * Get curve value from measurement point
     * @param point Measurement point
     * @param curveType Which curve value to extract
     * @return Curve value
     */
    static float getCurveValue(
        const MeasurementPoint& point,
        TrackCurve curveType
    );
};

/**
 * Spatial audio manager - handles stereo positioning based on track curvature
 */
class SpatialAudio {
public:
    SpatialAudio();
    
    /**
     * Update motor sound based on track curvature ahead
     * @param track Track points
     * @param playerAngle Current player angle
     * @param lookAheadDistance How far to look ahead (radians)
     * @return Pan value (-1.0 = full left, 0.0 = center, +1.0 = full right)
     */
    float calculatePan(
        const std::vector<TrackPoint>& track,
        float playerAngle,
        float lookAheadDistance
    );
    
    /**
     * Calculate average curve value ahead of player
     * @param track Track points
     * @param playerAngle Current player angle
     * @param lookAheadDistance How far to look ahead
     * @param curveType Which curve to analyze
     * @return Average curve value in lookahead region
     */
    float calculateAverageCurveAhead(
        const std::vector<TrackPoint>& track,
        float playerAngle,
        float lookAheadDistance,
        TrackCurve curveType
    );

private:
    float lastPan;  // For smoothing pan changes
};

/**
 * L-C Tuner for antenna matching
 */
class LCTuner {
public:
    LCTuner();
    
    /**
     * Adjust inductance
     * @param delta Change in microhenries (µH)
     * @return true if at limit (bumped)
     */
    bool adjustInductance(float delta);
    void setInductance(float value);
    
    /**
     * Adjust capacitance
     * @param delta Change in picofarads (pF)
     * @return true if at limit (bumped)
     */
    bool adjustCapacitance(float delta);
    void setCapacitance(float value);
    
    bool isAtMinInductance() const { return inductanceUH <= 0.01f; }
    bool isAtMaxInductance() const { return inductanceUH >= MAX_L_UH - 0.01f; }
    bool isAtMinCapacitance() const { return capacitancePF <= 0.01f; }
    bool isAtMaxCapacitance() const { return capacitancePF >= MAX_C_PF - 0.01f; }
    float getMaxInductance() const { return MAX_L_UH; }
    float getMaxCapacitance() const { return MAX_C_PF; }
    
    /**
     * Get current inductance in microhenries
     */
    float getInductance() const { return inductanceUH; }
    
    /**
     * Get current capacitance in picofarads
     */
    float getCapacitance() const { return capacitancePF; }
    
    /**
     * Reset to default values (minimum — effectively bypassed)
     */
    void reset() { inductanceUH = 0.0f; capacitancePF = 0.0f; }
    
    /**
     * Calculate transformed impedance
     * @param frequency Frequency in Hz
     * @param loadZ Load impedance (Ohms)
     * @return Transformed impedance at input
     */
    std::complex<float> calculateInputImpedance(float frequency, std::complex<float> loadZ) const;
    
private:
    float inductanceUH;   // Inductance in microhenries (µH)
    float capacitancePF;  // Capacitance in picofarads (pF)
    
    static constexpr float MIN_L_UH = 0.1f;    // 0.1 µH minimum
    static constexpr float MAX_L_UH = 100.0f;  // 100 µH maximum
    static constexpr float MIN_C_PF = 1.0f;     // 1 pF minimum
    static constexpr float MAX_C_PF = 1000.0f;  // 1000 pF maximum
};

/**
 * UnUn (Unbalanced to Unbalanced) impedance transformer
 */
class UnUn {
public:
    enum class Ratio {
        RATIO_1_1 = 0,   // 1:1 (no transformation)
        RATIO_4_1 = 1,   // 4:1 (multiply impedance by 4)
        RATIO_9_1 = 2,   // 9:1 (multiply impedance by 9)
        RATIO_16_1 = 3   // 16:1 (multiply impedance by 16)
    };
    
    UnUn();

    static float getMultiplier(Ratio ratio);
    
    /**
     * Set impedance ratio
     */
    void setRatio(Ratio ratio);
    
    /**
     * Get current ratio
     */
    Ratio getRatio() const { return currentRatio; }
    
    /**
     * Get ratio as multiplier
     */
    float getRatioMultiplier() const { return getMultiplier(currentRatio); }
    
    /**
     * Transform impedance through UnUn
     * @param impedance Input impedance
     * @return Transformed impedance
     */
    std::complex<float> transform(std::complex<float> impedance) const;
    
private:
    Ratio currentRatio;
};

/**
 * Complete antenna matching network
 */
class AntennaNetwork {
public:
    AntennaNetwork();
    
    /**
     * Get L-C tuner
     */
    LCTuner& getTuner() { return tuner; }
    const LCTuner& getTuner() const { return tuner; }
    
    /**
     * Get UnUn transformer
     */
    UnUn& getUnUn() { return unun; }
    const UnUn& getUnUn() const { return unun; }
    
    /**
     * Calculate final SWR with current matching network settings
     * @param track Track points
     * @param playerAngle Current player position
     * @return Adjusted SWR value
     */
    float calculateAdjustedSWR(const std::vector<TrackPoint>& track, float playerAngle) const;
    
    /**
     * Calculate adjusted reactance after matching network (tuner + UnUn).
     * Used for L/C indication in SWR warning tone.
     * @param track Track points
     * @param playerAngle Current player position
     * @return Adjusted reactance in Ohms (positive = inductive, negative = capacitive)
     */
    float calculateAdjustedReactance(const std::vector<TrackPoint>& track, float playerAngle) const;
    
    /**
     * Calculate speed factor based on matching quality
     * Perfect match (SWR = 1.0) = 1.0
     * Poor match (high SWR) = reduced speed
     * @param swr Current SWR
     * @return Speed multiplier (0.0 to 1.0)
     */
    float calculateSpeedFactor(float swr) const;
    
private:
    LCTuner tuner;
    UnUn unun;
};

/**
 * Morse code character definition
 */
struct MorseChar {
    char character;           // The character (A-Z, 0-9)
    std::string pattern;      // Morse pattern (e.g., ".-" for A)
    int difficulty;           // Difficulty level (1-5, lower is easier)
    
    MorseChar() : character(' '), pattern(""), difficulty(1) {}
    MorseChar(char c, const std::string& p, int d) 
        : character(c), pattern(p), difficulty(d) {}
};

/**
 * Morse code database
 */
class MorseDatabase {
public:
    MorseDatabase();
    
    /**
     * Get morse pattern for a character
     * @param c Character to look up
     * @return Morse pattern or empty string if not found
     */
    std::string getPattern(char c) const;
    
    /**
     * Get random character by difficulty level
     * @param maxDifficulty Maximum difficulty (1-5)
     * @return Random character within difficulty range
     */
    char getRandomChar(int maxDifficulty) const;
    
    /**
     * Get all characters in a string
     * @param str String to get characters from
     * @return Vector of MorseChar for each character
     */
    std::vector<MorseChar> getCharsForString(const std::string& str) const;
    
    /**
     * Get next character in alphanumeric sequence
     */
    static char getNextChar(char c);
    
    /**
     * Get previous character in alphanumeric sequence
     */
    static char getPrevChar(char c);
    
private:
    std::map<char, MorseChar> database;
    void initializeDatabase();
};

// ---- Lifetime constants (centralized for all entity types) ----
static constexpr float MORSE_SIGNAL_LIFETIME = 18.0f;   // Seconds a Morse signal persists
static constexpr float NOISE_ENEMY_BASE_LIFETIME = 25.0f; // Base lifetime for noise enemies (+ bandwidth scaling)
static constexpr float NOISE_ENEMY_BW_LIFETIME_SCALE = 20.0f; // Additional lifetime per radian bandwidth

/**
 * Common base for all track-based entities sharing the same lifecycle.
 * Encapsulates spawn timing, angular position, and removal state.
 */
struct TrackEntity {
    float angle = 0.0f;              // Position on track (radians)
    float spawnTime = 0.0f;          // When it was spawned (game time)
    float lifetime = 10.0f;          // How long it lasts (seconds)
    bool markedForRemoval = false;   // Flagged for deferred removal
};

/**
 * Remove entities whose lifetime has expired or that are flagged for removal.
 * Centralizes the erase-remove pattern used by all entity types.
 */
template<typename T>
void removeExpiredEntities(std::vector<T>& entities, float gameTime) {
    entities.erase(
        std::remove_if(entities.begin(), entities.end(),
            [gameTime](const T& e) {
                return e.markedForRemoval || (gameTime - e.spawnTime) > e.lifetime;
            }),
        entities.end()
    );
}

// Shared angular constants used by utility functions
static constexpr float HS_PI = 3.14159265359f;
static constexpr float HS_TWO_PI = 2.0f * HS_PI;

/**
 * Calculate stereo pan position from entity angle relative to player angle.
 * Returns value in [-1, 1]: -1 = full left, 0 = center, +1 = full right.
 */
inline float calculatePanPosition(float entityAngle, float playerAngle) {
    float angleDiff = entityAngle - playerAngle;
    while (angleDiff > HS_PI) angleDiff -= HS_TWO_PI;
    while (angleDiff < -HS_PI) angleDiff += HS_TWO_PI;
    return std::max(-1.0f, std::min(1.0f, angleDiff / HS_PI));
}

/**
 * Calculate aim-lock strength for a target at a given angle.
 * Returns normalized lock value 0.0–1.0 (1.0 = dead center, 0.0 = outside range).
 * Includes partial lock zone at 3× lockRange for faint indication.
 */
inline float calculateAimLock(float aimAngle, float targetAngle, float lockRange) {
    float angleDiff = targetAngle - aimAngle;
    while (angleDiff > HS_PI) angleDiff -= HS_TWO_PI;
    while (angleDiff < -HS_PI) angleDiff += HS_TWO_PI;
    float distance = std::abs(angleDiff);
    if (distance < lockRange) {
        return 1.0f - (distance / lockRange);
    } else if (distance < lockRange * 3.0f) {
        return 0.3f * (1.0f - (distance - lockRange) / (lockRange * 2.0f));
    }
    return 0.0f;
}

/**
 * Word-wrap text to a maximum line width.
 * Splits on spaces; words longer than maxWidth are broken at the limit.
 */
inline std::vector<std::string> wrapText(const std::string& text, int maxWidth) {
    std::vector<std::string> lines;
    if (text.empty() || maxWidth <= 0) return lines;
    std::string remaining = text;
    while (remaining.size() > static_cast<size_t>(maxWidth)) {
        size_t pos = remaining.rfind(' ', static_cast<size_t>(maxWidth));
        if (pos == std::string::npos || pos == 0) {
            pos = static_cast<size_t>(maxWidth);
        }
        lines.push_back(remaining.substr(0, pos));
        remaining = remaining.substr(pos < remaining.size() ? pos + 1 : pos);
    }
    if (!remaining.empty()) lines.push_back(remaining);
    return lines;
}

/**
 * Cursor blink state for text input overlays.
 * Uses a 1-second period: visible for the first 500 ms, hidden for the next 500 ms.
 */
inline bool isCursorVisible(float elapsedTimeSeconds) {
    return std::fmod(elapsedTimeSeconds, 1.0f) < 0.5f;
}

// ---- Centralized color palette (platform-neutral RGBA) ----
struct HamSpiritColor {
    uint8_t r, g, b, a;
};

static constexpr HamSpiritColor CLR_HS_BG        = {10, 10, 30, 255};
static constexpr HamSpiritColor CLR_HS_CYAN      = {0, 180, 220, 255};
static constexpr HamSpiritColor CLR_HS_YELLOW    = {255, 220, 0, 255};
static constexpr HamSpiritColor CLR_HS_WHITE     = {255, 255, 255, 255};
static constexpr HamSpiritColor CLR_HS_GRAY      = {160, 160, 160, 255};
static constexpr HamSpiritColor CLR_HS_DARK_GRAY = {80, 80, 80, 255};
static constexpr HamSpiritColor CLR_HS_GREEN     = {0, 200, 80, 255};
static constexpr HamSpiritColor CLR_HS_FIELD_BG  = {20, 20, 50, 255};
static constexpr HamSpiritColor CLR_HS_PANEL_BRD = {80, 80, 160, 255};
static constexpr HamSpiritColor CLR_HS_HIGHLIGHT = {30, 30, 80, 255};
static constexpr HamSpiritColor CLR_HS_BANNER_BG = {10, 10, 50, 255};

// ---- Wallpaper image paths (centralized for all platforms) ----
static const char* const WALLPAPER_FILENAMES[] = {
    "img/HamSpirit-1.PNG",   // Index 0: Title screen
    "img/HamSpirit-2.PNG",   // Index 1: Menu / intermissions
    "img/HamSpirit-3.PNG"    // Index 2: Gameplay / pause
};
static constexpr int WALLPAPER_COUNT = 3;

/**
 * Get the list of wallpaper image paths relative to the application directory.
 */
inline std::vector<std::string> getWallpaperPaths() {
    std::vector<std::string> paths;
    for (int i = 0; i < WALLPAPER_COUNT; i++) {
        paths.push_back(WALLPAPER_FILENAMES[i]);
    }
    return paths;
}

// ---- Status formatting utilities (centralized for TTS, Braille, Log, GUI) ----

/**
 * Format speed value for display.
 * @param speedKHz Speed in kHz/s
 * @return Formatted string like "2.5"
 */
inline std::string formatSpeed(float speedKHz) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", speedKHz);
    return std::string(buf);
}

/**
 * Format SWR value for display.
 * @param swr Standing wave ratio
 * @return Formatted string like "1.5"
 */
inline std::string formatSWR(float swr) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", swr);
    return std::string(buf);
}

/**
 * Format PA health value for display.
 * @param health PA health (0.0–1.0)
 * @return Formatted string like "85"
 */
inline std::string formatPAHealth(float health) {
    return std::to_string(static_cast<int>(health * 100.0f));
}

/**
 * Format score value for display.
 * @param score Integer score
 * @return Formatted string like "1234"
 */
inline std::string formatScore(int score) {
    return std::to_string(score);
}

// ---- GUI Overlay Models (platform-independent state) ----

/**
 * Menu overlay model — describes menu state consumed by platform renderers.
 * Layout logic (centering, positioning) lives here; renderers only draw.
 */
struct MenuModel {
    std::string title;
    std::vector<std::string> items;
    int selectedIndex = 0;
    bool visible = false;
    std::string footerText;
};

/**
 * Text overlay model — centered text display (intro, controls, game over).
 */
struct TextOverlayModel {
    std::string text;
    bool visible = false;
};

/**
 * Text input overlay model — single-line text entry with cursor.
 */
struct TextInputModel {
    std::string label;
    std::string inputText;
    int cursorPosition = 0;
    bool visible = false;
};

// ---- Braille Output Interface (platform-independent) ----

/**
 * Platform-independent braille output interface.
 * Windows: NVDA braille API. macOS: VoiceOver. Linux: BrlAPI (if available).
 */
class IBrailleOutput {
public:
    virtual ~IBrailleOutput() = default;
    
    /** Check whether a braille display is available and connected. */
    virtual bool isAvailable() const = 0;
    
    /** Display text on the braille device. */
    virtual void display(const std::string& text) = 0;
};

/**
 * Morse signal appearing on the track
 */
struct MorseSignal : TrackEntity {
    char character = ' ';             // The character to collect
    bool collected = false;           // Has it been collected?
    int collectedByPlayer = -1;       // Index of player who collected (-1 = none)
    float panPosition = 0.0f;        // Stereo pan position (-1 to 1)
    
    MorseSignal() { lifetime = MORSE_SIGNAL_LIFETIME; }
};

/**
 * Noise enemy — annoying static/hum that must be "shot" with noise blanker
 * Has a bandwidth (angular width) like real RF interference.
 * Wider = louder/tougher but easier to aim. Narrower = weaker but harder to hit at speed.
 */
struct NoiseEnemy : TrackEntity {
    float bandwidth = 0.3f;          // Angular width (radians) — like RF bandwidth
    int health = 1;                  // Hits to destroy (scales with bandwidth)
    float intensity = 0.5f;          // Noise volume/annoyance (0.0-1.0)
    bool destroyed = false;          // Has it been destroyed?
    int destroyedByPlayer = -1;      // Index of player who destroyed (-1 = none)
    
    NoiseEnemy() { lifetime = 15.0f; }
};

/**
 * QSO Störer enemy — aggressive interference that chases the player
 */
struct QSOStoerer : TrackEntity {
    float speed = 0.0f;              // Speed in radians/second
    float lateralOffset = 0.0f;      // Lateral position on track (-1..+1), like playerLateralOffset
    bool active = false;             // Currently active/chasing
    float health = 1.0f;             // Slowdown factor (1.0 = full speed, reduced by noise blanker hits)
    float lastCollisionTime = -10.0f;// Last time it collided with player
    float hornissPhase = 0.0f;       // Phase for hornet buzz sound
    float drivingErrorTimer = 0.0f;  // >0 = Störer is swerving/slowed after a driving error
    char lastMorseChar = '\0';       // Last morse char received (for "99" cheat detection)
    // Position tracking for TTS announcements and overtake sound
    // 0 = unknown/not tracked, 1 = ahead, 2 = alongside, 3 = behind
    int positionState = 0;
};

/**
 * Power-up types available on the track
 */
enum class PowerUpType {
    SPEED_BOOST = 0,       // Increases maximum speed
    FIRE_RATE = 1,         // Reduces weapon cooldown
    AUTO_FIRE = 2,         // Hold trigger for continuous fire
    SWR_IMMUNITY = 3,      // Immune to SWR damage
    DURATION_EXTEND = 4,   // Permanently extends power-up duration by 20s
    COUNT = 5
};

/**
 * Power-up zone on the track — collectible or destructible entity
 */
struct PowerUp : TrackEntity {
    PowerUpType type = PowerUpType::SPEED_BOOST;  // Which power-up this is
    float zoneHalfWidth = 0.3f;         // Half-width of the zone (radians)
    bool collected = false;              // Has it been collected?
    int collectedByPlayer = -1;          // Index of player who collected (-1 = none)
    bool destroyed = false;              // Has it been shot/exploded?
    float panPosition = 0.0f;           // Stereo pan (-1 to 1) relative to player aim
    float collectionTime = 2.0f;        // Seconds of dual-trigger hold required to collect
    float collectionProgress = 0.0f;    // Current collection progress (0..1)
    int quality = 1;                    // 1-3 quality tier (affects collection time and effects)
    uint32_t uid = 0;                   // Unique ID for stable tracking across vector modifications
    
    PowerUp() { lifetime = 30.0f; }
};

/**
 * Active power-up effect with individual expiration timer
 */
struct ActivePowerUp {
    PowerUpType type;            // Which effect is active
    float remainingTime;         // Seconds until expiration
    float savedValue;            // Saved value to restore on expiration (e.g., original maxSpeed)
    
    ActivePowerUp() : type(PowerUpType::SPEED_BOOST), remainingTime(20.0f), savedValue(0.0f) {}
    ActivePowerUp(PowerUpType t, float duration, float saved = 0.0f) 
        : type(t), remainingTime(duration), savedValue(saved) {}
};

/**
 * Morse cannon/paddle system
 */
class MorseCannon {
public:
    enum class Mode {
        VERTICAL_KEY,  // Single button for both dots and dashes
        PADDLE         // Two buttons: left=dot, right=dash
    };
    
    MorseCannon();
    
    /**
     * Set cannon mode
     */
    void setMode(Mode mode) { currentMode = mode; }
    Mode getMode() const { return currentMode; }
    
    /**
     * Update cannon state
     * @param input Current input state
     * @param dt Delta time
     * @param suppressVerticalKey If true, skip RT vertical key processing (for dual-trigger power-up collection)
     */
    void update(const GamepadState& input, float dt, bool suppressVerticalKey = false);
    
    /**
     * Get the character that was just sent (if any)
     * @return Character or '\0' if none
     */
    char getLastSentChar();
    
    /**
     * Reset cannon state
     */
    void reset();
    
    /**
     * Get current morse pattern being built
     */
    const std::string& getCurrentPattern() const { return currentPattern; }
    
    /**
     * Set paddle swap state
     */
    void setPaddleSwap(bool swap) { paddleSwapped = swap; }
    bool getPaddleSwap() const { return paddleSwapped; }
    
    /**
     * Is cannon currently active?
     */
    bool isActive() const;
    
    /**
     * Is the currently active paddle producing dashes? (for audio differentiation)
     */
    bool isDashPaddleActive() const;
    
    // Morse timing constants — public so audio playback can use the same values
    static constexpr float DOT_DURATION = 0.1f;      // 100ms — one dot length
    static constexpr float DASH_THRESHOLD = 0.20f;    // Hold >= 200ms = dash (shorter = dot)
    static constexpr float DASH_DURATION = 0.3f;     // 300ms — three dot lengths
    static constexpr float ELEMENT_SPACE = 0.1f;     // 100ms — inter-element space (one dot length)
    static constexpr float CHAR_TIMEOUT = 0.8f;      // 800ms — more time to finish a character
    static constexpr float PADDLE_REPEAT_DOT = 0.2f;   // Auto-repeat: DOT_DURATION + ELEMENT_SPACE
    static constexpr float PADDLE_REPEAT_DASH = 0.4f;  // Auto-repeat: DASH_DURATION + ELEMENT_SPACE
    static constexpr float PATTERN_REPEAT_PAUSE = 0.4f; // Pause between signal pattern repeats
    
private:
    Mode currentMode;
    std::string currentPattern;     // Pattern being built
    float lastInputTime;            // Time of last input
    float characterTimeout;         // Time before pattern resets
    char lastSentCharacter;         // Last character sent
    bool charWasRead;               // Was last char read by game?
    bool buttonWasPressed;          // Edge detection for vertical key
    bool buttonHeld;                // Is button currently held? (for vertical key hold timing)
    float buttonHoldTime;           // How long button has been held (seconds)
    bool leftWasPressed;            // Edge detection for paddle left
    bool rightWasPressed;           // Edge detection for paddle right
    bool paddleSwapped;             // Swap dot/dash assignment on bumpers
    bool paddleActive;              // Is either paddle button currently pressed?
    float leftHoldTime;             // Hold timer for paddle left auto-repeat
    float rightHoldTime;            // Hold timer for paddle right auto-repeat
    
    // Iambic paddle state
    bool iambicSqueeze;             // Both paddles pressed simultaneously
    bool iambicLastWasDot;          // Last element in iambic alternation was a dot
    float iambicElementTimer;       // Timer for current iambic element
    float iambicSpaceTimer;         // Timer for inter-element space
    bool iambicInSpace;             // Currently in inter-element space
    
    void addDot();
    void addDash();
    char recognizePattern();
};

/**
 * Weapon types available to player
 */
enum class WeaponType {
    NOISE_BLANKER = 0,  // Noise enemy weapon (Left Trigger)
    COUNT = 1
};

/**
 * Morse signal manager
 */
class MorseSignalManager {
public:
    MorseSignalManager(MorseDatabase* db);
    
    /**
     * Update all morse signals
     * @param playerAngle Current player position
     * @param gameTime Current game time
     * @param dt Delta time
     */
    void update(float playerAngle, float gameTime, float dt);
    
    /**
     * Spawn a new morse signal
     * @param character Character to spawn
     * @param angle Position on track
     * @param gameTime Current game time
     */
    void spawnSignal(char character, float angle, float gameTime);
    
    /**
     * Check if a position is too close to existing signals
     * @param angle Proposed position
     * @param minDistance Minimum angular distance (radians)
     * @return true if too close to existing signal
     */
    bool isPositionTooClose(float angle, float minDistance) const;
    
    /**
     * Get signal closest to player aiming direction
     * @param playerAngle Player position
     * @param aimAngle Aiming direction relative to player
     * @param aimMargin Aiming tolerance (radians)
     * @return Pointer to closest signal or nullptr
     */
    MorseSignal* getTargetedSignal(float playerAngle, float aimAngle, float aimMargin);
    
    /**
     * Attempt to collect a signal
     * @param signal Signal to collect
     * @param sentChar Character that was sent
     * @param reactance Current antenna reactance
     * @return true if collected
     */
    bool tryCollectSignal(MorseSignal* signal, char sentChar, float reactance);
    
    /**
     * Get all active signals
     */
    const std::vector<MorseSignal>& getSignals() const { return signals; }
    
    /**
     * Get mutable access to all active signals (for explosion damage etc.)
     */
    std::vector<MorseSignal>& getSignalsMutable() { return signals; }
    
    /**
     * Get count of active signals
     */
    int getActiveSignalCount() const { return static_cast<int>(signals.size()); }
    
    /**
     * Clear all signals
     */
    void clear();
    
private:
    MorseDatabase* database;
    std::vector<MorseSignal> signals;
    
    void removeExpiredSignals(float gameTime);
    void updateSignalPanning(float playerAngle);
};

/**
 * Title melody for the Ham Spirit game
 *
 * A 73-second chiptune composition in F harmonic minor with tempo changes,
 * sweeps that frame each section, and a unique driving feel.  Uses the
 * existing SynthesizerEngine waveforms:
 *   curve 0 = Triangle  (bass)
 *   curve 1 = Sine      (kick body / sub)
 *   curve 2 = Sine      (pad layer)
 *   curve 3 = Square    (melody lead)
 *   curve 4 = Sawtooth  (sweep / pad stabs)
 *
 * Structure:
 *   Intro   ( 0– 8 s)  90 BPM  — atmospheric rise, radio-tuning feel
 *   A       ( 8–24 s) 140 BPM  — driving main theme
 *   B       (24–36 s) 150 BPM  — syncopated groove, call-and-response
 *   Bridge  (36–44 s) 110 BPM  — breakdown, bass solo, tension sweep
 *   C       (44–58 s) 160 BPM  — climax, double-time hats, full energy
 *   Outro   (58–73 s) 160→80   — decelerating fade, melody dissolves
 *
 * Lifecycle:
 *  - start()          when the game launches (INTRO / MAIN_MENU)
 *  - beginFadeOut()   on last confirm before racing starts
 *  - stop()           on PLAYING state entry (guaranteed silence during race)
 *  - start()          when entering PAUSED state
 *  - beginFadeOut()   when leaving PAUSED back to PLAYING
 */
class TitleMelody {
public:
    TitleMelody();

    void start();
    void startVictory();    // Start victory melody (completed with PA intact)
    void startDefeat();     // Start defeat melody (PA destroyed)
    void beginFadeOut();
    void stop();
    bool isPlaying() const { return playing; }

    void renderFrame(std::vector<int16_t>& buffer,
                     int samples,
                     int sampleRate,
                     SynthesizerEngine* engine,
                     std::vector<int16_t>& mixBuf);

private:
    bool playing;
    bool fadingOut;
    double playbackPos;
    float masterVolume;
    float fadeSpeed;
    double totalLength;     // Current composition length (varies by melody type)

    // Phase accumulators for procedural drums
    double kickPhase;
    double snarePhase;
    unsigned int hihatSeed;

    // --- composition data ----------------------------------------------------
    struct NoteEvent {
        double time;
        double duration;
        double freqHz;
        int    curveIndex;
        float  volume;
        float  pan;         // 0..1 (0.5 = centre)
    };

    struct DrumHit {
        double time;
        int    type;        // 0 = kick, 1 = snare, 2 = closed hihat, 3 = open hihat
        float  volume;
    };

    std::vector<NoteEvent> melodyEvents;
    std::vector<NoteEvent> bassEvents;
    std::vector<NoteEvent> padEvents;   // sweeps & pad chords
    std::vector<DrumHit>   drumEvents;

    void buildComposition();
    void buildVictoryComposition();
    void buildDefeatComposition();

    // --- helpers -------------------------------------------------------------
    /// Get instantaneous BPM at a given playback position (seconds)
    static double getBPM(double pos);

    static constexpr float MELODY_VOLUME = 0.30f;
    static constexpr float BASS_VOLUME   = 0.28f;
    static constexpr float DRUM_VOLUME   = 0.22f;
    static constexpr float PAD_VOLUME    = 0.16f;
    static constexpr float FADE_DURATION = 2.0f;
    static constexpr double TOTAL_LENGTH = 73.0;
    static constexpr double VICTORY_LENGTH = 25.0;
    static constexpr double DEFEAT_LENGTH = 25.0;
};

/**
 * Main Ham Spirit game class
 */
class Game {
public:
    /**
     * Constructor
     * @param translation Translation manager for localization
     * @param logger Logger for debugging
     * @param consoleInput Console input for keyboard reading (optional)
     */
    Game(TranslationManager* translation, Logger* logger, IConsoleInput* consoleInput = nullptr);
    
    /**
     * Destructor
     */
    ~Game();
    
    /**
     * Initialize game with measurement data
     * @param measurements VNA measurement data
     * @param analyzer Reference to acoustic analyzer (for audio engines)
     * @return true on success
     */
    bool initialize(
        const std::vector<MeasurementPoint>& measurements,
        AcousticAnalyzer* analyzer
    );
    
    /**
     * Run the game (blocking call - returns when game exits)
     */
    void run();
    
    /**
     * Shutdown game and cleanup resources
     */
    void shutdown();
    
    /**
     * Get current game state
     */
    GameState getState() const { return currentState; }
    
    /**
     * Get game statistics
     */
    const GameStats& getStats() const { return stats; }
    
    /**
     * Get game configuration
     */
    GameConfig& getConfig() { return config; }
    const GameConfig& getConfig() const { return config; }

private:
    // Core systems
    TranslationManager* translation;
    Logger* logger;
    AcousticAnalyzer* analyzer;
    IConsoleInput* consoleInput;   // Console input for keyboard reading
    
    // Input systems
    std::unique_ptr<IGamepadInput> gamepad;
    std::unique_ptr<KeyboardGamepadEmulator> keyboard;
    
    // TTS for narration
    std::unique_ptr<TTSManager> tts;
    
    // Audio engine and backend for game sounds
    std::unique_ptr<SynthesizerEngine> audioEngine;
    std::unique_ptr<IAudioBackend> ownedAudioBackend;  // Independent audio backend (owned)
    IAudioBackend* audioBackend;              // Active audio backend pointer
    std::vector<int16_t> audioBuffer;    // Reusable audio buffer
    std::vector<int16_t> mixBuffer;      // Pre-allocated mixing buffer
    
    // Title melody (chiptune background music)
    std::unique_ptr<TitleMelody> titleMelody;
    
    // Audio thread — generates and plays audio independently of game loop
    std::thread audioThread;
    std::atomic<bool> audioRunning;
    std::atomic<bool> audioThreadExited{true};  // Set by audio thread on exit, for safe join
    std::mutex audioStateMtx;            // Protects shared audio state
    void audioThreadFunc();
    
    // 6.4: Lock-free immediate audio event queue (game thread → audio thread)
    // Critical (P0) events bypass the frame-based AudioParams mechanism for
    // sub-frame latency.  Game thread pushes, audio thread pops — no mutex needed.
    ImmediateAudioQueue immediateAudioQueue;
    
    // 6.1: Central feedback orchestrator — single entry point for all feedback triggers
    FeedbackOrchestrator feedbackOrchestrator;
    
    // 6.8: Latency tracker — measures end-to-end pipeline latency
    LatencyTracker latencyTracker;
    
    // 6.3: Audio thread frame start timestamp for sample-offset calculation
    FeedbackTimePoint audioFrameStartTime{};
    
    // 6.4: Previous morse cannon active state for edge detection (key-down/key-up)
    bool prevMorseCannonActive = false;
    
    // Shared audio state (written by game loop, read by audio thread)
    struct AudioParams {
        // 6.5: Event timestamp — shared time basis for sample-accurate rendering
        FeedbackTimePoint eventTimestamp{};   // When this state was written (steady_clock)
        // 6.8: Latency tracking measurement index (for current pipeline cycle)
        size_t latencyMeasurementIdx = 0;
        
        float pan = 0.0f;
        float motorFreq = 220.0f;
        float motorVolume = 0.0f;      // 0 = silent
        float motorRoughness = 0.0f;   // 0 = smooth road (good SWR), 1 = rough (bad SWR)
        float swrFreq = 880.0f;
        float swrVolume = 0.0f;        // 0 = silent (alert beeping controlled by swrAlertActive)
        bool swrAlertActive = false;   // true when SWR needs attention (beeping mode)
        float swrAlertRate = 0.0f;     // Beep rate: 0=slow, 1=fast (scales with SWR severity)
        bool morseCannonActive = false;
        int cannonAudioFrames = 0;       // Remaining frames of morse cannon tone
        bool morseCannonIsDash = false;   // true = dash tone, false = dot tone
        // Adjustment sound feedback (ascending/descending tones)
        int adjustSoundFrames = 0;     // Remaining frames to play adjustment sound
        bool adjustSoundUp = true;     // true = ascending, false = descending
        float adjustSoundPan = 0.5f;   // Pan position for adjustment sound (0=left, 0.5=center, 1=right)
        int bumperSoundFrames = 0;     // Bumper sound when tuner hits limit
        float reactanceAtPlayer = 0.0f;  // Current reactance: negative = capacitive, positive = inductive
        float paDamageLevel = 0.0f;    // PA damage level for audio distortion (0=healthy, 1=destroyed)
        int paDamageSoundFrames = 0;   // Partial damage event crackle/pop sound
        int paRepairSoundFrames = 0;   // PA repair chime (morse collection heals PA)
        // Collection feedback sounds
        int collectSoundFrames = 0;    // Success: ascending chime
        int missAimSoundFrames = 0;    // Correct morse but wrong aim
        int missMorseSoundFrames = 0;  // Wrong morse
        // Menu interaction sounds
        int menuNavSoundFrames = 0;    // Menu navigation beep
        int menuSelectSoundFrames = 0; // Menu selection beep
        int pauseSoundFrames = 0;      // Pause sound (descending)
        int unpauseSoundFrames = 0;    // Unpause/resume sound (ascending)
        int trafficBeepFrames = 0;     // Traffic report whistle/beep before announcement
        int statusStartSoundFrames = 0; // Status readout start chime
        int statusDoneSoundFrames = 0;  // Status readout completion chime
        int keyClickSoundFrames = 0;    // Keyboard/text input click feedback
        // Aim indicator — per-target-type lock strength for distinct audio cues
        float aimLockStrength = 0.0f;  // Max of all types (used for stereo narrowing)
        float aimLockMorse = 0.0f;     // 0..1 lock on nearest morse signal
        float aimLockNoise = 0.0f;     // 0..1 lock on nearest noise enemy
        float aimLockStoerer = 0.0f;   // 0..1 lock on QSO Störer
        float aimLockPowerUp = 0.0f;   // 0..1 lock on nearest power-up
        // Morse signal info (simplified for thread safety)
        struct MorseSignalAudio {
            float pan;     // 0..1
            int volume;    // 0..40
            std::string pattern;  // The morse pattern (e.g., ".-" for A)
        };
        std::vector<MorseSignalAudio> morseSignals;
        
        // Noise enemy audio
        struct NoiseEnemyAudio {
            float pan;
            int volume;
            float intensity;
        };
        std::vector<NoiseEnemyAudio> noiseEnemies;
        
        // Weapon system sounds
        int weaponSwitchSoundFrames = 0;     // Generic weapon switch click
        int weaponEquipSoundFrames = 0;      // Weapon-specific equip sound
        WeaponType equippedWeaponType = WeaponType::NOISE_BLANKER;
        int noiseBlankerFireFrames = 0;      // Laser-like noise blanker shot
        int noiseHitSoundFrames = 0;         // Noise enemy hit impact
        int noiseHitVariation = 0;           // 0-3 variation for different pitch on multi-hits
        int noiseDestroyedFrames = 0;        // Noise enemy destroyed explosion
        int emergencyBrakeSoundFrames = 0;   // Tire screech emergency brake
        int aimResetSoundFrames = 0;         // Swoosh for aim re-center
        // Border collision sounds
        int borderWarningSoundFrames = 0;
        int borderScrapeSoundFrames = 0;
        int borderCrashSoundFrames = 0;
        float borderWarningSide = 0.5f;
        float borderCollisionSide = 0.5f;       // Pan for scrape/crash sounds (matches warning side)
        // Per-category volume factors (copied from config each frame)
        float warningVolume = 0.8f;
        float collisionVolume = 0.8f;
        float enemyVolume = 0.8f;
        float uiVolume = 0.7f;
        // Graduated border warning beep system
        float borderWarningBeepTimer = 0.0f;   // Timer for periodic beep (0..1)
        float borderWarningIntensity = 0.0f;   // 0=no warning, 1=max warning (near edge)
        bool borderWarningActive = false;       // Whether graduated warning is active
        // QSO Störer audio
        float qsoStoererPan = 0.5f;
        int qsoStoererVolume = 0;
        bool qsoStoererActive = false;
        int qsoStoererCollisionFrames = 0;
        float qsoStoererBuzzFreq = 0.0f;
        bool qsoStoererBehind = false;  // True when Störer is behind the player
        int qsoStoererOvertakeFrames = 0;  // Overtake transition sweep sound
        // Band crossing jingle
        int bandJingleFrames = 0;          // Remaining frames of band crossing jingle
        bool bandJingleAscending = true;   // true = entering band (ascending), false = leaving (descending)
        // Morse cannon audio frame counter for proper dot/dash durations
        int cannonDotFrames = 0;           // Remaining frames of dot tone
        int cannonDashFrames = 0;          // Remaining frames of dash tone
        // Power-up audio
        struct PowerUpAudio {
            float pan;          // Stereo position (0=left, 0.5=center, 1=right)
            int volume;         // 0..100
            bool inZone;        // Player is within this power-up's zone
            float zoneDepth;    // 0=edge of zone, 1=center of zone
            PowerUpType type;   // Which power-up type (for distinct cues)
        };
        std::vector<PowerUpAudio> powerUpZones;       // Zone ambient sounds for each power-up
        float powerUpCollectProgress = 0.0f;          // 0..1 collection progress (rising tone)
        bool powerUpCollecting = false;                // Currently holding both triggers on target
        int powerUpMissSoundFrames = 0;                // Missed power-up collection cue
        int powerUpActivateFrames = 0;                 // Activation fanfare frames
        PowerUpType powerUpActivateType = PowerUpType::SPEED_BOOST;  // Which type activated
        int powerUpExpireFrames = 0;                   // Expiration sound frames
        int powerUpExplodeFrames = 0;                  // Explosion sound frames
        float powerUpExplodePan = 0.5f;                // Pan of explosion
        float powerUpExplodeIntensity = 0.0f;          // 0..1 explosion strength
    };
    AudioParams audioParams;
    
    // Audio state
    float currentPan;                    // Current stereo pan (-1..+1)
    float motorFreqHz;                   // Motor sound frequency
    float swrFreqHz;                     // SWR indicator frequency
    float swrAlertPhase;                 // Phase for SWR alert beeping (0..2π)
    bool audioInitialized;
    
    // Game state
    std::atomic<GameState> currentState;
    GameConfig config;
    GameStats stats;
    std::vector<MeasurementPoint> measurementData;
    
    // Track and spatial audio
    std::vector<TrackPoint> track;
    std::unique_ptr<SpatialAudio> spatialAudio;
    std::unique_ptr<AntennaNetwork> antennaNetwork;
    float playerAngle;          // Current position on track (0-2π)
    float playerSpeed;          // Current speed (radians per second)
    float maxSpeed;             // Maximum speed
    float baseMaxSpeed;         // Base maximum speed (before matching factor)
    float acceleration;         // Acceleration rate
    float deceleration;         // Deceleration rate
    float brakingForce;         // Braking deceleration (stronger than coasting)
    float friction;             // Rolling friction coefficient
    float steeringSpeed;        // How fast player can steer
    float playerLateralOffset;  // Lateral offset from track center (-1..+1)
    bool isBraking;             // Currently braking (stick back while moving forward)
    float reverseHoldTime;      // How long stick has been held back from standstill
    float smoothedForwardInput = 0.0f; // Low-pass filtered analog stick Y axis
    
    // Speed in kHz/s conversion
    float kHzPerRadian;         // Conversion factor: kHz per radian of track
    float freqStepKHz;          // Frequency step between measurement points in kHz
    
    // Border collision system
    float trackBorderProximity;
    float crashRecoveryTime;
    bool borderVibrationActive;       // Tactile border warning currently vibrating
    float crashVibrationTimer;        // Countdown for collision rumble effect
    
    // Power-up system
    std::vector<PowerUp> powerUps;                // Active power-up zones on track
    std::vector<ActivePowerUp> activePowerUps;    // Currently active power-up effects
    float powerUpBaseDuration = 20.0f;            // Base duration for power-ups (seconds)
    float powerUpDurationBonus = 0.0f;            // Permanent bonus from DURATION_EXTEND (seconds)
    float nextPowerUpSpawnTime = 30.0f;           // When to spawn next power-up
    float powerUpCollectTimer = 0.0f;             // How long both triggers have been held on target
    float powerUpTriggerHoldTime = 0.0f;          // Debounce time with both triggers held
    uint32_t powerUpCollectTargetUid = 0;         // UID of power-up being collected (0 = none)
    uint32_t nextPowerUpUid = 1;                  // Counter for assigning unique IDs to power-ups
    bool prevBothTriggersHeld = false;            // Edge detection for dual-trigger
    float powerUpCollectCountdownTimer = 0.0f;    // Timer for TTS countdown during collection
    int powerUpCollectLastCountdown = -1;         // Last spoken countdown second
    float powerUpNoAimCooldown = 0.0f;            // Cooldown before repeating "not aimed" message
    float savedMaxSpeedBeforeBoost = 0.0f;        // Saved max speed before speed boost
    float savedCooldownBeforeBoost = 0.0f;        // Saved cooldown before fire rate boost
    bool autoFireActive = false;                  // Auto-fire mode currently active
    bool swrImmunityActive = false;               // SWR immunity currently active
    void updatePowerUps(float dt);                // Update power-up zones, timers, effects
    void spawnPowerUp();                          // Spawn a new random power-up ahead of player
    void activatePowerUp(PowerUpType type, int quality); // Apply power-up effect
    void deactivatePowerUp(size_t index);         // Remove power-up effect, restore state
    void handlePowerUpCollection(const GamepadState& input, float dt); // Dual-trigger collection logic
    void handlePowerUpExplosion(int powerUpIdx);   // Shoot a power-up — explosion + area damage
    void triggerPowerUpActivateSound(PowerUpType type);
    void triggerPowerUpExpireSound();
    void triggerPowerUpExplodeSound(float pan, float intensity);
    int findPowerUpByUid(uint32_t uid) const;     // Find power-up index by UID (-1 if not found)
    
    // QSO Störer
    QSOStoerer qsoStoerer;
    float nextQSOStoererSpawnTime;
    void updateQSOStoerer(float dt);
    void spawnQSOStoerer();
    void triggerQSOStoererCollisionSound();
    
    // Morse system
    std::unique_ptr<MorseDatabase> morseDatabase;
    std::unique_ptr<MorseSignalManager> morseSignalManager;
    std::unique_ptr<MorseCannon> morseCannon;
    int morseMissCount = 0;     // Tracks consecutive misses on current morse target
    float aimAngle;             // Aiming direction relative to player (radians)
    float aimSpeed;             // How fast player can aim
    bool aimSyncToHeading;      // true = weapon turret follows vehicle heading automatically
    float cachedAimLockPowerUp = 0.0f;  // Cached aim lock value for power-ups (from updateGameplay)
    std::vector<char> collectedChars;  // Characters collected this game
    bool hamSpiritBonusAchieved;       // HAMSPIRIT word collected?
    float nextMorseSpawnTime;          // When to spawn next morse signal
    float announceCooldown;             // Timer to debounce TTS announcements
    
    // Curve announcement system
    float curveAnnounceCooldown = 0.0f;   // Cooldown between curve announcements
    float lastAnnouncedCurveAngle = -1.0f; // Angle of the last announced curve (avoid re-announcing)
    void checkCurveAnnouncement(float dt); // Scan ahead for upcoming curves and announce them
    
    // Weapon system
    WeaponType currentWeapon;           // Currently selected weapon
    bool prevDpadLeft = false, prevDpadRight = false;  // Weapon switch edge detection
    float noiseBlankerCooldown;         // Cooldown between shots
    static constexpr float NOISE_BLANKER_COOLDOWN = 0.5f;  // 500ms between shots
    // Pending hit delay: sound of projectile impact is delayed based on distance to target
    float pendingHitTimer;              // Countdown until hit sound plays (0 = no pending hit)
    int pendingHitHealth;               // Remaining health to pass to triggerNoiseHitSound
    bool pendingHitDestroyed;           // Was the target destroyed by this hit?
    int pendingHitBonus;                // Score bonus for destroyed target
    bool pendingHitIsQso;               // Is this a QSO Störer hit (vs noise enemy)?
    float pendingHitQsoHealth;          // QSO Störer health after hit (for TTS)
    bool prevLeftTrigger = false;       // Edge detection for LT
    bool prevRightStickBtn = false;     // Edge detection for right stick click (aim reset)
    bool prevLeftStickBtn = false;      // Edge detection for left stick click (emergency brake)
    float emergencyBrakeTimer = 0.0f;   // >0 = emergency brake active, decelerating over time
    float emergencyBrakeStartSpeed = 0.0f; // Speed when emergency brake was activated
    
    // Noise enemy system
    std::vector<NoiseEnemy> noiseEnemies;
    float nextNoiseSpawnTime;           // When to spawn next noise enemy
    float nextNoiseAnnounceTime;        // Pre-schedule: when to announce upcoming noise
    float scheduledNoiseAngle;          // Pre-scheduled noise enemy angle
    bool noiseScheduled;                // Is a noise enemy pre-scheduled?
    float lastNoisePanDebugTime; // Timestamp of last noise pan debug log (seconds)
    float lastKeyboardUnknownKeyTime; // Timestamp of last unknown key log (seconds)
    float lastConsoleFocusLogTime; // Timestamp of last focus loss log (seconds)
    bool lastConsoleFocused; // Tracks last console focus state
    void updateNoiseEnemies(float dt);
    void spawnNoiseEnemy(float gameTime);
    void handleWeaponInput(const GamepadState& input, float dt);
    void handleNoiseBlankerFire();
    void triggerWeaponSwitchSound();
    void triggerWeaponEquipSound();
    void triggerNoiseBlankerFireSound();
    void triggerNoiseDestroyedSound();
    void triggerNoiseHitSound(int remainingHealth);
    void triggerEmergencyBrakeSound();
    void triggerAimResetSound();
    void announceUpcomingNoise(float freqMHz);  // "Bandwacht reports noise at X MHz"
    
    // PA damage tracking for staged warnings
    int lastAnnouncedDamageStage;       // Last PA damage stage announced (0-5)
    float paReflectedPowerAccum;        // Accumulated reflected power (Watts*seconds)
    float paThermalLoad;                // Thermal load on PA (0=cool, 1=critical)
    
    // Band tracking for border crossing announcements
    std::vector<AmateurBand> bandPlan;  // Loaded band plan
    std::string currentBandName;        // Name of current band (empty = out of band)
    float minTrackFreqHz = 0.0f;        // Minimum frequency in measurement data
    float maxTrackFreqHz = 0.0f;        // Maximum frequency in measurement data
    void checkBandCrossing();           // Check if player crossed a band boundary
    void loadBandPlan();                // Load band plan from file
    void triggerBandJingle(bool entering); // Play band crossing jingle
    
    // "Traffic report" system — periodic humorous SWR/impedance warnings
    float nextTrafficReportTime;        // When to play next traffic report (game time)
    void checkTrafficReport();          // Check if it's time for a traffic report
    void generateTrafficReport();       // Generate and speak a traffic report
    
    // Status readout — Back/Select/View button announces all current values
    bool prevBackBtn = false;           // Edge detection for Back button
    bool statusReadoutActive = false;   // True when status readout speech is ongoing
    void announceFullStatus();          // Speak all important current values
    void triggerStatusStartSound();     // Play status readout start chime
    void triggerStatusDoneSound();      // Play status readout completion chime
    
    // Braille update timer (member instead of static to properly reset)
    float brailleUpdateTimer = 0.0f;
    
    // Timing
    std::chrono::steady_clock::time_point gameStartTime;
    std::chrono::steady_clock::time_point lastUpdateTime;
    float deltaTime;
    
    // Game loop flag
    std::atomic<bool> shouldExit;
    std::vector<int> lastPressedKeys;  // Keys pressed last frame (for release)
    
    // Tuner button edge-detection state (member vars — properly reset on restart)
    bool prevTunerY = false, prevTunerX = false;
    bool prevTunerB = false, prevTunerA = false;
    bool prevTunerCheat = false;
    float tunerSoundCooldown = 0.0f;
    bool prevDpadUp = false, prevDpadDown = false;
    bool prevStartBtn = false;  // For pause/resume toggle edge detection
    // Config menu edge detection (member vars, NOT static)
    bool prevCfgUp = false, prevCfgDown = false;
    bool prevCfgLeft = false, prevCfgRight = false;
    bool prevCfgA = false, prevCfgB = false;
    // Main menu edge detection (member vars, NOT static)
    bool prevMainUp = false, prevMainDown = false, prevMainA = false;
    // Pause menu edge detection (member vars, NOT static)
    bool prevMenuUp = false, prevMenuDown = false, prevMenuA = false;
    // D-pad debounce timer: prevents rapid re-triggers from controller bounce
    float dpadDebounceTimer = 0.0f;
    static constexpr float DPAD_DEBOUNCE_TIME = 0.12f;  // 120ms debounce window
    static constexpr float STICK_MENU_DEADZONE = 0.5f;  // analog stick deadzone for menu nav
    // L/C button hold-to-repeat state
    float tunerHoldTimerY = 0.0f, tunerHoldTimerX = 0.0f;
    float tunerHoldTimerB = 0.0f, tunerHoldTimerA = 0.0f;
    static constexpr float TUNER_HOLD_INITIAL_DELAY = 0.4f;  // Hold for 400ms before repeat
    static constexpr float TUNER_HOLD_REPEAT_RATE = 0.08f;   // Repeat every 80ms
    
    // State management
    void setState(GameState newState);
    void updateGameState(float deltaTime);
    
    // State handlers
    void runIntroSequence();
    void runMainMenu();           // Main menu: New Game, Tutorial, Config, Exit
    void runControlsIntro();      // Controls explanation before starting
    void updatePlayingState(float deltaTime);
    void updatePausedState(float deltaTime);
    void updateTrackVisualization();  // Push track/object data to GUI overlay
    void showGameOver();
    void runTutorial();  // Interactive tutorial mode
    
    // Main menu
    enum class MainMenuOption {
        NEW_GAME,
        LEADERBOARD,
        TUTORIAL,
        SPEAKER_TEST,
        LEARN_SOUNDS,
        CONFIGURE,
        EXIT
    };
    MainMenuOption currentMainMenuOption;
    void speakMainMenuOption();
    void handleMainMenuInput(const GamepadState& input);
    
    // Input handling
    void handleInput();
    void pollKeyboard();
    bool waitForInput(float timeoutSeconds = 30.0f);
    GamepadState getCurrentInput();
    /// Get calibrated input for a specific player based on their input assignment.
    /// Applies stick drift calibration offsets. Used by the centralized
    /// gatherMultiplayerInput() so all players are processed identically.
    GamepadState getInputForPlayer(int playerIndex);
    /// Get the gamepad index assigned to a specific player (-1 if keyboard).
    int getPlayerGamepadIndex(int playerIndex) const;
    /// Get the gamepad index that should receive vibration for player 0.
    /// In multiplayer returns the assigned gamepad slot; in singleplayer returns 0.
    int getPlayer0GamepadIndex() const;
    /// Set vibration on the correct controller for a specific player.
    /// Routes through the player's assigned gamepad index.  Noop if the player
    /// uses keyboard or no gamepad is connected.
    void setVibrationForPlayer(int playerIndex, float leftMotor, float rightMotor);
    /// Stop vibration on ALL registered players' controllers.
    void stopAllVibration();
    
    // Menu system
    enum class MenuOption {
        RESUME,
        CONFIGURE,
        LEARN_SOUNDS,
        RESTART,
        MAIN_MENU
    };
    MenuOption currentMenuOption;
    bool inConfigMenu;  // true when in config submenu
    bool configCalledFromMainMenu;  // true when config was opened from main menu (not pause)
    bool inSoundLearning;  // true when in sound learning submenu
    bool soundLearningFromMainMenu;  // true when opened from main menu
    int soundLearningIndex;  // currently selected sound index
    float soundLearningVibTimer;  // countdown timer for demo vibration (non-blocking)
    void showPauseMenu();
    void speakCurrentMenuOption();
    void handleMenuInput(const GamepadState& input);
    void runSpeakerTest();  // Speaker test: Left, Right, Center
    void runSoundLearningMenu();  // Interactive sound learning submenu
    void speakCurrentSoundEntry();  // Speak name+description of current sound
    void handleSoundLearningInput(const GamepadState& input);  // Handle input in sound learning
    
    // Configuration menu — two-level structure
    // Level 0: Categories (Track, Controls, Audio, Assist, Back)
    // Level 1: Options within selected category
    enum class ConfigCategory {
        TRACK,       // Track Curve, Difficulty, Lap Count
        CONTROLS,    // Steering Sensitivity, Acceleration Sensitivity, Deadzone, Paddle Swap
        AUDIO,       // Individual volume controls for all sound elements
        VIBRATION,   // Vibration on/off and intensity
        SPEECH,      // TTS engine and speed
        ASSIST,      // Aim Assist, Traffic Reports
        WEAPONS,     // Noise Blanker on/off
        BRAILLE,     // Braille display settings (only when NVDA is active)
        STATUS_READOUT, // Select button verbosity settings
        KEY_MAPPING, // Keyboard remapping
    GAME_ELEMENTS, // Accessibility: toggle individual game elements on/off
    BACK
    };
    enum class ConfigOption {
        // Track category
        TRACK_CURVE,
        DIFFICULTY,
        LAP_COUNT,
        // Controls category
        STEERING_SENS,
        ACCEL_SENS,
        AIM_SENS,
        INPUT_DEADZONE,
        PADDLE_SWAP,
        // Audio category — individual volume controls
        MOTOR_VOLUME,
        SWR_VOLUME,
        MORSE_VOLUME,
        WARNING_VOLUME,
        COLLISION_VOLUME,
        ENEMY_VOLUME,
        UI_VOLUME,
        // Vibration category
        VIBRATION_ENABLED,
        VIBRATION_INTENSITY,
        // Speech category
        TTS_ENGINE,
        TTS_SPEED,
        TTS_VOICE,
        // Assist category
        AIM_ASSIST,
        TRAFFIC_REPORTS,
        EMERGENCY_BRAKE,
        NOISE_ALERTS,
        INTRUDER_MONITORING,
        BORDER_WARNING,        // Graduated border warning system
        CURVE_ANNOUNCEMENT,    // Announce upcoming curves (direction, severity, distance)
        CURVE_ANNOUNCE_DIST,   // Warning distance for curve announcements (kHz)
        // Weapons category
        NOISE_BLANKER,
        // Braille category
        BRAILLE_ENABLED,       // Master toggle: all braille output on/off
        BRAILLE_SPEED,
        BRAILLE_FREQ,
        BRAILLE_SWR,
        BRAILLE_PA,
        BRAILLE_SCORE,
        BRAILLE_LAP,
        BRAILLE_TUNER,
        // Status readout category (Select button verbosity)
        STATUS_SPEED,
        STATUS_FREQ,
        STATUS_SWR,
        STATUS_PA,
        STATUS_TUNER,
        STATUS_SCORE,
        STATUS_LAPS,
        STATUS_TIME,
        // Game Elements category (accessibility toggles)
        ELEM_MORSE_SIGNALS,
        ELEM_SWR_DAMAGE,
        ELEM_NOISE_ENEMIES,
        ELEM_QSO_STOERER,
        ELEM_POWER_UPS,
        ELEM_AUTO_STEERING,
        ELEM_AUTO_AIM,
        MORSE_DIFFICULTY,      // Morse character difficulty (1-5, independent of game difficulty)
        // Key mapping category
        REMAP_KEYBOARD,        // Opens keyboard remapping dialog
        REMAP_CONTROLLER,      // Opens controller remapping dialog
        CONTROLLER_PRESET,     // Controller preset selection (Xbox, PS4, Auto-detect)
        CALIBRATE_CONTROLLER,  // Opens TTS-guided stick drift calibration wizard
        // Shared
        SUB_BACK     // Return to category menu
    };
    ConfigCategory currentConfigCategory;
    ConfigOption currentConfigOption;
    bool inConfigSubMenu;  // true = showing options within a category; false = showing categories
    void showConfigMenu();
    void speakCurrentConfigCategory();
    void speakCurrentConfigOption();
    void handleConfigInput(const GamepadState& input);
    // Get the options for a given category
    std::vector<ConfigOption> getOptionsForCategory(ConfigCategory cat) const;
    int currentSubOptionIndex;  // Index into current category's option list
    void runKeyRemappingDialog();  // Interactive keyboard remapping dialog
    void runControllerRemappingDialog(); // Interactive controller button remapping dialog
    void runControllerCalibration();     // TTS-guided stick drift calibration wizard
    void applyKeyMapping();       // Apply current key mapping to keyboard emulator
    
    // Helper methods
    void speakTranslated(const std::string& key, const std::string& fallback, bool interrupt = false);
    void speakText(const std::string& text, bool interrupt = false);
    bool shouldInterruptTts(bool requested) const;
    void offerNvdaControllerDownload();
    void log(const std::string& category, const std::string& message);
    std::string getTrackCurveName(TrackCurve curve) const;
    std::string getDataSourceName(bool useAll) const;
    void autoSelectUnUnRatio();
    void logMatchingFeasibility();
#ifndef NDEBUG
    static constexpr bool ENABLE_TUNER_CHEAT = true;
#else
    static constexpr bool ENABLE_TUNER_CHEAT = false;
#endif
    
    // Track gameplay methods
    void updatePlayerPosition(float dt);
    void updatePlayerSpeed(float dt, const GamepadState& input);
    void updateSpatialAudio();
    void generateAndPlayAudio();
    void initAudio();
    void shutdownAudio();
    void abortAllAudioBackends();  // Abort game + multiplayer backends
    float getCurrentSWR() const;
    void checkPAHealth(float dt);
    void triggerAdjustmentSound(bool ascending);  // Play short ascending/descending tone
    void triggerBumperSound();  // Play "hit the wall" bumper sound
    void triggerCollectSound();  // Play collection success chime
    void triggerMissAimSound();  // Play correct morse but wrong aim
    void triggerMissMorseSound();  // Play wrong morse
    void triggerMenuNavSound();   // Play menu navigation beep
    void triggerMenuSelectSound();  // Play menu selection beep
    void triggerKeyClickSound();    // Play keyboard/text input click
    void triggerPauseSound();     // Play pause sound
    void triggerUnpauseSound();   // Play resume sound
    void triggerPaDamageSound();  // Play PA damage crackle/pop
    void triggerPaRepairSound();  // Play PA repair chime
    enum class TunerParam { L, C, UNUN };
    void announceTunerParam(TunerParam param); // Announce only the changed parameter
    void applyPerfectTunerCheat();
    void restartGame();  // Reset game state for replay
    void saveGameConfig();  // Save config to config/hamspirit.cfg
    void loadGameConfig();  // Load config from config/hamspirit.cfg
    
    // Voice selection
    std::vector<std::string> availableVoices;  // Cached available voices
    int currentVoiceIndex;                      // Index into availableVoices
    void refreshAvailableVoices();              // Refresh voice list from TTS engine
    
    // High score / leaderboard
    std::vector<HighScoreEntry> highScores;
    std::string currentPlayerCallsign;
    std::string currentPlayerName;
    void loadHighScores();
    void saveHighScores();
    void addHighScoreEntry();
    void showLeaderboard();
    std::string promptTextInput(const std::string& promptMsg, int maxLen = 20);
    
    // NATO / amateur radio phonetic alphabet
    std::string callsignToPhonetic(const std::string& callsign);
    
    // Braille output for NVDA
    void updateBrailleDisplay(const std::string& text);
    std::string buildGameplayBrailleString() const;  // Build localized braille status string
    
    // Antenna network methods
    void handleAntennaNetworkInput(const GamepadState& input, float dt);
    void updateMaxSpeedFromMatching();
    void announceMatchingStatus();
    
    // All-curves mode tracking
    int allCurvesCurrentSection;    // Which curve section we're currently in (0-4)
    std::vector<std::vector<TrackPoint>> allCurvesTracks; // One track per curve
    
    // Morse system methods
    void updateMorseSystem(float dt);
    void handleMorseCannonInput(const GamepadState& input, float dt);
    void handleAimingInput(const GamepadState& input, float dt);
    void spawnMorseSignals(float gameTime);
    void checkMorseCollection(char sentChar);
    void checkHamSpiritBonus();
    
    // Multiplayer-aware interaction methods (playerIndex -1 or 0 = player 0 / singleplayer)
    void checkMorseCollectionForPlayer(char sentChar, int playerIndex);
    void handleNoiseBlankerFireForPlayer(int playerIndex);
    void handlePowerUpCollectionForPlayer(const GamepadState& input, float dt, int playerIndex);
    void addHighScoreEntryForPlayer(int playerIndex);
    
    // ── Multiplayer support ──────────────────────────────────────────────
    std::unique_ptr<MultiplayerManager> multiplayerMgr;
    MultiplayerConfig multiplayerConfig;
    
    // ── Server-authoritative architecture ─────────────────────────────────
    /// Central authoritative game instance that manages all world state.
    /// All players (including the former "Player 0") interact with the world
    /// exclusively through this authority via ActionRequests/WorldEvents.
    /// No player has special privileges — the authority treats all identically.
    std::unique_ptr<GameAuthority> gameAuthority;
    /// Per-player MorseCannon instances for proper iambic paddle behaviour
    /// on secondary players (index 1..MAX_PLAYERS-1).  Player 0 uses the
    /// existing `morseCannon` member.
    std::unique_ptr<MorseCannon> playerMorseCannons[MAX_PLAYERS];
    /// Shared global time reference for morse pattern phase so that all
    /// players hear world morse signals at the same point in their pattern.
    float globalMorsePatternPhase = 0.0f;

    /// Run the multiplayer setup menu (called after selecting "New Game" → "Multiplayer").
    /// Allows assigning controllers, audio devices, callsigns/names, and split orientation.
    void runMultiplayerSetupMenu();

    /// Run the game mode selection submenu (Singleplayer / Multiplayer).
    /// Returns true if user chose multiplayer, false for singleplayer.
    int showGameModeSelectionMenu();  // Returns -1=back, 0=singleplayer, 1=multiplayer

    /// Update all multiplayer players (spatial audio, collisions, per-player tuning).
    void updateMultiplayerState(float dt);
    
    /// Gather input from all multiplayer players (1+) and send INPUT_UPDATE actions
    /// to the GameAuthority. Called BEFORE tick() so all players' input is processed
    /// in the same physics step.
    void gatherMultiplayerInput(float dt);

    /// Compute per-player audio params for ANY player based on their individual
    /// position, speed, SWR, and proximity to game objects.
    /// Called each frame from updateMultiplayerState() for ALL players in
    /// multiplayer mode, feeding each player's independent audio pipeline.
    void computePlayerAudioParams(PlayerContext& ctx);

    /// Render collision sound for a player (metallic crunch/impact effect).
    /// @param buffer     Stereo audio buffer to mix into
    /// @param samples    Samples per channel
    /// @param sampleRate Sample rate (Hz)
    /// @param intensity  Collision intensity (0..1)
    /// @param pan        Stereo pan (-1..+1)
    /// @param frames     Remaining frames counter (decremented)
    void renderCollisionSound(std::vector<int16_t>& buffer, int samples,
                              int sampleRate, float intensity, float pan,
                              int& frames);

    /// Render another player's engine sound as heard by a listener (with Doppler).
    /// @param buffer        Output stereo buffer to mix into
    /// @param samples       Samples per channel
    /// @param sampleRate    Sample rate
    /// @param relation      Spatial relation from listener to source
    /// @param sourceSpeed   Source player speed (rad/s)
    /// @param baseMotorFreq Base motor frequency (Hz)
    void renderOtherPlayerEngine(std::vector<int16_t>& buffer, int samples,
                                 int sampleRate,
                                 const SpatialRelation& relation,
                                 float sourceSpeed, float baseMotorFreq,
                                 int sourceIndex);
};

/**
 * Launch Ham Spirit game from acoustic analyzer
 * @param measurements VNA measurement data
 * @param analyzer Reference to acoustic analyzer
 * @param translation Translation manager
 * @param logger Logger
 * @param consoleInput Console input for keyboard reading (optional)
 * @return true if game was successfully launched and completed
 */
bool launchGame(
    const std::vector<MeasurementPoint>& measurements,
    AcousticAnalyzer* analyzer,
    TranslationManager* translation,
    Logger* logger,
    IConsoleInput* consoleInput = nullptr
);

} // namespace HamSpirit

#endif // WITH_HAM_SPIRIT
