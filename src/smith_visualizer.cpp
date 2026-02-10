#include "smith_visualizer.h"
#include "config.h"
#include "math_logger.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <random>

// Windows Audio API headers for hardware detection
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

static constexpr double PI = 3.14159265358979323846;
static constexpr int SAMPLE_RATE = 44100;

SmithVisualizer::SmithVisualizer(Logger* logger_, TranslationManager* translation_)
    : logger(logger_), translation(translation_),
      currentMode(SmithVisualizationMode::CARTESIAN),
      audioCapability(AudioCapability::STEREO_ONLY),
      smithEnabled(false),
      smithCuesEnabled(false),
      markersEnabled(false),
      smithCuesVolume(DEFAULT_SMITH_CUES_VOLUME),
      noiseType(AppConfig::SmithNoiseType::PINK),
      lastQuadrant(-1),
      centerPulseEnabled(false),
      centerPulseVolume(DEFAULT_CENTER_PULSE_VOLUME),
      centerPulseIntervalSeconds(1.0),
      centerPulsePhase(0.0),
      centerPulseWaveform(AppConfig::CenterPulseWaveform::CLICK),
      axisEventsEnabled(false),
      axisEventsVolume(DEFAULT_AXIS_EVENTS_VOLUME),
      axisEventsDurationMs(DEFAULT_AXIS_EVENTS_DURATION_MS),
      axisEventsPitchMinHz(300.0),
      axisEventsPitchMaxHz(800.0),
      lastS11Im(0.0),
      lastS11Re(0.0),
      lastCrossingValid(false),
      lastCheckedPosition(0),
      axisCrossingSound(AppConfig::AxisCrossingSound::PLUCK) {
    
    if (logger) {
        logger->log("SMITH", "SmithVisualizer initialized");
    }
    
    // Auto-detect audio capability
    audioCapability = detectAudioCapability();
}

SmithVisualizer::~SmithVisualizer() {
    if (logger) {
        logger->log("SMITH", "SmithVisualizer destroyed");
    }
}

void SmithVisualizer::setMode(SmithVisualizationMode mode) {
    std::lock_guard<std::mutex> lock(stateMutex);
    currentMode = mode;
    
    if (logger) {
        logger->log("SMITH", "Mode changed to: " + getModeName(mode));
    }
}

void SmithVisualizer::setAudioCapability(AudioCapability cap) {
    std::lock_guard<std::mutex> lock(stateMutex);
    audioCapability = cap;
    
    if (logger) {
        const char* capName = "UNKNOWN";
        switch (cap) {
            case AudioCapability::STEREO_ONLY: capName = "Stereo"; break;
            case AudioCapability::SURROUND_5_1: capName = "5.1 Surround"; break;
            case AudioCapability::SURROUND_7_1: capName = "7.1 Surround"; break;
            case AudioCapability::SURROUND_ATMOS: capName = "Dolby Atmos"; break;
        }
        logger->log("SMITH", std::string("Audio capability set to: ") + capName);
    }
}

AudioCapability SmithVisualizer::detectAudioCapability() {
    // Detect audio capability based on hardware
    // This will be used by audio engine to configure channel count
    
    AudioCapability detected = AudioCapability::STEREO_ONLY;
    
    // Use Windows Core Audio API for detection
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(hr);
    
    if (comInitialized || hr == RPC_E_CHANGED_MODE) {
        IMMDeviceEnumerator* pEnumerator = NULL;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
                            CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                            (void**)&pEnumerator);
        
        if (SUCCEEDED(hr) && pEnumerator) {
            IMMDevice* pDevice = NULL;
            hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
            
            if (SUCCEEDED(hr) && pDevice) {
                IAudioClient* pAudioClient = NULL;
                hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                     NULL, (void**)&pAudioClient);
                
                if (SUCCEEDED(hr) && pAudioClient) {
                    WAVEFORMATEX* pWaveFormat = NULL;
                    hr = pAudioClient->GetMixFormat(&pWaveFormat);
                    
                    if (SUCCEEDED(hr) && pWaveFormat) {
                        int channels = pWaveFormat->nChannels;
                        
                        if (channels >= 8) {
                            detected = AudioCapability::SURROUND_7_1;
                        } else if (channels >= 6) {
                            detected = AudioCapability::SURROUND_5_1;
                        } else {
                            detected = AudioCapability::STEREO_ONLY;
                        }
                        
                        if (logger) {
                            std::ostringstream oss;
                            oss << "Audio hardware detection: " << channels << " channels -> ";
                            switch (detected) {
                                case AudioCapability::STEREO_ONLY: oss << "Stereo"; break;
                                case AudioCapability::SURROUND_5_1: oss << "5.1 Surround"; break;
                                case AudioCapability::SURROUND_7_1: oss << "7.1 Surround"; break;
                                case AudioCapability::SURROUND_ATMOS: oss << "Dolby Atmos"; break;
                            }
                            logger->log("SMITH", oss.str());
                            
                            // Additional debug information
                            std::ostringstream detail;
                            detail << "  Sample Rate: " << pWaveFormat->nSamplesPerSec << " Hz";
                            logger->log("SMITH", detail.str());
                            detail.str("");
                            detail << "  Bits per sample: " << pWaveFormat->wBitsPerSample;
                            logger->log("SMITH", detail.str());
                            
                            // Only access dwChannelMask if this is WAVEFORMATEXTENSIBLE
                            if (pWaveFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && 
                                pWaveFormat->cbSize >= (sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))) {
                                WAVEFORMATEXTENSIBLE* wfex = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pWaveFormat);
                                detail.str("");
                                detail << "  Channel mask: 0x" << std::hex << wfex->dwChannelMask;
                                logger->log("SMITH", detail.str());
                            }
                        }
                        
                        CoTaskMemFree(pWaveFormat);
                    }
                    
                    pAudioClient->Release();
                }
                
                pDevice->Release();
            }
            
            pEnumerator->Release();
        }
        
        if (comInitialized) {
            CoUninitialize();
        }
    }
    
    if (logger) {
        const char* capName = "UNKNOWN";
        switch (detected) {
            case AudioCapability::STEREO_ONLY: capName = "Stereo (2.0) - Left/Right panning only"; break;
            case AudioCapability::SURROUND_5_1: capName = "5.1 Surround - Front, Rear, Center + LFE"; break;
            case AudioCapability::SURROUND_7_1: capName = "7.1 Surround - Full 360° spatial audio"; break;
            case AudioCapability::SURROUND_ATMOS: capName = "Dolby Atmos - Height channels available"; break;
        }
        logger->log("SMITH", std::string("Final capability: ") + capName);
    }
    
    return detected;
}

void SmithVisualizer::setAudioEngine(std::shared_ptr<IAudioEngine> engine) {
    std::lock_guard<std::mutex> lock(stateMutex);
    audioEngine = engine;
    
    if (logger && engine) {
        logger->log("SMITH", std::string("Audio engine set: ") + engine->getName());
    }
}

void SmithVisualizer::setSmithCuesVolume(int volumePercent) {
    smithCuesVolume = std::clamp(volumePercent, MIN_SMITH_CUES_VOLUME, MAX_SMITH_CUES_VOLUME);
}

void SmithVisualizer::setCenterPulseVolume(int volumePercent) {
    centerPulseVolume = std::clamp(volumePercent, MIN_CENTER_PULSE_VOLUME, MAX_CENTER_PULSE_VOLUME);
}

