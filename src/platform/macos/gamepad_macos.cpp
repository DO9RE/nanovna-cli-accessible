#include "gamepad_interface.h"
#include "gamepad_utils.h"
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <mutex>
#include <cstdio>

#ifdef __APPLE__
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

static constexpr int MAX_CONTROLLERS = 4;

// Debug logging macro — always enabled for gamepad to help diagnose connection issues
#define GAMEPAD_LOG(fmt, ...) std::fprintf(stderr, "[GAMEPAD_MACOS] " fmt "\n", ##__VA_ARGS__)

#ifdef __APPLE__

// Per-device state tracked alongside each IOHIDDeviceRef
enum class ControllerType {
    XBOX_COMPATIBLE,   // Xbox and generic HID gamepads
    PS4_DUALSHOCK,     // Sony DualShock 4
    PS5_DUALSENSE      // Sony DualSense
};

struct DeviceInfo {
    IOHIDDeviceRef device;
    std::string name;
    int slotIndex; // index in the controllers array (0..3)
    ControllerType controllerType;     // currently active mapping type
    ControllerType autoDetectedType;   // type detected by VID/PID (for preset=Auto restore)
};

// Sony vendor ID
static constexpr int32_t SONY_VENDOR_ID = 0x054C;
// Sony DualShock 4 product IDs (v1 and v2)
static constexpr int32_t DS4_PID_V1 = 0x05C4;
static constexpr int32_t DS4_PID_V2 = 0x09CC;
// Sony DualSense product ID
static constexpr int32_t DUALSENSE_PID = 0x0CE6;
static constexpr int32_t DUALSENSE_EDGE_PID = 0x0DF2;

static ControllerType detectControllerType(int32_t vendorId, int32_t productId) {
    if (vendorId == SONY_VENDOR_ID) {
        if (productId == DS4_PID_V1 || productId == DS4_PID_V2) {
            return ControllerType::PS4_DUALSHOCK;
        }
        if (productId == DUALSENSE_PID || productId == DUALSENSE_EDGE_PID) {
            return ControllerType::PS5_DUALSENSE;
        }
    }
    return ControllerType::XBOX_COMPATIBLE;
}

/// Apply a preset override to a controller type.
/// @param preset 0=Auto (use autoDetected), 1=Xbox, 2=PlayStation
static ControllerType applyPreset(int preset, ControllerType autoDetected) {
    if (preset == 1) return ControllerType::XBOX_COMPATIBLE;
    if (preset == 2) return ControllerType::PS4_DUALSHOCK;
    return autoDetected;  // preset 0: use auto-detected type
}

/**
 * IOKit HID Manager gamepad implementation for macOS.
 * Enumerates game controllers (gamepads, joysticks, multi-axis controllers)
 * and polls button/axis state each frame.
 */
