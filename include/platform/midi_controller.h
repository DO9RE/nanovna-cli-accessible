#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

/**
 * @file midi_controller.h
 * @brief Platform-independent MIDI controller input interface
 * 
 * This interface abstracts MIDI controller hardware input across platforms.
 * It handles receiving Note On/Off and Control Change messages from
 * external MIDI controllers (e.g., Behringer X-Touch Compact).
 * 
 * Platform implementations:
 * - Windows: WinMM MIDI Input
 * - macOS: CoreMIDI Input
 * - Linux: ALSA Sequencer Input
 * 
 * Design follows the existing platform abstraction pattern used by
 * IConsoleInput and MIDIPlatformInterface.
 */

/**
 * MIDI message types for controller input
 */
enum class MidiMessageType {
    NOTE_ON = 0,       // Key/button pressed (status 0x9n)
    NOTE_OFF = 1,      // Key/button released (status 0x8n)
    CONTROL_CHANGE = 2 // Fader/knob moved (status 0xBn)
};

/**
 * Represents a single MIDI input event from a controller
 */
struct MidiControllerEvent {
    MidiMessageType type;
    uint8_t channel;   // MIDI channel (0-15)
    uint8_t data1;     // Note number or CC number (0-127)
    uint8_t data2;     // Velocity or CC value (0-127)
};

/**
 * Information about an available MIDI input device
 */
struct MidiDeviceInfo {
    int id;               // Platform-specific device ID
    std::string name;     // Human-readable device name
};

/**
 * Callback type for receiving MIDI events
 * Called from the MIDI input thread when events arrive
 */
using MidiEventCallback = std::function<void(const MidiControllerEvent&)>;

/**
 * Platform-independent MIDI controller input interface
 */
class IMidiControllerInput {
public:
    virtual ~IMidiControllerInput() = default;
    
    /**
     * List available MIDI input devices
     * @return Vector of available MIDI input devices
     */
    virtual std::vector<MidiDeviceInfo> listDevices() = 0;
    
    /**
     * Open a specific MIDI input device
     * @param deviceId Platform-specific device ID (-1 for default)
     * @return true on success, false on failure
     */
    virtual bool open(int deviceId = -1) = 0;
    
    /**
     * Close the currently open MIDI input device
     */
    virtual void close() = 0;
    
    /**
     * Check if a MIDI input device is currently open
     * @return true if open
     */
    virtual bool isOpen() const = 0;
    
    /**
     * Set callback for receiving MIDI events
     * The callback is invoked from the MIDI input thread
     * @param callback Function to call when MIDI events are received
     */
    virtual void setEventCallback(MidiEventCallback callback) = 0;
    
    /**
     * Send a MIDI message to the controller (for motor fader feedback)
     * @param status Status byte (command + channel)
     * @param data1 First data byte (0-127)
     * @param data2 Second data byte (0-127)
     * @return true on success
     */
    virtual bool sendFeedback(uint8_t status, uint8_t data1, uint8_t data2) = 0;
    
    /**
     * Get platform name for logging
     * @return Platform identifier string
     */
    virtual const char* getPlatformName() const = 0;
    
    /**
     * Get last error message
     * @return Error description or empty string if no error
     */
    virtual std::string getLastError() const = 0;
    
    /**
     * Get name of the currently open device
     * @return Device name or empty string if not open
     */
    virtual std::string getDeviceName() const = 0;
};

/**
 * Factory function to create platform-specific MIDI controller input
 * Defined in platform-specific source files
 * @return Pointer to platform-specific IMidiControllerInput implementation
 */
IMidiControllerInput* createMidiControllerInput();
