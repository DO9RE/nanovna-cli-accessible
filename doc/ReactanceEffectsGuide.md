# Reactance Effects Guide

## Overview

The Reactance Effects feature allows you to distinguish between capacitive and inductive reactance using MIDI Control Change (CC) parameters. This creates an additional auditory dimension beyond pitch changes, making it easier to identify the type of reactance by ear.

## Physical Intuition

Understanding the metaphors behind each CC parameter helps you choose the most meaningful effects for your analysis:

### Reverb (CC 91) - Spatial Depth ✅
**Metaphor:** The space between capacitor plates

* **Capacitive Reactance (X < 0):** Imagine sound reverberating in the space between capacitor plates. As capacitance increases (more negative X), the "room" gets larger, creating more reverb.
* **Use case:** Excellent for representing capacitive reactance as it creates a sense of "space" or "openness"
* **Platform compatibility:** All platforms (⚠️ may need enabling on macOS)

### Chorus (CC 93) - Detuning/Windings ✅
**Metaphor:** The windings of an inductor coil

* **Inductive Reactance (X > 0):** Think of the chorus effect as multiple voices (windings) speaking together. More inductance = more windings = richer chorus effect.
* **Use case:** Perfect for representing inductive reactance with its "thick" or "layered" sound
* **Platform compatibility:** All platforms

### Modulation/Vibrato (CC 1) - Oscillating Field
**Metaphor:** Oscillating magnetic field in an inductor

* **Inductive Reactance (X > 0):** The vibrato represents the oscillating nature of the magnetic field. Stronger inductance = more vibrato.
* **Use case:** Creates a "warbling" effect that grows with inductance
* **Platform compatibility:** All platforms

### Brightness (CC 74) - Frequency Response
**Metaphor:** High-frequency transmission characteristics

* **Use case:** Represents how reactance affects signal transmission at different frequencies
* **Note:** Effectiveness depends on the soundfont/synthesizer
* **Platform compatibility:** ⚠️ Soundfont-dependent

### Resonance (CC 71) - Filter Resonance
**Metaphor:** Resonant frequency characteristics

* **Use case:** Represents the "peaky" nature of resonance near resonant frequency
* **Note:** Effectiveness depends on the soundfont/synthesizer
* **Platform compatibility:** ⚠️ Soundfont-dependent

## Mode-Specific Recommendations

### Dotted Mode (Discrete Notes)

**Recommended Configuration:**
* **Capacitive:** Reverb (CC 91)
* **Inductive:** Chorus (CC 93)
* **Deadzone:** 5-10 Ω
* **Mapping:** Linear or Logarithmic

**Why this works:**
* Discrete notes have clear attack and decay phases
* Reverb tail survives between note retriggering
* Each point stands alone, making effects easy to identify
* Chorus adds richness without muddying the discrete nature

**Best for:**
* Analyzing specific frequency points
* Comparing reactance at distinct frequencies
* Training your ear to recognize reactance types

### Smooth Mode (Gliding Notes)

**Recommended Configuration:**
* **Capacitive:** Reverb (CC 91) OR Brightness (CC 74)
* **Inductive:** Chorus (CC 93) OR Modulation (CC 1)
* **Deadzone:** 3-5 Ω (smaller than dotted)
* **Mapping:** Linear or Square Root

**Why this works:**
* Continuous pitch gliding benefits from continuously modulating effects
* Smaller deadzone prevents "jumpy" transitions
* Chorus/Modulation create smooth, evolving textures
* Brightness can provide dynamic timbral changes

**Best for:**
* Understanding reactance curves as continuous functions
* Hearing transitions from capacitive to inductive
* Identifying resonance points (X crosses zero)

## Mapping Function Guide

### Linear Mapping
**Formula:** `CC = |X| / 300.0 * 127.0`

**Characteristics:**
* Direct proportional relationship
* Predictable, uniform response
* Easy to calibrate mentally

**When to use:**
* When you need analytical precision
* For scientific/measurement work
* When comparing absolute reactance values
* Default choice for most users

**Graph:**
```
CC Value
127 |                            ●
    |                          ●
    |                        ●
    |                      ●
  0 |●___________________●
    0                  300 Ω
```