void SmithVisualizer::setCenterPulseInterval(double intervalSeconds) {
    centerPulseIntervalSeconds = std::clamp(intervalSeconds, 0.5, 2.0);
    if (logger) {
        logger->log("SMITH", "Center pulse interval set to " + std::to_string(centerPulseIntervalSeconds) + " seconds");
    }
}

void SmithVisualizer::setAxisEventsVolume(int volumePercent) {
    axisEventsVolume = std::clamp(volumePercent, MIN_AXIS_EVENTS_VOLUME, MAX_AXIS_EVENTS_VOLUME);
}

void SmithVisualizer::setAxisEventsDuration(int durationMs) {
    axisEventsDurationMs = std::clamp(durationMs, MIN_AXIS_EVENTS_DURATION_MS, MAX_AXIS_EVENTS_DURATION_MS);
    if (logger) {
        logger->log("SMITH", "Axis events duration set to " + std::to_string(axisEventsDurationMs.load()) + " ms");
    }
}

void SmithVisualizer::setAxisEventsPitchRange(double minHz, double maxHz) {
    axisEventsPitchMinHz = std::clamp(minHz, 200.0, 1000.0);
    axisEventsPitchMaxHz = std::clamp(maxHz, 400.0, 2000.0);
    // Ensure min < max
    if (axisEventsPitchMinHz >= axisEventsPitchMaxHz) {
        axisEventsPitchMaxHz = axisEventsPitchMinHz + 100.0;
    }
    if (logger) {
        logger->log("SMITH", "Axis events pitch range set to " + 
                   std::to_string(axisEventsPitchMinHz) + " - " + 
                   std::to_string(axisEventsPitchMaxHz) + " Hz");
    }
}

// Configure surround sound settings
void SmithVisualizer::setSurroundConfig(int frontDist, int backDist, int sideDist, int centerStrength, 
                                       int fbSeparation, int sideEmphasis, AppConfig::SurroundFadingCurve curve) {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    surroundConfig.frontDistance = std::clamp(frontDist, 50, 200);
    surroundConfig.backDistance = std::clamp(backDist, 50, 200);
    surroundConfig.sideDistance = std::clamp(sideDist, 50, 200);
    surroundConfig.centerStrength = std::clamp(centerStrength, 0, 100);
    surroundConfig.fbSeparation = std::clamp(fbSeparation, 50, 200);
    surroundConfig.sideEmphasis = std::clamp(sideEmphasis, 50, 200);
    surroundConfig.fadingCurve = curve;
    
    if (logger) {
        logger->log("SMITH", "Surround configuration updated:");
        logger->log("SMITH", "  Front distance: " + std::to_string(surroundConfig.frontDistance) + "%");
        logger->log("SMITH", "  Back distance: " + std::to_string(surroundConfig.backDistance) + "%");
        logger->log("SMITH", "  Side distance: " + std::to_string(surroundConfig.sideDistance) + "%");
        logger->log("SMITH", "  Center strength: " + std::to_string(surroundConfig.centerStrength) + "%");
        logger->log("SMITH", "  F/B separation: " + std::to_string(surroundConfig.fbSeparation) + "%");
        logger->log("SMITH", "  Side emphasis: " + std::to_string(surroundConfig.sideEmphasis) + "%");
    }
}

// Calculate gamma magnitude from S11 parameters
double SmithVisualizer::calculateGammaMagnitude(const MeasurementPoint& pt) const {
    return std::sqrt(pt.s11_re * pt.s11_re + pt.s11_im * pt.s11_im);
}

// Calculate gamma phase from S11 parameters
double SmithVisualizer::calculateGammaPhase(const MeasurementPoint& pt) const {
    return std::atan2(pt.s11_im, pt.s11_re) * 180.0 / PI;
}

// Normalize impedance value for mapping
double SmithVisualizer::normalizeImpedance(double value, double Z0, double maxValue) const {
    // Normalize to -1.0 to +1.0 range
    double normalized = (value - Z0) / maxValue;
    return std::clamp(normalized, -1.0, 1.0);
}

// Calculate Cartesian position in Smith chart
SmithPosition3D SmithVisualizer::calculateCartesianPosition(const MeasurementPoint& pt) const {
    SmithPosition3D pos;
    
    // X-axis: Real part of Gamma
    pos.x = std::clamp(pt.s11_re, -1.0, 1.0);
    
    // Y-axis: Imaginary part of Gamma
    pos.y = std::clamp(pt.s11_im, -1.0, 1.0);
    
    // Z-axis: Not used in 2D Smith chart
    pos.z = 0.0;
    
    // Pitch: Based on SWR (low SWR = low pitch, high SWR = high pitch)
    // Map SWR 1.0-10.0 to 200-2000 Hz
    double swr = std::clamp(pt.swr, 1.0, 10.0);
    pos.pitch = 200.0 + (swr - 1.0) * 200.0;
    
    // Volume: Based on Return Loss (high RL = quiet, low RL = loud)
    // Map RL 0-30 dB to 1.0-0.2
    double rl = std::clamp(pt.rl, 0.0, 30.0);
    pos.volume = 1.0 - (rl / 40.0);  // Inverted: low RL = loud
    pos.volume = std::clamp(pos.volume, 0.2, 1.0);
    
    return pos;
}

// Calculate polar position in Smith chart
SmithPositionPolar SmithVisualizer::calculatePolarPosition(const MeasurementPoint& pt) const {
    SmithPositionPolar pos;
    
    // Angle: Phase of Gamma (-180 to +180 degrees)
    pos.angle = calculateGammaPhase(pt);
    
    // Radius: Magnitude of Gamma (0.0 to ~1.0)
    pos.radius = calculateGammaMagnitude(pt);
    pos.radius = std::clamp(pos.radius, 0.0, 1.0);
    
    // Pitch: Based on impedance type (resistive vs reactive)
    // Map reactance to pitch: high |X| = high pitch
    double absX = std::abs(pt.X);
    pos.pitch = 400.0 + std::min(absX / 50.0, 1.0) * 600.0;  // 400-1000 Hz
    
    // Volume: Based on radius (distance from center)
    // Far from center = loud, near center = quiet
    pos.volume = 0.2 + pos.radius * 0.8;  // 0.2 to 1.0
    
    return pos;
}

// Calculate impedance-direct position
SmithPosition3D SmithVisualizer::calculateImpedanceDirectPosition(const MeasurementPoint& pt) const {
    SmithPosition3D pos;
    
    // X-axis: Resistance (0-200Ω mapped to -1 to +1, centered at 50Ω)
    pos.x = normalizeImpedance(pt.R, 50.0, 150.0);
    
    // Y-axis: Reactance (-200 to +200Ω mapped to -1 to +1)
    pos.y = std::clamp(pt.X / 200.0, -1.0, 1.0);
    
    // Z-axis: Not used
    pos.z = 0.0;
    
    // Pitch: Based on |Z - 50|
    double impedanceDeviation = std::abs(pt.impedance_mag - 50.0);
    pos.pitch = 300.0 + std::min(impedanceDeviation / 150.0, 1.0) * 700.0;
    
    // Volume: Based on SWR
    double swr = std::clamp(pt.swr, 1.0, 10.0);
    pos.volume = 0.3 + (swr - 1.0) / 9.0 * 0.7;
    
    return pos;
}