class MacOSGamepadInput : public IGamepadInput {
public:
    MacOSGamepadInput()
        : initialized(false), deadzone(0.15f), presetOverride(0), hidManager(nullptr),
          scheduledRunLoop(nullptr) {
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            currentStates[i] = GamepadState();
            previousStates[i] = GamepadState();
        }
    }

    ~MacOSGamepadInput() override {
        shutdown();
    }

    bool initialize() override {
        if (initialized) {
            return true;
        }

        GAMEPAD_LOG("Initializing IOKit HID Manager...");

        hidManager = IOHIDManagerCreate(kCFAllocatorDefault,
                                        kIOHIDOptionsTypeNone);
        if (!hidManager) {
            GAMEPAD_LOG("ERROR: Failed to create IOHIDManager");
            return false;
        }

        // Build matching dictionaries for gamepads, joysticks, multi-axis
        CFMutableDictionaryRef matchGamepad = createMatchingDict(
            kHIDPage_GenericDesktop, kHIDUsage_GD_GamePad);
        CFMutableDictionaryRef matchJoystick = createMatchingDict(
            kHIDPage_GenericDesktop, kHIDUsage_GD_Joystick);
        CFMutableDictionaryRef matchMultiAxis = createMatchingDict(
            kHIDPage_GenericDesktop, kHIDUsage_GD_MultiAxisController);

        if (!matchGamepad || !matchJoystick || !matchMultiAxis) {
            GAMEPAD_LOG("ERROR: Failed to create matching dictionaries");
            if (matchGamepad)   CFRelease(matchGamepad);
            if (matchJoystick)  CFRelease(matchJoystick);
            if (matchMultiAxis) CFRelease(matchMultiAxis);
            CFRelease(hidManager);
            hidManager = nullptr;
            return false;
        }

        CFMutableDictionaryRef items[] = {
            matchGamepad, matchJoystick, matchMultiAxis
        };
        const void* voidItems[] = { items[0], items[1], items[2] };
        CFArrayRef matchArray = CFArrayCreate(
            kCFAllocatorDefault,
            voidItems, 3,
            &kCFTypeArrayCallBacks);

        IOHIDManagerSetDeviceMatchingMultiple(hidManager, matchArray);
        CFRelease(matchArray);
        GAMEPAD_LOG("Set device matching for GamePad, Joystick, MultiAxisController");

        // Register connect/disconnect callbacks
        IOHIDManagerRegisterDeviceMatchingCallback(
            hidManager, deviceAddedCallback, this);
        IOHIDManagerRegisterDeviceRemovalCallback(
            hidManager, deviceRemovedCallback, this);

        // Schedule on main run loop — this is where AppKit events are processed
        // and where pumpHamSpiritEvents() pumps. Using CFRunLoopGetCurrent()
        // would fail if initialize() and update() run on different threads.
        scheduledRunLoop = CFRunLoopGetMain();
        IOHIDManagerScheduleWithRunLoop(
            hidManager, scheduledRunLoop, kCFRunLoopDefaultMode);
        GAMEPAD_LOG("Scheduled on main run loop");

        IOReturn ret = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
        if (ret != kIOReturnSuccess) {
            GAMEPAD_LOG("ERROR: IOHIDManagerOpen failed with IOReturn %d", ret);
            CFRelease(hidManager);
            hidManager = nullptr;
            return false;
        }
        GAMEPAD_LOG("IOHIDManager opened successfully");

        // Process any already-connected devices.
        // A non-zero timeout (100ms) gives IOKit time to deliver device-matching
        // callbacks for controllers that were connected before our HID manager
        // was opened. With timeout=0.0 these callbacks often arrive too late.
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);

        initialized = true;
        GAMEPAD_LOG("Initialization complete, %d device(s) found", getConnectedCount());
        return true;
    }

    void shutdown() override {
        if (!initialized) {
            return;
        }

        GAMEPAD_LOG("Shutting down IOKit HID Manager");

        if (hidManager) {
            IOHIDManagerUnscheduleFromRunLoop(
                hidManager, scheduledRunLoop, kCFRunLoopDefaultMode);
            IOHIDManagerClose(hidManager, kIOHIDOptionsTypeNone);
            CFRelease(hidManager);
            hidManager = nullptr;
        }

        std::lock_guard<std::mutex> lock(deviceMutex);
        devices.clear();
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            currentStates[i] = GamepadState();
            previousStates[i] = GamepadState();
        }

        initialized = false;
        GAMEPAD_LOG("Shutdown complete");
    }

    void update() override {
        if (!initialized) {
            return;
        }

        // Save previous states
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            previousStates[i] = currentStates[i];
        }

        // Process pending HID events (connect/disconnect, value changes)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.0, false);

        // Poll each connected device
        {
            std::lock_guard<std::mutex> lock(deviceMutex);
            for (auto& dev : devices) {
                pollDevice(dev);
            }
        }

        // Fire event callbacks for connect/disconnect and button/axis changes
        if (eventCallback) {
            for (int i = 0; i < MAX_CONTROLLERS; i++) {
                if (currentStates[i].connected != previousStates[i].connected) {
                    InputEvent event;
                    event.controllerIndex = i;
                    event.type = currentStates[i].connected
                                     ? InputEventType::CONNECTED
                                     : InputEventType::DISCONNECTED;
                    eventCallback(event);
                }

                if (!currentStates[i].connected) continue;

                for (int b = 0; b < static_cast<int>(GamepadButton::COUNT); b++) {
                    if (currentStates[i].buttons[b] && !previousStates[i].buttons[b]) {
                        InputEvent event;
                        event.controllerIndex = i;
                        event.type = InputEventType::BUTTON_PRESSED;
                        event.button = static_cast<GamepadButton>(b);
                        eventCallback(event);
                    } else if (!currentStates[i].buttons[b] && previousStates[i].buttons[b]) {
                        InputEvent event;
                        event.controllerIndex = i;
                        event.type = InputEventType::BUTTON_RELEASED;
                        event.button = static_cast<GamepadButton>(b);
                        eventCallback(event);
                    }
                }

                for (int a = 0; a < static_cast<int>(GamepadAxis::COUNT); a++) {
                    if (currentStates[i].axes[a] != previousStates[i].axes[a]) {
                        InputEvent event;
                        event.controllerIndex = i;
                        event.type = InputEventType::AXIS_MOVED;
                        event.axis = static_cast<GamepadAxis>(a);
                        event.value = currentStates[i].axes[a];
                        eventCallback(event);
                    }
                }
            }
        }
    }

    int getConnectedCount() const override {
        int count = 0;
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            if (currentStates[i].connected) count++;
        }
        return count;
    }

    bool isConnected(int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS) return false;
        return currentStates[index].connected;
    }

    GamepadState getState(int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS) return GamepadState();
        return currentStates[index];
    }

    bool isButtonPressed(GamepadButton button, int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected)
            return false;
        return currentStates[index].buttons[static_cast<int>(button)];
    }

    bool wasButtonPressed(GamepadButton button, int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected)
            return false;
        int idx = static_cast<int>(button);
        return currentStates[index].buttons[idx] && !previousStates[index].buttons[idx];
    }

    bool wasButtonReleased(GamepadButton button, int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected)
            return false;
        int idx = static_cast<int>(button);
        return !currentStates[index].buttons[idx] && previousStates[index].buttons[idx];
    }

    float getAxisValue(GamepadAxis axis, int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected)
            return 0.0f;

        float value = currentStates[index].axes[static_cast<int>(axis)];

        // Apply deadzone with range scaling for sticks (not triggers)
        if (axis != GamepadAxis::LEFT_TRIGGER && axis != GamepadAxis::RIGHT_TRIGGER) {
            if (std::abs(value) < deadzone) {
                return 0.0f;
            }
            float sign = value > 0 ? 1.0f : -1.0f;
            return sign * ((std::abs(value) - deadzone) / (1.0f - deadzone));
        }

        return value;
    }

    void setDeadzone(float dz) override {
        deadzone = std::max(0.0f, std::min(1.0f, dz));
    }

    float getDeadzone() const override {
        return deadzone;
    }

    bool setVibration(int index, float leftMotor,
                      float rightMotor) override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected)
            return false;

        std::lock_guard<std::mutex> lock(deviceMutex);
        for (const auto& dev : devices) {
            if (dev.slotIndex == index) {
                if (dev.controllerType == ControllerType::PS4_DUALSHOCK) {
                    return sendDS4Vibration(dev.device, leftMotor, rightMotor);
                }
                if (dev.controllerType == ControllerType::PS5_DUALSENSE) {
                    return sendDualSenseVibration(dev.device, leftMotor, rightMotor);
                }
                // Xbox/generic — IOKit HID has no standard rumble API
                return false;
            }
        }
        return false;
    }

    std::string getControllerName(int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected)
            return "Not connected";

        std::lock_guard<std::mutex> lock(deviceMutex);
        for (const auto& dev : devices) {
            if (dev.slotIndex == index) {
                return dev.name;
            }
        }
        return "Gamepad " + std::to_string(index);
    }

    void setEventCallback(std::function<void(const InputEvent&)> callback) override {
        eventCallback = callback;
    }

    void setControllerPreset(int preset) override {
        presetOverride = preset;
        // Apply to all connected devices
        std::lock_guard<std::mutex> lock(deviceMutex);
        for (auto& dev : devices) {
            dev.controllerType = applyPreset(preset, dev.autoDetectedType);
            const char* typeStr = "Xbox/Generic";
            if (dev.controllerType == ControllerType::PS4_DUALSHOCK) typeStr = "PS4";
            else if (dev.controllerType == ControllerType::PS5_DUALSENSE) typeStr = "PS5";
            GAMEPAD_LOG("Preset applied: slot %d → %s (preset=%d)", dev.slotIndex, typeStr, preset);
        }
    }

