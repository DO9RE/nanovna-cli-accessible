#pragma once
#include <vector>
#include <cstdint>
#include <mutex>
#include "config.h"  // For AudioEngineType enum

/**
 * Audio Engine Interface
 * 
 * Base interface for all audio generation engines in the NanoVNA CLI application.
 * Implementations include:
 * - SynthesizerEngine: Waveform-based synthesis (Sine, Square, Triangle, Noise)
 * - MIDIEngine: MIDI-based sound generation with instrument support
 */
class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;
    
    /**
     * Initialize and open the audio engine
     * @return true on success, false on failure
     */
    virtual bool open() = 0;
    
    /**
     * Close and cleanup the audio engine
     */
    virtual void close() = 0;
    
    /**
     * Check if the audio engine is currently open
     * @return true if open, false otherwise
     */
    virtual bool isOpen() const = 0;
    
    /**
     * Generate and play an audio buffer for a single measurement point
     * 
     * @param buffer Output buffer to fill with audio samples (stereo, 16-bit)
     * @param samples Number of samples to generate (per channel)
     * @param curveIndex Curve identifier (0=SWR, 1=RL, 2=|Z|, 3=X, 4=Phase)
     * @param pitchHz Desired pitch in Hz
     * @param panFraction Position in sweep (0.0=left, 1.0=right) for stereo panning
     * @param volumePercent Volume level (0-200%)
     */
    virtual void generateAudio(
        std::vector<int16_t>& buffer,
        int samples,
        int curveIndex,
        double pitchHz,
        double panFraction,
        int volumePercent) = 0;
    
    /**
     * Get the name of this audio engine
     * @return Engine name for display purposes
     */
    virtual const char* getName() const = 0;
    
    /**
     * Get the type of this audio engine
     * @return Engine type (SYNTHESIZER or MIDI)
     */
    virtual AudioEngineType getEngineType() const = 0;
    
    /**
     * Stop all active notes/sounds (for MIDI engines)
     * Default implementation does nothing (for non-MIDI engines)
     */
    virtual void stopAllNotes() {}
    
    /**
     * Stop note for a specific curve (for MIDI engines)
     * @param curveIndex Curve identifier (0-4)
     * Default implementation does nothing (for non-MIDI engines)
     */
    virtual void stopCurveNote(int curveIndex) { (void)curveIndex; }
    
    /**
     * Generate audio specifically for the Y-axis ruler on a dedicated channel
     * This avoids conflicts with curve channels in MIDI mode
     * @param buffer Output buffer to fill with audio samples (stereo, 16-bit)
     * @param samples Number of samples to generate (per channel)
     * @param pitchHz Desired pitch in Hz
     * @param panFraction Position in sweep (0.0=left, 1.0=right) for stereo panning
     * @param volumePercent Volume level (0-200%)
     * @param waveformIndex Waveform/instrument index for this ruler sound
     * Default implementation uses generateAudio with curve 0
     */
    virtual void generateRulerAudio(
        std::vector<int16_t>& buffer,
        int samples,
        double pitchHz,
        double panFraction,
        int volumePercent,
        int waveformIndex = 0) {
        // Default: use curve 0
        (void)waveformIndex;  // Unused in base implementation
        generateAudio(buffer, samples, 0, pitchHz, panFraction, volumePercent);
    }
    
    /**
     * Set custom ruler instruments for MIDI mode
     * @param glidingInstrument MIDI program for gliding mode
     * @param dottedInstrument MIDI program for dotted mode
     * Default implementation does nothing (for non-MIDI engines)
     */
    virtual void setRulerCustomInstruments(int glidingInstrument, int dottedInstrument) {
        (void)glidingInstrument;
        (void)dottedInstrument;
    }
    
    /**
     * Stop ruler note (for MIDI engines)
     * Default implementation does nothing (for non-MIDI engines)
     */
    virtual void stopRulerNote() {}
    
    /**
     * Generate audio specifically for the X-axis ruler (noise impulses/percussion)
     * @param buffer Output buffer to fill with audio samples (stereo, 16-bit)
     * @param samples Number of samples to generate (per channel)
     * @param panFraction Position in sweep (0.0=left, 1.0=right) for stereo panning
     * @param volumePercent Volume level (0-200%)
     * Default implementation generates white noise
     */
    virtual void generateXAxisRulerAudio(
        std::vector<int16_t>& buffer,
        int samples,
        double panFraction,
        int volumePercent) {
        // Default: generate white noise impulse
        for (int i = 0; i < samples; i++) {
            // Generate white noise
            int16_t noiseSample = static_cast<int16_t>((rand() % 65536) - 32768);
            int16_t scaledSample = static_cast<int16_t>(noiseSample * volumePercent / 100);
            
            // Apply stereo panning
            double panL = 1.0 - panFraction;
            double panR = panFraction;
            
            buffer[i * 2 + 0] = static_cast<int16_t>(scaledSample * panL);
            buffer[i * 2 + 1] = static_cast<int16_t>(scaledSample * panR);
        }
    }
};

/**
 * Task 1.8: Audio Engine Base Class
 * 
 * Provides common lifecycle management (open/close/isOpen) for all audio engines.
 * Eliminates duplicate mutex-protected state management across AudioEngine,
 * SynthesizerEngine, and AcousticAnalyzer.
 * 
 * Subclasses should:
 * - Call the protected constructor
 * - Override onInitialize() for engine-specific setup
 * - Use isOpen() to check state before operations
 */
class AudioEngineBase : public IAudioEngine {
protected:
    std::mutex mtx;
    bool opened;
    
    /**
     * Constructor initializes the base state
     */
    AudioEngineBase() : opened(false) {}
    
    /**
     * Virtual hook for subclass-specific initialization
     * Called with mutex already locked during open()
     * @return true on success, false on failure
     */
    virtual bool onInitialize() { return true; }
    
    /**
     * Virtual hook for subclass-specific cleanup
     * Called with mutex already locked during close()
     */
    virtual void onCleanup() {}
    
public:
    /**
     * Open the audio engine with thread-safe state management
     * Calls onInitialize() hook for subclass-specific setup
     * @return true on success, false on failure
     */
    bool open() override {
        std::lock_guard<std::mutex> l(mtx);
        if (!opened) {
            if (!onInitialize()) {
                return false;
            }
            opened = true;
        }
        return true;
    }
    
    /**
     * Close the audio engine with thread-safe state management
     * Calls onCleanup() hook for subclass-specific cleanup
     */
    void close() override {
        std::lock_guard<std::mutex> l(mtx);
        if (opened) {
            onCleanup();
            opened = false;
        }
    }
    
    /**
     * Check if the engine is currently open (thread-safe)
     * @return true if open, false otherwise
     */
    bool isOpen() const override {
        return opened;
    }
};
