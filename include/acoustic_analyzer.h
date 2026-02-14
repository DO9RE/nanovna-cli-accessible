#pragma once
#include "measurement.h"
#include "audio.h"
#include "audio_engine_interface.h"
#include "logger.h"
#include "math_logger.h"
#include "translation.h"
#include "smith_visualizer.h"
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <memory>
#include <functional>

// Forward declaration to avoid circular dependency
class IAudioBackend;

enum class PlaybackState {
    STOPPED,    // Not playing
    PLAYING,    // Playing through sequence
    PAUSED,     // Hard pause - no audio
    FROZEN      // Freeze - static audio at current position
};

// Individual curve that can be enabled/disabled
struct AcousticCurve {
    bool enabled = false;
    std::string name;
};

class AcousticAnalyzer {
public:
    AcousticAnalyzer(Logger* logger, MathLogger* mathLogger, TranslationManager* translation);
    ~AcousticAnalyzer() noexcept;
    
    // Set output callback for routing text output through a centralized print wrapper
    void setOutputCallback(std::function<void(const std::string&)> callback);
    
    // Set measurement data
    void setData(const std::vector<MeasurementPoint>& data);
    void updateData(const std::vector<MeasurementPoint>& data);  // Update data while playing (for continuous sweep)
    void updatePointsByFrequency(const std::vector<MeasurementPoint>& newData);  // Update specific points by frequency matching
    
    // Audio engine management
    void setAudioEngine(std::shared_ptr<IAudioEngine> engine);
    std::shared_ptr<IAudioEngine> getAudioEngine() const { return audioEngine; }
    
    // Playback control
    void play();
    void pause();       // Hard pause - silence
    void freeze();      // Freeze at current position with static audio
    void stop();
    
    // Navigation
    void movePosition(int delta);  // Move by delta points (can be negative)
    int movePositionWithBoundaryCheck(int delta);  // Move by delta with loop boundary check, returns actual adjusted delta used
    void setPosition(size_t pos);  // Jump to specific position
    size_t getPosition() const { return currentPos; }
    
    // Loop control
    void setLoopLeft(size_t pos);
    void setLoopRight(size_t pos);
    void toggleLoop();
    void toggleContinuousReplay();
    void toggleLoopZoom();  // Toggle loop zoom mode
    void toggleLoopInvert();  // Toggle loop invert mode (play outside loop markers)
    bool isLoopEnabled() const { return loopEnabled; }
    bool isContinuousReplay() const { return continuousReplay; }
    bool isLoopZoomEnabled() const { return loopZoomEnabled; }
    bool isLoopInverted() const { return loopInverted; }
    void setLoopPauseMs(int pauseMs);  // Set loop pause duration (0-5000ms)
    int getLoopPauseMs() const { return loopPauseMs; }
    void setInvertedLoopGapMs(int gapMs);  // Set inverted loop gap duration (0-5000ms)
    int getInvertedLoopGapMs() const { return invertedLoopGapMs; }
    
    // Curve control
    void toggleCurve(int curveIndex);  // 0-4: SWR, RL, |Z|, X, Phase
    bool isCurveEnabled(int curveIndex) const;
    void setCurveVolume(int curveIndex, int volumePercent);  // 0-200%
    int getCurveVolume(int curveIndex) const;
    
    // Master volume control
    void setMasterVolume(int volumePercent);  // 0-100%
    int getMasterVolume() const;
    
    // Playback mode control
    void setSmoothMode(bool smooth);
    bool isSmoothMode() const { return smoothMode; }
    void setPlaybackTimeSeconds(int seconds);  // Time in seconds for complete sweep (1-60)
    int getPlaybackTimeSeconds() const { return playbackTimeSeconds; }
    
    // Frequency range control for synthesizer
    void setFrequencyRange(int minHz, int maxHz);  // Set frequency range (100-20000 Hz)
    int getMinFrequencyHz() const { return minFreqHz; }
    int getMaxFrequencyHz() const { return maxFreqHz; }
    
    // Dotted mode control
    void setDottedDurationMs(int durationMs);  // Set dot duration (30-500ms)
    int getDottedDurationMs() const { return dottedDurationMs; }
    void setDottedPauseMs(int pauseMs);  // Set dot pause duration (10-500ms)
    int getDottedPauseMs() const { return dottedPauseMs; }
    void setFreezePointPauseMs(int pauseMs);  // Set freeze point pause duration (50-2000ms)
    int getFreezePointPauseMs() const { return freezePointPauseMs; }
    
