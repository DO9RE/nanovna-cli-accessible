#pragma once
#include <string>
#include <cstdint>
#include <array>
#include <vector>
#include "waveform.h"  // Include waveform enum

// Audio engine types
enum class AudioEngineType {
    SYNTHESIZER = 0,  // Waveform-based synthesis (default)
    MIDI = 1          // MIDI-based synthesis
};

// MIDI playback mode types
enum class MIDIPlaybackMode {
    GLIDING = 0,  // Smooth/gliding mode - for sustained instruments, no retriggering
    DOTTED = 1    // Dotted mode - for percussive instruments, with retriggering
};

// Frequency range constants for synthesizer audio
constexpr int SYNTH_MIN_FREQ_HZ_LIMIT = 100;    // Minimum allowed frequency
constexpr int SYNTH_MAX_FREQ_HZ_LIMIT = 20000;  // Maximum allowed frequency (human hearing limit)

// Default frequency range for synthesizer audio (user-configurable)
constexpr int SYNTH_DEFAULT_MIN_FREQ_HZ = 100;   // Default minimum frequency
constexpr int SYNTH_DEFAULT_MAX_FREQ_HZ = 1000;  // Default maximum frequency

struct AppConfig {
    bool debug = false;
    std::string serial_port; // e.g., "COM4"
    unsigned int baud = 9600;
    uint64_t start_freq = 0ULL;  // 0 indicates not set by user yet
    uint64_t end_freq = 0ULL;    // 0 indicates not set by user yet
    uint64_t step = 0ULL;        // 0 indicates not set by user yet
    bool autostart = false;
    bool audio = true;
    std::string command_templates_file = "config/command_templates.cfg";
    
    // Language and display settings
    std::string language = "eng";  // Language code (eng, deu, etc.)
    std::string bandplan = "deu";  // Band plan code (usa, deu, etc.)
    bool first_start = true;  // Flag to indicate first start of the program
    
    // Acoustic analysis settings
    bool acoustic_smooth_mode = false;  // false = dotted, true = smooth
    int acoustic_time_seconds = 5;      // Time in seconds for complete sweep (default 5)
    bool continuous_replay = true;      // Continuous replay enabled by default
    std::array<bool, 5> curve_enabled = {true, false, false, false, false};  // SWR, RL, |Z|, X, Phase
    
    // Volume settings - separate for Synth and MIDI modes
    // Synth mode volumes: balanced based on waveform energy (sine=100%, others reduced based on harmonic content)
    std::array<int, 5> curve_volume_synth = {100, 50, 70, 50, 50};  // SWR(sine), RL(square), |Z|(triangle), X(sawtooth), Phase(pulse)
    // MIDI mode volumes: all at 100% as MIDI instruments are pre-balanced
    std::array<int, 5> curve_volume_midi = {100, 100, 100, 100, 100};  // All curves at full volume
    // Legacy volume array for backward compatibility (deprecated, will be removed in future)
    std::array<int, 5> curve_volume = {100, 100, 100, 100, 100};  // Volume percent per curve (legacy)
    
    // Master volume control (0-100%)
    int master_volume = 100;  // Global volume control for all audio output
    
    // Audio engine configuration
    AudioEngineType audio_engine = AudioEngineType::SYNTHESIZER;  // Default to synthesizer
    std::array<int, 5> midi_instruments = {19, 16, 81, 80, 48};  // MIDI program numbers for each curve
    // Default instruments: Church Organ, Drawbar Organ, Lead 2 (sawtooth), Lead 1 (square), String Ensemble
    // All selected for sustained, non-percussive tones suitable for continuous audio analysis
    
    // Synthesizer waveforms for each curve (0=SWR, 1=RL, 2=|Z|, 3=X, 4=Phase)
    std::array<Waveform, 5> synth_waveforms = {
        Waveform::SINE,          // SWR
        Waveform::SQUARE,        // Return Loss
        Waveform::TRIANGLE,      // Impedance Mag
        Waveform::SAWTOOTH,      // Reactance
        Waveform::PULSE          // Phase
    };
    
    // MIDI playback mode configuration
    MIDIPlaybackMode midi_playback_mode = MIDIPlaybackMode::GLIDING;  // Default to gliding mode for sustained instruments
    
