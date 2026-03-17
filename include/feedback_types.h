#pragma once

#ifdef WITH_HAM_SPIRIT

#include <chrono>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>

namespace HamSpirit {

/**
 * @file feedback_types.h
 * @brief Core types for the deterministic feedback pipeline.
 *
 * Implements strategies 6.3 (Shared Time Basis), 6.5 (Event-Timestamping),
 * 6.7 (Priority Model), and 6.8 (Latency Tracking) from the reactivity
 * analysis (doc/hamspirit_reactivity_analysis.md).
 */

// ============================================================================
// 6.7 – Priority Model for Feedback Events
// ============================================================================

/**
 * Feedback priority levels.
 * Determines processing order and latency budget for each event category.
 *
 * - CRITICAL (P0): Morse sidetone, collisions — immediate audio path.
 * - HIGH     (P1): UI navigation sounds — next audio frame.
 * - NORMAL   (P2): Background audio, ambient — best-effort.
 * - LOW      (P3): Non-essential TTS, status — may be deferred.
 */
enum class FeedbackPriority : uint8_t {
    CRITICAL = 0,   // Morse, collisions – immediate processing
    HIGH     = 1,   // UI navigation – next frame
    NORMAL   = 2,   // Background audio – best effort
    LOW      = 3    // Non-essential TTS – may be delayed
};

// ============================================================================
// 6.3 – Shared Time Basis
// ============================================================================

/// Alias for the system-wide monotonic clock used by all subsystems.
using FeedbackClock = std::chrono::steady_clock;
using FeedbackTimePoint = FeedbackClock::time_point;

/**
 * Calculate the sample offset for an event within an audio frame.
 *
 * Given the timestamp of an event and the start time of the current audio
 * frame, returns the sample index at which the event should take effect.
 * Clamps to [0, samplesPerFrame).
 *
 * @param eventTime     Timestamp of the triggering event
 * @param frameStart    Timestamp when the current audio frame began rendering
 * @param sampleRate    Audio sample rate (e.g. 44100)
 * @param samplesPerFrame  Number of samples in one frame (e.g. 1764)
 * @return Sample offset within the current frame
 */
inline int calculateSampleOffset(FeedbackTimePoint eventTime,
                                  FeedbackTimePoint frameStart,
                                  int sampleRate,
                                  int samplesPerFrame) {
    auto elapsed = std::chrono::duration<double>(eventTime - frameStart);
    double offsetD = elapsed.count() * static_cast<double>(sampleRate);
    // Guard against extreme values before int conversion
    if (offsetD < 0.0) return 0;
    if (offsetD >= static_cast<double>(samplesPerFrame)) return samplesPerFrame - 1;
    return static_cast<int>(offsetD);
}

// ============================================================================
// 6.4 – Immediate Audio Event (for lock-free queue)
// ============================================================================

/**
 * Lightweight event pushed from the game thread to the audio thread
 * via a lock-free SPSC ring buffer for time-critical sounds.
 *
 * Only P0 (CRITICAL) events use this path.  All other events continue
 * to use the frame-based AudioParams mechanism.
 */
struct ImmediateAudioEvent {
    enum class Type : uint8_t {
        MORSE_KEY_DOWN  = 0,   // Start morse sidetone
        MORSE_KEY_UP    = 1,   // Stop morse sidetone
        // UI / game sound triggers — bypass frame-based AudioParams for immediate playback
        MENU_NAV        = 2,   // Menu navigation click
        MENU_SELECT     = 3,   // Menu selection confirmation
        KEY_CLICK       = 4,   // Key click (morse pattern input)
        BUMPER          = 5,   // Bumper/limit buzz
        COLLECT         = 6,   // Collection success chime
        MISS_AIM        = 7,   // Wrong aim buzz
        MISS_MORSE      = 8,   // Wrong morse tone
        PAUSE           = 9,   // Pause tone
        UNPAUSE         = 10,  // Unpause tone
        ADJUST          = 11,  // Tuner adjustment sweep
        PA_DAMAGE       = 12,  // PA damage crackle
        PA_REPAIR       = 13,  // PA repair chime
        STATUS_START    = 14,  // Status readout start
        STATUS_DONE     = 15,  // Status readout done
        COLLISION       = 16,  // Border collision / crash
        EMERGENCY_BRAKE = 17,  // Emergency brake screech
        AIM_RESET       = 18,  // Aim reset swoosh
    };

