#include "settings.h"
#include <fstream>
#include <sstream>
#include <filesystem>

static void trim(std::string &s) {
    while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
    while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
}

// Helper function to safely parse unsigned 64-bit integer from string
// Returns true if parsing was successful, false otherwise
// If parsing fails or string is empty, result is set to 0
static bool parseUInt64(const std::string& str, uint64_t& result) {
    if (str.empty()) {
        result = 0;
        return false;
    }
    try {
        result = std::stoull(str);
        return true;
    } catch (const std::invalid_argument&) {
        result = 0;  // Invalid value, use 0 to indicate not set
        return false;
    } catch (const std::out_of_range&) {
        result = 0;  // Out of range value, use 0 to indicate not set
        return false;
    }
}

bool loadAppSettings(AppConfig& cfg, const std::string& path, std::string& err) {
    std::filesystem::path p = std::filesystem::u8path(path);
    if (!std::filesystem::exists(p)) {
        err = "Settings file not found: " + path;
        return false;
    }
    std::ifstream ifs(p);
    if (!ifs) { err = "Cannot open settings file: " + path; return false; }
    std::string line;
    while (std::getline(ifs, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0,pos);
        std::string v = line.substr(pos+1);
        trim(k); trim(v);
        if (k == "serial_port") cfg.serial_port = v;
        else if (k == "baud") cfg.baud = std::stoul(v);
        else if (k == "start_freq") parseUInt64(v, cfg.start_freq);
        else if (k == "end_freq") parseUInt64(v, cfg.end_freq);
        else if (k == "step") parseUInt64(v, cfg.step);
        else if (k == "command_templates_file") cfg.command_templates_file = v;
        else if (k == "audio") cfg.audio = (v == "1" || v == "true");
        else if (k == "language") cfg.language = v;
        else if (k == "bandplan") cfg.bandplan = v;
        else if (k == "first_start") cfg.first_start = (v == "1" || v == "true");
        else if (k == "acoustic_smooth_mode") cfg.acoustic_smooth_mode = (v == "1" || v == "true");
        else if (k == "acoustic_time_seconds") cfg.acoustic_time_seconds = std::stoi(v);
        else if (k == "continuous_replay") cfg.continuous_replay = (v == "1" || v == "true");
        // Legacy support for old config files
        else if (k == "acoustic_speed_percent") {
            // Convert old percentage to approximate time in seconds
            // Old percentage: 100% = normal, 50% = slower, 200% = faster
            // Approximate conversion: 5 seconds at 100%, scale inversely
            int percentage = std::stoi(v);
            if (percentage > 0) {
                cfg.acoustic_time_seconds = std::max(1, std::min(60, (500 / percentage)));
            } else {
                cfg.acoustic_time_seconds = 5;  // Default fallback
            }
        }
        else if (k.substr(0, 13) == "curve_enabled") {
            int idx = std::stoi(k.substr(13));
            if (idx >= 0 && idx < 5) cfg.curve_enabled[idx] = (v == "1" || v == "true");
        }
        // New separate volume settings for Synth and MIDI
        else if (k.substr(0, 18) == "curve_volume_synth") {
            int idx = std::stoi(k.substr(18));
            if (idx >= 0 && idx < 5) cfg.curve_volume_synth[idx] = std::stoi(v);
        }
        else if (k.substr(0, 17) == "curve_volume_midi") {
            int idx = std::stoi(k.substr(17));
            if (idx >= 0 && idx < 5) cfg.curve_volume_midi[idx] = std::stoi(v);
        }
        // Legacy curve_volume for backward compatibility
        else if (k.substr(0, 12) == "curve_volume" && k.length() == 13) {
            int idx = std::stoi(k.substr(12));
            if (idx >= 0 && idx < 5) {
                cfg.curve_volume[idx] = std::stoi(v);
                // Also populate new arrays if they haven't been set yet (migration path)
                cfg.curve_volume_synth[idx] = std::stoi(v);
                cfg.curve_volume_midi[idx] = std::stoi(v);
            }
        }
        else if (k == "master_volume") cfg.master_volume = std::stoi(v);
        else if (k == "ruler_volume") cfg.ruler_volume = std::stoi(v);
        else if (k == "ruler_sound_mode") {
            int soundMode = std::stoi(v);
            if (soundMode == 0) {
                cfg.ruler_sound_mode = AppConfig::RulerSoundMode::FOLLOW_LAST_CURVE;
            } else if (soundMode == 1) {
                cfg.ruler_sound_mode = AppConfig::RulerSoundMode::CUSTOM_SOUND;
            } else {
                cfg.ruler_sound_mode = AppConfig::RulerSoundMode::FOLLOW_LAST_CURVE;
            }
        }
        else if (k == "ruler_custom_sound_synth") cfg.ruler_custom_sound_synth = std::stoi(v);
        else if (k == "ruler_custom_sound_midi_gliding") cfg.ruler_custom_sound_midi_gliding = std::stoi(v);
        else if (k == "ruler_custom_sound_midi_dotted") cfg.ruler_custom_sound_midi_dotted = std::stoi(v);
        else if (k == "ruler_blip_duration_ms") cfg.ruler_blip_duration_ms = std::stoi(v);
        else if (k == "ruler_lengthening_factor_percent") cfg.ruler_lengthening_factor_percent = std::stoi(v);
        // X-Axis Ruler settings
        else if (k == "x_axis_ruler_enabled") cfg.x_axis_ruler_enabled = (v == "1" || v == "true");
        else if (k == "x_axis_ruler_volume") cfg.x_axis_ruler_volume = std::stoi(v);
        else if (k == "x_axis_ruler_blip_duration_ms") cfg.x_axis_ruler_blip_duration_ms = std::stoi(v);
        else if (k == "x_axis_ruler_noise_type") cfg.x_axis_ruler_noise_type = std::stoi(v);
        else if (k == "x_axis_ruler_midi_drum") cfg.x_axis_ruler_midi_drum = std::stoi(v);
        // Status line settings
        else if (k == "status_line_enabled") cfg.status_line_enabled = (v == "1" || v == "true");
        else if (k == "status_line_content") cfg.status_line_content = std::stoi(v);
        else if (k == "status_line_show_position") cfg.status_line_show_position = (v == "1" || v == "true");
        else if (k == "status_line_show_frequency") cfg.status_line_show_frequency = (v == "1" || v == "true");
        else if (k == "status_line_show_swr") cfg.status_line_show_swr = (v == "1" || v == "true");
        else if (k == "status_line_show_rl") cfg.status_line_show_rl = (v == "1" || v == "true");
        else if (k == "status_line_show_impedance") cfg.status_line_show_impedance = (v == "1" || v == "true");
        else if (k == "status_line_show_reactance") cfg.status_line_show_reactance = (v == "1" || v == "true");
        else if (k == "status_line_show_phase") cfg.status_line_show_phase = (v == "1" || v == "true");
        // Braille printer settings
        else if (k == "braille_protocol") {
            int protocol = std::stoi(v);
            if (protocol == 0) {
                cfg.braille_protocol = AppConfig::BrailleProtocol::INDEX_V4;
            } else if (protocol == 1) {
                cfg.braille_protocol = AppConfig::BrailleProtocol::INDEX_V5;
            } else {
                cfg.braille_protocol = AppConfig::BrailleProtocol::INDEX_V5;  // Default to V5
            }
        }
        else if (k == "braille_paper_size") {
            int paperSize = std::stoi(v);
            if (paperSize == 0) {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::A4;
            } else if (paperSize == 1) {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::LETTER;
            } else if (paperSize == 2) {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::A3;
            } else if (paperSize == 3) {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::LEGAL;
            } else if (paperSize == 4) {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::BLISTA_260x305;
            } else if (paperSize == 5) {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::BLISTA_270x340;
            } else if (paperSize == 6) {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::BLISTA_297x304;
            } else {
                cfg.braille_paper_size = AppConfig::BraillePaperSize::BLISTA_260x305;  // Default to Blista 260x305
            }
        }
        else if (k == "braille_orientation") {
            int orientation = std::stoi(v);
            if (orientation == 0) {
                cfg.braille_orientation = AppConfig::BrailleOrientation::PORTRAIT;
            } else if (orientation == 1) {
                cfg.braille_orientation = AppConfig::BrailleOrientation::LANDSCAPE;
            } else {
                cfg.braille_orientation = AppConfig::BrailleOrientation::PORTRAIT;  // Default to portrait
            }
        }
        // braille_show_axes removed - axes are now always shown
        else if (k == "braille_coordinate_grid") {
            try {
                int grid = std::stoi(v);
                if (grid == 0) {
                    cfg.braille_coordinate_grid = AppConfig::BrailleCoordinateGrid::NONE;
                } else if (grid == 1) {
                    cfg.braille_coordinate_grid = AppConfig::BrailleCoordinateGrid::DOTS;
                } else if (grid == 2) {
                    cfg.braille_coordinate_grid = AppConfig::BrailleCoordinateGrid::GRID_LINES;
                } else {
                    cfg.braille_coordinate_grid = AppConfig::BrailleCoordinateGrid::NONE;
                }
            } catch (...) {
                // Invalid value, keep default
            }
        }
        else if (k.rfind("braille_curve_pattern", 0) == 0) {
            // Format: braille_curve_pattern0, braille_curve_pattern1, etc.
            try {
                size_t idx = std::stoul(k.substr(21));  // Extract index after "braille_curve_pattern"
                if (idx < 5) {
                    cfg.braille_curve_patterns[idx] = v;
                }
            } catch (...) {
                // Invalid pattern index, skip
            }
        }
        else if (k == "braille_dpi") {
            try {
                double dpi = std::stod(v);
                // Clamp to reasonable range: 10-40 DPI
                if (dpi >= 10.0 && dpi <= 40.0) {
                    cfg.braille_dpi = dpi;
                }
            } catch (...) {
                // Invalid value, keep default
            }
        }
        else if (k == "braille_top_margin") {
            try {
                int tm = std::stoi(v);
                if (tm >= 0 && tm <= 10) {  // Valid range: 0-10
                    cfg.braille_top_margin = tm;
                }
            } catch (...) {}
        }
        else if (k == "braille_binding_indent") {
            try {
                int bi = std::stoi(v);
                if (bi >= 0 && bi <= 10) {  // Valid range: 0-10
                    cfg.braille_binding_indent = bi;
                }
            } catch (...) {}
        }
        else if (k == "braille_chars_per_line") {
            try {
                int ch = std::stoi(v);
                if (ch >= 10 && ch <= 50) {  // Valid range: 10-50
                    cfg.braille_chars_per_line = ch;
                }
            } catch (...) {}
        }
        else if (k == "braille_line_spacing") {
            try {
                int ls = std::stoi(v);
                if (ls >= 50 && ls <= 100) {  // Valid range: 50-100 (5.0-10.0mm)
                    cfg.braille_line_spacing = ls;
                }
            } catch (...) {}
        }
        else if (k == "braille_graph_width_percent_portrait") {
            try {
                double pct = std::stod(v);
                if (pct >= 0.8 && pct <= 0.99) {
                    cfg.braille_graph_width_percent_portrait = pct;
                }
            } catch (...) {}
        }
        else if (k == "braille_graph_width_percent_landscape") {
            try {
                double pct = std::stod(v);
                if (pct >= 0.8 && pct <= 0.99) {
                    cfg.braille_graph_width_percent_landscape = pct;
                }
            } catch (...) {}
        }
        else if (k == "braille_graph_height_percent_portrait") {
            try {
                double pct = std::stod(v);
                if (pct >= 0.6 && pct <= 0.9) {
                    cfg.braille_graph_height_percent_portrait = pct;
                }
            } catch (...) {}
        }
        else if (k == "braille_graph_height_percent_landscape") {
            try {
                double pct = std::stod(v);
                if (pct >= 0.6 && pct <= 0.9) {
                    cfg.braille_graph_height_percent_landscape = pct;
                }
            } catch (...) {}
        }
        else if (k == "braille_origin_x_mm") {
            try {
                double mm = std::stod(v);
                if (mm >= 0.0 && mm <= 10.0) {
                    cfg.braille_origin_x_mm = mm;
                }
            } catch (...) {}
        }
        else if (k == "braille_origin_y_mm") {
            try {
                double mm = std::stod(v);
                if (mm >= 0.0 && mm <= 20.0) {
                    cfg.braille_origin_y_mm = mm;
                }
            } catch (...) {}
        }
        else if (k == "braille_y_axis_space_mm") {
            try {
                double mm = std::stod(v);
                if (mm >= 1.0 && mm <= 5.0) {
                    cfg.braille_y_axis_space_mm = mm;
                }
            } catch (...) {}
        }
        else if (k == "braille_phase_discontinuity") {
            try {
                int mode = std::stoi(v);
                if (mode == 0) {
                    cfg.braille_phase_discontinuity = AppConfig::BraillePhaseDiscontinuityMode::ARROWS;
                } else if (mode == 1) {
                    cfg.braille_phase_discontinuity = AppConfig::BraillePhaseDiscontinuityMode::VERTICAL_LINE;
                } else {
                    cfg.braille_phase_discontinuity = AppConfig::BraillePhaseDiscontinuityMode::ARROWS;
                }
            } catch (...) {
                // Invalid value, keep default
            }
        }
        else if (k == "audio_engine") {
            int engineType = std::stoi(v);
            if (engineType == 0) {
                cfg.audio_engine = AudioEngineType::SYNTHESIZER;
            } else if (engineType == 1) {
                cfg.audio_engine = AudioEngineType::MIDI;
            } else {
                // Invalid value, default to synthesizer
                cfg.audio_engine = AudioEngineType::SYNTHESIZER;
            }
        }
        else if (k == "midi_playback_mode") {
            int midiMode = std::stoi(v);
            if (midiMode == 0) {
                cfg.midi_playback_mode = MIDIPlaybackMode::GLIDING;
            } else if (midiMode == 1) {
                cfg.midi_playback_mode = MIDIPlaybackMode::DOTTED;
            } else {
                cfg.midi_playback_mode = MIDIPlaybackMode::GLIDING;
            }
        }
        else if (k.substr(0, 16) == "midi_instrument") {
            int idx = std::stoi(k.substr(16));
            if (idx >= 0 && idx < 5) cfg.midi_instruments[idx] = std::stoi(v);
        }
        else if (k.substr(0, 15) == "synth_waveform") {
            int idx = std::stoi(k.substr(15));
            if (idx >= 0 && idx < 5) {
                int waveformInt = std::stoi(v);
                // Map integer to waveform enum
                if (waveformInt == 0) cfg.synth_waveforms[idx] = Waveform::SINE;
                else if (waveformInt == 1) cfg.synth_waveforms[idx] = Waveform::SQUARE;
                else if (waveformInt == 2) cfg.synth_waveforms[idx] = Waveform::TRIANGLE;
                else if (waveformInt == 3) cfg.synth_waveforms[idx] = Waveform::SAWTOOTH;
                else if (waveformInt == 4) cfg.synth_waveforms[idx] = Waveform::SAWTOOTH_INV;
                else if (waveformInt == 5) cfg.synth_waveforms[idx] = Waveform::PULSE;
                else cfg.synth_waveforms[idx] = Waveform::SINE;  // Default fallback
            }
        }
        else if (k.find("midi_instruments_gliding") == 0) {
            // Extract index from "midi_instruments_glidingN"
            std::string suffix = k.substr(24);  // Length of "midi_instruments_gliding"
            if (!suffix.empty()) {
                int idx = std::stoi(suffix);
                if (idx >= 0 && idx < 5) cfg.midi_instruments_gliding[idx] = std::stoi(v);
            }
        }
        else if (k.find("midi_instruments_dotted") == 0) {
            // Extract index from "midi_instruments_dottedN"
            std::string suffix = k.substr(23);  // Length of "midi_instruments_dotted"
            if (!suffix.empty()) {
                int idx = std::stoi(suffix);
                if (idx >= 0 && idx < 5) cfg.midi_instruments_dotted[idx] = std::stoi(v);
            }
        }
        else if (k == "synth_min_freq_hz") cfg.synth_min_freq_hz = std::stoi(v);
        else if (k == "synth_max_freq_hz") cfg.synth_max_freq_hz = std::stoi(v);
        else if (k == "midi_interpolated_pan_mode") cfg.midi_interpolated_pan_mode = (v == "1" || v == "true");
        else if (k == "midi_interpolation_strength") cfg.midi_interpolation_strength = std::stod(v);
        else if (k == "dotted_duration_ms") cfg.dotted_duration_ms = std::stoi(v);
        else if (k == "dotted_pause_ms") cfg.dotted_pause_ms = std::stoi(v);
        else if (k == "freeze_point_pause_ms") cfg.freeze_point_pause_ms = std::stoi(v);
        else if (k == "loop_pause_ms") cfg.loop_pause_ms = std::stoi(v);
        else if (k == "inverted_loop_gap_ms") cfg.inverted_loop_gap_ms = std::stoi(v);
        else if (k == "continuous_sweep_enabled") cfg.continuous_sweep_enabled = (v == "1" || v == "true");
        else if (k == "last_measurement_duration_seconds") cfg.last_measurement_duration_seconds = std::stod(v);
        else if (k == "navigation_jump_width") cfg.navigation_jump_width = std::stoi(v);
        else if (k == "calibration_bank") cfg.calibration_bank = std::stoi(v);
        else if (k == "table_columns") {
            // Parse comma-separated list of columns
            cfg.table_columns.clear();
            std::stringstream ss(v);
            std::string col;
            while (std::getline(ss, col, ',')) {
                trim(col);
                if (!col.empty()) {
                    cfg.table_columns.push_back(col);
                }
            }
        }
    }
    return true;
}

