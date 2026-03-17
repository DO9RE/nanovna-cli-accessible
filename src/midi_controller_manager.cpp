#include "midi_controller_manager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <cctype>
#include <regex>

// ----------------------------------------------------------------------------
// Value conversion helpers
// ----------------------------------------------------------------------------

int MidiControllerManager::percentToMidi(int percent, int maxPercent) {
    if (percent <= 0) return 0;
    if (percent >= maxPercent) return 127;
    return static_cast<int>(std::round(127.0 * percent / maxPercent));
}

int MidiControllerManager::midiToPercent(int midiValue, int maxPercent) {
    if (midiValue <= 0) return 0;
    if (midiValue >= 127) return maxPercent;
    return static_cast<int>(std::round(static_cast<double>(maxPercent) * midiValue / 127.0));
}

double MidiControllerManager::midiToNormalized(int midiValue) {
    if (midiValue <= 0) return 0.0;
    if (midiValue >= 127) return 1.0;
    return midiValue / 127.0;
}

int MidiControllerManager::normalizedToMidi(double value) {
    if (value <= 0.0) return 0;
    if (value >= 1.0) return 127;
    return static_cast<int>(std::round(127.0 * value));
}

// ----------------------------------------------------------------------------
// Command/function name helpers
// ----------------------------------------------------------------------------

std::string MidiControllerManager::getCommandName(MidiAppCommand cmd) {
    switch (cmd) {
        case MidiAppCommand::NONE: return "None";
        case MidiAppCommand::PLAY_PAUSE: return "Play/Pause";
        case MidiAppCommand::STOP: return "Stop";
        case MidiAppCommand::FREEZE: return "Freeze";
        case MidiAppCommand::TOGGLE_SMOOTH_DOTTED: return "Toggle Smooth/Dotted";
        case MidiAppCommand::TOGGLE_LOOP: return "Toggle Loop";
        case MidiAppCommand::TOGGLE_LOOP_ZOOM: return "Toggle Loop Zoom";
        case MidiAppCommand::TOGGLE_LOOP_INVERT: return "Toggle Loop Invert";
        case MidiAppCommand::TOGGLE_CONTINUOUS: return "Toggle Continuous";
        case MidiAppCommand::SET_LOOP_LEFT: return "Set Loop Left";
        case MidiAppCommand::SET_LOOP_RIGHT: return "Set Loop Right";
        case MidiAppCommand::MOVE_LEFT: return "Move Left";
        case MidiAppCommand::MOVE_RIGHT: return "Move Right";
        case MidiAppCommand::JUMP_WIDTH_UP: return "Jump Width Up";
        case MidiAppCommand::JUMP_WIDTH_DOWN: return "Jump Width Down";
        case MidiAppCommand::SPEED_UP: return "Speed Up";
        case MidiAppCommand::SPEED_DOWN: return "Speed Down";
        case MidiAppCommand::TOGGLE_CURVE_1: return "Toggle Curve 1 (SWR)";
        case MidiAppCommand::TOGGLE_CURVE_2: return "Toggle Curve 2 (RL)";
        case MidiAppCommand::TOGGLE_CURVE_3: return "Toggle Curve 3 (|Z|)";
        case MidiAppCommand::TOGGLE_CURVE_4: return "Toggle Curve 4 (X)";
        case MidiAppCommand::TOGGLE_CURVE_5: return "Toggle Curve 5 (Phase)";
        case MidiAppCommand::MUTE_CURVE_1: return "Mute Curve 1";
        case MidiAppCommand::MUTE_CURVE_2: return "Mute Curve 2";
        case MidiAppCommand::MUTE_CURVE_3: return "Mute Curve 3";
        case MidiAppCommand::MUTE_CURVE_4: return "Mute Curve 4";
        case MidiAppCommand::MUTE_CURVE_5: return "Mute Curve 5";
        case MidiAppCommand::SOLO_CURVE_1: return "Solo Curve 1 (SWR)";
        case MidiAppCommand::SOLO_CURVE_2: return "Solo Curve 2 (RL)";
        case MidiAppCommand::SOLO_CURVE_3: return "Solo Curve 3 (|Z|)";
        case MidiAppCommand::SOLO_CURVE_4: return "Solo Curve 4 (X)";
        case MidiAppCommand::SOLO_CURVE_5: return "Solo Curve 5 (Phase)";
        case MidiAppCommand::ANNOUNCE_CURVE_VALUE_1: return "Announce Curve Value 1 (SWR)";
        case MidiAppCommand::ANNOUNCE_CURVE_VALUE_2: return "Announce Curve Value 2 (RL)";
        case MidiAppCommand::ANNOUNCE_CURVE_VALUE_3: return "Announce Curve Value 3 (|Z|)";
        case MidiAppCommand::ANNOUNCE_CURVE_VALUE_4: return "Announce Curve Value 4 (X)";
        case MidiAppCommand::ANNOUNCE_CURVE_VALUE_5: return "Announce Curve Value 5 (Phase)";
        case MidiAppCommand::ANNOUNCE_MASTER_VOLUME: return "Announce Master Volume";
        case MidiAppCommand::SHOW_MEASUREMENT: return "Show Measurement";
        case MidiAppCommand::TOGGLE_STATUS_LINE: return "Toggle Status Line";
        case MidiAppCommand::TOGGLE_X_RULER: return "Toggle X-Axis Ruler";
        case MidiAppCommand::GO_TO_POSITION: return "Go To Position";
        case MidiAppCommand::OVERVIEW_CURVE_1: return "Overview Curve 1 (SWR)";
        case MidiAppCommand::OVERVIEW_CURVE_2: return "Overview Curve 2 (RL)";
        case MidiAppCommand::OVERVIEW_CURVE_3: return "Overview Curve 3 (|Z|)";
        case MidiAppCommand::OVERVIEW_CURVE_4: return "Overview Curve 4 (X)";
        case MidiAppCommand::OVERVIEW_CURVE_5: return "Overview Curve 5 (Phase)";
        default: return "Unknown";
    }
}

