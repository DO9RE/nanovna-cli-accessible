#include "midi_engine.h"
#include "platform/midi_platform.h"
#include "pitch_mapping.h"
#include "reactance_effects_config.h"
#include "config.h"
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>
#include <set>

// Note: A4_FREQ and A4_NOTE constants moved to pitch_mapping.h

// Pitch bend range constant
// We always use 24 semitones (2 octaves) for maximum flexibility
// This range is set via RPN commands and applies to both gliding and dotted modes
static constexpr int PITCH_BEND_SEMITONES = 24; // 24 semitones (2 octaves)

// Velocity for gliding mode (fixed to avoid volume changes on note start)
// In gliding mode, volume is controlled via CC7 (Volume) for smooth modulation
// Note velocity is kept constant to ensure consistent attack
static constexpr uint8_t GLIDING_MODE_VELOCITY = 100;

MIDIEngine::MIDIEngine() {
    // Create platform-specific MIDI implementation
    platform.reset(createMIDIPlatform());
    // Initialize default instruments for each curve
    // Using General MIDI instrument numbers - selected for sustained, non-percussive tones
    curveInstruments[0] = 19;  // SWR: Church Organ (sustained)
    curveInstruments[1] = 16;  // Return Loss: Drawbar Organ (sustained)
    curveInstruments[2] = 81;  // Impedance Mag: Lead 2 (sawtooth) (sustained)
    curveInstruments[3] = 80;  // Reactance: Lead 1 (square) (sustained)
    curveInstruments[4] = 48;  // Phase: String Ensemble 1 (sustained)
    
    // Initialize note states
    for (int i = 0; i < NUM_CURVES; i++) {
        channelNotes[i].active = false;
        channelNotes[i].note = 0;
        channelNotes[i].velocity = 0;
    }
    
    // Initialize ruler note state
    rulerNote.active = false;
    rulerNote.note = 0;
    rulerNote.velocity = 0;
    
    // Initialize X-axis ruler note state
    xAxisRulerNote.active = false;
    xAxisRulerNote.note = 0;
    xAxisRulerNote.velocity = 0;
}

MIDIEngine::~MIDIEngine() noexcept {
    // Destructor must not throw - wrap close() in try-catch
    try {
        close();
    } catch (const std::exception& e) {
        // Can't log here since logger may not be available
        // Silently swallow exception to prevent termination
    } catch (...) {
        // Silently swallow any exception to prevent termination
    }
}

bool MIDIEngine::open() {
    std::lock_guard<std::mutex> l(mtx);
    
    if (opened) {
        if (logger) logger->log("MIDI", "Device already opened");
        return true;
    }
    
    // Log attempt to open with platform details
    if (logger && platform) {
        std::string msg = "Attempting to open MIDI device using: ";
        msg += platform->getPlatformName();
        logger->log("MIDI", msg);
    }
    
    // Open platform-specific MIDI
    if (!platform->open()) {
        if (logger) {
            std::string err = "Failed to open MIDI on ";
            err += platform->getPlatformName();
            err += ": ";
            if (platform->getLastError()) {
                err += platform->getLastError();
            } else {
                err += "(no error details available)";
            }
            logger->log("MIDI", err);
        }
        return false;
    }
    
    if (logger) {
        std::string msg = "MIDI device opened successfully (";
        msg += platform->getPlatformName();
        msg += ")";
        logger->log("MIDI", msg);
    }
    
    opened = true;
    
    // Calculate reference note from synth range
    calculateReferenceNote();
    
    // Initialize all channels with their instruments
    for (int i = 0; i < NUM_CURVES; i++) {
        // Map curve index to MIDI channel (skip channel 9/10 which is drums)
        int midiChannel = (i < 4) ? i : (i + 1);  // 0,1,2,3,5
        sendProgramChange(midiChannel, curveInstruments[i]);
        
        // Set default pan to center
        sendPan(midiChannel, 64);
        
        // Set default volume
        sendVolume(midiChannel, 100);
        
        // Disable all audio effects for clean, precise measurement signals
        uint8_t status = 0xB0 | (midiChannel & 0x0F);  // Control Change
        
        // Disable modulation and vibrato effects
        sendMIDIMessage(status, 1, 0);    // CC 1: Modulation Wheel = 0 (off)
        sendMIDIMessage(status, 76, 0);   // CC 76: Vibrato Rate = 0 (off)
        sendMIDIMessage(status, 77, 0);   // CC 77: Vibrato Depth = 0 (off)
        sendMIDIMessage(status, 78, 0);   // CC 78: Vibrato Delay = 0 (off)
        
        // Disable reverb, chorus, and other effects (critical for macOS DLS Synth)
        sendMIDIMessage(status, 91, 0);   // CC 91: Reverb Send Level = 0 (no reverb)
        sendMIDIMessage(status, 93, 0);   // CC 93: Chorus Send Level = 0 (no chorus)
        sendMIDIMessage(status, 94, 0);   // CC 94: Detune/Celeste = 0 (no detune)
        
        // Disable portamento for immediate pitch changes
        sendMIDIMessage(status, 5, 0);    // CC 5: Portamento Time = 0 (instant)
        sendMIDIMessage(status, 65, 0);   // CC 65: Portamento Off
        sendMIDIMessage(status, 84, 0);   // CC 84: Portamento Control = 0
        
        // Set pitch bend range to 24 semitones (2 octaves) for maximum flexibility
        // This wide range allows:
        // - Gliding mode: Smooth pitch transitions without retriggering across the entire synth range
        // - Dotted mode: Accurate pitch representation for each retriggered note
        // RPN (Registered Parameter Number) for Pitch Bend Sensitivity:
        sendMIDIMessage(status, 101, 0);  // RPN MSB = 0
        sendMIDIMessage(status, 100, 0);  // RPN LSB = 0 (Pitch Bend Sensitivity)
        sendMIDIMessage(status, 6, PITCH_BEND_SEMITONES);   // Data Entry MSB = 24 semitones
        sendMIDIMessage(status, 38, 0);   // Data Entry LSB = 0 cents
        sendMIDIMessage(status, 101, 127); // RPN MSB = 127 (null)
        sendMIDIMessage(status, 100, 127); // RPN LSB = 127 (null)
        
        if (logger) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Initialized channel %d with instrument %d (all effects off, pitch bend range: 24 semitones)", midiChannel, curveInstruments[i]);
            logger->log("MIDI", msg);
        }
    }
    
    return true;
}

