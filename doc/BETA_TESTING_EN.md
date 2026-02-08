# Beta Testing Guide - NanoVNA CLI Accessible

**Version:** Beta  
**Date:** January 2026  
**Language:** English

---

## Table of Contents

1. [About Beta Testing](#1-about-beta-testing)
2. [What Should Be Tested](#2-what-should-be-tested)
3. [How to Enable Debug Output](#3-how-to-enable-debug-output)
4. [Understanding Debug Logs](#4-understanding-debug-logs)
5. [How to Report Feedback](#5-how-to-report-feedback)
6. [Test Scenarios](#6-test-scenarios)
7. [Known Issues and Limitations](#7-known-issues-and-limitations)
8. [Contact Information](#8-contact-information)

---

## 1. About Beta Testing

Thank you for participating in the beta test of NanoVNA CLI Accessible! Your feedback is invaluable for improving this application.

### Goals of Beta Testing:
- Verify functionality across different Windows versions
- Test compatibility with various screen readers
- Identify bugs and usability issues
- Validate acoustic analysis and Braille export features
- Gather feedback on the user interface and documentation

### What You Received:
- **Ready-to-use build** (no compilation needed)
- **User Manual** (this explains how to use the application)
- **This Beta Testing Guide** (explains what and how to test)

### Time Commitment:
- **Minimum:** 2-3 hours of testing
- **Recommended:** 5-10 hours for comprehensive testing
- Test at your own pace over several days

---

## 2. What Should Be Tested

### Priority 1: Core Functionality (Essential)

**Device Communication:**
- [ ] COM port detection and selection
- [ ] Device connection and identification
- [ ] Battery voltage reading
- [ ] Command execution and response

**Basic Measurements:**
- [ ] Single frequency sweep (S11 parameter)
- [ ] Different frequency ranges (HF, VHF, UHF)
- [ ] Different step sizes (small and large)
- [ ] Measurement data accuracy
- [ ] Data table display

**User Interface:**
- [ ] Main menu navigation
- [ ] Help system (context-sensitive)
- [ ] Language switching (English/German)
- [ ] Error messages clarity
- [ ] Screen reader compatibility

### Priority 2: Analysis Features (Important)

**U-Menu Analysis Toolkit:**
- [ ] Band suitability check
- [ ] Resonance finder
- [ ] SWR bandwidth calculation
- [ ] Feedpoint impedance analysis
- [ ] Matching hints
- [ ] Cable length estimation
- [ ] Cable fault detection
- [ ] Filter analysis (if S21 available)

**Summary and Statistics:**
- [ ] Minimum/maximum value detection
- [ ] Average calculations
- [ ] Frequency of best values

### Priority 3: Acoustic Analysis (Important)

**Basic Audio Playback:**
- [ ] Acoustic mode entry
- [ ] Play/pause/stop controls
- [ ] Audio engine selection (Synthesizer/MIDI)
- [ ] Individual curve toggling (keys 1-5)
- [ ] Volume control (Shift/Ctrl + 1-5)
- [ ] Audio clarity and quality

**Advanced Audio Features:**
- [ ] Smooth vs. dotted mode
- [ ] Navigation with arrow keys
- [ ] Jump width adjustment
- [ ] Playback time adjustment (+/- keys)
- [ ] Loop markers (L/R keys)
- [ ] Loop mode functionality
- [ ] Continuous sweep with live updates

**Audio Quality:**
- [ ] Stereo panning (left = start, right = end)
- [ ] Pitch represents value correctly
- [ ] No audio artifacts or distortion
- [ ] MIDI instrument selection and preview

### Priority 4: Data Management (Moderate)

**Export Functions:**
- [ ] CSV export functionality
- [ ] Text export functionality
- [ ] File naming convention
- [ ] Export with loop markers
- [ ] Data integrity in exported files

**Import Functions:**
- [ ] Loading previously saved measurements
- [ ] File selection from list
- [ ] Data integrity after import
- [ ] Analysis on imported data

**Braille Export:**
- [ ] Braille file export (.brl format)
- [ ] Direct printing to Index Braille printer (if available)
- [ ] Printer detection and selection
- [ ] Curve selection for export/print
- [ ] Output quality (if Braille printer available)

**Web Interface:**
- [ ] Starting the web interface (I key, then S)
- [ ] Accessing from localhost (same computer)
- [ ] Accessing from another device on the network
- [ ] All menu functions work through browser
- [ ] Screen reader compatibility in browser
- [ ] Stopping the web interface
- [ ] Port conflict handling
- [ ] Connection stability

### Priority 5: Advanced Features (Optional)

**Calibration:**
- [ ] Calibration wizard flow
- [ ] Open/Short/Load calibration
- [ ] Calibration effect on measurements
- [ ] Calibration data persistence

**Continuous Sweep:**
- [ ] Enabling/disabling continuous sweep
- [ ] Live measurement updates
- [ ] Performance with acoustic mode
- [ ] Stopping continuous sweep

**Table Customization:**
- [ ] Column selection
- [ ] Pagination
- [ ] Data formatting

---

## 3. How to Enable Debug Output

Debug output is **essential for beta testing**. It helps identify and fix bugs.

### Enabling Debug Mode:

**Method 1: Command Line (Recommended)**
1. Open Command Prompt or PowerShell
2. Navigate to the application directory:
   ```
   cd C:\Path\To\NanoVNA-CLI
   ```
3. Run with debug flag:
   ```
   nanovna-cli.exe -d
   ```

**Method 2: Create Shortcut**
1. Right-click on `nanovna-cli.exe`
2. Select "Create shortcut"
3. Right-click the shortcut → Properties
4. In "Target" field, add `-d` at the end:
   ```
   "C:\Path\To\nanovna-cli.exe" -d
   ```
5. Click OK
6. Use this shortcut for testing

**Method 3: Batch File**
Create a file named `start-debug.bat` in the application directory:
```batch
@echo off
nanovna-cli.exe -d
pause
```
Double-click this file to start with debug logging.

### What Debug Mode Does:

- Creates detailed log files in `logs/` directory
- Records all operations, commands, and responses
- Includes timestamps for each event
- Logs errors with detailed information
- Does NOT significantly slow down the application
- Does NOT affect functionality

**Important:** Always use debug mode during beta testing!

---

## 4. Understanding Debug Logs

Debug logs are saved in the `logs/` directory with timestamped filenames.

### Log File Types:

**1. debug_YYYYMMDD_HHMMSS.txt**
- **General application log**
- Contains: Menu actions, function calls, errors, warnings
- Use for: General troubleshooting, understanding program flow

**2. debug_comm_YYYYMMDD_HHMMSS.txt**
- **Serial communication log**
- Contains: All commands sent to NanoVNA, all responses received
- Use for: Device communication issues, measurement problems

### Reading Debug Logs:

**Log Entry Format:**
```
[TIMESTAMP] [MODULE] Message text
```

Example:
```
[2026-01-25 14:35:22] [SERIAL] Opening port COM4
[2026-01-25 14:35:22] [SERIAL] Port opened successfully
[2026-01-25 14:35:23] [PROTOCOL] Sending command: info
[2026-01-25 14:35:23] [PROTOCOL] Response: NanoVNA-H4 v1.0.70
```

**Important Log Modules:**

- **[SERIAL]** - Serial port operations
- **[PROTOCOL]** - NanoVNA command/response
- **[AUDIO]** - Audio synthesis and playback
- **[BRAILLE_PRINTER]** - Braille export and printing
- **[EXPORT]** - File export operations
- **[IMPORT]** - File import operations
- **[U_MENU]** - Analysis toolkit functions
- **[ERROR]** - Error messages
- **[WARNING]** - Warning messages

### Common Log Patterns:

**Successful Operation:**
```
[SERIAL] Opening port COM4
[SERIAL] Port opened successfully
[PROTOCOL] Device identified: NanoVNA-H4
```

**Error Pattern:**
```
[ERROR] Failed to open port COM4: Access denied
[WARNING] Please close other applications using this port
```

**Measurement Process:**
```
[PROTOCOL] Starting scan from 144000000 to 146000000
[PROTOCOL] Step size: 10000 Hz
[PROTOCOL] Collecting data point 1/200
[PROTOCOL] Collecting data point 2/200
...
[PROTOCOL] Scan completed. 200 points collected.
```

### What to Look For:

1. **Error messages** - Indicated by [ERROR] tag
2. **Warning messages** - Indicated by [WARNING] tag
3. **Unusual patterns** - Repeated errors, timeouts
4. **Failed operations** - "Failed to...", "Unable to...", "Error:"
5. **Unexpected values** - Values that don't make sense

---

## 5. How to Report Feedback

Your feedback is crucial! Here's how to report different types of issues.

### Bug Report Template:

```
BUG REPORT

Title: [Short description of the bug]

Description:
[Detailed description of what went wrong]

Steps to Reproduce:
1. [First step]
2. [Second step]
3. [Third step]

Expected Behavior:
[What should have happened]

Actual Behavior:
[What actually happened]

System Information:
- Windows Version: [e.g., Windows 10 64-bit]
- Screen Reader: [e.g., NVDA 2023.3, JAWS 2024, None]
- NanoVNA Model: [e.g., NanoVNA-H4]
- Firmware Version: [from Device Info menu]

Debug Log:
[Attach or paste relevant sections from debug log]
[Include: logs/debug_YYYYMMDD_HHMMSS.txt]

Additional Notes:
[Any other relevant information]
```

### Feature Feedback Template:

```
FEATURE FEEDBACK

Feature Name: [e.g., Acoustic Analysis Mode]

Rating: [1-5, where 5 = excellent, 1 = poor]

What Works Well:
- [Positive point 1]
- [Positive point 2]

What Could Be Improved:
- [Improvement 1]
- [Improvement 2]

Suggestions:
[Any specific suggestions for improvement]
```

### Usability Feedback Template:

```
USABILITY FEEDBACK

Area: [e.g., Main Menu, U-Menu, Acoustic Mode]

Ease of Use: [1-5, where 5 = very easy, 1 = very difficult]

Clarity: [1-5, where 5 = very clear, 1 = very confusing]

Comments:
[Detailed feedback about usability]

Screen Reader Notes:
[Specific feedback about screen reader experience]
```

### Documentation Feedback Template:

```
DOCUMENTATION FEEDBACK

Document: [User Manual / Beta Testing Guide]

Section: [Which section]

Issue:
[What's unclear, missing, or incorrect]

Suggestion:
[How to improve it]
```

### What to Include:

**Always Include:**
1. Your Windows version
2. Screen reader used (if any)
3. NanoVNA model and firmware version
4. Steps to reproduce the issue
5. Relevant debug log sections

**Include When Relevant:**
1. Screenshots (if visual issue)
2. Exported data files (if data issue)
3. Audio recordings (if audio issue)
4. Full debug log file (for crashes or serious bugs)

### Where to Send Feedback:

[Contact information will be provided separately by DO9RE]

**File Organization:**
- Create a folder with your name and date
- Include debug logs, screenshots, exported files
- Compress (ZIP) before sending if large

---

## 6. Test Scenarios

Here are specific test scenarios to work through. Try to complete as many as possible.

### Scenario 1: First-Time User Setup (15 minutes)

**Goal:** Verify the initial setup experience.

1. Extract the beta distribution to a new folder
2. Start the application with debug logging
3. **Test:** COM port selection
   - Does the application find your NanoVNA?
   - Is the device identified correctly?
   - Is the process clear and intuitive?
4. **Test:** First measurement
   - Configure frequency range (e.g., 144-146 MHz)
   - Perform measurement
   - View results in table
5. **Test:** Help system
   - Press H in main menu
   - Press H in acoustic mode
   - Is the help clear and useful?

**Report:** Initial setup experience, any confusion or difficulties.

### Scenario 2: Antenna Analysis (30 minutes)

**Goal:** Test antenna analysis features.

**Prerequisites:** NanoVNA connected, antenna or dummy load attached.

1. **Perform S11 measurement** on an antenna (any band)
2. **Test U-Menu → Band Suitability Check (Option 1)**
   - Does it correctly identify suitable bands?
   - Are SWR values reasonable?
3. **Test U-Menu → Resonance Finder (Option 2)**
   - Does it find the resonance point(s)?
   - Are frequencies accurate?
4. **Test U-Menu → SWR Bandwidth (Option 3)**
   - Calculate 2:1 bandwidth
   - Are values reasonable?
5. **Test U-Menu → Feedpoint Impedance (Option 4)**
   - Check impedance at resonance
   - Are R, X, |Z|, and phase displayed correctly?
6. **Test U-Menu → Matching Hints (Option 5)**
   - For off-resonance frequency
   - Are suggestions reasonable?

**Report:** Accuracy of analysis, usefulness of results, any bugs.

### Scenario 3: Acoustic Analysis - Basic (20 minutes)

**Goal:** Test basic acoustic analysis functionality.

**Prerequisites:** Working sound card, measurement data available.

1. **Perform a measurement** with clear features (e.g., antenna with resonance)
2. **Enter acoustic mode** (press A)
3. **Test basic playback:**
   - Press SPACE to play
   - Press SPACE to pause
   - Press S to stop
   - Press F to freeze
4. **Test curve toggling:**
   - Press 1-5 to toggle curves on/off
   - Can you hear the difference?
5. **Test volume control:**
   - Shift+1 to increase SWR volume
   - Ctrl+1 to decrease SWR volume
   - Try other curves
6. **Test audio engine switch:**
   - Press Y to open audio configuration
   - Switch to MIDI engine
   - Try different instruments
7. **Listen for stereo panning:**
   - Does left ear represent start frequency?
   - Does right ear represent end frequency?

**Report:** Audio quality, clarity of curves, usability, any bugs.

### Scenario 4: Acoustic Analysis - Advanced (20 minutes)

**Goal:** Test advanced acoustic features.

**Prerequisites:** Measurement data available.

1. **Test smooth vs. dotted mode:**
   - In acoustic mode, press T to toggle
   - Notice the difference
   - Which is more useful?
2. **Test navigation:**
   - Use ↑/↓ to change jump width
   - Use ←/→ to navigate
   - Does position update correctly?
3. **Test time adjustment:**
   - Press + to slow down playback
   - Press - to speed up playback
   - Find comfortable speed
4. **Test loop markers:**
   - Navigate to a region of interest
   - Press L to set left marker
   - Navigate to end of region
   - Press R to set right marker
   - Press O to enable loop
   - Does it loop correctly?
5. **Test continuous replay:**
   - Press C for continuous replay
   - Does it play repeatedly?

**Report:** Usefulness of features, any bugs, suggestions.

### Scenario 5: Continuous Sweep Mode (15 minutes)

**Goal:** Test live measurement updates during playback.

**Prerequisites:** Antenna or adjustable load.

1. **Configure frequency range** (e.g., around antenna resonance)
2. **Enable continuous sweep** (press W in main menu)
3. **Enter acoustic mode** (press A)
4. **Start playback** (press SPACE)
5. **Adjust antenna or load** while playing
   - Can you hear changes in real-time?
   - Is the update rate acceptable?
6. **Test stopping continuous sweep:**
   - Return to main menu
   - Press W to disable
   - Does it stop correctly?

**Report:** Performance, usefulness, any lag or issues.

### Scenario 6: Export and Import (15 minutes)

**Goal:** Test data export and import functionality.

**Prerequisites:** Measurement data available.

1. **Test CSV export:**
   - Press E in main menu
   - Select option 1 (CSV)
   - Find file in Export/ directory
   - Open in spreadsheet application
   - Verify data integrity
2. **Test text export:**
   - Press E in main menu
   - Select option 2 (Text)
   - Open in text editor
   - Is format readable?
3. **Test import:**
   - Press L in main menu
   - Select a previously exported file
   - Verify data loaded correctly
   - Try analysis on imported data
4. **Test export with loop markers:**
   - In acoustic mode, set loop markers (L and R)
   - Export data
   - Verify only loop range is exported

**Report:** File format quality, import success, any issues.

### Scenario 7: Braille Export (15-30 minutes)

**Goal:** Test Braille file export and direct printing.

**Prerequisites:** Measurement data available. Index Braille printer for direct print test.

**Part A: File Export (all testers)**
1. **Enter acoustic mode** (press A)
2. **Press E for export**
3. **Select option 3** (Braille file export)
4. **Select curves** (e.g., press "1 2" for SWR and Return Loss, or "a" for all)
5. **Check Export/ directory** for .brl file
6. **Report:** Was process clear? Any errors?

**Part B: Direct Printing (only if Index Braille printer available)**
1. **Verify printer is installed** in Windows and online
2. **Enter acoustic mode** (press A)
3. **Press E for export**
4. **Select option 4** (Direct print)
5. **Select curves** to print
6. **Select printer** from list
7. **Wait for printing** to complete
8. **Examine output:** Are curves recognizable tactilely?
9. **Report:** Print quality, any issues, printer model tested

**Part C: Error Handling (all testers)**
1. **Try to cancel** during curve selection (press Enter without selection)
2. **Try invalid inputs** during printer selection
3. **Report:** Are error messages clear?

**Check debug log** for Braille-related entries:
- `[BRAILLE_PRINTER]` entries
- Printer enumeration
- Data generation
- Print job status

### Scenario 8: Screen Reader Compatibility (30 minutes)

**Goal:** Test compatibility with your screen reader.

**Prerequisites:** Screen reader running (NVDA, JAWS, or other).

1. **Test all menus:**
   - Main menu navigation
   - U-menu navigation
   - Acoustic mode
   - Export/import dialogs
   - Calibration wizard
2. **Test prompts and messages:**
   - Are prompts clear?
   - Are error messages spoken correctly?
   - Is status information accessible?
3. **Test data presentation:**
   - Table view readability
   - Summary statistics
   - Measurement results
4. **Test help system:**
   - Context-sensitive help (H key)
   - Is help information clear?
5. **Note specific issues:**
   - Information not spoken
   - Confusing wording
   - Missing labels

**Report:** Screen reader name/version, what works well, what needs improvement.

### Scenario 9: Calibration (20 minutes)

**Goal:** Test calibration functionality.

**Prerequisites:** Calibration standards (Open, Short, Load).

1. **Enter calibration wizard** (press K)
2. **Follow prompts:**
   - Set frequency range
   - Connect OPEN standard → measure
   - Connect SHORT standard → measure
   - Connect LOAD standard → measure
3. **Perform measurement** after calibration
4. **Compare with uncalibrated measurement**
   - Is there noticeable improvement?
   - Are values more realistic?
5. **Test recalling calibration:**
   - Restart application
   - Is calibration lost? (expected)
   - Re-run calibration
   - Does it work consistently?

**Report:** Calibration process clarity, effectiveness, any issues.

### Scenario 10: Stress Testing (15 minutes)

**Goal:** Test application stability and error handling.

1. **Test large frequency ranges:**
   - Scan from 1 MHz to 900 MHz
   - Is performance acceptable?
   - Does acoustic mode work with many points?
2. **Test very small step sizes:**
   - Small range with many points
   - Does application slow down?
   - Any memory issues?
3. **Test rapid commands:**
   - Quickly press different keys
   - Switch between modes rapidly
   - Does application remain stable?
4. **Test invalid inputs:**
   - Enter non-numeric values when numbers expected
   - Enter out-of-range frequencies
   - Are error messages clear?
5. **Test disconnecting NanoVNA:**
   - Disconnect device during measurement
   - What happens?
   - Is error handling graceful?

**Report:** Any crashes, freezes, or unexpected behavior.

### Scenario 11: Web Interface (20 minutes)

**Goal:** Test remote access functionality via web interface.

**Prerequisites:** NanoVNA connected, another device (smartphone/tablet) on same network.

1. **Start web interface:**
   - Press I from main menu
   - Press S to start
   - Note the URLs displayed (localhost and network)
   - Check if Windows Firewall prompt appears
2. **Test local access:**
   - Open browser on same computer
   - Navigate to http://localhost:8080
   - Does the terminal interface appear?
   - Try navigating main menu
3. **Test network access:**
   - Open browser on another device (smartphone/tablet)
   - Navigate to the network URL shown (e.g., http://192.168.1.x:8080)
   - Does connection work?
   - Is the interface responsive?
4. **Test functionality through web:**
   - Try performing a measurement via web interface
   - Test acoustic mode (note: audio plays on server, not browser)
   - Test export functions
   - Test U-menu functions
5. **Test screen reader compatibility:**
   - Use screen reader in browser (NVDA, JAWS, browser's built-in)
   - Navigate through menus
   - Are all elements accessible?
6. **Stop web interface:**
   - Return to main menu (ESC)
   - Press I again
   - Press S to stop
   - Verify web interface stops

**Report:** Connection stability, screen reader compatibility in browser, any issues with remote control, suggestions for improvement.

---

## 7. Known Issues and Limitations

These are known limitations that don't need to be reported:

### Platform Limitations:
- **Windows-only:** Application cannot run on Linux or macOS
- **NanoVNA-H4 only:** Designed specifically for this model
- **Fixed baud rate:** 9600 baud, no auto-detection

### Measurement Limitations:
- **Calibration not saved:** Must recalibrate each session
- **Single sweep type:** S11 or S21, not simultaneous
- **Limited memory:** Very large datasets may be slow

### Audio Limitations:
- **Windows audio API:** Requires working Windows sound system
- **MIDI device:** Some MIDI instruments may not be available on all systems
- **Audio quality:** Depends on sound card quality

### Braille Limitations:
- **Index printers only:** Direct print optimized for Index V5 series
- **Graphics mode:** Uses tactile graphics, not Braille text
- **Windows printers:** Requires printer installed in Windows

### User Interface Limitations:
- **Console only:** No graphical interface
- **Keyboard only:** No mouse support (by design)
- **English/German only:** No other languages yet

---

## 8. Contact Information

### Reporting Feedback:

**Primary Contact:** DO9RE

**What to Send:**
1. Bug reports using templates from Section 5
2. Feature feedback using templates from Section 5
3. Debug log files from `logs/` directory
4. Screenshots (if relevant)
5. Exported data files (if relevant)

**File Organization:**
```
YourName_YYYYMMDD/
├── feedback_summary.txt
├── logs/
│   ├── debug_20260125_143522.txt
│   └── debug_comm_20260125_143522.txt
├── screenshots/
│   └── screenshot1.png
└── exports/
    └── problematic_export.csv
```

Compress to ZIP before sending.

### Response Time:

- **Bug reports:** Will be acknowledged within 48 hours
- **Questions:** Will be answered as soon as possible
- **Feature requests:** Will be collected for future versions

### Beta Test Duration:

- **Start:** [To be announced by DO9RE]
- **End:** [To be announced by DO9RE]
- **Final feedback deadline:** [To be announced by DO9RE]

---

## Thank You!

Your participation in this beta test is greatly appreciated. Your feedback will directly improve NanoVNA CLI Accessible and make it more useful for the amateur radio community.

**73 DE DO9RE**

---

## Quick Testing Checklist

Use this as a quick reference:

**Essential Tests:**
- [ ] COM port selection and device connection
- [ ] Perform basic S11 measurement
- [ ] View results in table
- [ ] Try U-Menu band suitability check
- [ ] Enter acoustic mode and play measurement
- [ ] Toggle curves on/off (keys 1-5)
- [ ] Export to CSV
- [ ] Test help system (H key)

**If Time Permits:**
- [ ] Test MIDI audio engine
- [ ] Test loop markers in acoustic mode
- [ ] Test continuous sweep mode
- [ ] Test import function
- [ ] Test Braille export (file or direct print)
- [ ] Test calibration wizard
- [ ] Test all U-Menu analysis functions

**Screen Reader Users:**
- [ ] Test all menus with screen reader
- [ ] Verify all prompts are spoken
- [ ] Check table readability
- [ ] Report any accessibility issues

**Always:**
- [ ] Run with debug logging (`-d` flag)
- [ ] Save debug logs from `logs/` directory
- [ ] Document any issues with steps to reproduce
- [ ] Include system information in reports

---

*Thank you for helping make NanoVNA CLI Accessible better for everyone!*