    // MIDI instrument presets for different playback modes
    // Gliding mode preset: sustained instruments (strings, organs, pads, synth leads)
    std::array<int, 5> midi_instruments_gliding = {48, 19, 16, 40, 81};  // String Ensemble, Church Organ, Drawbar Organ, Violin, Lead 2
    
    // Dotted mode preset: percussive and articulated instruments
    std::array<int, 5> midi_instruments_dotted = {11, 12, 13, 14, 8};  // Vibraphone, Marimba, Xylophone, Tubular Bells, Celesta
    
    // Frequency range for synthesizer audio (Hz)
    int synth_min_freq_hz = SYNTH_DEFAULT_MIN_FREQ_HZ;   // Minimum frequency (100-20000 Hz)
    int synth_max_freq_hz = SYNTH_DEFAULT_MAX_FREQ_HZ;   // Maximum frequency (100-20000 Hz)
    
    // MIDI interpolated panning (Mischtechniken) settings
    bool midi_interpolated_pan_mode = false;      // Enable volume-based pan interpolation for higher spatial resolution
    double midi_interpolation_strength = 0.3;     // Interpolation strength (0.0-1.0, default 0.3 = 30% volume modulation)
    
    // Reactance MIDI Effect Configuration
    // Maps reactance sign/magnitude to MIDI effects for audible inductance/capacitance distinction
    // Positive reactance (X > 0) = Inductive → Tremolo/Modulation effect
    // Negative reactance (X < 0) = Capacitive → Reverb effect
    // Separate configurations for gliding (sustained) and dotted (percussive) modes
    
    // Scaling curve types for effect intensity
    enum class EffectScaling {
        LINEAR = 0,        // Linear: proportional increase, even response
        SQUARE_ROOT = 1,   // Square root: fast initial rise, gentle at high values (natural perception)
        EXPONENTIAL = 2,   // Exponential: subtle at low values, dramatic at high values
        LOGARITHMIC = 3,   // Logarithmic: strong initial rise, compressed at high values
        S_CURVE = 4        // S-curve (sigmoid): smooth transition with plateau at extremes
    };
    
    // MIDI CC effect types available for reactance sonification
    enum class ReactanceEffectType {
        REVERB = 0,        // CC 91: Reverb Send Level (spatial, room-filling → capacitance)
        TREMOLO = 1,       // CC 1: Modulation Wheel (oscillation, vibration → inductance)
        CHORUS = 2,        // CC 93: Chorus Send Level (widening, thickening)
        VIBRATO_DEPTH = 3, // CC 77: Vibrato Depth (pitch oscillation intensity)
        VIBRATO_RATE = 4,  // CC 76: Vibrato Rate (pitch oscillation speed)
        DETUNE = 5,        // CC 94: Detune/Celeste (subtle pitch shifting)
        BRIGHTNESS = 6,    // CC 74: Brightness/Filter cutoff (timbral change)
        EXPRESSION = 7     // CC 11: Expression (dynamic volume modulation)
    };
    
    struct ReactanceModeEffectConfig {
        // Capacitive effect (X < 0): applied when reactance is negative
        ReactanceEffectType capacitive_effect = ReactanceEffectType::REVERB;
        int capacitive_max_value = 100;          // Maximum CC value at full capacitance (0-127)
        EffectScaling capacitive_scaling = EffectScaling::SQUARE_ROOT;
        
        // Inductive effect (X > 0): applied when reactance is positive
        ReactanceEffectType inductive_effect = ReactanceEffectType::TREMOLO;
        int inductive_max_value = 100;           // Maximum CC value at full inductance (0-127)
        EffectScaling inductive_scaling = EffectScaling::SQUARE_ROOT;
    };
    
    // Master enable for reactance effects
    bool reactance_effects_enabled = true;
    
    // Dead zone: range around X=0 where no effects are applied (in Ohms)
    // Values within ±deadzone are treated as purely resistive (no audible effect)
    double reactance_deadzone_ohms = 5.0;  // Default: ±5 Ohm dead zone
    
    // Maximum reactance value for full effect intensity (in Ohms)
    // Values beyond this are clamped to max effect
    double reactance_max_ohms = 300.0;     // Default: 300 Ohm = full effect
    
