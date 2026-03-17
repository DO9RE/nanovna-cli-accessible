#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>

/**
 * @file tts_interface.h
 * @brief Platform-independent Text-to-Speech interface
 * 
 * This interface provides access to native TTS engines across platforms:
 * - Windows: SAPI (Speech API)
 * - macOS: NSSpeechSynthesizer
 * - Linux: espeak/speech-dispatcher
 */

/**
 * TTS speech rate enumeration
 */
enum class TTSRate {
    VERY_SLOW = -2,
    SLOW = -1,
    NORMAL = 0,
    FAST = 1,
    VERY_FAST = 2
};

/**
 * TTS status callback
 */
enum class TTSStatus {
    STARTED,      // Speech has started
    COMPLETED,    // Speech has completed
    CANCELLED,    // Speech was cancelled
    FAILED        // An error occurred (renamed from ERROR to avoid Windows macro conflict)
};

/**
 * TTS engine selection
 */
enum class TTSEngineType {
    WINDOWS_SAPI = 0,
    NVDA = 1,
    MACOS_SAY = 2,         // macOS 'say' command (always available)
    MACOS_VOICEOVER = 3,   // macOS VoiceOver screen reader (when running)
    ESPEAK_NG = 4          // espeak-NG (cross-platform, installable via Homebrew on macOS)
};

/**
 * Platform-independent TTS interface
 */
class ITTSEngine {
public:
    virtual ~ITTSEngine() = default;
    
    /**
     * Initialize the TTS engine
     * @return true on success, false on failure
     */
    virtual bool initialize() = 0;
    
    /**
     * Shutdown the TTS engine
     */
    virtual void shutdown() = 0;
    
    /**
     * Check if TTS engine is available
     * @return true if TTS is available and initialized
     */
    virtual bool isAvailable() const = 0;
    
    /**
     * Speak text (asynchronous)
     * @param text Text to speak
     * @param interrupt If true, stop current speech before speaking
     * @return true if speech was queued successfully
     */
    virtual bool speak(const std::string& text, bool interrupt = false) = 0;
    
    /**
     * Speak text (synchronous - blocks until completion)
     * @param text Text to speak
     * @return true if speech completed successfully
     */
    virtual bool speakSync(const std::string& text) = 0;
    
    /**
     * Stop current speech
     */
    virtual void stop() = 0;
    
    /**
     * Check if currently speaking
     * @return true if speech is in progress
     */
    virtual bool isSpeaking() const = 0;
    
    /**
     * Set speech rate
     * @param rate Speech rate (-2 to +2)
     */
    virtual void setRate(TTSRate rate) = 0;
    
    /**
     * Set speech volume
     * @param volume Volume (0-100)
     */
    virtual void setVolume(int volume) = 0;
    
    /**
     * Set language/locale
     * @param languageCode Language code (e.g., "en-US", "de-DE")
     * @return true if language was set successfully
     */
    virtual bool setLanguage(const std::string& languageCode) = 0;
    
    /**
     * Get list of available voices
     * @return Vector of voice names
     */
    virtual std::vector<std::string> getAvailableVoices() const = 0;
    
    /**
     * Set voice by name
     * @param voiceName Name of voice to use
     * @return true if voice was set successfully
     */
    virtual bool setVoice(const std::string& voiceName) = 0;
    
    /**
     * Set status callback
     * @param callback Function to call on status changes
     */
    virtual void setStatusCallback(std::function<void(TTSStatus)> callback) = 0;
};

/**
 * Factory function to create platform-specific TTS engine
 * @return Unique pointer to TTS engine implementation
 */
std::unique_ptr<ITTSEngine> createTTSEngine(TTSEngineType type = TTSEngineType::WINDOWS_SAPI);

/**
 * Check if NVDA screen reader process is currently running.
 * Returns false on non-Windows platforms.
 */
bool isNvdaScreenReaderRunning();

/**
 * Download the NVDA controller client DLL from the official NV Access server.
 * Tries multiple download strategies: URLDownloadToFile, curl, PowerShell, bitsadmin.
 * Places the DLL in a "lib/" subdirectory next to the executable.
 * @return true if DLL was downloaded and placed successfully
 */
bool downloadNvdaControllerClientDll();

/**
 * Open the NVDA controller client download page in the user's default browser.
 * Used as a fallback when automatic download fails.
 */
void openNvdaDllDownloadPage();

/**
 * Get the target directory path where the NVDA controller DLL should be placed.
 * @return Path to the "lib/" subdirectory next to the executable, or empty string on failure.
 */
std::string getNvdaDllTargetDirectory();

/**
 * Send text to the NVDA braille display.
 * On non-Windows platforms or when NVDA is not running, this is a no-op.
 * @param text Text to display on braille line
 * @return true if message was sent successfully
 */
bool sendNvdaBrailleMessage(const std::string& text);
