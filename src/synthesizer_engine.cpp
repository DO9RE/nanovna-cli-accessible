#include "synthesizer_engine.h"
#include "audio_backend.h"
#include "waveform_utils.h"
#include <cmath>
#include <cstdio>
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
    
    // Initialize echo delay buffers for reverb/echo effect
    echoBufferL.resize(ECHO_BUFFER_SIZE, 0.0);
    echoBufferR.resize(ECHO_BUFFER_SIZE, 0.0);
    echoWritePos = 0;
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

// ============================================================================
// Synthesizer Reactance DSP Effects
// ============================================================================

double SynthesizerEngine::applyScaling(double normalizedValue, AppConfig::EffectScaling scaling) {
    double v = std::clamp(normalizedValue, 0.0, 1.0);
    switch (scaling) {
        case AppConfig::EffectScaling::LINEAR:
            return v;
        case AppConfig::EffectScaling::SQUARE_ROOT:
            return std::sqrt(v);
        case AppConfig::EffectScaling::EXPONENTIAL:
            return v * v;
        case AppConfig::EffectScaling::LOGARITHMIC:
            return std::log1p(v * 9.0) / std::log(10.0);
        case AppConfig::EffectScaling::S_CURVE:
            return v * v * (3.0 - 2.0 * v);
        default:
            return v;
    }
}

void SynthesizerEngine::applySynthReactanceEffects(
    std::vector<int16_t>& buffer,
    int samples,
    double reactanceOhms,
    const AppConfig::SynthReactanceModeEffectConfig& config,
    double deadzone, double maxOhms)
{
    if (!opened) {
        if (logger) logger->log("SYNTH_FX", "applySynthReactanceEffects: engine not opened, skipping");
        return;
    }
    if (samples <= 0) return;
    
    double absX = std::fabs(reactanceOhms);
    
    // Within dead zone: no effects
    if (absX <= deadzone) {
        if (logger) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Reactance X=%.1f Ohm within dead zone (+/-%.1f), no effect", reactanceOhms, deadzone);
            logger->log("SYNTH_FX", msg);
        }
        return;
    }
    
    // Calculate normalized intensity (0.0-1.0) from dead zone edge to max
    double effectiveRange = maxOhms - deadzone;
    if (effectiveRange <= 0.0) effectiveRange = 1.0;
    double normalizedIntensity = std::clamp((absX - deadzone) / effectiveRange, 0.0, 1.0);
    
    if (reactanceOhms < 0.0) {
        // Capacitive (X < 0): apply capacitive effect
        double scaled = applyScaling(normalizedIntensity, config.capacitive_scaling);
        double intensity = scaled * config.capacitive_max_percent / 100.0;
        if (logger) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Reactance X=%.1f Ohm (capacitive), effect=%d, normalized=%.3f, scaled=%.3f, intensity=%.3f, maxPct=%d",
                     reactanceOhms, static_cast<int>(config.capacitive_effect), normalizedIntensity, scaled, intensity, config.capacitive_max_percent);
            logger->log("SYNTH_FX", msg);
        }
        applySynthEffect(buffer, samples, config.capacitive_effect, intensity);
    } else {
        // Inductive (X > 0): apply inductive effect
        double scaled = applyScaling(normalizedIntensity, config.inductive_scaling);
        double intensity = scaled * config.inductive_max_percent / 100.0;
        if (logger) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Reactance X=%.1f Ohm (inductive), effect=%d, normalized=%.3f, scaled=%.3f, intensity=%.3f, maxPct=%d",
                     reactanceOhms, static_cast<int>(config.inductive_effect), normalizedIntensity, scaled, intensity, config.inductive_max_percent);
            logger->log("SYNTH_FX", msg);
        }
        applySynthEffect(buffer, samples, config.inductive_effect, intensity);
    }
}

