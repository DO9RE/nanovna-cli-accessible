#include "audio_backend.h"
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>
#include <queue>
#include <condition_variable>
#include <memory>
#include <cstring>

/**
 * Windows audio backend using waveOut API with buffer queue
 * Implements non-blocking playback to eliminate audio gaps and crackling in smooth playback mode
 */

// Structure to hold buffer data and header
struct AudioBuffer {
    std::vector<int16_t> data;
    WAVEHDR header;
    bool inUse = false;
};

class WindowsAudioBackend : public IAudioBackend {
private:
    HWAVEOUT hWave = NULL;
    WAVEFORMATEX currentFormat = {0};
    std::mutex waveMutex;
    std::condition_variable bufferAvailableCV;
    std::chrono::steady_clock::time_point lastPlayTime;
    static constexpr int NUM_BUFFERS = 3; // Number of buffers in the queue
    static constexpr int BUFFER_WAIT_TIMEOUT_MS = 100; // Timeout for waiting on free buffer
    std::vector<std::unique_ptr<AudioBuffer>> bufferPool;
    std::queue<AudioBuffer*> freeBuffers;
    int activeBufferCount = 0;
    
    // Callback for waveOut completion
    static void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
        if (uMsg == WOM_DONE) {
            WindowsAudioBackend* backend = reinterpret_cast<WindowsAudioBackend*>(dwInstance);
            WAVEHDR* pWaveHdr = reinterpret_cast<WAVEHDR*>(dwParam1);
            backend->onBufferComplete(pWaveHdr);
        }
    }
    
    void onBufferComplete(WAVEHDR* pWaveHdr) {
        std::lock_guard<std::mutex> lock(waveMutex);
        
        // Find the buffer and mark it as free
        for (auto& buf : bufferPool) {
            if (&buf->header == pWaveHdr) {
                waveOutUnprepareHeader(hWave, &buf->header, sizeof(WAVEHDR));
                buf->inUse = false;
                freeBuffers.push(buf.get());
                activeBufferCount--;
                bufferAvailableCV.notify_one();
                break;
            }
        }
    }
    
    void initializeBufferPool() {
        bufferPool.clear();
        while (!freeBuffers.empty()) freeBuffers.pop();
        activeBufferCount = 0;
        
        for (int i = 0; i < NUM_BUFFERS; i++) {
            auto buf = std::make_unique<AudioBuffer>();
            freeBuffers.push(buf.get());
            bufferPool.push_back(std::move(buf));
        }
    }
    
    void closeWave() {
        if (hWave) {
            // Reset all pending buffers - this marks them as done and triggers 
            // the completion callback for each, allowing us to safely unprepare them
            waveOutReset(hWave);
            
            // Unprepare any prepared headers
            for (auto& buf : bufferPool) {
                if (buf->inUse) {
                    waveOutUnprepareHeader(hWave, &buf->header, sizeof(WAVEHDR));
                    buf->inUse = false;
                }
            }
            
            waveOutClose(hWave);
            hWave = NULL;
            ZeroMemory(&currentFormat, sizeof(currentFormat));
            initializeBufferPool();
        }
    }
    
    bool ensureWaveOpen(int sampleRate, int channels, int bitsPerSample) {
        // Check if format changed or wave device not open
        bool formatChanged = (currentFormat.nSamplesPerSec != sampleRate ||
                            currentFormat.nChannels != channels ||
                            currentFormat.wBitsPerSample != bitsPerSample);
        
        if (hWave && formatChanged) {
            closeWave();
        }
        
        if (!hWave) {
            WAVEFORMATEX wfx = {0};
            wfx.wFormatTag = WAVE_FORMAT_PCM;
            wfx.nChannels = channels;
            wfx.nSamplesPerSec = sampleRate;
            wfx.wBitsPerSample = bitsPerSample;
            wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels;
            wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
            
            // Use callback for asynchronous buffer completion notification
            MMRESULT res = waveOutOpen(&hWave, WAVE_MAPPER, &wfx, 
                                      reinterpret_cast<DWORD_PTR>(waveOutProc), 
                                      reinterpret_cast<DWORD_PTR>(this), 
                                      CALLBACK_FUNCTION);
            if (res != MMSYSERR_NOERROR) {
                hWave = NULL;
                return false;
            }
            
            currentFormat = wfx;
            initializeBufferPool();
        }
        
        return true;
    }
    
public:
    WindowsAudioBackend() {
        initializeBufferPool();
    }
    
    ~WindowsAudioBackend() override {
        shutdown();
    }
    
    bool initialize() override {
        lastPlayTime = std::chrono::steady_clock::now();
        return true;
    }
    
    void shutdown() override {
        std::lock_guard<std::mutex> lock(waveMutex);
        closeWave();
    }
    
    bool playBuffer(
        const int16_t* buffer,
        int samples,
        int sampleRate,
        int channels,
        int bitsPerSample) override 
    {
        if (!buffer || samples <= 0) {
            return false;
        }
        
        std::unique_lock<std::mutex> lock(waveMutex);
        
        // Check if wave device has been idle and should be closed
        auto now = std::chrono::steady_clock::now();
        auto idleTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPlayTime).count();
        if (idleTime > AUDIO_IDLE_TIMEOUT_MS && hWave && activeBufferCount == 0) {
            closeWave();
        }
        
        // Ensure wave device is open with correct format
        if (!ensureWaveOpen(sampleRate, channels, bitsPerSample)) {
            return false;
        }
        
        // Wait for a free buffer if all are in use
        // Use a timeout to prevent deadlock
        if (freeBuffers.empty()) {
            bufferAvailableCV.wait_for(lock, std::chrono::milliseconds(BUFFER_WAIT_TIMEOUT_MS), 
                [this]{ return !freeBuffers.empty(); });
            
            // If still no buffer available, something is wrong
            if (freeBuffers.empty()) {
                return false;
            }
        }
        
        // Get a free buffer
        AudioBuffer* buf = freeBuffers.front();
        freeBuffers.pop();
        
        // Copy data to buffer
        size_t dataSize = samples * channels;
        buf->data.resize(dataSize);
        std::memcpy(buf->data.data(), buffer, dataSize * sizeof(int16_t));
        
        // Prepare wave header
        ZeroMemory(&buf->header, sizeof(WAVEHDR));
        buf->header.lpData = reinterpret_cast<LPSTR>(buf->data.data());
        buf->header.dwBufferLength = static_cast<DWORD>(dataSize * sizeof(int16_t));
        buf->header.dwFlags = 0;
        
        MMRESULT res = waveOutPrepareHeader(hWave, &buf->header, sizeof(WAVEHDR));
        if (res != MMSYSERR_NOERROR) {
            freeBuffers.push(buf);
            return false;
        }
        
        // Submit buffer for playback (non-blocking)
        buf->inUse = true;
        activeBufferCount++;
        res = waveOutWrite(hWave, &buf->header, sizeof(WAVEHDR));
        if (res != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(hWave, &buf->header, sizeof(WAVEHDR));
            buf->inUse = false;
            activeBufferCount--;
            freeBuffers.push(buf);
            return false;
        }
        
        lastPlayTime = now;
        
        return true;
    }
    
    int detectMaxChannels() override {
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
        
        return maxChannels;
    }
};

// Factory function implementation
IAudioBackend* createAudioBackend() {
    return new WindowsAudioBackend();
}
