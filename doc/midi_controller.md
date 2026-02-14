# MIDI Controller Integration

## Overview

The MIDI Controller feature allows control of the NanoVNA CLI Accessible CPA acoustic analysis mode 
using external MIDI hardware controllers. This enables physical buttons, faders, and knobs to control 
playback, navigation, curve management, and volume — providing a tactile alternative to keyboard input.

The implementation follows the existing platform abstraction pattern, with a common interface 
(`IMidiControllerInput`) and platform-specific implementations for Linux (ALSA), Windows (WinMM), 
and macOS (CoreMIDI).

## Architecture

```
┌─────────────────────────────┐
│   Acoustic Analyzer Loop    │
│   (src/ui.cpp)              │
│                             │
│  Input Sources:             │
│  ├── Keyboard (IConsoleInput)│
│  ├── Web Interface          │
│  └── MIDI Controller ←──── NEW
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  MidiControllerManager      │
│  (include/midi_controller_manager.h)
│                             │
│  • Maps MIDI events to      │
│    application commands     │
│  • Handles 0-127 scaling    │
│  • Motor fader feedback     │
│  • Preset file management   │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  IMidiControllerInput       │
│  (include/platform/midi_controller.h)
│                             │
│  Platform implementations:  │
│  ├── Linux: ALSA Sequencer  │
│  ├── Windows: WinMM         │
│  └── macOS: CoreMIDI        │
└─────────────────────────────┘
```

## Supported MIDI Message Types

### Note On (0x9n)
Used for button presses on the controller. When a button is pressed, a Note On message 
with velocity > 0 is sent. This triggers application commands (Play/Pause, Stop, etc.).

Note On with velocity 0 is treated as Note Off per the MIDI specification.

### Note Off (0x8n)
Sent when a button is released. Currently stored as a mapping option for future use,
but primary functionality relies on Note On events.

### Control Change (0xBn)
Used for faders and knobs. CC messages carry a value from 0-127 which is scaled to 
the appropriate application range (e.g., 0-100% for master volume, 0-200% for curve volume).

## Command Mapping

### Playback Control (via Note On)
| Command | Keyboard Equivalent | Description |
|---------|-------------------|-------------|
| PLAY_PAUSE | Space | Toggle play/pause |
| STOP | S | Stop and reset to start |
| FREEZE | F | Freeze at current position |

### Mode Toggles (via Note On)
| Command | Keyboard Equivalent | Description |
|---------|-------------------|-------------|
| TOGGLE_SMOOTH_DOTTED | T | Toggle smooth/dotted mode |
| TOGGLE_LOOP | O | Toggle loop on/off |
| TOGGLE_LOOP_ZOOM | Z | Toggle loop zoom |
| TOGGLE_LOOP_INVERT | I | Toggle loop invert |
| TOGGLE_CONTINUOUS | C | Toggle continuous replay |

### Navigation (via Note On)
| Command | Keyboard Equivalent | Description |
|---------|-------------------|-------------|
| MOVE_LEFT | Left Arrow | Move left by jump width |
| MOVE_RIGHT | Right Arrow | Move right by jump width |
| JUMP_WIDTH_UP | Up Arrow | Increase jump width |
| JUMP_WIDTH_DOWN | Down Arrow | Decrease jump width |
| SPEED_UP | + | Increase playback time |
| SPEED_DOWN | - | Decrease playback time |

### Curve Control (via Note On)
| Command | Description |
|---------|-------------|
| TOGGLE_CURVE_1..5 | Toggle individual curves on/off |
| MUTE_CURVE_1..5 | Mute/unmute individual curves |
| SOLO_CURVE_1..5 | Solo individual curve (toggle, with state save/restore) |
| ANNOUNCE_CURVE_VALUE_1..5 | Announce current value of individual curve at playback position |
| ANNOUNCE_MASTER_VOLUME | Announce current master volume percentage |

