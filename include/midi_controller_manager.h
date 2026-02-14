#pragma once
#include "platform/midi_controller.h"
#include "logger.h"
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <functional>
#include <memory>
#include <atomic>

/**
 * @file midi_controller_manager.h
 * @brief MIDI Controller mapping and dispatch manager
 *
 * Maps MIDI controller events (Note On/Off, CC) to application commands.
 * Supports motor fader feedback, value scaling (0-127 <-> app values),
 * and preset configurations (e.g., Behringer X-Touch Compact).
 *
 * The manager is agnostic to the command source — keyboard, web interface,
 * or MIDI hardware all produce the same application-level actions.
 */

/**
 * Application-level commands that can be triggered by MIDI controller events.
 * These correspond to the keyboard shortcuts in the acoustic analyzer.
 */
enum class MidiAppCommand {
    NONE = 0,
    
    // Playback control
    PLAY_PAUSE,          // Space bar equivalent
    STOP,                // 's' key
    FREEZE,              // 'f' key
    
    // Mode toggles
    TOGGLE_SMOOTH_DOTTED,  // 't' key
    TOGGLE_LOOP,           // 'o' key
    TOGGLE_LOOP_ZOOM,      // 'z' key
    TOGGLE_LOOP_INVERT,    // 'i' key
    TOGGLE_CONTINUOUS,     // 'c' key
    
    // Loop markers
    SET_LOOP_LEFT,       // 'l' key
    SET_LOOP_RIGHT,      // 'r' key
    
    // Navigation
    MOVE_LEFT,           // Left arrow
    MOVE_RIGHT,          // Right arrow
    JUMP_WIDTH_UP,       // Up arrow
    JUMP_WIDTH_DOWN,     // Down arrow
    
    // Speed control
    SPEED_UP,            // '+' key
    SPEED_DOWN,          // '-' key
    
    // Curve toggles (1-5)
    TOGGLE_CURVE_1,      // '1' key
    TOGGLE_CURVE_2,      // '2' key
    TOGGLE_CURVE_3,      // '3' key
    TOGGLE_CURVE_4,      // '4' key
    TOGGLE_CURVE_5,      // '5' key
    
    // Curve mute/solo
    MUTE_CURVE_1,
    MUTE_CURVE_2,
    MUTE_CURVE_3,
    MUTE_CURVE_4,
    MUTE_CURVE_5,
    SOLO_CURVE_1,
    SOLO_CURVE_2,
    SOLO_CURVE_3,
    SOLO_CURVE_4,
    SOLO_CURVE_5,
    
    // Per-curve value announcement (query individual curve value at current position)
    ANNOUNCE_CURVE_VALUE_1,  // Announce current value of curve 1 (SWR)
    ANNOUNCE_CURVE_VALUE_2,  // Announce current value of curve 2 (RL)
    ANNOUNCE_CURVE_VALUE_3,  // Announce current value of curve 3 (|Z|)
    ANNOUNCE_CURVE_VALUE_4,  // Announce current value of curve 4 (X)
    ANNOUNCE_CURVE_VALUE_5,  // Announce current value of curve 5 (Phase)
    
    // Master value announcement
    ANNOUNCE_MASTER_VOLUME,  // Announce current master volume
    
    // Miscellaneous
    SHOW_MEASUREMENT,    // 'm' key
    TOGGLE_STATUS_LINE,  // 'n' key area
    TOGGLE_X_RULER,      // 'x' key area
    GO_TO_POSITION,      // 'g' key
    
    COMMAND_COUNT        // Sentinel: number of commands
};

/**
 * Types of MIDI CC assignments for faders/knobs
 */
enum class MidiCCFunction {
    NONE = 0,
    
    // Position and curve value readout (controller receives values)
    POSITION_X_AXIS,         // X-axis position (sent to motor fader)
    CURVE_VALUE_SWR,         // SWR curve value (sent to motor fader)
    CURVE_VALUE_RL,          // Return Loss value (sent to motor fader)
    CURVE_VALUE_IMPEDANCE,   // |Z| value (sent to motor fader)
    CURVE_VALUE_REACTANCE,   // X (reactance) value (sent to motor fader)
    CURVE_VALUE_PHASE,       // Phase value (sent to motor fader)
    
    // Volume control (controller sends values to app)
    MASTER_VOLUME,           // Master volume control (read from fader)
    CURVE_VOLUME_1,          // Curve 1 volume (read from fader)
    CURVE_VOLUME_2,          // Curve 2 volume
    CURVE_VOLUME_3,          // Curve 3 volume
    CURVE_VOLUME_4,          // Curve 4 volume
    CURVE_VOLUME_5,          // Curve 5 volume
    
    // Fader touch detection (for freeze-by-touch feature)
    FADER_TOUCH_1,           // Motor fader 1 touch on/off (value 127=touched, 0=released)
    FADER_TOUCH_2,           // Motor fader 2 touch on/off
    FADER_TOUCH_3,           // Motor fader 3 touch on/off
    FADER_TOUCH_4,           // Motor fader 4 touch on/off
    FADER_TOUCH_5,           // Motor fader 5 touch on/off
    