std::string MidiControllerManager::getCCFunctionName(MidiCCFunction func) {
    switch (func) {
        case MidiCCFunction::NONE: return "None";
        case MidiCCFunction::POSITION_X_AXIS: return "X-Axis Position (Motor Fader)";
        case MidiCCFunction::CURVE_VALUE_SWR: return "SWR Value (Motor Fader)";
        case MidiCCFunction::CURVE_VALUE_RL: return "Return Loss Value (Motor Fader)";
        case MidiCCFunction::CURVE_VALUE_IMPEDANCE: return "|Z| Value (Motor Fader)";
        case MidiCCFunction::CURVE_VALUE_REACTANCE: return "Reactance Value (Motor Fader)";
        case MidiCCFunction::CURVE_VALUE_PHASE: return "Phase Value (Motor Fader)";
        case MidiCCFunction::MASTER_VOLUME: return "Master Volume";
        case MidiCCFunction::CURVE_VOLUME_1: return "Curve 1 Volume (SWR)";
        case MidiCCFunction::CURVE_VOLUME_2: return "Curve 2 Volume (RL)";
        case MidiCCFunction::CURVE_VOLUME_3: return "Curve 3 Volume (|Z|)";
        case MidiCCFunction::CURVE_VOLUME_4: return "Curve 4 Volume (X)";
        case MidiCCFunction::CURVE_VOLUME_5: return "Curve 5 Volume (Phase)";
        case MidiCCFunction::FADER_TOUCH_1: return "Motor Fader 1 Touch (SWR)";
        case MidiCCFunction::FADER_TOUCH_2: return "Motor Fader 2 Touch (RL)";
        case MidiCCFunction::FADER_TOUCH_3: return "Motor Fader 3 Touch (|Z|)";
        case MidiCCFunction::FADER_TOUCH_4: return "Motor Fader 4 Touch (X)";
        case MidiCCFunction::FADER_TOUCH_5: return "Motor Fader 5 Touch (Phase)";
        default: return "Unknown";
    }
}

// ----------------------------------------------------------------------------
// Stable key names for config file (human-readable, no numeric IDs)
// ----------------------------------------------------------------------------

