#pragma once
#include "waveform.h"
#include <vector>
#include <string>
#include <mutex>

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool open();
    void close();

    void playSequence(const std::vector<double>& yValues, uint64_t startFreq, uint64_t endFreq, Waveform wf);
    void playTone(double pitchHz, double panL, double panR, Waveform wf, int msDuration);

private:
    std::mutex mtx;
    bool opened = false;
    int sampleRate = 44100;
    int channels = 2;
    int bits = 16;

    void synthAndPlay(double freqHz, double panL, double panR, Waveform wf, int msDuration);
    double waveformSample(double t, Waveform wf);
};