    // Separate effect configurations for each playback mode
    // Sustained instruments (gliding) respond differently to effects than percussive (dotted)
    ReactanceModeEffectConfig reactance_effects_gliding;   // Config for gliding/smooth mode
    ReactanceModeEffectConfig reactance_effects_dotted;    // Config for dotted/percussive mode
    
    // Synthesizer DSP effect types for reactance sonification (non-MIDI)
    // These are native PCM buffer effects applied by the SynthesizerEngine
    // Designed to match the MIDI approach: capacitance = spatial/filling, inductance = oscillation
    enum class SynthReactanceEffectType {
        AM_TREMOLO = 0,     // Amplitude modulation/tremolo: periodic volume oscillation → inductance (vibration)
        ECHO = 1,           // Delay-based echo/reverb simulation: spatial, room-filling → capacitance
        RING_MOD = 2,       // Ring modulation: metallic, harmonic distortion (distinctive timbre change)
        FILTER_SWEEP = 3,   // Low-pass filter sweep: brightness change (getting duller = capacitance charging)
        NOISE_MIX = 4,      // White noise mixing: adds noise proportional to effect → hiss indicates reactance
        BITCRUSH = 5        // Bit-depth reduction: digital distortion, resolution loss (creative alternative)
    };
    
    struct SynthReactanceModeEffectConfig {
        // Capacitive effect (X < 0): applied when reactance is negative
        SynthReactanceEffectType capacitive_effect = SynthReactanceEffectType::ECHO;
        int capacitive_max_percent = 80;          // Maximum effect depth in percent (0-100)
        EffectScaling capacitive_scaling = EffectScaling::SQUARE_ROOT;
        
        // Inductive effect (X > 0): applied when reactance is positive
        SynthReactanceEffectType inductive_effect = SynthReactanceEffectType::AM_TREMOLO;
        int inductive_max_percent = 80;           // Maximum effect depth in percent (0-100)
        EffectScaling inductive_scaling = EffectScaling::SQUARE_ROOT;
    };
    
    // Separate synth effect configurations for each playback mode
    SynthReactanceModeEffectConfig synth_reactance_effects_smooth;   // Config for smooth/gliding mode
    SynthReactanceModeEffectConfig synth_reactance_effects_dotted;   // Config for dotted mode
    
    // Dotted mode settings
    int dotted_duration_ms = 30;  // Duration of each dot in milliseconds (30-500ms)
    int dotted_pause_ms = 60;      // Duration of pause between dots in milliseconds (10-500ms)
    int freeze_point_pause_ms = 200;  // Duration of pause between repeated points in freeze mode with dotted playback (50-2000ms)
    int loop_pause_ms = 0;  // Duration of pause before loop repeats in continuous replay mode (0-5000ms, 0 = no pause)
    int inverted_loop_gap_ms = 0;  // Duration of silent gap when skipping inverted loop section (0-5000ms, 0 = no gap)
    
    // Table view preferences
    std::vector<std::string> table_columns = {"FREQ", "SWR", "RL", "R", "X", "Z", "PHASE"};  // Default: all columns enabled
    
    // Continuous sweep settings
    bool continuous_sweep_enabled = false;  // Continuous sweep mode toggle
    double last_measurement_duration_seconds = 0.0;  // Duration of last measurement
    
    // Navigation jump width for acoustic analyzer
    int navigation_jump_width = 1;  // Current jump width (1, 10, 100, 500, 1000)
    
    // Calibration settings
    int calibration_bank = 0;  // Calibration bank number (0 is auto-loaded on device startup)
    
    // Y-Axis Ruler (Lineal) settings
    enum class RulerSoundMode {
        FOLLOW_LAST_CURVE = 0,  // Use sound of last activated curve
        CUSTOM_SOUND = 1         // Use custom sound setting
    };
    
    RulerSoundMode ruler_sound_mode = RulerSoundMode::FOLLOW_LAST_CURVE;  // Default to follow last curve
    int ruler_custom_sound_synth = 0;  // Custom waveform index for synth mode (0-5)
    int ruler_custom_sound_midi_gliding = 48;  // Custom MIDI instrument for gliding mode (default: String Ensemble)
    int ruler_custom_sound_midi_dotted = 11;   // Custom MIDI instrument for dotted mode (default: Vibraphone)
    int ruler_blip_duration_ms = 80;  // Duration of shortest blip (half integers) in milliseconds (30-500ms)
    int ruler_volume = 100;  // Volume for ruler sounds (0-100%)
    int ruler_lengthening_factor_percent = 150;  // Lengthening factor in % for longer tones (100-500%, default 150%)
    
