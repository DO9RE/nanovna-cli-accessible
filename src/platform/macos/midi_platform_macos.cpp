#include "platform/midi_platform.h"

#if defined(__APPLE__)

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <cstdio>

/**
 * macOS MIDI Platform Implementation
 * Uses AudioUnit (DLS Synth) for MIDI synthesis with AUGraph for audio routing
 */
class MIDIPlatformMacOS : public MIDIPlatformInterface {
public:
    MIDIPlatformMacOS() 
        : graph(nullptr)
        , samplerUnit(nullptr)
        , opened(false)
        , lastErrorMsg(nullptr) 
    {
        // This constructor being called proves macOS implementation is linked
    }
    
    ~MIDIPlatformMacOS() override {
        close();
    }
    
    bool open() override {
        if (opened) {
            return true;  // Already open
        }
        
        OSStatus status;
        
        // Create AUGraph for audio routing
        status = NewAUGraph(&graph);
        if (status != noErr) {
            snprintf(errorBuffer, sizeof(errorBuffer), "Failed to create AUGraph (error: %d)", (int)status);
            lastErrorMsg = errorBuffer;
            return false;
        }
        
        // Create DLS Synth node
        AudioComponentDescription synthDesc;
        synthDesc.componentType = kAudioUnitType_MusicDevice;
        synthDesc.componentSubType = kAudioUnitSubType_DLSSynth;
        synthDesc.componentManufacturer = kAudioUnitManufacturer_Apple;
        synthDesc.componentFlags = 0;
        synthDesc.componentFlagsMask = 0;
        
        AUNode synthNode;
        status = AUGraphAddNode(graph, &synthDesc, &synthNode);
        if (status != noErr) {
            snprintf(errorBuffer, sizeof(errorBuffer), "Failed to add DLS Synth node (error: %d)", (int)status);
            lastErrorMsg = errorBuffer;
            DisposeAUGraph(graph);
            graph = nullptr;
            return false;
        }
        
        // Create default output node
        AudioComponentDescription outputDesc;
        outputDesc.componentType = kAudioUnitType_Output;
        outputDesc.componentSubType = kAudioUnitSubType_DefaultOutput;
        outputDesc.componentManufacturer = kAudioUnitManufacturer_Apple;
        outputDesc.componentFlags = 0;
        outputDesc.componentFlagsMask = 0;
        
        AUNode outputNode;
        status = AUGraphAddNode(graph, &outputDesc, &outputNode);
        if (status != noErr) {
            snprintf(errorBuffer, sizeof(errorBuffer), "Failed to add output node (error: %d)", (int)status);
            lastErrorMsg = errorBuffer;
            DisposeAUGraph(graph);
            graph = nullptr;
            return false;
        }
        
        // Connect DLS Synth output to default output input
        status = AUGraphConnectNodeInput(graph, synthNode, 0, outputNode, 0);
        if (status != noErr) {
            snprintf(errorBuffer, sizeof(errorBuffer), "Failed to connect nodes (error: %d)", (int)status);
            lastErrorMsg = errorBuffer;
            DisposeAUGraph(graph);
            graph = nullptr;
            return false;
        }
        
        // Open the graph (prepares the audio units)
        status = AUGraphOpen(graph);
        if (status != noErr) {
            snprintf(errorBuffer, sizeof(errorBuffer), "Failed to open AUGraph (error: %d)", (int)status);
            lastErrorMsg = errorBuffer;
            DisposeAUGraph(graph);
            graph = nullptr;
            return false;
        }
        
        // Get reference to the DLS Synth unit for sending MIDI messages
        status = AUGraphNodeInfo(graph, synthNode, nullptr, &samplerUnit);
        if (status != noErr) {
            snprintf(errorBuffer, sizeof(errorBuffer), "Failed to get synth unit reference (error: %d)", (int)status);
            lastErrorMsg = errorBuffer;
            DisposeAUGraph(graph);
            graph = nullptr;
            samplerUnit = nullptr;
            return false;
        }
        
        // Configure DLS Synth for precise measurement (disable effects)
        // This must be done after getting the unit reference but before initialization
        
        // 1. Disable reverb using AudioUnit parameter (not property)
        // Using kMusicDeviceParam_ReverbVolume parameter instead of property
        Float32 reverbLevel = 0.0f;
        status = AudioUnitSetParameter(samplerUnit,
                                       kMusicDeviceParam_ReverbVolume,
                                       kAudioUnitScope_Global,
                                       0,
                                       reverbLevel,
                                       0);
        // Note: We don't fail if reverb can't be disabled, just continue
        // The MIDI CC messages will also help minimize effects
        
        // 2. Set CPU load to prioritize precision over effects
        // Lower CPU load = fewer effects, cleaner sound
        Float32 cpuLoad = 0.0f;  // Minimum CPU load = maximum precision
        AudioUnitSetProperty(samplerUnit,
                            kAudioUnitProperty_CPULoad,
                            kAudioUnitScope_Global,
                            0,
                            &cpuLoad,
                            sizeof(cpuLoad));
        
        // Note: We removed the modulation depth property call as it doesn't exist
        // Modulation is controlled via MIDI CC messages from midi_engine.cpp
        
        // Initialize the graph
        status = AUGraphInitialize(graph);
        if (status != noErr) {
            snprintf(errorBuffer, sizeof(errorBuffer), "Failed to initialize AUGraph (error: %d)", (int)status);
            lastErrorMsg = errorBuffer;
            DisposeAUGraph(graph);
            graph = nullptr;
            samplerUnit = nullptr;
            return false;
        }
        
        // Start the graph
        status = AUGraphStart(graph);
        if (status != noErr) {
            snprintf(errorBuffer, sizeof(errorBuffer), "Failed to start AUGraph (error: %d)", (int)status);
            lastErrorMsg = errorBuffer;
            AUGraphUninitialize(graph);
            DisposeAUGraph(graph);
            graph = nullptr;
            samplerUnit = nullptr;
            return false;
        }
        
        opened = true;
        lastErrorMsg = nullptr;
        return true;
    }
    
