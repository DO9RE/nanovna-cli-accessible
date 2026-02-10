#pragma once
#include "measurement.h"
#include "audio_engine_interface.h"
#include "logger.h"
#include "translation.h"
#include "config.h"  // For SmithNoiseType enum
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <cstdint>

/**
 * Smith Diagram Visualization Modes
 * 
 * Different approaches to acoustically visualize Smith diagrams:
 * - CARTESIAN: Position in 3D space (Re(Γ) → L/R, Im(Γ) → F/B)
 * - POLAR: Rotation around user (∠Γ → angle, |Γ| → distance)
 * - IMPEDANCE_DIRECT: R → L/R, X → F/B (simpler)
 * - SWR_CIRCLES: Focus on constant SWR circles
 * - TIME_DOMAIN_CUES: Time-sweep with Smith spatial cues
 * - HYBRID_MULTI: Multiple simultaneous layers
 */
enum class SmithVisualizationMode {
    CARTESIAN,           // Re(Γ) → X, Im(Γ) → Y
    POLAR,              // |Γ| → Distance, ∠Γ → Angle
    IMPEDANCE_DIRECT,   // R → X, X → Y (Alternative)
    SWR_CIRCLES,        // Constant SWR circles navigation
    TIME_DOMAIN_CUES,   // Zeitdomänen-Sweep with Smith-Cues
    HYBRID_MULTI        // Multi-layer audio (combines multiple)
};

/**
 * Audio hardware capability detection
 */
enum class AudioCapability {
    STEREO_ONLY,        // 2.0 stereo
    SURROUND_5_1,       // 5.1 surround
    SURROUND_7_1,       // 7.1 surround
    SURROUND_ATMOS      // Dolby Atmos with height
};

/**
 * Smith Diagram Position in 3D space
 */
struct SmithPosition3D {
    double x;           // Horizontal position (-1 to +1)
    double y;           // Vertical position (-1 to +1)
    double z;           // Depth position (optional, 0 for 2D)
    double pitch;       // Pitch in Hz
    double volume;      // Volume (0.0 to 1.0)
};

/**
 * Smith Diagram Polar Position
 */
struct SmithPositionPolar {
    double angle;       // Angle in degrees (-180 to +180)
    double radius;      // Radius (0.0 to 1.0)
    double pitch;       // Pitch in Hz
    double volume;      // Volume (0.0 to 1.0)
};

/**
 * Multi-channel audio gains for 7.1 surround
 * Channel order: FL, FR, FC, LFE, BL, BR, SL, SR
 */
struct MultiChannelGains {
    float frontLeft;
    float frontRight;
    float frontCenter;
    float lfe;          // Low Frequency Effects (subwoofer)
    float backLeft;
    float backRight;
    float sideLeft;
    float sideRight;
    
    MultiChannelGains() : frontLeft(0), frontRight(0), frontCenter(0), lfe(0),
                         backLeft(0), backRight(0), sideLeft(0), sideRight(0) {}
};

/**
 * Smith Chart Visualizer
 * 
 * Provides acoustic 3D visualization of Smith diagrams for blind users.
 * Supports multiple visualization modes and audio configurations.
 */
class SmithVisualizer {
public:
    // Marker types
    enum class MarkerType {
        NONE,
        X_AXIS_CROSS,       // Im(Γ) = 0 (resistive)
        Y_AXIS_CROSS,       // Re(Γ) = 0
        CENTER_REACHED,     // |Γ| < 0.2 (good match)
        EDGE_WARNING,       // |Γ| > 0.8 (poor match)
        RESONANCE,          // Local SWR minimum
        QUADRANT_CHANGE     // Smith quadrant changed
    };
    
    SmithVisualizer(Logger* logger, TranslationManager* translation);
    ~SmithVisualizer();
    
    // Mode control
    void setMode(SmithVisualizationMode mode);
    SmithVisualizationMode getMode() const { return currentMode; }
    
    // Hardware capability
    void setAudioCapability(AudioCapability cap);
    AudioCapability getAudioCapability() const { return audioCapability; }
    AudioCapability detectAudioCapability();  // Auto-detect hardware
    
    // Audio engine
    void setAudioEngine(std::shared_ptr<IAudioEngine> engine);
    std::shared_ptr<IAudioEngine> getAudioEngine() const { return audioEngine; }
    
    // Set loggers for debug output
    void setMathLogger(MathLogger* mathLog) { mathLogger = mathLog; }
    
    // Surround configuration
    void setSurroundConfig(int frontDist, int backDist, int sideDist, int centerStrength, 
                          int fbSeparation, int sideEmphasis, AppConfig::SurroundFadingCurve curve);
    
