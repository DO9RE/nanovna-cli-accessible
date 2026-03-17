#pragma once
#include <string>
#include <map>
#include <vector>
#include <sstream>

// Translation manager class for multi-language support
class TranslationManager {
public:
    TranslationManager();
    
    // Load a language file
    bool loadLanguage(const std::string& languageCode, std::string& error);
    
    // Load additional translation file (merges into existing translations without clearing)
    bool loadAdditionalFile(const std::string& filepath, std::string& error);
    
    // Get translated string by key
    std::string get(const std::string& key) const;
    
    // Get translated string with fallback
    std::string get(const std::string& key, const std::string& fallback) const;
    
    // Get translated string with fallback and format with replacements
    // Replaces {0}, {1}, etc. with provided arguments
    template<typename... Args>
    std::string format(const std::string& key, const std::string& fallback, Args... args) const {
        std::string text = get(key, fallback);
        return formatString(text, args...);
    }
    
    // Get available languages (by scanning Languages directory)
    static std::vector<std::pair<std::string, std::string>> getAvailableLanguages(std::string& error);
    
    // Get current language code
    std::string getCurrentLanguage() const { return currentLanguage; }
    
    // Check if translation exists for a key
    bool hasKey(const std::string& key) const;
    
private:
    std::string currentLanguage;
    std::map<std::string, std::string> translations;
    
    // Parse a language file
    bool parseLanguageFile(const std::string& filepath, std::string& error);
    
    // Helper function to format strings with placeholders
    template<typename T>
    std::string formatString(std::string text, T value) const {
        std::ostringstream oss;
        oss << value;
        size_t pos = text.find("{0}");
        if (pos != std::string::npos) {
            text.replace(pos, 3, oss.str());
        }
        return text;
    }
    
    template<typename T, typename... Args>
    std::string formatString(std::string text, T first, Args... rest) const {
        std::ostringstream oss;
        oss << first;
        size_t pos = text.find("{0}");
        if (pos != std::string::npos) {
            text.replace(pos, 3, oss.str());
        }
        // Shift remaining placeholders down by 1
        for (int i = 0; i < 10; i++) {
            std::string oldPlaceholder = "{" + std::to_string(i + 1) + "}";
            std::string newPlaceholder = "{" + std::to_string(i) + "}";
            size_t p = 0;
            while ((p = text.find(oldPlaceholder, p)) != std::string::npos) {
                text.replace(p, oldPlaceholder.length(), newPlaceholder);
                p += newPlaceholder.length();
            }
        }
        return formatString(text, rest...);
    }
};