std::string MidiControllerManager::getCommandKeyName(MidiAppCommand cmd) {
    switch (cmd) {
        case MidiAppCommand::NONE: return "none";
        case MidiAppCommand::PLAY_PAUSE: return "play_pause";
        case MidiAppCommand::STOP: return "stop";
        case MidiAppCommand::FREEZE: return "freeze";
        case MidiAppCommand::TOGGLE_SMOOTH_DOTTED: return "toggle_smooth_dotted";
        case MidiAppCommand::TOGGLE_LOOP: return "toggle_loop";
        case MidiAppCommand::TOGGLE_LOOP_ZOOM: return "toggle_loop_zoom";
        case MidiAppCommand::TOGGLE_LOOP_INVERT: return "toggle_loop_invert";
        case MidiAppCommand::TOGGLE_CONTINUOUS: return "toggle_continuous";
        case MidiAppCommand::SET_LOOP_LEFT: return "set_loop_left";
        case MidiAppCommand::SET_LOOP_RIGHT: return "set_loop_right";
        case MidiAppCommand::MOVE_LEFT: return "move_left";
        case MidiAppCommand::MOVE_RIGHT: return "move_right";
        case MidiAppCommand::JUMP_WIDTH_UP: return "jump_width_up";
        case MidiAppCommand::JUMP_WIDTH_DOWN: return "jump_width_down";
        case MidiAppCommand::SPEED_UP: return "speed_up";
        case MidiAppCommand::SPEED_DOWN: return "speed_down";
        case MidiAppCommand::TOGGLE_CURVE_1: return "toggle_curve_swr";
        case MidiAppCommand::TOGGLE_CURVE_2: return "toggle_curve_rl";
        case MidiAppCommand::TOGGLE_CURVE_3: return "toggle_curve_impedance";
        case MidiAppCommand::TOGGLE_CURVE_4: return "toggle_curve_reactance";
        case MidiAppCommand::TOGGLE_CURVE_5: return "toggle_curve_phase";
        case MidiAppCommand::MUTE_CURVE_1: return "mute_curve_swr";
        case MidiAppCommand::MUTE_CURVE_2: return "mute_curve_rl";
        case MidiAppCommand::MUTE_CURVE_3: return "mute_curve_impedance";
        case MidiAppCommand::MUTE_CURVE_4: return "mute_curve_reactance";
        case MidiAppCommand::MUTE_CURVE_5: return "mute_curve_phase";
        case MidiAppCommand::SOLO_CURVE_1: return "solo_curve_swr";
        case MidiAppCommand::SOLO_CURVE_2: return "solo_curve_rl";
        case MidiAppCommand::SOLO_CURVE_3: return "solo_curve_impedance";
        case MidiAppCommand::SOLO_CURVE_4: return "solo_curve_reactance";
        case MidiAppCommand::SOLO_CURVE_5: return "solo_curve_phase";
        case MidiAppCommand::ANNOUNCE_CURVE_VALUE_1: return "announce_curve_value_swr";
        case MidiAppCommand::ANNOUNCE_CURVE_VALUE_2: return "announce_curve_value_rl";
        case MidiAppCommand::ANNOUNCE_CURVE_VALUE_3: return "announce_curve_value_impedance";
        case MidiAppCommand::ANNOUNCE_CURVE_VALUE_4: return "announce_curve_value_reactance";
        case MidiAppCommand::ANNOUNCE_CURVE_VALUE_5: return "announce_curve_value_phase";
        case MidiAppCommand::ANNOUNCE_MASTER_VOLUME: return "announce_master_volume";
        case MidiAppCommand::SHOW_MEASUREMENT: return "show_measurement";
        case MidiAppCommand::TOGGLE_STATUS_LINE: return "toggle_status_line";
        case MidiAppCommand::TOGGLE_X_RULER: return "toggle_x_ruler";
        case MidiAppCommand::GO_TO_POSITION: return "go_to_position";
        case MidiAppCommand::OVERVIEW_CURVE_1: return "overview_curve_1";
        case MidiAppCommand::OVERVIEW_CURVE_2: return "overview_curve_2";
        case MidiAppCommand::OVERVIEW_CURVE_3: return "overview_curve_3";
        case MidiAppCommand::OVERVIEW_CURVE_4: return "overview_curve_4";
        case MidiAppCommand::OVERVIEW_CURVE_5: return "overview_curve_5";
        default: return "unknown";
    }
}

std::string MidiControllerManager::getCCFunctionKeyName(MidiCCFunction func) {
    switch (func) {
        case MidiCCFunction::NONE: return "none";
        case MidiCCFunction::POSITION_X_AXIS: return "position_x_axis";
        case MidiCCFunction::CURVE_VALUE_SWR: return "curve_value_swr";
        case MidiCCFunction::CURVE_VALUE_RL: return "curve_value_rl";
        case MidiCCFunction::CURVE_VALUE_IMPEDANCE: return "curve_value_impedance";
        case MidiCCFunction::CURVE_VALUE_REACTANCE: return "curve_value_reactance";
        case MidiCCFunction::CURVE_VALUE_PHASE: return "curve_value_phase";
        case MidiCCFunction::MASTER_VOLUME: return "master_volume";
        case MidiCCFunction::CURVE_VOLUME_1: return "curve_volume_swr";
        case MidiCCFunction::CURVE_VOLUME_2: return "curve_volume_rl";
        case MidiCCFunction::CURVE_VOLUME_3: return "curve_volume_impedance";
        case MidiCCFunction::CURVE_VOLUME_4: return "curve_volume_reactance";
        case MidiCCFunction::CURVE_VOLUME_5: return "curve_volume_phase";
        case MidiCCFunction::FADER_TOUCH_1: return "fader_touch_swr";
        case MidiCCFunction::FADER_TOUCH_2: return "fader_touch_rl";
        case MidiCCFunction::FADER_TOUCH_3: return "fader_touch_impedance";
        case MidiCCFunction::FADER_TOUCH_4: return "fader_touch_reactance";
        case MidiCCFunction::FADER_TOUCH_5: return "fader_touch_phase";
        default: return "unknown";
    }
}

MidiAppCommand MidiControllerManager::resolveCommandKeyName(const std::string& keyName) {
    for (int i = 0; i < static_cast<int>(MidiAppCommand::COMMAND_COUNT); i++) {
        MidiAppCommand cmd = static_cast<MidiAppCommand>(i);
        if (getCommandKeyName(cmd) == keyName) return cmd;
    }
    return MidiAppCommand::NONE;
}

MidiCCFunction MidiControllerManager::resolveCCFunctionKeyName(const std::string& keyName) {
    for (int i = 0; i < static_cast<int>(MidiCCFunction::FUNCTION_COUNT); i++) {
        MidiCCFunction func = static_cast<MidiCCFunction>(i);
        if (getCCFunctionKeyName(func) == keyName) return func;
    }
    return MidiCCFunction::NONE;
}

