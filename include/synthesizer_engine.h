#pragma once
#include "audio_engine_interface.h"
#include "waveform.h"
#include <mutex>
#include <memory>

// Forward declaration to avoid circular dependency
class IAudioBackend;

/**
 * Synthesizer Audio Engine
 * 
 * Waveform-based audio synthesis engine supporting multiple waveform types.
 * This is the original audio engine used in the application, now refactored
 * to implement the IAudioEngine interface and inherit from AudioEngineBase.
 * 
 * Features:
 * - Multiple waveform types (Sine, Square, Triangle, Noise)
 * - Stereo panning based on position in sweep
 * - Volume control per curve
 * - Phase continuity for smooth audio
 */
class SynthesizerEngine : public AudioEngineBase {
public:
    SynthesizerEngine();
    ~SynthesizerEngine() noexcept override;
    
    // IAudioEngine interface implementation
    // Note: open(), close(), isOpen() inherited from AudioEngineBase
    
    void generateAudio(
        std::vector<int16_t>& buffer,
        int samples,
        int curveIndex,
        double pitchHz,
        double panFraction,
        int volumePercent) override;
    
    const char* getName() const override { return "Synthesizer"; }
    AudioEngineType getEngineType() const override { return AudioEngineType::SYNTHESIZER; }
    
    void generateRulerAudio(
        std::vector<int16_t>& buffer,
        int samples,
        double pitchHz,
        double panFraction,
        int volumePercent,
        int waveformIndex) override;
    
    void generateXAxisRulerAudio(
        std::vector<int16_t>& buffer,
        int samples,
        double panFraction,
        int volumePercent) override;
    
    /**
     * Set the noise type for X-axis ruler
     * @param noiseType 0=White noise, 1=Pink noise, 2=Click
     */
    void setXAxisRulerNoiseType(int noiseType) { xAxisRulerNoiseType = noiseType; }
    
    /**
     * Get the noise type for X-axis ruler
     * @return Current noise type
     */
    int getXAxisRulerNoiseType() const { return xAxisRulerNoiseType; }
    
    /**
     * Set the waveform for a specific curve
     * @param curveIndex Curve identifier (0-4)
     * @param wf Waveform type to use
     */
    void setCurveWaveform(int curveIndex, Waveform wf);
    
    /**
     * Get the waveform for a specific curve
     * @param curveIndex Curve identifier (0-4)
     * @return Current waveform type
     */
    Waveform getCurveWaveform(int curveIndex) const;
    
    /**
     * Play a preview tone with the specified curve's waveform
     * @param curveIndex Curve identifier (0-4)
     * @param durationMs Duration in milliseconds
     */
    void playPreview(int curveIndex, int durationMs = 500);

protected:
    // AudioEngineBase hooks
    bool onInitialize() override;
    
private:
    // Note: mtx and opened inherited from AudioEngineBase
    int sampleRate = 44100;
    int channels = 2;
    int bits = 16;
    std::unique_ptr<IAudioBackend> backend;
    
    // Waveforms for each curve (0=SWR, 1=RL, 2=|Z|, 3=X, 4=Phase)
    Waveform curveWaveforms[5];
    
    // Phase accumulators for each curve (maintains phase continuity)
    double curvePhases[5];
    
    // Phase accumulator for ruler (separate from curves to avoid interference)
    double rulerPhase = 0.0;
    
    // X-axis ruler noise type (0=White, 1=Pink, 2=Click)
    int xAxisRulerNoiseType = 0;
};
