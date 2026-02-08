#include "help.h"

std::string HelpModule::getCommandLineHelp(const TranslationManager* tm) {
    // Command line help uses translation if available, otherwise English
    if (tm) {
        return tm->get("HELP_CMD_LINE", 
R"(NanoVNA-CLI-Accessible - Accessible Console Application for NanoVNA-H4

Usage: nanovna-cli.exe [OPTIONS]

Command Line Options:
  -h, --help              Show this help message
  -d                      Enable debug logging (logs to logs/ directory)
  -p, --port PORT         Specify serial port (e.g., COM4)
  --baud RATE             Set baud rate (default: 9600)
  --start FREQ            Set start frequency in Hz (e.g., 144000000)
  --end FREQ              Set end frequency in Hz (e.g., 146000000)
  --step FREQ             Set frequency step in Hz (e.g., 1000)
  --autostart             Automatically perform measurement on startup
  --no-audio              Disable audio output
  --config FILE           Specify config file path (default: config/command_templates.cfg)

Examples:
  Interactive mode:
    nanovna-cli.exe -d

  Direct measurement:
    nanovna-cli.exe -d -p COM4 --start 144000000 --end 146000000 --step 1000 --autostart

Configuration:
  Settings are stored in: config/app_settings.cfg
  Command templates in: config/command_templates.cfg
  Debug logs in: logs/
  Export files in: Export/

Main Menu Commands:
  Press 'H' in any screen for context-sensitive help.
)");
    }
    
    // Fallback to English
    return R"(
NanoVNA-CLI-Accessible - Accessible Console Application for NanoVNA-H4

Usage: nanovna-cli.exe [OPTIONS]

Command Line Options:
  -h, --help              Show this help message
  -d                      Enable debug logging (logs to logs/ directory)
  -p, --port PORT         Specify serial port (e.g., COM4)
  --baud RATE             Set baud rate (default: 9600)
  --start FREQ            Set start frequency in Hz (e.g., 144000000)
  --end FREQ              Set end frequency in Hz (e.g., 146000000)
  --step FREQ             Set frequency step in Hz (e.g., 1000)
  --autostart             Automatically perform measurement on startup
  --no-audio              Disable audio output
  --config FILE           Specify config file path (default: config/command_templates.cfg)

Examples:
  Interactive mode:
    nanovna-cli.exe -d

  Direct measurement:
    nanovna-cli.exe -d -p COM4 --start 144000000 --end 146000000 --step 1000 --autostart

Configuration:
  Settings are stored in: config/app_settings.cfg
  Command templates in: config/command_templates.cfg
  Debug logs in: logs/
  Export files in: Export/

Main Menu Commands:
  Press 'H' in any screen for context-sensitive help.
)";
}

std::string HelpModule::getMainMenuHelp(const TranslationManager& tm) {
    std::string help = "\n";
    help += tm.get("HELP_MAIN_MENU_TITLE", "=== Main Menu Help ===") + "\n\n";
    help += tm.get("HELP_MAIN_MENU_COMMANDS", "Available Commands:") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_SUMMARY", "  S - Summary       Display measurement summary (min/max/avg SWR)") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_TABLE", "  T - Table         View measurement data in table format with pagination") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_ACOUSTIC", "  A - Acoustic      Enter acoustic analysis mode for audio representation") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_EXPORT", "  E - Export        Export measurement data to CSV or TXT format") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_LOAD", "  L - Load          Load previously exported measurement data") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_CALIBRATE", "  K - Kalibrate     Start calibration flow (Open/Short/Load)") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_PORT", "  P - Port          Select and configure serial port") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_RANGE", "  R - Range         Set frequency range and perform scan") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_DEVICE", "  D - Device Info   View device information and battery status") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_MANUAL", "  M - Manual        Perform manual measurement with current settings") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_HELP", "  H - Help          Show this help message") + "\n";
    help += tm.get("HELP_MAIN_MENU_CMD_QUIT", "  Q - Quit          Exit the application") + "\n\n";
    help += tm.get("HELP_MAIN_MENU_TIPS", "Tips:\n  - Use 'R' to set frequency range before taking measurements\n  - Use 'T' then 'C' to customize table columns\n  - Use 'A' for acoustic analysis after measurements\n  - Use 'E' to export data, 'L' to load it later\n  - Press 'H' in any screen for context-specific help") + "\n";
    return help;
}