void MIDIEngine::close() {
    std::lock_guard<std::mutex> l(mtx);
    
    if (!opened) return;
    
    if (logger) logger->log("MIDI", "Closing MIDI device");
    
    // Stop all playing notes
    allNotesOff();
    
    // Close platform-specific MIDI
    platform->close();
    
    opened = false;
    if (logger) logger->log("MIDI", "MIDI device closed");
}

void MIDIEngine::sendMIDIMessage(uint8_t status, uint8_t data1, uint8_t data2) {
    if (!opened) return;
    platform->sendMessage(status, data1, data2);
}

void MIDIEngine::sendProgramChange(int channel, int program) {
    if (channel < 0 || channel > 15) return;
    if (program < 0 || program > 127) return;
    
    uint8_t status = 0xC0 | (channel & 0x0F);  // Program Change
    sendMIDIMessage(status, program, 0);
}

void MIDIEngine::sendNoteOn(int channel, uint8_t note, uint8_t velocity) {
    if (channel < 0 || channel > 15) return;
    
    uint8_t status = 0x90 | (channel & 0x0F);  // Note On
    sendMIDIMessage(status, note, velocity);
}

void MIDIEngine::sendNoteOff(int channel, uint8_t note) {
    if (channel < 0 || channel > 15) return;
    
    uint8_t status = 0x80 | (channel & 0x0F);  // Note Off
    sendMIDIMessage(status, note, 0);
}

void MIDIEngine::sendPitchBend(int channel, int16_t bend) {
    if (channel < 0 || channel > 15) return;
    
    // Pitch bend is 14-bit: 0 to 16383, with 8192 as center
    uint16_t bendValue = static_cast<uint16_t>(bend + 8192);
    bendValue = std::clamp(bendValue, static_cast<uint16_t>(0), static_cast<uint16_t>(16383));
    
    uint8_t lsb = bendValue & 0x7F;         // Lower 7 bits
    uint8_t msb = (bendValue >> 7) & 0x7F;  // Upper 7 bits
    
    uint8_t status = 0xE0 | (channel & 0x0F);  // Pitch Bend
    sendMIDIMessage(status, lsb, msb);
}

void MIDIEngine::sendPan(int channel, uint8_t pan) {
    if (channel < 0 || channel > 15) return;
    
    uint8_t status = 0xB0 | (channel & 0x0F);  // Control Change
    sendMIDIMessage(status, 10, pan);  // CC 10 = Pan
}

