#include "audio_backend.h"
#include <portaudio.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <mutex>
#include <atomic>

/**
 * PortAudio backend for macOS and Linux
 * Uses PortAudio library for cross-platform audio output
 * This implementation is shared between macOS and Linux platforms
 * 
 * Features persistent stream to eliminate audio gaps in smooth playback mode
 */
class PortAudioBackend : public IAudioBackend {
private:
    bool initialized = false;
    PaStream* persistentStream = nullptr;
    int currentSampleRate = 0;
    int currentChannels = 0;
    std::mutex streamMutex;
    std::chrono::steady_clock::time_point lastPlayTime;
    std::atomic<bool> abortRequested{false};
    
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
            // Open stream with smaller buffer for lower latency
            // Use paFramesPerBufferUnspecified to let PortAudio choose optimal buffer size
            PaError err = Pa_OpenDefaultStream(
                &persistentStream,
                0,                              // no input channels
                channels,                       // output channels
                paInt16,                        // 16-bit samples
                sampleRate,                     // sample rate
                paFramesPerBufferUnspecified,   // let PortAudio choose buffer size
                nullptr,                        // no callback, blocking mode
                nullptr                         // no user data
            );
            
            if (err != paNoError || !persistentStream) {
                persistentStream = nullptr;
                return false;
            }
            
            // Start the stream
            err = Pa_StartStream(persistentStream);
            if (err != paNoError) {
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
        lastPlayTime = std::chrono::steady_clock::now();
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
        
        // Check if stream has been idle and should be closed
        auto now = std::chrono::steady_clock::now();
        auto idleTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPlayTime).count();
        if (idleTime > AUDIO_IDLE_TIMEOUT_MS && persistentStream) {
            closeStream();
        }
        
        // Ensure stream is open with correct parameters
        if (!ensureStreamOpen(sampleRate, channels)) {
            return false;
        }
        
        // Write audio data (blocking)
        PaError err = Pa_WriteStream(persistentStream, buffer, samples);
        
        lastPlayTime = now;
        
        return (err == paNoError);
    }
    
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
};

// Factory function implementation
IAudioBackend* createAudioBackend() {
    return new PortAudioBackend();
}