// Calculate SWR Circles position - focus on radial distance from center
SmithPosition3D SmithVisualizer::calculateSwrCirclesPosition(const MeasurementPoint& pt) const {
    SmithPosition3D pos;
    
    // Calculate SWR-based radial position
    // SWR circles are concentric around the center (perfect match)
    double gammaMag = calculateGammaMagnitude(pt);
    double gammaPhase = calculateGammaPhase(pt);
    
    // Map to XY position based on phase angle (for stereo positioning)
    // but emphasize radial distance
    double angleRad = gammaPhase * PI / 180.0;
    pos.x = std::cos(angleRad) * gammaMag;
    pos.y = std::sin(angleRad) * gammaMag;
    pos.z = 0.0;
    
    // Pitch: Based primarily on SWR value
    // Low SWR (good) = low pitch, high SWR (bad) = high pitch
    double swr = std::clamp(pt.swr, 1.0, 10.0);
    pos.pitch = 150.0 + (swr - 1.0) * 200.0;  // 150-1950 Hz range
    
    // Volume: Based on distance from center (SWR quality)
    // Near center (SWR ≈ 1) = quiet, far from center = loud
    pos.volume = 0.1 + gammaMag * 0.9;  // 0.1-1.0 based on |Γ|
    pos.volume = std::clamp(pos.volume, 0.1, 1.0);
    
    return pos;
}

// Calculate Time Domain Cues position - frequency sweep with Smith spatial cues
SmithPosition3D SmithVisualizer::calculateTimeDomainCuesPosition(const MeasurementPoint& pt, double freqPosition) const {
    SmithPosition3D pos;
    
    // Primary axis: Frequency position (left to right)
    // freqPosition should be 0.0-1.0 representing position in frequency sweep
    pos.x = (freqPosition * 2.0) - 1.0;  // Map 0-1 to -1 to +1
    
    // Secondary axis: Smith chart Im(Γ) provides subtle front-back cues
    // Scale down to make it subtle (not dominant)
    pos.y = std::clamp(pt.s11_im, -1.0, 1.0) * 0.3;  // Reduced to 30% influence
    
    pos.z = 0.0;
    
    // Pitch: Based on measurement value (like standard acoustic mode)
    // Use SWR for pitch encoding
    double swr = std::clamp(pt.swr, 1.0, 10.0);
    pos.pitch = 200.0 + (swr - 1.0) * 200.0;
    
    // Volume: Modified by Smith position (Re(Γ) affects volume)
    // Center of Smith chart = louder, edges = quieter
    double gammaMag = calculateGammaMagnitude(pt);
    pos.volume = 1.0 - (gammaMag * 0.3);  // 0.7-1.0 based on match quality
    pos.volume = std::clamp(pos.volume, 0.5, 1.0);
    
    return pos;
}

// Calculate Hybrid Multi-Layer position - combines multiple approaches
SmithPosition3D SmithVisualizer::calculateHybridMultiPosition(const MeasurementPoint& pt) const {
    SmithPosition3D pos;
    
    // Layer 1 (Main): Use Polar as primary (strongest spatial cues)
    SmithPositionPolar polarPos = calculatePolarPosition(pt);
    double angleRad = polarPos.angle * PI / 180.0;
    
    // Convert polar to Cartesian for positioning
    pos.x = std::cos(angleRad) * polarPos.radius;
    pos.y = std::sin(angleRad) * polarPos.radius;
    pos.z = 0.0;
    
    // Layer 2 (Context): Blend in SWR information
    double swr = std::clamp(pt.swr, 1.0, 10.0);
    
    // Layer 3 (Markers): Will be handled by marker system separately
    
    // Pitch: Combine polar (reactance) and SWR
    pos.pitch = (polarPos.pitch + (200.0 + (swr - 1.0) * 200.0)) * 0.5;
    
    // Volume: Combine radius-based and SWR-based volume
    double swrVolume = 0.3 + (swr - 1.0) / 9.0 * 0.7;
    pos.volume = (polarPos.volume * 0.6 + swrVolume * 0.4);  // Weighted average
    pos.volume = std::clamp(pos.volume, 0.2, 1.0);
    
    return pos;
}

// Calculate 7.1 surround panning from Cartesian coordinates
MultiChannelGains SmithVisualizer::calculateCartesianPanning(double reGamma, double imGamma) const {
    MultiChannelGains gains;
    
    // Clamp inputs
    reGamma = std::clamp(reGamma, -1.0, 1.0);
    imGamma = std::clamp(imGamma, -1.0, 1.0);
    
    // Calculate based on audio capability
    if (audioCapability == AudioCapability::SURROUND_7_1 || 
        audioCapability == AudioCapability::SURROUND_ATMOS) {
        calculateVBAP7_1(reGamma, imGamma, gains);
    } else if (audioCapability == AudioCapability::SURROUND_5_1) {
        calculateVBAP5_1(reGamma, imGamma, gains);
    } else {
        // Stereo fallback
        // X (reGamma) -> Left-Right
        double lr = (reGamma + 1.0) / 2.0;  // 0.0 = left, 1.0 = right
        gains.frontLeft = static_cast<float>(1.0 - lr);
        gains.frontRight = static_cast<float>(lr);
        
        // Y (imGamma) modifies brightness (not spatial in stereo)
        // This could be used for timbre modulation in actual synthesis
    }
    
    return gains;
}