    // Get measurement at current position
    const MeasurementPoint* getCurrentMeasurement() const;
    
    // State queries
    PlaybackState getState() const { return state; }
    bool hasData() const { return !measurementData.empty(); }
    size_t getDataSize() const { return measurementData.size(); }
    
    // Get loop markers
    size_t getLoopLeft() const { return loopLeft; }
    size_t getLoopRight() const { return loopRight; }
    
    // Get current skip factor (how many points are being skipped)
    int getCurrentSkipFactor() const;
    
    // Get skip factor using dotted mode logic (for braille export consistency)
    int getDottedModeSkipFactor() const;
    
    // Get translated curve name
    std::string getCurveName(int curveIndex) const;
    
    // Smith Chart Visualization
    SmithVisualizer* getSmithVisualizer() { return smithVisualizer.get(); }
    const SmithVisualizer* getSmithVisualizer() const { return smithVisualizer.get(); }
    void enableSmithVisualization(bool enable);
    bool isSmithVisualizationEnabled() const;
    
    // Y-axis ruler - plays ascending tones to indicate Y-axis scale
    void playYAxisRuler();
    void stopYAxisRuler();  // Stop ruler playback
    bool isRulerPlaying() const { return rulerPlaying.load(); }
    
    // X-axis ruler - plays blips at each real measurement point during playback
    void toggleXAxisRuler();  // Toggle X-axis ruler on/off
    bool isXAxisRulerEnabled() const { return xAxisRulerEnabled.load(); }
    void setXAxisRulerVolume(int volumePercent);  // 0-100%
    int getXAxisRulerVolume() const;
    void setXAxisRulerBlipDuration(int durationMs);  // 30-200ms for X-axis blip
    int getXAxisRulerBlipDuration() const;
    
    // X-axis ruler sound configuration
    enum class XAxisRulerNoiseType {
        WHITE_NOISE = 0,      // White noise (default)
        PINK_NOISE = 1,       // Pink noise (filtered)
        CLICK = 2             // Short click
    };
    void setXAxisRulerNoiseType(XAxisRulerNoiseType type) { xAxisRulerNoiseType = type; }
    XAxisRulerNoiseType getXAxisRulerNoiseType() const { return xAxisRulerNoiseType; }
    
    void setXAxisRulerMidiDrum(int drumNote);  // MIDI drum note (35-81)
    int getXAxisRulerMidiDrum() const;
    
    // Status line - displays current playback information
    void toggleStatusLine();  // Toggle status line on/off
    bool isStatusLineEnabled() const { return statusLineEnabled.load(); }
    enum class StatusLineContent {
        POSITION = 0,      // Show position
        FREQUENCY = 1,     // Show frequency
        SWR = 2,           // Show SWR value
        ALL = 3            // Show all information
    };
    void setStatusLineContent(StatusLineContent content) { statusLineContent = content; }
    StatusLineContent getStatusLineContent() const { return statusLineContent; }
    
    // Individual status line parameter toggles
    void setStatusLineShowPosition(bool show) { statusLineShowPosition = show; }
    bool getStatusLineShowPosition() const { return statusLineShowPosition; }
    void setStatusLineShowFrequency(bool show) { statusLineShowFrequency = show; }
    bool getStatusLineShowFrequency() const { return statusLineShowFrequency; }
    void setStatusLineShowSWR(bool show) { statusLineShowSWR = show; }
    bool getStatusLineShowSWR() const { return statusLineShowSWR; }
    void setStatusLineShowRL(bool show) { statusLineShowRL = show; }
    bool getStatusLineShowRL() const { return statusLineShowRL; }
    void setStatusLineShowImpedance(bool show) { statusLineShowImpedance = show; }
    bool getStatusLineShowImpedance() const { return statusLineShowImpedance; }
    void setStatusLineShowReactance(bool show) { statusLineShowReactance = show; }
    bool getStatusLineShowReactance() const { return statusLineShowReactance; }
    void setStatusLineShowPhase(bool show) { statusLineShowPhase = show; }
    bool getStatusLineShowPhase() const { return statusLineShowPhase; }
    
    std::string getStatusLineText() const;  // Get current status line text
    
    // Ruler volume control
    void setRulerVolume(int volumePercent);  // 0-100%
    int getRulerVolume() const;
    
    // Ruler blip duration control
    void setRulerBlipDuration(int durationMs);  // 30-500ms for shortest blip
    int getRulerBlipDuration() const;
    
