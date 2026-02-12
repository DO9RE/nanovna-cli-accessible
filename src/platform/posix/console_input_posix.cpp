#if defined(__linux__) || defined(__APPLE__)

#include "../interface/console_input.h"
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>

/**
 * POSIX implementation of console input using termios
 * 
 * This unified implementation is used for both Linux and macOS, as they
 * both support the POSIX termios interface identically.
 * 
 * Replaces:
 * - src/platform/linux/console_input_linux.cpp
 * - src/platform/macos/console_input_mac.cpp
 * 
 * Task 2.3: Extended key handling
 * - Resolves ANSI escape sequences (\033[A, \033[B, etc.)
 * - Maps terminal-specific codes to LogicalKey enum
 */
class ConsoleInputPOSIX : public IConsoleInput {
private:
    struct termios original_termios;
    bool initialized;
    
    /**
     * Task 2.3: Read ANSI escape sequence
     * Expects to be called after reading ESC (27)
     * Returns LogicalKey code or KEY_UNKNOWN
     */
    int readEscapeSequence() {
        // Check if more characters available (with small timeout)
        struct timeval tv;
        fd_set rdfs;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms timeout for escape sequences
        
        FD_ZERO(&rdfs);
        FD_SET(STDIN_FILENO, &rdfs);
        
        if (select(STDIN_FILENO + 1, &rdfs, nullptr, nullptr, &tv) <= 0) {
            // No follow-up character - just ESC key
            return KEY_ESCAPE;
        }
        
        unsigned char ch1;
        if (read(STDIN_FILENO, &ch1, 1) != 1) {
            return KEY_ESCAPE;
        }
        
        // Check for CSI sequences (\033[ followed by codes)
        if (ch1 == '[') {
            unsigned char ch2;
            if (read(STDIN_FILENO, &ch2, 1) != 1) {
                return KEY_UNKNOWN;
            }
            
            // Simple escape sequences: \033[A, \033[B, etc.
            switch (ch2) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
                default: break;
            }
            
            // Multi-character sequences: \033[1~, \033[2~, etc.
            if (ch2 >= '1' && ch2 <= '9') {
                unsigned char ch3;
                if (read(STDIN_FILENO, &ch3, 1) != 1) {
                    return KEY_UNKNOWN;
                }
                
                if (ch3 == '~') {
                    switch (ch2) {
                        case '1': return KEY_HOME;
                        case '2': return KEY_INSERT;
                        case '3': return KEY_DELETE;
                        case '4': return KEY_END;
                        case '5': return KEY_PAGE_UP;
                        case '6': return KEY_PAGE_DOWN;
                        default: return KEY_UNKNOWN;
                    }
                }
                
                // Extended sequences like \033[15~ for F5
                if (ch3 >= '0' && ch3 <= '9') {
                    unsigned char ch4;
                    if (read(STDIN_FILENO, &ch4, 1) != 1) {
                        return KEY_UNKNOWN;
                    }
                    
                    if (ch4 == '~') {
                        int num = (ch2 - '0') * 10 + (ch3 - '0');
                        switch (num) {
                            case 11: return KEY_F1;
                            case 12: return KEY_F2;
                            case 13: return KEY_F3;
                            case 14: return KEY_F4;
                            case 15: return KEY_F5;
                            case 17: return KEY_F6;
                            case 18: return KEY_F7;
                            case 19: return KEY_F8;
                            case 20: return KEY_F9;
                            case 21: return KEY_F10;
                            case 23: return KEY_F11;
                            case 24: return KEY_F12;
                            default: return KEY_UNKNOWN;
                        }
                    }
                }
            }
            
            return KEY_UNKNOWN;
        }
        
        // Check for alternative escape sequences (\033O for some terminals)
        if (ch1 == 'O') {
            unsigned char ch2;
            if (read(STDIN_FILENO, &ch2, 1) != 1) {
                return KEY_UNKNOWN;
            }
            
            switch (ch2) {
                case 'P': return KEY_F1;
                case 'Q': return KEY_F2;
                case 'R': return KEY_F3;
                case 'S': return KEY_F4;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
                default: return KEY_UNKNOWN;
            }
        }
        
        // Unknown escape sequence
        return KEY_UNKNOWN;
    }
    
public:
    ConsoleInputPOSIX() : initialized(false) {}
    
    ~ConsoleInputPOSIX() override {
        cleanup();
    }
    
    bool kbhit() override {
        if (!initialized) return false;
        
        struct timeval tv;
        fd_set rdfs;
        
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        
        FD_ZERO(&rdfs);
        FD_SET(STDIN_FILENO, &rdfs);
        
        select(STDIN_FILENO + 1, &rdfs, nullptr, nullptr, &tv);
        return FD_ISSET(STDIN_FILENO, &rdfs);
    }
    
    int getch() override {
        if (!initialized) return -1;
        
        unsigned char ch;
        if (read(STDIN_FILENO, &ch, 1) == 1) {
            return ch;
        }
        return -1;
    }
    
    int getKey() override {
        // Task 2.3: Resolve ANSI escape sequences
        if (!initialized) return KEY_ERROR;
        
        unsigned char ch;
        if (read(STDIN_FILENO, &ch, 1) != 1) {
            return KEY_ERROR;
        }
        
        // Handle escape sequences
        if (ch == 27) {  // ESC
            return readEscapeSequence();
        }
        
        // Regular character or control key
        return ch;
    }
    
    bool initialize() override {
        if (initialized) return true;
        
        // Save original terminal settings
        if (tcgetattr(STDIN_FILENO, &original_termios) != 0) {
            return false;
        }
        
        struct termios raw = original_termios;
        
        // Set terminal to raw mode
        // Disable canonical mode (line buffering)
        raw.c_lflag &= ~(ICANON | ECHO);
        
        // Disable special processing
        raw.c_cc[VMIN] = 1;  // Minimum characters to read
        raw.c_cc[VTIME] = 0; // No timeout
        
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            return false;
        }
        
        initialized = true;
        return true;
    }
    
    void cleanup() override {
        if (initialized) {
            // Restore original terminal settings
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
            initialized = false;
        }
    }
    
    bool enableRawMode() override {
        if (!initialized) return false;
        
        struct termios raw = original_termios;
        
        // Set terminal to raw mode
        // Disable canonical mode (line buffering) and echo
        raw.c_lflag &= ~(ICANON | ECHO);
        
        // Disable special processing
        raw.c_cc[VMIN] = 1;  // Minimum characters to read
        raw.c_cc[VTIME] = 0; // No timeout
        
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            return false;
        }
        
        return true;
    }
    
    bool enableCanonicalMode() override {
        if (!initialized) return false;
        
        struct termios canonical = original_termios;
        
        // Enable canonical mode (line buffering) and echo
        canonical.c_lflag |= (ICANON | ECHO);
        
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &canonical) != 0) {
            return false;
        }
        
        return true;
    }
};

IConsoleInput* createConsoleInput() {
    return new ConsoleInputPOSIX();
}

#endif // __linux__ || __APPLE__
