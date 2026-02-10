#include "audio.h"
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <vector>
#include <cmath>
#include <random>
#include <ctime>
#include <sstream>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")

static constexpr double PI_CONST = 3.14159265358979323846;
static constexpr double AUDIO_AMPLITUDE_SCALE = 30000.0;  // Amplitude scaling for 16-bit audio

AudioEngine::AudioEngine() {}
AudioEngine::~AudioEngine() { close(); }

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
        int16_t li = (int16_t)std::lround(left * AUDIO_AMPLITUDE_SCALE);
        int16_t ri = (int16_t)std::lround(right * AUDIO_AMPLITUDE_SCALE);
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

// Detect maximum number of audio channels supported by hardware
int AudioEngine::detectMaxChannels() {
    int maxChannels = 2; // Default to stereo
    
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
                        maxChannels = pWaveFormat->nChannels;
                        
                        if (logger) {
                            std::ostringstream oss;
                            oss << "Audio device: " << pWaveFormat->nChannels << " channels, "
                                << pWaveFormat->nSamplesPerSec << " Hz, "
                                << pWaveFormat->wBitsPerSample << " bits";
                            logger->log("AUDIO", oss.str());
                            
                            // Log channel configuration
                            if (pWaveFormat->nChannels == 2) {
                                logger->log("AUDIO", "Configuration: STEREO (2.0)");
                            } else if (pWaveFormat->nChannels == 6) {
                                logger->log("AUDIO", "Configuration: SURROUND 5.1 detected");
                            } else if (pWaveFormat->nChannels == 8) {
                                logger->log("AUDIO", "Configuration: SURROUND 7.1 detected");
                            } else {
                                std::ostringstream oss2;
                                oss2 << "Configuration: " << pWaveFormat->nChannels << " channels";
                                logger->log("AUDIO", oss2.str());
                            }
                        }
                        
                        CoTaskMemFree(pWaveFormat);
                    } else if (logger) {
                        logger->log("AUDIO", "Failed to get audio mix format, defaulting to stereo");
                    }
                    
                    pAudioClient->Release();
                } else if (logger) {
                    logger->log("AUDIO", "Failed to activate audio client, defaulting to stereo");
                }
                
                pDevice->Release();
            } else if (logger) {
                logger->log("AUDIO", "Failed to get default audio endpoint, defaulting to stereo");
            }
            
            pEnumerator->Release();
        } else if (logger) {
            logger->log("AUDIO", "Failed to create device enumerator, defaulting to stereo");
        }
        
        if (comInitialized) {
            CoUninitialize();
        }
    } else if (logger) {
        logger->log("AUDIO", "Failed to initialize COM for audio detection, defaulting to stereo");
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
    if (!opened) return;
    
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
        double t = phase;
        double s = waveformSample(t, wf);
        phase += phaseInc;
        if (phase >= 1.0) phase -= 1.0;
        
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

    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = channels;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = bits;
    wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    HWAVEOUT hWave = NULL;
    MMRESULT res = waveOutOpen(&hWave, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    
    if (res != MMSYSERR_NOERROR) {
        if (logger) {
            std::ostringstream oss;
            oss << "waveOutOpen failed with error " << res << " for " << channels << " channels";
            logger->log("AUDIO", oss.str());
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
        return;
    }

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