#ifdef _WIN32

#include "platform/wmf_midi_renderer.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <comdef.h>
#include <fstream>
#include <algorithm>
#include <sstream>

// Link with Media Foundation libraries
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

WMFMIDIRenderer::WMFMIDIRenderer()
    : mfInitialized(false)
{
}

WMFMIDIRenderer::~WMFMIDIRenderer() {
    shutdownMediaFoundation();
}

bool WMFMIDIRenderer::renderToWav(const std::string& midiFilePath, const std::string& wavFilePath) {
    // Initialize Media Foundation
    if (!initializeMediaFoundation()) {
        return false;
    }
    
    HRESULT hr = S_OK;
    IMFSourceReader* pSourceReader = nullptr;
    IMFMediaType* pMediaType = nullptr;
    std::vector<int16_t> pcmBuffer;
    
    try {
        // Convert MIDI file path to wide string
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, midiFilePath.c_str(), -1, nullptr, 0);
        if (wideSize == 0) {
            setError("Failed to convert MIDI file path to wide string");
            return false;
        }
        
        std::wstring wideMidiPath(wideSize - 1, 0);  // Exclude null terminator in string size
        MultiByteToWideChar(CP_UTF8, 0, midiFilePath.c_str(), -1, &wideMidiPath[0], wideSize);
        
        // Create source reader from MIDI file
        hr = MFCreateSourceReaderFromURL(wideMidiPath.c_str(), nullptr, &pSourceReader);
        if (FAILED(hr)) {
            setError("Failed to create source reader from MIDI file: " + hresultToString(hr));
            return false;
        }
        
        // Create PCM media type (44100 Hz, 16-bit, stereo)
        hr = MFCreateMediaType(&pMediaType);
        if (FAILED(hr)) {
            setError("Failed to create media type: " + hresultToString(hr));
            if (pSourceReader) pSourceReader->Release();
            return false;
        }
        
        // Set media type to PCM
        hr = pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) {
            hr = pMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        }
        if (SUCCEEDED(hr)) {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, SAMPLE_RATE);
        }
        if (SUCCEEDED(hr)) {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, CHANNELS);
        }
        if (SUCCEEDED(hr)) {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, BITS_PER_SAMPLE);
        }
        if (SUCCEEDED(hr)) {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, BLOCK_ALIGN);
        }
        if (SUCCEEDED(hr)) {
            hr = pMediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, SAMPLE_RATE * BLOCK_ALIGN);
        }
        
        if (FAILED(hr)) {
            setError("Failed to configure PCM media type: " + hresultToString(hr));
            pMediaType->Release();
            pSourceReader->Release();
            return false;
        }
        
        // Set the media type on the source reader
        hr = pSourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pMediaType);
        if (FAILED(hr)) {
            setError("Failed to set media type on source reader: " + hresultToString(hr));
            pMediaType->Release();
            pSourceReader->Release();
            return false;
        }
        
        pMediaType->Release();
        pMediaType = nullptr;
        
        // Read all audio samples
        bool readSuccess = true;
        while (true) {
            DWORD dwFlags = 0;
            LONGLONG llTimestamp = 0;
            IMFSample* pSample = nullptr;
            
            hr = pSourceReader->ReadSample(
                (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                0,
                nullptr,
                &dwFlags,
                &llTimestamp,
                &pSample
            );
            
            if (FAILED(hr)) {
                setError("Failed to read sample: " + hresultToString(hr));
                readSuccess = false;
                break;
            }
            
            // Check for end of stream
            if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
                break;
            }
            
            // Process sample if available
            if (pSample) {
                IMFMediaBuffer* pBuffer = nullptr;
                hr = pSample->ConvertToContiguousBuffer(&pBuffer);
                
                if (SUCCEEDED(hr)) {
                    BYTE* pAudioData = nullptr;
                    DWORD cbCurrentLength = 0;
                    
                    hr = pBuffer->Lock(&pAudioData, nullptr, &cbCurrentLength);
                    
                    if (SUCCEEDED(hr)) {
                        // Copy audio data to our buffer
                        int16_t* samples = reinterpret_cast<int16_t*>(pAudioData);
                        size_t numSamples = cbCurrentLength / sizeof(int16_t);
                        
                        pcmBuffer.insert(pcmBuffer.end(), samples, samples + numSamples);
                        
                        pBuffer->Unlock();
                    }
                    
                    pBuffer->Release();
                }
                
                pSample->Release();
            }
        }
        
        pSourceReader->Release();
        pSourceReader = nullptr;
        
        if (!readSuccess) {
            return false;
        }
        
        // Check if we got any audio data
        if (pcmBuffer.empty()) {
            setError("No audio data read from MIDI file");
            return false;
        }
        
        // Write WAV file
        if (!writeWavFile(wavFilePath, pcmBuffer)) {
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        setError(std::string("Exception during rendering: ") + e.what());
        if (pMediaType) pMediaType->Release();
        if (pSourceReader) pSourceReader->Release();
        return false;
    }
}

