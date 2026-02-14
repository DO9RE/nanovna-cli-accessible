#include "midi_file_writer.h"
#include <filesystem>
#include <cstring>
#include <algorithm>

MIDIFileWriter::MIDIFileWriter()
    : trackStarted(false)
{
}

MIDIFileWriter::~MIDIFileWriter() {
    close();
}

bool MIDIFileWriter::createTempFile(const std::string& baseFilename) {
    try {
        // Get system temp directory
        std::filesystem::path tempDir = std::filesystem::temp_directory_path();
        
        // Create filename with .mid extension
        std::string filename = baseFilename + ".mid";
        std::filesystem::path tempFilePath = tempDir / filename;
        
        // Store the full path
        filePath = tempFilePath.string();
        
        // Open file for binary writing
        file.open(filePath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            setError("Failed to create MIDI file: " + filePath);
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception creating MIDI file: ") + e.what());
        return false;
    }
}

bool MIDIFileWriter::createFile(const std::string& fullPath) {
    try {
        // Store the full path
        filePath = fullPath;
        
        // Open file for binary writing
        file.open(filePath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            setError("Failed to create MIDI file: " + filePath);
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception creating MIDI file: ") + e.what());
        return false;
    }
}

bool MIDIFileWriter::writeHeader(uint16_t numTracks) {
    if (!file.is_open()) {
        setError("File not open");
        return false;
    }
    
    try {
        // Write "MThd" chunk type
        file.write("MThd", 4);
        
        // Write chunk length (6 bytes)
        writeBE32(6);
        
        // Write format (0 = single track, 1 = multi-track)
        uint16_t format = (numTracks > 1) ? 1 : 0;
        writeBE16(format);
        
        // Write number of tracks
        writeBE16(numTracks);
        
        // Write division (480 ticks per quarter note)
        writeBE16(TICKS_PER_QUARTER_NOTE);
        
        if (!file.good()) {
            setError("Failed to write MIDI header");
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception writing header: ") + e.what());
        return false;
    }
}

bool MIDIFileWriter::beginTrack() {
    if (!file.is_open()) {
        setError("File not open");
        return false;
    }
    
    try {
        // Write "MTrk" chunk type
        file.write("MTrk", 4);
        
        // Remember position to write track length later
        trackLengthPos = file.tellp();
        
        // Write placeholder for track length
        writeBE32(0);
        
        // Clear track data buffer
        trackData.clear();
        trackStarted = true;
        
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception beginning track: ") + e.what());
        return false;
    }
}

bool MIDIFileWriter::writeProgramChange(uint32_t deltaTime, uint8_t channel, uint8_t program) {
    if (!trackStarted) {
        setError("Track not started");
        return false;
    }
    
    if (channel > 15 || program > 127) {
        setError("Invalid Program Change parameters");
        return false;
    }
    
    try {
        writeVLQ(deltaTime);
        uint8_t status = 0xC0 | (channel & 0x0F);
        trackData.push_back(status);
        trackData.push_back(program);
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception writing Program Change: ") + e.what());
        return false;
    }
}

bool MIDIFileWriter::writeNoteOn(uint32_t deltaTime, uint8_t channel, uint8_t note, uint8_t velocity) {
    if (!trackStarted) {
        setError("Track not started");
        return false;
    }
    
    if (channel > 15 || note > 127 || velocity > 127) {
        setError("Invalid Note On parameters");
        return false;
    }
    
    try {
        writeVLQ(deltaTime);
        uint8_t status = 0x90 | (channel & 0x0F);
        trackData.push_back(status);
        trackData.push_back(note);
        trackData.push_back(velocity);
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception writing Note On: ") + e.what());
        return false;
    }
}

bool MIDIFileWriter::writeNoteOff(uint32_t deltaTime, uint8_t channel, uint8_t note) {
    if (!trackStarted) {
        setError("Track not started");
        return false;
    }
    
    if (channel > 15 || note > 127) {
        setError("Invalid Note Off parameters");
        return false;
    }
    
    try {
        writeVLQ(deltaTime);
        uint8_t status = 0x80 | (channel & 0x0F);
        trackData.push_back(status);
        trackData.push_back(note);
        trackData.push_back(0);  // Velocity = 0
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception writing Note Off: ") + e.what());
        return false;
    }
}

bool MIDIFileWriter::writePitchBend(uint32_t deltaTime, uint8_t channel, int16_t bend) {
    if (!trackStarted) {
        setError("Track not started");
        return false;
    }
    
    if (channel > 15) {
        setError("Invalid Pitch Bend channel");
        return false;
    }
    
    try {
        writeVLQ(deltaTime);
        
        // Convert bend value to 14-bit unsigned (0-16383, center at 8192)
        uint16_t bendValue = static_cast<uint16_t>(bend + 8192);
        bendValue = std::clamp(bendValue, static_cast<uint16_t>(0), static_cast<uint16_t>(16383));
        
        uint8_t lsb = bendValue & 0x7F;
        uint8_t msb = (bendValue >> 7) & 0x7F;
        
        uint8_t status = 0xE0 | (channel & 0x0F);
        trackData.push_back(status);
        trackData.push_back(lsb);
        trackData.push_back(msb);
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception writing Pitch Bend: ") + e.what());
        return false;
    }
}

bool MIDIFileWriter::writeControlChange(uint32_t deltaTime, uint8_t channel, uint8_t controller, uint8_t value) {
    if (!trackStarted) {
        setError("Track not started");
        return false;
    }
    
    if (channel > 15 || controller > 127 || value > 127) {
        setError("Invalid Control Change parameters");
        return false;
    }
    
    try {
        writeVLQ(deltaTime);
        uint8_t status = 0xB0 | (channel & 0x0F);
        trackData.push_back(status);
        trackData.push_back(controller);
        trackData.push_back(value);
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception writing Control Change: ") + e.what());
        return false;
    }
}

bool MIDIFileWriter::writeTempo(uint32_t deltaTime, uint32_t microsecondsPerQuarter) {
    if (!trackStarted) {
        setError("Track not started");
        return false;
    }
    
    try {
        writeVLQ(deltaTime);
        trackData.push_back(0xFF);  // Meta event
        trackData.push_back(0x51);  // Tempo
        trackData.push_back(0x03);  // Length = 3 bytes
        trackData.push_back((microsecondsPerQuarter >> 16) & 0xFF);
        trackData.push_back((microsecondsPerQuarter >> 8) & 0xFF);
        trackData.push_back(microsecondsPerQuarter & 0xFF);
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception writing Tempo: ") + e.what());
        return false;
    }
}

bool MIDIFileWriter::writeTrackName(uint32_t deltaTime, const std::string& name) {
    if (!trackStarted) {
        setError("Track not started");
        return false;
    }
    
    try {
        writeVLQ(deltaTime);
        trackData.push_back(0xFF);  // Meta event
        trackData.push_back(0x03);  // Track Name
        writeVLQ(static_cast<uint32_t>(name.size()));
        for (char c : name) {
            trackData.push_back(static_cast<uint8_t>(c));
        }
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception writing Track Name: ") + e.what());
        return false;
    }
}

bool MIDIFileWriter::endTrack() {
    if (!trackStarted) {
        setError("Track not started");
        return false;
    }
    
    try {
        // Write End of Track meta event
        // Delta time = 0, Meta event = 0xFF, Type = 0x2F, Length = 0
        writeVLQ(0);
        trackData.push_back(0xFF);  // Meta event
        trackData.push_back(0x2F);  // End of Track
        trackData.push_back(0x00);  // Length = 0
        
        // Write track data to file
        file.write(reinterpret_cast<const char*>(trackData.data()), trackData.size());
        
        // Update track length
        std::streampos currentPos = file.tellp();
        uint32_t trackLength = static_cast<uint32_t>(trackData.size());
        
        file.seekp(trackLengthPos);
        writeBE32(trackLength);
        file.seekp(currentPos);
        
        if (!file.good()) {
            setError("Failed to write track data");
            return false;
        }
        
        trackStarted = false;
        return true;
    } catch (const std::exception& e) {
        setError(std::string("Exception ending track: ") + e.what());
        return false;
    }
}

void MIDIFileWriter::close() {
    if (file.is_open()) {
        file.close();
    }
}

uint32_t MIDIFileWriter::secondsToTicks(double seconds, uint32_t tempo) {
    // tempo is in microseconds per quarter note
    // ticks per quarter note is 480
    // ticks per second = (1000000 / tempo) * 480
    double ticksPerSecond = (1000000.0 / tempo) * TICKS_PER_QUARTER_NOTE;
    return static_cast<uint32_t>(seconds * ticksPerSecond);
}

void MIDIFileWriter::writeBE32(uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (value >> 24) & 0xFF;
    bytes[1] = (value >> 16) & 0xFF;
    bytes[2] = (value >> 8) & 0xFF;
    bytes[3] = value & 0xFF;
    file.write(reinterpret_cast<const char*>(bytes), 4);
}

void MIDIFileWriter::writeBE16(uint16_t value) {
    uint8_t bytes[2];
    bytes[0] = (value >> 8) & 0xFF;
    bytes[1] = value & 0xFF;
    file.write(reinterpret_cast<const char*>(bytes), 2);
}

void MIDIFileWriter::writeVLQ(uint32_t value) {
    // Variable-length quantity encoding
    // Split value into 7-bit chunks
    uint8_t buffer[4];
    int numBytes = 0;
    
    buffer[0] = value & 0x7F;
    numBytes = 1;
    
    value >>= 7;
    while (value > 0) {
        buffer[numBytes] = (value & 0x7F) | 0x80;
        numBytes++;
        value >>= 7;
    }
    
    // Write bytes in reverse order (big-endian)
    for (int i = numBytes - 1; i >= 0; i--) {
        trackData.push_back(buffer[i]);
    }
}

void MIDIFileWriter::setError(const std::string& error) {
    lastError = error;
}
