#include "midi_engine.h"
#include <cmath>
#include <algorithm>

#if defined(_WIN32)
#pragma comment(lib, "winmm.lib")
#endif

static constexpr double A4_FREQ = 440.0;  // A4 = 440 Hz
static constexpr int A4_NOTE = 69;         // MIDI note number for A4

// Pitch bend range constant
// We always use 24 semitones (2 octaves) for maximum flexibility
// This range is set via RPN commands and applies to both gliding and dotted modes
static constexpr int PITCH_BEND_SEMITONES = 24; // 24 semitones (2 octaves)

// Velocity for gliding mode (fixed to avoid volume changes on note start)
// In gliding mode, volume is controlled via CC7 (Volume) for smooth modulation
// Note velocity is kept constant to ensure consistent attack
static constexpr uint8_t GLIDING_MODE_VELOCITY = 100;

MIDIEngine::MIDIEngine() 
#if defined(_WIN32)
    : hMidiOut(nullptr)
#endif
{
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
#if defined(_WIN32)
    std::lock_guard<std::mutex> l(mtx);
    
    if (opened) {
        if (logger) logger->log("MIDI", "Device already opened");
        return true;
    }
    
    // Open the default MIDI output device
    MMRESULT result = midiOutOpen(&hMidiOut, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        if (logger) {
            char errMsg[512];
            snprintf(errMsg, sizeof(errMsg), "midiOutOpen failed with error code: %d", result);
            logger->log("MIDI", errMsg);
        }
        return false;
    }
    
    if (logger) logger->log("MIDI", "MIDI device opened successfully");
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
        
        // Disable vibrato and modulation effects for clean waveforms
        uint8_t status = 0xB0 | (midiChannel & 0x0F);  // Control Change
        sendMIDIMessage(status, 1, 0);    // CC 1: Modulation Wheel = 0 (off)
        sendMIDIMessage(status, 76, 0);   // CC 76: Vibrato Rate = 0 (off)
        sendMIDIMessage(status, 77, 0);   // CC 77: Vibrato Depth = 0 (off)
        sendMIDIMessage(status, 78, 0);   // CC 78: Vibrato Delay = 0 (off)
        
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
            snprintf(msg, sizeof(msg), "Initialized channel %d with instrument %d (vibrato off, pitch bend range: 24 semitones)", midiChannel, curveInstruments[i]);
            logger->log("MIDI", msg);
        }
    }
    
    return true;
#else
    if (logger) logger->log("MIDI", "MIDI engine not supported on this platform");
    return false;  // MIDI engine only supported on Windows
#endif
}

void MIDIEngine::close() {
#if defined(_WIN32)
    std::lock_guard<std::mutex> l(mtx);
    
    if (!opened) return;
    
    if (logger) logger->log("MIDI", "Closing MIDI device");
    
    // Stop all playing notes
    allNotesOff();
    
    // Close MIDI device
    if (hMidiOut != nullptr) {
        midiOutReset(hMidiOut);
        midiOutClose(hMidiOut);
        hMidiOut = nullptr;
    }
    
    opened = false;
    if (logger) logger->log("MIDI", "MIDI device closed");
#endif
}

void MIDIEngine::sendMIDIMessage(uint8_t status, uint8_t data1, uint8_t data2) {
#if defined(_WIN32)
    if (!opened || hMidiOut == nullptr) return;
    
    DWORD message = status | (data1 << 8) | (data2 << 16);
    midiOutShortMsg(hMidiOut, message);
#endif
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
    // Convert frequency to MIDI note number with fractional part
    // Formula: note = 69 + 12 * log2(freq / 440)
    double noteFloat = A4_NOTE + 12.0 * std::log2(freqHz / A4_FREQ);
    
    // Clamp to valid MIDI range
    noteFloat = std::clamp(noteFloat, 0.0, 127.0);
    
    // Get integer note and fractional part
    outNote = static_cast<uint8_t>(std::floor(noteFloat));
    double fraction = noteFloat - outNote;
    
    // Convert fraction to pitch bend (-8192 to +8191)
    // NOTE: This method is used for DOTTED mode where notes are retriggered
    // The pitch bend range is always set to 24 semitones (hardware setting via RPN)
    // fraction (0-1 semitone) needs to be scaled to the pitch bend range
    // Full bend range ±8192 corresponds to ±24 semitones
    // So 1 semitone = 8192/24 = 341.33 bend units
    outBend = static_cast<int16_t>(fraction * (8192.0 / PITCH_BEND_SEMITONES));
}

void MIDIEngine::frequencyToPitchBend(double freqHz, int16_t& outBend) {
    // Convert the reference note to its frequency
    // Formula: freq = 440 * 2^((note - 69) / 12)
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
    
    // Convert volume percent to MIDI velocity (0-127)
    int velocity = static_cast<int>(std::clamp(volumePercent * 127.0 / 100.0, 0.0, 127.0));
    
    // Convert pan fraction to MIDI pan (0-127, 64 = center)
    uint8_t pan = static_cast<uint8_t>(std::clamp(panFraction * 127.0, 0.0, 127.0));
    
    // Update pan for ruler channel
    sendPan(rulerMidiChannel, pan);
    
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
        // First blip: Start the reference note
        sendNoteOn(rulerMidiChannel, referenceNote, velocity);
        rulerNote.active = true;
        rulerNote.note = referenceNote;
        rulerNote.velocity = velocity;
        
        if (logger) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Y-Axis Ruler: Started reference note %d on channel %d with pitch bend", referenceNote, rulerMidiChannel);
            logger->log("MIDI", msg);
        }
    } else {
        // Subsequent blips: Update volume using expression controller (CC 11)
        // Expression allows dynamic volume changes without retriggering
        uint8_t expression = static_cast<uint8_t>(std::clamp(velocity * 1.0, 0.0, 127.0));
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
    
    // Convert volume percent to MIDI velocity (0-127)
    int velocity = static_cast<int>(std::clamp(volumePercent * 127.0 / 100.0, 0.0, 127.0));
    
    // Convert pan fraction to MIDI pan (0-127, 64 = center)
    uint8_t pan = static_cast<uint8_t>(std::clamp(panFraction * 127.0, 0.0, 127.0));
    
    // Update pan for drum channel
    sendPan(drumChannel, pan);
    
    // Stop previous X-axis ruler note if any (for protection against hanging notes)
    if (xAxisRulerNote.active) {
        sendNoteOff(drumChannel, xAxisRulerNote.note);
        xAxisRulerNote.active = false;
    }
    
    // Play drum sound
    sendNoteOn(drumChannel, static_cast<uint8_t>(xAxisRulerDrum), static_cast<uint8_t>(velocity));
    xAxisRulerNote.active = true;
    xAxisRulerNote.note = static_cast<uint8_t>(xAxisRulerDrum);
    xAxisRulerNote.velocity = static_cast<uint8_t>(velocity);
    
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
#if defined(_WIN32)
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
    Sleep(durationMs);
    
    // Stop note
    sendNoteOff(previewChannel, previewNote);
#endif
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
#if defined(_WIN32)
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
            
            // Disable vibrato and modulation effects for clean waveforms
            sendMIDIMessage(status, 1, 0);    // CC 1: Modulation Wheel = 0 (off)
            sendMIDIMessage(status, 76, 0);   // CC 76: Vibrato Rate = 0 (off)
            sendMIDIMessage(status, 77, 0);   // CC 77: Vibrato Depth = 0 (off)
            sendMIDIMessage(status, 78, 0);   // CC 78: Vibrato Delay = 0 (off)
            
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
#endif
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