std::string HelpModule::getAcousticAnalysisHelp(const TranslationManager& tm) {
    std::string help = "\n";
    help += tm.get("HELP_ACOUSTIC_TITLE", "=== Acoustic Analysis Help ===") + "\n\n";
    help += tm.get("HELP_ACOUSTIC_PLAYBACK", "Playback Control:") + "\n";
    help += tm.get("HELP_ACOUSTIC_PLAY", "  SPACE       Play / Pause (hard pause - silence)") + "\n";
    help += tm.get("HELP_ACOUSTIC_FREEZE", "  F           Freeze (static audio at current position)") + "\n";
    help += tm.get("HELP_ACOUSTIC_STOP", "  S           Stop and reset to start") + "\n";
    help += tm.get("HELP_ACOUSTIC_TOGGLE_MODE", "  T           Toggle smooth/dotted playback") + "\n";
    help += tm.get("HELP_ACOUSTIC_TIME", "  +/-         Increase/decrease playback time (in seconds)") + "\n\n";
    help += tm.get("HELP_ACOUSTIC_NAVIGATION", "Navigation:") + "\n";
    help += tm.get("HELP_ACOUSTIC_NAV_LR", "  Left/Right         Move ±1 point") + "\n";
    help += tm.get("HELP_ACOUSTIC_NAV_CTRL", "  Ctrl+Left/Right    Move ±10 points") + "\n";
    help += tm.get("HELP_ACOUSTIC_NAV_SHIFT", "  Ctrl+Shift+L/R     Move ±100 points") + "\n\n";
    help += tm.get("HELP_ACOUSTIC_LOOP", "Loop Control:") + "\n";
    help += tm.get("HELP_ACOUSTIC_LOOP_LEFT", "  L           Set left loop marker") + "\n";
    help += tm.get("HELP_ACOUSTIC_LOOP_RIGHT", "  R           Set right loop marker") + "\n";
    help += tm.get("HELP_ACOUSTIC_LOOP_TOGGLE", "  O           Toggle loop playback") + "\n";
    help += tm.get("HELP_ACOUSTIC_LOOP_ZOOM", "  Z           Toggle loop zoom (centers loop in stereo, applies full time to loop)") + "\n";
    help += tm.get("HELP_ACOUSTIC_LOOP_INVERT", "  I           Invert loop (play outside loop markers)") + "\n";
    help += tm.get("HELP_ACOUSTIC_CONTINUOUS", "  C           Toggle continuous replay") + "\n\n";
    help += tm.get("HELP_ACOUSTIC_CURVES", "Curves (1-5: toggle, Ctrl+digit: vol-, Shift+digit: vol+):") + "\n";
    help += tm.get("HELP_ACOUSTIC_CURVE1", "  1           SWR (Sine wave, stereo pan)") + "\n";
    help += tm.get("HELP_ACOUSTIC_CURVE2", "  2           Return Loss (Pure sine, stereo pan)") + "\n";
    help += tm.get("HELP_ACOUSTIC_CURVE3", "  3           Impedance |Z| (Triangle wave, stereo pan)") + "\n";
    help += tm.get("HELP_ACOUSTIC_CURVE4", "  4           Reactance X (Sawtooth, stereo pan)") + "\n";
    help += tm.get("HELP_ACOUSTIC_CURVE5", "  5           Phase (Sine with stereo pan)") + "\n\n";
    help += tm.get("HELP_ACOUSTIC_AUDIO_CONFIG", "Audio Configuration:") + "\n";
    help += tm.get("HELP_ACOUSTIC_AUDIO_Y", "  A           Open audio configuration screen\n              - Toggle Synthesizer/MIDI engine\n              - Configure frequency range for Synthesizer (R)\n              - Configure MIDI instruments per curve\n              - Preview MIDI sounds") + "\n\n";
    help += tm.get("HELP_ACOUSTIC_OTHER", "Other:") + "\n";
    help += tm.get("HELP_ACOUSTIC_RULER", "  Y           Play acoustic Y-axis ruler (ascending scale reference)") + "\n";
    help += tm.get("HELP_ACOUSTIC_X_RULER", "  X           Toggle X-axis ruler (blips at each measurement point)") + "\n";
    help += tm.get("HELP_ACOUSTIC_STATUS_LINE", "  N           Toggle status line (shows detailed info during playback)") + "\n";
    help += tm.get("HELP_ACOUSTIC_GOTO", "  G           Go To specific frequency/position") + "\n";
    help += tm.get("HELP_ACOUSTIC_MEASURE", "  M           Show measurement at current position") + "\n";
    help += tm.get("HELP_ACOUSTIC_EXPORT", "  E           Export measurement (in freeze mode)") + "\n";
    help += tm.get("HELP_ACOUSTIC_HELP", "  H           Show this help message") + "\n";
    help += tm.get("HELP_ACOUSTIC_ESC", "  ESC         Back to main menu") + "\n\n";
    help += tm.get("HELP_ACOUSTIC_MODES", "Playback Modes:\n  Smooth - Continuous gliding transitions between points\n  Dotted - Individual dots at timed intervals") + "\n\n";
    help += tm.get("HELP_ACOUSTIC_AUDIO_REP", "Audio Representation:\n  - Pitch represents Y-axis value (higher pitch = higher value)\n  - Stereo panning represents X-axis position (left to right)\n  - Each curve has a unique waveform for identification") + "\n\n";
    help += tm.get("HELP_ACOUSTIC_LOOP_ZOOM_MODE", "Loop Zoom Mode:\n  When enabled with loop active:\n  - Loop section is centered in the stereo field\n  - Full playback time is applied to the loop range\n  - Easier to hear panning/timing in edge-positioned loops") + "\n\n";
    help += tm.get("HELP_ACOUSTIC_TIPS", "Practical Tips:\n  - Start with smooth mode to get overview\n  - Switch to dotted mode to count points\n  - Use loop markers to focus on problem areas") + "\n";
    return help;
}

