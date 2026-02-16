# nanoVNA-cli-accessible

<!-- BUILD_VERSION --> **Build Version:** 0.6.2-beta (2026-02-16 07:22:07)

Accessible console application for controlling, measuring, and audibly visualizing the NanoVNA-H4.  

> **📋 PLATFORM SUPPORT STATUS**  
> - **Windows**: ✅ Fully supported (native build, static linking, no external dependencies)
> - **macOS**: ✅ Fully supported (requires PortAudio via Homebrew)
> - **Linux**: ✅ Fully supported (requires PortAudio, ALSA development libraries for MIDI)
>
> **Note**: All platforms feature complete audio (PortAudio/waveOut), MIDI (ALSA Sequencer/CoreMIDI/WinMM), 
> and console input (termios/conio.h) implementations. No stubs remain.

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

**Smith Diagram Visualization (NEW!):**
Experience impedance matching through 3D spatial audio:
- **V** - Toggle Smith visualization on/off
- **B** - Select visualization mode (6 modes)
- **Six visualization modes:**
  1. **Cartesian**: Position in 3D space (Re(Γ) → L/R, Im(Γ) → F/B)
  2. **Polar**: Rotation around user (∠Γ → angle, |Γ| → distance)
  3. **Impedance Direct**: R → L/R, X → F/B (simpler)
  4. **SWR Circles**: Focus on constant SWR levels
  5. **Time Domain**: Acoustic analysis + Smith spatial cues
  6. **Hybrid**: Multiple audio layers simultaneously
- **Spatial Audio**: Ambient noise positioned in stereo field shows impedance location
- **Center = 50Ω match**, **Edge = poor match**, **Front = inductive**, **Back = capacitive**
- Press **H** while Smith active for detailed mode help
- Optimized for headphones (stereo/5.1/7.1 surround supported)

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

**Reactance MIDI Effects (Z key in Audio Config):**
- Hear inductance vs. capacitance through MIDI effects on the reactance curve
- Capacitive (X<0) → Reverb (room-filling), Inductive (X>0) → Tremolo (oscillation)
- Configurable dead zone, 8 effect types, 5 scaling curves
- Separate settings for Gliding (sustained) and Dotted (percussive) modes

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

### Windows Installation
Requires Windows with MSYS2/MinGW-w64 or Visual Studio.

#### VS Code Build Workflow (recommended for contributors)

**Install once:**

1. **MSYS2 MinGW64 toolchain** (recommended Windows path):
```bash
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-make
```
2. **VS Code extensions:**
  - `ms-vscode.cmake-tools`
  - `ms-vscode.cpptools`

**Build in VS Code using presets:**

1. Open workspace in VS Code
2. `Ctrl+Shift+P` → **CMake: Select Configure Preset**
3. Choose one of:
  - `Windows (MSYS2 MinGW64, default path)` (uses `C:/msys64/...`)
  - `Windows (MinGW from PATH)` (for custom MinGW setup)
4. `Ctrl+Shift+P` → **CMake: Configure**
5. `Ctrl+Shift+P` → **CMake: Build**

The presets are defined in `CMakePresets.json`, so no per-user `.vscode` setup is required.

```bash
# Using the provided build script (recommended)
./build.sh

# Or manually:
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
mingw32-make -j4
```

### macOS Installation
macOS is fully supported. Requires PortAudio as an external dependency.

```bash
# Install dependencies
brew install cmake portaudio

# Build using the macOS build script (recommended - can optionally bundle PortAudio)
./build-macos.sh

# Or manually:
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
```

**Note**: The `build-macos.sh` script can optionally bundle PortAudio for distribution. For development, system-installed PortAudio via Homebrew is sufficient.

### Linux Installation
Linux is fully supported. Requires PortAudio and optionally ALSA development libraries for MIDI support.

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install build-essential cmake portaudio19-dev libasound2-dev

# Install dependencies (Fedora/RHEL)
sudo dnf install cmake portaudio-devel alsa-lib-devel

# Build using the build script
./build.sh

# Or manually:
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

**Note**: ALSA development libraries (`libasound2-dev`/`alsa-lib-devel`) are required for MIDI support. If not available, the application will compile and run with a fallback message for MIDI functionality.

### Usage

**Interactive mode:**
```bash
# Windows
nanovna-cli.exe -d

# macOS/Linux - Option 1 (Terminal)
cd build
./nanovna-cli -d

# macOS - Option 2 (Finder - double-click)
# Navigate to the build/ directory in Finder
# Double-click the file: nanovna-cli.command
# This will open a Terminal window and run the application
```

**Direct measurement:**
```bash
# Windows
nanovna-cli.exe -d -p COM4 --start 144000000 --end 146000000 --step 1000 --autostart

# macOS
./nanovna-cli -d -p /dev/cu.usbmodem14201 --start 144000000 --end 146000000 --step 1000 --autostart

# Linux
./nanovna-cli -d -p /dev/ttyUSB0 --start 144000000 --end 146000000 --step 1000 --autostart
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
- **?** - Manuals and Training (submenu with User Manual, Training Suite, Beta Test Instructions)
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

**Smith Visualization:**
- **V** - Toggle Smith diagram visualization
- **B** - Change Smith mode (6 modes: Cartesian/Polar/Direct/SWR/Time/Hybrid)
- **H** - Show help (Smith-specific when Smith active)

**Other:**
- **Y** - Audio configuration (engine/MIDI instruments)
- **M** - Show current measurement
- **E** - Export | **ESC** - Back

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
- **Serial:** Default 9600 baud, configurable via `--baud` parameter (supported: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600). No auto-detection.
- **Audio:** 44.1 kHz, 16-bit stereo
  - Windows: waveOut API (built-in, no dependencies)
  - macOS: CoreAudio via PortAudio
  - Linux: ALSA/PulseAudio via PortAudio
- **MIDI:**
  - Windows: WinMM API
  - macOS: CoreMIDI with AudioUnit DLS Synth
  - Linux: ALSA Sequencer API (requires ALSA development libraries)
- **Serial Ports:**
  - Windows: COM ports (SetupAPI enumeration)
  - macOS: `/dev/cu.*` and `/dev/tty.*` (IOKit USB enumeration)
  - Linux: `/dev/ttyUSB*` and `/dev/ttyACM*` (sysfs enumeration)

## Development Notes

This is a cross-platform application with platform-specific backends:
- **Windows**: Static linking (self-contained executable), Windows Multimedia API (waveOut, WinMM), conio.h, SetupAPI
- **macOS**: PortAudio (external dependency), CoreMIDI/AudioUnit, termios/POSIX, IOKit
- **Linux**: PortAudio (external dependency), ALSA Sequencer, termios/POSIX, sysfs

<!-- BETA_DOWNLOADS_START -->
# Beta Version Download

The current beta is available for multiple platforms:

## Windows Version (v0.6.2-beta)
**[📥 nanovna-cli-accessible-beta-windows-0.6.2-beta.zip](https://github.com/DO9RE/nanovna-cli-accessible/raw/refs/heads/main/nanovna-cli-accessible-beta-windows-0.6.2-beta.zip)**

## macOS Version (v0.6.2-beta)
**[📥 nanovna-cli-accessible-beta-macos-0.6.2-beta.zip](https://github.com/DO9RE/nanovna-cli-accessible/raw/refs/heads/main/nanovna-cli-accessible-beta-macos-0.6.2-beta.zip)**

## Linux Version
> ⚠️ **Not yet available.** The Linux build for version 0.6.2-beta has not been created yet. Please check back later.
<!-- BETA_DOWNLOADS_END -->
