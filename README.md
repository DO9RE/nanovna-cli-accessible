# nanoVNA-cli-accessible

Accessible console application for controlling, measuring, and audibly visualizing the NanoVNA-H4.  
Barrierefreie Konsolenanwendung zur Steuerung, Messung und auditiven Darstellung des NanoVNA-H4.

> **⚠️ WINDOWS-ONLY APPLICATION**  
> This application requires Windows and cannot be built on Linux/macOS.  
> Uses Windows-specific APIs: winmm, Windows API, conio.h, SetupAPI.

## Overview

nanoVNA-cli-accessible is a specialized tool for amateur radio operators and RF engineers, making VNA measurements accessible through:
- **Audio representation** of RF parameters (SWR, impedance, reactance, phase)
- **Screen-reader compatibility** with context-sensitive help
- **Comprehensive analysis tools** for antennas, cables, and filters
- **Bilingual interface** (English/Deutsch)

## Key Features

### 🎯 U-Menu: Analysis Toolkit
Press **U** for instant access to specialized measurement tools:

**Antenna & Impedance (S11):**
1. Band Suitability Check - Test performance across 15 amateur bands (160m-23cm)
2. Resonance Finder - Locate minimum SWR frequencies
3. SWR Bandwidth - Calculate 1.5:1, 2:1, 3:1 bandwidth ranges
4. Feedpoint Impedance - Detailed R, X, |Z|, phase analysis
5. Matching Hints - Impedance matching network suggestions
6. Cable Length - Estimate length from phase response
7. Cable Fault Detection - Detect shorts, opens, damage

**Cable & Filter (S21):**
8. Cable Attenuation - Measure loss per meter (dB/m, dB/100ft)
9. Filter Analysis - Analyze passband, ripple, -3dB points

**Utilities:**
10. Before/After Comparison - Compare two measurement snapshots
11. Auto-Marker Placement - Set markers at min SWR, X=0, max S21
12. Configuration - Velocity factor, SWR threshold, cable loss

### 🎵 Acoustic Analysis Mode
Transform measurements into interactive multi-channel audio:

**Dual Audio Engines:**
- **Synthesizer** (default): Waveform-based synthesis (sine, triangle, sawtooth)
- **MIDI**: 128 General MIDI instruments with real-time preview
- Switch engines via **Y** key in acoustic mode

**Five Simultaneous Curves:**
1. **SWR** - Sine wave / String Ensemble
2. **Return Loss** - Pure sine / Drawbar Organ
3. **Impedance |Z|** - Triangle wave / Church Organ
4. **Reactance X** - Sawtooth / Violin (rising=inductive, falling=capacitive)
5. **Phase** - Sine / Synth Lead

**Playback Features:**
- Stereo panning: left=start frequency, right=end frequency
- Pitch mapping: higher pitch = higher value
- Individual curve volume control (0-200%)
- Two modes: Smooth (continuous) or Dotted (discrete points)
  - **Intelligent downsampling**: Uses LTTB (Largest Triangle Three Buckets) algorithm
  - Preserves curve characteristics: peaks, valleys, and significant bends
  - Automatic warnings when time window is too small for accurate representation
- Loop markers for focused analysis
- Continuous sweep mode with live updates
- Position scrubbing with arrow keys

### 📊 Measurement & Data
- S11 and S21 parameter support
- Interactive frequency range configuration
- Multi-sweep stitching for wide frequency spans
- Calibration flow (Open/Short/Load)
- CSV/TXT/Braille export with timestamps
- Import previously saved measurements
- Paginated table view

#### Braille Export
Export acoustic analysis curves as tactile graphics for Index Braille printers:
- **Format**: `.brl` files with Index Braille printer escape sequences OR direct printing
- **Features**:
  - Select individual curves or all at once (SWR, Return Loss, |Z|, Reactance, Phase)
  - 80x25 Braille cell raster graphics (2x4 dot cells, 8-dot Braille)
  - Compatible with Index Braille printer models (Basic, Everest, V3, V4, V5)
  - Standard escape sequences: `ESC G` (enter graphics), `ESC E` (exit graphics), `0x1A` (end document)
  - Includes header with frequency range and curve information
  - **NEW: Direct printing** - Print directly to Windows printers without saving files
- **Usage**: 
  - In acoustic mode, press **E** → select option **3** (save file) or **4** (direct print)
  - For direct print: Choose curves → Select printer from list → Print
  - Automatic printer enumeration with full debug logging
- **File Output**: `Export/nanovna_YYYYMMDD_HHMMSS_startfreq_endfreq_step.brl`
- **Direct Print**: RAW data sent directly to Windows printer spooler (requires Windows)

### 🔌 Device Management
- COM port selection with device identification
- Device information and battery voltage query
- Calibration save/recall

