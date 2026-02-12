#include "synthesizer_engine.h"
#include "audio_backend.h"
#include "waveform_utils.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <ctime>
#include <thread>
#include <functional>

using namespace WaveformUtils;

SynthesizerEngine::SynthesizerEngine() : backend(createAudioBackend()) {
    // Initialize default waveforms for each curve
    curveWaveforms[0] = Waveform::SINE;          // SWR
    curveWaveforms[1] = Waveform::SQUARE;        // Return Loss
    curveWaveforms[2] = Waveform::TRIANGLE;      // Impedance Mag
    curveWaveforms[3] = Waveform::SAWTOOTH;      // Reactance
    curveWaveforms[4] = Waveform::PULSE;         // Phase
    
    // Initialize phase accumulators
    for (int i = 0; i < 5; i++) {
        curvePhases[i] = 0.0;
    }
}

SynthesizerEngine::~SynthesizerEngine() noexcept {
    // Destructor must not throw - wrap close() in try-catch
    try {
        close();
    } catch (const std::exception& e) {
        // Silently swallow exception to prevent termination
    } catch (...) {
        // Silently swallow any exception to prevent termination
    }
    
    // Clean up audio backend
    // Memory automatically freed by unique_ptr, but shutdown() called explicitly to release audio resources
    if (backend) {
        backend->shutdown();
    }
}

// Task 1.8: onInitialize hook replaces old open() implementation
bool SynthesizerEngine::onInitialize() {
    // Initialize audio backend if not already initialized
    // Note: mutex already locked by AudioEngineBase::open()
    if (backend) {
        backend->initialize();
    }
    return true;
}

void SynthesizerEngine::generateAudio(
    std::vector<int16_t>& buffer,
    int samples,
    int curveIndex,
    double pitchHz,
    double panFraction,
    int volumePercent)
{
    if (!opened) return;
    if (curveIndex < 0 || curveIndex >= 5) return;
    if (samples <= 0) return;
    
    // Ensure buffer is large enough (stereo)
    ensureStereoBuffer(buffer, samples);
    
    // Get waveform and phase for this curve
    Waveform wf = curveWaveforms[curveIndex];
    double& phase = curvePhases[curveIndex];
    
    // Calculate phase increment per sample
    double phaseInc = pitchHz / sampleRate;
    
    // Calculate stereo panning using constant-power panning
    double panL, panR;
    constantPowerPan(panFraction, panL, panR);
    
    // Apply volume scaling
    double volumeScale = normalizeVolume(volumePercent, 0, 200);
    
    // Headroom management: Scale down to prevent clipping when multiple curves are mixed
    // With up to 5 curves potentially active, divide amplitude by 5 to ensure headroom
    // This prevents intermodulation distortion when signals mix in the stereo field
    constexpr int MAX_CONCURRENT_CURVES = 5;
    const double headroomScale = 1.0 / MAX_CONCURRENT_CURVES;
    const double baseAmplitude = AUDIO_AMPLITUDE_SCALE * headroomScale;  // ±6000 per curve
    
    // Soft limiting constants (declared once, outside loop)
    constexpr double SOFT_LIMIT_THRESHOLD = 30000.0;
    constexpr double SOFT_LIMIT_SCALE = 32000.0;
    
    // Generate samples
    for (int i = 0; i < samples; ++i) {
        double s = generateWaveformSample(phase, wf);
        
        // Update phase for next sample
        phase = advancePhase(phase, phaseInc);
        
        // Apply panning and volume
        double left = s * panL * volumeScale;
        double right = s * panR * volumeScale;
        
        // Convert to 16-bit samples with headroom management
        int16_t li = static_cast<int16_t>(std::lround(std::clamp(left * baseAmplitude, -32768.0, 32767.0)));
        int16_t ri = static_cast<int16_t>(std::lround(std::clamp(right * baseAmplitude, -32768.0, 32767.0)));
        
        // Mix into buffer (add to existing samples)
        // Use 32-bit arithmetic to prevent overflow during addition
        int32_t leftSum = static_cast<int32_t>(buffer[i * 2 + 0]) + static_cast<int32_t>(li);
        int32_t rightSum = static_cast<int32_t>(buffer[i * 2 + 1]) + static_cast<int32_t>(ri);
        
        // Apply soft limiting to reduce harsh clipping artifacts
        // Use a tanh-based soft limiter for smoother saturation curve
        // Note: tanh is only called when needed (typically rare with proper headroom)
        if (std::abs(leftSum) > SOFT_LIMIT_THRESHOLD) {
            leftSum = static_cast<int32_t>(SOFT_LIMIT_SCALE * std::tanh(leftSum / SOFT_LIMIT_SCALE));
        }
        if (std::abs(rightSum) > SOFT_LIMIT_THRESHOLD) {
            rightSum = static_cast<int32_t>(SOFT_LIMIT_SCALE * std::tanh(rightSum / SOFT_LIMIT_SCALE));
        }
        
        // Final hard clamp to ensure we never exceed int16_t range
        buffer[i * 2 + 0] = static_cast<int16_t>(std::clamp(leftSum, 
            static_cast<int32_t>(-32768), static_cast<int32_t>(32767)));
        buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(rightSum,
            static_cast<int32_t>(-32768), static_cast<int32_t>(32767)));
    }
}