    // X-Axis Ruler settings
    bool x_axis_ruler_enabled = false;  // X-axis ruler enabled by default?
    int x_axis_ruler_volume = 70;  // Volume for X-axis ruler (0-100%, default 70%)
    int x_axis_ruler_blip_duration_ms = 50;  // Duration of X-axis ruler blips (30-200ms, default 50ms)
    int x_axis_ruler_noise_type = 0;  // Noise type for synthesizer mode (0=White, 1=Pink, 2=Click)
    int x_axis_ruler_midi_drum = 42;  // MIDI drum note for X-axis ruler (35-81, default 42 = Closed Hi-Hat)
    
    // Status line settings
    bool status_line_enabled = false;  // Status line enabled by default?
    int status_line_content = 3;  // Status line content (0=Position, 1=Frequency, 2=SWR, 3=All)
    // Status line individual toggles (for flexible configuration)
    bool status_line_show_position = true;  // Show position in status line
    bool status_line_show_frequency = true;  // Show frequency in status line
    bool status_line_show_swr = true;  // Show SWR value in status line
    bool status_line_show_rl = false;  // Show Return Loss value in status line
    bool status_line_show_impedance = false;  // Show Impedance magnitude value in status line
    bool status_line_show_reactance = false;  // Show Reactance value in status line
    bool status_line_show_phase = false;  // Show Phase value in status line
    
    // Smith Diagram Visualization settings
    int smith_cues_volume = 30;  // Volume for Smith ambient cues (10-100%, default 30%)
    
    enum class SmithNoiseType {
        PINK = 0,      // Pink noise (default, warm filtered sound)
        WHITE = 1,     // White noise (brighter, full spectrum)
        BROWN = 2,     // Brown noise (darker, low frequency emphasis)
        SINE_WAVE = 3  // Sine wave (pure tone, cleaner)
    };
    
    SmithNoiseType smith_noise_type = SmithNoiseType::PINK;  // Default to pink noise
    
    // Center pulse (reference signal) settings
    bool center_pulse_enabled = false;  // Center pulse disabled by default
    int center_pulse_volume = 40;  // Volume for center pulse (10-100%, default 40%)
    double center_pulse_interval = 1.0;  // Pulse interval in seconds (0.5-2.0, default 1.0)
    
    enum class CenterPulseWaveform {
        CLICK = 0,      // Filtered noise click (default, percussive)
        SINE = 1,       // Sine wave blip (clean, musical)
        SQUARE = 2,     // Square wave blip (bright, synthetic)
        TRIANGLE = 3,   // Triangle wave blip (warm, mellow)
        SAWTOOTH = 4,   // Sawtooth wave blip (bright, rich)
        PULSE = 5       // Short pulse wave (sharp, electronic)
    };
    
    CenterPulseWaveform center_pulse_waveform = CenterPulseWaveform::CLICK;  // Default to click
    
    // Axis crossing events settings
    bool axis_events_enabled = false;  // Axis crossing events disabled by default
    int axis_events_volume = 60;  // Volume for axis events (10-100%, default 60%)
    double axis_events_pitch_min = 300.0;  // Minimum pitch for gestures in Hz (200-1000)
    double axis_events_pitch_max = 800.0;  // Maximum pitch for gestures in Hz (400-2000)
    int axis_events_duration_ms = 100;  // Duration of axis crossing sounds in ms (50-500, default 100)
    
    enum class AxisCrossingSound {
        PLUCK = 0,      // Plucked string sound (default, natural gesture)
        SWEEP = 1,      // Pure sine sweep (clean, directional)
        CHIRP = 2,      // Complex chirp with harmonics (attention-grabbing)
        BELL = 3,       // Bell-like tone (pleasant, resonant)
        PERCUSSION = 4  // Percussive hit (sharp, distinctive)
    };
    
    AxisCrossingSound axis_crossing_sound = AxisCrossingSound::PLUCK;  // Default to pluck
    
