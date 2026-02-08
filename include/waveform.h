#pragma once

/**
 * Waveform types for audio synthesis
 * 
 * This enum is shared by both the AudioEngine and SynthesizerEngine
 * to define the types of waveforms that can be generated.
 */
enum class Waveform { 
    SINE,           // Pure sine wave
    SQUARE,         // Square wave
    TRIANGLE,       // Triangle wave
    SAWTOOTH,       // Sawtooth wave (rising)
    SAWTOOTH_INV,   // Inverse sawtooth (falling)
    PULSE           // Pulse wave (25% duty cycle)
};