// VBAP for 7.1 surround
void SmithVisualizer::calculateVBAP7_1(double x, double y, MultiChannelGains& gains) const {
    // Map x, y to speaker positions
    // x: -1 (left) to +1 (right)
    // y: -1 (back) to +1 (front)
    
    // Apply fading curve to inputs for more natural spatial perception
    double fadedX = x;
    double fadedY = y;
    
    switch (surroundConfig.fadingCurve) {
        case AppConfig::SurroundFadingCurve::LOGARITHMIC:
            fadedX = (x >= 0) ? std::log1p(x) / std::log(2.0) : -std::log1p(-x) / std::log(2.0);
            fadedY = (y >= 0) ? std::log1p(y) / std::log(2.0) : -std::log1p(-y) / std::log(2.0);
            break;
        case AppConfig::SurroundFadingCurve::EXPONENTIAL:
            fadedX = (x >= 0) ? (std::exp(x) - 1.0) / (std::exp(1.0) - 1.0) : -(std::exp(-x) - 1.0) / (std::exp(1.0) - 1.0);
            fadedY = (y >= 0) ? (std::exp(y) - 1.0) / (std::exp(1.0) - 1.0) : -(std::exp(-y) - 1.0) / (std::exp(1.0) - 1.0);
            break;
        case AppConfig::SurroundFadingCurve::SINE:
            fadedX = std::sin(x * PI / 2.0);
            fadedY = std::sin(y * PI / 2.0);
            break;
        case AppConfig::SurroundFadingCurve::LINEAR:
        default:
            // No transformation needed
            break;
    }
    
    // Apply front/back separation enhancement
    fadedY *= (surroundConfig.fbSeparation / 100.0);
    fadedY = std::clamp(fadedY, -1.0, 1.0);
    
    // Calculate front-back balance
    double frontBalance = (fadedY + 1.0) / 2.0;  // 0.0 = back, 1.0 = front
    
    // Calculate left-right balance
    double rightBalance = (fadedX + 1.0) / 2.0;  // 0.0 = left, 1.0 = right
    
    // Log calculations if math logger is available
    if (mathLogger && mathLogger->isEnabled()) {
        std::ostringstream oss;
        oss << "VBAP 7.1 calculation: x=" << x << ", y=" << y
            << " -> fadedX=" << fadedX << ", fadedY=" << fadedY
            << ", frontBalance=" << frontBalance << ", rightBalance=" << rightBalance;
        mathLogger->logDataFlow("SMITH_VBAP", oss.str());
    }
    
    // Distribute to speakers based on position
    if (frontBalance > 0.5) {
        // Front speakers dominant
        double frontAmount = (frontBalance - 0.5) * 2.0;
        frontAmount *= (surroundConfig.frontDistance / 100.0);
        
        gains.frontLeft = static_cast<float>((1.0 - rightBalance) * frontAmount);
        gains.frontRight = static_cast<float>(rightBalance * frontAmount);
        gains.frontCenter = static_cast<float>((1.0 - std::abs(rightBalance - 0.5) * 2.0) * frontAmount * 
                                               (surroundConfig.centerStrength / 100.0));
        
        // Add some side speakers for smooth transition
        double sideAmount = (1.0 - frontAmount) * (surroundConfig.sideEmphasis / 100.0);
        gains.sideLeft = static_cast<float>((1.0 - rightBalance) * sideAmount * (surroundConfig.sideDistance / 100.0));
        gains.sideRight = static_cast<float>(rightBalance * sideAmount * (surroundConfig.sideDistance / 100.0));
    } else {
        // Back speakers dominant
        double backAmount = (0.5 - frontBalance) * 2.0;
        backAmount *= (surroundConfig.backDistance / 100.0);
        
        gains.backLeft = static_cast<float>((1.0 - rightBalance) * backAmount);
        gains.backRight = static_cast<float>(rightBalance * backAmount);
        
        // Add side speakers for transition
        double sideAmount = (1.0 - backAmount) * (surroundConfig.sideEmphasis / 100.0);
        gains.sideLeft = static_cast<float>((1.0 - rightBalance) * sideAmount * (surroundConfig.sideDistance / 100.0));
        gains.sideRight = static_cast<float>(rightBalance * sideAmount * (surroundConfig.sideDistance / 100.0));
    }
    
    // Normalize total power to 1.0
    float total = gains.frontLeft + gains.frontRight + gains.frontCenter +
                  gains.backLeft + gains.backRight + gains.sideLeft + gains.sideRight;
    if (total > 0.0001f) {
        float scale = 1.0f / total;
        gains.frontLeft *= scale;
        gains.frontRight *= scale;
        gains.frontCenter *= scale;
        gains.backLeft *= scale;
        gains.backRight *= scale;
        gains.sideLeft *= scale;
        gains.sideRight *= scale;
    }
    
    // Log final gains if math logger is available
    if (mathLogger && mathLogger->isEnabled()) {
        std::ostringstream oss;
        oss << "Final 7.1 gains: FL=" << gains.frontLeft << ", FR=" << gains.frontRight
            << ", FC=" << gains.frontCenter << ", BL=" << gains.backLeft << ", BR=" << gains.backRight
            << ", SL=" << gains.sideLeft << ", SR=" << gains.sideRight;
        mathLogger->logDataFlow("SMITH_VBAP_RESULT", oss.str());
    }
}

// VBAP for 5.1 surround (no side speakers)
void SmithVisualizer::calculateVBAP5_1(double x, double y, MultiChannelGains& gains) const {
    // Similar to 7.1 but without side speakers
    double frontBalance = (y + 1.0) / 2.0;
    double rightBalance = (x + 1.0) / 2.0;
    
    // Front speakers
    if (frontBalance > 0.5) {
        double frontAmount = (frontBalance - 0.5) * 2.0;
        gains.frontLeft = static_cast<float>((1.0 - rightBalance) * frontAmount);
        gains.frontRight = static_cast<float>(rightBalance * frontAmount);
        gains.frontCenter = static_cast<float>((1.0 - std::abs(rightBalance - 0.5) * 2.0) * frontAmount * 0.5);
    }
    
    // Back speakers
    if (frontBalance < 0.5) {
        double backAmount = (0.5 - frontBalance) * 2.0;
        gains.backLeft = static_cast<float>((1.0 - rightBalance) * backAmount);
        gains.backRight = static_cast<float>(rightBalance * backAmount);
    }
    
    // For middle position (y ≈ 0), blend front and back
    if (std::abs(y) < 0.3) {
        double blendAmount = 1.0 - std::abs(y) / 0.3;
        gains.frontLeft = static_cast<float>(gains.frontLeft + (1.0 - rightBalance) * blendAmount * 0.5);
        gains.frontRight = static_cast<float>(gains.frontRight + rightBalance * blendAmount * 0.5);
        gains.backLeft = static_cast<float>(gains.backLeft + (1.0 - rightBalance) * blendAmount * 0.5);
        gains.backRight = static_cast<float>(gains.backRight + rightBalance * blendAmount * 0.5);
    }
    
    // Normalize
    float total = gains.frontLeft + gains.frontRight + gains.frontCenter +
                  gains.backLeft + gains.backRight;
    if (total > 0.0001f) {
        float scale = 1.0f / total;
        gains.frontLeft *= scale;
        gains.frontRight *= scale;
        gains.frontCenter *= scale;
        gains.backLeft *= scale;
        gains.backRight *= scale;
    }
}

// Calculate polar panning (rotation around user)
MultiChannelGains SmithVisualizer::calculatePolarPanning(double angleDeg, double radius) const {
    MultiChannelGains gains;
    
    // Convert angle to radians
    double angleRad = angleDeg * PI / 180.0;
    
    // Map polar coordinates to Cartesian
    double x = std::cos(angleRad) * radius;  // -1 to +1
    double y = std::sin(angleRad) * radius;  // -1 to +1
    
    // Use Cartesian panning
    return calculateCartesianPanning(x, y);
}

// Calculate impedance-based panning
MultiChannelGains SmithVisualizer::calculateImpedancePanning(double R, double X, double Z0) const {
    // Normalize impedance to -1 to +1 range
    double normR = normalizeImpedance(R, Z0, 150.0);
    double normX = std::clamp(X / 200.0, -1.0, 1.0);
    
    // Use Cartesian panning with normalized values
    return calculateCartesianPanning(normR, normX);
}

