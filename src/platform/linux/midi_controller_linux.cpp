#include "platform/midi_controller.h"

#if defined(__linux__)

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstring>
#include <vector>
#include <poll.h>

/**
 * Linux ALSA implementation of IMidiControllerInput
 * 
 * Uses ALSA Sequencer API for MIDI input from hardware controllers.
 * Creates both input and output ports so we can receive events
 * and send feedback to motor faders.
 */
class AlsaMidiControllerInput : public IMidiControllerInput {
public:
    AlsaMidiControllerInput() = default;
    
    ~AlsaMidiControllerInput() override {
        close();
    }
    
    std::vector<MidiDeviceInfo> listDevices() override {
        std::vector<MidiDeviceInfo> devices;
        
        snd_seq_t* seq = nullptr;
        if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
            lastError = "Failed to open ALSA sequencer for device enumeration";
            return devices;
        }
        
        snd_seq_client_info_t* cinfo;
        snd_seq_port_info_t* pinfo;
        snd_seq_client_info_alloca(&cinfo);
        snd_seq_port_info_alloca(&pinfo);
        
        snd_seq_client_info_set_client(cinfo, -1);
        while (snd_seq_query_next_client(seq, cinfo) >= 0) {
            int client = snd_seq_client_info_get_client(cinfo);
            
            // Skip system clients
            if (client == 0) continue;
            // Skip our own client
            if (seq_handle && client == snd_seq_client_id(seq_handle)) continue;
            
            snd_seq_port_info_set_client(pinfo, client);
            snd_seq_port_info_set_port(pinfo, -1);
            
            while (snd_seq_query_next_port(seq, pinfo) >= 0) {
                unsigned int caps = snd_seq_port_info_get_capability(pinfo);
                unsigned int type = snd_seq_port_info_get_type(pinfo);
                
                // Look for ports that can read (output to us) and are hardware or software
                bool canRead = (caps & SND_SEQ_PORT_CAP_READ) && (caps & SND_SEQ_PORT_CAP_SUBS_READ);
                bool isMidiPort = (type & SND_SEQ_PORT_TYPE_MIDI_GENERIC) || 
                                  (type & SND_SEQ_PORT_TYPE_HARDWARE);
                
                if (canRead && isMidiPort) {
                    MidiDeviceInfo info;
                    // Encode client:port as single ID
                    info.id = (client << 8) | snd_seq_port_info_get_port(pinfo);
                    info.name = std::string(snd_seq_client_info_get_name(cinfo)) + ":" +
                               std::string(snd_seq_port_info_get_name(pinfo));
                    devices.push_back(info);
                }
            }
        }
        
        snd_seq_close(seq);
        return devices;
    }
    
    bool open(int deviceId) override {
        if (seq_handle) close();
        
        int err = snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
        if (err < 0) {
            lastError = "Failed to open ALSA sequencer: " + std::string(snd_strerror(err));
            return false;
        }
        
        snd_seq_set_client_name(seq_handle, "NanoVNA-CLI-Controller");
        
        // Create input port
        inputPort = snd_seq_create_simple_port(seq_handle, "Controller Input",
            SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_MIDI_GENERIC);
        
        if (inputPort < 0) {
            lastError = "Failed to create ALSA input port";
            snd_seq_close(seq_handle);
            seq_handle = nullptr;
            return false;
        }
        
        // Create output port (for motor fader feedback)
        outputPort = snd_seq_create_simple_port(seq_handle, "Controller Output",
            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
            SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_MIDI_GENERIC);
        
        if (outputPort < 0) {
            lastError = "Failed to create ALSA output port";
            snd_seq_close(seq_handle);
            seq_handle = nullptr;
            return false;
        }
        
        // Connect to the specified device
        if (deviceId >= 0) {
            int destClient = (deviceId >> 8) & 0xFF;
            int destPort = deviceId & 0xFF;
            
            // Subscribe to input from the device
            err = snd_seq_connect_from(seq_handle, inputPort, destClient, destPort);
            if (err < 0) {
                lastError = "Failed to connect input from device " + std::to_string(destClient) + ":" + 
                           std::to_string(destPort) + ": " + std::string(snd_strerror(err));
                snd_seq_close(seq_handle);
                seq_handle = nullptr;
                return false;
            }
            
            // Connect output to the device (for motor fader feedback)
            // Check if device port supports write
            snd_seq_port_info_t* pinfo;
            snd_seq_port_info_alloca(&pinfo);
            if (snd_seq_get_any_port_info(seq_handle, destClient, destPort, pinfo) >= 0) {
                unsigned int caps = snd_seq_port_info_get_capability(pinfo);
                if (caps & SND_SEQ_PORT_CAP_WRITE) {
                    snd_seq_connect_to(seq_handle, outputPort, destClient, destPort);
                }
            }
            
            // Store connected device info
            snd_seq_client_info_t* cinfo;
            snd_seq_client_info_alloca(&cinfo);
            if (snd_seq_get_any_client_info(seq_handle, destClient, cinfo) >= 0) {
                currentDeviceName = snd_seq_client_info_get_name(cinfo);
            }
        }
        
        // Start the input polling thread
        running = true;
        pollThread = std::thread(&AlsaMidiControllerInput::pollLoop, this);
        
        return true;
    }
    
    void close() override {
        running = false;
        if (pollThread.joinable()) {
            pollThread.join();
        }
        
        if (seq_handle) {
            snd_seq_close(seq_handle);
            seq_handle = nullptr;
        }
        inputPort = -1;
        outputPort = -1;
        currentDeviceName.clear();
    }
    
    bool isOpen() const override {
        return seq_handle != nullptr && running;
    }
    
    void setEventCallback(MidiEventCallback callback) override {
        std::lock_guard<std::mutex> lock(cbMutex);
        eventCallback = callback;
    }
    
    bool sendFeedback(uint8_t status, uint8_t data1, uint8_t data2) override {
        if (!seq_handle || outputPort < 0) return false;
        
        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_source(&ev, outputPort);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        
        uint8_t type = status & 0xF0;
        uint8_t channel = status & 0x0F;
        
        switch (type) {
            case 0xB0: // Control Change
                snd_seq_ev_set_controller(&ev, channel, data1, data2);
                break;
            case 0x90: // Note On
                snd_seq_ev_set_noteon(&ev, channel, data1, data2);
                break;
            case 0x80: // Note Off
                snd_seq_ev_set_noteoff(&ev, channel, data1, data2);
                break;
            default:
                return false;
        }
        
        int err = snd_seq_event_output_direct(seq_handle, &ev);
        return err >= 0;
    }
    
    const char* getPlatformName() const override {
        return "Linux ALSA";
    }
    
    std::string getLastError() const override {
        return lastError;
    }
    
    std::string getDeviceName() const override {
        return currentDeviceName;
    }