### CC Functions (Faders/Knobs)
| Function | Direction | Description |
|----------|-----------|-------------|
| MASTER_VOLUME | Bidirectional | Control master volume (0-100%). Fader changes volume; volume changes move fader. |
| CURVE_VOLUME_1..5 | Read from knob | Control individual curve volume (0-200%) with min/max clamping |
| CURVE_VALUE_SWR..PHASE | Write to motor fader | Display curve amplitude at current position |
| FADER_TOUCH_1..5 | Read from fader | Freeze-by-touch: temporarily freeze playback when fader is touched |

## Value Scaling (0-127 MIDI Range)

All values are converted between the application's native ranges and the MIDI 0-127 range:

| Parameter | App Range | MIDI Range | Conversion |
|-----------|-----------|------------|------------|
| Master Volume | 0-100% | 0-127 | Linear scaling |
| Curve Volume | 0-200% | 0-127 | Linear scaling (127 = 200%) |
| Position | 0 to N-1 | 0-127 | Normalized (0.0-1.0) |
| SWR | 1.0-10.0 | 0-127 | (SWR-1)/9 normalized |
| Return Loss | -40 to 0 dB | 0-127 | (RL+40)/40 normalized |
| Impedance | 0-1000 Ω | 0-127 | |Z|/1000 normalized |
| Reactance | -500 to +500 Ω | 0-127 | (X+500)/1000 normalized |
| Phase | -180° to +180° | 0-127 | (Phase+180)/360 normalized |

## Freeze by Touch

When **Freeze by Touch** is enabled (via `T` in the MIDI configuration menu or `midi_controller_freeze_by_touch=1` 
in the config file), touching a motor fader temporarily freezes playback. This allows blind users to 
feel the current fader positions without them moving away.

### How It Works
1. Touch any motor fader → playback freezes, all faders hold their position
2. Feel the fader positions to understand the current measurement values
3. Release all faders → playback resumes from where it was frozen

### Technical Details
- Each fader has separate touch (CC value ≥ 64) and release (CC value 0) detection
- Multiple faders can be touched simultaneously; playback resumes only when ALL are released
- Touch states are reset on manual Play/Pause toggle or Stop command (fallback safety)
- In frozen smooth mode, audio buffers are not flooded — the thread sleeps at normal frame rate
- When unfreezing, audio buffers are flushed for instant response

### Behringer X-Touch Compact Touch CCs
| Fader | CC Number | Description |
|-------|-----------|-------------|
| Fader 1 (SWR) | CC 101 | Touch sensor |
| Fader 2 (RL) | CC 102 | Touch sensor |
| Fader 3 (|Z|) | CC 103 | Touch sensor |
| Fader 4 (X) | CC 104 | Touch sensor |
| Fader 5 (Phase) | CC 105 | Touch sensor |

## Volume Control Debouncing

When moving a fader quickly (especially motor faders), the controller sends every intermediate 
value along the way. To prevent blocking the main loop, volume changes (both master and per-curve) 
use a 50ms debounce: only the last value after 50ms of inactivity is applied.

Additionally, when a volume control reaches its minimum (0) or maximum value, further turns 
in the same direction are suppressed with a one-time "[minimum reached]" or "[maximum reached]" 
notification instead of flooding repeated messages.

## Motor Fader Feedback

When motor fader feedback is enabled (`midi_controller_feedback=1`), the application sends 
CC messages back to the controller to physically move motor faders. This creates a tangible 
representation of the measurement data on the controller surface.

### Playback Mode Behavior
- **Dotted mode**: Faders snap to new values at each measurement point
- **Smooth mode**: Faders move smoothly between measurement points using interpolated values
- **Frozen mode**: Faders hold their position; if user moves them, they stay (no snap-back flooding)

### Curve Visibility
- Only active (enabled or solo-activated) curves move their faders
- Disabled curves have their faders set to 0
- Master volume fader is bidirectional: moving it changes volume, changing volume moves it

## Preset Files

Preset files are stored in the `midi/` directory and use a simple key=value format.

