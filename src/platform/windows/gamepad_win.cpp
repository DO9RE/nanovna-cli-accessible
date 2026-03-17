#include "gamepad_interface.h"
#include "gamepad_utils.h"
#include <windows.h>
#include <xinput.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>

/**
 * Windows gamepad implementation with XInput + Raw HID fallback.
 *
 * XInput handles Xbox 360/One controllers natively.
 * For controllers not seen by XInput (e.g. Sony DualShock 4, DualSense),
 * we enumerate HID game controllers and read their reports directly.
 *
 * Vibration:
 *  - Xbox: via XInputSetState (native).
 *  - DS4:  via HID output report (report ID 0x05, 32 bytes).
 */

// XInput function pointer types for dynamic loading
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
typedef DWORD (WINAPI *PFN_XInputSetState)(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);

// Dynamic XInput state
static HMODULE s_xinputDLL = nullptr;
static PFN_XInputGetState s_XInputGetState = nullptr;
static PFN_XInputSetState s_XInputSetState = nullptr;
static bool s_xinputLoadAttempted = false;

static bool loadXInput() {
    if (s_xinputLoadAttempted) {
        return s_xinputDLL != nullptr;
    }
    s_xinputLoadAttempted = true;

    const char* dllNames[] = {
        "xinput1_4.dll",
        "xinput1_3.dll",
        "xinput9_1_0.dll",
        nullptr
    };

    for (int i = 0; dllNames[i]; i++) {
        s_xinputDLL = LoadLibraryExA(dllNames[i], nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (s_xinputDLL) {
            s_XInputGetState = reinterpret_cast<PFN_XInputGetState>(GetProcAddress(s_xinputDLL, "XInputGetState"));
            s_XInputSetState = reinterpret_cast<PFN_XInputSetState>(GetProcAddress(s_xinputDLL, "XInputSetState"));
            if (s_XInputGetState && s_XInputSetState) {
                return true;
            }
            FreeLibrary(s_xinputDLL);
            s_xinputDLL = nullptr;
            s_XInputGetState = nullptr;
            s_XInputSetState = nullptr;
        }
    }
    return false;
}

// ─── HID-based PS4/PS5 controller support ───────────────────────────────────

static constexpr int MAX_CONTROLLERS = 4;

// Sony vendor ID and product IDs
static constexpr USHORT SONY_VID        = 0x054C;
static constexpr USHORT DS4_PID_V1      = 0x05C4;
static constexpr USHORT DS4_PID_V2      = 0x09CC;
static constexpr USHORT DUALSENSE_PID   = 0x0CE6;
static constexpr USHORT DUALSENSE_EDGE  = 0x0DF2;

enum class HidControllerType { DS4, DUALSENSE, UNKNOWN };

static HidControllerType classifyHidDevice(USHORT vid, USHORT pid) {
    if (vid != SONY_VID) return HidControllerType::UNKNOWN;
    if (pid == DS4_PID_V1 || pid == DS4_PID_V2) return HidControllerType::DS4;
    if (pid == DUALSENSE_PID || pid == DUALSENSE_EDGE) return HidControllerType::DUALSENSE;
    return HidControllerType::UNKNOWN;
}

/// State for a single HID (non-XInput) controller
struct HidController {
    HANDLE handle = INVALID_HANDLE_VALUE;
    HidControllerType type = HidControllerType::UNKNOWN;
    std::string name;
    int slotIndex = -1;
    USHORT vid = 0, pid = 0;

    void close() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
    }
};

