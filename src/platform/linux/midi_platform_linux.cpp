#include "platform/midi_platform.h"
#include <cstring>

#if defined(__linux__)

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#include <thread>
#include <chrono>
#endif

/**
 * Linux MIDI Platform Implementation using ALSA Sequencer
 * 
 * Task 2.1: Full MIDI implementation for Linux to achieve feature parity
 * with Windows (WinMM) and macOS (CoreMIDI).
 * 
 * Uses ALSA Sequencer API for MIDI output with software synthesizer.
 */
class MIDIPlatformLinux : public MIDIPlatformInterface {
public:
    MIDIPlatformLinux() 
        : opened(false)
#ifdef HAVE_ALSA
        , seq_handle(nullptr)
        , port_id(-1)
#endif
    {
        std::strcpy(errorMsg, "");
    }
    
    ~MIDIPlatformLinux() override {
        close();
    }
    
    bool open() override {
#ifdef HAVE_ALSA
        if (opened) {
            return true;
        }
        
        // Open ALSA sequencer in non-blocking mode
        int err = snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_OUTPUT, SND_SEQ_NONBLOCK);
        if (err < 0) {
            snprintf(errorMsg, sizeof(errorMsg), 
                "Failed to open ALSA sequencer: %s", snd_strerror(err));
            return false;
        }
        
        // Set client name
        snd_seq_set_client_name(seq_handle, "NanoVNA-CLI");
        
        // Create output port
        port_id = snd_seq_create_simple_port(seq_handle, "MIDI Output",
            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        
        if (port_id < 0) {
            snprintf(errorMsg, sizeof(errorMsg),
                "Failed to create ALSA port: %s", snd_strerror(port_id));
            snd_seq_close(seq_handle);
            seq_handle = nullptr;
            return false;
        }
        
        // Try to connect to a software synthesizer (TiMidity or FluidSynth)
        // We'll try common MIDI through ports
        connectToSynth();
        
        opened = true;
        std::strcpy(errorMsg, "");
        return true;
#else
        std::strcpy(errorMsg, "ALSA not available. Install libasound2-dev and rebuild.");
        return false;
#endif
    }
    
    void close() override {
#ifdef HAVE_ALSA
        if (!opened) {
            return;
        }
        
        if (seq_handle) {
            // Send all notes off to avoid stuck notes
            for (int channel = 0; channel < 16; channel++) {
                sendMessage(0xB0 | channel, 123, 0);  // All notes off
            }
            
            // Give time for messages to be sent
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            snd_seq_close(seq_handle);
            seq_handle = nullptr;
        }
        
        port_id = -1;
        opened = false;
#endif
    }
    
    bool isOpen() const override {
        return opened;
    }
    
    void sendMessage(uint8_t status, uint8_t data1, uint8_t data2) override {
#ifdef HAVE_ALSA
        if (!opened || !seq_handle) {
            return;
        }
        
        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_source(&ev, port_id);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        
        uint8_t cmd = status & 0xF0;
        uint8_t channel = status & 0x0F;
        
        switch (cmd) {
            case 0x80:  // Note Off
                snd_seq_ev_set_noteoff(&ev, channel, data1, data2);
                break;
            case 0x90:  // Note On
                snd_seq_ev_set_noteon(&ev, channel, data1, data2);
                break;
            case 0xB0:  // Control Change
                snd_seq_ev_set_controller(&ev, channel, data1, data2);
                break;
            case 0xC0:  // Program Change
                snd_seq_ev_set_pgmchange(&ev, channel, data1);
                break;
            case 0xE0:  // Pitch Bend
                {
                    int value = (data2 << 7) | data1;
                    snd_seq_ev_set_pitchbend(&ev, channel, value - 8192);
                }
                break;
            default:
                // Unsupported message type
                return;
        }
        
        snd_seq_event_output(seq_handle, &ev);
        snd_seq_drain_output(seq_handle);
#else
        (void)status;
        (void)data1;
        (void)data2;
#endif
    }
    
    const char* getPlatformName() const override {
#ifdef HAVE_ALSA
        return "Linux (ALSA)";
#else
        return "Linux (ALSA not available)";
#endif
    }
    
    const char* getLastError() const override {
        return errorMsg[0] != '\0' ? errorMsg : nullptr;
    }

private:
#ifdef HAVE_ALSA
    void connectToSynth() {
        // Try to find and connect to a software synthesizer
        // Common ports: TiMidity (128:0), FluidSynth (various)
        snd_seq_client_info_t *cinfo;
        snd_seq_port_info_t *pinfo;
        
        snd_seq_client_info_alloca(&cinfo);
        snd_seq_port_info_alloca(&pinfo);
        
        snd_seq_client_info_set_client(cinfo, -1);
        
        while (snd_seq_query_next_client(seq_handle, cinfo) >= 0) {
            int client = snd_seq_client_info_get_client(cinfo);
            
            snd_seq_port_info_set_client(pinfo, client);
            snd_seq_port_info_set_port(pinfo, -1);
            
            while (snd_seq_query_next_port(seq_handle, pinfo) >= 0) {
                unsigned int cap = snd_seq_port_info_get_capability(pinfo);
                unsigned int type = snd_seq_port_info_get_type(pinfo);
                
                // Look for writeable MIDI synth ports
                if ((cap & SND_SEQ_PORT_CAP_WRITE) &&
                    (cap & SND_SEQ_PORT_CAP_SUBS_WRITE) &&
                    (type & (SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_SYNTH))) {
                    
                    int port = snd_seq_port_info_get_port(pinfo);
                    const char *name = snd_seq_port_info_get_name(pinfo);
                    
                    // Try to connect to this port
                    snd_seq_port_subscribe_t *subs;
                    snd_seq_port_subscribe_alloca(&subs);
                    snd_seq_addr_t sender;
                    sender.client = snd_seq_client_id(seq_handle);
                    sender.port = port_id;
                    snd_seq_port_subscribe_set_sender(subs, &sender);
                    snd_seq_addr_t dest;
                    dest.client = client;
                    dest.port = port;
                    snd_seq_port_subscribe_set_dest(subs, &dest);
                    
                    if (snd_seq_subscribe_port(seq_handle, subs) >= 0) {
                        // Successfully connected to synth
                        snprintf(errorMsg, sizeof(errorMsg), 
                            "Connected to MIDI synth: %s", name);
                        return;
                    }
                }
            }
        }
        
        // No synth found - user will need to manually connect or run TiMidity/FluidSynth
        snprintf(errorMsg, sizeof(errorMsg),
            "Warning: No MIDI synthesizer found. Run 'timidity -iA' or 'fluidsynth' for sound.");
    }
    
    snd_seq_t *seq_handle;
    int port_id;
#endif
    
    bool opened;
    char errorMsg[256];
};

// Factory function
MIDIPlatformInterface* createMIDIPlatform() {
    return new MIDIPlatformLinux();
}

#endif // __linux__