// ----------------------------------------------------------------------------
// Constructor / Destructor
// ----------------------------------------------------------------------------

MidiControllerManager::MidiControllerManager() {
    controllerInput.reset(createMidiControllerInput());
    if (controllerInput) {
        controllerInput->setEventCallback([this](const MidiControllerEvent& event) {
            onMidiEvent(event);
        });
    }
}

MidiControllerManager::~MidiControllerManager() {
    closeDevice();
}

void MidiControllerManager::setLogger(Logger* log) {
    logger = log;
}

// ----------------------------------------------------------------------------
// Device management
// ----------------------------------------------------------------------------

std::vector<MidiDeviceInfo> MidiControllerManager::listDevices() {
    if (!controllerInput) return {};
    auto devices = controllerInput->listDevices();
    if (logger) {
        logger->log("MIDI_CTRL", "Listed " + std::to_string(devices.size()) + " MIDI input device(s)");
        for (const auto& dev : devices) {
            logger->log("MIDI_CTRL", "  Device " + std::to_string(dev.id) + ": " + dev.name);
        }
    }
    return devices;
}

bool MidiControllerManager::openDevice(int deviceId) {
    if (!controllerInput) {
        if (logger) logger->log("MIDI_CTRL", "ERROR: No MIDI controller input implementation available");
        return false;
    }
    
    bool success = controllerInput->open(deviceId);
    if (success) {
        if (logger) logger->log("MIDI_CTRL", "Opened MIDI controller: " + controllerInput->getDeviceName());
        enabled = true;
    } else {
        if (logger) logger->log("MIDI_CTRL", "ERROR: Failed to open MIDI controller: " + controllerInput->getLastError());
    }
    return success;
}

int MidiControllerManager::openDeviceByName(const std::string& deviceName, int* foundIdOut) {
    if (!controllerInput || deviceName.empty()) return -1;

    auto devices = controllerInput->listDevices();
    // Prefer exact match, then fall back to prefix / substring match
    int foundId = -1;
    for (const auto& dev : devices) {
        if (dev.name == deviceName) { foundId = dev.id; break; }
    }
    if (foundId < 0) {
        for (const auto& dev : devices) {
            // Secondary fallback: check if the live device name contains the stored name
            // (handles cases where the stored name is a prefix/substring, e.g. a truncated
            // WinMM name or an ALSA name without the port suffix).
            if (dev.name.find(deviceName) != std::string::npos) {
                foundId = dev.id;
                break;
            }
        }
    }

    if (foundId < 0) {
        if (logger) logger->log("MIDI_CTRL", "openDeviceByName: device not found: " + deviceName);
        return -1;
    }

    if (foundIdOut) *foundIdOut = foundId;

    bool success = controllerInput->open(foundId);
    if (success) {
        if (logger) logger->log("MIDI_CTRL", "Opened MIDI controller by name: " + controllerInput->getDeviceName());
        enabled = true;
        return foundId;
    } else {
        if (logger) logger->log("MIDI_CTRL", "ERROR: openDeviceByName failed: " + controllerInput->getLastError());
        return -1;
    }
}

void MidiControllerManager::closeDevice() {
    if (controllerInput && controllerInput->isOpen()) {
        if (logger) logger->log("MIDI_CTRL", "Closing MIDI controller: " + controllerInput->getDeviceName());
        controllerInput->close();
    }
    enabled = false;
}

bool MidiControllerManager::isDeviceOpen() const {
    return controllerInput && controllerInput->isOpen();
}

std::string MidiControllerManager::getDeviceName() const {
    if (controllerInput && controllerInput->isOpen()) {
        return controllerInput->getDeviceName();
    }
    return "";
}

// ----------------------------------------------------------------------------
// Callback registration
// ----------------------------------------------------------------------------

void MidiControllerManager::setCommandCallback(MidiCommandCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    commandCallback = callback;
}

void MidiControllerManager::setCCValueCallback(MidiCCValueCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    ccValueCallback = callback;
}

void MidiControllerManager::setOverviewTouchCallback(MidiOverviewTouchCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    overviewTouchCallback = callback;
}

// ----------------------------------------------------------------------------
// Mapping management
// ----------------------------------------------------------------------------

void MidiControllerManager::loadMappings(const MidiMappingPreset& preset) {
    currentPreset = preset;
    overviewFaderMappings.clear();
    overviewTouchMappings.clear();
    rebuildLookupMaps();
    if (logger) {
        logger->log("MIDI_CTRL", "Loaded mapping preset: " + preset.name + 
                     " (" + std::to_string(preset.mappings.size()) + " mappings)");
    }
}

void MidiControllerManager::addMapping(const MidiMapping& mapping) {
    currentPreset.mappings.push_back(mapping);
    rebuildLookupMaps();
}

void MidiControllerManager::clearMappings() {
    currentPreset.mappings.clear();
    noteOnMap.clear();
    noteOffMap.clear();
    ccMap.clear();
}

