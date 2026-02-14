#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>

/**
 * MIDI File Writer
 * 
 * Generates Standard MIDI File Format 0 or Format 1 files for offline rendering.
 * Supports the MIDI messages used by MIDIEngine for acoustic analysis.
 * 
 * Features:
 * - MIDI Format 0 (single track) or Format 1 (multi-track)
 * - 480 ticks per quarter note
 * - Support for Program Change, Note On/Off, Pitch Bend, Control Change
 * - Variable-length quantity (VLQ) encoding for delta times
 * - Temporary file generation in system temp directory
 */
class MIDIFileWriter {
public:
    MIDIFileWriter();
    ~MIDIFileWriter();
    
    /**
     * Create a new MIDI file in the system temp directory
     * @param baseFilename Base filename (without path or extension)
     * @return true on success, false on failure
     */
    bool createTempFile(const std::string& baseFilename);
    
    /**
     * Create a new MIDI file at the specified full path
     * @param fullPath Full path including filename and .mid extension
     * @return true on success, false on failure
     */
    bool createFile(const std::string& fullPath);
    
    /**
     * Get the full path of the created MIDI file
     * @return Full path to the MIDI file
     */
    std::string getFilePath() const { return filePath; }
    
    /**
     * Write MIDI header chunk
     * @param numTracks Number of tracks (1 = Format 0, >1 = Format 1)
     * @return true on success, false on failure
     */
    bool writeHeader(uint16_t numTracks = 1);
    
    /**
     * Begin a new track
     * @return true on success, false on failure
     */
    bool beginTrack();
    
    /**
     * Write a Program Change event
     * @param deltaTime Delta time in ticks since last event
     * @param channel MIDI channel (0-15)
     * @param program Program number (0-127)
     * @return true on success, false on failure
     */
    bool writeProgramChange(uint32_t deltaTime, uint8_t channel, uint8_t program);
    
    /**
     * Write a Note On event
     * @param deltaTime Delta time in ticks since last event
     * @param channel MIDI channel (0-15)
     * @param note Note number (0-127)
     * @param velocity Velocity (0-127)
     * @return true on success, false on failure
     */
    bool writeNoteOn(uint32_t deltaTime, uint8_t channel, uint8_t note, uint8_t velocity);
    
    /**
     * Write a Note Off event
     * @param deltaTime Delta time in ticks since last event
     * @param channel MIDI channel (0-15)
     * @param note Note number (0-127)
     * @return true on success, false on failure
     */
    bool writeNoteOff(uint32_t deltaTime, uint8_t channel, uint8_t note);
    
    /**
     * Write a Pitch Bend event
     * @param deltaTime Delta time in ticks since last event
     * @param channel MIDI channel (0-15)
     * @param bend Pitch bend value (-8192 to +8191, 0 = center)
     * @return true on success, false on failure
     */
    bool writePitchBend(uint32_t deltaTime, uint8_t channel, int16_t bend);
    
    /**
     * Write a Control Change event
     * @param deltaTime Delta time in ticks since last event
     * @param channel MIDI channel (0-15)
     * @param controller Controller number (0-127)
     * @param value Controller value (0-127)
     * @return true on success, false on failure
     */
    bool writeControlChange(uint32_t deltaTime, uint8_t channel, uint8_t controller, uint8_t value);
    
    /**
     * Write a Tempo meta event
     * @param deltaTime Delta time in ticks since last event
     * @param microsecondsPerQuarter Tempo in microseconds per quarter note
     * @return true on success, false on failure
     */
    bool writeTempo(uint32_t deltaTime, uint32_t microsecondsPerQuarter);
    
    /**
     * Write a Track Name meta event
     * @param deltaTime Delta time in ticks since last event
     * @param name Track name string
     * @return true on success, false on failure
     */
    bool writeTrackName(uint32_t deltaTime, const std::string& name);
    
    /**
     * End the current track and close the file
     * Writes End of Track meta event and updates track length
     * @return true on success, false on failure
     */
    bool endTrack();
    
    /**
     * Close the file (called automatically by endTrack)
     */
    void close();
    
    /**
     * Get the last error message
     * @return Error message string
     */
    std::string getLastError() const { return lastError; }
    
    /**
     * Calculate delta time in ticks from seconds
     * @param seconds Time in seconds
     * @param tempo Tempo in microseconds per quarter note (default: 500000 = 120 BPM)
     * @return Delta time in ticks
     */
    static uint32_t secondsToTicks(double seconds, uint32_t tempo = 500000);
    
private:
    std::string filePath;
    std::string lastError;
    std::ofstream file;
    std::streampos trackLengthPos;  // Position to write track length
    std::vector<uint8_t> trackData;  // Buffer for track data
    bool trackStarted;
    
    static constexpr uint16_t TICKS_PER_QUARTER_NOTE = 480;
    static constexpr uint32_t DEFAULT_TEMPO_USPQN = 500000;  // 120 BPM
    
    /**
     * Write a 32-bit big-endian value
     */
    void writeBE32(uint32_t value);
    
    /**
     * Write a 16-bit big-endian value
     */
    void writeBE16(uint16_t value);
    
    /**
     * Write a variable-length quantity (VLQ)
     * Used for delta times in MIDI files
     */
    void writeVLQ(uint32_t value);
    
    /**
     * Write a MIDI message to the track buffer
     */
    void writeMIDIMessage(uint8_t status, uint8_t data1, uint8_t data2 = 0, bool hasData2 = true);
    
    /**
     * Set error message
     */
    void setError(const std::string& error);
};
