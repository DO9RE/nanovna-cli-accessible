#include "gamepad_interface.h"
#include "gamepad_utils.h"
#include <vector>
#include <cstring>
#include <cmath>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <linux/joystick.h>
#include <linux/input.h>
#include <cerrno>
#include <climits>

static constexpr int MAX_CONTROLLERS = 4;

#define BITS_PER_LONG    (sizeof(unsigned long) * CHAR_BIT)
#define BITS_TO_LONGS(n) (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define TEST_BIT(arr, b) ((arr[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1)

/**
 * Linux gamepad implementation using the kernel joystick API.
 *
 * Reads /dev/input/js0..js3 for button/axis events and optionally
 * opens the corresponding /dev/input/event* device for force-feedback.
 */
class LinuxGamepadInput : public IGamepadInput {
public:
    LinuxGamepadInput() : initialized(false), deadzone(0.15f), pollCounter(0) {
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            jsFds[i] = -1;
            ffFds[i] = -1;
            ffEffectIds[i] = -1;
            currentStates[i] = GamepadState();
            previousStates[i] = GamepadState();
        }
    }

    ~LinuxGamepadInput() override {
        shutdown();
    }

    bool initialize() override {
        if (initialized) {
            return true;
        }
        initialized = true;
        scanDevices();
        return true;
    }

    void shutdown() override {
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            closeDevice(i);
        }
        initialized = false;
    }

    void update() override {
        if (!initialized) {
            return;
        }

        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            previousStates[i] = currentStates[i];
        }

        // Periodically check for newly connected / disconnected devices
        pollCounter++;
        if (pollCounter >= 60) {
            pollCounter = 0;
            scanDevices();
        }

        // Read pending events from each open joystick fd
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            if (jsFds[i] < 0) {
                continue;
            }
            struct js_event ev;
            while (true) {
                ssize_t n = ::read(jsFds[i], &ev, sizeof(ev));
                if (n != static_cast<ssize_t>(sizeof(ev))) {
                    if (n < 0 && errno != EAGAIN) {
                        // Device disconnected or error
                        closeDevice(i);
                    }
                    break;
                }
                processEvent(i, ev);
            }
        }

        fireCallbacks();
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

    bool isConnected(int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS) {
            return false;
        }
        return currentStates[index].connected;
    }

    GamepadState getState(int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS) {
            return GamepadState();
        }
        return currentStates[index];
    }

    bool isButtonPressed(GamepadButton button, int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return false;
        }
        return currentStates[index].buttons[static_cast<int>(button)];
    }

    bool wasButtonPressed(GamepadButton button, int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return false;
        }
        int idx = static_cast<int>(button);
        return currentStates[index].buttons[idx] && !previousStates[index].buttons[idx];
    }

    bool wasButtonReleased(GamepadButton button, int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return false;
        }
        int idx = static_cast<int>(button);
        return !currentStates[index].buttons[idx] && previousStates[index].buttons[idx];
    }

    float getAxisValue(GamepadAxis axis, int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return 0.0f;
        }

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

    bool setVibration(int index, float leftMotor, float rightMotor) override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return false;
        }

        // Lazily open the evdev force-feedback device
        if (ffFds[index] < 0) {
            ffFds[index] = openForceFeedbackDevice(index);
            if (ffFds[index] < 0) {
                return false;
            }
        }

        struct ff_effect effect;
        std::memset(&effect, 0, sizeof(effect));
        effect.type = FF_RUMBLE;
        effect.id = ffEffectIds[index];
        effect.u.rumble.strong_magnitude = GamepadUtils::floatToU16(leftMotor);
        effect.u.rumble.weak_magnitude = GamepadUtils::floatToU16(rightMotor);
        effect.replay.length = 5000; // 5 seconds
        effect.replay.delay = 0;

        if (::ioctl(ffFds[index], EVIOCSFF, &effect) < 0) {
            return false;
        }
        ffEffectIds[index] = effect.id;

        struct input_event play;
        std::memset(&play, 0, sizeof(play));
        play.type = EV_FF;
        play.code = static_cast<__u16>(effect.id);
        play.value = 1;
        if (::write(ffFds[index], &play, sizeof(play)) < 0) {
            return false;
        }
        return true;
    }

    std::string getControllerName(int index) const override {
        if (index < 0 || index >= MAX_CONTROLLERS || !currentStates[index].connected) {
            return "Not connected";
        }
        return controllerNames[index];
    }

    void setEventCallback(std::function<void(const InputEvent&)> callback) override {
        eventCallback = callback;
    }