    // Surround sound configuration settings
    bool surround_enabled = true;  // Enable surround sound if available
    int surround_front_distance = 100;  // Distance factor for front speakers (50-200%, default 100%)
    int surround_back_distance = 100;   // Distance factor for back speakers (50-200%, default 100%)
    int surround_side_distance = 100;   // Distance factor for side speakers (50-200%, default 100%)
    int surround_center_strength = 50;  // Center channel strength (0-100%, default 50%)
    
    // Fading curves for spatial audio perception
    enum class SurroundFadingCurve {
        LINEAR = 0,      // Linear fading (default, equal perceived movement)
        LOGARITHMIC = 1, // Logarithmic (more emphasis on center)
        EXPONENTIAL = 2, // Exponential (more emphasis on edges)
        SINE = 3         // Sine curve (smooth, natural transition)
    };
    
    SurroundFadingCurve surround_fading_curve = SurroundFadingCurve::LINEAR;  // Default to linear
    
    // Front/back separation enhancement
    int surround_fb_separation = 100;  // Front/back separation strength (50-200%, default 100%)
    
    // Side channel emphasis for better 90° localization
    int surround_side_emphasis = 100;  // Side channel emphasis (50-200%, default 100%)
    
    // Braille printer settings
    enum class BrailleProtocol {
        INDEX_V4 = 0,  // Index Everest V4 protocol (raster graphics)
        INDEX_V5 = 1   // Index Everest V5 protocol (floating dot area)
    };
    
    enum class BraillePaperSize {
        A4 = 0,           // A4 paper (210mm x 297mm)
        LETTER = 1,       // US Letter (215.9mm x 279.4mm)
        A3 = 2,           // A3 paper (297mm x 420mm)
        LEGAL = 3,        // US Legal (215.9mm x 355.6mm)
        BLISTA_260x305 = 4,  // Blista Brailletec (260mm x 305mm)
        BLISTA_270x340 = 5,  // Blista Brailletec (270mm x 340mm)
        BLISTA_297x304 = 6   // Blista Brailletec (297mm x 304mm)
    };
    
    enum class BrailleOrientation {
        PORTRAIT = 0,   // Portrait orientation
        LANDSCAPE = 1   // Landscape orientation
    };
    
    BrailleProtocol braille_protocol = BrailleProtocol::INDEX_V5;  // Default to V5 (floating dot area)
    BraillePaperSize braille_paper_size = BraillePaperSize::BLISTA_260x305;    // Default to Blista 260x305
    BrailleOrientation braille_orientation = BrailleOrientation::PORTRAIT;  // Default to portrait
    
    // Braille grid options (axes are always shown)
    enum class BrailleCoordinateGrid {
        NONE = 0,        // No coordinate grid
        DOTS = 1,        // Individual dots at integer coordinates
        GRID_LINES = 2   // Full grid lines
    };
    
    BrailleCoordinateGrid braille_coordinate_grid = BrailleCoordinateGrid::DOTS;  // Default: dots at integers
    
    // Phase discontinuity display mode (for phase curve jumps at ±180°)
    enum class BraillePhaseDiscontinuityMode {
        ARROWS = 0,        // Show small directional arrows (default, compact)
        VERTICAL_LINE = 1  // Draw vertical line with curve pattern applied
    };
    
    BraillePhaseDiscontinuityMode braille_phase_discontinuity = BraillePhaseDiscontinuityMode::VERTICAL_LINE;  // Default: vertical line
    
    // Curve pattern definitions (for tactile differentiation)
    // Format: alternating draw-pause segments, numbers indicate count of dots/pauses
    // Pattern alternates: first number = draw, second = pause, third = draw, etc.
    // Examples: "0" or empty = solid line (all dots)
    //           "2-1" = draw 2 dots, pause 1 dot, repeat
    //           "3-1" = draw 3 dots, pause 1 dot, repeat
    std::array<std::string, 5> braille_curve_patterns = {"1-1", "2-1", "3-1", "4-1", "5-1"};
    // Defaults: SWR=1-1, RL=2-1, |Z|=3-1, X=4-1, Phase=5-1
    
    // Braille DPI setting (dots per inch)
    // Controls the minimum spacing between dots
    // Default: 25 DPI = 1 dot per mm (25/25.4 ≈ 0.984 mm)
    // Range: 10-40 DPI (2.54mm - 0.635mm spacing)
    double braille_dpi = 18.0;  // Default: 18 DPI
    
