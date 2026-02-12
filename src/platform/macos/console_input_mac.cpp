#ifdef __APPLE__

#include "../interface/console_input.h"
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>

/**
 * macOS implementation of console input using termios
 */
class ConsoleInputMacOS : public IConsoleInput {
private:
    struct termios original_termios;
    bool initialized;
    
public:
    ConsoleInputMacOS() : initialized(false) {}
    
    ~ConsoleInputMacOS() override {
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
};

IConsoleInput* createConsoleInput() {
    return new ConsoleInputMacOS();
}

#endif // __APPLE__
