#include "audio_backend.h"
#include "../../include/logger.h"
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <queue>
#include <condition_variable>
#include <memory>
#include <cstring>
#include <cstdio>
#include <cstdarg>

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
    // waveMutex protects hWave, bufferPool, freeBuffers, currentFormat.
    // Uses std::recursive_mutex so playBuffer can call ensureWaveOpenWithLock which
    // temporarily unlocks/relocks.
    std::recursive_mutex waveMutex;
    static constexpr int NUM_BUFFERS = 3; // Triple-buffering: one playing, one ready, one being filled — minimizes queue latency
    static constexpr int BUFFER_WAIT_TIMEOUT_MS = 100; // Timeout for waiting on free buffer (shorter for lower latency)
    std::vector<std::unique_ptr<AudioBuffer>> bufferPool;
    std::queue<AudioBuffer*> freeBuffers;
    std::atomic<int> activeBufferCount{0};
    // aborted: external stop signal to break playBuffer waits (also reused during close)
    // closing: internal guard to reject playBuffer submissions while resetting/closing the device
    std::atomic<bool> aborted{false};
    std::atomic<bool> closing{false};
    // Selected device: WAVE_MAPPER (-1) = default, 0..N = specific waveOut device ID
    UINT selectedDeviceId = WAVE_MAPPER;
    // Optional logger — when set, debug output goes to the game's log file
    Logger* debugLogger = nullptr;
    AudioError lastError_ = AudioError::NONE;

    // Event signalled by WOM_DONE callback to wake threads waiting for free
    // buffers.  SetEvent is safe inside waveOut callbacks.
    HANDLE bufferDoneEvent = NULL;

    // Write a diagnostic message to the Logger (if set), otherwise discard.
    void audioLog(const char* fmt, ...) {
        if (!debugLogger) return;
        va_list args;
        va_start(args, fmt);
        char buf[512];
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        debugLogger->log("AUDIO_WIN", std::string(buf));
    }
    
    // Callback for waveOut completion — runs on the audio driver thread.
    // Per Microsoft docs, only EnterCriticalSection/LeaveCriticalSection,
    // SetEvent, PostMessage, and a few timer functions are allowed here.
    // Specifically: NO waveOut* calls (waveOutUnprepareHeader etc.) and
    // NO std::mutex/condition_variable operations.
    static void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
        if (uMsg == WOM_DONE) {
            WindowsAudioBackend* backend = reinterpret_cast<WindowsAudioBackend*>(dwInstance);
            backend->onBufferComplete(reinterpret_cast<WAVEHDR*>(dwParam1));
        }
    }
    
    // Called from the driver thread inside waveOutProc.  Must only use
    // CRITICAL_SECTION and SetEvent — nothing else.
    void onBufferComplete(WAVEHDR* pWaveHdr) {
        // The WHDR_DONE flag is already set by the driver before the callback.
        // We just signal the event so waiting threads know a buffer completed.
        // The actual unprepare + reclaim happens in reclaimCompletedBuffers()
        // called from playBuffer / closeWave / selectDevice — outside the
        // callback context where waveOut* calls are safe.
        activeBufferCount.fetch_sub(1, std::memory_order_release);
        SetEvent(bufferDoneEvent);
    }
    
    // Reclaim all completed buffers (WHDR_DONE set by driver).
    // Must be called from a normal thread context (NOT from a waveOut callback).
    // Caller must hold waveMutex.
    void reclaimCompletedBuffers(HWAVEOUT handle) {
        for (auto& buf : bufferPool) {
            if (buf->inUse && (buf->header.dwFlags & WHDR_DONE)) {
                waveOutUnprepareHeader(handle, &buf->header, sizeof(WAVEHDR));
                buf->inUse = false;
                freeBuffers.push(buf.get());
            }
        }
    }
    
    void initializeBufferPool() {
        bufferPool.clear();
        while (!freeBuffers.empty()) freeBuffers.pop();
        activeBufferCount.store(0);
        
        for (int i = 0; i < NUM_BUFFERS; i++) {
            auto buf = std::make_unique<AudioBuffer>();
            freeBuffers.push(buf.get());
            bufferPool.push_back(std::move(buf));
        }
    }
    
    void closeWave() {
        audioLog("closeWave() called");
        // IMPORTANT: waveOutReset must run without holding waveMutex to avoid
        // deadlocks with WOM_DONE callbacks. Guard handle hand-off carefully and
        // block new playBuffer submissions via the abort flag while closing.
        HWAVEOUT waveHandle = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(waveMutex);
            if (closing.load() || !hWave) {
                audioLog("closeWave: already closing or no hWave, returning early");
                return;
            }
            waveHandle = hWave;
            closing.store(true);
            aborted.store(true);
            hWave = nullptr;  // Signal other threads that handle is being closed
            audioLog("closeWave: flags set, hWave cleared");
        }
        
        audioLog("closeWave: calling waveOutReset");
        waveOutReset(waveHandle);
        audioLog("closeWave: waveOutReset returned");
        
        {
            std::lock_guard<std::recursive_mutex> lock(waveMutex);
            reclaimCompletedBuffers(waveHandle);
        }
        
        audioLog("closeWave: calling waveOutClose");
        waveOutClose(waveHandle);
        audioLog("closeWave: waveOutClose returned");
        
        {
            std::lock_guard<std::recursive_mutex> lock(waveMutex);
            ZeroMemory(&currentFormat, sizeof(currentFormat));
            initializeBufferPool();
            aborted.store(false);
            closing.store(false);
        }
        audioLog("closeWave: complete");
    }
    
    // NOTE: expects caller to pass its held lock by reference; this function
    // will temporarily unlock/relock around closeWave() to drop ALL ownership
    // counts on waveMutex before calling waveOutReset inside closeWave.
    bool ensureWaveOpenWithLock(int sampleRate, int channels, int bitsPerSample, std::unique_lock<std::recursive_mutex>& lock) {
        // Check if format changed or wave device not open
        bool formatChanged = (currentFormat.nSamplesPerSec != sampleRate ||
                            currentFormat.nChannels != channels ||
                            currentFormat.wBitsPerSample != bitsPerSample);
        
        if (hWave && formatChanged) {
            audioLog("ensureWaveOpen: format changed, closing device");
            lock.unlock();
            closeWave();
            lock.lock();
            if (closing.load() || aborted.load()) {
                audioLog("ensureWaveOpen: closing/aborted after format change close");
                return false;
            }
        }
        
        if (!hWave) {
            if (closing.load() || aborted.load()) {
                audioLog("ensureWaveOpen: closing/aborted, skipping waveOutOpen");
                return false;
            }
            WAVEFORMATEX wfx = {0};
            wfx.wFormatTag = WAVE_FORMAT_PCM;
            wfx.nChannels = channels;
            wfx.nSamplesPerSec = sampleRate;
            wfx.wBitsPerSample = bitsPerSample;
            wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels;
            wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
            
            // First, query whether the device natively supports this format.
            // If not, try common fallback rates to avoid distorted/sped-up playback
            // caused by poor driver-level sample rate conversion on some USB and
            // virtual audio devices.
            MMRESULT queryRes = waveOutOpen(nullptr, selectedDeviceId, &wfx,
                                            0, 0, WAVE_FORMAT_QUERY);
            if (queryRes != MMSYSERR_NOERROR && selectedDeviceId != WAVE_MAPPER) {
                audioLog("ensureWaveOpen: device %u does not natively support rate=%d, "
                         "querying fallback rates", selectedDeviceId, sampleRate);
                // Try common fallback rates (most USB devices support 48000 natively)
                constexpr int fallbackRates[] = { 48000, 44100, 22050, 16000 };
                for (int rate : fallbackRates) {
                    if (rate == sampleRate) continue; // already tried
                    WAVEFORMATEX tryFmt = wfx;
                    tryFmt.nSamplesPerSec = rate;
                    tryFmt.nBlockAlign = (tryFmt.wBitsPerSample / 8) * tryFmt.nChannels;
                    tryFmt.nAvgBytesPerSec = tryFmt.nSamplesPerSec * tryFmt.nBlockAlign;
                    if (waveOutOpen(nullptr, selectedDeviceId, &tryFmt, 0, 0, WAVE_FORMAT_QUERY) == MMSYSERR_NOERROR) {
                        audioLog("ensureWaveOpen: device %u supports rate=%d natively, "
                                 "waveOut will convert from %d — some pitch artifacts may "
                                 "occur on devices with poor driver-level resampling",
                                 selectedDeviceId, rate, sampleRate);
                        break;
                    }
                }
            }

            audioLog("ensureWaveOpen: calling waveOutOpen(device=%u, rate=%d, ch=%d, bits=%d)",
                     selectedDeviceId, sampleRate, channels, bitsPerSample);
            MMRESULT res = waveOutOpen(&hWave, selectedDeviceId, &wfx, 
                                      reinterpret_cast<DWORD_PTR>(waveOutProc), 
                                      reinterpret_cast<DWORD_PTR>(this), 
                                      CALLBACK_FUNCTION);
            if (res != MMSYSERR_NOERROR) {
                audioLog("ensureWaveOpen: waveOutOpen FAILED with MMRESULT=%u", res);
                hWave = NULL;
                return false;
            }
            audioLog("ensureWaveOpen: waveOutOpen succeeded");
            
            currentFormat = wfx;
            initializeBufferPool();
        }
        
        return true;
    }
    