void MIDIEngine::sendVolume(int channel, uint8_t volume) {
    if (channel < 0 || channel > 15) return;
    
    uint8_t status = 0xB0 | (channel & 0x0F);  // Control Change
    sendMIDIMessage(status, 7, volume);  // CC 7 = Volume
}

void MIDIEngine::frequencyToMIDI(double freqHz, uint8_t& outNote, int16_t& outBend) {
    // Task 1.9: Use centralized pitch mapping function
    PitchMapping::frequencyToMIDINote(freqHz, outNote, outBend);
    
    // Adjust bend for our 24-semitone range (the centralized function uses ±2 semitones)
    // Our hardware is configured for ±24 semitones via RPN
    // The centralized function returns bend in ±2 semitone range (±8192)
    // We need to scale to ±24 semitone range
    // Scale factor: (8192 / 2 semitones) / (8192 / 24 semitones) = 24/2 = 12
    // Actually, we need to scale DOWN since we have MORE range
    // 1 semitone in our system = 8192/24 = 341.33 bend units
    // But centralized function gives us bend for ±2 semitones (8192 per 2 semitones)
    // So we need to divide by 12 to get the right scaling
    int16_t adjustedBend = (outBend - 8192) / 12;  // Center at 0, scale down, then re-center would be: 8192 + adjustedBend
    // But actually let's recalculate from the note fraction directly
    
    // Get the fraction from the note
    double noteFloat = 69.0 + 12.0 * std::log2(freqHz / 440.0);
    noteFloat = std::clamp(noteFloat, 0.0, 127.0);
    double fraction = noteFloat - std::floor(noteFloat);
    
    // Convert fraction to pitch bend for 24-semitone range
    // fraction (0-1 semitone) scaled to pitch bend units
    // 1 semitone = 8192/24 = 341.33 bend units
    outBend = static_cast<int16_t>(fraction * (8192.0 / PITCH_BEND_SEMITONES));
}

void MIDIEngine::frequencyToPitchBend(double freqHz, int16_t& outBend) {
    // Convert the reference note to its frequency
    // Formula: freq = 440 * 2^((note - 69) / 12)
    constexpr double A4_FREQ = 440.0;
    constexpr int A4_NOTE = 69;
    double refFreqHz = A4_FREQ * std::pow(2.0, (referenceNote - A4_NOTE) / 12.0);
    
    // Calculate the semitone difference from reference
    // Formula: semitones = 12 * log2(freq / refFreq)
    double semitonesDiff = 12.0 * std::log2(freqHz / refFreqHz);
    
    // Clamp to the pitch bend range (±24 semitones)
    semitonesDiff = std::clamp(semitonesDiff, -static_cast<double>(PITCH_BEND_SEMITONES), 
                                               static_cast<double>(PITCH_BEND_SEMITONES));
    
    // Convert semitone difference to pitch bend value
    // Full bend range ±8192 corresponds to ±24 semitones
    // So 1 semitone = 8192/24 = 341.33 bend units
    outBend = static_cast<int16_t>(semitonesDiff * (8192.0 / PITCH_BEND_SEMITONES));
    
    // Safety clamp to ensure we're within valid MIDI pitch bend range
    // This handles potential rounding edge cases (e.g., getting 8192 instead of 8191)
    outBend = std::clamp(outBend, static_cast<int16_t>(-8192), static_cast<int16_t>(8191));
}

void MIDIEngine::calculateReferenceNote() {
    // Calculate the middle frequency of the synth range (geometric mean for better pitch centering)
    double midFreqHz = std::sqrt(synthMinFreqHz * synthMaxFreqHz);
    
    // Convert to MIDI note number
    // Formula: note = 69 + 12 * log2(freq / 440)
    constexpr double A4_FREQ = 440.0;
    constexpr int A4_NOTE = 69;
    double noteFloat = A4_NOTE + 12.0 * std::log2(midFreqHz / A4_FREQ);
    
    // Round to nearest integer note
    referenceNote = static_cast<uint8_t>(std::clamp(std::round(noteFloat), 0.0, 127.0));
    
    if (logger) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Reference note calculated: MIDI note %d (from synth range %d-%d Hz, mid=%.1f Hz)", 
                 referenceNote, synthMinFreqHz, synthMaxFreqHz, midFreqHz);
        logger->log("MIDI", msg);
    }
}