std::string HelpModule::getDeviceInfoHelp(const TranslationManager& tm) {
    std::string help = "\n";
    help += tm.get("HELP_DEVICE_TITLE", "=== Device Info Help ===") + "\n\n";
    help += tm.get("HELP_DEVICE_COMMANDS", "Available Commands:") + "\n";
    help += tm.get("HELP_DEVICE_INFO", "  I - Info          Display device information") + "\n";
    help += tm.get("HELP_DEVICE_BATTERY", "  B - Battery       Query and display battery voltage") + "\n";
    help += tm.get("HELP_DEVICE_HELP", "  H - Help          Show this help message") + "\n";
    help += tm.get("HELP_COMMAND_ESC_BACK", "  ESC - Back        Return to main menu") + "\n\n";
    help += tm.get("HELP_DEVICE_INFO_DESC", "Device Information:\n  Shows firmware version, hardware info, and device capabilities.") + "\n\n";
    help += tm.get("HELP_DEVICE_BATTERY_DESC", "Battery Status:\n  Displays current battery voltage of the NanoVNA device.\n  Helps monitor device power status during measurements.") + "\n\n";
    help += tm.get("HELP_DEVICE_TIPS", "Tips:\n  - Check battery before long measurement sessions\n  - Low battery can affect measurement accuracy") + "\n";
    return help;
}

std::string HelpModule::getTableViewHelp(const TranslationManager& tm) {
    std::string help = "\n";
    help += tm.get("HELP_TABLE_TITLE", "=== Table View Help ===") + "\n\n";
    help += tm.get("HELP_TABLE_NAV_COMMANDS", "Navigation Commands:") + "\n";
    help += tm.get("HELP_TABLE_SPACE", "  SPACE       Next page") + "\n";
    help += tm.get("HELP_TABLE_NEXT", "  N           Next page") + "\n";
    help += tm.get("HELP_TABLE_PREV", "  P           Previous page") + "\n";
    help += tm.get("HELP_TABLE_CUSTOMIZE", "  C           Customize columns (toggle which columns to display)") + "\n";
    help += tm.get("HELP_TABLE_ESC", "  ESC         Back to main menu") + "\n";
    help += tm.get("HELP_TABLE_HELP", "  H           Show this help message") + "\n\n";
    help += tm.get("HELP_TABLE_COLUMNS", "Table Columns:") + "\n";
    help += tm.get("HELP_TABLE_COL_INDEX", "  Index       Measurement point index") + "\n";
    help += tm.get("HELP_TABLE_COL_FREQ", "  Freq(Hz)    Frequency in Hertz") + "\n";
    help += tm.get("HELP_TABLE_COL_SWR", "  SWR         Standing Wave Ratio") + "\n";
    help += tm.get("HELP_TABLE_COL_RL", "  RL(dB)      Return Loss in decibels") + "\n";
    help += tm.get("HELP_TABLE_COL_R", "  R           Resistance (Real part of impedance)") + "\n";
    help += tm.get("HELP_TABLE_COL_X", "  X           Reactance (Imaginary part of impedance)") + "\n\n";
    help += tm.get("HELP_TABLE_TIPS", "Tips:\n  - Use SPACE or N to advance through pages\n  - Use P to go back to previous pages\n  - Table shows all measurement points with pagination") + "\n";
    return help;
}

