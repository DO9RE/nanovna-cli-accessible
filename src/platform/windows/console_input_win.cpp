#ifdef _WIN32

#include "../interface/console_input.h"
#include <conio.h>

/**
 * Windows implementation of console input using conio.h
 * 
 * Updated to match POSIX behavior:
 * - Tracks initialization state
 * - Returns -1 from getch() when not initialized
 * - Returns false from kbhit() when not initialized
 * 
 * Task 2.3: Extended key handling
 * - Resolves 224/0 prefix codes for arrow keys and function keys
 * - Maps Windows-specific codes to LogicalKey enum
 */
class ConsoleInputWindows : public IConsoleInput {
private:
    bool initialized;
    
public:
    ConsoleInputWindows() : initialized(false) {}
    
    ~ConsoleInputWindows() override {
        cleanup();
    }
    
    bool kbhit() override {
        // Check initialization state (consistent with POSIX)
        if (!initialized) return false;
        
        return _kbhit() != 0;
    }
    
    int getch() override {
        // Return -1 if not initialized (consistent with POSIX)
        if (!initialized) return -1;
        
        return _getch();
    }
    
    int getKey() override {
        // Task 2.3: Resolve Windows extended keys
        if (!initialized) return KEY_ERROR;
        
        int ch = _getch();
        
        // Handle extended key prefix (224 or 0)
        if (ch == 224 || ch == 0) {
            if (!_kbhit()) {
                return KEY_UNKNOWN;
            }
            int ext = _getch();
            
            // Map Windows extended key codes to LogicalKey
            switch (ext) {
                // Arrow keys
                case 72: return KEY_UP;
                case 80: return KEY_DOWN;
                case 75: return KEY_LEFT;
                case 77: return KEY_RIGHT;
                
                // Navigation keys
                case 71: return KEY_HOME;
                case 79: return KEY_END;
                case 73: return KEY_PAGE_UP;
                case 81: return KEY_PAGE_DOWN;
                case 82: return KEY_INSERT;
                case 83: return KEY_DELETE;
                
                // Function keys
                case 59: return KEY_F1;
                case 60: return KEY_F2;
                case 61: return KEY_F3;
                case 62: return KEY_F4;
                case 63: return KEY_F5;
                case 64: return KEY_F6;
                case 65: return KEY_F7;
                case 66: return KEY_F8;
                case 67: return KEY_F9;
                case 68: return KEY_F10;
                
                default:
                    // Unknown extended key
                    return KEY_UNKNOWN;
            }
        }
        
        // Regular character or control key
        return ch;
    }
    
    bool initialize() override {
        if (initialized) return true;
        
        // Windows console works out of the box with conio.h
        // but we track initialization for consistent behavior
        initialized = true;
        return true;
    }
    
    void cleanup() override {
        // Mark as uninitialized
        initialized = false;
    }
    
    bool enableRawMode() override {
        // Windows console is always in "raw" mode with conio.h
        // No need to change terminal settings
        return initialized;
    }
    
    bool enableCanonicalMode() override {
        // Windows console handles line editing automatically
        // No need to change terminal settings
        return initialized;
    }
};

IConsoleInput* createConsoleInput() {
    return new ConsoleInputWindows();
}

#endif // _WIN32