// Convert multi-channel to stereo with psychoacoustic enhancements
void SmithVisualizer::multiChannelToStereo(const MultiChannelGains& mc, float& left, float& right) const {
    // For stereo-only mode, apply psychoacoustic processing to simulate spatial audio
    // when only 2 channels are available
    
    if (audioCapability == AudioCapability::STEREO_ONLY) {
        // Enhanced stereo with psychoacoustic cues for front/back distinction
        
        // Front channels: Full brightness, slight high-frequency emphasis
        float frontL = mc.frontLeft + mc.frontCenter * 0.5f;
        float frontR = mc.frontRight + mc.frontCenter * 0.5f;
        
        // Back channels: Reduced brightness, simulated by lowpass effect
        // Apply ~0.7x gain reduction to simulate distance/occlusion
        float backL = mc.backLeft * 0.7f;
        float backR = mc.backRight * 0.7f;
        
        // Side channels: Emphasized for lateral localization
        // Strong side presence helps with left-right positioning
        float sideL = mc.sideLeft * 0.8f;
        float sideR = mc.sideRight * 0.8f;
        
        // Apply cross-feed for front/back distinction
        // Small amount of opposite channel mixed in creates subtle ILD cues
        // This helps distinguish front (more separation) from back (less separation)
        const float crossfeedAmount = 0.15f;  // 15% crossfeed for back channels
        
        // Front: High separation (minimal crossfeed)
        left = frontL + backL + sideL;
        right = frontR + backR + sideR;
        
        // Add crossfeed primarily for back channels to reduce separation
        left += backR * crossfeedAmount;
        right += backL * crossfeedAmount;
        
        // Apply subtle ITD (Inter-aural Time Difference) simulation
        // by slightly delaying the contralateral ear for side sounds
        // Note: This would require a delay buffer in a real implementation
        // For now, we apply a simple phase shift approximation via gain modulation
        
        // Normalize to prevent clipping
        float maxGain = std::max(left, right);
        if (maxGain > 1.0f) {
            left /= maxGain;
            right /= maxGain;
        }
    } else {
        // Multi-channel output: Direct mapping to appropriate channels
        // When surround sound is available, use it directly
        // TODO: Implement actual multi-channel output instead of stereo downmix
        
        // Sum left channels
        left = mc.frontLeft + mc.backLeft + mc.sideLeft * 0.7f + mc.frontCenter * 0.5f;
        
        // Sum right channels
        right = mc.frontRight + mc.backRight + mc.sideRight * 0.7f + mc.frontCenter * 0.5f;
        
        // Normalize to prevent clipping
        float maxGain = std::max(left, right);
        if (maxGain > 1.0f) {
            left /= maxGain;
            right /= maxGain;
        }
    }
}

// Generate Smith ambient audio (for TIME_DOMAIN_CUES mode)
void SmithVisualizer::generateSmithAmbientAudio(const MeasurementPoint& pt,
                                                std::vector<int16_t>& buffer,
                                                int samples) const {
    if (!smithEnabled || !smithCuesEnabled || !audioEngine) {
        return;
    }
    
    // Calculate Smith position and panning based on current mode
    MultiChannelGains gains;
    double posX = 0.0, posY = 0.0;  // For logging
    
    switch (currentMode) {
        case SmithVisualizationMode::CARTESIAN: {
            SmithPosition3D pos = calculateCartesianPosition(pt);
            gains = calculateCartesianPanning(pos.x, pos.y);
            posX = pos.x;
            posY = pos.y;
            break;
        }
        case SmithVisualizationMode::POLAR: {
            SmithPositionPolar pos = calculatePolarPosition(pt);
            gains = calculatePolarPanning(pos.angle, pos.radius);
            // Convert back for logging
            double angleRad = pos.angle * PI / 180.0;
            posX = std::cos(angleRad) * pos.radius;
            posY = std::sin(angleRad) * pos.radius;
            break;
        }
        case SmithVisualizationMode::IMPEDANCE_DIRECT: {
            SmithPosition3D pos = calculateImpedanceDirectPosition(pt);
            gains = calculateImpedancePanning(pt.R, pt.X);
            posX = pos.x;
            posY = pos.y;
            break;
        }
        case SmithVisualizationMode::SWR_CIRCLES: {
            SmithPosition3D pos = calculateSwrCirclesPosition(pt);
            gains = calculateCartesianPanning(pos.x, pos.y);
            posX = pos.x;
            posY = pos.y;
            break;
        }
        case SmithVisualizationMode::TIME_DOMAIN_CUES: {
            // For TIME_DOMAIN_CUES, we need frequency position
            // This will be 0.0 for now (caller should provide it)
            // TODO: Pass frequency position from acoustic analyzer
            SmithPosition3D pos = calculateTimeDomainCuesPosition(pt, 0.5);
            gains = calculateCartesianPanning(pos.x, pos.y);
            posX = pos.x;
            posY = pos.y;
            break;
        }
        case SmithVisualizationMode::HYBRID_MULTI: {
            SmithPosition3D pos = calculateHybridMultiPosition(pt);
            gains = calculateCartesianPanning(pos.x, pos.y);
            posX = pos.x;
            posY = pos.y;
            break;
        }
        default: {
            // Fall back to Cartesian for unknown modes
            SmithPosition3D pos = calculateCartesianPosition(pt);
            gains = calculateCartesianPanning(pos.x, pos.y);
            posX = pos.x;
            posY = pos.y;
            break;
        }
    }
    
    // Convert to stereo for current implementation
    float left, right;
    multiChannelToStereo(gains, left, right);
    
    // Log first time generation happens (for debugging)
    static bool firstLog = true;
    if (firstLog && logger) {
        std::ostringstream oss;
        oss << "Smith ambient audio generation: "
            << "Mode=" << getModeName(currentMode)
            << " | Gamma=(" << pt.s11_re << "," << pt.s11_im << ")"
            << " | Pos=(" << posX << "," << posY << ")"
            << " | Pan=(L:" << left << ",R:" << right << ")"
            << " | Vol=" << smithCuesVolume.load() << "%"
            << " | NoiseType=" << static_cast<int>(noiseType);
        logger->log("SMITH", oss.str());
        firstLog = false;
    }
    
    // Generate ambient noise with spatial cues
    // Use low-frequency noise for subtle background
    int volumePercent = smithCuesVolume.load();
    double baseVolume = volumePercent / 100.0 * 0.3;  // 30% max volume for ambient
    
    // Thread-local filtered value for pink/brown noise generation
    thread_local double filtered = 0.0;
    thread_local double brownFiltered = 0.0;
    
    // Thread-local random generator for thread safety
    thread_local std::mt19937 rng(std::random_device{}());
    thread_local std::uniform_int_distribution<int16_t> dist(-16384, 16383);
    
    // Sine wave state for SINE_WAVE mode
    thread_local double sinePhase = 0.0;
    const double sineFreq = 220.0;  // A3 note, warm and pleasant
    const double phaseIncrement = 2.0 * 3.14159265358979323846 * sineFreq / 44100.0;
    
    for (int i = 0; i < samples; i++) {
        int16_t audioSample = 0;
        
        // Generate different sound types based on noiseType
        switch (noiseType) {
            case AppConfig::SmithNoiseType::WHITE: {
                // White noise (full spectrum, brighter)
                audioSample = dist(rng);
                break;
            }
            
            case AppConfig::SmithNoiseType::PINK: {
                // Pink noise (filtered white noise, warm)
                int16_t noiseSample = dist(rng);
                filtered = filtered * 0.95 + noiseSample * 0.05;
                audioSample = static_cast<int16_t>(filtered);
                break;
            }
            
            case AppConfig::SmithNoiseType::BROWN: {
                // Brown noise (darker, low frequency emphasis)
                int16_t noiseSample = dist(rng);
                brownFiltered = brownFiltered * 0.98 + noiseSample * 0.02;
                audioSample = static_cast<int16_t>(brownFiltered);
                break;
            }
            
            case AppConfig::SmithNoiseType::SINE_WAVE: {
                // Pure sine wave (clean, musical tone)
                audioSample = static_cast<int16_t>(16384.0 * std::sin(sinePhase));
                sinePhase += phaseIncrement;
                if (sinePhase > 2.0 * 3.14159265358979323846) {
                    sinePhase -= 2.0 * 3.14159265358979323846;
                }
                break;
            }
        }
        
        // Apply spatial panning and volume
        int16_t leftSample = static_cast<int16_t>(audioSample * left * baseVolume);
        int16_t rightSample = static_cast<int16_t>(audioSample * right * baseVolume);
        
        // Mix into buffer
        buffer[i * 2 + 0] = static_cast<int16_t>(std::clamp(
            static_cast<int>(buffer[i * 2 + 0]) + leftSample, -32768, 32767));
        buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(
            static_cast<int>(buffer[i * 2 + 1]) + rightSample, -32768, 32767));
    }
}