bool saveAppSettings(const AppConfig& cfg, const std::string& path, std::string& err) {
    std::filesystem::path p = std::filesystem::u8path(path);
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p);
    if (!ofs) { err = "Cannot write settings file: " + path; return false; }
    ofs << "# nanoVNA-cli-accessible settings\n";
    ofs << "serial_port=" << cfg.serial_port << "\n";
    ofs << "baud=" << cfg.baud << "\n";
    ofs << "start_freq=" << cfg.start_freq << "\n";
    ofs << "end_freq=" << cfg.end_freq << "\n";
    ofs << "step=" << cfg.step << "\n";
    ofs << "command_templates_file=" << cfg.command_templates_file << "\n";
    ofs << "audio=" << (cfg.audio ? "1" : "0") << "\n";
    ofs << "\n# Language and display settings\n";
    ofs << "language=" << cfg.language << "\n";
    ofs << "bandplan=" << cfg.bandplan << "\n";
    ofs << "first_start=" << (cfg.first_start ? "1" : "0") << "\n";
    ofs << "\n# Acoustic analysis settings\n";
    ofs << "acoustic_smooth_mode=" << (cfg.acoustic_smooth_mode ? "1" : "0") << "\n";
    ofs << "acoustic_time_seconds=" << cfg.acoustic_time_seconds << "\n";
    ofs << "continuous_replay=" << (cfg.continuous_replay ? "1" : "0") << "\n";
    for (int i = 0; i < 5; i++) {
        ofs << "curve_enabled" << i << "=" << (cfg.curve_enabled[i] ? "1" : "0") << "\n";
    }
    
    // Volume settings - separate for Synth and MIDI modes
    ofs << "\n# Volume settings for Synthesizer mode (balanced for waveform mixing)\n";
    ofs << "# SINE=100% (baseline), SQUARE=50%, TRIANGLE=70%, SAWTOOTH=50%, PULSE=50%\n";
    for (int i = 0; i < 5; i++) {
        ofs << "curve_volume_synth" << i << "=" << cfg.curve_volume_synth[i] << "\n";
    }
    ofs << "\n# Volume settings for MIDI mode (instruments pre-balanced)\n";
    for (int i = 0; i < 5; i++) {
        ofs << "curve_volume_midi" << i << "=" << cfg.curve_volume_midi[i] << "\n";
    }
    
    ofs << "\n# Master volume control (0-100%)\n";
    ofs << "master_volume=" << cfg.master_volume << "\n";
    
    ofs << "\n# Y-Axis Ruler (Lineal) settings\n";
    ofs << "ruler_volume=" << cfg.ruler_volume << "\n";
    ofs << "ruler_sound_mode=" << static_cast<int>(cfg.ruler_sound_mode) << "  # 0=Follow last curve, 1=Custom sound\n";
    ofs << "ruler_custom_sound_synth=" << cfg.ruler_custom_sound_synth << "  # Custom waveform for synth mode (0-5)\n";
    ofs << "ruler_custom_sound_midi_gliding=" << cfg.ruler_custom_sound_midi_gliding << "  # Custom MIDI instrument for gliding mode\n";
    ofs << "ruler_custom_sound_midi_dotted=" << cfg.ruler_custom_sound_midi_dotted << "  # Custom MIDI instrument for dotted mode\n";
    ofs << "ruler_blip_duration_ms=" << cfg.ruler_blip_duration_ms << "  # Duration of shortest blip in ms (30-500)\n";
    ofs << "ruler_lengthening_factor_percent=" << cfg.ruler_lengthening_factor_percent << "  # Lengthening factor in % for longer tones (100-500%, default 150%)\n";
    
    ofs << "\n# X-Axis Ruler settings\n";
    ofs << "x_axis_ruler_enabled=" << (cfg.x_axis_ruler_enabled ? "1" : "0") << "  # X-axis ruler enabled by default\n";
    ofs << "x_axis_ruler_volume=" << cfg.x_axis_ruler_volume << "  # X-axis ruler volume (0-100%, default 70%)\n";
    ofs << "x_axis_ruler_blip_duration_ms=" << cfg.x_axis_ruler_blip_duration_ms << "  # X-axis ruler blip duration in ms (30-200, default 50)\n";
    ofs << "x_axis_ruler_noise_type=" << cfg.x_axis_ruler_noise_type << "  # X-axis ruler noise type (0=White, 1=Pink, 2=Click, default 0)\n";
    ofs << "x_axis_ruler_midi_drum=" << cfg.x_axis_ruler_midi_drum << "  # X-axis ruler MIDI drum note (35-81, default 42)\n";
    
    ofs << "\n# Status line settings\n";
    ofs << "status_line_enabled=" << (cfg.status_line_enabled ? "1" : "0") << "  # Status line enabled by default\n";
    ofs << "status_line_content=" << cfg.status_line_content << "  # Status line content (0=Position, 1=Frequency, 2=SWR, 3=All)\n";
    ofs << "status_line_show_position=" << (cfg.status_line_show_position ? "1" : "0") << "  # Show position in status line\n";
    ofs << "status_line_show_frequency=" << (cfg.status_line_show_frequency ? "1" : "0") << "  # Show frequency in status line\n";
    ofs << "status_line_show_swr=" << (cfg.status_line_show_swr ? "1" : "0") << "  # Show SWR value in status line\n";
    ofs << "status_line_show_rl=" << (cfg.status_line_show_rl ? "1" : "0") << "  # Show Return Loss value in status line\n";
    ofs << "status_line_show_impedance=" << (cfg.status_line_show_impedance ? "1" : "0") << "  # Show Impedance magnitude value in status line\n";
    ofs << "status_line_show_reactance=" << (cfg.status_line_show_reactance ? "1" : "0") << "  # Show Reactance value in status line\n";
    ofs << "status_line_show_phase=" << (cfg.status_line_show_phase ? "1" : "0") << "  # Show Phase value in status line\n";
    
    ofs << "\n# Braille printer settings\n";
    ofs << "braille_protocol=" << static_cast<int>(cfg.braille_protocol) << "  # 0=Index V4 (Raster), 1=Index V5 (Floating Dot Area)\n";
    ofs << "braille_paper_size=" << static_cast<int>(cfg.braille_paper_size) << "  # 0=A4, 1=Letter, 2=A3, 3=Legal, 4=Blista 260x305, 5=Blista 270x340, 6=Blista 297x304\n";
    ofs << "braille_orientation=" << static_cast<int>(cfg.braille_orientation) << "  # 0=Portrait, 1=Landscape\n";
    ofs << "braille_coordinate_grid=" << static_cast<int>(cfg.braille_coordinate_grid) << "  # 0=None, 1=Dots at integers, 2=Grid lines\n";
    ofs << "braille_phase_discontinuity=" << static_cast<int>(cfg.braille_phase_discontinuity) << "  # 0=Arrows, 1=Vertical line with pattern\n";
    ofs << "braille_dpi=" << std::fixed << std::setprecision(1) << cfg.braille_dpi << "  # Dots per inch (10-40, default 18)\n";
    
    ofs << "\n# Advanced Braille printer parameters (Index protocol)\n";
    ofs << "braille_top_margin=" << cfg.braille_top_margin << "  # TM: Top margin in lines (0-10, 0=no margin for max space)\n";
    ofs << "braille_binding_indent=" << cfg.braille_binding_indent << "  # BI: Binding margin/indent (0-10, default 2)\n";
    ofs << "braille_chars_per_line=" << cfg.braille_chars_per_line << "  # CH: Characters per line (10-50, default 29)\n";
    ofs << "braille_line_spacing=" << cfg.braille_line_spacing << "  # LS: Line spacing in 0.1mm (50=5.0mm, 100=10.0mm)\n";
    
    ofs << "\n# Braille layout percentages (0.80-0.99 for width, 0.60-0.90 for height)\n";
    ofs << "braille_graph_width_percent_portrait=" << std::fixed << std::setprecision(2) << cfg.braille_graph_width_percent_portrait << "  # Portrait width % (default 0.95)\n";
    ofs << "braille_graph_width_percent_landscape=" << std::fixed << std::setprecision(2) << cfg.braille_graph_width_percent_landscape << "  # Landscape width % (default 0.98)\n";
    ofs << "braille_graph_height_percent_portrait=" << std::fixed << std::setprecision(2) << cfg.braille_graph_height_percent_portrait << "  # Portrait height % (default 0.70)\n";
    ofs << "braille_graph_height_percent_landscape=" << std::fixed << std::setprecision(2) << cfg.braille_graph_height_percent_landscape << "  # Landscape height % (default 0.85)\n";
    
    ofs << "\n# Braille origin and spacing (in mm)\n";
    ofs << "braille_origin_x_mm=" << std::fixed << std::setprecision(1) << cfg.braille_origin_x_mm << "  # Horizontal offset from left (0-10mm, default 3)\n";
    ofs << "braille_origin_y_mm=" << std::fixed << std::setprecision(1) << cfg.braille_origin_y_mm << "  # Vertical offset from text insertion (0-20mm, default 0)\n";
    ofs << "braille_y_axis_space_mm=" << std::fixed << std::setprecision(1) << cfg.braille_y_axis_space_mm << "  # Y-axis label space (1-5mm, default 2)\n";
    
    ofs << "\n# Braille curve patterns\n";
    for (int i = 0; i < 5; i++) {
        ofs << "braille_curve_pattern" << i << "=" << cfg.braille_curve_patterns[i] << "  # Curve " << i << " pattern (e.g., '0'=solid, '2-1'=draw 2+pause 1)\n";
    }
    
    ofs << "\n# Audio engine settings\n";
    ofs << "audio_engine=" << static_cast<int>(cfg.audio_engine) << "\n";
    ofs << "midi_playback_mode=" << static_cast<int>(cfg.midi_playback_mode) << "\n";
    for (int i = 0; i < 5; i++) {
        ofs << "midi_instrument" << i << "=" << cfg.midi_instruments[i] << "\n";
    }
    ofs << "\n# Synthesizer waveforms (0=SINE, 1=SQUARE, 2=TRIANGLE, 3=SAWTOOTH, 4=SAWTOOTH_INV, 5=PULSE)\n";
    for (int i = 0; i < 5; i++) {
        ofs << "synth_waveform" << i << "=" << static_cast<int>(cfg.synth_waveforms[i]) << "\n";
    }
    ofs << "\n# MIDI instrument presets for different playback modes\n";
    ofs << "# Gliding mode preset: Sustained instruments (strings, organs, pads, synth leads)\n";
    for (int i = 0; i < 5; i++) {
        ofs << "midi_instruments_gliding" << i << "=" << cfg.midi_instruments_gliding[i] << "\n";
    }
    ofs << "# Dotted mode preset: Percussive and articulated instruments\n";
    for (int i = 0; i < 5; i++) {
        ofs << "midi_instruments_dotted" << i << "=" << cfg.midi_instruments_dotted[i] << "\n";
    }
    ofs << "\n# Synthesizer frequency range (Hz)\n";
    ofs << "synth_min_freq_hz=" << cfg.synth_min_freq_hz << "\n";
    ofs << "synth_max_freq_hz=" << cfg.synth_max_freq_hz << "\n";
    
    ofs << "\n";
    ofs << "# MIDI interpolated panning (Mischtechniken)\n";
    ofs << "# Enable volume-based pan interpolation for smoother spatial transitions\n";
    ofs << "# 0=disabled, 1=enabled\n";
    ofs << "midi_interpolated_pan_mode=" << (cfg.midi_interpolated_pan_mode ? "1" : "0") << "\n";
    ofs << "\n";
    ofs << "# Interpolation strength: how much volume affects perceived pan position\n";
    ofs << "# Range: 0.0 (no effect) to 1.0 (maximum effect)\n";
    ofs << "# Recommended: 0.2-0.4 for subtle effect, 0.5-0.8 for pronounced effect\n";
    ofs << "midi_interpolation_strength=" << cfg.midi_interpolation_strength << "\n";
    
    ofs << "\n# Dotted mode settings\n";
    ofs << "dotted_duration_ms=" << cfg.dotted_duration_ms << "\n";
    ofs << "dotted_pause_ms=" << cfg.dotted_pause_ms << "\n";
    ofs << "freeze_point_pause_ms=" << cfg.freeze_point_pause_ms << "\n";
    ofs << "loop_pause_ms=" << cfg.loop_pause_ms << "\n";
    ofs << "inverted_loop_gap_ms=" << cfg.inverted_loop_gap_ms << "\n";
    ofs << "\n# Continuous sweep settings\n";
    ofs << "continuous_sweep_enabled=" << (cfg.continuous_sweep_enabled ? "1" : "0") << "\n";
    ofs << "last_measurement_duration_seconds=" << cfg.last_measurement_duration_seconds << "\n";
    ofs << "\n# Navigation settings\n";
    ofs << "navigation_jump_width=" << cfg.navigation_jump_width << "\n";
    ofs << "\n# Calibration settings\n";
    ofs << "calibration_bank=" << cfg.calibration_bank << "\n";
    ofs << "\n# Table view preferences\n";
    ofs << "table_columns=";
    for (size_t i = 0; i < cfg.table_columns.size(); i++) {
        if (i > 0) ofs << ",";
        ofs << cfg.table_columns[i];
    }
    ofs << "\n";
    ofs.flush();
    return true;
}

