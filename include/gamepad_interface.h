#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <functional>

/**
 * @file gamepad_interface.h
 * @brief Platform-independent gamepad/controller input interface
 * 
 * Provides abstraction for Xbox 360/One controllers and keyboard fallback
 * Designed for the Ham Spirit game but could be used elsewhere
 */

/**
 * Gamepad button enumeration
 * Maps to Xbox controller button layout
 */
enum class GamepadButton {
    // Face buttons (right side)
    A = 0,              // Bottom button (green on Xbox)
    B = 1,              // Right button (red on Xbox)
    X = 2,              // Left button (blue on Xbox)
    Y = 3,              // Top button (yellow on Xbox)
    
    // Shoulder buttons
    LEFT_SHOULDER = 4,   // L1 / LB
    RIGHT_SHOULDER = 5,  // R1 / RB
    
    // Center buttons
    BACK = 6,           // Back/View/Select
    START = 7,          // Start/Menu
    
    // Stick buttons
    LEFT_STICK = 8,     // L3
    RIGHT_STICK = 9,    // R3
    
    // D-pad
    DPAD_UP = 10,
    DPAD_DOWN = 11,
    DPAD_LEFT = 12,
    DPAD_RIGHT = 13,
    
    // Special
    GUIDE = 14,         // Xbox button (center logo)
    
    COUNT = 15
};

/**
 * Gamepad axis enumeration
 */
enum class GamepadAxis {
    LEFT_X = 0,         // Left stick horizontal (-1.0 left, +1.0 right)
    LEFT_Y = 1,         // Left stick vertical (-1.0 up, +1.0 down)
    RIGHT_X = 2,        // Right stick horizontal
    RIGHT_Y = 3,        // Right stick vertical
    LEFT_TRIGGER = 4,   // L2 / LT (0.0 released, 1.0 fully pressed)
    RIGHT_TRIGGER = 5,  // R2 / RT
    
    COUNT = 6
};

/**
 * Gamepad state structure
 */
struct GamepadState {
    bool buttons[static_cast<int>(GamepadButton::COUNT)];
    float axes[static_cast<int>(GamepadAxis::COUNT)];
    bool connected;
    
    GamepadState() : connected(false) {
        for (int i = 0; i < static_cast<int>(GamepadButton::COUNT); i++) {
            buttons[i] = false;
        }
        for (int i = 0; i < static_cast<int>(GamepadAxis::COUNT); i++) {
            axes[i] = 0.0f;
        }
    }
};

/**
 * Input event types
 */
enum class InputEventType {
    BUTTON_PRESSED,
    BUTTON_RELEASED,
    AXIS_MOVED,
    CONNECTED,
    DISCONNECTED
};

/**
 * Input event structure
 */
struct InputEvent {
    InputEventType type;
    int controllerIndex;
    GamepadButton button;  // Valid for button events
    GamepadAxis axis;      // Valid for axis events
    float value;           // Valid for axis events
    
    InputEvent() : type(InputEventType::BUTTON_PRESSED), controllerIndex(0),
                   button(GamepadButton::A), axis(GamepadAxis::LEFT_X), value(0.0f) {}
};

/**
 * Platform-independent gamepad input interface
 */
class IGamepadInput {
public:
    virtual ~IGamepadInput() = default;
    
    /**
     * Initialize gamepad system
     * @return true on success
     */
    virtual bool initialize() = 0;
    
    /**
     * Shutdown gamepad system
     */
    virtual void shutdown() = 0;
    
    /**
     * Update gamepad state (call once per frame)
     */
    virtual void update() = 0;
    
    /**
     * Get number of connected gamepads
     * @return Number of connected controllers (0-4)
     */
    virtual int getConnectedCount() const = 0;
    
    /**
     * Check if specific gamepad is connected
     * @param index Controller index (0-3)
     * @return true if connected
     */
    virtual bool isConnected(int index = 0) const = 0;
    
    /**
     * Get current state of gamepad
     * @param index Controller index (0-3)
     * @return Current gamepad state
     */
    virtual GamepadState getState(int index = 0) const = 0;
    
    /**
     * Check if button is currently pressed
     * @param button Button to check
     * @param index Controller index (0-3)
     * @return true if pressed
     */
    virtual bool isButtonPressed(GamepadButton button, int index = 0) const = 0;
    