// Check if marker should be played
bool SmithVisualizer::shouldPlayMarker(const MeasurementPoint& pt,
                                      const MeasurementPoint& prevPt,
                                      MarkerType& outType) {
    if (!markersEnabled) {
        return false;
    }
    
    outType = MarkerType::NONE;
    
    // Check for X-axis crossing (Im(Γ) = 0, resistive impedance)
    if ((prevPt.s11_im < 0 && pt.s11_im >= 0) || (prevPt.s11_im > 0 && pt.s11_im <= 0)) {
        outType = MarkerType::X_AXIS_CROSS;
        return true;
    }
    
    // Check for center proximity (good match)
    double gammaMag = calculateGammaMagnitude(pt);
    double prevGammaMag = calculateGammaMagnitude(prevPt);
    if (prevGammaMag >= 0.2 && gammaMag < 0.2) {
        outType = MarkerType::CENTER_REACHED;
        return true;
    }
    
    // Check for edge warning (poor match)
    if (prevGammaMag < 0.8 && gammaMag >= 0.8) {
        outType = MarkerType::EDGE_WARNING;
        return true;
    }
    
    // Check for quadrant change
    int currentQuadrant = -1;
    if (pt.s11_re >= 0 && pt.s11_im >= 0) currentQuadrant = 0;
    else if (pt.s11_re < 0 && pt.s11_im >= 0) currentQuadrant = 1;
    else if (pt.s11_re < 0 && pt.s11_im < 0) currentQuadrant = 2;
    else if (pt.s11_re >= 0 && pt.s11_im < 0) currentQuadrant = 3;
    
    int prevQuadrant = lastQuadrant.load();
    if (prevQuadrant != -1 && currentQuadrant != prevQuadrant) {
        outType = MarkerType::QUADRANT_CHANGE;
        lastQuadrant.store(currentQuadrant);
        return true;
    }
    lastQuadrant.store(currentQuadrant);
    
    return false;
}

// Generate marker sound
void SmithVisualizer::generateMarkerSound(MarkerType type,
                                         std::vector<int16_t>& buffer,
                                         int samples) const {
    if (type == MarkerType::NONE || !audioEngine) {
        return;
    }
    
    // Generate different sounds for different markers
    double frequency = 0.0;
    double duration = 0.05;  // 50ms default
    
    switch (type) {
        case MarkerType::X_AXIS_CROSS:
            frequency = 800.0;  // "Ding"
            break;
        case MarkerType::CENTER_REACHED:
            frequency = 1200.0;  // "Bell"
            duration = 0.1;  // Longer
            break;
        case MarkerType::EDGE_WARNING:
            frequency = 400.0;  // Low warning tone
            duration = 0.15;
            break;
        case MarkerType::QUADRANT_CHANGE:
            frequency = 600.0;  // Medium tone
            duration = 0.03;  // Short
            break;
        default:
            return;
    }
    
    // Generate simple sine wave marker
    double phase = 0.0;
    double phaseIncrement = 2.0 * PI * frequency / SAMPLE_RATE;
    int markerSamples = static_cast<int>(duration * SAMPLE_RATE);
    markerSamples = std::min(markerSamples, samples);
    
    for (int i = 0; i < markerSamples; i++) {
        // Apply envelope (fade in/out)
        double envelope = 1.0;
        if (i < markerSamples / 10) {
            envelope = static_cast<double>(i) / (markerSamples / 10);
        } else if (i > markerSamples * 9 / 10) {
            envelope = static_cast<double>(markerSamples - i) / (markerSamples / 10);
        }
        
        double sample = std::sin(phase) * envelope * 0.5;  // 50% volume
        int16_t intSample = static_cast<int16_t>(sample * 32767.0);
        
        // Stereo (centered)
        buffer[i * 2 + 0] = intSample;
        buffer[i * 2 + 1] = intSample;
        
        phase += phaseIncrement;
        if (phase >= 2.0 * PI) phase -= 2.0 * PI;
    }
}

// Get mode name
std::string SmithVisualizer::getModeName(SmithVisualizationMode mode) const {
    switch (mode) {
        case SmithVisualizationMode::CARTESIAN:
            return translation ? translation->get("SMITH_MODE_CARTESIAN") : "Cartesian";
        case SmithVisualizationMode::POLAR:
            return translation ? translation->get("SMITH_MODE_POLAR") : "Polar";
        case SmithVisualizationMode::IMPEDANCE_DIRECT:
            return translation ? translation->get("SMITH_MODE_IMPEDANCE") : "Impedance Direct";
        case SmithVisualizationMode::SWR_CIRCLES:
            return translation ? translation->get("SMITH_MODE_SWR") : "SWR Circles";
        case SmithVisualizationMode::TIME_DOMAIN_CUES:
            return translation ? translation->get("SMITH_MODE_TIME") : "Time Domain";
        case SmithVisualizationMode::HYBRID_MULTI:
            return translation ? translation->get("SMITH_MODE_HYBRID") : "Hybrid Multi";
        default:
            return "Unknown";
    }
}

std::string SmithVisualizer::getCurrentModeName() const {
    return getModeName(currentMode);
}

