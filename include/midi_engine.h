#pragma once
#include "audio_engine_interface.h"
#include "logger.h"
#include <mutex>
#include <cstdint>
#include <algorithm>
#include <memory>

// Forward declaration of platform interface
class MIDIPlatformInterface;

/**
 * MIDI Audio Engine
 * 
 * Platform-independent MIDI audio synthesis engine.
 * Each curve uses a separate MIDI channel with its own instrument.
 * 
 * Features:
 * - Cross-platform MIDI synthesis (Windows/macOS/Linux)
 * - 5 MIDI channels (one per curve)
 * - Configurable instrument per channel (General MIDI)
 * - Two playback modes:
 *   - Gliding mode: For sustained instruments (strings, organs, pads) - no retriggering
 *   - Dotted mode: For percussive instruments (vibraphone, bells) - with retriggering
 * - Default instruments optimized for continuous tone playback:
 *   - SWR: Church Organ (sustained)
 *   - Return Loss: Drawbar Organ (sustained)
 *   - Impedance Magnitude: Lead 2 (sawtooth) (sustained)
 *   - Reactance: Lead 1 (square) (sustained)
 *   - Phase: String Ensemble 1 (sustained)
 * - Pitch bend for fine-grained pitch control (14-bit resolution)
 * - PCM-layer panning via stereo note velocity/expression
 * - Smooth transitions and volume control
 * 
 * MIDI Channel Assignment:
 * - Channel 0: SWR
 * - Channel 1: Return Loss
 * - Channel 2: Impedance Magnitude
 * - Channel 3: Reactance
 * - Channel 5: Phase (skipping channel 4)
 * 
 * Note: Uses channel 10 skipping (channel 9 is reserved for drums in GM)
 */
class MIDIEngine : public IAudioEngine {
public:
    MIDIEngine();
    ~MIDIEngine() noexcept override;
    
    // IAudioEngine interface implementation
    bool open() override;
    void close() override;
    bool isOpen() const override { return opened; }
    
    void generateAudio(
        std::vector<int16_t>& buffer,
        int samples,
        int curveIndex,
        double pitchHz,
        double panFraction,
        int volumePercent) override;
    
    const char* getName() const override { return "MIDI"; }
    AudioEngineType getEngineType() const override { return AudioEngineType::MIDI; }
    
    // Override IAudioEngine methods for note management
    void stopAllNotes() override { allNotesOff(); }
    void stopCurveNote(int curveIndex) override;
    void generateRulerAudio(
        std::vector<int16_t>& buffer,
        int samples,
        double pitchHz,
        double panFraction,
        int volumePercent,
        int waveformIndex) override;
    void stopRulerNote() override;
    void setRulerCustomInstruments(int glidingInstrument, int dottedInstrument) override;
    
    void generateXAxisRulerAudio(
        std::vector<int16_t>& buffer,
        int samples,
        double panFraction,
        int volumePercent) override;
    
    /**
     * Set the MIDI instrument (program) for a specific curve
     * @param curveIndex Curve identifier (0-4)
     * @param program MIDI program number (0-127, General MIDI)
     */
    void setCurveInstrument(int curveIndex, int program);
    
    /**
     * Get the MIDI instrument for a specific curve
     * @param curveIndex Curve identifier (0-4)
     * @return MIDI program number (0-127)
     */
    int getCurveInstrument(int curveIndex) const;
    
    /**
     * Play a preview tone with the specified instrument
     * @param program MIDI program number (0-127)
     * @param durationMs Duration in milliseconds
     */
    void playPreview(int program, int durationMs = 500);
    
    /**
     * Set MIDI playback mode (gliding vs dotted)
     * @param glidingMode true for gliding mode (no retriggering), false for dotted mode (with retriggering)
     */
    void setGlidingMode(bool glidingMode);
    
    /**
     * Get current MIDI playback mode
     * @return true if in gliding mode, false if in dotted mode
     */
    bool isGlidingMode() const { return glidingMode; }
    
    /**
     * Reset MIDI engine state (stop all notes, reinitialize)
     * Call this when switching playback modes to prevent hanging notes
     */
    void reset();
    
    /**
     * Set logger for debug output
     * @param logger Pointer to logger instance
     */
    void setLogger(Logger* logger);
    
    /**
     * Set the synth frequency range for pitch bend calculations
     * @param minHz Minimum frequency in Hz
     * @param maxHz Maximum frequency in Hz
     */
    void setSynthFrequencyRange(int minHz, int maxHz);
    
    /**
     * Set the MIDI drum note for X-axis ruler
     * @param drumNote MIDI drum note (35-81)
     */
    void setXAxisRulerDrum(int drumNote);
    
    /**
     * Get the MIDI drum note for X-axis ruler
     * @return MIDI drum note
     */
    int getXAxisRulerDrum() const { return xAxisRulerDrum; }
    
    /**
     * Enable/disable interpolated panning mode
     * Uses volume modulation to create perceived pan positions between MIDI's 128 discrete steps
     * @param enable true to enable volume-based pan interpolation
     */
    void setInterpolatedPanMode(bool enable);
    