    // Ruler lengthening factor control
    void setRulerLengtheningFactor(int percentFactor);  // 100-500% for tone lengthening
    int getRulerLengtheningFactor() const;
    
    // Ruler sound mode configuration
    enum class RulerSoundMode {
        FOLLOW_LAST_CURVE = 0,
        CUSTOM_SOUND = 1
    };
    void setRulerSoundMode(RulerSoundMode mode) { rulerSoundMode = mode; }
    RulerSoundMode getRulerSoundMode() const { return rulerSoundMode; }
    void setRulerCustomSoundSynth(int waveformIndex) { rulerCustomSoundSynth = waveformIndex; }
    void setRulerCustomSoundMidiGliding(int instrument) { rulerCustomSoundMidiGliding = instrument; }
    void setRulerCustomSoundMidiDotted(int instrument) { rulerCustomSoundMidiDotted = instrument; }
    int getRulerCustomSoundSynth() const { return rulerCustomSoundSynth; }
    int getRulerCustomSoundMidiGliding() const { return rulerCustomSoundMidiGliding; }
    int getRulerCustomSoundMidiDotted() const { return rulerCustomSoundMidiDotted; }

private:
    Logger* logger;
    MathLogger* mathLogger;
    TranslationManager* translation;
    std::function<void(const std::string&)> outputCallback;
    std::vector<MeasurementPoint> measurementData;
    
    // Audio engine (can be SynthesizerEngine or MIDIEngine)
    std::shared_ptr<IAudioEngine> audioEngine;
    
    // Smith Chart Visualizer
    std::unique_ptr<SmithVisualizer> smithVisualizer;
    
    // Playback state
    std::atomic<PlaybackState> state;
    std::atomic<size_t> currentPos;
    
    // Loop state
    std::atomic<size_t> loopLeft;
    std::atomic<size_t> loopRight;
    std::atomic<bool> loopEnabled;
    std::atomic<bool> continuousReplay;
    std::atomic<bool> loopZoomEnabled;  // When true, centers loop in stereo field and applies full time to loop
    std::atomic<bool> loopInverted;  // When true, plays outside loop markers instead of inside
    std::atomic<int> loopPauseMs;  // Pause duration before loop repeats (0-5000ms)
    std::atomic<int> invertedLoopGapMs;  // Duration of silent gap when skipping inverted loop section (0-5000ms)
    
    // Playback settings
    std::atomic<bool> smoothMode;
    std::atomic<int> playbackTimeSeconds;  // Total time in seconds for complete sweep
    std::atomic<int> minFreqHz;  // Minimum frequency for synthesizer (Hz)
    std::atomic<int> maxFreqHz;  // Maximum frequency for synthesizer (Hz)
    std::atomic<int> dottedDurationMs;  // Duration of each dot in dotted mode (ms)
    std::atomic<int> dottedPauseMs;      // Pause duration between dots in dotted mode (ms)
    std::atomic<int> freezePointPauseMs; // Pause duration between repeated points in freeze mode (ms)
    
    // Curves (0:SWR, 1:RL, 2:|Z|, 3:X, 4:Phase)
    AcousticCurve curves[5];
    int curveVolumes[5];  // Volume percentage per curve
    int masterVolume;     // Master volume percentage (0-100%)
    std::mutex curveMutex;
    
    // Phase accumulators for smooth pitch gliding (persist across buffers)
    double swrPhase;
    double rlPhase;
    double zPhase;
    double xPhase;
    double phasePhaseL;  // Left channel for phase curve
    double phasePhaseR;  // Right channel for phase curve
    
    // Audio thread
    std::thread audioThread;
    std::atomic<bool> shouldStop;
    std::atomic<bool> buffersWereFlushed;  // Flag to signal immediate restart after flush
    
    // Y-Axis Ruler state
    std::atomic<bool> rulerPlaying;  // Whether ruler is currently playing
    std::atomic<bool> rulerShouldStop;  // Flag to stop ruler playback
    std::thread rulerThread;  // Separate thread for ruler playback
    int rulerVolume;  // Volume for ruler (0-100%)
    int rulerBlipDurationMs;  // Duration of shortest blip in ms
    int rulerLengtheningFactorPercent;  // Lengthening factor in % for longer tones (100-500%)
    PlaybackState stateBeforeRuler;  // State to restore after ruler completes
    