std::string HelpModule::getCalibrationMenuHelp(const TranslationManager& tm) {
    std::string help = "\n";
    help += tm.get("HELP_CAL_TITLE", "=== Calibration Menu Help ===") + "\n\n";
    help += tm.get("HELP_CAL_COMMANDS", "Available Commands:") + "\n";
    help += tm.get("HELP_CAL_LOAD", "  L - Load          Load calibration from device memory bank") + "\n";
    help += tm.get("HELP_CAL_PERFORM", "  P - Perform       Start calibration wizard (Open/Short/Load)") + "\n";
    help += tm.get("HELP_CAL_HELP", "  H - Help          Show this help message") + "\n";
    help += tm.get("HELP_COMMAND_ESC_BACK", "  ESC - Back        Return to main menu") + "\n\n";
    help += tm.get("HELP_CAL_DESC", "Calibration:\n  Calibration improves measurement accuracy by compensating for\n  cable and connector effects. The wizard guides you through:\n  1. Open circuit (nothing connected)\n  2. Short circuit (short calibration standard)\n  3. Load (50 Ohm load)\n  Results are saved to the device for future measurements.") + "\n";
    return help;
}

std::string HelpModule::getOptionsMenuHelp(const TranslationManager& tm) {
    std::string help = "\n";
    help += tm.get("HELP_OPT_TITLE", "=== Options Menu Help ===") + "\n\n";
    help += tm.get("HELP_OPT_COMMANDS", "Available Commands:") + "\n";
    help += tm.get("HELP_OPT_LANGUAGE", "  L - Language      Select display language") + "\n";
    help += tm.get("HELP_OPT_BANDPLAN", "  B - Bandplan      Select amateur radio band plan (IARU regions)") + "\n";
    help += tm.get("HELP_OPT_HELP", "  H - Help          Show this help message") + "\n";
    help += tm.get("HELP_COMMAND_ESC_BACK", "  ESC - Back        Return to main menu") + "\n\n";
    help += tm.get("HELP_OPT_TIPS", "Tips:\n  - Language changes affect all menus and messages\n  - Bandplan affects comfort functions like Band Suitability Check\n  - Settings are saved automatically") + "\n";
    return help;
}

std::string HelpModule::getGoToMenuHelp(const TranslationManager& tm) {
    std::string help = "\n";
    help += tm.get("HELP_GOTO_TITLE", "=== Go To Menu Help ===") + "\n\n";
    help += tm.get("HELP_GOTO_COMMANDS", "Available Commands:") + "\n";
    help += tm.get("HELP_GOTO_POINT", "  P - Point         Jump to specific point index") + "\n";
    help += tm.get("HELP_GOTO_FREQ", "  F - Frequency     Jump to nearest frequency") + "\n";
    help += tm.get("HELP_GOTO_MIN", "  M - Min SWR       Jump to point with minimum SWR") + "\n";
    help += tm.get("HELP_GOTO_MAX", "  X - Max SWR       Jump to point with maximum SWR") + "\n";
    help += tm.get("HELP_GOTO_HELP", "  H - Help          Show this help message") + "\n";
    help += tm.get("HELP_GOTO_CANCEL", "  ESC - Cancel      Return to table view") + "\n\n";
    help += tm.get("HELP_GOTO_TIPS", "Tips:\n  - Point index starts at 1\n  - Frequency search finds the closest match\n  - Min/Max SWR helps find resonance and problem areas") + "\n";
    return help;
}

std::string HelpModule::getGoToMenuAcousticHelp(const TranslationManager& tm) {
    std::string help = "\n";
    help += tm.get("HELP_GOTO_ACOUSTIC_TITLE", "=== Go To Menu (Acoustic) Help ===") + "\n\n";
    help += tm.get("HELP_GOTO_ACOUSTIC_COMMANDS", "Available Commands:") + "\n";
    help += tm.get("HELP_GOTO_ACOUSTIC_POINT", "  P - Point         Jump to specific point index") + "\n";
    help += tm.get("HELP_GOTO_ACOUSTIC_FREQ", "  F - Frequency     Jump to nearest frequency") + "\n";
    help += tm.get("HELP_GOTO_ACOUSTIC_MIN", "  M - Min SWR       Jump to point with minimum SWR") + "\n";
    help += tm.get("HELP_GOTO_ACOUSTIC_MAX", "  X - Max SWR       Jump to point with maximum SWR") + "\n";
    help += tm.get("HELP_GOTO_ACOUSTIC_CROSS", "  C - Cross Point   Find intersection between two curves") + "\n";
    help += tm.get("HELP_GOTO_ACOUSTIC_HELP", "  H - Help          Show this help message") + "\n";
    help += tm.get("HELP_GOTO_ACOUSTIC_CANCEL", "  ESC - Cancel      Return to acoustic analysis") + "\n\n";
    help += tm.get("HELP_GOTO_ACOUSTIC_TIPS", "Tips:\n  - Cross point helps find where impedance matches target\n  - Use after jumping to hear the audio at that position\n  - Combine with loop markers to focus on specific ranges") + "\n";
    return help;
}