    void close() override {
        if (graph != nullptr) {
            // Stop the graph first (safe even if not running)
            Boolean isRunning = FALSE;
            AUGraphIsRunning(graph, &isRunning);
            if (isRunning) {
                AUGraphStop(graph);
            }
            
            // Uninitialize (safe even if not initialized)
            Boolean isInitialized = FALSE;
            AUGraphIsInitialized(graph, &isInitialized);
            if (isInitialized) {
                AUGraphUninitialize(graph);
            }
            
            // Finally dispose the graph
            DisposeAUGraph(graph);
            graph = nullptr;
            samplerUnit = nullptr;
        }
        opened = false;
    }
    
    bool isOpen() const override {
        return opened;
    }
    
    void sendMessage(uint8_t status, uint8_t data1, uint8_t data2) override {
        if (samplerUnit == nullptr) return;
        
        // Send MIDI event directly to audio unit
        // MusicDeviceMIDIEvent sends a 3-byte MIDI message
        MusicDeviceMIDIEvent(
            samplerUnit,
            status,
            data1,
            data2,
            0  // Sample offset (0 = immediate)
        );
    }
    
    const char* getPlatformName() const override {
        return "macOS (AudioUnit/DLS Synth)";
    }
    
    const char* getLastError() const override {
        return lastErrorMsg;
    }

private:
    AUGraph graph;
    AudioUnit samplerUnit;
    bool opened;
    const char* lastErrorMsg;
    char errorBuffer[256];  // Thread-safe instance storage for error messages
};

// Factory function
MIDIPlatformInterface* createMIDIPlatform() {
    return new MIDIPlatformMacOS();
}

#endif // __APPLE__