### 🌐 Web Interface (Remote Access)
Access the application from any browser on your local network:
- **Full keyboard control** via browser (desktop, smartphone, tablet)
- **Screen reader compatible** with ARIA live regions
- **Real-time output streaming** via Server-Sent Events
- **Accessible interface** - no visual dependencies
- **Local network only** - HTTP, no authentication required
- Press **I** in main menu to start/stop web interface
- Default port: 8080 (http://localhost:8080 or http://[your-ip]:8080)

**Use Cases:**
- Control from smartphone while at antenna mast
- Remote operation from tablet
- Screen reader access from any device
- Multiple observers viewing same session

**Debugging:**
- Comprehensive debug logging for troubleshooting
- See WEB_INTERFACE_DEBUGGING.md for complete guide
- German guide: WEB_INTERFACE_DEBUGGING_DE.md

**Security Note:** Web interface is HTTP-only, intended for local network use only.

## Quick Start

### Installation
Requires Windows with MSYS2/MinGW-w64 or Visual Studio.

```bash
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
mingw32-make -j4
```

### Usage

**Interactive mode:**
```bash
nanovna-cli.exe -d
```

**Direct measurement:**
```bash
nanovna-cli.exe -d -p COM4 --start 144000000 --end 146000000 --step 1000 --autostart
```

## Command Reference

### Main Menu
- **U** - Comfort Functions (Analysis Toolkit)
- **A** - Acoustic Analysis Mode
- **R** - Range: Configure frequency scan
- **M** - Manual: Perform single measurement
- **S** - Summary: Min/max/avg statistics
- **T** - Table: View measurement data
- **E** - Export / **L** - Load measurements
- **K** - Calibration wizard
- **P** - Port selection
- **D** - Device info and battery
- **W** - Toggle continuous sweep
- **I** - Web Interface (Remote access)
- **O** - Options (Language, Bandplan, Braille)
- **H** - Help (context-sensitive)
- **Q** - Quit

### Acoustic Analysis Mode
**Playback:**
- **SPACE** - Play/Pause | **S** - Stop | **F** - Freeze
- **T** - Toggle smooth/dotted mode
- **+/-** - Adjust playback time

**Navigation:**
- **↑/↓** - Change jump width (1/10/100/500/1000)
- **←/→** - Navigate by jump width

**Loop & Markers:**
- **L** - Set left marker | **R** - Set right marker
- **O** - Toggle loop | **C** - Toggle continuous replay

**Curves:**
- **1-5** - Toggle curve on/off
- **Ctrl+1-5** - Decrease volume | **Shift+1-5** - Increase volume

**Other:**
- **Y** - Audio configuration (engine/MIDI instruments)
- **M** - Show current measurement
- **E** - Export | **H** - Help | **B** - Back

### Continuous Sweep Mode
1. Configure frequency range (**R** or **M**)
2. Enable continuous sweep (**W**)
3. Enter acoustic mode (**A**)
4. Start playback (**SPACE**) - measurements update live

## Command Line Options

```
nanovna-cli.exe [OPTIONS]

  -h, --help              Show help
  -d                      Enable debug logging
  -p, --port PORT         Serial port (e.g., COM4)
  --baud RATE             Baud rate (default: 9600)
  --start FREQ            Start frequency in Hz
  --end FREQ              End frequency in Hz
  --step FREQ             Frequency step in Hz
  --autostart             Auto-measure on startup
  --no-audio              Disable audio
  --config FILE           Config file path
```

## File Locations

**Configuration:** `config/`
- `command_templates.cfg` - NanoVNA commands
- `app_settings.cfg` - Application settings (auto-saved)

**Logs:** `logs/`
- `debug_YYYYMMDD_HHMMSS.txt` - General debug log
- `debug_comm_YYYYMMDD_HHMMSS.txt` - Serial communication log

**Exports:** `Export/`
- `nanovna_YYYYMMDD_HHMMSS_startfreq_endfreq_step.csv` - CSV data export
- `nanovna_YYYYMMDD_HHMMSS_startfreq_endfreq_step.txt` - Text export
- `nanovna_YYYYMMDD_HHMMSS_startfreq_endfreq_step.brl` - Braille graphics export

## Technical Specifications

- **Target Device:** NanoVNA-H4
- **Serial:** 9600 baud (fixed, no auto-detection)
- **Audio:** Windows winmm API, 44.1 kHz, 16-bit stereo
- **Platform:** Windows-only (MSYS2/MinGW-w64 or Visual Studio)

## Development Notes

This is a Windows-native application using:
- Windows Multimedia API (winmm) for audio synthesis
- Windows API for system functions
- conio.h for keyboard input
- SetupAPI for COM port enumeration

Cross-platform compilation is not supported due to these dependencies.