### File Format
```
preset_name=My Controller
preset_description=Custom mapping for my controller
controller_name=Brand Model

# Mapping format: function_name=type,channel,number,command_id,ccfunction_id,description
curve_volume_swr=cc,0,10,0,1,Curve volume SWR
toggle_curve_swr=noteon,0,16,14,0,Toggle curve SWR
solo_curve_swr=noteon,0,24,19,0,Solo curve SWR
master_volume=cc,0,9,0,7,Master volume fader
fader_touch_swr=cc,0,101,0,13,Fader touch SWR
```

### Fields
- **type**: `noteon`, `noteoff`, or `cc`
- **channel**: MIDI channel (0-15, 255 = any channel)
- **number**: Note number or CC number (0-127)
- **command**: MidiAppCommand enum value (for Note events, 0 = none)
- **ccfunction**: MidiCCFunction enum value (for CC events, 0 = none)

### Mapping Key Names
Each mapping uses a descriptive function name as its key. Examples:
- `curve_volume_swr` — Volume control for SWR curve
- `toggle_curve_rl` — Toggle Return Loss curve on/off
- `solo_curve_impedance` — Solo Impedance curve
- `announce_curve_value_swr` — Announce SWR value at current position
- `announce_master_volume` — Announce master volume
- `master_volume` — Master volume fader (bidirectional)
- `fader_touch_swr` — Fader touch sensor for SWR (freeze-by-touch)
- `play_pause`, `stop`, `move_left`, `move_right` — Transport controls

### Included Presets
- `behringer_x_touch_compact.cfg` — Full mapping for Behringer X-Touch Compact in MC mode

## Configuration

### App Settings (config/app_settings.cfg)
```
midi_controller_enabled=1
midi_controller_device_id=12345
midi_controller_device_name=X-Touch Compact
midi_controller_preset=behringer_x_touch_compact.cfg
midi_controller_feedback=1
midi_controller_freeze_by_touch=0
```

Note: Inline comments (e.g., `value  # comment`) are automatically stripped when loading settings.

### Configuration Menu
Access via: Acoustic Analysis → A (Audio Config) → C (MIDI Controller)

Options:
- **E** — Enable/disable MIDI controller
- **D** — Select MIDI input device (scans for connected devices)
- **P** — Select mapping preset from `midi/` directory
- **M** — Edit individual mappings
- **F** — Toggle motor fader feedback
- **T** — Toggle freeze by touch
- **S** — Save current mapping as new preset

Settings are saved immediately and applied when leaving the configuration screen.

## Debug Logging

When debug mode is enabled (`-d` flag), the MIDI controller system logs extensively:

- **[MIDI_CTRL]** — Controller manager messages (device open/close, preset loading)
- **[MIDI_CTRL] Event:** — Raw MIDI events received (type, channel, data1, data2)
- **[MIDI_CTRL] Triggering command:** — Application commands dispatched
- **[MIDI_CTRL] CC value change:** — CC value changes processed
- **[MIDI_CTRL] Position feedback:** — Motor fader position updates sent

## Compatibility

### Works with Both Audio Modes
The MIDI controller integration works identically whether the acoustic analyzer uses 
the Synthesizer engine or the MIDI audio engine. Volume values are automatically 
saved to the correct configuration array (curve_volume_synth or curve_volume_midi).

### Platform Support
| Platform | MIDI Input | Motor Fader Output | Library |
|----------|-----------|-------------------|---------|
| Linux | ✓ | ✓ | ALSA (libasound2-dev) |
| Windows | ✓ | ✓ | WinMM |
| macOS | ✓ | ✓ | CoreMIDI |

### Behringer X-Touch Compact Notes
- The exact hardware layout will be documented separately based on aseqdump captures
- Motor faders respond to CC messages on the same CC numbers they send
- The included preset maps rotary encoders to curve volumes, push buttons to curve toggles 
  and solo, and motor faders to curve amplitude display