    Type type = Type::MORSE_KEY_DOWN;
    FeedbackTimePoint timestamp{};          // When the input occurred
    FeedbackPriority priority = FeedbackPriority::CRITICAL;
    bool isDash = false;                    // For morse: dash vs dot
    float pan = 0.5f;                       // Stereo position
    int playerIndex = 0;                    // Source player
    int durationMs = 0;                     // Duration in ms (for sounds with variable length)
    bool ascending = true;                  // For adjust/pause: direction
};

// ============================================================================
// Lock-free Single-Producer Single-Consumer Ring Buffer
// ============================================================================

/**
 * SPSC ring buffer for audio-thread communication.
 *
 * - Game thread is the sole producer.
 * - Audio thread is the sole consumer.
 * - No locks, no allocations in push/pop.
 * - Fixed capacity N (power of 2 recommended but not required).
 */
template<typename T, size_t N>
class SPSCRingBuffer {
public:
    SPSCRingBuffer() = default;

    /**
     * Push an item (producer side).
     * @return true on success, false if full.
     */
    bool push(const T& item) {
        size_t wp = writePos_.load(std::memory_order_relaxed);
        size_t next = (wp + 1) % N;
        if (next == readPos_.load(std::memory_order_acquire))
            return false;  // Full
        buffer_[wp] = item;
        writePos_.store(next, std::memory_order_release);
        return true;
    }

    /**
     * Pop an item (consumer side).
     * @return true if an item was retrieved, false if empty.
     */
    bool pop(T& item) {
        size_t rp = readPos_.load(std::memory_order_relaxed);
        if (rp == writePos_.load(std::memory_order_acquire))
            return false;  // Empty
        item = buffer_[rp];
        readPos_.store((rp + 1) % N, std::memory_order_release);
        return true;
    }

    /** Check if the buffer is empty (consumer side). */
    bool empty() const {
        return readPos_.load(std::memory_order_acquire) ==
               writePos_.load(std::memory_order_acquire);
    }

private:
    std::array<T, N> buffer_{};
    std::atomic<size_t> readPos_{0};
    std::atomic<size_t> writePos_{0};
};

/// Capacity for the immediate audio event queue.
static constexpr size_t IMMEDIATE_AUDIO_QUEUE_SIZE = 64;

/// Type alias for the immediate audio event queue.
using ImmediateAudioQueue = SPSCRingBuffer<ImmediateAudioEvent, IMMEDIATE_AUDIO_QUEUE_SIZE>;

// ============================================================================
// 6.8 – Latency Tracking
// ============================================================================

/**
 * Single pipeline latency measurement.
 */
struct LatencyMeasurement {
    FeedbackTimePoint inputTime{};          // When input was captured
    FeedbackTimePoint eventCreationTime{};  // When event was created
    FeedbackTimePoint audioStateWriteTime{};// When audio state was written
    FeedbackTimePoint audioReadTime{};      // When audio thread read the state
    FeedbackTimePoint audioRenderTime{};    // When audio buffer was rendered

    /** Total pipeline latency in milliseconds (input → render). */
    float totalLatencyMs() const {
        if (audioRenderTime == FeedbackTimePoint{} || inputTime == FeedbackTimePoint{})
            return -1.0f;  // Not measured
        return std::chrono::duration<float, std::milli>(audioRenderTime - inputTime).count();
    }

    /** Input-to-audio-write latency in milliseconds. */
    float inputToWriteMs() const {
        if (audioStateWriteTime == FeedbackTimePoint{} || inputTime == FeedbackTimePoint{})
            return -1.0f;
        return std::chrono::duration<float, std::milli>(audioStateWriteTime - inputTime).count();
    }

