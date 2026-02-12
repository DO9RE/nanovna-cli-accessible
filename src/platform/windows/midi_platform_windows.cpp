#include "platform/midi_platform.h"

#if defined(_WIN32)

#include <windows.h>
#include <mmsystem.h>
#include <cstdio>

/**
 * Windows MIDI Platform Implementation
 * Uses WinMM (Windows Multimedia) API with MIDI_MAPPER
 */
class MIDIPlatformWindows : public MIDIPlatformInterface {
public:
    MIDIPlatformWindows() 
        : hMidiOut(nullptr)
        , lastErrorMsg(nullptr) 
    {
    }
    
    ~MIDIPlatformWindows() override {
        close();
    }
    
    bool open() override {
        if (hMidiOut != nullptr) {
            return true;  // Already open
        }
        
        MMRESULT result = midiOutOpen(&hMidiOut, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
        if (result != MMSYSERR_NOERROR) {
            snprintf(errorBuffer, sizeof(errorBuffer), "midiOutOpen failed with error code: %d", result);
            lastErrorMsg = errorBuffer;
            return false;
        }
        
        lastErrorMsg = nullptr;
        return true;
    }
    
    void close() override {
        if (hMidiOut != nullptr) {
            midiOutReset(hMidiOut);
            midiOutClose(hMidiOut);
            hMidiOut = nullptr;
        }
    }
    
    bool isOpen() const override {
        return hMidiOut != nullptr;
    }
    
    void sendMessage(uint8_t status, uint8_t data1, uint8_t data2) override {
        if (hMidiOut == nullptr) return;
        
        DWORD message = status | (data1 << 8) | (data2 << 16);
        midiOutShortMsg(hMidiOut, message);
    }
    
    const char* getPlatformName() const override {
        return "Windows (WinMM)";
    }
    
    const char* getLastError() const override {
        return lastErrorMsg;
    }

private:
    HMIDIOUT hMidiOut;
    const char* lastErrorMsg;
    char errorBuffer[256];  // Thread-safe instance storage for error messages
};

// Factory function
MIDIPlatformInterface* createMIDIPlatform() {
    return new MIDIPlatformWindows();
}

#endif // _WIN32