void MIDIEngine::setSynthFrequencyRange(int minHz, int maxHz) {
    std::lock_guard<std::mutex> l(mtx);
    
    synthMinFreqHz = minHz;
    synthMaxFreqHz = maxHz;
    
    // Recalculate reference note based on new range
    calculateReferenceNote();
    
    if (logger) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Synth frequency range set to %d-%d Hz", minHz, maxHz);
        logger->log("MIDI", msg);
    }
}

void MIDIEngine::allNotesOff() {
    for (int i = 0; i < NUM_CURVES; i++) {
        if (channelNotes[i].active) {
            int midiChannel = (i < 4) ? i : (i + 1);
            sendNoteOff(midiChannel, channelNotes[i].note);
            channelNotes[i].active = false;
        }
    }
}

void MIDIEngine::stopCurveNote(int curveIndex) {
    if (curveIndex < 0 || curveIndex >= NUM_CURVES) return;
    
    if (channelNotes[curveIndex].active) {
        int midiChannel = (curveIndex < 4) ? curveIndex : (curveIndex + 1);
        sendNoteOff(midiChannel, channelNotes[curveIndex].note);
        channelNotes[curveIndex].active = false;
    }
}

void MIDIEngine::generateRulerAudio(
    std::vector<int16_t>& buffer,
    int samples,
    double pitchHz,
    double panFraction,
    int volumePercent,
    int waveformIndex)
{
    if (!opened) return;
    
    // Use dedicated ruler channel (channel 6) to avoid conflicts with curve channels 0,1,2,3,5
    const int rulerMidiChannel = 6;
    
    // Convert volume percent to MIDI base volume (0-127)
    uint8_t baseVolume = static_cast<uint8_t>(
        std::clamp(volumePercent * 127.0 / 100.0, 0.0, 127.0)
    );
    
    // Calculate interpolated pan and volume using the same model as curve sounds
    uint8_t pan, volume;
    calculateInterpolatedPanVolume(panFraction, baseVolume, pan, volume);
    
    // Update pan and volume for ruler channel
    sendPan(rulerMidiChannel, pan);
    sendVolume(rulerMidiChannel, volume);
    
    // Set instrument for ruler (use waveformIndex to determine instrument)
    // Map waveform to appropriate MIDI instrument
    int rulerInstrument;
    if (waveformIndex == -1) {
        // Custom sound mode: use custom ruler instruments based on playback mode
        rulerInstrument = glidingMode ? rulerCustomGlidingInstrument : rulerCustomDottedInstrument;
    } else if (waveformIndex >= 0 && waveformIndex < NUM_CURVES) {
        // Follow curve mode: use the instrument configured for the specified curve
        rulerInstrument = curveInstruments[waveformIndex];
    } else {
        // Fallback: use default vibraphone
        rulerInstrument = 11;
    }
    sendProgramChange(rulerMidiChannel, rulerInstrument);
    
    // Use pitch bend approach (similar to curve playback in gliding mode)
    // Play reference note once, then use only pitch bend to change pitch
    // This creates smooth transitions between ruler blips
    
    if (!rulerNote.active) {
        // First blip: Start the reference note with calculated volume as velocity
        sendNoteOn(rulerMidiChannel, referenceNote, volume);
        rulerNote.active = true;
        rulerNote.note = referenceNote;
        rulerNote.velocity = volume;
        
        if (logger) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Y-Axis Ruler: Started reference note %d on channel %d with pitch bend", referenceNote, rulerMidiChannel);
            logger->log("MIDI", msg);
        }
    } else {
        // Subsequent blips: Update volume using expression controller (CC 11)
        // Expression allows dynamic volume changes without retriggering
        // Use the interpolated volume directly (already calculated by calculateInterpolatedPanVolume)
        // This ensures consistent volume modulation with the panning model
        uint8_t expression = volume;
        sendMIDIMessage(0xB0 | rulerMidiChannel, 11, expression);  // CC 11 = Expression
    }
    
    // Calculate pitch bend from the target frequency
    int16_t bend;
    frequencyToPitchBend(pitchHz, bend);
    
    // Update pitch bend for smooth pitch transitions
    sendPitchBend(rulerMidiChannel, bend);
    
    // Ensure buffer is properly sized (MIDI plays through OS, not directly in buffer)
    if (buffer.size() < static_cast<size_t>(samples * 2)) {
        buffer.resize(samples * 2, 0);
    }
}

void MIDIEngine::stopRulerNote() {
    if (rulerNote.active) {
        const int rulerMidiChannel = 6;
        sendNoteOff(rulerMidiChannel, rulerNote.note);
        rulerNote.active = false;
    }
}

