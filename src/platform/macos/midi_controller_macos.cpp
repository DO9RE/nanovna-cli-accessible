#include "platform/midi_controller.h"

#ifdef __APPLE__

#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mutex>
#include <atomic>

/**
 * macOS CoreMIDI implementation of IMidiControllerInput
 * 
 * Uses CoreMIDI for receiving MIDI events from hardware controllers
 * and sending feedback to motor faders.
 */
class CoreMidiControllerInput : public IMidiControllerInput {
public:
    CoreMidiControllerInput() = default;
    
    ~CoreMidiControllerInput() override {
        close();
    }
    
    std::vector<MidiDeviceInfo> listDevices() override {
        std::vector<MidiDeviceInfo> devices;
        
        ItemCount numSources = MIDIGetNumberOfSources();
        for (ItemCount i = 0; i < numSources; i++) {
            MIDIEndpointRef src = MIDIGetSource(i);
            if (src == 0) continue;
            
            MidiDeviceInfo info;
            info.id = static_cast<int>(i);
            
            CFStringRef name = nullptr;
            MIDIObjectGetStringProperty(src, kMIDIPropertyDisplayName, &name);
            if (name) {
                char buf[256];
                CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
                info.name = buf;
                CFRelease(name);
            } else {
                info.name = "MIDI Source " + std::to_string(i);
            }
            
            devices.push_back(info);
        }
        return devices;
    }
    
    bool open(int deviceId) override {
        if (midiClient) close();
        
        OSStatus status = MIDIClientCreate(CFSTR("NanoVNA-CLI-Controller"), nullptr, nullptr, &midiClient);
        if (status != noErr) {
            lastError = "Failed to create MIDI client: " + std::to_string(status);
            return false;
        }
        
        status = MIDIInputPortCreate(midiClient, CFSTR("Controller Input"), readProc, this, &inputPort);
        if (status != noErr) {
            lastError = "Failed to create MIDI input port: " + std::to_string(status);
            MIDIClientDispose(midiClient);
            midiClient = 0;
            return false;
        }
        
        status = MIDIOutputPortCreate(midiClient, CFSTR("Controller Output"), &outputPort);
        if (status != noErr) {
            // Output port is optional (for feedback), continue without it
            outputPort = 0;
        }
        
        // Connect to specified source
        ItemCount numSources = MIDIGetNumberOfSources();
        if (numSources == 0) {
            lastError = "No MIDI sources available";
            MIDIClientDispose(midiClient);
            midiClient = 0;
            return false;
        }
        
        ItemCount srcIdx = (deviceId >= 0 && static_cast<ItemCount>(deviceId) < numSources) 
                          ? static_cast<ItemCount>(deviceId) : 0;
        
        connectedSource = MIDIGetSource(srcIdx);
        status = MIDIPortConnectSource(inputPort, connectedSource, nullptr);
        if (status != noErr) {
            lastError = "Failed to connect to MIDI source: " + std::to_string(status);
            MIDIClientDispose(midiClient);
            midiClient = 0;
            return false;
        }
        
        // Get device name
        CFStringRef name = nullptr;
        MIDIObjectGetStringProperty(connectedSource, kMIDIPropertyDisplayName, &name);
        if (name) {
            char buf[256];
            CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
            currentDeviceName = buf;
            CFRelease(name);
        }
        
        // Find matching destination for feedback
        ItemCount numDests = MIDIGetNumberOfDestinations();
        for (ItemCount i = 0; i < numDests; i++) {
            MIDIEndpointRef dest = MIDIGetDestination(i);
            CFStringRef destName = nullptr;
            MIDIObjectGetStringProperty(dest, kMIDIPropertyDisplayName, &destName);
            if (destName) {
                char buf[256];
                CFStringGetCString(destName, buf, sizeof(buf), kCFStringEncodingUTF8);
                if (std::string(buf).find(currentDeviceName.substr(0, std::min(size_t(16), currentDeviceName.size()))) != std::string::npos) {
                    connectedDest = dest;
                }
                CFRelease(destName);
            }
        }
        
        isOpenFlag = true;
        return true;
    }
    
    void close() override {
        isOpenFlag = false;
        
        if (midiClient) {
            MIDIClientDispose(midiClient);
            midiClient = 0;
        }
        inputPort = 0;
        outputPort = 0;
        connectedSource = 0;
        connectedDest = 0;
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
        if (!outputPort || !connectedDest) return false;
        
        MIDIPacketList packetList;
        MIDIPacket* packet = MIDIPacketListInit(&packetList);
        
        Byte msg[3] = { status, data1, data2 };
        packet = MIDIPacketListAdd(&packetList, sizeof(packetList), packet, 0, 3, msg);
        
        if (!packet) return false;
        
        OSStatus err = MIDISend(outputPort, connectedDest, &packetList);
        return err == noErr;
    }
    
    const char* getPlatformName() const override {
        return "macOS CoreMIDI";
    }
    
    std::string getLastError() const override {
        return lastError;
    }
    
    std::string getDeviceName() const override {
        return currentDeviceName;
    }

private:
    static void readProc(const MIDIPacketList* pktList, void* readProcRefCon, void* srcConnRefCon) {
        auto* self = static_cast<CoreMidiControllerInput*>(readProcRefCon);
        if (!self || !self->isOpenFlag) return;
        
        const MIDIPacket* packet = &pktList->packet[0];
        for (UInt32 i = 0; i < pktList->numPackets; i++) {
            // Process each MIDI message in the packet
            for (UInt16 j = 0; j < packet->length;) {
                uint8_t statusByte = packet->data[j];
                uint8_t type = statusByte & 0xF0;
                
                MidiControllerEvent event;
                event.channel = statusByte & 0x0F;
                bool validEvent = false;
                
                if (type == 0x90 && j + 2 < packet->length) {
                    event.type = MidiMessageType::NOTE_ON;
                    event.data1 = packet->data[j + 1];
                    event.data2 = packet->data[j + 2];
                    validEvent = true;
                    j += 3;
                } else if (type == 0x80 && j + 2 < packet->length) {
                    event.type = MidiMessageType::NOTE_OFF;
                    event.data1 = packet->data[j + 1];
                    event.data2 = packet->data[j + 2];
                    validEvent = true;
                    j += 3;
                } else if (type == 0xB0 && j + 2 < packet->length) {
                    event.type = MidiMessageType::CONTROL_CHANGE;
                    event.data1 = packet->data[j + 1];
                    event.data2 = packet->data[j + 2];
                    validEvent = true;
                    j += 3;
                } else {
                    j++; // Skip unknown bytes
                }
                
                if (validEvent) {
                    std::lock_guard<std::mutex> lock(self->cbMutex);
                    if (self->eventCallback) {
                        self->eventCallback(event);
                    }
                }
            }
            
            packet = MIDIPacketNext(packet);
        }
    }
    
    MIDIClientRef midiClient = 0;
    MIDIPortRef inputPort = 0;
    MIDIPortRef outputPort = 0;
    MIDIEndpointRef connectedSource = 0;
    MIDIEndpointRef connectedDest = 0;
    
    std::string currentDeviceName;
    std::string lastError;
    std::atomic<bool> isOpenFlag{false};
    
    MidiEventCallback eventCallback;
    std::mutex cbMutex;
};

IMidiControllerInput* createMidiControllerInput() {
    return new CoreMidiControllerInput();
}

#endif // __APPLE__