void MidiControllerManager::rebuildLookupMaps() {
    noteOnMap.clear();
    noteOffMap.clear();
    ccMap.clear();
    
    for (auto& mapping : currentPreset.mappings) {
        uint16_t key = (static_cast<uint16_t>(mapping.channel) << 8) | mapping.number;
        
        switch (mapping.triggerType) {
            case MidiMessageType::NOTE_ON:
                noteOnMap[key] = &mapping;
                break;
            case MidiMessageType::NOTE_OFF:
                noteOffMap[key] = &mapping;
                break;
            case MidiMessageType::CONTROL_CHANGE:
                ccMap[key] = &mapping;
                break;
        }
    }
    
    if (logger) {
        logger->log("MIDI_CTRL", "Lookup maps rebuilt: " + 
                     std::to_string(noteOnMap.size()) + " NoteOn, " +
                     std::to_string(noteOffMap.size()) + " NoteOff, " +
                     std::to_string(ccMap.size()) + " CC mappings");
    }
}

// ----------------------------------------------------------------------------
// MIDI event processing
// ----------------------------------------------------------------------------

void MidiControllerManager::onMidiEvent(const MidiControllerEvent& event) {
    if (!enabled.load()) return;
    
    if (logger) {
        std::string typeStr;
        switch (event.type) {
            case MidiMessageType::NOTE_ON: typeStr = "NoteOn"; break;
            case MidiMessageType::NOTE_OFF: typeStr = "NoteOff"; break;
            case MidiMessageType::CONTROL_CHANGE: typeStr = "CC"; break;
        }
        logger->log("MIDI_CTRL", "Event: " + typeStr + 
                     " ch=" + std::to_string(event.channel) + 
                     " d1=" + std::to_string(event.data1) + 
                     " d2=" + std::to_string(event.data2));
    }
    
    switch (event.type) {
        case MidiMessageType::NOTE_ON:
        case MidiMessageType::NOTE_OFF:
            processNoteEvent(event);
            break;
        case MidiMessageType::CONTROL_CHANGE:
            processCCEvent(event);
            break;
    }
}

MidiMapping* MidiControllerManager::findNoteMapping(MidiMessageType type, uint8_t channel, uint8_t note) {
    auto& map = (type == MidiMessageType::NOTE_ON) ? noteOnMap : noteOffMap;
    
    // Try exact channel match first
    uint16_t key = (static_cast<uint16_t>(channel) << 8) | note;
    auto it = map.find(key);
    if (it != map.end()) return it->second;
    
    // Try "any channel" match (channel 255)
    key = (255 << 8) | note;
    it = map.find(key);
    if (it != map.end()) return it->second;
    
    return nullptr;
}

MidiMapping* MidiControllerManager::findCCMapping(uint8_t channel, uint8_t ccNumber) {
    // Try exact channel match first
    uint16_t key = (static_cast<uint16_t>(channel) << 8) | ccNumber;
    auto it = ccMap.find(key);
    if (it != ccMap.end()) return it->second;
    
    // Try "any channel" match (channel 255)
    key = (255 << 8) | ccNumber;
    it = ccMap.find(key);
    if (it != ccMap.end()) return it->second;
    
    return nullptr;
}

void MidiControllerManager::processNoteEvent(const MidiControllerEvent& event) {
    // Note On with velocity 0 is treated as Note Off per MIDI spec
    MidiMessageType effectiveType = event.type;
    if (event.type == MidiMessageType::NOTE_ON && event.data2 == 0) {
        effectiveType = MidiMessageType::NOTE_OFF;
    }
    
    MidiMapping* mapping = findNoteMapping(effectiveType, event.channel, event.data1);
    if (!mapping) return;
    
    if (mapping->command != MidiAppCommand::NONE) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (commandCallback) {
            if (logger) {
                logger->log("MIDI_CTRL", "Triggering command: " + getCommandName(mapping->command) + 
                            " (from " + mapping->description + ")");
            }
            commandCallback(mapping->command);
        }
    }
}

void MidiControllerManager::processCCEvent(const MidiControllerEvent& event) {
    MidiMapping* mapping = findCCMapping(event.channel, event.data1);
    if (!mapping) return;
    
    // Dispatch dynamic overview touch events via overviewTouchCallback
    if (mapping->overviewTouchIndex >= 0) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (overviewTouchCallback) {
            if (logger) {
                logger->log("MIDI_CTRL", "Overview touch " + std::to_string(mapping->overviewTouchIndex) +
                            (event.data2 >= 64 ? " touched" : " released"));
            }
            overviewTouchCallback(mapping->overviewTouchIndex, event.data2 >= 64);
        }
        return;
    }
    
    if (mapping->ccFunction != MidiCCFunction::NONE) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (ccValueCallback) {
            if (logger) {
                logger->log("MIDI_CTRL", "CC value change: " + getCCFunctionName(mapping->ccFunction) + 
                            " = " + std::to_string(event.data2) + 
                            " (from " + mapping->description + ")");
            }
            ccValueCallback(mapping->ccFunction, event.data2);
        }
    }
}