void MIDIEngine::setRulerCustomInstruments(int glidingInstrument, int dottedInstrument) {
    std::lock_guard<std::mutex> l(mtx);
    rulerCustomGlidingInstrument = glidingInstrument;
    rulerCustomDottedInstrument = dottedInstrument;
    
    if (logger) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Set ruler custom instruments: gliding=%d, dotted=%d", 
                glidingInstrument, dottedInstrument);
        logger->log("MIDI", msg);
    }
}

void MIDIEngine::setXAxisRulerDrum(int drumNote) {
    std::lock_guard<std::mutex> l(mtx);
    // MIDI drums are on channel 9 (0-indexed), notes typically 35-81
    if (drumNote >= 35 && drumNote <= 81) {
        xAxisRulerDrum = drumNote;
        if (logger) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Set X-axis ruler drum: note=%d", drumNote);
            logger->log("MIDI", msg);
        }
    }
}

void MIDIEngine::generateXAxisRulerAudio(
    std::vector<int16_t>& buffer,
    int samples,
    double panFraction,
    int volumePercent)
{
    if (!opened) return;
    
    // Use MIDI channel 9 (drum channel, 0-indexed)
    const int drumChannel = 9;
    
    // Convert volume percent to MIDI base volume (0-127)
    uint8_t baseVolume = static_cast<uint8_t>(
        std::clamp(volumePercent * 127.0 / 100.0, 0.0, 127.0)
    );
    
    // Calculate interpolated pan and volume using the same model as curve sounds
    uint8_t pan, volume;
    calculateInterpolatedPanVolume(panFraction, baseVolume, pan, volume);
    
    // Update pan and volume for drum channel
    sendPan(drumChannel, pan);
    sendVolume(drumChannel, volume);
    
    // Stop previous X-axis ruler note if any (for protection against hanging notes)
    if (xAxisRulerNote.active) {
        sendNoteOff(drumChannel, xAxisRulerNote.note);
        xAxisRulerNote.active = false;
    }
    
    // Play drum sound with calculated volume as velocity
    sendNoteOn(drumChannel, static_cast<uint8_t>(xAxisRulerDrum), volume);
    xAxisRulerNote.active = true;
    xAxisRulerNote.note = static_cast<uint8_t>(xAxisRulerDrum);
    xAxisRulerNote.velocity = volume;
    
    // Ensure buffer is properly sized (MIDI plays through OS, not directly in buffer)
    if (buffer.size() < static_cast<size_t>(samples * 2)) {
        buffer.resize(samples * 2, 0);
    }
    
    // Note: For drum sounds, we'll send a note off after a short delay in the acoustic analyzer
    // or rely on the drum sound's natural decay
}

