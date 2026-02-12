#ifndef CONSOLE_INPUT_H
#define CONSOLE_INPUT_H

/**
 * @file console_input.h
 * @brief Platform-independent console input interface
 * 
 * This interface abstracts keyboard input functionality across different platforms.
 * Windows uses _kbhit() and _getch() from conio.h
 * Unix-like systems (macOS, Linux) use termios with select()
 * 
 * Contract:
 * - initialize() must be called before kbhit() or getch()
 * - kbhit() returns false if not initialized
 * - getch() returns -1 on error or if not initialized
 * - cleanup() restores the console to its original state
 * - getKey() resolves platform-specific extended keys to logical codes
 */

/**
 * Task 2.3: Logical key codes for platform-independent keyboard handling
 * 
 * These codes provide a unified representation of keyboard input across platforms:
 * - ASCII characters (32-126) are passed through unchanged
 * - Extended keys (arrows, function keys, etc.) are mapped to high values (>256)
 * - Special control characters are defined explicitly
 */
enum LogicalKey {
    // Special keys (low range)
    KEY_UNKNOWN = -1,
    KEY_ERROR = -2,
    
    // Control characters
    KEY_BACKSPACE = 8,
    KEY_TAB = 9,
    KEY_ENTER = 13,
    KEY_ESCAPE = 27,
    
    // Extended keys (high range to avoid ASCII conflicts)
    KEY_UP = 256,
    KEY_DOWN = 257,
    KEY_LEFT = 258,
    KEY_RIGHT = 259,
    KEY_HOME = 260,
    KEY_END = 261,
    KEY_PAGE_UP = 262,
    KEY_PAGE_DOWN = 263,
    KEY_INSERT = 264,
    KEY_DELETE = 265,
    
    // Function keys
    KEY_F1 = 270,
    KEY_F2 = 271,
    KEY_F3 = 272,
    KEY_F4 = 273,
    KEY_F5 = 274,
    KEY_F6 = 275,
    KEY_F7 = 276,
    KEY_F8 = 277,
    KEY_F9 = 278,
    KEY_F10 = 279,
    KEY_F11 = 280,
    KEY_F12 = 281
};

class IConsoleInput {
public:
    virtual ~IConsoleInput() = default;
    
    /**
     * Check if a key has been pressed (non-blocking)
     * @return true if a key is available, false otherwise or if not initialized
     */
    virtual bool kbhit() = 0;
    
    /**
     * Get a character from console (blocking)
     * @return The character code pressed, or -1 on error or if not initialized
     */
    virtual int getch() = 0;
    
    /**
     * Task 2.3: Get logical key with extended key resolution (blocking)
     * 
     * Resolves platform-specific extended key sequences to LogicalKey codes:
     * - Windows: Handles 224/0 prefix for arrow keys and extended keys
     * - POSIX: Handles ANSI escape sequences (\033[A, \033[B, etc.)
     * - ASCII characters are returned unchanged
     * 
     * @return LogicalKey code, or KEY_ERROR on error/not initialized
     */
    virtual int getKey() = 0;
    
    /**
     * Initialize the console for input
     * @return true on success, false on failure
     */
    virtual bool initialize() = 0;
    
    /**
     * Restore console to normal mode
     */
    virtual void cleanup() = 0;
    
    /**
     * Enable raw mode for immediate key input (non-canonical, no echo)
     * Required for kbhit() and getch() to work properly
     * @return true on success, false on failure
     */
    virtual bool enableRawMode() = 0;
    
    /**
     * Enable canonical mode for line-based input with echo and editing
     * Required for std::getline() to work properly with visible text and backspace
     * @return true on success, false on failure
     */
    virtual bool enableCanonicalMode() = 0;
};

/**
 * Factory function to create platform-specific console input
 * @return Pointer to platform-specific IConsoleInput implementation
 */
IConsoleInput* createConsoleInput();

#endif // CONSOLE_INPUT_H