    // Advanced Braille Printer Parameters (Index Protocol)
    // These control low-level printer behavior for fine-tuning output
    
    // Document parameters (ESC D command)
    int braille_top_margin = 0;        // TM: Top margin in lines (0-10, default 0 for max space)
    int braille_binding_indent = 2;    // BI: Binding margin/indent (0-10, default 2)
    int braille_chars_per_line = 29;   // CH: Characters per line (depends on paper width)
    int braille_line_spacing = 50;     // LS: Line spacing in 0.1mm (50=5.0mm, 100=10.0mm)
    
    // Floating dot area layout parameters (for V5 protocol)
    // These control how much of the paper is used for graphics
    double braille_graph_width_percent_portrait = 0.95;   // Portrait: % of paper width for graph (0.8-0.99)
    double braille_graph_width_percent_landscape = 0.98;  // Landscape: % of paper width for graph (0.8-0.99)
    double braille_graph_height_percent_portrait = 0.70;  // Portrait: % of paper height for graph (0.6-0.9)
    double braille_graph_height_percent_landscape = 0.85; // Landscape: % of paper height for graph (0.6-0.9)
    
    // Origin offsets for floating dot area (in mm from margins)
    double braille_origin_x_mm = 3.0;  // Horizontal offset from left margin (0-10mm)
    double braille_origin_y_mm = 0.0;  // Vertical offset from text insertion point (0-20mm)
    
    // Y-axis space reservation (in mm, subtracted from graph width for axis labels)
    double braille_y_axis_space_mm = 2.0;  // Space reserved for Y-axis (1-5mm)
    
    // Spatial Audio Calibration settings
    bool spatial_audio_calibrated = false;  // Whether spatial audio has been calibrated
    
    // User hearing characteristics (calibrated via wizard)
    struct SpatialAudioCalibration {
        // Direction perception accuracy (0.0-1.0, higher = better)
        double front_back_accuracy = 0.5;       // How well user distinguishes front from back
        double left_right_accuracy = 0.9;       // How well user distinguishes left from right
        double diagonal_accuracy = 0.7;         // How well user perceives diagonal positions
        
        // Distance perception characteristics
        double near_threshold = 0.3;            // Position value perceived as "near" (0.0-0.5)
        double far_threshold = 0.7;             // Position value perceived as "far" (0.5-1.0)
        double distance_sensitivity = 0.5;      // How sensitive to distance changes (0.0-1.0)
        
        // Volume/loudness preferences
        double preferred_curve_volume = 0.7;    // Preferred volume for curve sounds (0.0-1.0)
        double preferred_event_volume = 0.6;    // Preferred volume for event sounds (0.0-1.0)
        double preferred_ambient_volume = 0.3;  // Preferred volume for ambient cues (0.0-1.0)
        
        // Sound type preferences (indices into available sound types)
        int preferred_axis_crossing_sound = 0;  // Index of preferred axis crossing sound
        int preferred_center_pulse_waveform = 0; // Index of preferred center pulse waveform
        
        // Psychoacoustic parameters (for stereo mode)
        double crossfeed_amount = 0.15;         // Amount of crossfeed for front/back (0.0-0.5)
        double back_attenuation = 0.7;          // Attenuation for back sounds (0.5-1.0)
        double side_emphasis = 0.8;             // Emphasis for side channels (0.5-1.0)
        
        // Surround-specific parameters (only used when surround is available)
        bool surround_available = false;        // Whether surround was detected during calibration
        double front_speaker_distance = 1.0;    // Relative front speaker distance (0.5-2.0)
        double back_speaker_distance = 1.0;     // Relative back speaker distance (0.5-2.0)
        double side_speaker_distance = 1.0;     // Relative side speaker distance (0.5-2.0)
        
    } spatial_calibration;
    
    // MIDI Controller settings
    bool midi_controller_enabled = false;     // Whether MIDI controller input is enabled
    int midi_controller_device_id = -1;       // Platform-specific device ID (-1 = none selected)
    std::string midi_controller_device_name;  // Stored device name for display
    std::string midi_controller_preset = "";  // Preset filename (e.g., "behringer_x_touch_compact.cfg")
    bool midi_controller_feedback = true;     // Send motor fader feedback to controller
    bool midi_controller_freeze_by_touch = false;  // Freeze playback when motor fader is touched
};