private:
    bool initialized;
    float deadzone;
    int presetOverride;  // 0=auto, 1=Xbox, 2=PS
    IOHIDManagerRef hidManager;
    CFRunLoopRef scheduledRunLoop;  // Run loop the HID manager is scheduled on
    GamepadState currentStates[MAX_CONTROLLERS];
    GamepadState previousStates[MAX_CONTROLLERS];
    std::function<void(const InputEvent&)> eventCallback;

    std::vector<DeviceInfo> devices;
    mutable std::mutex deviceMutex;

    // -----------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------

    static CFMutableDictionaryRef createMatchingDict(uint32_t usagePage,
                                                     uint32_t usage) {
        CFMutableDictionaryRef dict = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        if (!dict) return nullptr;

        CFNumberRef pageNum = CFNumberCreate(
            kCFAllocatorDefault, kCFNumberIntType, &usagePage);
        CFNumberRef usageNum = CFNumberCreate(
            kCFAllocatorDefault, kCFNumberIntType, &usage);

        CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsagePageKey), pageNum);
        CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsageKey), usageNum);

        CFRelease(pageNum);
        CFRelease(usageNum);
        return dict;
    }

    static std::string getDeviceName(IOHIDDeviceRef device) {
        CFStringRef cfName = static_cast<CFStringRef>(
            IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey)));
        if (cfName) {
            char buf[256];
            if (CFStringGetCString(cfName, buf, sizeof(buf),
                                   kCFStringEncodingUTF8)) {
                return std::string(buf);
            }
        }
        return "Unknown Controller";
    }

    int findFreeSlot() const {
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            bool used = false;
            for (const auto& d : devices) {
                if (d.slotIndex == i) { used = true; break; }
            }
            if (!used) return i;
        }
        return -1;
    }

    // -----------------------------------------------------------
    // Device matching / removal callbacks (static → instance)
    // -----------------------------------------------------------

    static void deviceAddedCallback(void* ctx, IOReturn /*result*/,
                                    void* /*sender*/,
                                    IOHIDDeviceRef device) {
        auto* self = static_cast<MacOSGamepadInput*>(ctx);
        self->onDeviceAdded(device);
    }

    static void deviceRemovedCallback(void* ctx, IOReturn /*result*/,
                                      void* /*sender*/,
                                      IOHIDDeviceRef device) {
        auto* self = static_cast<MacOSGamepadInput*>(ctx);
        self->onDeviceRemoved(device);
    }

    void onDeviceAdded(IOHIDDeviceRef device) {
        std::string name = getDeviceName(device);

        // Log vendor and product IDs for debugging
        int32_t vendorId = 0, productId = 0;
        CFNumberRef vendorRef = static_cast<CFNumberRef>(
            IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey)));
        CFNumberRef productRef = static_cast<CFNumberRef>(
            IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey)));
        if (vendorRef) CFNumberGetValue(vendorRef, kCFNumberSInt32Type, &vendorId);
        if (productRef) CFNumberGetValue(productRef, kCFNumberSInt32Type, &productId);

        GAMEPAD_LOG("Device connected: '%s' (VID=0x%04X PID=0x%04X)",
                    name.c_str(), vendorId, productId);

        std::lock_guard<std::mutex> lock(deviceMutex);

        // Already tracked?
        for (const auto& d : devices) {
            if (d.device == device) {
                GAMEPAD_LOG("  -> Already tracked in slot %d, ignoring", d.slotIndex);
                return;
            }
        }

        int slot = findFreeSlot();
        if (slot < 0) {
            GAMEPAD_LOG("  -> No free slot available (max %d), ignoring", MAX_CONTROLLERS);
            return;
        }

        DeviceInfo info;
        info.device = device;
        info.name = name;
        info.slotIndex = slot;
        info.autoDetectedType = detectControllerType(vendorId, productId);
        info.controllerType = applyPreset(presetOverride, info.autoDetectedType);
        devices.push_back(info);

        const char* typeStr = "Xbox/Generic";
        if (info.controllerType == ControllerType::PS4_DUALSHOCK) typeStr = "PS4 DualShock";
        else if (info.controllerType == ControllerType::PS5_DUALSENSE) typeStr = "PS5 DualSense";

        currentStates[slot] = GamepadState();
        currentStates[slot].connected = true;
        GAMEPAD_LOG("  -> Assigned to slot %d as %s (total: %d devices)", slot, typeStr, (int)devices.size());
    }

    void onDeviceRemoved(IOHIDDeviceRef device) {
        std::lock_guard<std::mutex> lock(deviceMutex);

        for (auto it = devices.begin(); it != devices.end(); ++it) {
            if (it->device == device) {
                int slot = it->slotIndex;
                GAMEPAD_LOG("Device disconnected: '%s' (slot %d)", it->name.c_str(), slot);
                currentStates[slot] = GamepadState(); // resets connected=false
                devices.erase(it);
                return;
            }
        }
        GAMEPAD_LOG("Device removed callback for unknown device");
    }

    // -----------------------------------------------------------
    // Per-frame device polling
    // -----------------------------------------------------------

    void pollDevice(const DeviceInfo& dev) {
        int slot = dev.slotIndex;
        IOHIDDeviceRef device = dev.device;

        // Reset buttons each frame; axes keep 0 defaults from GamepadState()
        for (int b = 0; b < static_cast<int>(GamepadButton::COUNT); b++)
            currentStates[slot].buttons[b] = false;
        for (int a = 0; a < static_cast<int>(GamepadAxis::COUNT); a++)
            currentStates[slot].axes[a] = 0.0f;
        currentStates[slot].connected = true;

        CFArrayRef elements = IOHIDDeviceCopyMatchingElements(
            device, nullptr, kIOHIDOptionsTypeNone);
        if (!elements) return;

        CFIndex count = CFArrayGetCount(elements);
        for (CFIndex i = 0; i < count; i++) {
            IOHIDElementRef elem = static_cast<IOHIDElementRef>(
                const_cast<void*>(CFArrayGetValueAtIndex(elements, i)));

            IOHIDElementType type = IOHIDElementGetType(elem);
            if (type != kIOHIDElementTypeInput_Misc &&
                type != kIOHIDElementTypeInput_Button &&
                type != kIOHIDElementTypeInput_Axis) {
                continue;
            }

            uint32_t usagePage = IOHIDElementGetUsagePage(elem);
            uint32_t usage     = IOHIDElementGetUsage(elem);

            IOHIDValueRef valueRef = nullptr;
            if (IOHIDDeviceGetValue(device, elem, &valueRef) != kIOReturnSuccess
                || !valueRef) {
                continue;
            }
            CFIndex rawValue = IOHIDValueGetIntegerValue(valueRef);

            CFIndex logMin = IOHIDElementGetLogicalMin(elem);
            CFIndex logMax = IOHIDElementGetLogicalMax(elem);

            if (usagePage == kHIDPage_Button) {
                // HID button usages are 1-based
                mapButton(slot, usage, rawValue != 0, dev.controllerType);
            } else if (usagePage == kHIDPage_GenericDesktop) {
                bool isPS = (dev.controllerType == ControllerType::PS4_DUALSHOCK ||
                             dev.controllerType == ControllerType::PS5_DUALSENSE);
                switch (usage) {
                    case kHIDUsage_GD_X:  // Left stick X (same for Xbox and PS)
                        currentStates[slot].axes[static_cast<int>(
                            GamepadAxis::LEFT_X)] =
                            normalizeAxis(rawValue, logMin, logMax);
                        break;
                    case kHIDUsage_GD_Y:  // Left stick Y (same for Xbox and PS)
                        currentStates[slot].axes[static_cast<int>(
                            GamepadAxis::LEFT_Y)] =
                            normalizeAxis(rawValue, logMin, logMax);
                        break;
                    case kHIDUsage_GD_Z:
                        if (isPS) {
                            // PS4/PS5: Z = Right Stick X
                            currentStates[slot].axes[static_cast<int>(
                                GamepadAxis::RIGHT_X)] =
                                normalizeAxis(rawValue, logMin, logMax);
                        } else {
                            // Xbox: Z = Left Trigger
                            currentStates[slot].axes[static_cast<int>(
                                GamepadAxis::LEFT_TRIGGER)] =
                                normalizeTrigger(rawValue, logMin, logMax);
                        }
                        break;
                    case kHIDUsage_GD_Rx:
                        if (isPS) {
                            // PS4/PS5: Rx = Left Trigger
                            currentStates[slot].axes[static_cast<int>(
                                GamepadAxis::LEFT_TRIGGER)] =
                                normalizeTrigger(rawValue, logMin, logMax);
                        } else {
                            // Xbox: Rx = Right Stick X
                            currentStates[slot].axes[static_cast<int>(
                                GamepadAxis::RIGHT_X)] =
                                normalizeAxis(rawValue, logMin, logMax);
                        }
                        break;
                    case kHIDUsage_GD_Ry:
                        if (isPS) {
                            // PS4/PS5: Ry = Right Trigger
                            currentStates[slot].axes[static_cast<int>(
                                GamepadAxis::RIGHT_TRIGGER)] =
                                normalizeTrigger(rawValue, logMin, logMax);
                        } else {
                            // Xbox: Ry = Right Stick Y
                            currentStates[slot].axes[static_cast<int>(
                                GamepadAxis::RIGHT_Y)] =
                                normalizeAxis(rawValue, logMin, logMax);
                        }
                        break;
                    case kHIDUsage_GD_Rz:
                        if (isPS) {
                            // PS4/PS5: Rz = Right Stick Y
                            currentStates[slot].axes[static_cast<int>(
                                GamepadAxis::RIGHT_Y)] =
                                normalizeAxis(rawValue, logMin, logMax);
                        } else {
                            // Xbox: Rz = Right Trigger
                            currentStates[slot].axes[static_cast<int>(
                                GamepadAxis::RIGHT_TRIGGER)] =
                                normalizeTrigger(rawValue, logMin, logMax);
                        }
                        break;
                    case kHIDUsage_GD_Hatswitch:
                        mapHatSwitch(slot, rawValue, logMin, logMax);
                        break;
                    default:
                        break;
                }
            }
        }
        CFRelease(elements);
    }

    // ─── Vibration via HID output reports ────────────────────────────────────

    /// Send a DualShock 4 USB vibration output report (report ID 0x05, 32 bytes).
    static bool sendDS4Vibration(IOHIDDeviceRef device, float leftMotor, float rightMotor) {
        uint8_t report[32] = {};
        report[0] = 0x05;           // Report ID for USB rumble
        report[1] = 0xFF;           // Enable rumble + LED
        report[4] = GamepadUtils::floatToU8(rightMotor); // Right (weak) motor
        report[5] = GamepadUtils::floatToU8(leftMotor);  // Left (strong) motor

        IOReturn ret = IOHIDDeviceSetReport(device,
                                             kIOHIDReportTypeOutput,
                                             0x05,  // report ID
                                             report,
                                             sizeof(report));
        if (ret != kIOReturnSuccess) {
            GAMEPAD_LOG("DS4 vibration failed: IOReturn %d", ret);
        }
        return ret == kIOReturnSuccess;
    }

    /// Send a DualSense vibration output report (report ID 0x02, 48 bytes).
    static bool sendDualSenseVibration(IOHIDDeviceRef device, float leftMotor, float rightMotor) {
        uint8_t report[48] = {};
        report[0] = 0x02;           // Report ID for USB output
        report[1] = 0xFF;           // Feature flags byte 0 (enable haptics)
        report[2] = 0x15;           // Feature flags byte 1
        report[3] = GamepadUtils::floatToU8(rightMotor); // Right (weak) motor
        report[4] = GamepadUtils::floatToU8(leftMotor);  // Left (strong) motor

        IOReturn ret = IOHIDDeviceSetReport(device,
                                             kIOHIDReportTypeOutput,
                                             0x02,
                                             report,
                                             sizeof(report));
        if (ret != kIOReturnSuccess) {
            GAMEPAD_LOG("DualSense vibration failed: IOReturn %d", ret);
        }
        return ret == kIOReturnSuccess;
    }

    // Normalize a stick axis from [logMin..logMax] → [-1.0 .. +1.0]
    static float normalizeAxis(CFIndex value, CFIndex logMin, CFIndex logMax) {
        return GamepadUtils::normalizeAxis(static_cast<int>(value), static_cast<int>(logMin), static_cast<int>(logMax));
    }

    // Normalize a trigger axis from [logMin..logMax] → [0.0 .. 1.0]
    static float normalizeTrigger(CFIndex value, CFIndex logMin, CFIndex logMax) {
        return GamepadUtils::normalizeTrigger(static_cast<int>(value), static_cast<int>(logMin), static_cast<int>(logMax));
    }

    // Map HID button usage (1-based) to GamepadButton enum.
    // PS4 DualShock 4 / PS5 DualSense use a different button numbering than Xbox.
    //
    // PS4 DS4 HID mapping on macOS (via IOKit):
    //   1 = Square (→ X)       2 = Cross (→ A)
    //   3 = Circle (→ B)       4 = Triangle (→ Y)
    //   5 = L1 (→ LB)          6 = R1 (→ RB)
    //   7 = L2 (button, not trigger axis)  8 = R2 (button)
    //   9 = Share (→ Back)    10 = Options (→ Start)
    //  11 = L3 (Left Stick)   12 = R3 (Right Stick)
    //  13 = PS Button (→ Guide)  14 = Touchpad Click
    //
    // Xbox HID mapping on macOS:
    //   1 = A   2 = B   3 = X   4 = Y
    //   5 = LB  6 = RB  7 = Back  8 = Start
    //   9 = L3  10 = R3  11 = Guide
    void mapButton(int slot, uint32_t usage, bool pressed, ControllerType ctrlType) {
        GamepadButton btn;

        if (ctrlType == ControllerType::PS4_DUALSHOCK ||
            ctrlType == ControllerType::PS5_DUALSENSE) {
            // PS4/PS5 button mapping — map to equivalent Xbox function
            switch (usage) {
                case 1:  btn = GamepadButton::X;              break; // Square → X
                case 2:  btn = GamepadButton::A;              break; // Cross → A
                case 3:  btn = GamepadButton::B;              break; // Circle → B
                case 4:  btn = GamepadButton::Y;              break; // Triangle → Y
                case 5:  btn = GamepadButton::LEFT_SHOULDER;  break; // L1 → LB
                case 6:  btn = GamepadButton::RIGHT_SHOULDER; break; // R1 → RB
                // 7 = L2 button click, 8 = R2 button click — treat as digital trigger
                case 7:  // L2 click — ignore (triggers are on analog axes)
                case 8:  // R2 click — ignore
                    return;
                case 9:  btn = GamepadButton::BACK;           break; // Share → Back
                case 10: btn = GamepadButton::START;          break; // Options → Start
                case 11: btn = GamepadButton::LEFT_STICK;     break; // L3
                case 12: btn = GamepadButton::RIGHT_STICK;    break; // R3
                case 13: btn = GamepadButton::GUIDE;          break; // PS → Guide
                // 14 = Touchpad click — unmapped
                default: return;
            }
        } else {
            // Standard Xbox-style HID button mapping
            switch (usage) {
                case 1:  btn = GamepadButton::A;              break;
                case 2:  btn = GamepadButton::B;              break;
                case 3:  btn = GamepadButton::X;              break;
                case 4:  btn = GamepadButton::Y;              break;
                case 5:  btn = GamepadButton::LEFT_SHOULDER;  break;
                case 6:  btn = GamepadButton::RIGHT_SHOULDER; break;
                case 7:  btn = GamepadButton::BACK;           break;
                case 8:  btn = GamepadButton::START;          break;
                case 9:  btn = GamepadButton::LEFT_STICK;     break;
                case 10: btn = GamepadButton::RIGHT_STICK;    break;
                case 11: btn = GamepadButton::GUIDE;          break;
                default: return; // unmapped
            }
        }
        currentStates[slot].buttons[static_cast<int>(btn)] = pressed;
    }

    // Map a hat-switch (d-pad) value to four directional buttons.
    // Standard 8-position hat: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW
    // Values outside 0..7 (or logMin..logMax boundary) mean centered / neutral.
    void mapHatSwitch(int slot, CFIndex value, CFIndex logMin, CFIndex logMax) {
        bool up = false, down = false, left = false, right = false;

        // Neutral if value is outside the valid hat range
        if (value >= logMin && value <= logMax) {
            int hat = static_cast<int>(value - logMin);
            switch (hat) {
                case 0: up = true;                        break; // N
                case 1: up = true;  right = true;         break; // NE
                case 2: right = true;                     break; // E
                case 3: down = true; right = true;        break; // SE
                case 4: down = true;                      break; // S
                case 5: down = true; left = true;         break; // SW
                case 6: left = true;                      break; // W
                case 7: up = true;   left = true;         break; // NW
                default: break; // centered / neutral
            }
        }

        currentStates[slot].buttons[static_cast<int>(GamepadButton::DPAD_UP)]    = up;
        currentStates[slot].buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]  = down;
        currentStates[slot].buttons[static_cast<int>(GamepadButton::DPAD_LEFT)]  = left;
        currentStates[slot].buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)] = right;
    }
};