// ----------------------------------------------------------------------------
// Motor fader feedback
// ----------------------------------------------------------------------------

int MidiControllerManager::findCCNumberForFunction(MidiCCFunction func) const {
    for (const auto& mapping : currentPreset.mappings) {
        if (mapping.triggerType == MidiMessageType::CONTROL_CHANGE && mapping.ccFunction == func) {
            return mapping.number;
        }
    }
    return -1;
}

uint8_t MidiControllerManager::findChannelForFunction(MidiCCFunction func) const {
    for (const auto& mapping : currentPreset.mappings) {
        if (mapping.triggerType == MidiMessageType::CONTROL_CHANGE && mapping.ccFunction == func) {
            return mapping.channel;
        }
    }
    return 0;
}

void MidiControllerManager::sendPositionFeedback(double normalizedPosition) {
    if (!controllerInput || !controllerInput->isOpen()) return;
    
    int ccNumber = findCCNumberForFunction(MidiCCFunction::POSITION_X_AXIS);
    if (ccNumber < 0) return;
    
    uint8_t channel = findChannelForFunction(MidiCCFunction::POSITION_X_AXIS);
    int midiValue = normalizedToMidi(normalizedPosition);
    
    // Send CC message: 0xB0 | channel, ccNumber, value
    controllerInput->sendFeedback(0xB0 | channel, static_cast<uint8_t>(ccNumber), static_cast<uint8_t>(midiValue));
    
    if (logger) {
        logger->log("MIDI_CTRL", "Position feedback: " + std::to_string(normalizedPosition) + 
                     " -> CC" + std::to_string(ccNumber) + "=" + std::to_string(midiValue));
    }
}

void MidiControllerManager::sendCurveValueFeedback(int curveIndex, double normalizedValue) {
    if (!controllerInput || !controllerInput->isOpen()) return;
    if (curveIndex < 0 || curveIndex > 4) return;
    
    // Map curve index to CC function
    MidiCCFunction func;
    switch (curveIndex) {
        case 0: func = MidiCCFunction::CURVE_VALUE_SWR; break;
        case 1: func = MidiCCFunction::CURVE_VALUE_RL; break;
        case 2: func = MidiCCFunction::CURVE_VALUE_IMPEDANCE; break;
        case 3: func = MidiCCFunction::CURVE_VALUE_REACTANCE; break;
        case 4: func = MidiCCFunction::CURVE_VALUE_PHASE; break;
        default: return;
    }
    
    int ccNumber = findCCNumberForFunction(func);
    if (ccNumber < 0) return;
    
    uint8_t channel = findChannelForFunction(func);
    int midiValue = normalizedToMidi(normalizedValue);
    
    controllerInput->sendFeedback(0xB0 | channel, static_cast<uint8_t>(ccNumber), static_cast<uint8_t>(midiValue));
}

void MidiControllerManager::sendMasterVolumeFeedback(int volumePercent) {
    if (!controllerInput || !controllerInput->isOpen()) return;
    
    int ccNumber = findCCNumberForFunction(MidiCCFunction::MASTER_VOLUME);
    if (ccNumber < 0) return;
    
    uint8_t channel = findChannelForFunction(MidiCCFunction::MASTER_VOLUME);
    int midiValue = percentToMidi(volumePercent, 100);
    
    controllerInput->sendFeedback(0xB0 | channel, static_cast<uint8_t>(ccNumber), static_cast<uint8_t>(midiValue));
}

void MidiControllerManager::sendCurveVolumeFeedback(int curveIndex, int volumePercent) {
    if (!controllerInput || !controllerInput->isOpen()) return;
    if (curveIndex < 0 || curveIndex > 4) return;
    
    MidiCCFunction func;
    switch (curveIndex) {
        case 0: func = MidiCCFunction::CURVE_VOLUME_1; break;
        case 1: func = MidiCCFunction::CURVE_VOLUME_2; break;
        case 2: func = MidiCCFunction::CURVE_VOLUME_3; break;
        case 3: func = MidiCCFunction::CURVE_VOLUME_4; break;
        case 4: func = MidiCCFunction::CURVE_VOLUME_5; break;
        default: return;
    }
    
    int ccNumber = findCCNumberForFunction(func);
    if (ccNumber < 0) return;
    
    uint8_t channel = findChannelForFunction(func);
    // Curve volume is 0-200%, scale to 0-127
    int midiValue = percentToMidi(volumePercent, 200);
    
    controllerInput->sendFeedback(0xB0 | channel, static_cast<uint8_t>(ccNumber), static_cast<uint8_t>(midiValue));
}

void MidiControllerManager::sendOverviewFaderFeedback(int faderIndex, double normalizedValue) {
    if (!controllerInput || !controllerInput->isOpen()) return;
    
    // Look up the CC number and channel for this overview fader (1-based index)
    for (const auto& entry : overviewFaderMappings) {
        if (entry.index == faderIndex) {
            int midiValue = normalizedToMidi(normalizedValue);
            controllerInput->sendFeedback(0xB0 | entry.channel, entry.ccNumber, static_cast<uint8_t>(midiValue));
            return;
        }
    }
}

