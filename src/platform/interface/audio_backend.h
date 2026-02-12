#ifndef AUDIO_BACKEND_H
#define AUDIO_BACKEND_H

#include <cstdint>
#include <vector>

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
 * Audio idle timeout in milliseconds
 * Streams are automatically closed after this period of inactivity
 * to conserve system resources
 */
constexpr int AUDIO_IDLE_TIMEOUT_MS = 500;

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
};

/**
 * Factory function to create platform-specific audio backend
 * @return Pointer to platform-specific IAudioBackend implementation
 */
IAudioBackend* createAudioBackend();

#endif // AUDIO_BACKEND_H
