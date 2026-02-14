#pragma once

#ifdef _WIN32

#include <string>
#include <vector>
#include <cstdint>

/**
 * Windows Media Foundation MIDI Renderer
 * 
 * Uses Windows Media Foundation to render MIDI files to PCM audio
 * and save as WAV files.
 * 
 * Platform: Windows only
 * 
 * Features:
 * - Initialize Media Foundation
 * - Open MIDI file with IMFSourceReader
 * - Configure output format to PCM (44100 Hz, 16-bit, stereo)
 * - Read and accumulate audio samples
 * - Write WAV file with proper RIFF/WAVE headers
 * - Clean up resources
 */
class WMFMIDIRenderer {
public:
    WMFMIDIRenderer();
    ~WMFMIDIRenderer();
    
    /**
     * Render a MIDI file to a WAV file
     * @param midiFilePath Path to the input MIDI file
     * @param wavFilePath Path to the output WAV file
     * @return true on success, false on failure
     */
    bool renderToWav(const std::string& midiFilePath, const std::string& wavFilePath);
    
    /**
     * Get the last error message
     * @return Error message string
     */
    std::string getLastError() const { return lastError; }
    
private:
    std::string lastError;
    bool mfInitialized;
    
    // Audio format constants
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int CHANNELS = 2;
    static constexpr int BITS_PER_SAMPLE = 16;
    static constexpr int BYTES_PER_SAMPLE = BITS_PER_SAMPLE / 8;
    static constexpr int BLOCK_ALIGN = CHANNELS * BYTES_PER_SAMPLE;
    
    /**
     * Initialize Media Foundation
     * @return true on success, false on failure
     */
    bool initializeMediaFoundation();
    
    /**
     * Shutdown Media Foundation
     */
    void shutdownMediaFoundation();
    
    /**
     * Read all audio samples from the source reader
     * @param buffer Output buffer for PCM samples
     * @return true on success, false on failure
     */
    bool readAllSamples(std::vector<int16_t>& buffer);
    
    /**
     * Write WAV file with RIFF/WAVE headers
     * @param wavFilePath Path to output WAV file
     * @param samples PCM sample buffer (interleaved stereo)
     * @return true on success, false on failure
     */
    bool writeWavFile(const std::string& wavFilePath, const std::vector<int16_t>& samples);
    
    /**
     * Set error message
     */
    void setError(const std::string& error);
    
    /**
     * Convert HRESULT to string for error messages
     */
    std::string hresultToString(long hr);
};

#endif // _WIN32