// Generate center pulse (reference signal for Smith chart center)
void SmithVisualizer::generateCenterPulse(std::vector<int16_t>& buffer, int samples, double deltaTimeSeconds) {
    if (!centerPulseEnabled || !audioEngine) {
        return;
    }
    
    // Update phase based on time
    centerPulsePhase += deltaTimeSeconds;
    
    // Check if we should trigger a pulse
    double interval = centerPulseIntervalSeconds;
    if (centerPulsePhase >= interval) {
        centerPulsePhase = std::fmod(centerPulsePhase, interval);
        
        // Generate a short pulse at the beginning of this buffer
        // Pulse duration defined by CENTER_PULSE_DURATION_SECONDS constant
        int pulseSamples = static_cast<int>(CENTER_PULSE_DURATION_SECONDS * SAMPLE_RATE);
        pulseSamples = std::min(pulseSamples, samples);
        
        int volumePercent = centerPulseVolume.load();
        double volume = volumePercent / 100.0 * 0.4;  // Max 40% of full scale
        
        // Thread-local random generator for noise-based waveforms
        thread_local std::mt19937 rng(std::random_device{}());
        thread_local std::uniform_int_distribution<int16_t> dist(-16384, 16383);
        
        // Generate waveform samples based on selected waveform type
        for (int i = 0; i < pulseSamples; i++) {
            // Fast attack and decay envelope
            double envelope = 1.0;
            double pos = static_cast<double>(i) / pulseSamples;
            if (pos < 0.2) {
                // Fast attack
                envelope = pos / 0.2;
            } else {
                // Decay
                envelope = 1.0 - ((pos - 0.2) / 0.8);
            }
            
            int16_t audioSample = 0;
            
            // Generate waveform based on selected type
            switch (centerPulseWaveform) {
                case AppConfig::CenterPulseWaveform::CLICK: {
                    // Filtered noise (soft click character)
                    int16_t noiseSample = dist(rng);
                    audioSample = static_cast<int16_t>(noiseSample * envelope * volume);
                    break;
                }
                
                case AppConfig::CenterPulseWaveform::SINE: {
                    // Sine wave blip (clean, musical)
                    double phase = 2.0 * PI * 440.0 * i / SAMPLE_RATE;  // A4 = 440 Hz
                    audioSample = static_cast<int16_t>(32767.0 * std::sin(phase) * envelope * volume);
                    break;
                }
                
                case AppConfig::CenterPulseWaveform::SQUARE: {
                    // Square wave blip (bright, synthetic)
                    double phase = 2.0 * PI * 440.0 * i / SAMPLE_RATE;
                    double sineValue = std::sin(phase);
                    double squareValue = (sineValue >= 0.0) ? 1.0 : -1.0;
                    audioSample = static_cast<int16_t>(32767.0 * squareValue * envelope * volume);
                    break;
                }
                
                case AppConfig::CenterPulseWaveform::TRIANGLE: {
                    // Triangle wave blip (warm, mellow)
                    double phase = std::fmod(440.0 * i / SAMPLE_RATE, 1.0);
                    double triangleValue = (phase < 0.5) ? (4.0 * phase - 1.0) : (3.0 - 4.0 * phase);
                    audioSample = static_cast<int16_t>(32767.0 * triangleValue * envelope * volume);
                    break;
                }
                
                case AppConfig::CenterPulseWaveform::SAWTOOTH: {
                    // Sawtooth wave blip (bright, rich)
                    double phase = std::fmod(440.0 * i / SAMPLE_RATE, 1.0);
                    double sawtoothValue = 2.0 * phase - 1.0;
                    audioSample = static_cast<int16_t>(32767.0 * sawtoothValue * envelope * volume);
                    break;
                }
                
                case AppConfig::CenterPulseWaveform::PULSE: {
                    // Pulse wave (sharp, electronic) - 25% duty cycle
                    double phase = std::fmod(440.0 * i / SAMPLE_RATE, 1.0);
                    double pulseValue = (phase < 0.25) ? 1.0 : -1.0;
                    audioSample = static_cast<int16_t>(32767.0 * pulseValue * envelope * volume);
                    break;
                }
            }
            
            // Center positioning: equal in all channels (phantom center)
            // For stereo: equal left and right
            // For surround: all channels at equal level for perfect center
            MultiChannelGains centerGains;
            if (audioCapability == AudioCapability::SURROUND_7_1 || 
                audioCapability == AudioCapability::SURROUND_ATMOS) {
                // 7.1: Use all speakers at equal level for perfect center
                centerGains.frontLeft = 0.25f;
                centerGains.frontRight = 0.25f;
                centerGains.frontCenter = 0.5f;  // Emphasize center channel
                centerGains.backLeft = 0.125f;
                centerGains.backRight = 0.125f;
                centerGains.sideLeft = 0.125f;
                centerGains.sideRight = 0.125f;
            } else if (audioCapability == AudioCapability::SURROUND_5_1) {
                // 5.1: Similar but no side channels
                centerGains.frontLeft = 0.25f;
                centerGains.frontRight = 0.25f;
                centerGains.frontCenter = 0.5f;
                centerGains.backLeft = 0.125f;
                centerGains.backRight = 0.125f;
            } else {
                // Stereo: Perfect phantom center
                centerGains.frontLeft = 0.5f;
                centerGains.frontRight = 0.5f;
            }
            
            // Convert to stereo (current buffer format)
            float left, right;
            multiChannelToStereo(centerGains, left, right);
            
            // Mix into buffer
            int16_t leftSample = static_cast<int16_t>(audioSample * left);
            int16_t rightSample = static_cast<int16_t>(audioSample * right);
            
            buffer[i * 2 + 0] = static_cast<int16_t>(std::clamp(
                static_cast<int>(buffer[i * 2 + 0]) + leftSample, -32768, 32767));
            buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(
                static_cast<int>(buffer[i * 2 + 1]) + rightSample, -32768, 32767));
        }
    }
}

// Detect axis crossing
bool SmithVisualizer::detectAxisCrossing(const MeasurementPoint& pt, const MeasurementPoint& prevPt,
                                        bool& isHorizontal, bool& isUpward) {
    if (!axisEventsEnabled) {
        return false;
    }
    
    // Initialize on first call
    if (!lastCrossingValid) {
        lastS11Im = prevPt.s11_im;
        lastS11Re = prevPt.s11_re;
        lastCrossingValid = true;
        return false;
    }
    
    // From the listener's acoustic perspective in the Smith space:
    // - Horizontal axis (left-right in stereo): Re(Γ) = X-axis crosses 0
    // - Vertical axis (front-back in surround): Im(Γ) = Y-axis crosses 0
    //
    // Note: This differs from Smith chart conventions where:
    // - Im(Γ) = 0 represents the resistive axis (reactance = 0)
    // - Re(Γ) = 0 represents R = R₀ (normalized resistance at characteristic impedance)
    //
    // But for acoustic navigation, we map:
    // - Re(Γ) → X (left-right spatial position)
    // - Im(Γ) → Y (front-back spatial position)
    
    // Use defined constant for debounce threshold
    const double threshold = AXIS_CROSSING_THRESHOLD;
    
    // Check horizontal axis (Re crosses 0) - listener hears left-right crossing
    if ((lastS11Re < -threshold && pt.s11_re > threshold) ||
        (lastS11Re > threshold && pt.s11_re < -threshold)) {
        isHorizontal = true;
        isUpward = (pt.s11_re > lastS11Re);  // Moving from left to right
        
        lastS11Im = pt.s11_im;
        lastS11Re = pt.s11_re;
        return true;
    }
    
    // Check vertical axis (Im crosses 0) - listener hears front-back crossing
    if ((lastS11Im < -threshold && pt.s11_im > threshold) ||
        (lastS11Im > threshold && pt.s11_im < -threshold)) {
        isHorizontal = false;
        isUpward = (pt.s11_im > lastS11Im);  // Moving from back to front
        
        lastS11Im = pt.s11_im;
        lastS11Re = pt.s11_re;
        return true;
    }
    
    // No crossing detected, update state
    lastS11Im = pt.s11_im;
    lastS11Re = pt.s11_re;
    return false;
}