void MIDIEngine::generateAudio(
    std::vector<int16_t>& buffer,
    int samples,
    int curveIndex,
    double pitchHz,
    double panFraction,
    int volumePercent)
{
    if (!opened) return;
    if (curveIndex < 0 || curveIndex >= NUM_CURVES) return;
    
    // Map curve index to MIDI channel (skip channel 9 which is drums)
    int midiChannel = (curveIndex < 4) ? curveIndex : (curveIndex + 1);
    
    // Convert volume percent to MIDI volume (0-127)
    uint8_t baseVolume = static_cast<uint8_t>(
        std::clamp(volumePercent * 127.0 / 100.0, 0.0, 127.0)
    );
    
    // Calculate interpolated pan and volume
    uint8_t pan, volume;
    calculateInterpolatedPanVolume(panFraction, baseVolume, pan, volume);
    
    // Apply calculated values
    sendPan(midiChannel, pan);
    sendVolume(midiChannel, volume);
    
    // Check if we need to change the note
    NoteState& noteState = channelNotes[curveIndex];
    
    // Retriggering logic depends on playback mode:
    // - Gliding mode: Use pitch-bend-only approach - play reference note once, then only use pitch bend
    // - Dotted mode: Always retrigger (for percussive instruments like vibraphone, bells)
    
    if (glidingMode) {
        // **GLIDING MODE: Pitch-bend-only approach**
        // Play the reference note only once when first activated
        // Then use only pitch bend to change pitch (no retriggering)
        
        if (!noteState.active) {
            // First time activating this curve - play the reference note
            // In gliding mode, we use a fixed velocity and control volume via CC7
            // This ensures consistent attack and allows smooth volume modulation
            sendNoteOn(midiChannel, referenceNote, GLIDING_MODE_VELOCITY);
            noteState.active = true;
            noteState.note = referenceNote;
            noteState.velocity = GLIDING_MODE_VELOCITY;
            
            if (logger) {
                char msg[512];
                snprintf(msg, sizeof(msg), "Gliding mode: Started reference note %d on channel %d", referenceNote, midiChannel);
                logger->log("MIDI", msg);
            }
        }
        
        // Calculate pitch bend from the target frequency
        int16_t bend;
        frequencyToPitchBend(pitchHz, bend);
        
        // Update pitch bend for smooth pitch transitions
        // This is the ONLY way we change pitch in gliding mode
        sendPitchBend(midiChannel, bend);
        
    } else {
        // **DOTTED MODE: Traditional note-based approach**
        // Always retrigger for percussive character
        // This gives clear articulation for each data point
        
        // Convert frequency to MIDI note and pitch bend using traditional method
        uint8_t note;
        int16_t bend;
        frequencyToMIDI(pitchHz, note, bend);
        
        // Stop previous note if any
        if (noteState.active) {
            sendNoteOff(midiChannel, noteState.note);
        }
        
        // Start new note
        // In dotted mode, we use the modulated volume as velocity for dynamic attack
        // This creates percussive articulation where both attack and sustain respond to volume
        sendNoteOn(midiChannel, note, volume);
        noteState.active = true;
        noteState.note = note;
        noteState.velocity = volume;
        
        // Update pitch bend for fine-tuning
        sendPitchBend(midiChannel, bend);
    }
    
    // Note: MIDI doesn't directly fill the buffer - it plays through the system MIDI synth
    // The buffer is used by the synthesizer engine. For MIDI, we just need to ensure
    // the buffer is zeroed or properly sized since mixing happens at the OS level
    if (buffer.size() < static_cast<size_t>(samples * 2)) {
        buffer.resize(samples * 2, 0);
    }
}

void MIDIEngine::setCurveInstrument(int curveIndex, int program) {
    if (curveIndex < 0 || curveIndex >= NUM_CURVES) return;
    if (program < 0 || program > 127) return;
    
    std::lock_guard<std::mutex> l(mtx);
    curveInstruments[curveIndex] = program;
    
    if (logger) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Set curve %d to instrument %d", curveIndex, program);
        logger->log("MIDI", msg);
    }
    
    // If MIDI is open, update the instrument immediately
    if (opened) {
        int midiChannel = (curveIndex < 4) ? curveIndex : (curveIndex + 1);
        sendProgramChange(midiChannel, program);
    }
}

int MIDIEngine::getCurveInstrument(int curveIndex) const {
    if (curveIndex >= 0 && curveIndex < NUM_CURVES) {
        return curveInstruments[curveIndex];
    }
    return 0;
}

void MIDIEngine::playPreview(int program, int durationMs) {
    if (!opened) return;
    
    // Use channel 0 for preview
    const int previewChannel = 0;
    const uint8_t previewNote = 60;  // Middle C
    const uint8_t previewVelocity = 100;
    
    // Set instrument
    sendProgramChange(previewChannel, program);
    
    // Play note
    sendNoteOn(previewChannel, previewNote, previewVelocity);
    
    // Wait for duration
    std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
    
    // Stop note
    sendNoteOff(previewChannel, previewNote);
}

void MIDIEngine::setGlidingMode(bool gliding) {
    std::lock_guard<std::mutex> l(mtx);
    
    // If mode is changing, reset to prevent hanging notes
    if (glidingMode != gliding) {
        if (logger) {
            logger->log("MIDI", gliding ? "Switching to GLIDING mode (no retriggering)" : "Switching to DOTTED mode (with retriggering)");
        }
        // Stop all active notes before mode switch
        allNotesOff();
        glidingMode = gliding;
    }
}