    FUNCTION_COUNT           // Sentinel
};

/**
 * A single MIDI mapping entry: binds a MIDI event to an application command
 */
struct MidiMapping {
    MidiMessageType triggerType;  // NOTE_ON, NOTE_OFF, or CONTROL_CHANGE
    uint8_t channel;              // MIDI channel (0-15, 255 = any)
    uint8_t number;               // Note number or CC number
    MidiAppCommand command;       // Application command to trigger (for Note events)
    MidiCCFunction ccFunction;    // CC function (for Control Change events)
    std::string description;      // Human-readable description
};

/**
 * A complete MIDI mapping preset (e.g., for a specific controller)
 */
struct MidiMappingPreset {
    std::string name;                    // Preset name
    std::string description;             // Description
    std::string controllerName;          // Target controller model
    std::vector<MidiMapping> mappings;   // All mappings in this preset
};

/**
 * Callback type for application commands triggered by MIDI
 */
using MidiCommandCallback = std::function<void(MidiAppCommand command)>;

/**
 * Callback type for CC value changes (volume, position feedback)
 */
using MidiCCValueCallback = std::function<void(MidiCCFunction function, int value)>;

/**
 * MIDI Controller Manager
 *
 * Manages the connection between MIDI hardware and application functions.
 * Handles mapping, feedback to motor faders, and value scaling.
 */
class MidiControllerManager {
public:
    MidiControllerManager();
    ~MidiControllerManager();
    
    // Logger
    void setLogger(Logger* logger);
    
    // Device management
    std::vector<MidiDeviceInfo> listDevices();
    bool openDevice(int deviceId = -1);
    void closeDevice();
    bool isDeviceOpen() const;
    std::string getDeviceName() const;
    
    // Mapping management
    void loadMappings(const MidiMappingPreset& preset);
    bool loadMappingsFromFile(const std::string& filepath);
    bool saveMappingsToFile(const std::string& filepath) const;
    std::vector<std::string> listPresetFiles(const std::string& directory = "midi") const;
    const MidiMappingPreset& getCurrentPreset() const { return currentPreset; }
    
    // Add/remove individual mappings
    void addMapping(const MidiMapping& mapping);
    void clearMappings();
    
    // Callback registration
    void setCommandCallback(MidiCommandCallback callback);
    void setCCValueCallback(MidiCCValueCallback callback);
    
    // Motor fader feedback: send current values to controller
    void sendPositionFeedback(double normalizedPosition);  // 0.0-1.0
    void sendCurveValueFeedback(int curveIndex, double normalizedValue);  // 0.0-1.0
    void sendMasterVolumeFeedback(int volumePercent);  // 0-100
    void sendCurveVolumeFeedback(int curveIndex, int volumePercent);  // 0-200
    
    // Value conversion helpers
    static int percentToMidi(int percent, int maxPercent = 100);   // percent -> 0-127
    static int midiToPercent(int midiValue, int maxPercent = 100); // 0-127 -> percent
    static double midiToNormalized(int midiValue);                 // 0-127 -> 0.0-1.0
    static int normalizedToMidi(double value);                     // 0.0-1.0 -> 0-127
    
    // Get command/function names for display and mapping file keys
    static std::string getCommandName(MidiAppCommand cmd);
    static std::string getCCFunctionName(MidiCCFunction func);
    
    // Get stable mapping key name for config file (e.g., "toggle_curve_swr", "master_volume")
    static std::string getCommandKeyName(MidiAppCommand cmd);
    static std::string getCCFunctionKeyName(MidiCCFunction func);
    
    // Resolve mapping key name back to enum value (returns NONE/0 if unknown)
    static MidiAppCommand resolveCommandKeyName(const std::string& keyName);
    static MidiCCFunction resolveCCFunctionKeyName(const std::string& keyName);
    
    // Enable/disable
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled.load(); }
    
private:
    void onMidiEvent(const MidiControllerEvent& event);
    void processNoteEvent(const MidiControllerEvent& event);
    void processCCEvent(const MidiControllerEvent& event);
    
    // Find mapping for a given MIDI event
    MidiMapping* findNoteMapping(MidiMessageType type, uint8_t channel, uint8_t note);
    MidiMapping* findCCMapping(uint8_t channel, uint8_t ccNumber);
    
    // Find CC number assigned to a function (for feedback)
    int findCCNumberForFunction(MidiCCFunction func) const;
    uint8_t findChannelForFunction(MidiCCFunction func) const;
    
    std::unique_ptr<IMidiControllerInput> controllerInput;
    MidiMappingPreset currentPreset;
    
    MidiCommandCallback commandCallback;
    MidiCCValueCallback ccValueCallback;
    
    Logger* logger = nullptr;
    std::mutex callbackMutex;
    std::atomic<bool> enabled{false};
    
    // Index maps for fast lookup
    // Key: (channel << 8) | number, or (255 << 8) | number for "any channel"
    std::map<uint16_t, MidiMapping*> noteOnMap;
    std::map<uint16_t, MidiMapping*> noteOffMap;
    std::map<uint16_t, MidiMapping*> ccMap;
    
    void rebuildLookupMaps();
};