    /**
     * Check if button was just pressed this frame
     * @param button Button to check
     * @param index Controller index (0-3)
     * @return true if just pressed
     */
    virtual bool wasButtonPressed(GamepadButton button, int index = 0) const = 0;
    
    /**
     * Check if button was just released this frame
     * @param button Button to check
     * @param index Controller index (0-3)
     * @return true if just released
     */
    virtual bool wasButtonReleased(GamepadButton button, int index = 0) const = 0;
    
    /**
     * Get axis value
     * @param axis Axis to query
     * @param index Controller index (0-3)
     * @return Axis value (-1.0 to +1.0 for sticks, 0.0 to 1.0 for triggers)
     */
    virtual float getAxisValue(GamepadAxis axis, int index = 0) const = 0;
    
    /**
     * Set axis deadzone
     * @param deadzone Deadzone radius (0.0 to 1.0, default 0.15)
     */
    virtual void setDeadzone(float deadzone) = 0;
    
    /**
     * Get axis deadzone
     */
    virtual float getDeadzone() const = 0;
    
    /**
     * Enable/disable vibration (rumble)
     * @param index Controller index
     * @param leftMotor Left motor speed (0.0 to 1.0)
     * @param rightMotor Right motor speed (0.0 to 1.0)
     * @return true if vibration supported
     */
    virtual bool setVibration(int index, float leftMotor, float rightMotor) = 0;
    
    /**
     * Get controller name/description
     * @param index Controller index
     * @return Human-readable controller name
     */
    virtual std::string getControllerName(int index = 0) const = 0;
    
    /**
     * Set event callback for input events
     * @param callback Function to call on events
     */
    virtual void setEventCallback(std::function<void(const InputEvent&)> callback) = 0;

    /**
     * Set controller preset override.
     * @param preset 0=Auto-detect (use VID/PID), 1=Force Xbox mapping, 2=Force PlayStation mapping
     */
    virtual void setControllerPreset(int preset) { (void)preset; }
};

/**
 * Keyboard gamepad emulation
 * Maps keyboard keys to gamepad buttons/axes
 */
/**
 * Keyboard-to-gamepad key mapping — action to VK code mapping.
 * Used by the keyboard emulator to dynamically support remapped keys.
 */
struct KeyboardEmulatorMapping {
    int steerLeft = 0x25;       // VK_LEFT
    int steerRight = 0x27;      // VK_RIGHT
    int accelerate = 0x26;      // VK_UP
    int brake = 0x28;           // VK_DOWN
    int aimLeft = 'A';
    int aimRight = 'D';
    int aimUp = 'W';
    int aimDown = 'S';
    int morseKey = 0x20;        // VK_SPACE (right trigger)
    int paddleDot = 'U';
    int paddleDash = 'O';
    int noiseBlanker = 'F';     // left trigger
    int inductanceUp = 'Q';     // Y button
    int inductanceDown = 'E';   // X button
    int capacitanceUp = 'Z';    // B button (in game)
    int capacitanceDown = 'C';  // A button (in game)
    int ununUp = 'I';           // D-pad up
    int ununDown = 'K';         // D-pad down
    int weaponPrev = 'J';       // D-pad left
    int weaponNext = 'L';       // D-pad right
    int pause = 'P';            // Start
    int statusReadout = 0x09;   // VK_TAB (Back)
};

class KeyboardGamepadEmulator {
public:
    virtual ~KeyboardGamepadEmulator() = default;
    
    /**
     * Update keyboard state and emulate gamepad
     * @param state Gamepad state to update
     * @param deltaTime Time since last update (seconds)
     */
    virtual void update(GamepadState& state, float deltaTime) = 0;
    
    /**
     * Process keyboard input
     * @param key Key code (platform-specific)
     * @param pressed true if pressed, false if released
     */
    virtual void handleKeyEvent(int key, bool pressed) = 0;
    
    /**
     * Get current mapping as string (for help display)
     */
    virtual std::string getMappingDescription() const = 0;
    
    /**
     * Update the key mapping for remappable actions.
     * Called when the user changes key assignments in the configuration.
     */
    virtual void setKeyMapping(const KeyboardEmulatorMapping& mapping) = 0;
};

/**
 * Factory function to create platform-specific gamepad input
 */
std::unique_ptr<IGamepadInput> createGamepadInput();

/**
 * Factory function to create keyboard emulator
 */
std::unique_ptr<KeyboardGamepadEmulator> createKeyboardEmulator();