private:
    void pollLoop() {
        int npfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
        if (npfds <= 0) return;
        
        std::vector<struct pollfd> pfds(npfds);
        snd_seq_poll_descriptors(seq_handle, pfds.data(), npfds, POLLIN);
        
        while (running) {
            int ret = poll(pfds.data(), npfds, 100); // 100ms timeout
            if (ret <= 0) continue;
            
            snd_seq_event_t* ev = nullptr;
            while (snd_seq_event_input(seq_handle, &ev) >= 0 && ev) {
                MidiControllerEvent event;
                bool validEvent = false;
                
                switch (ev->type) {
                    case SND_SEQ_EVENT_NOTEON:
                        event.type = MidiMessageType::NOTE_ON;
                        event.channel = ev->data.note.channel;
                        event.data1 = ev->data.note.note;
                        event.data2 = ev->data.note.velocity;
                        validEvent = true;
                        break;
                        
                    case SND_SEQ_EVENT_NOTEOFF:
                        event.type = MidiMessageType::NOTE_OFF;
                        event.channel = ev->data.note.channel;
                        event.data1 = ev->data.note.note;
                        event.data2 = ev->data.note.velocity;
                        validEvent = true;
                        break;
                        
                    case SND_SEQ_EVENT_CONTROLLER:
                        event.type = MidiMessageType::CONTROL_CHANGE;
                        event.channel = ev->data.control.channel;
                        event.data1 = ev->data.control.param;
                        event.data2 = ev->data.control.value;
                        validEvent = true;
                        break;
                        
                    default:
                        break;
                }
                
                if (validEvent) {
                    std::lock_guard<std::mutex> lock(cbMutex);
                    if (eventCallback) {
                        eventCallback(event);
                    }
                }
            }
        }
    }
    
    snd_seq_t* seq_handle = nullptr;
    int inputPort = -1;
    int outputPort = -1;
    std::string currentDeviceName;
    std::string lastError;
    
    std::atomic<bool> running{false};
    std::thread pollThread;
    
    MidiEventCallback eventCallback;
    std::mutex cbMutex;
};

#endif // HAVE_ALSA

/**
 * Stub implementation when ALSA is not available
 */
#ifndef HAVE_ALSA
class StubMidiControllerInput : public IMidiControllerInput {
public:
    std::vector<MidiDeviceInfo> listDevices() override { return {}; }
    bool open(int) override { lastError = "ALSA not available"; return false; }
    void close() override {}
    bool isOpen() const override { return false; }
    void setEventCallback(MidiEventCallback) override {}
    bool sendFeedback(uint8_t, uint8_t, uint8_t) override { return false; }
    const char* getPlatformName() const override { return "Linux (no ALSA)"; }
    std::string getLastError() const override { return lastError; }
    std::string getDeviceName() const override { return ""; }
private:
    std::string lastError = "ALSA not available - install libasound2-dev";
};
#endif

IMidiControllerInput* createMidiControllerInput() {
#ifdef HAVE_ALSA
    return new AlsaMidiControllerInput();
#else
    return new StubMidiControllerInput();
#endif
}

#endif // __linux__
