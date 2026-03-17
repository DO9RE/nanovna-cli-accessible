#include "tts_manager.h"

TTSManager::TTSManager(TranslationManager* trans, TTSEngineType type) 
    : translation(trans), engine(createTTSEngine(type)), engineType(type) {
}

TTSManager::~TTSManager() {
    if (engine) {
        engine->shutdown();
    }
}

bool TTSManager::initialize() {
    if (!engine) {
        return false;
    }
    
    bool success = engine->initialize();
    if (success) {
        updateLanguage();
    }
    
    return success;
}

bool TTSManager::isAvailable() const {
    return engine && engine->isAvailable();
}

void TTSManager::speak(const std::string& text, bool interrupt) {
    if (isAvailable()) {
        engine->speak(text, interrupt);
    }
}

void TTSManager::speakSync(const std::string& text) {
    if (isAvailable()) {
        engine->speakSync(text);
    }
}

void TTSManager::speakTranslated(const std::string& key, const std::string& fallback, bool interrupt) {
    std::string text = fallback;
    if (translation) {
        text = translation->get(key, fallback);
    }
    speak(text, interrupt);
}

void TTSManager::stop() {
    if (isAvailable()) {
        engine->stop();
    }
}

bool TTSManager::isSpeaking() const {
    return isAvailable() && engine->isSpeaking();
}

void TTSManager::setRate(TTSRate rate) {
    if (isAvailable()) {
        engine->setRate(rate);
    }
}

void TTSManager::setVolume(int volume) {
    if (isAvailable()) {
        engine->setVolume(volume);
    }
}

bool TTSManager::setEngineType(TTSEngineType type) {
    if (engineType == type) {
        return true;
    }
    
    try {
        // Create and initialize the new engine BEFORE shutting down the old one.
        // This avoids leaving us without any TTS if the new engine fails.
        auto newEngine = createTTSEngine(type);
        if (!newEngine || !newEngine->initialize()) {
            // New engine failed — keep using the current one
            if (newEngine) {
                try { newEngine->shutdown(); } catch (...) {}
            }
            return false;
        }
        
        // New engine is ready — now shut down the old one safely
        if (engine) {
            try { engine->stop(); } catch (...) {}
            try { engine->shutdown(); } catch (...) {}
        }
        
        engine = std::move(newEngine);
        engineType = type;
        updateLanguage();
        return true;
    } catch (const std::exception&) {
        // If anything went wrong, ensure we still have a working engine
        if (!engine || !engine->isAvailable()) {
            try {
                engine = createTTSEngine(engineType);
                if (engine) {
                    engine->initialize();
                    updateLanguage();
                }
            } catch (...) {
                engine.reset();
            }
        }
        return false;
    } catch (...) {
        return false;
    }
}

void TTSManager::updateLanguage() {
    if (!isAvailable() || !translation) {
        return;
    }
    
    std::string lang = translation->getCurrentLanguage();
    
    // Map language codes
    if (lang == "eng") {
        engine->setLanguage("en");
    } else if (lang == "deu") {
        engine->setLanguage("de");
    } else {
        engine->setLanguage(lang);
    }
}