    /**
     * Get current interpolated panning mode state
     * @return true if interpolated panning is enabled
     */
    bool isInterpolatedPanMode() const { return interpolatedPanMode; }
    
    /**
     * Set interpolation strength (how much volume affects perceived pan)
     * @param strength 0.0 (no effect) to 1.0 (maximum effect)
     */
    void setInterpolationStrength(double strength);
    
    /**
     * Get current interpolation strength
     * @return interpolation strength (0.0-1.0)
     */
    double getInterpolationStrength() const { return interpolationStrength; }
    
    // Number of curves/channels supported
    static constexpr int NUM_CURVES = 5;

private:
    std::mutex mtx;
    bool opened = false;
    bool glidingMode = true;  // Default to gliding mode (no retriggering for sustained instruments)
    Logger* logger = nullptr;  // Debug logger
    
    // Synth frequency range for pitch bend calculations
    int synthMinFreqHz = 100;   // Minimum synth frequency (Hz)
    int synthMaxFreqHz = 1000;  // Maximum synth frequency (Hz)
    uint8_t referenceNote = 60; // Reference MIDI note (middle C by default)
    
    // Interpolated panning mode (Mischtechniken)
    bool interpolatedPanMode = false;      // Enable volume-based pan interpolation
    double interpolationStrength = 0.3;    // Default: 30% volume modulation
    
    // Platform-specific MIDI implementation
    std::unique_ptr<MIDIPlatformInterface> platform;
    
    // MIDI instrument (program) for each curve
    int curveInstruments[NUM_CURVES];
    
    // Custom ruler instruments
    int rulerCustomGlidingInstrument = 48;  // Default: String Ensemble
    int rulerCustomDottedInstrument = 11;    // Default: Vibraphone
    int xAxisRulerDrum = 42;  // Default: Closed Hi-Hat (MIDI note 42)
    
    // Current note state for each channel (for note off messages)
    struct NoteState {
        bool active;
        uint8_t note;
        uint8_t velocity;
    };
    NoteState channelNotes[NUM_CURVES];
    NoteState rulerNote;  // Separate note state for ruler (uses channel 6)
    NoteState xAxisRulerNote;  // Separate note state for X-axis ruler (uses channel 9 = drums)
    
    /**
     * Send a MIDI message
     * @param status Status byte (command + channel)
     * @param data1 First data byte
     * @param data2 Second data byte
     */
    void sendMIDIMessage(uint8_t status, uint8_t data1, uint8_t data2);
    
    /**
     * Send a program change message
     * @param channel MIDI channel (0-15)
     * @param program Program number (0-127)
     */
    void sendProgramChange(int channel, int program);
    
    /**
     * Send a note on message
     * @param channel MIDI channel (0-15)
     * @param note Note number (0-127)
     * @param velocity Velocity (0-127)
     */
    void sendNoteOn(int channel, uint8_t note, uint8_t velocity);
    
    /**
     * Send a note off message
     * @param channel MIDI channel (0-15)
     * @param note Note number (0-127)
     */
    void sendNoteOff(int channel, uint8_t note);
    
    /**
     * Send a pitch bend message
     * @param channel MIDI channel (0-15)
     * @param bend Pitch bend value (-8192 to +8191, 0 = center)
     */
    void sendPitchBend(int channel, int16_t bend);
    
    /**
     * Send a pan (CC 10) message
     * @param channel MIDI channel (0-15)
     * @param pan Pan value (0-127, 64 = center)
     */
    void sendPan(int channel, uint8_t pan);
    
    /**
     * Send a volume (CC 7) message
     * @param channel MIDI channel (0-15)
     * @param volume Volume value (0-127)
     */
    void sendVolume(int channel, uint8_t volume);
    
    /**
     * Convert frequency in Hz to MIDI note number and pitch bend
     * @param freqHz Frequency in Hz
     * @param outNote Output MIDI note number (0-127)
     * @param outBend Output pitch bend (-8192 to +8191)
     */
    void frequencyToMIDI(double freqHz, uint8_t& outNote, int16_t& outBend);
    
    /**
     * Convert frequency in Hz to pitch bend only (using reference note)
     * This method uses only pitch bend for pitch changes, avoiding note retriggering
     * @param freqHz Frequency in Hz
     * @param outBend Output pitch bend (-8192 to +8191)
     */
    void frequencyToPitchBend(double freqHz, int16_t& outBend);
    
    /**
     * Calculate reference note from synth frequency range
     * Updates the referenceNote member based on the middle frequency
     */
    void calculateReferenceNote();
    
    /**
     * Stop all notes on all channels
     */
    void allNotesOff();
    
    /**
     * Calculate interpolated pan and volume values
     * @param panFraction Desired pan position (0.0 = left, 1.0 = right)
     * @param baseVolume Base volume level (0-127)
     * @param outPan Output pan value
     * @param outVolume Output volume value
     */
    void calculateInterpolatedPanVolume(
        double panFraction, 
        uint8_t baseVolume,
        uint8_t& outPan, 
        uint8_t& outVolume);
};
