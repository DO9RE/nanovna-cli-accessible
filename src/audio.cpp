#include "audio.h"
#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <cmath>
#include <random>
#include <ctime>

#pragma comment(lib, "winmm.lib")

static constexpr double PI_CONST = 3.14159265358979323846;

AudioEngine::AudioEngine() {}
AudioEngine::~AudioEngine() { close(); }

bool AudioEngine::open() {
    std::lock_guard<std::mutex> l(mtx);
    opened = true;
    return true;
}

void AudioEngine::close() {
    std::lock_guard<std::mutex> l(mtx);
    opened = false;
}

double AudioEngine::waveformSample(double t, Waveform wf) {
    switch (wf) {
        case Waveform::SINE:
            return std::sin(2.0 * PI_CONST * t);
        case Waveform::SQUARE:
            return (std::sin(2.0 * PI_CONST * t) >= 0.0) ? 1.0 : -1.0;
        case Waveform::TRIANGLE: {
            double v = 2.0 * fabs(2.0 * (t - floor(t + 0.5))) - 1.0;
            return v;
        }
        case Waveform::SAWTOOTH:
            // Rising sawtooth waveform (ramp from -1 to +1)
            return 2.0 * (t - floor(t)) - 1.0;
        case Waveform::SAWTOOTH_INV:
            // Falling sawtooth waveform (ramp from +1 to -1)
            return 1.0 - 2.0 * (t - floor(t));
        case Waveform::PULSE:
            // Pulse wave with 25% duty cycle
            return ((t - floor(t)) < 0.25) ? 1.0 : -1.0;
    }
    return 0.0;
}

void AudioEngine::synthAndPlay(double freqHz, double panL, double panR, Waveform wf, int msDuration) {
    if (!opened) return;
    const int samples = (int)(sampleRate * (msDuration / 1000.0));
    if (samples <= 0) return;
    std::vector<int16_t> buffer(samples * channels);
    double phase = 0.0;
    double phaseInc = freqHz / sampleRate;
    for (int i = 0; i < samples; ++i) {
        double t = phase;
        double s = waveformSample(t, wf);
        phase += phaseInc;
        if (phase >= 1.0) phase -= 1.0;
        double left = s * panL;
        double right = s * panR;
        int16_t li = (int16_t)std::lround(left * 30000.0);
        int16_t ri = (int16_t)std::lround(right * 30000.0);
        buffer[i*2+0] = li;
        buffer[i*2+1] = ri;
    }

    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = channels;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = bits;
    wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    HWAVEOUT hWave = NULL;
    MMRESULT res = waveOutOpen(&hWave, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) return;

    WAVEHDR hdr = {0};
    hdr.lpData = (LPSTR)buffer.data();
    hdr.dwBufferLength = (DWORD)(buffer.size() * sizeof(int16_t));
    hdr.dwFlags = 0;
    waveOutPrepareHeader(hWave, &hdr, sizeof(hdr));
    waveOutWrite(hWave, &hdr, sizeof(hdr));
    while (!(hdr.dwFlags & WHDR_DONE)) {
        Sleep(5);
    }
    waveOutUnprepareHeader(hWave, &hdr, sizeof(hdr));
    waveOutClose(hWave);
}

void AudioEngine::playTone(double pitchHz, double panL, double panR, Waveform wf, int msDuration) {
    synthAndPlay(pitchHz, panL, panR, wf, msDuration);
}

void AudioEngine::playSequence(const std::vector<double>& yValues, uint64_t startFreq, uint64_t endFreq, Waveform wf) {
    if (!opened) open();
    size_t n = yValues.size();
    if (n == 0) return;
    for (size_t i=0;i<n;i++) {
        double frac = (double)i / (double)(n-1);
        double panL = 1.0 - frac;
        double panR = frac;
        double y = yValues[i];
        double minY = 1.0, maxY = 10.0;
        if (y < minY) y = minY;
        if (y > maxY) y = maxY;
        double pitch = 400.0 + ((y - minY)/(maxY - minY)) * (2200.0 - 400.0);
        playTone(pitch, panL, panR, wf, 30);
    }
}