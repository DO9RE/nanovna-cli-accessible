#include "translation.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

static void trim(std::string &s) {
    while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
    while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
}

TranslationManager::TranslationManager() : currentLanguage("eng") {
    // Default to English
}

bool TranslationManager::loadLanguage(const std::string& languageCode, std::string& error) {
    std::string filepath = "Languages/" + languageCode + ".lng";
    
    if (!parseLanguageFile(filepath, error)) {
        return false;
    }
    
    currentLanguage = languageCode;
    return true;
}

std::string TranslationManager::get(const std::string& key) const {
    auto it = translations.find(key);
    if (it != translations.end()) {
        return it->second;
    }
    // Return key itself if not found (fallback)
    return key;
}

std::string TranslationManager::get(const std::string& key, const std::string& fallback) const {
    auto it = translations.find(key);
    if (it != translations.end()) {
        return it->second;
    }
    return fallback;
}

bool TranslationManager::hasKey(const std::string& key) const {
    return translations.find(key) != translations.end();
}

std::vector<std::pair<std::string, std::string>> TranslationManager::getAvailableLanguages(std::string& error) {
    std::vector<std::pair<std::string, std::string>> languages;
    
    try {
        std::filesystem::path langDir = std::filesystem::u8path("Languages");
        
        if (!std::filesystem::exists(langDir)) {
            error = "Languages directory not found";
            return languages;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(langDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".lng") {
                std::string filename = entry.path().filename().string();
                std::string langCode = filename.substr(0, filename.length() - 4);  // Remove .lng
                
                // Read the first line of the file to get the language name
                std::ifstream file(entry.path());
                std::string line;
                std::string langName = langCode;  // Default to code
                
                if (file && std::getline(file, line)) {
                    // Check if first line is a comment with language name
                    trim(line);
                    if (line.length() > 2 && line[0] == '#') {
                        std::string comment = line.substr(1);
                        trim(comment);
                        // First line should be: # LANGUAGE_NAME=English or # LANGUAGE_NAME=Deutsch
                        if (comment.find("LANGUAGE_NAME=") == 0) {
                            langName = comment.substr(14);
                            trim(langName);
                        } else {
                            // Just use the comment as language name
                            langName = comment;
                        }
                    }
                }
                
                languages.push_back({langCode, langName});
            }
        }
        
        // Sort by language code
        std::sort(languages.begin(), languages.end(), 
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        
    } catch (const std::exception& e) {
        error = std::string("Error scanning Languages directory: ") + e.what();
        return languages;
    }
    
    return languages;
}

bool TranslationManager::parseLanguageFile(const std::string& filepath, std::string& error) {
    std::filesystem::path p = std::filesystem::u8path(filepath);
    
    if (!std::filesystem::exists(p)) {
        error = "Language file not found: " + filepath;
        return false;
    }
    
    std::ifstream ifs(p);
    if (!ifs) {
        error = "Cannot open language file: " + filepath;
        return false;
    }
    
    translations.clear();
    std::string line;
    int lineNum = 0;
    
    while (std::getline(ifs, line)) {
        lineNum++;
        trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Parse key=value pairs
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            // Skip lines without '=' (not an error, just ignore)
            continue;
        }
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        trim(key);
        trim(value);
        
        if (key.empty()) {
            // Skip empty keys
            continue;
        }
        
        // Unescape special characters in value
        // Support \n for newlines
        size_t escapePos = 0;
        while ((escapePos = value.find("\\n", escapePos)) != std::string::npos) {
            value.replace(escapePos, 2, "\n");
            escapePos += 1;
        }
        
        translations[key] = value;
    }
    
    return true;
}
