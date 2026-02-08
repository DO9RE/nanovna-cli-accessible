#pragma once
#include "config.h"
#include <string>
#include <vector>

bool loadAppSettings(AppConfig& cfg, const std::string& path, std::string& err);
bool saveAppSettings(const AppConfig& cfg, const std::string& path, std::string& err);

// Braille printer profile management
bool saveBrailleProfile(const AppConfig& cfg, const std::string& profileName, std::string& err);
bool loadBrailleProfile(AppConfig& cfg, const std::string& profileName, std::string& err);
std::vector<std::string> listBrailleProfiles();