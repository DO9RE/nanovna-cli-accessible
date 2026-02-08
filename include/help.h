#pragma once
#include <string>
#include "translation.h"

class HelpModule {
public:
    // Command line help (no translation needed - uses English for CLI args)
    static std::string getCommandLineHelp(const TranslationManager* tm = nullptr);
    
    // Screen-specific help
    static std::string getMainMenuHelp(const TranslationManager& tm);
    static std::string getAcousticAnalysisHelp(const TranslationManager& tm);
    static std::string getDeviceInfoHelp(const TranslationManager& tm);
    static std::string getTableViewHelp(const TranslationManager& tm);
    static std::string getCalibrationMenuHelp(const TranslationManager& tm);
    static std::string getOptionsMenuHelp(const TranslationManager& tm);
    static std::string getGoToMenuHelp(const TranslationManager& tm);
    static std::string getGoToMenuAcousticHelp(const TranslationManager& tm);
    static std::string getComfortFunctionsMenuHelp(const TranslationManager& tm);
    static std::string getCustomizeMenuHelp(const TranslationManager& tm);
};