### Logarithmic Mapping
**Formula:** `CC = log10(1 + |X| / 30.0) / log10(11.0) * 127.0`

**Characteristics:**
* Strong response for small values
* Compressed response for large values
* Emphasizes changes near X = 0

**When to use:**
* Finding resonance points (where X ≈ 0)
* Analyzing small reactance changes
* When large reactance values dominate your sweep
* Antenna tuning near resonance

**Graph:**
```
CC Value
127 |         ●●●●●●●●●●●●●●●●
    |      ●
    |    ●
    |  ●
  0 |●
    0                  300 Ω
```

### Exponential Mapping
**Formula:** `CC = (|X| / 300.0)^2 * 127.0`

**Characteristics:**
* Weak response for small values
* Strong response for large values
* De-emphasizes near-zero reactance

**When to use:**
* When you want to ignore small reactance
* Highlighting grossly mismatched impedances
* Cable fault detection (large X indicates problems)
* When resonance isn't your focus

**Graph:**
```
CC Value
127 |                            ●
    |                        ●
    |                   ●
    |             ●
  0 |●●●●●●●●●●
    0                  300 Ω
```

### Square Root Mapping
**Formula:** `CC = sqrt(|X| / 300.0) * 127.0`

**Characteristics:**
* Softer, more gradual response
* Compressed dynamic range
* Gentle transitions

**When to use:**
* When effects are too "aggressive"
* Learning mode (gentler feedback)
* When you want subtle hints rather than strong indicators
* Smooth mode with fast-changing reactance

**Graph:**
```
CC Value
127 |             ●●●●●●●●●●●●●
    |         ●●●
    |      ●●
    |    ●●
  0 |●●●
    0                  300 Ω
```

## Deadzone Configuration

### What is the Deadzone?

The deadzone creates a "neutral zone" around X = 0 Ω where no effects are applied. This prevents rapid oscillation when reactance hovers near zero.

### Recommended Deadzone Sizes

* **±3 Ω:** Minimal deadzone, for high-precision work
* **±5 Ω:** Standard default, good for most cases
* **±10 Ω:** Larger deadzone, reduces "nervous" transitions near resonance
* **±20 Ω:** Very large deadzone, only for noisy data or when near-zero reactance isn't interesting

### When to Disable Deadzone

* Studying resonance behavior in detail
* When reactance never crosses zero in your sweep
* High-quality, low-noise measurements

## Usage Examples

### Example 1: Finding Antenna Resonance

**Goal:** Find where your antenna is resonant (X = 0)

**Configuration:**
* Mode: Smooth
* Capacitive: Reverb (CC 91)
* Inductive: Chorus (CC 93)
* Deadzone: Disabled or ±3 Ω
* Mapping: Logarithmic

**What you'll hear:**
* As you sweep through frequencies, you'll hear reverb decreasing
* At resonance (X ≈ 0), both effects drop to near-zero (quiet, dry sound)
* Past resonance, chorus starts increasing
* The "quietest" point is your resonance!

### Example 2: Comparing Capacitive vs Inductive Loads

**Goal:** Distinguish between too-short (inductive) and too-long (capacitive) antennas

**Configuration:**
* Mode: Dotted
* Capacitive: Reverb (CC 91)
* Inductive: Chorus (CC 93)
* Deadzone: ±5 Ω
* Mapping: Linear

**What you'll hear:**
* **Too long (capacitive):** Each tone has a "spacious" reverb tail
* **Too short (inductive):** Each tone has a "thick" chorus texture
* **Just right:** Minimal effects, clear pure tones

### Example 3: Troubleshooting Cable Faults

**Goal:** Detect opens/shorts in cables by abnormal reactance

**Configuration:**
* Mode: Smooth
* Capacitive: Reverb (CC 91)
* Inductive: Chorus (CC 93)
* Deadzone: ±10 Ω
* Mapping: Exponential

**What you'll hear:**
* **Normal cable:** Gentle, gradually changing effects
* **Cable fault:** Sudden, strong effect (large reactance spike)
* **Open circuit:** Very strong inductive effect (high positive X)
* **Short circuit:** Very strong capacitive effect (high negative X)