public:
    WindowsAudioBackend() {
        bufferDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        initializeBufferPool();
    }
    
    ~WindowsAudioBackend() override {
        shutdown();
        if (bufferDoneEvent) CloseHandle(bufferDoneEvent);
    }
    
    bool initialize() override {
        return true;
    }
    
    void shutdown() override {
        closeWave();
    }
    
    void abort() override {
        aborted.store(true);
        SetEvent(bufferDoneEvent);
    }
    
    void resetAbort() override {
        aborted.store(false);
    }

    void setLogger(Logger* logger) override {
        debugLogger = logger;
    }
    
    bool playBuffer(
        const int16_t* buffer,
        int samples,
        int sampleRate,
        int channels,
        int bitsPerSample) override 
    {
        if (!buffer || samples <= 0 || aborted.load() || closing.load()) {
            return false;
        }
        
        std::unique_lock<std::recursive_mutex> lock(waveMutex);
        
        // Note: No idle timeout here. Device is closed explicitly via shutdown()/abort().
        // The previous idle-timeout closeWave() called waveOutReset() while holding the lock,
        // which could block indefinitely on some Windows audio drivers, hanging the entire
        // audio thread and subsequently the program.
        
        // Ensure wave device is open with correct format
        if (!ensureWaveOpenWithLock(sampleRate, channels, bitsPerSample, lock)) {
            return false;
        }
        
        // Reclaim any completed buffers (unprepare headers outside callback context)
        if (hWave) {
            reclaimCompletedBuffers(hWave);
        }
        
        // Wait for a free buffer if all are in use
        if (freeBuffers.empty()) {
            // Release waveMutex while waiting so WOM_DONE callbacks and other
            // threads aren't blocked.  Use the Win32 event set by the callback.
            lock.unlock();
            WaitForSingleObject(bufferDoneEvent, BUFFER_WAIT_TIMEOUT_MS);
            lock.lock();
            
            // Reclaim buffers that completed while we waited
            if (hWave) {
                reclaimCompletedBuffers(hWave);
            }
            
            // If still no buffer available or aborted, give up
            if (freeBuffers.empty() || aborted.load()) {
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
            // Device may have become invalid — close it so ensureWaveOpen will reopen it next call
            if (res == MMSYSERR_INVALHANDLE || res == MMSYSERR_NODRIVER) {
                lastError_ = AudioError::FATAL;
                lock.unlock();
                closeWave();
                lock.lock();
            } else {
                lastError_ = AudioError::TRANSIENT;
            }
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
            // Device may have become invalid — close so it gets reopened
            if (res == MMSYSERR_INVALHANDLE || res == MMSYSERR_NODRIVER) {
                lastError_ = AudioError::FATAL;
                lock.unlock();
                closeWave();
                lock.lock();
            } else {
                lastError_ = AudioError::TRANSIENT;
            }
            return false;
        }
        
        lastError_ = AudioError::NONE;
        return true;
    }
    
    AudioError getLastError() const override { return lastError_; }
    
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

    std::vector<AudioOutputDevice> enumerateDevices() override {
        std::vector<AudioOutputDevice> devices;
        UINT numDevs = waveOutGetNumDevs();
        for (UINT i = 0; i < numDevs; i++) {
            WAVEOUTCAPSA caps = {};
            if (waveOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
                AudioOutputDevice dev;
                dev.index = static_cast<int>(i);
                dev.name = caps.szPname;
                dev.maxChannels = caps.wChannels;
                dev.defaultSampleRate = 44100;
                dev.isDefault = (i == 0);  // First device is typically the default mapper target
                devices.push_back(dev);
            }
        }
        if (devices.empty()) {
            // Fallback: return default device entry
            AudioOutputDevice def;
            def.index = -1;
            def.name = "Default Audio Device";
            def.maxChannels = detectMaxChannels();
            def.defaultSampleRate = 44100;
            def.isDefault = true;
            devices.push_back(def);
        }
        return devices;
    }

    bool selectDevice(int deviceIndex) override {
        audioLog("selectDevice(%d) called", deviceIndex);
        // Atomically set the new device ID AND prepare for close in one lock
        // acquisition.  Previously, the device ID was set under one lock and
        // closeWave() re-acquired the lock separately.  The audio thread could
        // race between those two acquisitions — entering playBuffer/wait_for
        // and creating contention with WOM_DONE callbacks during waveOutReset,
        // which on some Windows audio drivers causes waveOutReset to block
        // indefinitely (priority inversion or driver-internal serialization).
        HWAVEOUT waveHandle = nullptr;
        {
            audioLog("selectDevice: acquiring waveMutex for flag setup");
            std::lock_guard<std::recursive_mutex> lock(waveMutex);
            if (deviceIndex < 0) {
                selectedDeviceId = WAVE_MAPPER;
            } else {
                selectedDeviceId = static_cast<UINT>(deviceIndex);
            }
            if (hWave) {
                waveHandle = hWave;
                hWave = nullptr;
                closing.store(true);
                aborted.store(true);
                audioLog("selectDevice: flags set, old handle saved, hWave cleared");
            } else {
                audioLog("selectDevice: no wave device was open");
            }
        }

        if (!waveHandle) {
            audioLog("selectDevice: no device to close, returning true");
            return true;  // No device was open; nothing to close
        }

        // Wake any audio-thread waits so they see aborted=true promptly
        audioLog("selectDevice: signalling event and notifying");
        SetEvent(bufferDoneEvent);

        // Synchronization barrier: the audio thread might still be inside
        // playBuffer (it passed the pre-lock aborted/closing check before
        // we set the flags).  Acquiring and releasing the lock guarantees
        // that any such in-flight call has completed.  After release,
        // closing=true prevents re-entry.
        audioLog("selectDevice: sync barrier — acquiring waveMutex");
        {
            std::lock_guard<std::recursive_mutex> lock(waveMutex);
        }
        audioLog("selectDevice: sync barrier complete");

        // Now no thread holds waveMutex and no thread will acquire it.
        // The WOM_DONE callback no longer calls any waveOut functions
        // (it only decrements a counter and sets an event), so
        // waveOutReset is safe and will not deadlock.
        audioLog("selectDevice: calling waveOutReset");
        waveOutReset(waveHandle);
        audioLog("selectDevice: waveOutReset returned");

        {
            std::lock_guard<std::recursive_mutex> lock(waveMutex);
            reclaimCompletedBuffers(waveHandle);
        }

        audioLog("selectDevice: calling waveOutClose");
        waveOutClose(waveHandle);
        audioLog("selectDevice: waveOutClose returned");

        {
            std::lock_guard<std::recursive_mutex> lock(waveMutex);
            ZeroMemory(&currentFormat, sizeof(currentFormat));
            initializeBufferPool();
            aborted.store(false);
            closing.store(false);
        }

        audioLog("selectDevice: complete, returning true");
        return true;
    }
};

// Factory function implementation
IAudioBackend* createAudioBackend() {
    return new WindowsAudioBackend();
}