// Save only braille printer settings to a profile file
bool saveBrailleProfile(const AppConfig& cfg, const std::string& profileName, std::string& err) {
    std::string profilePath = "config/" + profileName;
    // Check if path ends with ".ini" (C++17 compatible)
    const std::string suffix = ".ini";
    if (profilePath.size() < suffix.size() || 
        profilePath.compare(profilePath.size() - suffix.size(), suffix.size(), suffix) != 0) {
        profilePath += ".ini";
    }
    
    std::filesystem::path p = std::filesystem::u8path(profilePath);
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p);
    if (!ofs) { 
        err = "Cannot write profile file: " + profilePath; 
        return false; 
    }
    
    ofs << "# Braille Printer Profile: " << profileName << "\n";
    ofs << "# This profile contains braille printer-specific settings\n\n";
    
    ofs << "# Braille printer settings\n";
    ofs << "braille_protocol=" << static_cast<int>(cfg.braille_protocol) << "  # 0=Index V4 (Raster), 1=Index V5 (Floating Dot Area)\n";
    ofs << "braille_paper_size=" << static_cast<int>(cfg.braille_paper_size) << "  # 0=A4, 1=Letter, 2=A3, 3=Legal, 4=Blista 260x305, 5=Blista 270x340, 6=Blista 297x304\n";
    ofs << "braille_orientation=" << static_cast<int>(cfg.braille_orientation) << "  # 0=Portrait, 1=Landscape\n";
    ofs << "braille_coordinate_grid=" << static_cast<int>(cfg.braille_coordinate_grid) << "  # 0=None, 1=Dots at integers, 2=Grid lines\n";
    ofs << "braille_phase_discontinuity=" << static_cast<int>(cfg.braille_phase_discontinuity) << "  # 0=Arrows, 1=Vertical line with pattern\n";
    ofs << "braille_dpi=" << std::fixed << std::setprecision(1) << cfg.braille_dpi << "  # Dots per inch (10-40, default 18)\n";
    
    ofs << "\n# Advanced Braille printer parameters (Index protocol)\n";
    ofs << "braille_top_margin=" << cfg.braille_top_margin << "  # TM: Top margin in lines (0-10, 0=no margin for max space)\n";
    ofs << "braille_binding_indent=" << cfg.braille_binding_indent << "  # BI: Binding margin/indent (0-10, default 2)\n";
    ofs << "braille_chars_per_line=" << cfg.braille_chars_per_line << "  # CH: Characters per line (10-50, default 29)\n";
    ofs << "braille_line_spacing=" << cfg.braille_line_spacing << "  # LS: Line spacing in 0.1mm (50=5.0mm, 100=10.0mm)\n";
    
    ofs << "\n# Braille layout percentages (0.80-0.99 for width, 0.60-0.90 for height)\n";
    ofs << "braille_graph_width_percent_portrait=" << std::fixed << std::setprecision(2) << cfg.braille_graph_width_percent_portrait << "  # Portrait width % (default 0.95)\n";
    ofs << "braille_graph_width_percent_landscape=" << std::fixed << std::setprecision(2) << cfg.braille_graph_width_percent_landscape << "  # Landscape width % (default 0.98)\n";
    ofs << "braille_graph_height_percent_portrait=" << std::fixed << std::setprecision(2) << cfg.braille_graph_height_percent_portrait << "  # Portrait height % (default 0.70)\n";
    ofs << "braille_graph_height_percent_landscape=" << std::fixed << std::setprecision(2) << cfg.braille_graph_height_percent_landscape << "  # Landscape height % (default 0.85)\n";
    
    ofs << "\n# Braille origin and spacing (in mm)\n";
    ofs << "braille_origin_x_mm=" << std::fixed << std::setprecision(1) << cfg.braille_origin_x_mm << "  # Horizontal offset from left (0-10mm, default 3)\n";
    ofs << "braille_origin_y_mm=" << std::fixed << std::setprecision(1) << cfg.braille_origin_y_mm << "  # Vertical offset from text insertion (0-20mm, default 0)\n";
    ofs << "braille_y_axis_space_mm=" << std::fixed << std::setprecision(1) << cfg.braille_y_axis_space_mm << "  # Y-axis label space (1-5mm, default 2)\n";
    
    ofs << "\n# Braille curve patterns\n";
    for (int i = 0; i < 5; i++) {
        ofs << "braille_curve_pattern" << i << "=" << cfg.braille_curve_patterns[i] << "  # Curve " << i << " pattern (e.g., '0'=solid, '2-1'=draw 2+pause 1)\n";
    }
    
    ofs.flush();
    return true;
}

