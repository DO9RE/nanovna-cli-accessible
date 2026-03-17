#include "audio_backend.h"
#include "logger.h"
#include <portaudio.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <atomic>

/**
 * PortAudio backend for macOS and Linux
 * Uses PortAudio library for cross-platform audio output
 * This implementation is shared between macOS and Linux platforms
 * 
 * Features persistent stream to eliminate audio gaps in smooth playback mode.
 * Supports device selection for multiplayer (per-player audio output).
 */
class PortAudioBackend : public IAudioBackend {
private:
    bool initialized = false;
    PaStream* persistentStream = nullptr;
    int currentSampleRate = 0;
    int currentChannels = 0;
    int selectedDeviceIndex = -1;  // -1 = default device
    std::mutex streamMutex;
    std::atomic<bool> abortRequested{false};
    Logger* debugLogger = nullptr;
    AudioError lastError_ = AudioError::NONE;

    void audioLog(const char* fmt, ...) {
        if (!debugLogger) return;
        va_list args;
        va_start(args, fmt);
        char buf[512];
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        debugLogger->log("AUDIO_MAC", std::string(buf));
    }
    
    void closeStream() {
        if (persistentStream) {
            Pa_StopStream(persistentStream);
            Pa_CloseStream(persistentStream);
            persistentStream = nullptr;
            currentSampleRate = 0;
            currentChannels = 0;
        }
    }
    
    bool ensureStreamOpen(int sampleRate, int channels) {
        // Check if stream parameters changed or stream doesn't exist
        if (persistentStream && (currentSampleRate != sampleRate || currentChannels != channels)) {
            closeStream();
        }
        
        if (!persistentStream) {
            if (selectedDeviceIndex >= 0) {
                // Open specific device for multiplayer per-player output
                PaStreamParameters outputParams = {};
                outputParams.device = selectedDeviceIndex;
                outputParams.channelCount = channels;
                outputParams.sampleFormat = paInt16;
                const PaDeviceInfo* devInfo = Pa_GetDeviceInfo(selectedDeviceIndex);
                outputParams.suggestedLatency = devInfo ? devInfo->defaultLowOutputLatency : 0.02;

                // Match buffer size to typical write size for low latency
                const unsigned long framesPerBuffer = 882; // 20ms at 44100 Hz
                PaError err = Pa_OpenStream(
                    &persistentStream,
                    nullptr,          // no input
                    &outputParams,
                    sampleRate,
                    framesPerBuffer,
                    paNoFlag,
                    nullptr,          // blocking mode
                    nullptr
                );

                if (err != paNoError || !persistentStream) {
                    std::fprintf(stderr, "[PORTAUDIO] Failed to open device %d: %s\n",
                                  selectedDeviceIndex, Pa_GetErrorText(err));
                    persistentStream = nullptr;
                    return false;
                }
            } else {
                // Use default device with optimized buffer size.
                // Match framesPerBuffer to the typical write size (882 samples = 20ms at 44100 Hz)
                // to reduce latency on macOS CoreAudio, which works best when buffer
                // size and write size are aligned.
                // With matching buffer size, PortAudio's internal double-buffering still provides
                // headroom, and the aligned size eliminates partial-buffer stalls that caused
                // timing jitter in morse code generation on macOS.
                const unsigned long framesPerBuffer = 882; // 20ms at 44100 Hz — matches write size
                PaError err = Pa_OpenDefaultStream(
                    &persistentStream,
                    0,                              // no input channels
                    channels,                       // output channels
                    paInt16,                        // 16-bit samples
                    sampleRate,                     // sample rate
                    framesPerBuffer,                // optimized buffer size
                    nullptr,                        // no callback, blocking mode
                    nullptr                         // no user data
                );
                
                if (err != paNoError || !persistentStream) {
                    persistentStream = nullptr;
                    return false;
                }
            }
            
            // Start the stream
            PaError startErr = Pa_StartStream(persistentStream);
            if (startErr != paNoError) {
                Pa_CloseStream(persistentStream);
                persistentStream = nullptr;
                return false;
            }
            
            currentSampleRate = sampleRate;
            currentChannels = channels;
        }
        
        return true;
    }
    
public:
    PortAudioBackend() = default;
    
    ~PortAudioBackend() override {
        shutdown();
    }
    
    bool initialize() override {
        if (initialized) {
            return true;
        }
        
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            return false;
        }
        
