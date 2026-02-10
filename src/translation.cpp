#include "translation.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

static void trim(std::string &s) {
    while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
    while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
}

// Helper function to process escape sequences
static void processEscapeSequence(char escapeChar, std::string& output, bool& inEscape) {
    switch (escapeChar) {
        case 'n':
            output += '\n';
            inEscape = false;
            break;
        case 't':
            output += '\t';
            inEscape = false;
            break;
        case 'r':
            output += '\r';
            inEscape = false;
            break;
        case '\\':
            output += '\\';
            inEscape = false;
            break;
        case '"':
            output += '"';
            inEscape = false;
            break;
        default:
            // Unknown escape - keep the backslash and character
            output += '\\';
            output += escapeChar;
            inEscape = false;
            break;
    }
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
        
        // Check if value is quoted - if so, support multi-line quoted strings
        if (!value.empty() && value[0] == '"') {
            // Start of quoted string - read until closing quote (handling escapes)
            std::string quotedValue;
            bool inEscape = false;
            bool foundClosingQuote = false;
            
            // Skip opening quote
            for (size_t i = 1; i < value.length(); i++) {
                if (inEscape) {
                    processEscapeSequence(value[i], quotedValue, inEscape);
                } else if (value[i] == '\\') {
                    inEscape = true;
                } else if (value[i] == '"') {
                    // Found closing quote
                    foundClosingQuote = true;
                    break;
                } else {
                    quotedValue += value[i];
                }
            }
            
            // If we didn't find closing quote on this line, continue reading lines
            while (!foundClosingQuote && std::getline(ifs, line)) {
                lineNum++;
                // Add newline for the line break (literal newline in multi-line string)
                quotedValue += '\n';
                
                // Process this continuation line
                for (size_t i = 0; i < line.length(); i++) {
                    if (inEscape) {
                        processEscapeSequence(line[i], quotedValue, inEscape);
                    } else if (line[i] == '\\') {
                        inEscape = true;
                    } else if (line[i] == '"') {
                        // Found closing quote
                        foundClosingQuote = true;
                        break;
                    } else {
                        quotedValue += line[i];
                    }
                }
            }
            
            if (!foundClosingQuote) {
                error = "Unclosed quoted string at line " + std::to_string(lineNum) + " for key: " + key;
                return false;
            }
            
            translations[key] = quotedValue;
        } else {
            // Unquoted value - process escape sequences for backward compatibility
            // Use a single-pass approach with state machine to handle sequences correctly
            std::string processed;
            for (size_t i = 0; i < value.length(); i++) {
                if (value[i] == '\\' && i + 1 < value.length()) {
                    // Escape sequence
                    char next = value[i + 1];
                    switch (next) {
                        case 'n':
                            processed += '\n';
                            i++; // Skip next character
                            break;
                        case 't':
                            processed += '\t';
                            i++;
                            break;
                        case 'r':
                            processed += '\r';
                            i++;
                            break;
                        case '\\':
                            processed += '\\';
                            i++;
                            break;
                        case '"':
                            processed += '"';
                            i++;
                            break;
                        default:
                            // Unknown escape - keep the backslash
                            processed += value[i];
                            break;
                    }
                } else {
                    processed += value[i];
                }
            }
            
            translations[key] = processed;
        }
    }
    
    return true;
}