/// Parse a DualShock 4 USB input report (64 bytes, report ID 0x01).
/// Updates the GamepadState for the assigned slot.
static void parseDS4Report(const BYTE* report, int reportLen, GamepadState& state) {
    if (reportLen < 10) return;

    // Byte layout (USB report ID 0x01, first byte is report ID):
    // [0] = report ID (0x01)
    // [1] = left stick X  (0..255, 128=center)
    // [2] = left stick Y  (0..255, 128=center)
    // [3] = right stick X
    // [4] = right stick Y
    // [5] = buttons + d-pad (low nibble = hat, bits 4-7 = Square,Cross,Circle,Triangle)
    // [6] = buttons (L1,R1,L2btn,R2btn,Share,Options,L3,R3)
    // [7] = PS button + touchpad click
    // [8] = left trigger  (0..255)
    // [9] = right trigger (0..255)

    int offset = (report[0] == 0x01) ? 1 : 0; // skip report ID if present

    auto norm = [](BYTE val) -> float {
        return (static_cast<float>(val) - 128.0f) / 127.0f;
    };

    state.axes[static_cast<int>(GamepadAxis::LEFT_X)]  = norm(report[offset + 0]);
    state.axes[static_cast<int>(GamepadAxis::LEFT_Y)]  = norm(report[offset + 1]);
    state.axes[static_cast<int>(GamepadAxis::RIGHT_X)] = norm(report[offset + 2]);
    state.axes[static_cast<int>(GamepadAxis::RIGHT_Y)] = norm(report[offset + 3]);

    BYTE btn0 = report[offset + 4];
    BYTE btn1 = report[offset + 5];
    BYTE btn2 = report[offset + 6];

    // D-pad (low nibble of btn0)
    int hat = btn0 & 0x0F;
    state.buttons[static_cast<int>(GamepadButton::DPAD_UP)]    = (hat == 0 || hat == 1 || hat == 7);
    state.buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)] = (hat == 1 || hat == 2 || hat == 3);
    state.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)]  = (hat == 3 || hat == 4 || hat == 5);
    state.buttons[static_cast<int>(GamepadButton::DPAD_LEFT)]  = (hat == 5 || hat == 6 || hat == 7);

    // Face buttons (map PS → Xbox convention)
    state.buttons[static_cast<int>(GamepadButton::X)] = (btn0 & 0x10) != 0; // Square
    state.buttons[static_cast<int>(GamepadButton::A)] = (btn0 & 0x20) != 0; // Cross
    state.buttons[static_cast<int>(GamepadButton::B)] = (btn0 & 0x40) != 0; // Circle
    state.buttons[static_cast<int>(GamepadButton::Y)] = (btn0 & 0x80) != 0; // Triangle

    // Shoulder & center
    state.buttons[static_cast<int>(GamepadButton::LEFT_SHOULDER)]  = (btn1 & 0x01) != 0; // L1
    state.buttons[static_cast<int>(GamepadButton::RIGHT_SHOULDER)] = (btn1 & 0x02) != 0; // R1
    state.buttons[static_cast<int>(GamepadButton::BACK)]           = (btn1 & 0x10) != 0; // Share
    state.buttons[static_cast<int>(GamepadButton::START)]          = (btn1 & 0x20) != 0; // Options
    state.buttons[static_cast<int>(GamepadButton::LEFT_STICK)]     = (btn1 & 0x40) != 0; // L3
    state.buttons[static_cast<int>(GamepadButton::RIGHT_STICK)]    = (btn1 & 0x80) != 0; // R3

    // PS button & touchpad
    state.buttons[static_cast<int>(GamepadButton::GUIDE)] = (btn2 & 0x01) != 0;

    // Analog triggers
    state.axes[static_cast<int>(GamepadAxis::LEFT_TRIGGER)]  = static_cast<float>(report[offset + 7]) / 255.0f;
    state.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)] = static_cast<float>(report[offset + 8]) / 255.0f;

    state.connected = true;
}

/// Send a DS4 USB vibration output report (report ID 0x05, 32 bytes).
static bool sendDS4Vibration(HANDLE handle, float leftMotor, float rightMotor) {
    BYTE report[32] = {};
    report[0] = 0x05;           // Report ID
    report[1] = 0xFF;           // Enable rumble + LED
    report[4] = GamepadUtils::floatToU8(rightMotor); // Right (weak) motor
    report[5] = GamepadUtils::floatToU8(leftMotor);  // Left (strong) motor

    DWORD written = 0;
    return WriteFile(handle, report, sizeof(report), &written, nullptr) != 0;
}

