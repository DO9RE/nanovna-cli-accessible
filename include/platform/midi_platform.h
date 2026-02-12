#pragma once
#include <cstdint>

/**
 * Platform-independent MIDI interface
 * 
 * This interface abstracts platform-specific MIDI functionality.
 * Implementations are selected at compile-time based on target platform.
 * 
 * Platform implementations:
 * - Windows: WinMM (MIDI Mapper)
 * - macOS: CoreMIDI + AudioUnit (DLS Synth)
 * - Linux: ALSA or FluidSynth (future)
 */
class MIDIPlatformInterface {
public:
    virtual ~MIDIPlatformInterface() = default;
    
    /**
     * Open and initialize MIDI device
     * @return true on success, false on failure
     */
    virtual bool open() = 0;
    
    /**
     * Close MIDI device
     */
    virtual void close() = 0;
    
    /**
     * Check if MIDI device is open
     * @return true if open, false otherwise
     */
    virtual bool isOpen() const = 0;
    
    /**
     * Send a 3-byte MIDI message
     * @param status Status byte (command + channel)
     * @param data1 First data byte (0-127)
     * @param data2 Second data byte (0-127)
     */
    virtual void sendMessage(uint8_t status, uint8_t data1, uint8_t data2) = 0;
    
    /**
     * Get platform name for logging
     * @return Platform identifier string
     */
    virtual const char* getPlatformName() const = 0;
    
    /**
     * Get last error message
     * @return Error description or nullptr if no error
     */
    virtual const char* getLastError() const = 0;
};

/**
 * Factory function to create platform-specific MIDI implementation
 * Defined in platform-specific source files
 */
MIDIPlatformInterface* createMIDIPlatform();