void SynthesizerEngine::applySynthEffect(
    std::vector<int16_t>& buffer,
    int samples,
    AppConfig::SynthReactanceEffectType effectType,
    double intensity)
{
    if (intensity <= 0.0 || samples <= 0) return;
    intensity = std::clamp(intensity, 0.0, 1.0);
    
    static const char* effectNames[] = {"AM_TREMOLO", "ECHO", "RING_MOD", "FILTER_SWEEP", "NOISE_MIX", "BITCRUSH"};
    int effectIdx = static_cast<int>(effectType);
    const char* effectName = (effectIdx >= 0 && effectIdx < 6) ? effectNames[effectIdx] : "UNKNOWN";
    
    if (logger) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Applying DSP effect: %s (type=%d), intensity=%.3f, samples=%d",
                 effectName, effectIdx, intensity, samples);
        logger->log("SYNTH_FX", msg);
    }
    
    switch (effectType) {
        case AppConfig::SynthReactanceEffectType::AM_TREMOLO: {
            // Amplitude modulation / Tremolo
            // LFO modulates the amplitude at ~6 Hz (typical tremolo rate)
            // Intensity controls modulation depth (0=no modulation, 1=full modulation 0-100%)
            const double lfoRateHz = 6.0;
            const double lfoInc = lfoRateHz / sampleRate;
            
            for (int i = 0; i < samples; ++i) {
                // LFO: oscillates between (1-intensity) and 1.0
                double lfo = 1.0 - intensity * 0.5 * (1.0 + std::sin(2.0 * PI_CONST * tremoloLfoPhase));
                tremoloLfoPhase = advancePhase(tremoloLfoPhase, lfoInc);
                
                // Apply to both channels
                int32_t left = static_cast<int32_t>(buffer[i * 2 + 0] * lfo);
                int32_t right = static_cast<int32_t>(buffer[i * 2 + 1] * lfo);
                buffer[i * 2 + 0] = static_cast<int16_t>(std::clamp(left, (int32_t)-32768, (int32_t)32767));
                buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(right, (int32_t)-32768, (int32_t)32767));
            }
            break;
        }
        
        case AppConfig::SynthReactanceEffectType::ECHO: {
            // Delay-based echo/reverb simulation
            // Uses a circular delay buffer with feedback to create spatial "room" feeling
            // Delay time ~100ms, feedback proportional to intensity
            const int delayTimeSamples = sampleRate / 10;  // 100ms delay
            const double feedback = 0.3 * intensity;  // Feedback amount
            const double wetMix = 0.4 * intensity;    // Wet/dry mix
            
            for (int i = 0; i < samples; ++i) {
                // Read from delay buffer
                int readPos = (echoWritePos - delayTimeSamples + ECHO_BUFFER_SIZE) % ECHO_BUFFER_SIZE;
                double delayedL = echoBufferL[readPos];
                double delayedR = echoBufferR[readPos];
                
                // Current dry signal
                double dryL = buffer[i * 2 + 0] / 32768.0;
                double dryR = buffer[i * 2 + 1] / 32768.0;
                
                // Write to delay buffer (input + feedback from delay output)
                echoBufferL[echoWritePos] = dryL + delayedL * feedback;
                echoBufferR[echoWritePos] = dryR + delayedR * feedback;
                echoWritePos = (echoWritePos + 1) % ECHO_BUFFER_SIZE;
                
                // Mix: dry + wet delayed signal
                double outL = dryL + delayedL * wetMix;
                double outR = dryR + delayedR * wetMix;
                
                buffer[i * 2 + 0] = static_cast<int16_t>(std::clamp(outL * 32768.0, -32768.0, 32767.0));
                buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(outR * 32768.0, -32768.0, 32767.0));
            }
            break;
        }
        
        case AppConfig::SynthReactanceEffectType::RING_MOD: {
            // Ring modulation: multiply signal with a carrier frequency
            // Creates metallic, inharmonic tones — very distinctive timbre change
            // Carrier at ~300Hz, intensity controls wet/dry mix
            const double carrierHz = 300.0;
            const double carrierInc = carrierHz / sampleRate;
            // Reuse tremoloLfoPhase for ring mod carrier (they won't both be active at once)
            double ringPhase = tremoloLfoPhase;
            
            for (int i = 0; i < samples; ++i) {
                double carrier = std::sin(2.0 * PI_CONST * ringPhase);
                ringPhase = advancePhase(ringPhase, carrierInc);
                
                double dryL = buffer[i * 2 + 0] / 32768.0;
                double dryR = buffer[i * 2 + 1] / 32768.0;
                
                // Blend ring-modulated with dry signal
                double wetL = dryL * carrier;
                double wetR = dryR * carrier;
                double outL = dryL * (1.0 - intensity) + wetL * intensity;
                double outR = dryR * (1.0 - intensity) + wetR * intensity;
                
                buffer[i * 2 + 0] = static_cast<int16_t>(std::clamp(outL * 32768.0, -32768.0, 32767.0));
                buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(outR * 32768.0, -32768.0, 32767.0));
            }
            tremoloLfoPhase = ringPhase;
            break;
        }
        
        case AppConfig::SynthReactanceEffectType::FILTER_SWEEP: {
            // Simple low-pass filter with cutoff proportional to (1 - intensity)
            // As intensity increases, cutoff drops → sound gets duller
            // This simulates "capacitance charging" (high frequencies absorbed)
            // Uses a one-pole IIR filter: y[n] = alpha * x[n] + (1-alpha) * y[n-1]
            double cutoffNormalized = 1.0 - 0.9 * intensity;  // 1.0 = no filtering, 0.1 = heavy filter
            double alpha = cutoffNormalized;  // Simple coefficient
            
            // Use static thread-local for filter state persistence
            thread_local static double filterStateL = 0.0;
            thread_local static double filterStateR = 0.0;
            
            for (int i = 0; i < samples; ++i) {
                double inL = buffer[i * 2 + 0] / 32768.0;
                double inR = buffer[i * 2 + 1] / 32768.0;
                
                filterStateL = alpha * inL + (1.0 - alpha) * filterStateL;
                filterStateR = alpha * inR + (1.0 - alpha) * filterStateR;
                
                buffer[i * 2 + 0] = static_cast<int16_t>(std::clamp(filterStateL * 32768.0, -32768.0, 32767.0));
                buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(filterStateR * 32768.0, -32768.0, 32767.0));
            }
            break;
        }
        
        case AppConfig::SynthReactanceEffectType::NOISE_MIX: {
            // Mix white noise into the signal proportional to intensity
            // Creates a "hiss" that indicates reactance magnitude
            // Thread-safe pseudo-random generation
            thread_local static unsigned int noiseSeed = static_cast<unsigned int>(
                time(nullptr) + std::hash<std::thread::id>{}(std::this_thread::get_id()));
            
            for (int i = 0; i < samples; ++i) {
                // Generate noise
                noiseSeed = noiseSeed * 1103515245 + 12345;
                double noise = (static_cast<double>(noiseSeed) / 2147483648.0) - 1.0;
                
                double dryL = buffer[i * 2 + 0] / 32768.0;
                double dryR = buffer[i * 2 + 1] / 32768.0;
                
                // Mix: signal + noise scaled by intensity
                double noiseLevel = intensity * 0.3;  // Keep noise level modest
                double outL = dryL + noise * noiseLevel;
                double outR = dryR + noise * noiseLevel;
                
                buffer[i * 2 + 0] = static_cast<int16_t>(std::clamp(outL * 32768.0, -32768.0, 32767.0));
                buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(outR * 32768.0, -32768.0, 32767.0));
            }
            break;
        }
        
        case AppConfig::SynthReactanceEffectType::BITCRUSH: {
            // Bit-depth reduction: reduces resolution for digital distortion effect
            // At intensity=1.0, reduces to ~4 bits (16 levels)
            // At intensity=0.0, full 16-bit resolution (no effect)
            double bitReduction = intensity * 12.0;  // 0-12 bits of reduction
            double levels = std::pow(2.0, 16.0 - bitReduction);
            if (levels < 2.0) levels = 2.0;
            
            for (int i = 0; i < samples; ++i) {
                double inL = buffer[i * 2 + 0] / 32768.0;
                double inR = buffer[i * 2 + 1] / 32768.0;
                
                // Quantize to reduced bit depth
                double outL = std::round(inL * levels) / levels;
                double outR = std::round(inR * levels) / levels;
                
                buffer[i * 2 + 0] = static_cast<int16_t>(std::clamp(outL * 32768.0, -32768.0, 32767.0));
                buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(outR * 32768.0, -32768.0, 32767.0));
            }
            break;
        }
    }
}