void SynthesizerEngine::setCurveWaveform(int curveIndex, Waveform wf) {
    if (curveIndex >= 0 && curveIndex < 5) {
        std::lock_guard<std::mutex> l(mtx);
        curveWaveforms[curveIndex] = wf;
    }
}

Waveform SynthesizerEngine::getCurveWaveform(int curveIndex) const {
    if (curveIndex >= 0 && curveIndex < 5) {
        return curveWaveforms[curveIndex];
    }
    return Waveform::SINE;
}

void SynthesizerEngine::playPreview(int curveIndex, int durationMs) {
    if (!opened || !backend) return;
    if (curveIndex < 0 || curveIndex >= 5) return;
    
    // Generate a preview tone at 440 Hz (A4) with the curve's waveform
    const int previewFreqHz = 440;
    const int samples = (sampleRate * durationMs) / 1000;
    
    // Create buffer for the preview
    std::vector<int16_t> buffer(samples * 2, 0);
    
    // Generate audio with center panning and 80% volume
    generateAudio(buffer, samples, curveIndex, previewFreqHz, 0.5, 80);
    
    // Play the buffer using the platform audio backend
    backend->playBuffer(buffer.data(), samples, sampleRate, channels, bits);
}

void SynthesizerEngine::generateRulerAudio(
    std::vector<int16_t>& buffer,
    int samples,
    double pitchHz,
    double panFraction,
    int volumePercent,
    int waveformIndex)
{
    if (!opened) return;
    if (samples <= 0) return;
    
    // Ensure buffer is large enough (stereo)
    ensureStereoBuffer(buffer, samples);
    
    // Number of curves in the system
    constexpr int NUM_CURVES = 5;
    // Number of waveform types available
    constexpr int NUM_WAVEFORMS = 6;  // SINE, SQUARE, TRIANGLE, SAWTOOTH, SAWTOOTH_INV, PULSE
    
    // Determine waveform to use
    Waveform wf;
    if (waveformIndex >= 0 && waveformIndex < NUM_WAVEFORMS) {
        // Use specified waveform (for custom sound mode)
        wf = static_cast<Waveform>(waveformIndex);
    } else if (waveformIndex >= 0 && waveformIndex < NUM_CURVES) {
        // Use curve's waveform (for follow last curve mode)
        wf = curveWaveforms[waveformIndex];
    } else {
        // Default to sine wave
        wf = Waveform::SINE;
    }
    
    // Calculate phase increment per sample
    double phaseInc = pitchHz / sampleRate;
    
    // Calculate stereo panning using constant-power panning
    double panL, panR;
    constantPowerPan(panFraction, panL, panR);
    
    // Apply volume scaling
    double volumeScale = normalizeVolume(volumePercent, 0, 200);
    
    // Ruler uses full amplitude (no headroom reduction needed for single source)
    const double baseAmplitude = AUDIO_AMPLITUDE_SCALE;
    
    // Generate samples
    for (int i = 0; i < samples; ++i) {
        double s = generateWaveformSample(rulerPhase, wf);
        
        // Update phase for next sample
        rulerPhase = advancePhase(rulerPhase, phaseInc);
        
        // Apply panning and volume
        double left = s * panL * volumeScale;
        double right = s * panR * volumeScale;
        
        // Convert to 16-bit samples
        int16_t li = static_cast<int16_t>(std::lround(std::clamp(left * baseAmplitude, -32768.0, 32767.0)));
        int16_t ri = static_cast<int16_t>(std::lround(std::clamp(right * baseAmplitude, -32768.0, 32767.0)));
        
        // Mix into buffer using mixSamples helper
        buffer[i * 2 + 0] = mixSamples(buffer[i * 2 + 0], li);
        buffer[i * 2 + 1] = mixSamples(buffer[i * 2 + 1], ri);
    }
}

