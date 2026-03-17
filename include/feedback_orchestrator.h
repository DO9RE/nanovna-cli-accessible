#pragma once

#ifdef WITH_HAM_SPIRIT

#include "feedback_types.h"
#include <mutex>
#include <functional>

namespace HamSpirit {

/**
 * @file feedback_orchestrator.h
 * @brief Central feedback orchestrator for deterministic cross-channel
 *        coordination of audio, haptics, and TTS.
 *
 * Implements strategy 6.1 (Central Event-Orchestrierung) from the
 * reactivity analysis (doc/hamspirit_reactivity_analysis.md).
 *
 * Design principles:
 * - Single entry point for all feedback triggers.
 * - Events carry timestamps for sample-accurate audio rendering.
 * - Critical events (P0) are pushed to a lock-free immediate queue.
 * - Non-critical events use the existing frame-based AudioParams path.
 * - Thread-safe: game thread writes, audio thread reads.
 */
class FeedbackOrchestrator {
public:
    FeedbackOrchestrator() = default;

    // ----------------------------------------------------------------
    // Configuration
    // ----------------------------------------------------------------

    /** Set the immediate audio event queue (shared with audio thread). */
    void setImmediateQueue(ImmediateAudioQueue* queue) { immediateQueue_ = queue; }

    /** Set the latency tracker instance. */
    void setLatencyTracker(LatencyTracker* tracker) { latencyTracker_ = tracker; }

    // ----------------------------------------------------------------
    // P0 (Critical) – Morse Cannon Events
    // ----------------------------------------------------------------

    /**
     * Trigger morse cannon key-down (start sidetone).
     *
     * Pushes an immediate event to the lock-free queue so the audio thread
     * can start the tone within the current audio frame at the correct
     * sample offset, rather than waiting for the next frame boundary.
     *
     * @param isDash    true for dash, false for dot
     * @param pan       stereo position (0..1)
     * @param playerIdx source player index
     */
    void triggerMorseKeyDown(bool isDash, float pan = 0.5f, int playerIdx = 0) {
        auto now = FeedbackClock::now();
        if (immediateQueue_) {
            ImmediateAudioEvent evt;
            evt.type = ImmediateAudioEvent::Type::MORSE_KEY_DOWN;
            evt.timestamp = now;
            evt.priority = FeedbackPriority::CRITICAL;
            evt.isDash = isDash;
            evt.pan = pan;
            evt.playerIndex = playerIdx;
            immediateQueue_->push(evt);
        }
        lastMorseEventTime_ = now;
    }

    /**
     * Trigger morse cannon key-up (stop sidetone).
     *
     * @param playerIdx source player index
     */
    void triggerMorseKeyUp(int playerIdx = 0) {
        auto now = FeedbackClock::now();
        if (immediateQueue_) {
            ImmediateAudioEvent evt;
            evt.type = ImmediateAudioEvent::Type::MORSE_KEY_UP;
            evt.timestamp = now;
            evt.priority = FeedbackPriority::CRITICAL;
            evt.playerIndex = playerIdx;
            immediateQueue_->push(evt);
        }
        lastMorseEventTime_ = now;
    }

    // ----------------------------------------------------------------
    // P1 (High) – UI and Game Sound Events
    // ----------------------------------------------------------------

    /** Trigger a generic immediate sound event. */
    void triggerSound(ImmediateAudioEvent::Type type, int durationMs = 0,
                      float pan = 0.5f, bool ascending = true) {
        if (!immediateQueue_) return;
        ImmediateAudioEvent evt;
        evt.type = type;
        evt.timestamp = FeedbackClock::now();
        evt.priority = FeedbackPriority::HIGH;
        evt.pan = pan;
        evt.durationMs = durationMs;
        evt.ascending = ascending;
        immediateQueue_->push(evt);
    }

    // ----------------------------------------------------------------
    // Latency Tracking Helpers
    // ----------------------------------------------------------------

    /** Record an input event for latency measurement. */
    size_t recordInputEvent() {
        if (latencyTracker_ && latencyTracker_->isEnabled()) {
            return latencyTracker_->recordInput(FeedbackClock::now());
        }
        return 0;
    }

    /** Record audio state write for latency measurement. */
    void recordAudioStateWrite(size_t measurementIdx) {
        if (latencyTracker_ && latencyTracker_->isEnabled()) {
            latencyTracker_->recordAudioStateWrite(measurementIdx, FeedbackClock::now());
        }
    }

    /** Record audio read for latency measurement (called from audio thread). */
    void recordAudioRead(size_t measurementIdx) {
        if (latencyTracker_ && latencyTracker_->isEnabled()) {
            latencyTracker_->recordAudioRead(measurementIdx, FeedbackClock::now());
        }
    }

    /** Record audio render completion (called from audio thread). */
    void recordAudioRender(size_t measurementIdx) {
        if (latencyTracker_ && latencyTracker_->isEnabled()) {
            latencyTracker_->recordAudioRender(measurementIdx, FeedbackClock::now());
        }
    }

    /** Get last morse event timestamp. */
    FeedbackTimePoint getLastMorseEventTime() const { return lastMorseEventTime_; }

private:
    ImmediateAudioQueue* immediateQueue_ = nullptr;
    LatencyTracker* latencyTracker_ = nullptr;
    FeedbackTimePoint lastMorseEventTime_{};
};

} // namespace HamSpirit

#endif // WITH_HAM_SPIRIT