std::string HelpModule::getComfortFunctionsMenuHelp(const TranslationManager& tm) {
    std::string help = "\n";
    help += tm.get("HELP_COMFORT_TITLE", "=== Comfort Functions Menu Help ===") + "\n\n";
    help += tm.get("HELP_COMFORT_DESC", "Comfort Functions provide convenient tools for antenna and cable analysis.") + "\n\n";
    help += tm.get("HELP_COMFORT_COMMANDS", "Available Functions:") + "\n";
    help += tm.get("HELP_COMFORT_1", "  1 - Band Suitability      Check antenna performance on amateur radio bands") + "\n";
    help += tm.get("HELP_COMFORT_2", "  2 - Resonance Finder      Find frequencies with minimum SWR") + "\n";
    help += tm.get("HELP_COMFORT_3", "  3 - SWR Bandwidth         Calculate 1.5:1 and 2:1 SWR bandwidth") + "\n";
    help += tm.get("HELP_COMFORT_4", "  4 - Feedpoint Impedance   Detailed impedance at selected frequency") + "\n";
    help += tm.get("HELP_COMFORT_5", "  5 - Matching Hints        Suggestions for impedance matching") + "\n";
    help += tm.get("HELP_COMFORT_6", "  6 - Cable Length          Estimate cable length from phase") + "\n";
    help += tm.get("HELP_COMFORT_7", "  7 - Cable Fault           Detect shorts, opens, and damage") + "\n";
    help += tm.get("HELP_COMFORT_8", "  8 - Cable Attenuation     Measure cable loss (requires S21)") + "\n";
    help += tm.get("HELP_COMFORT_9", "  9 - Filter Quick Check    Analyze filter characteristics (requires S21)") + "\n";
    help += tm.get("HELP_COMFORT_10", "  10 - Before/After         Compare two measurements") + "\n";
    help += tm.get("HELP_COMFORT_11", "  11 - Auto-Marker (A)      Automatically set markers at key points") + "\n";
    help += tm.get("HELP_COMFORT_12", "  12 - Configuration (C)    Set velocity factor, SWR limits, etc.") + "\n";
    help += tm.get("HELP_COMFORT_HELP", "  H - Help                  Show this help message") + "\n";
    help += tm.get("HELP_COMMAND_ESC_BACK", "  ESC - Back        Return to main menu") + "\n\n";
    help += tm.get("HELP_COMFORT_TIPS", "Tips:\n  - Functions 1-7 work with S11 (reflection) measurements\n  - Functions 8-9 require S21 (transmission) measurements\n  - Most functions will prompt to measure if no data available\n  - Configuration settings affect function behavior") + "\n";
    return help;
}

std::string HelpModule::getCustomizeMenuHelp(const TranslationManager& tm) {
    std::string help = "\n";
    help += tm.get("HELP_CUSTOM_TITLE", "=== Customize Columns Help ===") + "\n\n";
    help += tm.get("HELP_CUSTOM_DESC", "Customize which columns are displayed in the table view.") + "\n\n";
    help += tm.get("HELP_CUSTOM_COLUMNS", "Available Columns:") + "\n";
    help += tm.get("HELP_CUSTOM_FREQ", "  FREQ  - Frequency in Hz") + "\n";
    help += tm.get("HELP_CUSTOM_SWR", "  SWR   - Standing Wave Ratio") + "\n";
    help += tm.get("HELP_CUSTOM_RL", "  RL    - Return Loss in dB") + "\n";
    help += tm.get("HELP_CUSTOM_R", "  R     - Resistance (real impedance)") + "\n";
    help += tm.get("HELP_CUSTOM_X", "  X     - Reactance (imaginary impedance)") + "\n";
    help += tm.get("HELP_CUSTOM_Z", "  Z     - Impedance magnitude |Z|") + "\n";
    help += tm.get("HELP_CUSTOM_PHASE", "  PHASE - Phase angle in degrees") + "\n\n";
    help += tm.get("HELP_CUSTOM_USAGE", "Usage:\n  - Enter column number (1-7) to toggle it on/off\n  - Press Enter to return to table view\n  - [X] indicates enabled, [ ] indicates disabled\n  - Changes are saved automatically") + "\n";
    return help;
}
