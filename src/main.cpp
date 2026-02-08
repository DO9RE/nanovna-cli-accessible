#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <filesystem>
#include <ctime>

#if defined(_WIN32)
// Include winsock2.h before windows.h to avoid conflicts
#include <winsock2.h>
#include <windows.h>  // For SetConsoleOutputCP
#endif

#include "config.h"
#include "logger.h"
#include "math_logger.h"
#include "comm_serial.h"
#include "protocol.h"
#include "ui.h"
#include "settings.h"
#include "help.h"
#include "translation.h"
#include "frequency_utils.h"

int main(int argc, char** argv) {
#if defined(_WIN32)
    // Enable UTF-8 support for console output on Windows
    // This allows proper display of German umlauts (ä, ö, ü, Ä, Ö, Ü, ß)
    // NOTE: These warnings are shown before translation system is initialized,
    // so they must remain in English. They are shown only on Windows startup errors.
    if (!SetConsoleOutputCP(CP_UTF8)) {
        std::cerr << "Warning: Failed to set console output to UTF-8. Umlauts may not display correctly.\n";
    }
    if (!SetConsoleCP(CP_UTF8)) {
        std::cerr << "Warning: Failed to set console input to UTF-8.\n";
    }
#endif

    AppConfig cfg;

    for (int i=1;i<argc;i++) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            std::cout << HelpModule::getCommandLineHelp() << std::endl;
            return 0;
        }
        else if (a == "-d") cfg.debug = true;
        else if (a == "--baud" && i+1<argc) cfg.baud = std::stoul(argv[++i]);
        else if ((a == "-p" || a == "--port") && i+1<argc) cfg.serial_port = argv[++i];
        else if (a == "--start" && i+1<argc) {
            if (!parseFrequencyString(argv[++i], cfg.start_freq)) {
                std::cerr << "Error: Invalid start frequency format. Use plain Hz or with units (e.g., 144M)\n";
                return 1;
            }
        }
        else if (a == "--end" && i+1<argc) {
            if (!parseFrequencyString(argv[++i], cfg.end_freq)) {
                std::cerr << "Error: Invalid end frequency format. Use plain Hz or with units (e.g., 146M)\n";
                return 1;
            }
        }
        else if (a == "--step" && i+1<argc) {
            if (!parseFrequencyString(argv[++i], cfg.step)) {
                std::cerr << "Error: Invalid step format. Use plain Hz or with units (e.g., 1K)\n";
                return 1;
            }
        }
        else if (a == "--autostart") cfg.autostart = true;
        else if (a == "--no-audio") cfg.audio = false;
        else if (a == "--config" && i+1<argc) cfg.command_templates_file = argv[++i];
    }
    
    // Create required directories
    std::filesystem::create_directories("logs");
    std::filesystem::create_directories("Export");
    std::filesystem::create_directories("config");
    std::filesystem::create_directories("Languages");

    Logger logger;
    MathLogger mathLogger;
    std::ostringstream name;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    name << "logs/debug_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".txt";
    if (cfg.debug) {
        logger.open(name.str());
        std::ostringstream commname;
        commname << "logs/debug_comm_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".txt";
        logger.openCommLog(commname.str());
        
        // Open math debug logger
        std::ostringstream mathname;
        mathname << "logs/debug_math_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".txt";
        mathLogger.open(mathname.str());
        logger.log("MAIN", "Math debug logger enabled: " + mathname.str());
    }
    logger.log("MAIN", "Program start");

    // Load settings (optional)
    std::string settingsErr;
    std::string settingsPath = "config/app_settings.cfg";
    loadAppSettings(cfg, settingsPath, settingsErr);

    // Initialize translation manager for main.cpp messages
    TranslationManager mainTranslation;
    std::string translationErr;
    if (!mainTranslation.loadLanguage(cfg.language, translationErr)) {
        // Fall back to English if loading fails
        mainTranslation.loadLanguage("eng", translationErr);
    }

    // Load command templates
    CommandTemplates templates;
    std::string loadErr;
    logger.log("MAIN", "Loading command templates: " + cfg.command_templates_file);
    if (!templates.loadFromFile(cfg.command_templates_file, loadErr)) {
        logger.log("MAIN", "Failed to load templates: " + loadErr);
        std::cerr << mainTranslation.format("MAIN_TEMPLATE_LOAD_WARNING", "Warning: {0}\nUsing defaults.", loadErr);
        templates.map["SCAN"] = "scan {start} {stop} {points} {outmask}";
        templates.map["CAL"] = "cal {caltype}";
        templates.map["SAVE_CAL"] = "save";
        templates.map["LOAD_CAL"] = "recall";
        templates.map["GET_INFO"] = "info";
    }

    SerialComm serial;
    serial.setLogger(&logger);

    struct SerialAdapter : public IComm {
        SerialComm* s = nullptr;
        bool write(const std::string& sdata, std::string& err) override { return s->writeData(sdata, err); }
        bool readLine(std::string& out, int timeoutMs, std::string& err) override { return s->readLine(out, timeoutMs, err); }
    } serialAdapter;
    serialAdapter.s = &serial;

    NanoVNAProtocol proto(&serialAdapter, templates);

    // Connect if port provided
    if (!cfg.serial_port.empty()) {
        std::string err;
        if (!serial.openPort(cfg.serial_port, cfg.baud, err)) {
            logger.log("COMM", std::string("Serial open failed: ") + err);
            std::cerr << mainTranslation.format("MAIN_SERIAL_OPEN_FAILED", "Serial open failed: {0}", err) << "\n";
        } else {
            logger.log("COMM", "Connected via Serial");
            
            // Validate that a NanoVNA device is actually connected and responding
            std::string validateErr;
            if (!proto.validateDevice(validateErr)) {
                logger.log("COMM", "Device validation failed: " + validateErr);
                std::cerr << mainTranslation.format("MAIN_DEVICE_VALIDATION_FAILED", 
                    "Warning: Device validation failed.\n{0}\n\n"
                    "The program will continue, but device operations may fail.\n"
                    "Use 'P' in the main menu to select a different port.", validateErr) << "\n";
                // Don't close the port - let user try again from menu
            } else {
                logger.log("COMM", "Device validation successful - NanoVNA detected");
                
                // Auto-load calibration from saved bank only if device validated
                if (cfg.calibration_bank >= 0) {
                    std::string calErr;
                    if (proto.loadCal(cfg.calibration_bank, calErr)) {
                        std::cout << mainTranslation.format("MAIN_CAL_LOADED", "Calibration profile loaded from bank {0}", cfg.calibration_bank) << "\n";
                        logger.log("CAL", "Auto-loaded calibration from bank " + std::to_string(cfg.calibration_bank));
                    } else {
                        // Only log error, don't show to user as it might be expected for empty banks
                        logger.log("CAL", "Auto-load from bank " + std::to_string(cfg.calibration_bank) + " failed: " + calErr);
                    }
                }
            }
        }
    }

    ConsoleUI ui(cfg, &logger, &mathLogger, &serial);
    ui.run(&proto);

    logger.log("MAIN", "Program end");
    mathLogger.close();
    logger.close();
    
    // Farewell message with delay for GUI users
    std::cout << mainTranslation.get("MAIN_FAREWELL", "\nNanoVNA-CLI-Accessible QRT, 73 DE DO9RE") << "\n";
    std::cout << mainTranslation.get("MAIN_CLOSING", "Program will close in 3 seconds...") << "\n" << std::endl;
    
    // Wait 3 seconds so users launching from GUI can read the message
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    return 0;
}