bool WMFMIDIRenderer::initializeMediaFoundation() {
    if (mfInitialized) {
        return true;
    }
    
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        setError("Failed to initialize Media Foundation: " + hresultToString(hr));
        return false;
    }
    
    mfInitialized = true;
    return true;
}

void WMFMIDIRenderer::shutdownMediaFoundation() {
    if (mfInitialized) {
        MFShutdown();
        mfInitialized = false;
    }
}

bool WMFMIDIRenderer::writeWavFile(const std::string& wavFilePath, const std::vector<int16_t>& samples) {
    try {
        std::ofstream wavFile(wavFilePath, std::ios::binary);
        if (!wavFile.is_open()) {
            setError("Failed to create WAV file: " + wavFilePath);
            return false;
        }
        
        uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
        uint32_t fileSize = 36 + dataSize;
        uint32_t byteRate = SAMPLE_RATE * BLOCK_ALIGN;
        
        // Write RIFF header
        wavFile.write("RIFF", 4);
        wavFile.write(reinterpret_cast<const char*>(&fileSize), 4);
        wavFile.write("WAVE", 4);
        
        // Write fmt chunk
        wavFile.write("fmt ", 4);
        uint32_t fmtSize = 16;
        wavFile.write(reinterpret_cast<const char*>(&fmtSize), 4);
        
        uint16_t audioFormat = 1;  // PCM
        wavFile.write(reinterpret_cast<const char*>(&audioFormat), 2);
        
        uint16_t numChannels = CHANNELS;
        wavFile.write(reinterpret_cast<const char*>(&numChannels), 2);
        
        uint32_t sampleRate = SAMPLE_RATE;
        wavFile.write(reinterpret_cast<const char*>(&sampleRate), 4);
        
        wavFile.write(reinterpret_cast<const char*>(&byteRate), 4);
        
        uint16_t blockAlign = BLOCK_ALIGN;
        wavFile.write(reinterpret_cast<const char*>(&blockAlign), 2);
        
        uint16_t bitsPerSample = BITS_PER_SAMPLE;
        wavFile.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
        
        // Write data chunk
        wavFile.write("data", 4);
        wavFile.write(reinterpret_cast<const char*>(&dataSize), 4);
        
        // Write audio samples
        wavFile.write(reinterpret_cast<const char*>(samples.data()), dataSize);
        
        wavFile.close();
        
        if (!wavFile.good()) {
            setError("Failed to write WAV file data");
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        setError(std::string("Exception writing WAV file: ") + e.what());
        return false;
    }
}

void WMFMIDIRenderer::setError(const std::string& error) {
    lastError = error;
}

std::string WMFMIDIRenderer::hresultToString(long hr) {
    _com_error err(hr);
    
#ifdef UNICODE
    // UNICODE build: ErrorMessage() returns const wchar_t*
    std::wstring wideMsg = err.ErrorMessage();
    
    // Convert wide string to narrow string
    int size = WideCharToMultiByte(CP_UTF8, 0, wideMsg.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        std::stringstream ss;
        ss << "0x" << std::hex << hr;
        return ss.str();
    }
    
    std::string narrowMsg(size - 1, 0);  // Exclude null terminator in string size
    WideCharToMultiByte(CP_UTF8, 0, wideMsg.c_str(), -1, &narrowMsg[0], size, nullptr, nullptr);
    
    return narrowMsg;
#else
    // Non-UNICODE build: ErrorMessage() returns const char*
    const char* msg = err.ErrorMessage();
    if (msg) {
        return std::string(msg);
    } else {
        std::stringstream ss;
        ss << "0x" << std::hex << hr;
        return ss.str();
    }
#endif
}

#endif // _WIN32
