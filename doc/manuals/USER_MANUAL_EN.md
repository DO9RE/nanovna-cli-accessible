# NanoVNA CLI Accessible - User Manual

**Version:** Beta  
**Date:** January 2026  
**Language:** English

---

## Table of Contents

1. [About NanoVNA CLI Accessible](#1-about-nanovna-cli-accessible)
2. [System Requirements](#2-system-requirements)
3. [Installation](#3-installation)
4. [First Steps](#4-first-steps)
5. [Main Menu Overview](#5-main-menu-overview)
6. [Basic Operations](#6-basic-operations)
7. [U-Menu: Analysis Toolkit](#7-u-menu-analysis-toolkit)
8. [Acoustic Analysis Mode](#8-acoustic-analysis-mode)
9. [Export and Import](#9-export-and-import)
10. [Web Interface](#10-web-interface)
11. [Calibration](#11-calibration)
12. [Troubleshooting](#12-troubleshooting)
13. [File Structure](#13-file-structure)

---

## 1. About NanoVNA CLI Accessible

NanoVNA CLI Accessible is a specialized Windows console application for operating the NanoVNA-H4 Vector Network Analyzer. It was developed specifically for amateur radio operators who are blind or visually impaired.

### Key Features:
- **Acoustic representation** of RF parameters (SWR, impedance, reactance, phase)
- **Screen reader compatible** with context-sensitive help
- **Comprehensive analysis tools** for antennas, cables, and filters
- **Bilingual interface** (English and German)
- **Braille export** for tactile graphics on Index Braille printers

---

## 2. System Requirements

### Hardware:
- **Operating System:** Windows 7 or later (64-bit recommended)
- **NanoVNA Device:** NanoVNA-H4
- **Connection:** USB cable (appears as COM port)
- **Sound Card:** For acoustic analysis mode
- **Optional:** Index Braille printer for tactile graphics

### Software:
- The application is fully self-contained
- No additional software installation required
- All dependencies are statically linked

---

## 3. Installation

The beta distribution comes as a ready-to-use build directory. **No compilation is required.**

### Installation Steps:

1. **Extract the ZIP file** to a location of your choice (e.g., `C:\NanoVNA-CLI`)

2. **Verify the directory structure:**
   ```
   nanovna-cli.exe         (Main executable)
   Languages/              (Language files)
   config/                 (Configuration files)
   bandplans/              (Amateur radio band definitions)
   logs/                   (Log files will be stored here)
   Export/                 (Exported data will be saved here)
   ```

3. **Connect your NanoVNA-H4** to a USB port

4. **Done!** You can now run the application.

---

## 4. First Steps

### Starting the Application:

**Option 1: Simple start**
- Double-click `nanovna-cli.exe`
- Or from command line: `nanovna-cli.exe`

**Option 2: With debug logging (recommended for beta testing)**
- Open Command Prompt or PowerShell
- Navigate to the application directory
- Run: `nanovna-cli.exe -d`

### First-Time Setup:

1. **Select COM Port:**
   - Press **P** in the main menu
   - The application scans for available COM ports
   - Select the port number where your NanoVNA is connected
   - If unsure, try each port - the device identification is shown

2. **Set Frequency Range:**
   - Press **R** for Range configuration
   - Or press **M** for Manual measurement
   - Enter start frequency (in Hz, e.g., `144000000` for 144 MHz)
   - Enter end frequency (in Hz)
   - Enter step size (e.g., `10000` for 10 kHz steps)

3. **Perform First Measurement:**
   - Press **M** to start a measurement
   - Wait for the scan to complete
   - The results are now stored and ready for analysis

### Language Selection:

- The application automatically detects your Windows language
- English and German are supported
- Language can be changed in the configuration file

---

## 5. Main Menu Overview

The main menu provides access to all functions:

```
Main Menu Commands:
  U - Comfort Functions    Analysis toolkit (see section 7)
  A - Acoustic Analysis    Transform data into sound
  R - Range               Configure frequency scan
  M - Manual              Perform single measurement
  S - Summary             Show min/max/avg statistics
  T - Table               View measurement data
  E - Export              Save measurements to file
  L - Load                Load previously saved measurements
  K - Calibration         Calibration wizard
  P - Port                Select COM port
  D - Device Info         Battery voltage and device info
  I - Web Interface       Start/stop web interface for remote access
  W - Continuous Sweep    Toggle automatic measurements
  C - Customize Columns   Configure table display
  O - Options             Settings and configuration
  H - Help                Context-sensitive help
  Q - Quit                Exit application
```

**Note:** In German mode, some keys are adapted to German words (e.g., **Z** for "Zusammenfassung" instead of **S** for Summary).

---

## 6. Basic Operations

### Performing Measurements:

#### Quick Measurement:
1. Press **M** (Manual)
2. Enter start frequency, end frequency, and step size
3. Wait for completion
4. Results are stored automatically

#### Continuous Sweep Mode:
1. Configure range with **R** or **M**
2. Press **W** to enable continuous sweep
3. Press **A** to enter acoustic mode
4. Press **SPACE** to start - measurements update live

### Viewing Results:

#### Table View (**T**):
- Shows all data points in paginated table format
- Press **Page Up/Down** to navigate
- Press **C** to customize which columns to display
- Columns: Frequency, SWR, Return Loss, |Z|, R, X, Phase

#### Summary (**S**):
- Shows minimum, maximum, and average values
- Displays best frequency for each parameter
- Quick overview of measurement quality

### Device Information (**D**):
- Shows NanoVNA firmware version
- Displays battery voltage
- Useful for checking device connection

---

## 7. U-Menu: Analysis Toolkit

Press **U** in the main menu to access the comprehensive analysis toolkit. This menu provides specialized tools for different measurement scenarios.

### Antenna & Impedance Analysis (S11 Required):

**1. Band Suitability Check**
- Tests your antenna across 15 amateur radio bands (160m to 23cm)
- Shows SWR and impedance for each band
- Indicates which bands are suitable (SWR < threshold)

**2. Resonance Finder**
- Locates frequencies where SWR is minimum
- Useful for finding antenna resonance points
- Shows multiple resonances if present

**3. SWR Bandwidth**
- Calculates bandwidth where SWR stays below 1.5:1, 2:1, or 3:1
- Important for determining usable frequency range
- Reports bandwidth in kHz or MHz

**4. Feedpoint Impedance Analysis**
- Detailed impedance analysis at specific frequency
- Shows: R (resistance), X (reactance), |Z| (magnitude), phase
- Indicates if antenna is capacitive or inductive

**5. Matching Hints**
- Suggests impedance matching networks
- Provides component values (L and C)
- Helps achieve 50-ohm match

**6. Cable Length Estimation**
- Estimates cable length from phase response
- Requires velocity factor configuration
- Useful for cable identification

**7. Cable Fault Detection**
- Detects shorts, open circuits, or cable damage
- Uses Time Domain Reflectometry (TDR) principle
- Shows approximate distance to fault

### Cable & Filter Analysis (S21 Required):

**8. Cable Attenuation Measurement**
- Measures cable loss per meter or per 100 feet
- Requires through-calibrated S21 measurement
- Compare with manufacturer specifications

**9. Filter Analysis**
- Analyzes passband, stopband, and ripple
- Finds -3 dB cutoff frequencies
- Measures insertion loss

### Utilities:

**10. Before/After Comparison**
- Compare two measurement snapshots
- Useful for tuning adjustments
- Shows differences in all parameters

**11. Auto-Marker Placement**
- Automatically places markers at interesting points
- Minimum SWR, zero reactance, maximum S21
- Speeds up analysis

**12. Configuration**
- Set velocity factor for cable measurements (default: 0.66)
- Configure SWR threshold for band suitability
- Set cable loss specifications

---

## 8. Acoustic Analysis Mode

Transform your RF measurements into interactive multi-channel audio for intuitive analysis.

### Entering Acoustic Mode:
1. Perform a measurement (**M** or **R**)
2. Press **A** to enter acoustic mode
3. Press **SPACE** to start playback

### Audio Representation:

The application converts 5 parameters into simultaneous audio curves:

**Two Audio Engines Available:**

#### Synthesizer (Default):
1. **SWR** - Sine wave
2. **Return Loss** - Pure sine
3. **Impedance |Z|** - Triangle wave
4. **Reactance X** - Sawtooth (rising = inductive, falling = capacitive)
5. **Phase** - Sine wave

#### MIDI Engine:
1. **SWR** - String Ensemble
2. **Return Loss** - Church Organ
3. **Impedance |Z|** - Drawbar Organ
4. **Reactance X** - Violin
5. **Phase** - Lead 2 (Synth Lead)

Switch engines with **A** key in acoustic mode (opens audio configuration screen).

### Playback Features:

**Stereo Panning:**
- Left channel = Start frequency
- Right channel = End frequency
- Center = Middle frequencies

**Pitch Mapping:**
- Higher pitch = Higher parameter value
- Lower pitch = Lower parameter value
- Pitch range is automatically scaled

**Volume Control:**
- **1-5** keys: Toggle individual curves on/off
- **Ctrl + 1-5**: Decrease curve volume
- **Shift + 1-5**: Increase curve volume
- Volume range: 0-200%

### Playback Modes:

**Smooth Mode (Default):**
- Continuous sweep through all data points
- Represents overall curve shape
- Good for general overview

**Dotted Mode (Press T):**
- Plays discrete data points
- Uses intelligent downsampling (LTTB algorithm)
- Preserves peaks, valleys, and significant features
- Warning if time window too small for accurate representation

### Navigation:

**Arrow Keys:**
- **↑/↓** - Change jump width (1, 10, 100, 500, 1000 points)
- **←/→** - Navigate by jump width
- Real-time position and frequency display

**Playback Control:**
- **SPACE** - Play/Pause
- **S** - Stop and reset to beginning
- **F** - Freeze (pause without changing position)

**Time Window Adjustment:**
- **+** - Increase playback time (slower, more detail)
- **-** - Decrease playback time (faster, less detail)
- Typical range: 5-60 seconds

### Loop Markers:

**Setting Loop Markers:**
1. Navigate to desired start position
2. Press **L** to set left marker
3. Navigate to end position
4. Press **R** to set right marker

**Using Loops:**
- Press **O** to toggle loop mode on/off
- Press **Z** to toggle loop zoom (centers loop in stereo field)
- Press **I** to invert loop (play outside markers instead of inside)
- Playback repeats between markers
- Useful for detailed analysis of specific frequency ranges
- Press **C** for continuous replay mode

### Advanced Features:

**Y-Axis Ruler (Y key):**
- Plays ascending scale reference from min to max value
- Helps calibrate your perception of pitch-to-value mapping
- Useful for understanding the audio representation

**X-Axis Ruler (X key):**
- Toggles blips at each measurement point
- Helps identify position in the frequency sweep
- Useful for counting data points

**Status Line (N key):**
- Toggles detailed status information during playback
- Shows position, frequency, and measurement values
- Updates in real-time during playback

**Go To Menu (G key):**
- Jump to specific frequency or position
- Quick navigation to areas of interest

**Audio Configuration (A key):**
- Toggle between Synthesizer and MIDI engines
- Configure frequency range for Synthesizer
- Select MIDI instruments per curve
- Preview sounds before applying

### Smith Diagram Visualization:

Experience impedance matching through 3D spatial audio representation of the Smith Chart.

**What is Smith Diagram Visualization?**
- Acoustic representation of the Smith Chart using spatial audio
- Shows impedance position in stereo or surround field (7.1/5.1/2.0)
- Center = perfect 50Ω match, Edge = poor match
- Front position = inductive reactance (antenna too short)
- Back position = capacitive reactance (antenna too long)

**Audio Hardware Support:**
The application automatically detects your audio hardware capabilities:
- **Stereo (2.0):** Left/Right panning only
- **5.1 Surround:** Front, Rear, and Center speakers for spatial audio
- **7.1 Surround:** Full 360° spatial audio with side speakers
- **Dolby Atmos:** Height channels (detected but not yet used)

For best Smith Diagram spatial localization, use a 7.1 or 5.1 surround headset!

**Activating Smith Visualization:**
1. Enter acoustic analysis mode (**A**)
2. Press **V** to toggle Smith visualization on/off
3. Press **B** to select visualization mode (1-6)
4. Press **H** for Smith-specific help
5. Press **C** → **Smith Audio** to configure surround parameters

**Surround Sound Configuration:**
In Audio Configuration menu, press **S** (only available with 5.1/7.1 hardware):
- **Front/Back/Side Distance:** Adjust perceived speaker distance (50-200%)
- **Center Channel Strength:** Control center speaker prominence (0-100%)
- **F/B Separation:** Enhance front/back distinction (50-200%)
- **Side Emphasis:** Improve 90° localization (50-200%)
- **Fading Curves:** Choose spatial movement perception:
  - Linear: Equal perceived movement (default)
  - Logarithmic: More emphasis on center positions
  - Exponential: More emphasis on edge positions
  - Sine: Smooth, natural transitions

**Six Visualization Modes:**

1. **Cartesian (Default):**
   - Position in 3D space
   - Re(Γ) → Left/Right position
   - Im(Γ) → Front/Back position
   - ⭐ Best with 7.1/5.1 surround for full 360° localization
   - Easiest to understand for beginners

2. **Polar:**
   - Rotation around user
   - Angle ∠Γ → direction in surround field
   - Magnitude |Γ| → distance from center
   - ⭐ Excellent with surround for complete rotation perception
   - Good for tracking impedance loops

3. **Impedance Direct:**
   - Direct mapping of R and X
   - Resistance → Left/Right (centered at 50Ω)
   - Reactance → Front/Back (0 at center)
   - Works well with surround or stereo
   - Simpler alternative to Cartesian

4. **SWR Circles:**
   - Focus on constant SWR levels
   - Helps identify SWR contours
   - Useful for bandwidth analysis

5. **Time Domain:**
   - Standard acoustic analysis enhanced with Smith spatial cues
   - Combines traditional curves with position information
   - Best for detailed multi-parameter analysis

6. **Hybrid Multi-Layer:**
   - Multiple audio layers simultaneously
   - Advanced mode for experienced users
   - Richest information density

**Smith Audio Characteristics:**
- Subtle ambient pink noise positioned in spatial field
- Volume: 30% of main curves (configurable 10-100%)
- Works best with headphones
- Spatial position shows impedance location
- Sound moves toward center when approaching resonance
- With 7.1/5.1: Full 360° spatial positioning
- With stereo: Left/Right positioning only

**Tips for Using Smith Visualization:**
- Use a 7.1 or 5.1 surround headset for best experience
- Start with Cartesian mode (easiest)
- Configure surround parameters if front/back localization is unclear
- Smith cues work best with smooth playback mode
- Enable Smith visualization after starting playback
- Press **H** while Smith active for detailed help
- Listen for movement toward center (improving match)
- Front/back position indicates reactive component
- Adjust "F/B Separation" if front and back sound too similar

**Troubleshooting Spatial Audio:**
- If you only hear left/right panning: Check audio hardware detection in debug log (-d flag)
- If front/back sounds identical: Increase "F/B Separation" in surround config
- If 90° (side) positions are unclear: Increase "Side Emphasis" setting
- For subtle movements: Try "Logarithmic" or "Sine" fading curve

**Example Interpretations:**
- **Center sound** = Perfect 50Ω match (SWR 1.0)
- **Sound moves to center** = Approaching resonance
- **Sound in front** = Inductive reactance (antenna too short)
- **Sound in back** = Capacitive reactance (antenna too long)
- **Sound at edge** = High SWR, poor impedance match
- **Circling motion** = Impedance changing with frequency

**📖 Detailed Information on Smith Diagram Modes:**

For a comprehensive description of all six visualization modes, spatial relations, and listener-event relationships, see the separate file:
- **smith_modi_beschreibung.md** (in the doc directory, German only)
- Contains detailed technical explanations of each mode
- Describes axis crossings and movement patterns
- Provides tips for blind users on spatial orientation
- This file remains available as a quick reference

### Continuous Sweep Mode:
1. Enable continuous sweep (**W** in main menu)
2. Enter acoustic mode (**A**)
3. Press **SPACE** to start
4. Measurements update live while playing
5. Useful for real-time tuning

### Other Functions:
- **M** - Show current measurement info
- **E** - Export current data
- **H** - Show help (displays all available keys)
- **ESC** - Return to main menu

### Quick Key Reference:

**Playback:** SPACE (play/pause), F (freeze), S (stop), T (smooth/dotted toggle)  
**Navigation:** Arrows (←→ move, ↑↓ change jump width)  
**Time:** +/- (adjust playback speed)  
**Curves:** 1-5 (toggle), Ctrl+1-5 (volume down), Shift+1-5 (volume up)  
**Loop:** L (left marker), R (right marker), O (toggle), Z (zoom), I (invert), C (continuous)  
**Smith:** V (toggle visualization), B (change mode 1-6)  
**Tools:** Y (Y-ruler), X (X-ruler), N (status line), G (go to), A (audio config), M (measure), E (export), H (help)

---

## 9. Export and Import

### Exporting Measurements:

Press **E** in main menu or acoustic mode to open export menu.

**Export Options:**

**1. CSV Export**
- Standard CSV format with semicolon separator
- Contains: Frequency, SWR, Return Loss, |Z|, R, X, Phase
- Filename: `nanovna_YYYYMMDD_HHMMSS_startfreq_endfreq_step.csv`
- Location: `Export/` directory
- Can be opened in Excel, LibreOffice, or any spreadsheet application

**2. Text Export**
- Human-readable formatted text
- Contains all measurement data with units
- Includes measurement metadata (date, time, range)
- Filename: `nanovna_YYYYMMDD_HHMMSS_startfreq_endfreq_step.txt`
- Location: `Export/` directory

**3. Braille Graphics Export (File)**
- Exports acoustic curves as tactile graphics
- Format: `.brl` files for Index Braille printers
- 80x25 Braille cell raster (8-dot Braille)
- Compatible with Index Basic, Everest, V3, V4, V5 printers
- Select individual curves or all curves
- Uses ESC G/ESC E format with nibble encoding
- Filename: `nanovna_YYYYMMDD_HHMMSS_startfreq_endfreq_step.brl`
- Location: `Export/` directory

**4. Braille Direct Print**
- Print directly to connected Index Braille printer
- No intermediate file created
- Uses ESC Z graphics mode (vertical column encoding)
- Select curves to print
- Application lists all Windows printers
- Select Index Braille printer from list
- Produces tactile graphics of selected curves
- Optimized for Index V5 series printers

**Exporting with Loop Markers:**
- If loop markers are set (L and R in acoustic mode)
- Only data within the loop range is exported
- Useful for exporting specific frequency ranges

### Importing Measurements:

Press **L** in main menu to load previously saved measurements.

**Import Process:**
1. Press **L** for Load
2. Select file from list (shows all exports)
3. Data is loaded into memory
4. Can now use all analysis functions on imported data

**Supported Import Formats:**
- CSV files (.csv)
- Text files (.txt)
- Must be created by this application (correct format)

---

## 10. Web Interface

The web interface allows you to control the NanoVNA CLI application remotely from any web browser on your local network.

### Purpose and Use Cases:

- **Remote Control:** Operate the device from a smartphone or tablet
- **Field Work:** Place the NanoVNA at the antenna mast while controlling it from a safe distance
- **Accessibility:** Use the web interface with any screen reader that works with your browser
- **Multi-Device:** Access from any device on your local network

### Starting the Web Interface:

1. Press **I** from the main menu
2. Review the information displayed
3. Press **S** to start the web server
4. Note the URLs displayed:
   - **Local access:** `http://localhost:8080` (from the same computer)
   - **Network access:** `http://[Your-IP]:8080` (from other devices on your network)
5. Open the URL in any web browser

### Using the Web Interface:

- The web interface mirrors the terminal interface
- All keyboard commands work the same way
- Screen readers will read the content normally
- Audio output from acoustic mode plays on the server computer (not in the browser yet)

### Stopping the Web Interface:

1. Return to the main menu
2. Press **I** again
3. Press **S** to stop the web server

### Security Notes:

- **HTTP only:** No encryption (intended for local network use only)
- **No authentication:** Anyone on your network can connect
- **Local network only:** Not designed for internet access
- **Firewall:** Windows may ask for permission on first use

### Troubleshooting:

**Cannot connect from another device:**
- Check if both devices are on the same network
- Verify firewall settings on the server computer
- Use the correct IP address shown in the terminal

**Port already in use:**
- Another application might be using port 8080
- Close the other application or restart your computer

---

## 11. Calibration

Proper calibration is essential for accurate measurements.

### Calibration Types:

**S11 Calibration (One-Port):**
- Required for: Antenna measurements, SWR, impedance
- Standards: Open, Short, Load (50 ohm)

**S21 Calibration (Two-Port):**
- Required for: Cable loss, filter measurements
- Standards: Through connection, isolation

### Calibration Wizard (Press K):

**For S11 Measurements:**
1. Press **K** in main menu
2. Select frequency range (must match measurement range)
3. **Step 1:** Connect OPEN standard → measure
4. **Step 2:** Connect SHORT standard → measure
5. **Step 3:** Connect LOAD (50Ω) standard → measure
6. Calibration is now active

**For S21 Measurements:**
1. Configure for two-port measurement
2. **Through:** Connect ports directly → measure
3. **Isolation:** Leave ports unconnected → measure

### Saving and Recalling Calibration:

**Save Calibration:**
- After calibration wizard, data is automatically used
- Calibration can be saved to NanoVNA internal memory
- Use device commands in protocol mode

**Recall Calibration:**
- Calibration is lost when application restarts
- Re-run calibration wizard for each session
- Or recall from NanoVNA internal memory

### Calibration Tips:

- Use high-quality calibration standards
- Keep cable lengths consistent
- Tighten all connections firmly
- Avoid hand contact with connectors during measurement
- Recalibrate if changing frequency range
- Check calibration quality with known good device

---

## 12. Troubleshooting

### COM Port Issues:

**Problem:** Cannot find NanoVNA on COM port
- **Solution 1:** Check USB connection
- **Solution 2:** Try different COM ports (**P** menu shows all ports)
- **Solution 3:** Check Windows Device Manager for COM port number
- **Solution 4:** Verify NanoVNA is powered on (display lit)
- **Solution 5:** Try different USB cable

**Problem:** "Error opening serial port"
- **Solution:** Close other applications using the COM port
- **Solution:** Disconnect and reconnect NanoVNA
- **Solution:** Restart application

### Measurement Issues:

**Problem:** Strange or unrealistic values
- **Solution 1:** Check calibration (run **K** menu)
- **Solution 2:** Verify frequency range is appropriate
- **Solution 3:** Check connections (loose cables)
- **Solution 4:** Ensure antenna/device under test is properly connected

**Problem:** "No data available"
- **Solution 1:** Perform a measurement first (**M** or **R**)
- **Solution 2:** Check that measurement completed successfully
- **Solution 3:** Verify COM port is selected and device responding

### Audio Issues:

**Problem:** No sound in acoustic mode
- **Solution 1:** Check Windows volume settings
- **Solution 2:** Verify sound card is working (play test sound in Windows)
- **Solution 3:** Toggle curves on/off (keys 1-5)
- **Solution 4:** Increase curve volumes (Shift+1 to Shift+5)
- **Solution 5:** Try different audio engine (**Y** key)

**Problem:** Audio is distorted or crackling
- **Solution 1:** Reduce curve volumes (Ctrl+1 to Ctrl+5)
- **Solution 2:** Close other audio applications
- **Solution 3:** Adjust playback time (+ / - keys for longer/shorter duration)

### Braille Printing Issues:

**Problem:** "No printers found"
- **Solution:** Install at least one printer in Windows
- **Solution:** Check Windows Devices and Printers

**Problem:** "Failed to open printer"
- **Solution 1:** Verify printer is online and ready
- **Solution 2:** Check printer name matches exactly
- **Solution 3:** Try printing Windows test page first
- **Solution 4:** Check printer permissions in Windows

**Problem:** Print job successful but nothing prints
- **Solution 1:** Check printer queue (Devices and Printers)
- **Solution 2:** Verify paper is loaded
- **Solution 3:** Check printer-specific error indicators

**Problem:** Garbled output on Braille printer
- **Solution 1:** Try file export (option 3) instead of direct print
- **Solution 2:** Verify printer model compatibility (Index printers)
- **Solution 3:** Check debug log for errors

### Application Issues:

**Problem:** Application crashes or freezes
- **Solution 1:** Run with debug logging: `nanovna-cli.exe -d`
- **Solution 2:** Check `logs/` directory for error messages
- **Solution 3:** Report issue with debug log

**Problem:** Screen reader compatibility issues
- **Solution:** Application uses standard console output
- **Solution:** Most screen readers work well with console applications
- **Solution:** Use context-sensitive help (**H** key)

### Getting Help:

If problems persist:
1. Enable debug logging: `nanovna-cli.exe -d`
2. Reproduce the problem
3. Check log files in `logs/` directory:
   - `debug_YYYYMMDD_HHMMSS.txt` (general log)
   - `debug_comm_YYYYMMDD_HHMMSS.txt` (serial communication)
4. Report the issue with log files (see Beta Testing Guide)

---

## 13. File Structure

Understanding the file structure helps with troubleshooting and organization.

### Directory Structure:

```
nanovna-cli.exe                Main executable
│
├── Languages/                 Language files
│   ├── eng.lng               English translation
│   └── deu.lng               German translation
│
├── config/                    Configuration files
│   ├── app_settings.cfg      Application settings (auto-saved)
│   ├── command_templates.cfg NanoVNA command templates
│   └── cables.cfg            Cable specifications
│
├── bandplans/                 Amateur radio band definitions
│   ├── usa.ini               US amateur bands
│   └── deu.ini               German amateur bands
│
├── logs/                      Log files (auto-generated)
│   ├── debug_*.txt           General debug logs
│   └── debug_comm_*.txt      Serial communication logs
│
└── Export/                    Exported measurements
    ├── *.csv                 CSV exports
    ├── *.txt                 Text exports
    └── *.brl                 Braille graphics exports
```

### Configuration Files:

**app_settings.cfg**
- Automatically saved on exit
- Contains last used settings:
  - Language preference
  - Last COM port
  - Last frequency range
  - Velocity factor
  - SWR thresholds
  - Audio settings
- Can be edited manually (text format)
- Delete to reset to defaults

**command_templates.cfg**
- NanoVNA serial commands
- Should not need modification
- Format: `COMMAND=command_string`

**cables.cfg**
- Cable specifications for loss calculations
- Format: `CABLE_NAME=loss_per_meter,velocity_factor`
- Can add custom cables

### Log Files:

**debug_YYYYMMDD_HHMMSS.txt**
- General application log
- Contains all operations and errors
- Useful for troubleshooting
- Created when using `-d` flag

**debug_comm_YYYYMMDD_HHMMSS.txt**
- Serial communication log
- Shows all commands sent to NanoVNA
- Shows all responses received
- Useful for device communication issues

### Export Files:

All exports use filename format:
`nanovna_YYYYMMDD_HHMMSS_startfreq_endfreq_step.ext`

Example:
`nanovna_20260125_143522_144000000_146000000_10000.csv`
- Date: 2026-01-25
- Time: 14:35:22
- Start: 144.000 MHz
- End: 146.000 MHz
- Step: 10 kHz

---

## Quick Reference Card

### Essential Commands:
```
P - Select COM Port
M - Measure
A - Acoustic Mode
U - Analysis Toolkit
E - Export
H - Help
Q - Quit
```

### Acoustic Mode:
```
SPACE - Play/Pause
←/→   - Navigate
1-5   - Toggle curves
+/-   - Adjust speed
L/R   - Set loop markers
B     - Back to menu
```

### U-Menu Analysis:
```
1 - Band Suitability
2 - Resonance Finder
3 - SWR Bandwidth
4 - Feedpoint Impedance
5 - Matching Hints
```

---

## About Screen Reader Accessibility

NanoVNA CLI Accessible was designed from the ground up to be accessible:

- **Standard console output:** Compatible with all screen readers
- **Clear, descriptive messages:** No cryptic codes or symbols
- **Context-sensitive help:** Press **H** in any menu
- **Logical navigation:** Consistent keyboard shortcuts
- **Audio representation:** "See" RF curves through sound
- **Braille output:** Tactile graphics for detailed analysis

---

## Getting Support

For questions, bug reports, or feedback during beta testing, please refer to the Beta Testing Guide included with this distribution.

**73 DE DO9RE**

---

*NanoVNA CLI Accessible - Making RF measurements accessible to everyone.*
