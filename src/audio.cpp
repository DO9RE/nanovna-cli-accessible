#include "audio.h"
#include "audio_backend.h"
#include "waveform_utils.h"
#include <vector>
#include <cmath>
#include <random>
#include <ctime>
#include <sstream>
#include <chrono>
#include <thread>

using namespace WaveformUtils;

AudioEngine::AudioEngine() : backend(createAudioBackend()) {
    if (backend) {
        backend->initialize();
    }
}

AudioEngine::~AudioEngine() { 
    close();
    if (backend) {
        backend->shutdown();
        delete backend;
        backend = nullptr;
    }
}

bool AudioEngine::open() {
    std::lock_guard<std::mutex> l(mtx);
    opened = true;
    
    // Detect available channels
    int maxChannels = detectMaxChannels();
    if (logger) {
        std::ostringstream oss;
        oss << "Audio hardware detected: " << maxChannels << " channels available";
        logger->log("AUDIO", oss.str());
    }
    
    return true;
}

void AudioEngine::close() {
    std::lock_guard<std::mutex> l(mtx);
    opened = false;
}

void AudioEngine::synthAndPlay(double freqHz, double panL, double panR, Waveform wf, int msDuration) {
    if (!opened || !backend) return;
    const int samples = (int)(sampleRate * (msDuration / 1000.0));
    if (samples <= 0) return;
    std::vector<int16_t> buffer(samples * channels);
    double phase = 0.0;
    double phaseInc = freqHz / sampleRate;
    for (int i = 0; i < samples; ++i) {
        double s = generateWaveformSample(phase, wf);
        phase = advancePhase(phase, phaseInc);
        double left = s * panL;
        double right = s * panR;
        int16_t li = (int16_t)std::lround(left * AUDIO_AMPLITUDE_SCALE);
        int16_t ri = (int16_t)std::lround(right * AUDIO_AMPLITUDE_SCALE);
        writeStereoSample(buffer, i, li, ri);
    }

    // Use platform audio backend
    backend->playBuffer(buffer.data(), samples, sampleRate, channels, bits);
}

void AudioEngine::playTone(double pitchHz, double panL, double panR, Waveform wf, int msDuration) {
    synthAndPlay(pitchHz, panL, panR, wf, msDuration);
}

// Detect maximum number of audio channels supported by hardware
int AudioEngine::detectMaxChannels() {
    if (!backend) {
        return 2; // Default to stereo if no backend
    }
    
    int maxChannels = backend->detectMaxChannels();
    
    if (logger) {
        std::ostringstream oss;
        oss << "Audio hardware detected: " << maxChannels << " channels available";
        logger->log("AUDIO", oss.str());
        
        // Log channel configuration
        if (maxChannels == 2) {
            logger->log("AUDIO", "Configuration: STEREO (2.0)");
        } else if (maxChannels == 6) {
            logger->log("AUDIO", "Configuration: SURROUND 5.1 detected");
        } else if (maxChannels == 8) {
            logger->log("AUDIO", "Configuration: SURROUND 7.1 detected");
        } else {
            std::ostringstream oss2;
            oss2 << "Configuration: " << maxChannels << " channels";
            logger->log("AUDIO", oss2.str());
        }
    }
    
    return maxChannels;
}

// Set channel count for audio output
void AudioEngine::setChannelCount(int numChannels) {
    std::lock_guard<std::mutex> l(mtx);
    
    // Validate channel count (2, 6, or 8)
    if (numChannels == 2 || numChannels == 6 || numChannels == 8) {
        channels = numChannels;
        
        if (logger) {
            std::ostringstream oss;
            oss << "Audio engine configured for " << channels << " channels";
            logger->log("AUDIO", oss.str());
        }
    } else {
        if (logger) {
            std::ostringstream oss;
            oss << "Invalid channel count " << numChannels << ", keeping " << channels;
            logger->log("AUDIO", oss.str());
        }
    }
}

// Play tone with multi-channel gains (7.1 surround)
void AudioEngine::playToneMultiChannel(double pitchHz, const AudioMultiChannelGains& gains, Waveform wf, int msDuration) {
    if (channels > 2) {
        synthAndPlayMultiChannel(pitchHz, gains, wf, msDuration);
    } else {
        // Fallback to stereo: sum front and side channels
        float left = gains.frontLeft + gains.sideLeft + gains.backLeft;
        float right = gains.frontRight + gains.sideRight + gains.backRight;
        
        // Normalize
        float maxGain = std::max(left, right);
        if (maxGain > 0.0001f) {
            left /= maxGain;
            right /= maxGain;
        }
        
        synthAndPlay(pitchHz, left, right, wf, msDuration);
    }
}