class WindowsGamepadInput : public IGamepadInput {
public:
    WindowsGamepadInput() : initialized(false), xinputAvailable(false), deadzone(0.15f) {
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            currentStates[i] = GamepadState();
            previousStates[i] = GamepadState();
            isXInput[i] = false;
        }
    }
    
    ~WindowsGamepadInput() override {
        shutdown();
    }
    
    bool initialize() override {
        if (initialized) {
            return true;
        }
        
        xinputAvailable = loadXInput();
        enumerateHidControllers();
        initialized = true;
        update(); // Initial state poll
        return true;
    }
    
    void shutdown() override {
        for (auto& hc : hidControllers) {
            hc.close();
        }
        hidControllers.clear();
        initialized = false;
    }
    
    void update() override {
        if (!initialized) {
            return;
        }
        
        // Copy current to previous
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            previousStates[i] = currentStates[i];
        }
        
        // 1) Poll XInput controllers
        if (xinputAvailable) {
            for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
                XINPUT_STATE xstate;
                DWORD result = s_XInputGetState(i, &xstate);
                
                if (result == ERROR_SUCCESS) {
                    int slot = findOrAssignXInputSlot(i);
                    if (slot >= 0) {
                        currentStates[slot].connected = true;
                        isXInput[slot] = true;
                        updateFromXInputState(slot, xstate);
                    }
                } else {
                    // Clear slot if this XInput index was occupying one
                    for (int s = 0; s < MAX_CONTROLLERS; s++) {
                        if (isXInput[s] && xinputSlotMap[s] == static_cast<int>(i) && currentStates[s].connected) {
                            currentStates[s] = GamepadState();
                            isXInput[s] = false;
                            xinputSlotMap[s] = -1;
                        }
                    }
                }
            }
        }
        
        // 2) Poll HID controllers (PS4/PS5)
        for (auto& hc : hidControllers) {
            if (hc.handle == INVALID_HANDLE_VALUE || hc.slotIndex < 0) continue;
            
            BYTE reportBuf[64] = {};
            DWORD bytesRead = 0;
            
            // Non-blocking read — use overlapped I/O check
            if (ReadFile(hc.handle, reportBuf, sizeof(reportBuf), &bytesRead, nullptr)) {
                if (bytesRead >= 10 && hc.type == HidControllerType::DS4) {
                    parseDS4Report(reportBuf, static_cast<int>(bytesRead), currentStates[hc.slotIndex]);
                }
            } else {
                // Device disconnected or error
                currentStates[hc.slotIndex] = GamepadState();
            }
        }
        
        // Trigger callbacks for changes
        if (eventCallback) {
            for (int i = 0; i < MAX_CONTROLLERS; i++) {
                if (currentStates[i].connected != previousStates[i].connected) {
                    InputEvent event;
                    event.controllerIndex = i;
                    event.type = currentStates[i].connected ? 
                                InputEventType::CONNECTED : InputEventType::DISCONNECTED;
                    eventCallback(event);
                }
                
                for (int b = 0; b < static_cast<int>(GamepadButton::COUNT); b++) {
                    if (currentStates[i].buttons[b] != previousStates[i].buttons[b]) {
                        InputEvent event;
                        event.controllerIndex = i;
                        event.button = static_cast<GamepadButton>(b);
                        event.type = currentStates[i].buttons[b] ? 
                                    InputEventType::BUTTON_PRESSED : InputEventType::BUTTON_RELEASED;
                        eventCallback(event);
                    }
                }
            }
        }
    }
    
    int getConnectedCount() const override {
        int count = 0;
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            if (currentStates[i].connected) {
                count++;
            }
        }
        return count;
    }
    
    bool isConnected(int index = 0) const override {
        if (index < 0 || index >= MAX_CONTROLLERS) {
            return false;
        }
        return currentStates[index].connected;
    }
    
    GamepadState getState(int index = 0) const override {
        if (index < 0 || index >= MAX_CONTROLLERS) {
            return GamepadState();
        }
        return currentStates[index];
    }
    
    bool isButtonPressed(GamepadButton button, int index = 0) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return false;
        }
        return currentStates[index].buttons[static_cast<int>(button)];
    }
    
    bool wasButtonPressed(GamepadButton button, int index = 0) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return false;
        }
        int idx = static_cast<int>(button);
        return currentStates[index].buttons[idx] && !previousStates[index].buttons[idx];
    }
    
    bool wasButtonReleased(GamepadButton button, int index = 0) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return false;
        }
        int idx = static_cast<int>(button);
        return !currentStates[index].buttons[idx] && previousStates[index].buttons[idx];
    }
    
    float getAxisValue(GamepadAxis axis, int index = 0) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return 0.0f;
        }
        
        float value = currentStates[index].axes[static_cast<int>(axis)];
        
        // Apply deadzone for sticks (not triggers)
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
    
    bool setVibration(int index, float leftMotor, float rightMotor) override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return false;
        }
        
        // XInput controller — use native vibration
        if (isXInput[index] && xinputAvailable) {
            DWORD xIdx = static_cast<DWORD>(xinputSlotMap[index]);
            XINPUT_VIBRATION vibration;
            vibration.wLeftMotorSpeed = GamepadUtils::floatToU16(leftMotor);
            vibration.wRightMotorSpeed = GamepadUtils::floatToU16(rightMotor);
            return s_XInputSetState(xIdx, &vibration) == ERROR_SUCCESS;
        }
        
        // HID controller — send vendor-specific vibration
        for (auto& hc : hidControllers) {
            if (hc.slotIndex == index && hc.handle != INVALID_HANDLE_VALUE) {
                if (hc.type == HidControllerType::DS4) {
                    return sendDS4Vibration(hc.handle, leftMotor, rightMotor);
                }
                break;
            }
        }
        return false;
    }
    
    std::string getControllerName(int index = 0) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return "Not connected";
        }
        
        if (isXInput[index]) {
            return "Xbox Controller " + std::to_string(index + 1);
        }
        
        for (const auto& hc : hidControllers) {
            if (hc.slotIndex == index) {
                return hc.name;
            }
        }
        return "Controller " + std::to_string(index + 1);
    }
    
    void setEventCallback(std::function<void(const InputEvent&)> callback) override {
        eventCallback = callback;
    }
    
