#include "platform/midi_controller.h"

#ifdef _WIN32

#include <windows.h>
#include <mmsystem.h>
#include <mutex>
#include <atomic>
#include <cstring>

#pragma comment(lib, "winmm.lib")

/**
 * Windows WinMM implementation of IMidiControllerInput
 * 
 * Uses the Windows Multimedia MIDI Input API for receiving
 * MIDI events from hardware controllers and sending feedback.
 */
class WinMidiControllerInput : public IMidiControllerInput {
public:
    WinMidiControllerInput() = default;
    
    ~WinMidiControllerInput() override {
        close();
    }
    
    std::vector<MidiDeviceInfo> listDevices() override {
        std::vector<MidiDeviceInfo> devices;
        UINT numDevs = midiInGetNumDevs();
        
        for (UINT i = 0; i < numDevs; i++) {
            MIDIINCAPS caps;
            if (midiInGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
                MidiDeviceInfo info;
                info.id = static_cast<int>(i);
                info.name = caps.szPname;
                devices.push_back(info);
            }
        }
        return devices;
    }
    
    bool open(int deviceId) override {
        if (hMidiIn) close();
        
        UINT numDevs = midiInGetNumDevs();
        if (numDevs == 0) {
            lastError = "No MIDI input devices found";
            return false;
        }
        
        UINT devId = (deviceId >= 0) ? static_cast<UINT>(deviceId) : 0;
        if (devId >= numDevs) {
            lastError = "Invalid MIDI device ID";
            return false;
        }
        
        instance = this;
        MMRESULT result = midiInOpen(&hMidiIn, devId, 
                                      reinterpret_cast<DWORD_PTR>(midiInCallback),
                                      0, CALLBACK_FUNCTION);
        
        if (result != MMSYSERR_NOERROR) {
            char errMsg[256];
            midiInGetErrorText(result, errMsg, sizeof(errMsg));
            lastError = "Failed to open MIDI input: " + std::string(errMsg);
            hMidiIn = nullptr;
            return false;
        }
        
        // Store device name
        MIDIINCAPS caps;
        if (midiInGetDevCaps(devId, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            currentDeviceName = caps.szPname;
        }
        
        // Also open MIDI output for feedback if available
        UINT numOutDevs = midiOutGetNumDevs();
        // Try to find matching output device by name
        for (UINT i = 0; i < numOutDevs; i++) {
            MIDIOUTCAPS outCaps;
            if (midiOutGetDevCaps(i, &outCaps, sizeof(outCaps)) == MMSYSERR_NOERROR) {
                if (std::string(outCaps.szPname).find(currentDeviceName.substr(0, std::min(size_t(16), currentDeviceName.size()))) != std::string::npos) {
                    midiOutOpen(&hMidiOut, i, 0, 0, CALLBACK_NULL);
                    break;
                }
            }
        }
        
        result = midiInStart(hMidiIn);
        if (result != MMSYSERR_NOERROR) {
            lastError = "Failed to start MIDI input";
            midiInClose(hMidiIn);
            hMidiIn = nullptr;
            return false;
        }
        
        isOpenFlag = true;
        return true;
    }
    
    void close() override {
        isOpenFlag = false;
        
        if (hMidiIn) {
            midiInStop(hMidiIn);
            midiInReset(hMidiIn);
            midiInClose(hMidiIn);
            hMidiIn = nullptr;
        }
        
        if (hMidiOut) {
            midiOutClose(hMidiOut);
            hMidiOut = nullptr;
        }
        
        currentDeviceName.clear();
    }
    
    bool isOpen() const override {
        return isOpenFlag;
    }
    
    void setEventCallback(MidiEventCallback callback) override {
        std::lock_guard<std::mutex> lock(cbMutex);
        eventCallback = callback;
    }
    
    bool sendFeedback(uint8_t status, uint8_t data1, uint8_t data2) override {
        if (!hMidiOut) return false;
        
        DWORD msg = static_cast<DWORD>(status) | 
                    (static_cast<DWORD>(data1) << 8) | 
                    (static_cast<DWORD>(data2) << 16);
        
        return midiOutShortMsg(hMidiOut, msg) == MMSYSERR_NOERROR;
    }
    
    const char* getPlatformName() const override {
        return "Windows WinMM";
    }
    
    std::string getLastError() const override {
        return lastError;
    }
    
    std::string getDeviceName() const override {
        return currentDeviceName;
    }

private:
    static void CALLBACK midiInCallback(HMIDIIN hMidiIn, UINT wMsg, 
                                          DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
        if (wMsg != MIM_DATA || !instance) return;
        
        DWORD msg = static_cast<DWORD>(dwParam1);
        uint8_t status = msg & 0xFF;
        uint8_t data1 = (msg >> 8) & 0xFF;
        uint8_t data2 = (msg >> 16) & 0xFF;
        
        MidiControllerEvent event;
        uint8_t type = status & 0xF0;
        event.channel = status & 0x0F;
        event.data1 = data1;
        event.data2 = data2;
        
        bool validEvent = false;
        switch (type) {
            case 0x90:
                event.type = MidiMessageType::NOTE_ON;
                validEvent = true;
                break;
            case 0x80:
                event.type = MidiMessageType::NOTE_OFF;
                validEvent = true;
                break;
            case 0xB0:
                event.type = MidiMessageType::CONTROL_CHANGE;
                validEvent = true;
                break;
        }
        
        if (validEvent) {
            std::lock_guard<std::mutex> lock(instance->cbMutex);
            if (instance->eventCallback) {
                instance->eventCallback(event);
            }
        }
    }
    
    static WinMidiControllerInput* instance;
    
    HMIDIIN hMidiIn = nullptr;
    HMIDIOUT hMidiOut = nullptr;
    std::string currentDeviceName;
    std::string lastError;
    std::atomic<bool> isOpenFlag{false};
    
    MidiEventCallback eventCallback;
    std::mutex cbMutex;
};

WinMidiControllerInput* WinMidiControllerInput::instance = nullptr;

IMidiControllerInput* createMidiControllerInput() {
    return new WinMidiControllerInput();
}

#endif // _WIN32