void MIDIEngine::reset() {
    std::lock_guard<std::mutex> l(mtx);
    
    // Stop all notes
    allNotesOff();
    
    // Reset note states
    for (int i = 0; i < NUM_CURVES; i++) {
        channelNotes[i].active = false;
        channelNotes[i].note = 0;
        channelNotes[i].velocity = 0;
    }
    
    // Reinitialize channels if MIDI is open
    if (opened) {
        // Send all notes off and reset all controllers
        for (int i = 0; i < NUM_CURVES; i++) {
            int midiChannel = (i < 4) ? i : (i + 1);
            
            // CC 121: Reset All Controllers
            uint8_t status = 0xB0 | (midiChannel & 0x0F);
            sendMIDIMessage(status, 121, 0);
            
            // CC 123: All Notes Off
            sendMIDIMessage(status, 123, 0);
            
            // Reinitialize instrument
            sendProgramChange(midiChannel, curveInstruments[i]);
            
            // Reset pan to center
            sendPan(midiChannel, 64);
            
            // Reset volume
            sendVolume(midiChannel, 100);
            
            // Disable all audio effects for clean, precise measurement signals
            sendMIDIMessage(status, 1, 0);    // CC 1: Modulation Wheel = 0 (off)
            sendMIDIMessage(status, 76, 0);   // CC 76: Vibrato Rate = 0 (off)
            sendMIDIMessage(status, 77, 0);   // CC 77: Vibrato Depth = 0 (off)
            sendMIDIMessage(status, 78, 0);   // CC 78: Vibrato Delay = 0 (off)
            sendMIDIMessage(status, 91, 0);   // CC 91: Reverb Send Level = 0 (no reverb)
            sendMIDIMessage(status, 93, 0);   // CC 93: Chorus Send Level = 0 (no chorus)
            sendMIDIMessage(status, 94, 0);   // CC 94: Detune/Celeste = 0 (no detune)
            sendMIDIMessage(status, 5, 0);    // CC 5: Portamento Time = 0 (instant)
            sendMIDIMessage(status, 65, 0);   // CC 65: Portamento Off
            sendMIDIMessage(status, 84, 0);   // CC 84: Portamento Control = 0
            
            // Set pitch bend range to 24 semitones (2 octaves)
            sendMIDIMessage(status, 101, 0);  // RPN MSB = 0
            sendMIDIMessage(status, 100, 0);  // RPN LSB = 0 (Pitch Bend Sensitivity)
            sendMIDIMessage(status, 6, PITCH_BEND_SEMITONES);   // Data Entry MSB = 24 semitones
            sendMIDIMessage(status, 38, 0);   // Data Entry LSB = 0 cents
            sendMIDIMessage(status, 101, 127); // RPN MSB = 127 (null)
            sendMIDIMessage(status, 100, 127); // RPN LSB = 127 (null)
            
            // Reset pitch bend to center
            sendPitchBend(midiChannel, 0);
        }
    }
}

void MIDIEngine::calculateInterpolatedPanVolume(
    double panFraction, 
    uint8_t baseVolume,
    uint8_t& outPan, 
    uint8_t& outVolume)
{
    if (!interpolatedPanMode) {
        // Standard mode: direct pan conversion
        outPan = static_cast<uint8_t>(std::clamp(panFraction * 127.0, 0.0, 127.0));
        outVolume = baseVolume;
        return;
    }
    
    // Interpolated mode: calculate fractional pan position
    double panFloat = panFraction * 127.0;
    uint8_t panLow = static_cast<uint8_t>(std::floor(panFloat));
    double fraction = panFloat - panLow;
    
    // Handle edge case: if panLow is already at maximum (127), no interpolation needed
    if (panLow >= 127) {
        outPan = 127;
        outVolume = baseVolume;
        return;
    }
    
    // Choose nearest discrete pan position
    outPan = (fraction < 0.5) ? panLow : (panLow + 1);
    
    // Adjust volume based on distance to chosen pan position
    double volumeModulation = 1.0;
    if (fraction < 0.5) {
        // Using lower pan, modulate based on distance to higher pan
        volumeModulation = 1.0 - (fraction * interpolationStrength);
    } else {
        // Using higher pan, modulate based on distance from lower pan
        volumeModulation = 1.0 - ((1.0 - fraction) * interpolationStrength);
    }
    
    outVolume = static_cast<uint8_t>(
        std::clamp(baseVolume * volumeModulation, 0.0, 127.0)
    );
}

void MIDIEngine::setInterpolatedPanMode(bool enable) {
    std::lock_guard<std::mutex> l(mtx);
    interpolatedPanMode = enable;
    
    if (logger) {
        logger->log("MIDI", enable ? 
            "Interpolated pan mode ENABLED" : 
            "Interpolated pan mode DISABLED");
    }
}

void MIDIEngine::setInterpolationStrength(double strength) {
    std::lock_guard<std::mutex> l(mtx);
    interpolationStrength = std::clamp(strength, 0.0, 1.0);
    
    if (logger) {
        char msg[256];
        snprintf(msg, sizeof(msg), 
            "Pan interpolation strength set to %.2f", interpolationStrength);
        logger->log("MIDI", msg);
    }
}

