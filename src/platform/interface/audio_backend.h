#ifndef AUDIO_BACKEND_H
#define AUDIO_BACKEND_H

#include <cstdint>
#include <vector>
#include <string>

// Forward declaration — Logger is optional (only used when debug mode is active)
class Logger;

/**
 * @file audio_backend.h
 * @brief Platform-independent audio output interface
 * 
 * This interface abstracts audio playback functionality across different platforms.
 * Windows uses waveOut API
 * macOS/Linux use PortAudio library
 * 
 * Task 2.5: IMPORTANT - Only 16-bit samples are supported.
 * playBuffer() will return false if bitsPerSample != 16.
 */

/**
 * Audio device descriptor for enumeration.
 */
struct AudioOutputDevice {
    int index;                 // Backend-specific device index
    std::string name;          // Human-readable name
    int maxChannels;           // Maximum output channels
    int defaultSampleRate;     // Preferred sample rate
    bool isDefault;            // Is this the system default device?

    AudioOutputDevice() : index(-1), maxChannels(2), defaultSampleRate(44100), isDefault(false) {}
};

/**
 * Audio error classification for unified recovery handling.
 * TRANSIENT errors allow immediate retry; FATAL errors require device reopen.
 */
enum class AudioError {
    NONE,       // No error
    TRANSIENT,  // Buffer underflow — retry allowed
    FATAL       // Device lost — requires shutdown + reinitialize
};

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;
    
    /**
     * Initialize the audio backend
     * @return true on success, false on failure
     */
    virtual bool initialize() = 0;
    
    /**
     * Shutdown the audio backend
     */
    virtual void shutdown() = 0;
    
    /**
     * Abort any ongoing audio playback immediately.
     * Stops output within 50 ms. Pending buffers are discarded, not played.
     * Default implementation does nothing.
     */
    virtual void abort() {}
    
    /**
     * Reset the abort state so that playback can resume after a previous abort
     * Must be called before playBuffer() will accept new data again
     * Default implementation does nothing
     */
    virtual void resetAbort() {}
    
    /**
     * Play audio buffer (blocking)
     * @param buffer Interleaved stereo PCM data (int16_t, left/right alternating)
     * @param samples Number of samples per channel
     * @param sampleRate Sample rate in Hz
     * @param channels Number of channels (2 for stereo, 6 for 5.1, 8 for 7.1)
     * @param bitsPerSample Bits per sample (typically 16)
     * @return true on success, false on failure
     */
    virtual bool playBuffer(
        const int16_t* buffer,
        int samples,
        int sampleRate,
        int channels,
        int bitsPerSample) = 0;
    
    /**
     * Detect maximum number of audio channels supported by hardware
     * @return Maximum number of channels (2, 6, or 8)
     */
    virtual int detectMaxChannels() = 0;

    /**
     * Return the error type from the last failed playBuffer() call.
     * Used by game-level recovery to distinguish retry vs. reconnect.
     */
    virtual AudioError getLastError() const { return AudioError::NONE; }

    /**
     * Enumerate available audio output devices.
     * Default implementation returns only the default device.
     * @return Vector of available output devices
     */
    virtual std::vector<AudioOutputDevice> enumerateDevices() {
        AudioOutputDevice def;
        def.index = -1;
        def.name = "Default Audio Device";
        def.maxChannels = detectMaxChannels();
        def.defaultSampleRate = 44100;
        def.isDefault = true;
        return { def };
    }

    /**
     * Select a specific output device by index.
     * Must be called before initialize() or after shutdown().
     * @param deviceIndex Device index from enumerateDevices(), or -1 for default
     * @return true if device was selected successfully
     */
    virtual bool selectDevice(int deviceIndex) {
        (void)deviceIndex;
        return true;  // Default: accept any device (uses system default)
    }

    /**
     * Set a Logger instance for debug output.
     * When set, backend-internal diagnostics are written to the debug log file
     * instead of stderr, so all output appears in a single log.
     * @param logger Pointer to Logger (may be null to disable logging)
     */
    virtual void setLogger(Logger* /*logger*/) {}
};

/**
 * Factory function to create platform-specific audio backend
 * @return Pointer to platform-specific IAudioBackend implementation
 */
IAudioBackend* createAudioBackend();

#endif // AUDIO_BACKEND_H
