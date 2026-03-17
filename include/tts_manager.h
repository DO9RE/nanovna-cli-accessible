#pragma once

#include "tts_interface.h"
#include "translation.h"
#include <memory>
#include <string>

/**
 * @file tts_manager.h
 * @brief High-level TTS manager with translation integration
 */

class TTSManager {
public:
    TTSManager(TranslationManager* translation = nullptr, TTSEngineType engineType = TTSEngineType::WINDOWS_SAPI);
    ~TTSManager();
    
    /**
     * Initialize TTS engine
     * @return true if TTS is available
     */
    bool initialize();
    
    /**
     * Check if TTS is available
     */
    bool isAvailable() const;
    
    /**
     * Speak text (async)
     * @param text Text to speak
     * @param interrupt If true, stop current speech
     */
    void speak(const std::string& text, bool interrupt = false);
    
    /**
     * Speak text (sync - blocks until complete)
     * @param text Text to speak
     */
    void speakSync(const std::string& text);
    
    /**
     * Speak translated string
     * @param key Translation key
     * @param fallback Fallback text if key not found
     * @param interrupt If true, stop current speech
     */
    void speakTranslated(const std::string& key, const std::string& fallback, bool interrupt = false);
    
    /**
     * Stop current speech
     */
    void stop();
    
    /**
     * Check if currently speaking
     */
    bool isSpeaking() const;
    
    /**
     * Set speech rate
     */
    void setRate(TTSRate rate);
    
    /**
     * Set volume (0-100)
     */
    void setVolume(int volume);
    
    /**
     * Switch TTS engine type
     * @return true if engine was switched successfully
     */
    bool setEngineType(TTSEngineType engineType);
    
    /**
     * Get current engine type
     */
    TTSEngineType getEngineType() const { return engineType; }
    
    /**
     * Update language based on current translation manager language
     */
    void updateLanguage();
    
    /**
     * Get underlying TTS engine
     */
    ITTSEngine* getEngine() { return engine.get(); }
    
private:
    std::unique_ptr<ITTSEngine> engine;
    TranslationManager* translation;
    TTSEngineType engineType;
};