    /** Audio-write-to-render latency in milliseconds. */
    float writeToRenderMs() const {
        if (audioRenderTime == FeedbackTimePoint{} || audioStateWriteTime == FeedbackTimePoint{})
            return -1.0f;
        return std::chrono::duration<float, std::milli>(audioRenderTime - audioStateWriteTime).count();
    }
};

/**
 * Ring-buffer based latency tracker.
 *
 * Records the most recent measurements and provides running statistics.
 * Thread-safe for single-writer (game thread records input/write times,
 * audio thread records read/render times — writes to different fields
 * of non-overlapping entries).
 */
class LatencyTracker {
public:
    static constexpr size_t HISTORY_SIZE = 128;

    LatencyTracker() = default;

    /** Enable or disable tracking (disabled by default for zero overhead). */
    void setEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_relaxed); }
    bool isEnabled() const { return enabled_.load(std::memory_order_relaxed); }

    /**
     * Record the input timestamp for the current measurement cycle.
     * Call from the game thread when an input event is captured.
     * @return Measurement index to pass to subsequent record* calls.
     */
    size_t recordInput(FeedbackTimePoint t) {
        if (!enabled_.load(std::memory_order_relaxed)) return 0;
        size_t idx = writeIndex_.fetch_add(1, std::memory_order_relaxed) % HISTORY_SIZE;
        measurements_[idx] = LatencyMeasurement{};
        measurements_[idx].inputTime = t;
        return idx;
    }

    /** Record when the event was created (game thread). */
    void recordEventCreation(size_t idx, FeedbackTimePoint t) {
        if (!enabled_.load(std::memory_order_relaxed)) return;
        measurements_[idx % HISTORY_SIZE].eventCreationTime = t;
    }

    /** Record when audio state was written (game thread). */
    void recordAudioStateWrite(size_t idx, FeedbackTimePoint t) {
        if (!enabled_.load(std::memory_order_relaxed)) return;
        measurements_[idx % HISTORY_SIZE].audioStateWriteTime = t;
    }

    /** Record when audio thread read the state. */
    void recordAudioRead(size_t idx, FeedbackTimePoint t) {
        if (!enabled_.load(std::memory_order_relaxed)) return;
        measurements_[idx % HISTORY_SIZE].audioReadTime = t;
    }

    /** Record when audio was rendered. */
    void recordAudioRender(size_t idx, FeedbackTimePoint t) {
        if (!enabled_.load(std::memory_order_relaxed)) return;
        measurements_[idx % HISTORY_SIZE].audioRenderTime = t;
    }

    /** Get the most recent complete measurement. */
    LatencyMeasurement getLastMeasurement() const {
        size_t idx = writeIndex_.load(std::memory_order_relaxed);
        if (idx == 0) return {};
        return measurements_[(idx - 1) % HISTORY_SIZE];
    }

    /**
     * Calculate average total latency over recent measurements.
     * @return Average latency in ms, or -1 if no valid measurements.
     */
    float getAverageLatencyMs() const {
        float sum = 0.0f;
        int count = 0;
        size_t end = writeIndex_.load(std::memory_order_relaxed);
        size_t start = (end > HISTORY_SIZE) ? (end - HISTORY_SIZE) : 0;
        for (size_t i = start; i < end; i++) {
            float ms = measurements_[i % HISTORY_SIZE].totalLatencyMs();
            if (ms >= 0.0f) {
                sum += ms;
                count++;
            }
        }
        return (count > 0) ? (sum / static_cast<float>(count)) : -1.0f;
    }

private:
    std::array<LatencyMeasurement, HISTORY_SIZE> measurements_{};
    std::atomic<size_t> writeIndex_{0};
    std::atomic<bool> enabled_{false};
};

} // namespace HamSpirit

#endif // WITH_HAM_SPIRIT