## Platform-Specific Notes

### macOS
**Known Issue:** Reverb (CC 91) is disabled by default in `platform/midi_macos.mm` to prevent "smearing" of other curves.

**Solution:** The Reactance Effects feature automatically enables Reverb ONLY for curve index 3 (Reactance). Other curves remain unaffected.

**Verification:** After enabling reactance effects, you should hear:
* SWR, Return Loss, Impedance, Phase: No reverb (as before)
* Reactance: Reverb works correctly

### Windows
**Soundfont Dependency:** Brightness (CC 74) and Resonance (CC 71) effectiveness depends on your soundfont.

**Default Windows Soundfont:** Microsoft GS Wavetable Synth has limited support for these parameters.

**Recommendation:** Stick with Reverb (CC 91) and Chorus (CC 93) for consistent results.

### Linux (ALSA)
**Full Support:** All CC parameters should work correctly with most software synthesizers (FluidSynth, TiMidity++).

**Best Practice:** Test each CC parameter with your specific setup to verify effectiveness.

## Troubleshooting

### "I don't hear any difference"

**Possible causes:**
1. **Effects are set to "None"** - Check configuration, ensure CCs are not 0
2. **Deadzone too large** - Reduce deadzone size or disable it
3. **Reactance values too small** - Use Logarithmic mapping
4. **Wrong playback mode** - Try switching between Dotted and Smooth
5. **Platform limitation** - Try different CC parameters

### "Effects are too subtle"

**Solutions:**
* Use **Exponential mapping** to amplify large values
* Reduce or disable deadzone
* Ensure your soundfont/synth responds to the chosen CC
* Try **Modulation (CC 1)** which often has stronger effect

### "Effects are too aggressive"

**Solutions:**
* Use **Square Root mapping** for gentler response
* Increase deadzone size
* Switch from Exponential to Linear mapping
* Try less "dramatic" effects (e.g., Brightness instead of Reverb)

### "Effects jump around erratically"

**Solutions:**
* Increase deadzone size (try ±10 Ω or ±20 Ω)
* Use **Square Root** or **Logarithmic** mapping
* Switch to Smooth mode for continuous transitions
* Check measurement quality (noisy data causes jumping)

## Advanced Tips

### Combining Multiple Effects

You can assign the same effect to both capacitive and inductive reactance:
* **Bidirectional Reverb:** Use Reverb for both - effect increases as you move away from resonance in either direction
* **Symmetrical Indication:** Good for finding resonance (effects = 0 at X = 0)

### Training Your Ear

1. **Start simple:** Use defaults (Reverb/Chorus, Linear, Dotted mode)
2. **Practice with known loads:** Measure a capacitor, then an inductor
3. **Find resonance:** Use a resonant antenna and identify X = 0 by ear
4. **Experiment:** Try different CCs and mappings to find your preference

### Exporting MIDI with Effects

Reactance effects are included when exporting to MIDI files! This means:
* You can share your acoustic analysis with effects intact
* Effects work even on computers without NanoVNA hardware
* Great for creating training materials or documentation

## Summary

| Feature | Best for Capacitive | Best for Inductive | Deadzone | Mapping |
|---------|-------------------|------------------|----------|---------|
| **Default (Versatile)** | Reverb (91) | Chorus (93) | ±5 Ω | Linear |
| **Resonance Finding** | Reverb (91) | Chorus (93) | Off/±3 Ω | Logarithmic |
| **Cable Fault Detection** | Reverb (91) | Chorus (93) | ±10 Ω | Exponential |
| **Subtle Learning Mode** | Brightness (74) | Modulation (1) | ±5 Ω | Square Root |
| **Dramatic Indication** | Reverb (91) | Modulation (1) | ±3 Ω | Exponential |

## Further Reading

* **NanoVNA Training Scenarios:** See `Training/` directory for acoustic analysis exercises
* **MIDI CC Standard:** [MIDI Association - Control Change Messages](https://www.midi.org/)
* **Reactance Theory:** Smith Chart guides in `doc/` directory

---

**Note:** This feature is experimental. Your feedback helps improve it! Report issues or suggestions to the project maintainers.