    // X-Axis Ruler state
    std::atomic<bool> xAxisRulerEnabled;  // Whether X-axis ruler is enabled
    int xAxisRulerVolume;  // Volume for X-axis ruler (0-100%)
    int xAxisRulerBlipDurationMs;  // Duration of X-axis ruler blips in ms (30-200ms)
    size_t lastXAxisBlipPosition;  // Last position where X-axis blip was played
    XAxisRulerNoiseType xAxisRulerNoiseType;  // Type of noise for X-axis ruler
    int xAxisRulerMidiDrum;  // MIDI drum note for X-axis ruler (default: 42 = closed hi-hat)
    
    // Status line state
    std::atomic<bool> statusLineEnabled;  // Whether status line is enabled
    StatusLineContent statusLineContent;  // What to display in status line (legacy)
    bool statusLineShowPosition;  // Show position in status line
    bool statusLineShowFrequency;  // Show frequency in status line
    bool statusLineShowSWR;  // Show SWR value in status line
    bool statusLineShowRL;  // Show Return Loss value in status line
    bool statusLineShowImpedance;  // Show Impedance magnitude value in status line
    bool statusLineShowReactance;  // Show Reactance value in status line
    bool statusLineShowPhase;  // Show Phase value in status line
    mutable std::mutex statusLineMutex;  // Mutex to protect status line access
    
    // Ruler sound configuration
    RulerSoundMode rulerSoundMode;  // Sound mode (follow curve or custom)
    int rulerCustomSoundSynth;  // Custom waveform for synth mode (0-5)
    int rulerCustomSoundMidiGliding;  // Custom MIDI instrument for gliding
    int rulerCustomSoundMidiDotted;  // Custom MIDI instrument for dotted
    int lastEnabledCurve;  // Track last enabled curve for FOLLOW_LAST_CURVE mode
    
    // Timing constants for skip factor calculation
    static constexpr int MIN_SMOOTH_TRANSITION_TIME_MS = 20;   // Minimum time per transition in smooth mode (matches frame duration)
    static constexpr int MIN_DOTTED_DURATION_MS = 100;         // Default minimum duration per dot in dotted mode
    static constexpr int CURVE_TOGGLE_PAUSE_DELAY_MS = 10;     // Delay between pause and play when toggling curves (ms)
    static constexpr double DOTTED_SOUND_FRACTION = 0.5;        // Fraction of time for sound in dotted mode (0.5 = 50%)
    
    // Dotted playback timing constraints
    static constexpr int MIN_DOT_DURATION_THRESHOLD_MS = 30;   // Threshold for tight time budget detection
    static constexpr int ABSOLUTE_MIN_DOT_DURATION_MS = 10;     // Absolute minimum dot duration in tight budget
    static constexpr int MIN_SILENCE_DURATION_MS = 10;          // Minimum silence between dots
    static constexpr int ABSOLUTE_MIN_SILENCE_MS = 5;           // Absolute minimum silence in tight budget
    
    // Inverted loop gap timing constraints
    static constexpr int MIN_INVERTED_LOOP_CLICK_INTERVAL_MS = 10;  // Minimum milliseconds between accelerated clicks (audibility threshold)
    static constexpr int INVERTED_LOOP_CLICK_GAP_MS = 5;            // Small gap between blip duration and click interval (milliseconds)
    
    // Intelligent point selection for dotted mode
    std::vector<size_t> selectedPointsCache;  // Cache of selected point indices for dotted mode
    std::mutex selectedPointsCacheMutex;       // Mutex to protect selectedPointsCache access
    std::atomic<bool> needsPointSelection;     // Flag to indicate point selection needs updating
    
    // Constants for point importance scoring
    static constexpr int NUM_CURVES = 5;                       // Total number of curves (SWR, RL, |Z|, X, Phase)
    static constexpr double IMPORTANCE_BOUNDARY = 100.0;       // Importance score for boundary points (start/end)
    static constexpr double IMPORTANCE_EXTREMUM = 50.0;        // Base importance for local extrema (peaks/valleys)
    static constexpr double IMPORTANCE_EXTREMUM_WEIGHT = 10.0; // Weight multiplier for extremum deviation magnitude
    static constexpr double IMPORTANCE_DIRECTION_CHANGE = 30.0;// Importance for sharp direction changes
    static constexpr double IMPORTANCE_BASE = 1.0;             // Base importance for all points (for tie-breaking)
    static constexpr double DIRECTION_CHANGE_THRESHOLD = 0.3;  // Minimum change ratio to be "sharp" (30%)
    static constexpr double EPSILON_SLOPE = 1e-10;             // Small epsilon for slope calculations
    