// Detect axis crossing in a range (checks all points in the full dataset between startPos and endPos)
// This ensures we detect crossings even when points are skipped in smooth or dotted mode
bool SmithVisualizer::detectAxisCrossingInRange(const std::vector<MeasurementPoint>& fullData,
                                                size_t startPos, size_t endPos,
                                                bool& isHorizontal, bool& isUpward, size_t& crossingPos) {
    if (!axisEventsEnabled || fullData.empty()) {
        return false;
    }
    
    // Ensure startPos <= endPos and both are valid
    if (startPos >= fullData.size() || endPos >= fullData.size()) {
        return false;
    }
    
    // If startPos >= endPos, no range to check
    if (startPos >= endPos) {
        return false;
    }
    
    // Initialize on first call
    if (!lastCrossingValid) {
        lastS11Im = fullData[startPos].s11_im;
        lastS11Re = fullData[startPos].s11_re;
        lastCrossingValid = true;
        lastCheckedPosition = startPos;
        return false;
    }
    
    // Use defined constant for debounce threshold
    const double threshold = AXIS_CROSSING_THRESHOLD;
    
    // Check all consecutive pairs between startPos and endPos for crossings
    // This ensures we don't miss crossings when points are skipped
    // Loop from startPos+1 to endPos (inclusive) to check pairs (startPos, startPos+1), ..., (endPos-1, endPos)
    for (size_t i = startPos + 1; i <= endPos; ++i) {
        const MeasurementPoint& pt = fullData[i];
        
        // Check horizontal axis (Re crosses 0) - listener hears left-right crossing
        if ((lastS11Re < -threshold && pt.s11_re > threshold) ||
            (lastS11Re > threshold && pt.s11_re < -threshold)) {
            isHorizontal = true;
            isUpward = (pt.s11_re > lastS11Re);  // Moving from left to right
            crossingPos = i;
            
            lastS11Im = pt.s11_im;
            lastS11Re = pt.s11_re;
            lastCheckedPosition = i;
            return true;
        }
        
        // Check vertical axis (Im crosses 0) - listener hears front-back crossing
        if ((lastS11Im < -threshold && pt.s11_im > threshold) ||
            (lastS11Im > threshold && pt.s11_im < -threshold)) {
            isHorizontal = false;
            isUpward = (pt.s11_im > lastS11Im);  // Moving from back to front
            crossingPos = i;
            
            lastS11Im = pt.s11_im;
            lastS11Re = pt.s11_re;
            lastCheckedPosition = i;
            return true;
        }
        
        // Update state for next iteration
        lastS11Im = pt.s11_im;
        lastS11Re = pt.s11_re;
    }
    
    // No crossing detected, update last checked position
    lastCheckedPosition = endPos;
    return false;
}

// Generate axis crossing event sound
void SmithVisualizer::generateAxisEventSound(std::vector<int16_t>& buffer, int samples,
                                           bool isHorizontal, bool isUpward, 
                                           double posX, double posY) const {
    if (!axisEventsEnabled || !audioEngine) {
        return;
    }
    
    // Generate event sound based on selected sound type
    // Duration is now configurable via axisEventsDurationMs
    int durationMs = axisEventsDurationMs.load();
    int eventSamples = static_cast<int>((durationMs / 1000.0) * SAMPLE_RATE);
    eventSamples = std::min(eventSamples, samples);
    
    int volumePercent = axisEventsVolume.load();
    double volume = volumePercent / 100.0 * 0.6;  // Max 60% of full scale
    
    // Pitch gesture parameters
    double pitchStart, pitchEnd;
    if (isUpward) {
        // Moving upward: pitch rises quickly
        pitchStart = axisEventsPitchMinHz;
        pitchEnd = axisEventsPitchMaxHz;
    } else {
        // Moving downward: pitch falls quickly
        pitchStart = axisEventsPitchMaxHz;
        pitchEnd = axisEventsPitchMinHz;
    }
    
    // Calculate spatial position based on crossing location
    MultiChannelGains spatialGains = calculateCartesianPanning(posX, posY);
    float left, right;
    multiChannelToStereo(spatialGains, left, right);
    
    // Generate sound based on selected type
    double phase = 0.0;
    for (int i = 0; i < eventSamples; i++) {
        double progress = static_cast<double>(i) / eventSamples;
        
        // Exponential pitch sweep for more natural gesture
        double pitchFactor = std::exp(std::log(pitchEnd / pitchStart) * progress);
        double currentFreq = pitchStart * pitchFactor;
        double phaseIncrement = 2.0 * PI * currentFreq / SAMPLE_RATE;
        
        double sample = 0.0;
        double envelope = 0.0;
        
        // Generate waveform based on selected sound type
        switch (axisCrossingSound) {
            case AppConfig::AxisCrossingSound::PLUCK: {
                // Fast pluck envelope (like plucked string)
                envelope = std::exp(-4.0 * progress);
                // Mix of sine and harmonics for pluck-like character
                sample = std::sin(phase) * 0.7 +           // Fundamental
                        std::sin(phase * 2.0) * 0.2 +      // 2nd harmonic
                        std::sin(phase * 3.0) * 0.1;       // 3rd harmonic
                sample *= envelope * volume;
                break;
            }
            
            case AppConfig::AxisCrossingSound::SWEEP: {
                // Pure sine sweep (clean, directional)
                envelope = std::exp(-2.0 * progress);  // Slower decay
                sample = std::sin(phase) * envelope * volume;
                break;
            }
            
            case AppConfig::AxisCrossingSound::CHIRP: {
                // Complex chirp with harmonics (attention-grabbing)
                envelope = std::exp(-3.0 * progress);
                // Rich harmonic content
                sample = std::sin(phase) * 0.5 +           // Fundamental
                        std::sin(phase * 2.0) * 0.25 +     // 2nd harmonic
                        std::sin(phase * 3.0) * 0.15 +     // 3rd harmonic
                        std::sin(phase * 4.0) * 0.1;       // 4th harmonic
                sample *= envelope * volume;
                break;
            }
            
            case AppConfig::AxisCrossingSound::BELL: {
                // Bell-like tone (pleasant, resonant)
                envelope = std::exp(-1.5 * progress);  // Long decay like a bell
                // Inharmonic partials for bell-like character
                sample = std::sin(phase) * 0.6 +                    // Fundamental
                        std::sin(phase * 2.2) * 0.25 +             // Slightly detuned 2nd
                        std::sin(phase * 3.3) * 0.1 +              // Detuned 3rd
                        std::sin(phase * 4.5) * 0.05;              // Detuned 4th
                sample *= envelope * volume;
                break;
            }
            
            case AppConfig::AxisCrossingSound::PERCUSSION: {
                // Percussive hit (sharp, distinctive)
                // Very fast attack and decay
                if (progress < 0.1) {
                    envelope = progress / 0.1;  // Fast attack
                } else {
                    envelope = std::exp(-8.0 * (progress - 0.1));  // Very fast decay
                }
                // Noise-like character with some pitch
                thread_local std::mt19937 rng(std::random_device{}());
                thread_local std::uniform_real_distribution<double> dist(-1.0, 1.0);
                double noise = dist(rng);
                sample = (std::sin(phase) * 0.5 + noise * 0.5) * envelope * volume;
                break;
            }
        }
        
        int16_t audioSample = static_cast<int16_t>(sample * 32767.0);
        
        // Apply spatial panning
        int16_t leftSample = static_cast<int16_t>(audioSample * left);
        int16_t rightSample = static_cast<int16_t>(audioSample * right);
        
        // Mix into buffer
        buffer[i * 2 + 0] = static_cast<int16_t>(std::clamp(
            static_cast<int>(buffer[i * 2 + 0]) + leftSample, -32768, 32767));
        buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(
            static_cast<int>(buffer[i * 2 + 1]) + rightSample, -32768, 32767));
        
        phase += phaseIncrement;
        if (phase >= 2.0 * PI) phase -= 2.0 * PI;
    }
    
    if (logger && mathLogger && mathLogger->isEnabled()) {
        std::ostringstream oss;
        oss << "Axis crossing event: " 
            << (isHorizontal ? "Horizontal" : "Vertical")
            << " | Direction: " << (isUpward ? "Upward" : "Downward")
            << " | Position: (" << posX << ", " << posY << ")"
            << " | Pitch: " << pitchStart << " -> " << pitchEnd << " Hz";
        mathLogger->logDataFlow("SMITH_AXIS_EVENT", oss.str());
    }
}