private:
    bool initialized;
    bool xinputAvailable;
    float deadzone;
    GamepadState currentStates[MAX_CONTROLLERS];
    GamepadState previousStates[MAX_CONTROLLERS];
    bool isXInput[MAX_CONTROLLERS] = {};        // true if slot is an XInput controller
    int xinputSlotMap[MAX_CONTROLLERS] = {-1, -1, -1, -1}; // XInput user index per slot
    std::vector<HidController> hidControllers;
    std::function<void(const InputEvent&)> eventCallback;
    
    /// Find or assign a slot for an XInput device.
    int findOrAssignXInputSlot(DWORD xinputIndex) {
        // Check if already assigned
        for (int s = 0; s < MAX_CONTROLLERS; s++) {
            if (isXInput[s] && xinputSlotMap[s] == static_cast<int>(xinputIndex)) {
                return s;
            }
        }
        // Find free slot — skip slots already claimed by HID controllers
        for (int s = 0; s < MAX_CONTROLLERS; s++) {
            if (!currentStates[s].connected && !isXInput[s]) {
                bool hidOwned = false;
                for (const auto& hc : hidControllers) {
                    if (hc.slotIndex == s) { hidOwned = true; break; }
                }
                if (!hidOwned) {
                    xinputSlotMap[s] = static_cast<int>(xinputIndex);
                    return s;
                }
            }
        }
        return -1;
    }
    
    /// Enumerate HID game controllers that are NOT seen by XInput (e.g. DS4, DualSense).
    void enumerateHidControllers() {
        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);
        
        HDEVINFO devInfo = SetupDiGetClassDevsA(&hidGuid, nullptr, nullptr,
                                                 DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devInfo == INVALID_HANDLE_VALUE) return;
        
        SP_DEVICE_INTERFACE_DATA ifData = {};
        ifData.cbSize = sizeof(ifData);
        
        for (DWORD idx = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, idx, &ifData); idx++) {
            DWORD reqSize = 0;
            SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, nullptr, 0, &reqSize, nullptr);
            if (reqSize == 0) continue;
            
            std::vector<BYTE> detailBuf(reqSize);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(detailBuf.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
            
            if (!SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, detail, reqSize, nullptr, nullptr))
                continue;
            
            HANDLE h = CreateFileA(detail->DevicePath,
                                    GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            
            HIDD_ATTRIBUTES attrs = {};
            attrs.Size = sizeof(attrs);
            if (!HidD_GetAttributes(h, &attrs)) {
                CloseHandle(h);
                continue;
            }
            
            auto ctype = classifyHidDevice(attrs.VendorID, attrs.ProductID);
            if (ctype == HidControllerType::UNKNOWN) {
                CloseHandle(h);
                continue;
            }
            
            // Get product name
            wchar_t nameBuf[256] = {};
            std::string devName = "PlayStation Controller";
            if (HidD_GetProductString(h, nameBuf, sizeof(nameBuf))) {
                char narrowBuf[256];
                WideCharToMultiByte(CP_UTF8, 0, nameBuf, -1, narrowBuf, sizeof(narrowBuf), nullptr, nullptr);
                devName = narrowBuf;
            }
            
            // Find a free slot
            int slot = -1;
            for (int s = 0; s < MAX_CONTROLLERS; s++) {
                if (!currentStates[s].connected && !isXInput[s]) {
                    bool used = false;
                    for (const auto& hc : hidControllers) {
                        if (hc.slotIndex == s) { used = true; break; }
                    }
                    if (!used) { slot = s; break; }
                }
            }
            
            if (slot < 0) {
                CloseHandle(h);
                continue;
            }
            
            HidController hc;
            hc.handle = h;
            hc.type = ctype;
            hc.name = devName;
            hc.slotIndex = slot;
            hc.vid = attrs.VendorID;
            hc.pid = attrs.ProductID;
            hidControllers.push_back(hc);
            
            std::fprintf(stderr, "[GAMEPAD_WIN] HID controller: '%s' (VID=0x%04X PID=0x%04X) → slot %d\n",
                          devName.c_str(), attrs.VendorID, attrs.ProductID, slot);
        }
        
        SetupDiDestroyDeviceInfoList(devInfo);
    }
    
    void updateFromXInputState(int index, const XINPUT_STATE& xstate) {
        const XINPUT_GAMEPAD& gamepad = xstate.Gamepad;
        GamepadState& state = currentStates[index];
        
        // Buttons
        state.buttons[static_cast<int>(GamepadButton::A)] = (gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
        state.buttons[static_cast<int>(GamepadButton::B)] = (gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
        state.buttons[static_cast<int>(GamepadButton::X)] = (gamepad.wButtons & XINPUT_GAMEPAD_X) != 0;
        state.buttons[static_cast<int>(GamepadButton::Y)] = (gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0;
        
        state.buttons[static_cast<int>(GamepadButton::LEFT_SHOULDER)] = (gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
        state.buttons[static_cast<int>(GamepadButton::RIGHT_SHOULDER)] = (gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        
        state.buttons[static_cast<int>(GamepadButton::BACK)] = (gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
        state.buttons[static_cast<int>(GamepadButton::START)] = (gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;
        
        state.buttons[static_cast<int>(GamepadButton::LEFT_STICK)] = (gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        state.buttons[static_cast<int>(GamepadButton::RIGHT_STICK)] = (gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
        
        state.buttons[static_cast<int>(GamepadButton::DPAD_UP)] = (gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
        state.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)] = (gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
        state.buttons[static_cast<int>(GamepadButton::DPAD_LEFT)] = (gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
        state.buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)] = (gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
        
        // Axes (normalize to -1.0 to +1.0 for sticks, 0.0 to 1.0 for triggers)
        // Negate Y axes: XInput uses up=positive, but our convention is up=negative (matching keyboard emulator)
        state.axes[static_cast<int>(GamepadAxis::LEFT_X)] = normalizeStickAxis(gamepad.sThumbLX);
        state.axes[static_cast<int>(GamepadAxis::LEFT_Y)] = -normalizeStickAxis(gamepad.sThumbLY);
        state.axes[static_cast<int>(GamepadAxis::RIGHT_X)] = normalizeStickAxis(gamepad.sThumbRX);
        state.axes[static_cast<int>(GamepadAxis::RIGHT_Y)] = -normalizeStickAxis(gamepad.sThumbRY);
        
        state.axes[static_cast<int>(GamepadAxis::LEFT_TRIGGER)] = normalizeTrigger(gamepad.bLeftTrigger);
        state.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)] = normalizeTrigger(gamepad.bRightTrigger);
    }
    
    float normalizeStickAxis(SHORT value) const {
        return GamepadUtils::normalizeAxis(value, -32767, 32767);
    }
    
    float normalizeTrigger(BYTE value) const {
        return GamepadUtils::normalizeTrigger(value, 0, 255);
    }
};

/**
 * Keyboard gamepad emulator for Windows
 * Uses same mapping as Linux version
 */
class WindowsKeyboardEmulator : public KeyboardGamepadEmulator {
public:
    WindowsKeyboardEmulator() {
        std::memset(keyStates, 0, sizeof(keyStates));
    }
    
    void update(GamepadState& state, float deltaTime) override {
        state = GamepadState();
        state.connected = true;
        
        // Movement (steering + acceleration)
        if (keyStates[km.steerLeft]) state.axes[static_cast<int>(GamepadAxis::LEFT_X)] = -1.0f;
        if (keyStates[km.steerRight]) state.axes[static_cast<int>(GamepadAxis::LEFT_X)] = 1.0f;
        if (keyStates[km.accelerate]) state.axes[static_cast<int>(GamepadAxis::LEFT_Y)] = -1.0f;
        if (keyStates[km.brake]) state.axes[static_cast<int>(GamepadAxis::LEFT_Y)] = 1.0f;
        
        // Aiming
        if (keyStates[km.aimLeft]) state.axes[static_cast<int>(GamepadAxis::RIGHT_X)] = -1.0f;
        if (keyStates[km.aimRight]) state.axes[static_cast<int>(GamepadAxis::RIGHT_X)] = 1.0f;
        if (keyStates[km.aimUp]) state.axes[static_cast<int>(GamepadAxis::RIGHT_Y)] = -1.0f;
        if (keyStates[km.aimDown]) state.axes[static_cast<int>(GamepadAxis::RIGHT_Y)] = 1.0f;
        
        // Tuner controls (face buttons)
        if (keyStates[km.inductanceUp]) state.buttons[static_cast<int>(GamepadButton::Y)] = true;
        if (keyStates[km.inductanceDown]) state.buttons[static_cast<int>(GamepadButton::X)] = true;
        if (keyStates[km.capacitanceUp]) state.buttons[static_cast<int>(GamepadButton::B)] = true;
        if (keyStates[km.capacitanceDown]) state.buttons[static_cast<int>(GamepadButton::A)] = true;
        
        // UnUn ratio (D-pad up/down)
        if (keyStates[km.ununUp]) state.buttons[static_cast<int>(GamepadButton::DPAD_UP)] = true;
        if (keyStates[km.ununDown]) state.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)] = true;
        
        // Weapon switch (D-pad left/right)
        if (keyStates[km.weaponPrev]) state.buttons[static_cast<int>(GamepadButton::DPAD_LEFT)] = true;
        if (keyStates[km.weaponNext]) state.buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)] = true;
        
        // Morse cannon (right trigger)
        if (keyStates[km.morseKey]) state.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)] = 1.0f;
        
        // Noise blanker (left trigger)
        if (keyStates[km.noiseBlanker]) state.axes[static_cast<int>(GamepadAxis::LEFT_TRIGGER)] = 1.0f;
        
        // Morse paddles
        if (keyStates[km.paddleDot]) state.buttons[static_cast<int>(GamepadButton::LEFT_SHOULDER)] = true;
        if (keyStates[km.paddleDash]) state.buttons[static_cast<int>(GamepadButton::RIGHT_SHOULDER)] = true;
        
        // Status readout
        if (keyStates[km.statusReadout]) state.buttons[static_cast<int>(GamepadButton::BACK)] = true;
        
        // Pause
        if (keyStates[km.pause]) state.buttons[static_cast<int>(GamepadButton::START)] = true;
        
        // Enter → A button (confirm in menus); Backspace → B button (back in menus)
        // These are always active so keyboard users can navigate all menus.
        if (keyStates[VK_RETURN]) state.buttons[static_cast<int>(GamepadButton::A)] = true;
        if (keyStates[VK_BACK])   state.buttons[static_cast<int>(GamepadButton::B)] = true;
        
    }
    
    void handleKeyEvent(int key, bool pressed) override {
        if (key >= 0 && key < 256) {
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
    
    void setKeyMapping(const KeyboardEmulatorMapping& mapping) override {
        // Clear all current key states to avoid stuck keys from old mapping
        std::memset(keyStates, 0, sizeof(keyStates));
        km = mapping;
    }
    
private:
    bool keyStates[256];
    KeyboardEmulatorMapping km;  // Dynamic key mapping (defaults match original hardcoded layout)
};

// Factory functions
std::unique_ptr<IGamepadInput> createGamepadInput() {
    return std::make_unique<WindowsGamepadInput>();
}

std::unique_ptr<KeyboardGamepadEmulator> createKeyboardEmulator() {
    return std::make_unique<WindowsKeyboardEmulator>();
}