    // Warning thresholds for downsampling
    static constexpr int MIN_POINTS_FOR_ACCURATE_REPRESENTATION = 10; // Minimum points to preserve curve characteristics
    static constexpr double WARNING_DOWNSAMPLING_RATIO = 0.1;  // Warn if keeping less than 10% of points
    static constexpr double LTTB_FALLBACK_THRESHOLD = 0.8;     // Fallback to importance-based if LTTB returns < 80% of expected points
    
    // Helper methods for intelligent point selection
    void selectPointsForDottedMode(size_t startIdx, size_t endIdx, int maxPoints);
    void selectPointsForDottedModeInverted(size_t loopLeft, size_t loopRight, int maxPoints);
    std::vector<size_t> selectPointsUsingLTTB(size_t startIdx, size_t endIdx, int threshold);
    double getPointImportance(size_t idx, int curveIndex) const;
    bool isLocalExtremum(size_t idx, int curveIndex) const;
    bool hasSharpDirectionChange(size_t idx, int curveIndex) const;
    void checkAndWarnDownsamplingQuality(size_t originalCount, size_t selectedCount, double timePerPointMs) const;
    
    // Audio backend for cross-platform audio output
    std::unique_ptr<IAudioBackend> backend;
    std::mutex audioMutex;
    
    // Audio playback helper
    void playAudioBuffer(const std::vector<int16_t>& buffer);
    
    void audioThreadFunc();
    void playCurrentPosition(int durationMs = 20);  // Play with configurable duration (default 20ms for smooth mode)
    void playCurrentPositionSmooth(double fractionalProgress, int skipFactor = 1);  // For smooth gliding between points
    void advancePosition();
    
    // Y-Axis Ruler helpers
    void rulerThreadFunc();  // Thread function for ruler playback
    
    // Pitch calculation helpers
    double calcSWRPitch(const MeasurementPoint& pt);
    double calcRLPitch(const MeasurementPoint& pt);
    double calcZPitch(const MeasurementPoint& pt);
    double calcXPitch(const MeasurementPoint& pt);
    double calcPhasePitch(const MeasurementPoint& pt);
    
    // Audio synthesis for each curve type (legacy - for synthesizer engine compatibility)
    void synthSWR(const MeasurementPoint& pt, std::vector<int16_t>& buffer, int samples, double frac);
    void synthReturnLoss(const MeasurementPoint& pt, std::vector<int16_t>& buffer, int samples, double frac);
    void synthImpedanceMag(const MeasurementPoint& pt, std::vector<int16_t>& buffer, int samples, double frac);
    void synthReactance(const MeasurementPoint& pt, std::vector<int16_t>& buffer, int samples, double frac);
    void synthPhase(const MeasurementPoint& pt, std::vector<int16_t>& buffer, int samples, double frac);
    
    // Interpolated audio synthesis for smooth mode (continuous pitch transitions)
    void synthSWRInterpolated(const MeasurementPoint& pt1, const MeasurementPoint& pt2, std::vector<int16_t>& buffer, int samples, double frac1, double frac2, double progress = 0.5);
    void synthReturnLossInterpolated(const MeasurementPoint& pt1, const MeasurementPoint& pt2, std::vector<int16_t>& buffer, int samples, double frac1, double frac2, double progress = 0.5);
    void synthImpedanceMagInterpolated(const MeasurementPoint& pt1, const MeasurementPoint& pt2, std::vector<int16_t>& buffer, int samples, double frac1, double frac2, double progress = 0.5);
    void synthReactanceInterpolated(const MeasurementPoint& pt1, const MeasurementPoint& pt2, std::vector<int16_t>& buffer, int samples, double frac1, double frac2, double progress = 0.5);
    void synthPhaseInterpolated(const MeasurementPoint& pt1, const MeasurementPoint& pt2, std::vector<int16_t>& buffer, int samples, double frac1, double frac2, double progress = 0.5);
    
    // Helper to mix audio into buffer
    void mixIntoBuffer(std::vector<int16_t>& buffer, const std::vector<int16_t>& source);
    
    // Check and generate axis crossing sounds for range (handles skipped points)
    void checkAxisCrossingsInRange(size_t prevPos, size_t currentPos, std::vector<int16_t>& buffer, int samples);
    
    // Track last position for axis crossing detection
    size_t lastAxisCrossingCheckPos;
    
    // Buffer for ongoing axis event sound that spans multiple frames
    std::vector<int16_t> pendingAxisEventBuffer;
    size_t pendingAxisEventOffset;  // Current offset in the pending buffer
    std::mutex axisEventBufferMutex;
};