    // Enable/disable Smith visualization
    void setEnabled(bool enabled) { smithEnabled = enabled; }
    bool isEnabled() const { return smithEnabled; }
    
    // Smith cues configuration (for TIME_DOMAIN_CUES mode)
    void setSmithCuesEnabled(bool enabled) { smithCuesEnabled = enabled; }
    bool isSmithCuesEnabled() const { return smithCuesEnabled; }
    void setSmithCuesVolume(int volumePercent);
    int getSmithCuesVolume() const { return smithCuesVolume; }
    
    // Noise type configuration
    void setNoiseType(AppConfig::SmithNoiseType type) { noiseType = type; }
    AppConfig::SmithNoiseType getNoiseType() const { return noiseType; }
    
    // Marker sounds configuration
    void setMarkersEnabled(bool enabled) { markersEnabled = enabled; }
    bool isMarkersEnabled() const { return markersEnabled; }
    
    // Center pulse (reference signal) configuration
    void setCenterPulseEnabled(bool enabled) { centerPulseEnabled = enabled; }
    bool isCenterPulseEnabled() const { return centerPulseEnabled; }
    void setCenterPulseVolume(int volumePercent);
    int getCenterPulseVolume() const { return centerPulseVolume; }
    void setCenterPulseInterval(double intervalSeconds);  // Pulse interval in seconds (0.5-2.0)
    double getCenterPulseInterval() const { return centerPulseIntervalSeconds; }
    void setCenterPulseWaveform(AppConfig::CenterPulseWaveform waveform) { centerPulseWaveform = waveform; }
    AppConfig::CenterPulseWaveform getCenterPulseWaveform() const { return centerPulseWaveform; }
    
    // Axis crossing events configuration
    void setAxisEventsEnabled(bool enabled) { axisEventsEnabled = enabled; }
    bool isAxisEventsEnabled() const { return axisEventsEnabled; }
    void setAxisEventsVolume(int volumePercent);
    int getAxisEventsVolume() const { return axisEventsVolume; }
    void setAxisEventsPitchRange(double minHz, double maxHz);  // Pitch range for direction gestures
    double getAxisEventsPitchMin() const { return axisEventsPitchMinHz; }
    double getAxisEventsPitchMax() const { return axisEventsPitchMaxHz; }
    void setAxisCrossingSound(AppConfig::AxisCrossingSound sound) { axisCrossingSound = sound; }
    AppConfig::AxisCrossingSound getAxisCrossingSound() const { return axisCrossingSound; }
    void setAxisEventsDuration(int durationMs);  // Duration of axis crossing sounds (50-500ms)
    int getAxisEventsDuration() const { return axisEventsDurationMs; }
    
    // Coordinate calculations
    SmithPosition3D calculateCartesianPosition(const MeasurementPoint& pt) const;
    SmithPositionPolar calculatePolarPosition(const MeasurementPoint& pt) const;
    SmithPosition3D calculateImpedanceDirectPosition(const MeasurementPoint& pt) const;
    SmithPosition3D calculateSwrCirclesPosition(const MeasurementPoint& pt) const;
    SmithPosition3D calculateTimeDomainCuesPosition(const MeasurementPoint& pt, double freqPosition) const;
    SmithPosition3D calculateHybridMultiPosition(const MeasurementPoint& pt) const;
    
    // Multi-channel panning
    MultiChannelGains calculateCartesianPanning(double reGamma, double imGamma) const;
    MultiChannelGains calculatePolarPanning(double angleDeg, double radius) const;
    MultiChannelGains calculateImpedancePanning(double R, double X, double Z0 = 50.0) const;
    
    // Convert multi-channel to stereo for fallback
    void multiChannelToStereo(const MultiChannelGains& mc, float& left, float& right) const;
    
    // Generate Smith-related audio (ambient cues, markers, etc.)
    void generateSmithAmbientAudio(const MeasurementPoint& pt, 
                                   std::vector<int16_t>& buffer, 
                                   int samples) const;
    
    // Center pulse generation
    void generateCenterPulse(std::vector<int16_t>& buffer, int samples, double deltaTimeSeconds);
    
    // Axis crossing event detection and sound generation
    bool detectAxisCrossing(const MeasurementPoint& pt, const MeasurementPoint& prevPt,
                           bool& isHorizontal, bool& isUpward);
    
    // Check for axis crossings in full dataset between two positions (for skipped points)
    bool detectAxisCrossingInRange(const std::vector<MeasurementPoint>& fullData,
                                   size_t startPos, size_t endPos,
                                   bool& isHorizontal, bool& isUpward, size_t& crossingPos);
    