// Load braille printer settings from a profile file
bool loadBrailleProfile(AppConfig& cfg, const std::string& profileName, std::string& err) {
    std::string profilePath = "config/" + profileName;
    // Check if path ends with ".ini" (C++17 compatible)
    const std::string suffix = ".ini";
    if (profilePath.size() < suffix.size() || 
        profilePath.compare(profilePath.size() - suffix.size(), suffix.size(), suffix) != 0) {
        profilePath += ".ini";
    }
    
    std::filesystem::path p = std::filesystem::u8path(profilePath);
    if (!std::filesystem::exists(p)) {
        err = "Profile not found: " + profilePath;
        return false;
    }
    
    std::ifstream ifs(p);
    if (!ifs) { 
        err = "Cannot open profile file: " + profilePath; 
        return false; 
    }
    
    std::string line;
    while (std::getline(ifs, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0, pos);
        std::string v = line.substr(pos + 1);
        trim(k); trim(v);
        
        // Remove inline comments
        auto commentPos = v.find('#');
        if (commentPos != std::string::npos) {
            v = v.substr(0, commentPos);
            trim(v);
        }
        
        // Parse braille settings
        if (k == "braille_protocol") {
            int proto = std::stoi(v);
            if (proto == 0) cfg.braille_protocol = AppConfig::BrailleProtocol::INDEX_V4;
            else if (proto == 1) cfg.braille_protocol = AppConfig::BrailleProtocol::INDEX_V5;
        }
        else if (k == "braille_paper_size") {
            int size = std::stoi(v);
            if (size >= 0 && size <= 6) {
                cfg.braille_paper_size = static_cast<AppConfig::BraillePaperSize>(size);
            }
        }
        else if (k == "braille_orientation") {
            int orient = std::stoi(v);
            if (orient == 0) cfg.braille_orientation = AppConfig::BrailleOrientation::PORTRAIT;
            else if (orient == 1) cfg.braille_orientation = AppConfig::BrailleOrientation::LANDSCAPE;
        }
        else if (k == "braille_coordinate_grid") {
            int grid = std::stoi(v);
            if (grid >= 0 && grid <= 2) {
                cfg.braille_coordinate_grid = static_cast<AppConfig::BrailleCoordinateGrid>(grid);
            }
        }
        else if (k == "braille_phase_discontinuity") {
            int mode = std::stoi(v);
            if (mode == 0) cfg.braille_phase_discontinuity = AppConfig::BraillePhaseDiscontinuityMode::ARROWS;
            else if (mode == 1) cfg.braille_phase_discontinuity = AppConfig::BraillePhaseDiscontinuityMode::VERTICAL_LINE;
        }
        else if (k == "braille_dpi") {
            try {
                double dpi = std::stod(v);
                if (dpi >= 10.0 && dpi <= 40.0) {
                    cfg.braille_dpi = dpi;
                }
            } catch (...) {}
        }
        else if (k == "braille_top_margin") {
            try {
                int tm = std::stoi(v);
                if (tm >= 0 && tm <= 10) cfg.braille_top_margin = tm;
            } catch (...) {}
        }
        else if (k == "braille_binding_indent") {
            try {
                int bi = std::stoi(v);
                if (bi >= 0 && bi <= 10) cfg.braille_binding_indent = bi;
            } catch (...) {}
        }
        else if (k == "braille_chars_per_line") {
            try {
                int ch = std::stoi(v);
                if (ch >= 10 && ch <= 50) cfg.braille_chars_per_line = ch;
            } catch (...) {}
        }
        else if (k == "braille_line_spacing") {
            try {
                int ls = std::stoi(v);
                if (ls >= 50 && ls <= 100) cfg.braille_line_spacing = ls;
            } catch (...) {}
        }
        else if (k == "braille_graph_width_percent_portrait") {
            try {
                double pct = std::stod(v);
                if (pct >= 0.8 && pct <= 0.99) cfg.braille_graph_width_percent_portrait = pct;
            } catch (...) {}
        }
        else if (k == "braille_graph_width_percent_landscape") {
            try {
                double pct = std::stod(v);
                if (pct >= 0.8 && pct <= 0.99) cfg.braille_graph_width_percent_landscape = pct;
            } catch (...) {}
        }
        else if (k == "braille_graph_height_percent_portrait") {
            try {
                double pct = std::stod(v);
                if (pct >= 0.6 && pct <= 0.9) cfg.braille_graph_height_percent_portrait = pct;
            } catch (...) {}
        }
        else if (k == "braille_graph_height_percent_landscape") {
            try {
                double pct = std::stod(v);
                if (pct >= 0.6 && pct <= 0.9) cfg.braille_graph_height_percent_landscape = pct;
            } catch (...) {}
        }
        else if (k == "braille_origin_x_mm") {
            try {
                double mm = std::stod(v);
                if (mm >= 0.0 && mm <= 10.0) cfg.braille_origin_x_mm = mm;
            } catch (...) {}
        }
        else if (k == "braille_origin_y_mm") {
            try {
                double mm = std::stod(v);
                if (mm >= 0.0 && mm <= 20.0) cfg.braille_origin_y_mm = mm;
            } catch (...) {}
        }
        else if (k == "braille_y_axis_space_mm") {
            try {
                double mm = std::stod(v);
                if (mm >= 1.0 && mm <= 5.0) cfg.braille_y_axis_space_mm = mm;
            } catch (...) {}
        }
        // Check if key starts with "braille_curve_pattern" (C++17 compatible)
        else if (k.size() >= 21 && k.compare(0, 21, "braille_curve_pattern") == 0) {
            int idx = k.back() - '0';
            if (idx >= 0 && idx < 5) {
                cfg.braille_curve_patterns[idx] = v;
            }
        }
    }
    
    return true;
}

// List available braille printer profiles
std::vector<std::string> listBrailleProfiles() {
    std::vector<std::string> profiles;
    
    try {
        std::filesystem::path configDir = std::filesystem::u8path("config");
        if (std::filesystem::exists(configDir) && std::filesystem::is_directory(configDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(configDir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    // Check if filename ends with ".ini" (C++17 compatible)
                    const std::string suffix = ".ini";
                    bool endsWithIni = filename.size() >= suffix.size() && 
                                      filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
                    // Only include .ini files, excluding main settings files
                    if (endsWithIni && 
                        filename != "app_settings.cfg" &&
                        filename != "cables.cfg" &&
                        filename != "command_templates.cfg") {
                        // Remove .ini extension for display
                        std::string profileName = filename.substr(0, filename.length() - 4);
                        profiles.push_back(profileName);
                    }
                }
            }
        }
    } catch (...) {
        // If directory reading fails, return empty list
    }
    
    return profiles;
}