void MidiControllerManager::setEnabled(bool en) {
    enabled = en;
    if (logger) {
        logger->log("MIDI_CTRL", std::string("MIDI controller ") + (en ? "enabled" : "disabled"));
    }
}

// ----------------------------------------------------------------------------
// Preset file I/O (simple key=value format matching app_settings pattern)
// ----------------------------------------------------------------------------

bool MidiControllerManager::loadMappingsFromFile(const std::string& filepath) {
    // Basic path traversal protection: reject paths with ".."
    if (filepath.find("..") != std::string::npos) {
        if (logger) logger->log("MIDI_CTRL", "ERROR: Path traversal rejected: " + filepath);
        return false;
    }
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        if (logger) logger->log("MIDI_CTRL", "ERROR: Cannot open mapping file: " + filepath);
        return false;
    }
    
    MidiMappingPreset preset;
    std::string line;
    
    // Temporary storage for dynamic overview entries (populated before loadMappings clears them)
    std::vector<OverviewFaderEntry> tempFaderEntries;
    std::vector<OverviewFaderEntry> tempTouchEntries;
    
    // Helper: try to parse "overview_fader_N" or "overview_touch_N", return index (>=1) or 0 if no match
    auto parseOverviewFaderKey = [](const std::string& s, bool wantTouch) -> int {
        const std::string prefix = wantTouch ? "overview_touch_" : "overview_fader_";
        if (s.substr(0, prefix.size()) != prefix) return 0;
        std::string suffix = s.substr(prefix.size());
        if (suffix.empty()) return 0;
        for (char c : suffix) { if (!std::isdigit(static_cast<unsigned char>(c))) return 0; }
        int idx = std::stoi(suffix);
        return (idx >= 1) ? idx : 0;
    };
    
    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        
        // Skip comments
        if (line[0] == '#') continue;
        
        // Parse key=value
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        
        // Trim key and value
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        start = value.find_first_not_of(" \t");
        if (start != std::string::npos) value = value.substr(start);
        
        if (key == "preset_name") preset.name = value;
        else if (key == "preset_description") preset.description = value;
        else if (key == "controller_name") preset.controllerName = value;
        else {
            // Any other key is treated as a mapping entry.
            // Supports multiple formats:
            // Old: mapping_N=type,channel,number,command_int,ccfunction_int,description
            // Named: function_name=type,channel,number,command_name,ccfunction_name,description
            std::istringstream ss(value);
            std::string token;
            std::vector<std::string> tokens;
            while (std::getline(ss, token, ',')) {
                // Trim each token
                size_t s = token.find_first_not_of(" \t");
                size_t e = token.find_last_not_of(" \t");
                if (s != std::string::npos) token = token.substr(s, e - s + 1);
                tokens.push_back(token);
            }
            
            if (tokens.size() >= 5) {
                MidiMapping m;
                
                if (tokens[0] == "noteon") m.triggerType = MidiMessageType::NOTE_ON;
                else if (tokens[0] == "noteoff") m.triggerType = MidiMessageType::NOTE_OFF;
                else if (tokens[0] == "cc") m.triggerType = MidiMessageType::CONTROL_CHANGE;
                else continue;
                
                try {
                    int channel = std::stoi(tokens[1]);
                    int number = std::stoi(tokens[2]);
                    
                    // Validate channel and number ranges
                    if (channel < 0 || channel > 255) continue;
                    if (number < 0 || number > 127) continue;
                    m.channel = static_cast<uint8_t>(channel);
                    m.number = static_cast<uint8_t>(number);
                    
                    // Try parsing command as name first, then as integer (backward compatible)
                    MidiAppCommand resolvedCmd = resolveCommandKeyName(tokens[3]);
                    if (resolvedCmd != MidiAppCommand::NONE || tokens[3] == "none") {
                        m.command = resolvedCmd;
                    } else {
                        int command = std::stoi(tokens[3]);
                        if (command < 0 || command >= static_cast<int>(MidiAppCommand::COMMAND_COUNT)) continue;
                        m.command = static_cast<MidiAppCommand>(command);
                    }
                    
                    // Check for dynamic overview_fader_N or overview_touch_N ccfunction name
                    int overviewFaderIdx = parseOverviewFaderKey(tokens[4], false);
                    int overviewTouchIdx = parseOverviewFaderKey(tokens[4], true);
                    
                    if (overviewFaderIdx > 0 && m.triggerType == MidiMessageType::CONTROL_CHANGE) {
                        // overview_fader_N: output-only, store in tempFaderEntries
                        OverviewFaderEntry entry;
                        entry.index = overviewFaderIdx;
                        entry.channel = m.channel;
                        entry.ccNumber = m.number;
                        tempFaderEntries.push_back(entry);
                        // Do NOT add to preset.mappings (no input dispatch needed)
                        continue;
                    } else if (overviewTouchIdx > 0 && m.triggerType == MidiMessageType::CONTROL_CHANGE) {
                        // overview_touch_N: input, store in tempTouchEntries + add to mappings for dispatch
                        OverviewFaderEntry entry;
                        entry.index = overviewTouchIdx;
                        entry.channel = m.channel;
                        entry.ccNumber = m.number;
                        tempTouchEntries.push_back(entry);
                        m.ccFunction = MidiCCFunction::NONE;
                        m.overviewTouchIndex = overviewTouchIdx;
                    } else {
                        // Try parsing CC function as name first, then as integer (backward compatible)
                        MidiCCFunction resolvedFunc = resolveCCFunctionKeyName(tokens[4]);
                        if (resolvedFunc != MidiCCFunction::NONE || tokens[4] == "none") {
                            m.ccFunction = resolvedFunc;
                        } else {
                            int ccFunc = std::stoi(tokens[4]);
                            if (ccFunc < 0 || ccFunc >= static_cast<int>(MidiCCFunction::FUNCTION_COUNT)) continue;
                            m.ccFunction = static_cast<MidiCCFunction>(ccFunc);
                        }
                    }
                } catch (const std::exception&) {
                    continue;  // Skip malformed entries
                }
                
                if (tokens.size() >= 6) {
                    m.description = tokens[5];
                }
                
                preset.mappings.push_back(m);
            }
        }
    }
    
    loadMappings(preset);
    // Restore dynamic overview entries (loadMappings clears them)
    overviewFaderMappings = std::move(tempFaderEntries);
    overviewTouchMappings = std::move(tempTouchEntries);
    
    if (logger) {
        logger->log("MIDI_CTRL", "Loaded preset from file: " + filepath + 
                     " (" + std::to_string(preset.mappings.size()) + " mappings, " +
                     std::to_string(overviewFaderMappings.size()) + " overview faders, " +
                     std::to_string(overviewTouchMappings.size()) + " overview touches)");
    }
    return true;
}