void SynthesizerEngine::generateXAxisRulerAudio(
    std::vector<int16_t>& buffer,
    int samples,
    double panFraction,
    int volumePercent)
{
    if (!opened) return;
    if (samples <= 0) return;
    
    // Ensure buffer is large enough (stereo)
    ensureStereoBuffer(buffer, samples);
    
    // Calculate stereo panning using constant-power panning
    double panL, panR;
    constantPowerPan(panFraction, panL, panR);
    
    // Apply volume scaling
    double volumeScale = normalizeVolume(volumePercent, 0, 200);
    
    // Base amplitude for noise
    const double baseAmplitude = AUDIO_AMPLITUDE_SCALE;
    
    // Generate noise based on type
    // Using thread-local storage for thread-safe pseudo-random number generation
    thread_local static unsigned int noiseSeed = static_cast<unsigned int>(time(nullptr) + std::hash<std::thread::id>{}(std::this_thread::get_id()));
    
    // Pink noise filter state (for type 1)
    thread_local static double b0 = 0.0, b1 = 0.0, b2 = 0.0, b3 = 0.0, b4 = 0.0, b5 = 0.0, b6 = 0.0;
    
    for (int i = 0; i < samples; ++i) {
        double noiseSample = 0.0;
        
        if (xAxisRulerNoiseType == 2) {
            // Type 2: Click - short impulse at start, silence after
            if (i < 5) {
                noiseSample = 1.0;
            } else {
                noiseSample = 0.0;
            }
        } else {
            // Generate base white noise for both types 0 and 1
            noiseSeed = noiseSeed * 1103515245 + 12345;
            double whiteNoise = (static_cast<double>(noiseSeed) / 2147483648.0) - 1.0;  // -1.0 to 1.0
            
            if (xAxisRulerNoiseType == 1) {
                // Type 1: Pink noise - filter white noise
                // Paul Kellett's pink noise filter
                b0 = 0.99886 * b0 + whiteNoise * 0.0555179;
                b1 = 0.99332 * b1 + whiteNoise * 0.0750759;
                b2 = 0.96900 * b2 + whiteNoise * 0.1538520;
                b3 = 0.86650 * b3 + whiteNoise * 0.3104856;
                b4 = 0.55000 * b4 + whiteNoise * 0.5329522;
                b5 = -0.7616 * b5 - whiteNoise * 0.0168980;
                noiseSample = b0 + b1 + b2 + b3 + b4 + b5 + b6 + whiteNoise * 0.5362;
                noiseSample *= 0.11; // Adjust amplitude
                b6 = whiteNoise * 0.115926;
            } else {
                // Type 0: White noise (default)
                noiseSample = whiteNoise;
            }
        }
        
        // Apply envelope: short attack, exponential decay
        double envelope = std::exp(-5.0 * static_cast<double>(i) / static_cast<double>(samples));
        noiseSample *= envelope;
        
        // Apply panning and volume
        double left = noiseSample * panL * volumeScale;
        double right = noiseSample * panR * volumeScale;
        
        // Convert to 16-bit samples
        int16_t li = static_cast<int16_t>(std::lround(std::clamp(left * baseAmplitude, -32768.0, 32767.0)));
        int16_t ri = static_cast<int16_t>(std::lround(std::clamp(right * baseAmplitude, -32768.0, 32767.0)));
        
        // Write directly to buffer using writeStereoSample
        writeStereoSample(buffer, i, li, ri);
    }
}