        initialized = true;
        return true;
    }
    
    void shutdown() override {
        std::lock_guard<std::mutex> lock(streamMutex);
        closeStream();
        if (initialized) {
            Pa_Terminate();
            initialized = false;
        }
    }
    
    void abort() override {
        // Signal abort to unblock any ongoing Pa_WriteStream call
        abortRequested.store(true);
        // Pa_AbortStream immediately terminates the stream without waiting
        // for pending buffers to complete - safe to call from another thread
        // We don't hold streamMutex here to avoid deadlock with playBuffer
        PaStream* stream = persistentStream;
        if (stream) {
            Pa_AbortStream(stream);
        }
    }
    
    void resetAbort() override {
        abortRequested.store(false);
    }
    
    bool playBuffer(
        const int16_t* buffer,
        int samples,
        int sampleRate,
        int channels,
        int bitsPerSample) override 
    {
        if (!initialized) {
            if (!initialize()) {
                return false;
            }
        }
        
        if (!buffer || samples <= 0) {
            return false;
        }
        
        // Check abort flag before starting a new write
        if (abortRequested.load()) {
            return false;
        }
        
        // Task 2.5: Validate bitsPerSample parameter (only 16-bit supported)
        if (bitsPerSample != 16) {
            return false;  // PortAudio backend only supports 16-bit samples
        }
        
        std::lock_guard<std::mutex> lock(streamMutex);
        
        // Re-check abort after acquiring lock
        if (abortRequested.load()) {
            return false;
        }
        
        // Note: No idle timeout here. Stream is closed explicitly via shutdown().
        // Idle-timeout closeStream() during playback can cause Pa_StopStream to hang
        // or drop audio on some drivers.
        
        // Ensure stream is open with correct parameters
        if (!ensureStreamOpen(sampleRate, channels)) {
            return false;
        }
        
        // Write audio data (blocking)
        PaError err = Pa_WriteStream(persistentStream, buffer, samples);
        
        // paOutputUnderflowed is a recoverable condition — audio had a gap but the
        // stream is still alive.  Treat it as success so the audio thread keeps running.
        if (err == paOutputUnderflowed) {
            lastError_ = AudioError::TRANSIENT;
            return true;
        }
        
        // Any other non-success error: try to recover by reopening the stream once.
        // This handles drivers that silently kill the stream after an internal error.
        if (err != paNoError) {
            lastError_ = AudioError::FATAL;
            closeStream();
            if (!ensureStreamOpen(sampleRate, channels)) {
                return false;
            }
            // Retry the write on the fresh stream
            err = Pa_WriteStream(persistentStream, buffer, samples);
            if (err == paNoError || err == paOutputUnderflowed) {
                lastError_ = (err == paOutputUnderflowed) ? AudioError::TRANSIENT : AudioError::NONE;
                return true;
            }
            return false;
        }
        
        lastError_ = AudioError::NONE;
        return true;
    }
    
    AudioError getLastError() const override { return lastError_; }
    
    int detectMaxChannels() override {
        if (!initialized) {
            if (!initialize()) {
                return 2; // Default to stereo on failure
            }
        }
        
        // Get default output device
        PaDeviceIndex defaultDevice = Pa_GetDefaultOutputDevice();
        if (defaultDevice == paNoDevice) {
            return 2; // Default to stereo
        }
        
        // Get device info
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(defaultDevice);
        if (!deviceInfo) {
            return 2; // Default to stereo
        }
        
        // Return maximum output channels
        int maxChannels = deviceInfo->maxOutputChannels;
        
        // Clamp to supported values (2, 6, or 8)
        if (maxChannels >= 8) {
            return 8; // 7.1 surround
        } else if (maxChannels >= 6) {
            return 6; // 5.1 surround
        } else {
            return 2; // Stereo
        }
    }

    std::vector<AudioOutputDevice> enumerateDevices() override {
        std::vector<AudioOutputDevice> devices;

        if (!initialized) {
            if (!initialize()) return devices;
        }

        PaDeviceIndex defaultDev = Pa_GetDefaultOutputDevice();
        int numDevices = Pa_GetDeviceCount();

        for (int i = 0; i < numDevices; i++) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (!info || info->maxOutputChannels < 2) continue;

            AudioOutputDevice dev;
            dev.index = i;
            dev.name = info->name ? info->name : "Unknown Device";
            dev.maxChannels = info->maxOutputChannels;
            dev.defaultSampleRate = static_cast<int>(info->defaultSampleRate);
            dev.isDefault = (i == defaultDev);
            devices.push_back(dev);
        }

        return devices;
    }

    bool selectDevice(int deviceIndex) override {
        audioLog("selectDevice(%d) called", deviceIndex);
        // Must be called before stream is opened or after shutdown
        std::lock_guard<std::mutex> lock(streamMutex);
        audioLog("selectDevice: closing existing stream");
        closeStream();
        selectedDeviceIndex = deviceIndex;
        audioLog("selectDevice: complete, device=%d", deviceIndex);
        return true;
    }

    void setLogger(Logger* logger) override {
        debugLogger = logger;
    }
};

// Factory function implementation
IAudioBackend* createAudioBackend() {
    return new PortAudioBackend();
}