    void generateAxisEventSound(std::vector<int16_t>& buffer, int samples,
                              bool isHorizontal, bool isUpward, double posX, double posY) const;
    
    // Marker detection and playback
    bool shouldPlayMarker(const MeasurementPoint& pt, const MeasurementPoint& prevPt,
                         MarkerType& outType);
    void generateMarkerSound(MarkerType type, std::vector<int16_t>& buffer, int samples) const;
    
    // Get translated mode name
    std::string getModeName(SmithVisualizationMode mode) const;
    std::string getCurrentModeName() const;
    
    // Configuration limits
    static constexpr int MIN_SMITH_CUES_VOLUME = 10;
    static constexpr int MAX_SMITH_CUES_VOLUME = 100;
    static constexpr int DEFAULT_SMITH_CUES_VOLUME = 30;
    
    static constexpr int MIN_CENTER_PULSE_VOLUME = 10;
    static constexpr int MAX_CENTER_PULSE_VOLUME = 100;
    static constexpr int DEFAULT_CENTER_PULSE_VOLUME = 40;
    
    static constexpr int MIN_AXIS_EVENTS_VOLUME = 10;
    static constexpr int MAX_AXIS_EVENTS_VOLUME = 100;
    static constexpr int DEFAULT_AXIS_EVENTS_VOLUME = 60;
    
    static constexpr int MIN_AXIS_EVENTS_DURATION_MS = 50;
    static constexpr int MAX_AXIS_EVENTS_DURATION_MS = 500;
    static constexpr int DEFAULT_AXIS_EVENTS_DURATION_MS = 100;
    
    // Audio generation constants
    static constexpr double CENTER_PULSE_DURATION_SECONDS = 0.03;  // 30ms pulse duration
    static constexpr double AXIS_CROSSING_THRESHOLD = 0.02;        // Debounce threshold for axis crossing detection
    
private:
    Logger* logger;
    TranslationManager* translation;
    MathLogger* mathLogger = nullptr;
    
    // Current configuration
    SmithVisualizationMode currentMode;
    AudioCapability audioCapability;
    std::shared_ptr<IAudioEngine> audioEngine;
    
    // Surround configuration
    struct SurroundConfig {
        int frontDistance = 100;
        int backDistance = 100;
        int sideDistance = 100;
        int centerStrength = 50;
        int fbSeparation = 100;
        int sideEmphasis = 100;
        AppConfig::SurroundFadingCurve fadingCurve = AppConfig::SurroundFadingCurve::LINEAR;
    } surroundConfig;
    
    // State flags
    std::atomic<bool> smithEnabled;
    std::atomic<bool> smithCuesEnabled;
    std::atomic<bool> markersEnabled;
    std::atomic<int> smithCuesVolume;
    AppConfig::SmithNoiseType noiseType;  // Noise type for ambient audio
    
    // Center pulse (reference signal) state
    std::atomic<bool> centerPulseEnabled;
    std::atomic<int> centerPulseVolume;
    double centerPulseIntervalSeconds;  // Pulse interval (0.5-2.0 seconds)
    mutable double centerPulsePhase;    // Phase accumulator for pulse timing
    AppConfig::CenterPulseWaveform centerPulseWaveform;  // Waveform type for center pulse
    
    // Axis crossing events state
    std::atomic<bool> axisEventsEnabled;
    std::atomic<int> axisEventsVolume;
    std::atomic<int> axisEventsDurationMs;  // Duration of axis crossing sounds (50-500ms)
    double axisEventsPitchMinHz;       // Minimum pitch for gestures (default 300 Hz)
    double axisEventsPitchMaxHz;       // Maximum pitch for gestures (default 800 Hz)
    mutable double lastS11Im;          // Last imaginary component for crossing detection
    mutable double lastS11Re;          // Last real component for crossing detection
    mutable bool lastCrossingValid;    // Whether we have valid previous data
    mutable size_t lastCheckedPosition;  // Last position checked for crossings (to detect skipped points)
    AppConfig::AxisCrossingSound axisCrossingSound;  // Sound type for axis crossing events
    
    // Helper functions
    double calculateGammaMagnitude(const MeasurementPoint& pt) const;
    double calculateGammaPhase(const MeasurementPoint& pt) const;
    double normalizeImpedance(double value, double Z0, double maxValue) const;
    
    // VBAP (Vector Base Amplitude Panning) for 7.1
    void calculateVBAP7_1(double x, double y, MultiChannelGains& gains) const;
    void calculateVBAP5_1(double x, double y, MultiChannelGains& gains) const;
    
    // Quadrant tracking for marker detection
    mutable std::atomic<int> lastQuadrant;
    
    std::mutex stateMutex;
};