#else // !__APPLE__  (allow compilation on non-macOS for testing)

class MacOSGamepadInput : public IGamepadInput {
public:
    MacOSGamepadInput() = default;
    bool initialize() override { return false; }
    void shutdown() override {}
    void update() override {}
    int getConnectedCount() const override { return 0; }
    bool isConnected(int) const override { return false; }
    GamepadState getState(int) const override { return GamepadState(); }
    bool isButtonPressed(GamepadButton, int) const override { return false; }
    bool wasButtonPressed(GamepadButton, int) const override { return false; }
    bool wasButtonReleased(GamepadButton, int) const override { return false; }
    float getAxisValue(GamepadAxis, int) const override { return 0.0f; }
    void setDeadzone(float) override {}
    float getDeadzone() const override { return 0.15f; }
    bool setVibration(int, float, float) override { return false; }
    std::string getControllerName(int) const override { return "Not connected"; }
    void setEventCallback(std::function<void(const InputEvent&)>) override {}
};

#endif // __APPLE__

/**
 * Keyboard gamepad emulator for macOS.
 * Uses KeyboardEmulatorMapping for configurable key bindings.
 */
class MacOSKeyboardEmulator : public KeyboardGamepadEmulator {
public:
    MacOSKeyboardEmulator() {
        std::memset(keyStates, 0, sizeof(keyStates));
    }

