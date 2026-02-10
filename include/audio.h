#pragma once
#include "waveform.h"
#include "logger.h"
#include "math_logger.h"
#include <vector>
#include <string>
#include <mutex>

// Multi-channel audio gains for 7.1 surround
struct AudioMultiChannelGains {
    float frontLeft = 0.0f;
    float frontRight = 0.0f;
    float frontCenter = 0.0f;
    float lfe = 0.0f;          // Low Frequency Effects (subwoofer)
    float backLeft = 0.0f;
    float backRight = 0.0f;
    float sideLeft = 0.0f;
    float sideRight = 0.0f;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool open();
    void close();
    
    // Set loggers for debug output
    void setLogger(Logger* log) { logger = log; }
    void setMathLogger(MathLogger* mathLog) { mathLogger = mathLog; }

    void playSequence(const std::vector<double>& yValues, uint64_t startFreq, uint64_t endFreq, Waveform wf);
    void playTone(double pitchHz, double panL, double panR, Waveform wf, int msDuration);
    
    // Multi-channel audio support
    void playToneMultiChannel(double pitchHz, const AudioMultiChannelGains& gains, Waveform wf, int msDuration);
    
    // Query audio hardware capabilities
    int detectMaxChannels();
    int getCurrentChannelCount() const { return channels; }
    void setChannelCount(int numChannels);

private:
    std::mutex mtx;
    bool opened = false;
    int sampleRate = 44100;
    int channels = 2;
    int bits = 16;
    Logger* logger = nullptr;
    MathLogger* mathLogger = nullptr;

    void synthAndPlay(double freqHz, double panL, double panR, Waveform wf, int msDuration);
    void synthAndPlayMultiChannel(double freqHz, const AudioMultiChannelGains& gains, Waveform wf, int msDuration);
    double waveformSample(double t, Waveform wf);
};