bool MidiControllerManager::saveMappingsToFile(const std::string& filepath) const {
    // Basic path traversal protection
    if (filepath.find("..") != std::string::npos) {
        if (logger) logger->log("MIDI_CTRL", "ERROR: Path traversal rejected: " + filepath);
        return false;
    }
    
    std::ofstream file(filepath);
    if (!file.is_open()) {
        if (logger) logger->log("MIDI_CTRL", "ERROR: Cannot write mapping file: " + filepath);
        return false;
    }
    
    file << "# MIDI Controller Mapping Preset\n";
    file << "# Generated by NanoVNA CLI Accessible CPA\n";
    file << "#\n";
    file << "# Mapping format: function_name=type,channel,number,command_name,ccfunction_name,description\n";
    file << "# type: noteon, noteoff, cc\n";
    file << "# channel: 0-15 (255 = any channel)\n";
    file << "# number: MIDI note or CC number (0-127)\n";
    file << "# command_name: symbolic name for command (e.g., toggle_curve_swr, solo_curve_rl, none)\n";
    file << "# ccfunction_name: symbolic name for CC function (e.g., curve_volume_swr, master_volume, none)\n\n";
    
    file << "preset_name=" << currentPreset.name << "\n";
    file << "preset_description=" << currentPreset.description << "\n";
    file << "controller_name=" << currentPreset.controllerName << "\n\n";
    
    int idx = 0;
    for (const auto& m : currentPreset.mappings) {
        std::string typeStr;
        switch (m.triggerType) {
            case MidiMessageType::NOTE_ON: typeStr = "noteon"; break;
            case MidiMessageType::NOTE_OFF: typeStr = "noteoff"; break;
            case MidiMessageType::CONTROL_CHANGE: typeStr = "cc"; break;
        }
        
        // Build a descriptive key from the command or CC function name
        std::string cmdKey = getCommandKeyName(m.command);
        std::string ccKey = getCCFunctionKeyName(m.ccFunction);
        std::string mapKey;
        if (m.command != MidiAppCommand::NONE) {
            mapKey = cmdKey;
        } else if (m.ccFunction != MidiCCFunction::NONE) {
            mapKey = ccKey;
        } else {
            mapKey = "unknown_" + std::to_string(idx);
        }
        
        file << mapKey << "=" 
             << typeStr << ","
             << static_cast<int>(m.channel) << ","
             << static_cast<int>(m.number) << ","
             << cmdKey << ","
             << ccKey << ","
             << m.description << "\n";
        idx++;
    }
    
    if (logger) {
        logger->log("MIDI_CTRL", "Saved preset to file: " + filepath);
    }
    return true;
}

std::vector<std::string> MidiControllerManager::listPresetFiles(const std::string& directory) const {
    std::vector<std::string> files;
    try {
        if (std::filesystem::exists(directory)) {
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".cfg" || ext == ".preset" || ext == ".txt") {
                        files.push_back(entry.path().filename().string());
                    }
                }
            }
            std::sort(files.begin(), files.end());
        }
    } catch (const std::exception& e) {
        if (logger) logger->log("MIDI_CTRL", "ERROR: Failed to list preset directory: " + std::string(e.what()));
    }
    return files;
}
