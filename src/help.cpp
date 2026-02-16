#include "help.h"

std::string HelpModule::getCommandLineHelp(const TranslationManager* tm) {
    // Command line help uses translation if available, otherwise English
    if (tm) {
        return tm->get("HELP_CMD_LINE", 
R"(NanoVNA-CLI-Accessible - Accessible Console Application for NanoVNA-H4

Usage: nanovna-cli.exe [OPTIONS]

Command Line Options:
  -h, --help              Show this help message
  -v, --version           Show version information
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
  -v, --version           Show version information
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
    return tm.get("HELP_MAIN_MENU_FULL",
R"(
Main Menu Help

Available Commands:
  S - Summary       Display measurement summary (min/max/avg SWR)
  T - Table         View measurement data in table format with pagination
  A - Acoustic      Enter acoustic analysis mode for audio representation
  E - Export        Export measurement data to CSV or TXT format
  L - Load          Load previously exported measurement data
  K - Kalibrate     Start calibration flow (Open/Short/Load)
  P - Port          Select and configure serial port
  R - Range         Set frequency range and perform scan
  D - Device Info   View device information and battery status
  M - Manual        Perform manual measurement with current settings
  H - Help          Show this help message
  Q - Quit          Exit the application

Tips:
  - Use 'R' to set frequency range before taking measurements
  - Use 'T' then 'C' to customize table columns
  - Use 'A' for acoustic analysis after measurements
  - Use 'E' to export data, 'L' to load it later
  - Press 'H' in any screen for context-specific help
)");
}

std::string HelpModule::getAcousticAnalysisHelp(const TranslationManager& tm) {
    return tm.get("HELP_ACOUSTIC_FULL",
R"(
Acoustic Analysis Help

Playback Control:
  SPACE       Play / Pause (hard pause - silence)
  F           Freeze (static audio at current position)
  S           Stop and reset to start
  T           Toggle smooth/dotted playback
  +/-         Increase/decrease playback time (in seconds)

Navigation:
  Left/Right         Move by current jump width
  Up/Down            Adjust jump width (1/10/100/500/1000 points)

Loop Control:
  L           Set left loop marker
  R           Set right loop marker
  O           Toggle loop playback
  Z           Toggle loop zoom (centers loop in stereo, applies full time to loop)
  I           Invert loop (play outside loop markers)
  C           Toggle continuous replay

Curves (1-5: toggle, Ctrl+digit: vol-, Shift+digit: vol+):
  1           SWR (Sine wave, stereo pan)
  2           Return Loss (Pure sine, stereo pan)
  3           Impedance |Z| (Triangle wave, stereo pan)
  4           Reactance X (Sawtooth, stereo pan)
  5           Phase (Sine with stereo pan)

Audio Configuration:
  A           Open audio configuration screen
              - Toggle Synthesizer/MIDI engine
              - Configure frequency range for Synthesizer (R)
              - Configure MIDI instruments per curve
              - Preview MIDI sounds
  W           Export audio file
              - MIDI mode: exports MIDI file to Export/ directory
              - Synth mode: renders audio to WAV file in Export/ directory

Other:
  Y           Play acoustic Y-axis ruler (ascending scale reference)
  X           Toggle X-axis ruler (blips at each measurement point)
  N           Toggle status line (shows detailed info during playback)
  V           Toggle Smith diagram visualization
  B           Change Smith visualization mode (6 modes available)
  G           Go To specific frequency/position
  M           Show measurement at current position
  E           Export measurement (in freeze mode)
  H           Show this help message
  ESC         Back to main menu

Playback Modes:
  Smooth - Continuous gliding transitions between points
  Dotted - Individual dots at timed intervals

Audio Representation:
  - Pitch represents Y-axis value (higher pitch = higher value)
  - Stereo panning represents X-axis position (left to right)
  - Each curve has a unique waveform for identification

Loop Zoom Mode:
  When enabled with loop active:
  - Loop section is centered in the stereo field
  - Full playback time is applied to the loop range
  - Easier to hear panning/timing in edge-positioned loops

Practical Tips:
  - Start with smooth mode to get overview
  - Switch to dotted mode to count points
  - Use loop markers to focus on problem areas
)");
}

std::string HelpModule::getDeviceInfoHelp(const TranslationManager& tm) {
    return tm.get("HELP_DEVICE_FULL",
R"(
Device Info Help

Available Commands:
  I - Info          Display device information
  B - Battery       Query and display battery voltage
  H - Help          Show this help message
  ESC - Back        Return to main menu

Device Information:
  Shows firmware version, hardware info, and device capabilities.

Battery Status:
  Displays current battery voltage of the NanoVNA device.
  Helps monitor device power status during measurements.

Tips:
  - Check battery before long measurement sessions
  - Low battery can affect measurement accuracy
)");
}

std::string HelpModule::getTableViewHelp(const TranslationManager& tm) {
    return tm.get("HELP_TABLE_FULL",
R"(
Table View Help

Navigation Commands:
  SPACE       Next page
  N           Next page
  P           Previous page
  C           Customize columns (toggle which columns to display)
  ESC         Back to main menu
  H           Show this help message

Table Columns:
  Index       Measurement point index
  Freq(Hz)    Frequency in Hertz
  SWR         Standing Wave Ratio
  RL(dB)      Return Loss in decibels
  R           Resistance (Real part of impedance)
  X           Reactance (Imaginary part of impedance)

Tips:
  - Use SPACE or N to advance through pages
  - Use P to go back to previous pages
  - Press G to jump to specific point or frequency
  - Press C to customize which columns are displayed
  - Hide unneeded columns to reduce screen reader verbosity
  - Use Go To > Min SWR to find resonance quickly
)");
}

std::string HelpModule::getCalibrationMenuHelp(const TranslationManager& tm) {
    return tm.get("HELP_CAL_FULL",
R"(
Calibration Menu Help

Available Commands:
  L - Load          Load calibration from device memory bank
  P - Perform       Start calibration wizard (Open/Short/Load)
  H - Help          Show this help message
  ESC - Back        Return to main menu

Calibration:
  Calibration improves measurement accuracy by compensating for
  cable and connector effects. The wizard guides you through:
  1. Open circuit (nothing connected)
  2. Short circuit (short calibration standard)
  3. Load (50 Ohm load)
  Results are saved to the device for future measurements.
)");
}

std::string HelpModule::getOptionsMenuHelp(const TranslationManager& tm) {
    return tm.get("HELP_OPT_FULL",
R"(
Options Menu Help

Available Commands:
  L - Language      Select display language
  B - Bandplan      Select amateur radio band plan (IARU regions)
  H - Help          Show this help message
  ESC - Back        Return to main menu

Tips:
  - Language changes affect all menus and messages
  - Bandplan affects comfort functions like Band Suitability Check
  - Settings are saved automatically
)");
}

std::string HelpModule::getGoToMenuHelp(const TranslationManager& tm) {
    return tm.get("HELP_GOTO_FULL",
R"(
Go To Menu Help

Available Commands:
  P - Point         Jump to specific point index
  F - Frequency     Jump to nearest frequency
  M - Min SWR       Jump to point with minimum SWR
  X - Max SWR       Jump to point with maximum SWR
  H - Help          Show this help message
  ESC - Cancel      Return to table view

Tips:
  - Point index starts at 1
  - Frequency search finds the closest match
  - Min/Max SWR helps find resonance and problem areas
)");
}

std::string HelpModule::getGoToMenuAcousticHelp(const TranslationManager& tm) {
    return tm.get("HELP_GOTO_ACOUSTIC_FULL",
R"(
Go To Menu (Acoustic) Help

Available Commands:
  P - Point         Jump to specific point index
  F - Frequency     Jump to nearest frequency
  M - Min SWR       Jump to point with minimum SWR
  X - Max SWR       Jump to point with maximum SWR
  C - Cross Point   Find intersection between two curves
  H - Help          Show this help message
  ESC - Cancel      Return to acoustic analysis

Tips:
  - Cross point helps find where impedance matches target
  - Use after jumping to hear the audio at that position
  - Combine with loop markers to focus on specific ranges
)");
}

std::string HelpModule::getComfortFunctionsMenuHelp(const TranslationManager& tm) {
    return tm.get("HELP_COMFORT_FULL", 
R"(
Comfort Functions Menu Help

Comfort Functions provide convenient tools for antenna and cable analysis.

Available Functions:
  1 - Band Suitability      Check antenna performance on amateur radio bands
  2 - Resonance Finder      Find frequencies with minimum SWR
  3 - SWR Bandwidth         Calculate 1.5:1 and 2:1 SWR bandwidth
  4 - Feedpoint Impedance   Detailed impedance at selected frequency
  5 - Matching Hints        Suggestions for impedance matching
  6 - Cable Length          Estimate cable length from phase
  7 - Cable Fault           Detect shorts, opens, and damage
  8 - Cable Attenuation     Measure cable loss (requires S21)
  9 - Filter Quick Check    Analyze filter characteristics (requires S21)
  10 - Before/After         Compare two measurements
  11 - Auto-Marker (A)      Automatically set markers at key points
  12 - Configuration (C)    Set velocity factor, SWR limits, etc.
  H - Help                  Show this help message
  ESC - Back        Return to main menu

Tips:
  - Functions 1-7 work with S11 (reflection) measurements
  - Functions 8-9 require S21 (transmission) measurements
  - Most functions will prompt to measure if no data available
  - Configuration settings affect function behavior
)");
}

std::string HelpModule::getCustomizeMenuHelp(const TranslationManager& tm) {
    return tm.get("HELP_CUSTOM_FULL",
R"(
Customize Columns Help

Customize which columns are displayed in the table view.

Available Columns:
  FREQ  - Frequency in Hz
  SWR   - Standing Wave Ratio
  RL    - Return Loss in dB
  R     - Resistance (real impedance)
  X     - Reactance (imaginary impedance)
  Z     - Impedance magnitude |Z|
  PHASE - Phase angle in degrees

Usage:
  - Enter column number (1-7) to toggle it on/off
  - Press Enter to return to table view
  - [X] indicates enabled, [ ] indicates disabled
  - Changes are saved automatically
)");
}

std::string HelpModule::getSmithVisualizationHelp(const TranslationManager& tm) {
    return tm.get("HELP_SMITH_FULL",
R"(
Smith Diagram Visualization Help

Smith Diagram shows impedance matching acoustically using spatial audio.
YOU are positioned at the center of the Smith chart (perfect 50Ω match).
Measurement points move around you in 3D space.

Key Concepts:
  Center (YOU) = Perfect match (50Ω, SWR 1.0)
  Edge         = Poor match (high SWR)
  Left-Right   = Resistance level (Re of reflection coefficient)
  Front-Back   = Reactance (Im of reflection coefficient)
    Front = Inductive (antenna too short)
    Back  = Capacitive (antenna too long)

Visualization Modes (All 6 Modes Fully Implemented):

1 - CARTESIAN (Easiest - Recommended for Beginners)
    Position in rectangular 3D space
    • Re(Γ) → Left/Right axis
    • Im(Γ) → Front/Back axis
    • You stand at center, sound moves in rectangular pattern
    • Best for: Learning Smith chart basics
    
2 - POLAR (Advanced - Natural Smith Structure)
    Sound rotates around you in a circle
    • Angle → Direction around you (0°=front, ±180°=back)
    • Radius → Distance from you (closer=better match)
    • Sound circles clockwise/counterclockwise over frequency
    • Best for: Cable analysis, detecting rotations
    
3 - IMPEDANCE DIRECT (Simple - No Smith Knowledge Needed)
    Direct R and X values mapped to space
    • R (Resistance) → Left/Right (0Ω left, 50Ω center, 200Ω right)
    • X (Reactance) → Front/Back (+200Ω front, 0Ω center, -200Ω back)
    • No gamma conversion, direct impedance perception
    • Best for: Beginners without Smith experience
    
4 - SWR CIRCLES (Goal-Oriented - Find Best Match)
    Focus on SWR quality, radial positioning
    • Sound distance from you = SWR level
    • Near you = Good match (low SWR)
    • Far from you = Poor match (high SWR)
    • Volume increases with worse SWR
    • Best for: Antenna tuning, finding resonance
    
5 - TIME DOMAIN CUES (Hybrid - Frequency + Smith Context)
    Standard frequency sweep enhanced with subtle Smith cues
    • Primary: Frequency progression (left to right)
    • Secondary: Subtle Smith position cues (front/back, volume)
    • Best for: Users who like acoustic mode but want Smith context
    
6 - HYBRID MULTI (Expert - Maximum Information)
    Multiple simultaneous audio layers
    • Layer 1: Polar positioning (main spatial cue)
    • Layer 2: SWR information (volume modulation)
    • Layer 3: Event markers (axis crossings, resonance)
    • Combines best of all modes
    • Best for: Experienced users, complex analysis

Controls:
  V - Toggle Smith visualization on/off
  B - Change visualization mode (1-6)
  C - Configure Smith Audio settings (volumes, surround, etc.)
  H - Show this Smith help

Audio Representation:
  • Spatial position shows impedance location in Smith chart
  • Subtle ambient noise indicates position
  • Axis crossing events mark important transitions
  • Center pulse (optional) provides reference signal
  • Default volume: 30% (configurable in Audio Config → Smith → C)

Hardware Recommendations:
  • Stereo Headphones: Psychoacoustic processing simulates 3D
  • 5.1/7.1 Surround: Full 360° spatial positioning
  • Calibration: Run Spatial Audio Wizard (Audio Config → W)
    for personalized optimization!

Tips:
  • Start with mode 1 (Cartesian) or 3 (Impedance Direct)
  • Use headphones for best spatial audio experience
  • Smith cues work best with smooth playback mode
  • Sound moves toward center when approaching resonance
  • Front/back position indicates inductive/capacitive
  • Run the Spatial Calibration Wizard (W key) for optimal results!

Example Interpretations:
  Center sound = Perfect 50Ω match (YOU are here!)
  Sound moves to center = Approaching resonance
  Sound in front = Inductive reactance (antenna too short)
  Sound in back = Capacitive reactance (antenna too long)
  Sound at edge = High SWR, poor match
  Sound circles around = Frequency sweep over reactive component
)");
}