private:
    bool initialized;
    float deadzone;
    int pollCounter;

    int jsFds[MAX_CONTROLLERS];
    int ffFds[MAX_CONTROLLERS];
    int ffEffectIds[MAX_CONTROLLERS];
    std::string controllerNames[MAX_CONTROLLERS];

    GamepadState currentStates[MAX_CONTROLLERS];
    GamepadState previousStates[MAX_CONTROLLERS];
    std::function<void(const InputEvent&)> eventCallback;

    void scanDevices() {
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            std::string path = "/dev/input/js" + std::to_string(i);
            bool exists = (::access(path.c_str(), R_OK) == 0);

            if (exists && jsFds[i] < 0) {
                int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
                if (fd >= 0) {
                    jsFds[i] = fd;
                    currentStates[i].connected = true;

                    char name[128] = "Unknown Controller";
                    if (::ioctl(fd, JSIOCGNAME(sizeof(name)), name) >= 0) {
                        controllerNames[i] = name;
                    } else {
                        controllerNames[i] = "Gamepad " + std::to_string(i);
                    }
                }
            } else if (!exists && jsFds[i] >= 0) {
                closeDevice(i);
            }
        }
    }

    void closeDevice(int index) {
        if (ffFds[index] >= 0) {
            if (ffEffectIds[index] >= 0) {
                ::ioctl(ffFds[index], EVIOCRMFF, ffEffectIds[index]);
                ffEffectIds[index] = -1;
            }
            ::close(ffFds[index]);
            ffFds[index] = -1;
        }
        if (jsFds[index] >= 0) {
            ::close(jsFds[index]);
            jsFds[index] = -1;
        }
        currentStates[index] = GamepadState();
        controllerNames[index].clear();
    }

    // Xbox joystick button mapping: 0=A,1=B,2=X,3=Y,4=LB,5=RB,6=Back,7=Start,8=Guide,9=LS,10=RS
    void processEvent(int index, const struct js_event& ev) {
        unsigned int type = ev.type & ~JS_EVENT_INIT;
        GamepadState& state = currentStates[index];

        if (type == JS_EVENT_BUTTON) {
            GamepadButton btn;
            bool mapped = mapButton(ev.number, btn);
            if (mapped) {
                state.buttons[static_cast<int>(btn)] = (ev.value != 0);
            }
        } else if (type == JS_EVENT_AXIS) {
            processAxis(state, ev.number, ev.value);
        }
    }

    static bool mapButton(unsigned int number, GamepadButton& btn) {
        switch (number) {
            case 0: btn = GamepadButton::A; return true;
            case 1: btn = GamepadButton::B; return true;
            case 2: btn = GamepadButton::X; return true;
            case 3: btn = GamepadButton::Y; return true;
            case 4: btn = GamepadButton::LEFT_SHOULDER; return true;
            case 5: btn = GamepadButton::RIGHT_SHOULDER; return true;
            case 6: btn = GamepadButton::BACK; return true;
            case 7: btn = GamepadButton::START; return true;
            case 8: btn = GamepadButton::GUIDE; return true;
            case 9: btn = GamepadButton::LEFT_STICK; return true;
            case 10: btn = GamepadButton::RIGHT_STICK; return true;
            default: return false;
        }
    }

    // Xbox joystick axis mapping: 0=LX,1=LY,2=LT,3=RX,4=RY,5=RT,6=DpadX,7=DpadY
    void processAxis(GamepadState& state, unsigned int axis, int16_t value) {
        switch (axis) {
            case 0:
                state.axes[static_cast<int>(GamepadAxis::LEFT_X)] = normalizeStick(value);
                break;
            case 1:
                state.axes[static_cast<int>(GamepadAxis::LEFT_Y)] = normalizeStick(value);
                break;
            case 2:
                state.axes[static_cast<int>(GamepadAxis::LEFT_TRIGGER)] = normalizeTrigger(value);
                break;
            case 3:
                state.axes[static_cast<int>(GamepadAxis::RIGHT_X)] = normalizeStick(value);
                break;
            case 4:
                state.axes[static_cast<int>(GamepadAxis::RIGHT_Y)] = normalizeStick(value);
                break;
            case 5:
                state.axes[static_cast<int>(GamepadAxis::RIGHT_TRIGGER)] = normalizeTrigger(value);
                break;
            case 6: // D-pad X axis: -32767=left, 0=center, 32767=right
                state.buttons[static_cast<int>(GamepadButton::DPAD_LEFT)] = (value < -16000);
                state.buttons[static_cast<int>(GamepadButton::DPAD_RIGHT)] = (value > 16000);
                break;
            case 7: // D-pad Y axis: -32767=up, 0=center, 32767=down
                state.buttons[static_cast<int>(GamepadButton::DPAD_UP)] = (value < -16000);
                state.buttons[static_cast<int>(GamepadButton::DPAD_DOWN)] = (value > 16000);
                break;
            default:
                break;
        }
    }

    static float normalizeStick(int16_t value) {
        return GamepadUtils::normalizeAxis(value, -32767, 32767);
    }

    // Trigger raw range is -32767..32767; map to 0.0..1.0
    static float normalizeTrigger(int16_t value) {
        return GamepadUtils::normalizeTrigger(value, -32767, 32767);
    }

    // Try to find and open the matching /dev/input/eventN device for FF
    int openForceFeedbackDevice(int controllerIndex) {
        // The joystick number may correspond to the same physical device
        // exposed as /dev/input/eventN.  Scan event devices.
        DIR* dir = ::opendir("/dev/input");
        if (!dir) {
            return -1;
        }

        int result = -1;
        struct dirent* entry;
        while ((entry = ::readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.rfind("event", 0) != 0) {
                continue;
            }
            std::string path = "/dev/input/" + name;
            int fd = ::open(path.c_str(), O_RDWR);
            if (fd < 0) {
                continue;
            }

            // Check if this device supports FF_RUMBLE
            unsigned long ffBits[BITS_TO_LONGS(FF_CNT)] = {0};
            if (::ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ffBits)), ffBits) >= 0) {
                if (TEST_BIT(ffBits, FF_RUMBLE)) {
                    // Verify it matches by comparing device names
                    char evName[128] = "";
                    ::ioctl(fd, EVIOCGNAME(sizeof(evName)), evName);
                    if (controllerNames[controllerIndex] == evName) {
                        result = fd;
                        break;
                    }
                }
            }
            ::close(fd);
        }
        ::closedir(dir);
        return result;
    }

    void fireCallbacks() {
        if (!eventCallback) {
            return;
        }
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            if (currentStates[i].connected != previousStates[i].connected) {
                InputEvent event;
                event.controllerIndex = i;
                event.type = currentStates[i].connected ?
                            InputEventType::CONNECTED : InputEventType::DISCONNECTED;
                eventCallback(event);
            }
            if (!currentStates[i].connected) {
                continue;
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
};

/**
 * Keyboard gamepad emulator for Linux.
 * Uses a dynamic KeyboardEmulatorMapping (matching the Windows emulator).
 */
class LinuxKeyboardEmulator : public KeyboardGamepadEmulator {
public:
    LinuxKeyboardEmulator() {
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

    void setKeyMapping(const KeyboardEmulatorMapping& mapping) override {
        std::memset(keyStates, 0, sizeof(keyStates));
        km = mapping;
    }

private:
    bool keyStates[512];
    KeyboardEmulatorMapping km;
};

// Factory functions
std::unique_ptr<IGamepadInput> createGamepadInput() {
    return std::make_unique<LinuxGamepadInput>();
}

std::unique_ptr<KeyboardGamepadEmulator> createKeyboardEmulator() {
    return std::make_unique<LinuxKeyboardEmulator>();
}