void MIDIEngine::setLogger(Logger* logger) {
    this->logger = logger;
    
    // Sanity check: verify platform was created
    if (!platform) {
        if (logger) {
            logger->log("MIDI", "CRITICAL ERROR: Platform implementation is NULL! Factory function failed.");
        }
        return;
    }
    
    // Log platform information when logger is set
    if (logger) {
        std::string msg = "MIDI Platform: ";
        msg += platform->getPlatformName();
        logger->log("MIDI", msg);
    }
}


void MIDIEngine::applyReactanceEffects(double reactanceX, bool isSmoothMode) {
    std::lock_guard<std::mutex> l(mtx);
    
    if (!opened) return;
    
    // Load configuration from global config
    // This will be updated to read from the config system
    extern AppConfig cfg;  // Access global configuration
    
    // Get the appropriate configuration based on mode
    int capacitiveCC = isSmoothMode ? cfg.reactance_smooth_capacitive_cc : cfg.reactance_dotted_capacitive_cc;
    int inductiveCC = isSmoothMode ? cfg.reactance_smooth_inductive_cc : cfg.reactance_dotted_inductive_cc;
    bool deadzoneEnabled = isSmoothMode ? cfg.reactance_smooth_deadzone_enabled : cfg.reactance_dotted_deadzone_enabled;
    double deadzoneSize = isSmoothMode ? cfg.reactance_smooth_deadzone_size : cfg.reactance_dotted_deadzone_size;
    int mappingFunc = isSmoothMode ? cfg.reactance_smooth_mapping_function : cfg.reactance_dotted_mapping_function;
    
    // Build ModeConfig for calculation
    ReactanceEffects::ModeConfig config;
    config.capacitiveCC = ReactanceEffects::Config::getCCParameterFromNumber(capacitiveCC);
    config.inductiveCC = ReactanceEffects::Config::getCCParameterFromNumber(inductiveCC);
    config.deadzoneEnabled = deadzoneEnabled;
    config.deadzoneSize = deadzoneSize;
    config.mappingFunc = static_cast<ReactanceEffects::MappingFunction>(mappingFunc);
    
    // Determine which effect to apply based on reactance sign
    bool isCapacitive = (reactanceX < 0.0);
    bool isInductive = (reactanceX > 0.0);
    
    // Calculate CC values
    int capacitiveCCValue = 0;
    int inductiveCCValue = 0;
    
    if (isCapacitive && capacitiveCC != 0) {
        capacitiveCCValue = ReactanceEffects::calculateCCValue(reactanceX, true, config);
    }
    if (isInductive && inductiveCC != 0) {
        inductiveCCValue = ReactanceEffects::calculateCCValue(reactanceX, false, config);
    }
    
    // Apply CC to Reactance channel (channel 3)
    constexpr int REACTANCE_CHANNEL = 3;
    uint8_t status = 0xB0 | (REACTANCE_CHANNEL & 0x0F);  // Control Change
    
    // Send capacitive effect
    if (capacitiveCC != 0) {
        sendMIDIMessage(status, static_cast<uint8_t>(capacitiveCC), static_cast<uint8_t>(capacitiveCCValue));
    }
    
    // Send inductive effect
    if (inductiveCC != 0) {
        sendMIDIMessage(status, static_cast<uint8_t>(inductiveCC), static_cast<uint8_t>(inductiveCCValue));
    }
}

void MIDIEngine::resetReactanceEffects() {
    std::lock_guard<std::mutex> l(mtx);
    
    if (!opened) return;
    
    // Load configuration to know which CCs to reset
    extern AppConfig cfg;
    
    constexpr int REACTANCE_CHANNEL = 3;
    uint8_t status = 0xB0 | (REACTANCE_CHANNEL & 0x0F);  // Control Change
    
    // Reset all possible reactance effect CCs to 0
    // Check both dotted and smooth configurations
    int ccToReset[] = {
        cfg.reactance_dotted_capacitive_cc,
        cfg.reactance_dotted_inductive_cc,
        cfg.reactance_smooth_capacitive_cc,
        cfg.reactance_smooth_inductive_cc
    };
    
    // Remove duplicates and send reset
    std::set<int> uniqueCCs;
    for (int cc : ccToReset) {
        if (cc != 0 && uniqueCCs.find(cc) == uniqueCCs.end()) {
            uniqueCCs.insert(cc);
            sendMIDIMessage(status, static_cast<uint8_t>(cc), 0);
        }
    }
}