// Multi-channel synthesis and playback
void AudioEngine::synthAndPlayMultiChannel(double freqHz, const AudioMultiChannelGains& gains, Waveform wf, int msDuration) {
    if (!opened || !backend) return;
    
    const int samples = (int)(sampleRate * (msDuration / 1000.0));
    if (samples <= 0) return;
    
    std::vector<int16_t> buffer(samples * channels);
    double phase = 0.0;
    double phaseInc = freqHz / sampleRate;
    
    // Log multi-channel audio calculation if math logger is available
    if (mathLogger && mathLogger->isEnabled()) {
        std::ostringstream oss;
        oss << "Multi-channel audio synthesis: freq=" << freqHz << "Hz, channels=" << channels 
            << ", duration=" << msDuration << "ms";
        mathLogger->logDataFlow("AUDIO_SYNTHESIS", oss.str());
        
        std::ostringstream gains_str;
        gains_str << "Channel gains: FL=" << gains.frontLeft << ", FR=" << gains.frontRight
                  << ", FC=" << gains.frontCenter << ", BL=" << gains.backLeft
                  << ", BR=" << gains.backRight << ", SL=" << gains.sideLeft
                  << ", SR=" << gains.sideRight;
        mathLogger->logDataFlow("AUDIO_PANNING", gains_str.str());
    }
    
    for (int i = 0; i < samples; ++i) {
        double s = generateWaveformSample(phase, wf);
        phase = advancePhase(phase, phaseInc);
        
        if (channels == 8) {
            // 7.1 surround: FL, FR, FC, LFE, BL, BR, SL, SR
            buffer[i*8+0] = (int16_t)std::lround(s * gains.frontLeft * AUDIO_AMPLITUDE_SCALE);
            buffer[i*8+1] = (int16_t)std::lround(s * gains.frontRight * AUDIO_AMPLITUDE_SCALE);
            buffer[i*8+2] = (int16_t)std::lround(s * gains.frontCenter * AUDIO_AMPLITUDE_SCALE);
            buffer[i*8+3] = (int16_t)std::lround(s * gains.lfe * AUDIO_AMPLITUDE_SCALE);
            buffer[i*8+4] = (int16_t)std::lround(s * gains.backLeft * AUDIO_AMPLITUDE_SCALE);
            buffer[i*8+5] = (int16_t)std::lround(s * gains.backRight * AUDIO_AMPLITUDE_SCALE);
            buffer[i*8+6] = (int16_t)std::lround(s * gains.sideLeft * AUDIO_AMPLITUDE_SCALE);
            buffer[i*8+7] = (int16_t)std::lround(s * gains.sideRight * AUDIO_AMPLITUDE_SCALE);
        } else if (channels == 6) {
            // 5.1 surround: FL, FR, FC, LFE, BL, BR
            buffer[i*6+0] = (int16_t)std::lround(s * gains.frontLeft * AUDIO_AMPLITUDE_SCALE);
            buffer[i*6+1] = (int16_t)std::lround(s * gains.frontRight * AUDIO_AMPLITUDE_SCALE);
            buffer[i*6+2] = (int16_t)std::lround(s * gains.frontCenter * AUDIO_AMPLITUDE_SCALE);
            buffer[i*6+3] = (int16_t)std::lround(s * gains.lfe * AUDIO_AMPLITUDE_SCALE);
            buffer[i*6+4] = (int16_t)std::lround(s * (gains.backLeft + gains.sideLeft) * AUDIO_AMPLITUDE_SCALE);
            buffer[i*6+5] = (int16_t)std::lround(s * (gains.backRight + gains.sideRight) * AUDIO_AMPLITUDE_SCALE);
        }
    }

    // Use platform audio backend
    bool success = backend->playBuffer(buffer.data(), samples, sampleRate, channels, bits);
    
    if (!success) {
        if (logger) {
            logger->log("AUDIO", "Audio playback failed for " + std::to_string(channels) + " channels");
            logger->log("AUDIO", "Falling back to stereo mode");
        }
        
        // Fallback to stereo if multi-channel fails
        if (channels > 2) {
            float left = gains.frontLeft + gains.sideLeft + gains.backLeft;
            float right = gains.frontRight + gains.sideRight + gains.backRight;
            float maxGain = std::max(left, right);
            if (maxGain > 0.0001f) {
                left /= maxGain;
                right /= maxGain;
            }
            
            int oldChannels = channels;
            channels = 2;
            synthAndPlay(freqHz, left, right, wf, msDuration);
            channels = oldChannels;
        }
    }
}

void AudioEngine::playSequence(const std::vector<double>& yValues, uint64_t startFreq, uint64_t endFreq, Waveform wf) {
    if (!opened) open();
    size_t n = yValues.size();
    if (n == 0) return;
    for (size_t i=0;i<n;i++) {
        double frac = (double)i / (double)(n-1);
        double panL, panR;
        linearPan(frac, panL, panR);
        double y = yValues[i];
        double minY = 1.0, maxY = 10.0;
        if (y < minY) y = minY;
        if (y > maxY) y = maxY;
        double pitch = 400.0 + ((y - minY)/(maxY - minY)) * (2200.0 - 400.0);
        playTone(pitch, panL, panR, wf, 30);
    }
}