    void setKeyMapping(const KeyboardEmulatorMapping& mapping) override {
        std::memset(keyStates, 0, sizeof(keyStates));
        km = mapping;
    }

    void update(GamepadState& state, float /*deltaTime*/) override {
        state = GamepadState();
        state.connected = true; // keyboard is always "connected"

        // Steering (left stick)
        if (keyStates[km.steerLeft])
            state.axes[static_cast<int>(GamepadAxis::LEFT_X)] = -1.0f;
        if (keyStates[km.steerRight])
            state.axes[static_cast<int>(GamepadAxis::LEFT_X)] = 1.0f;
        if (keyStates[km.accelerate])
            state.axes[static_cast<int>(GamepadAxis::LEFT_Y)] = -1.0f;
        if (keyStates[km.brake])
            state.axes[static_cast<int>(GamepadAxis::LEFT_Y)] = 1.0f;

        // Aiming (right stick)
        if (keyStates[km.aimLeft])
            state.axes[static_cast<int>(GamepadAxis::RIGHT_X)] = -1.0f;
        if (keyStates[km.aimRight])
            state.axes[static_cast<int>(GamepadAxis::RIGHT_X)] = 1.0f;
        if (keyStates[km.aimUp])
            state.axes[static_cast<int>(GamepadAxis::RIGHT_Y)] = -1.0f;
        if (keyStates[km.aimDown])
            state.axes[static_cast<int>(GamepadAxis::RIGHT_Y)] = 1.0f;

        // Face buttons
        if (keyStates[km.inductanceUp])
            state.buttons[static_cast<int>(GamepadButton::Y)] = true;
        if (keyStates[km.inductanceDown])
            state.buttons[static_cast<int>(GamepadButton::X)] = true;
        if (keyStates[km.capacitanceUp])
            state.buttons[static_cast<int>(GamepadButton::B)] = true;
        if (keyStates[km.capacitanceDown])
            state.buttons[static_cast<int>(GamepadButton::A)] = true;

        // D-pad
        if (keyStates[km.ununUp])
            state.buttons[static_cast<int>(GamepadButton::DPAD_UP)] = true;
        if (keyStates[km.ununDown])
            state.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)] = true;
        if (keyStates[km.weaponPrev])
            state.buttons[static_cast<int>(GamepadButton::DPAD_LEFT)] = true;
        if (keyStates[km.weaponNext])
            state.buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)] = true;

        // Triggers
        if (keyStates[km.morseKey])
            state.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)] = 1.0f;
        if (keyStates[km.noiseBlanker])
            state.axes[static_cast<int>(GamepadAxis::LEFT_TRIGGER)] = 1.0f;

        // Shoulder buttons (morse paddles)
        if (keyStates[km.paddleDot])
            state.buttons[static_cast<int>(GamepadButton::LEFT_SHOULDER)] = true;
        if (keyStates[km.paddleDash])
            state.buttons[static_cast<int>(GamepadButton::RIGHT_SHOULDER)] = true;

        // Start / Back
        if (keyStates[km.statusReadout])
            state.buttons[static_cast<int>(GamepadButton::BACK)] = true;
        if (keyStates[km.pause])
            state.buttons[static_cast<int>(GamepadButton::START)] = true;

        // Enter → A button (confirm in menus); Backspace → B button (back in menus)
        if (keyStates[0x0D])  // Enter/Return
            state.buttons[static_cast<int>(GamepadButton::A)] = true;
        if (keyStates[0x08])  // Backspace
            state.buttons[static_cast<int>(GamepadButton::B)] = true;
    }

    void handleKeyEvent(int key, bool pressed) override {
        if (key >= 0 && key < 512) {
            keyStates[key] = pressed;
        }
    }

    std::string getMappingDescription() const override {
        return
            "Arrow Keys: Steer left/right, speed up/down\n"
            "W/A/S/D: Aim turret\n"
            "Q: Increase inductance (L), E: Decrease inductance (L)\n"
            "Z: Increase capacitance (C), C: Decrease capacitance (C)\n"
            "I/K: UnUn ratio up/down\n"
            "J/L: Switch weapon\n"
            "Space: Morse key\n"
            "F: Noise blanker\n"
            "U/O: Morse paddles (dot/dash)\n"
            "Tab: Status readout (may switch focus away from the game)\n"
            "P: Pause";
    }

private:
    bool keyStates[512];
    KeyboardEmulatorMapping km;
};

// Factory functions
std::unique_ptr<IGamepadInput> createGamepadInput() {
    return std::make_unique<MacOSGamepadInput>();
}

std::unique_ptr<KeyboardGamepadEmulator> createKeyboardEmulator() {
    return std::make_unique<MacOSKeyboardEmulator>();
}
