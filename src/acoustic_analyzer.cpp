#include "acoustic_analyzer.h"
#include "config.h"
#include "synthesizer_engine.h"
#include "midi_engine.h"
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstring>
#include <sstream>
#include <iostream>
#include <iomanip>

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

// Cross-platform sleep
static void platform_sleep_ms(int ms) {
#if defined(_WIN32)
    Sleep(ms);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

static constexpr double PI = 3.14159265358979323846;
static constexpr int SAMPLE_RATE = 44100;
static constexpr int CHANNELS = 2;
static constexpr int BITS = 16;

AcousticAnalyzer::AcousticAnalyzer(Logger* logger_, MathLogger* mathLogger_, TranslationManager* translation_) 
    : logger(logger_), mathLogger(mathLogger_), translation(translation_), state(PlaybackState::STOPPED), currentPos(0),
      loopLeft(0), loopRight(0), loopEnabled(false), continuousReplay(false), loopZoomEnabled(false), loopInverted(false), loopPauseMs(0), invertedLoopGapMs(0),
      smoothMode(false), playbackTimeSeconds(5),  // Default 5 seconds per sweep
      minFreqHz(SYNTH_MIN_FREQ_HZ_LIMIT), maxFreqHz(SYNTH_MAX_FREQ_HZ_LIMIT),  // Default frequency range
      dottedDurationMs(100),  // Default 100ms dot duration
      dottedPauseMs(50),      // Default 50ms pause duration
      freezePointPauseMs(200), // Default 200ms freeze point pause
      needsPointSelection(true),  // Initialize point selection flag
      swrPhase(0.0), rlPhase(0.0), zPhase(0.0), xPhase(0.0), 
      phasePhaseL(0.0), phasePhaseR(0.0),
      shouldStop(false), buffersWereFlushed(false),
      rulerPlaying(false), rulerShouldStop(false), 
      rulerVolume(100), rulerBlipDurationMs(80), rulerLengtheningFactorPercent(150), stateBeforeRuler(PlaybackState::STOPPED),
      xAxisRulerEnabled(false), xAxisRulerVolume(80), xAxisRulerBlipDurationMs(50), lastXAxisBlipPosition(SIZE_MAX),
      xAxisRulerNoiseType(XAxisRulerNoiseType::WHITE_NOISE), xAxisRulerMidiDrum(42),
      statusLineEnabled(false), statusLineContent(StatusLineContent::ALL),
      statusLineShowPosition(true), statusLineShowFrequency(true), statusLineShowSWR(true),
      statusLineShowRL(false), statusLineShowImpedance(false), statusLineShowReactance(false), statusLineShowPhase(false),
      rulerSoundMode(RulerSoundMode::FOLLOW_LAST_CURVE),
      rulerCustomSoundSynth(0), rulerCustomSoundMidiGliding(48), rulerCustomSoundMidiDotted(11),
      lastEnabledCurve(0)
#if defined(_WIN32)
      , hWaveOut(nullptr), audioDeviceOpen(false),
      nextBufferToQueue(0), nextBufferToCheck(0), buffersInFlight(0)
#endif
{
    
    // Initialize curves with translation keys (actual names will be retrieved via getCurveName)
    curves[0].name = "CURVE_NAME_SWR";
    curves[0].enabled = true;  // SWR enabled by default
    
    curves[1].name = "CURVE_NAME_RETURN_LOSS";
    curves[1].enabled = false;
    
    curves[2].name = "CURVE_NAME_IMPEDANCE_MAG";
    curves[2].enabled = false;
    
    curves[3].name = "CURVE_NAME_REACTANCE";
    curves[3].enabled = false;
    
    curves[4].name = "CURVE_NAME_PHASE";
    curves[4].enabled = false;
    
    // Initialize volumes to 100%
    for (int i = 0; i < 5; i++) {
        curveVolumes[i] = 100;
    }
    masterVolume = 100;  // Initialize master volume to 100%
    
#if defined(_WIN32)
    // Initialize double-buffering structures
    initializeBuffers();
#endif
}

AcousticAnalyzer::~AcousticAnalyzer() noexcept {
    // Destructor must not throw - wrap all operations in try-catch
    try {
        stop();
    } catch (const std::exception& e) {
        if (logger) logger->log("ACOUSTIC", std::string("Exception in stop() during destructor: ") + e.what());
    } catch (...) {
        if (logger) logger->log("ACOUSTIC", "Unknown exception in stop() during destructor");
    }
    
    try {
        stopYAxisRuler();  // Stop ruler thread if running
    } catch (const std::exception& e) {
        if (logger) logger->log("ACOUSTIC", std::string("Exception in stopYAxisRuler() during destructor: ") + e.what());
    } catch (...) {
        if (logger) logger->log("ACOUSTIC", "Unknown exception in stopYAxisRuler() during destructor");
    }
    
#if defined(_WIN32)
    try {
        closeAudioDevice();
    } catch (const std::exception& e) {
        if (logger) logger->log("ACOUSTIC", std::string("Exception in closeAudioDevice() during destructor: ") + e.what());
    } catch (...) {
        if (logger) logger->log("ACOUSTIC", "Unknown exception in closeAudioDevice() during destructor");
    }
    
    try {
        cleanupBuffers();
    } catch (const std::exception& e) {
        if (logger) logger->log("ACOUSTIC", std::string("Exception in cleanupBuffers() during destructor: ") + e.what());
    } catch (...) {
        if (logger) logger->log("ACOUSTIC", "Unknown exception in cleanupBuffers() during destructor");
    }
#endif
}

void AcousticAnalyzer::setData(const std::vector<MeasurementPoint>& data) {
    stop();
    measurementData = data;
    currentPos = 0;
    loopLeft = 0;
    loopRight = data.empty() ? 0 : data.size() - 1;
    loopEnabled = false;
    continuousReplay = false;
    loopZoomEnabled = false;
    needsPointSelection.store(true);  // Recalculate point selection with new data
    if (logger) {
        logger->log("ACOUSTIC", "Data set: " + std::to_string(data.size()) + " points");
    }
}

void AcousticAnalyzer::updateData(const std::vector<MeasurementPoint>& data) {
    // Update data while preserving playback state and position
    // This is used for continuous sweep mode
    if (data.empty()) return;
    
    size_t oldSize = measurementData.size();
    measurementData = data;
    
    // Adjust loop markers if data size changed
    if (loopRight >= data.size()) {
        loopRight = data.size() - 1;
    }
    
    // Adjust current position if out of bounds
    if (currentPos >= data.size()) {
        currentPos = data.size() - 1;
    }
    
    needsPointSelection.store(true);  // Recalculate point selection with updated data
    
    if (logger) {
        logger->log("ACOUSTIC", "Data updated: " + std::to_string(data.size()) + " points (was: " + std::to_string(oldSize) + ")");
    }
}

void AcousticAnalyzer::updatePointsByFrequency(const std::vector<MeasurementPoint>& newData) {
    // Update specific measurement points by matching frequencies
    // This preserves the full dataset size and position tracking
    if (newData.empty() || measurementData.empty()) return;
    
    int updateCount = 0;
    for (const auto& newPt : newData) {
        // Find matching point in existing data by frequency
        for (auto& existingPt : measurementData) {
            if (existingPt.freq == newPt.freq) {
                // Update all measurement values
                existingPt.s11_re = newPt.s11_re;
                existingPt.s11_im = newPt.s11_im;
                existingPt.s21_re = newPt.s21_re;
                existingPt.s21_im = newPt.s21_im;
                existingPt.hasS21 = newPt.hasS21;
                existingPt.swr = newPt.swr;
                existingPt.rl = newPt.rl;
                existingPt.R = newPt.R;
                existingPt.X = newPt.X;
                existingPt.impedance_mag = newPt.impedance_mag;
                existingPt.phase_deg = newPt.phase_deg;
                updateCount++;
                break;
            }
        }
    }
    
    if (logger) {
        logger->log("ACOUSTIC", "Updated " + std::to_string(updateCount) + " points by frequency matching (dataset size: " + std::to_string(measurementData.size()) + ")");
    }
}

void AcousticAnalyzer::setAudioEngine(std::shared_ptr<IAudioEngine> engine) {
    audioEngine = engine;
    if (audioEngine && logger) {
        logger->log("ACOUSTIC", std::string("Audio engine set to: ") + audioEngine->getName());
        
        // Configure engine-specific settings
        if (audioEngine->getEngineType() == AudioEngineType::SYNTHESIZER) {
            auto synthEngine = std::dynamic_pointer_cast<SynthesizerEngine>(audioEngine);
            if (synthEngine) {
                synthEngine->setXAxisRulerNoiseType(static_cast<int>(xAxisRulerNoiseType));
            }
        } else if (audioEngine->getEngineType() == AudioEngineType::MIDI) {
            auto midiEngine = std::dynamic_pointer_cast<MIDIEngine>(audioEngine);
            if (midiEngine) {
                midiEngine->setXAxisRulerDrum(xAxisRulerMidiDrum);
            }
        }
    }
}

void AcousticAnalyzer::play() {
    if (measurementData.empty()) return;
    
    if (state == PlaybackState::PLAYING) return;  // Already playing
    
    // If loop is enabled, ensure current position is within loop range
    // EXCEPT when loop is inverted - in that case, position can be outside loop
    if (loopEnabled.load() && !loopInverted.load()) {
        size_t pos = currentPos.load();
        size_t left = loopLeft.load();
        size_t right = loopRight.load();
        size_t dataSize = measurementData.size();
        
        // Ensure valid loop markers
        if (left >= dataSize) left = 0;
        if (right >= dataSize) right = dataSize - 1;
        if (left > right) std::swap(left, right);
        
        // If current position is outside loop range, jump to loop start
        if (pos < left || pos > right) {
            currentPos.store(left);
            if (logger) {
                logger->log("ACOUSTIC", "Position outside loop range, jumping to loop start: " + std::to_string(left));
            }
        }
    }
    
#if defined(_WIN32)
    // Open audio device for continuous playback
    if (!openAudioDevice()) {
        if (logger) logger->log("ACOUSTIC", "Failed to open audio device for playback");
        return;
    }
#endif
    
    state = PlaybackState::PLAYING;
    
    if (!audioThread.joinable()) {
        shouldStop = false;
        audioThread = std::thread(&AcousticAnalyzer::audioThreadFunc, this);
    }
    
    if (logger) logger->log("ACOUSTIC", "Play started");
}

void AcousticAnalyzer::pause() {
    state = PlaybackState::PAUSED;
    
    // Stop all active notes to prevent hanging notes
    if (audioEngine) {
        audioEngine->stopAllNotes();
    }
    
    if (logger) logger->log("ACOUSTIC", "Paused");
}

void AcousticAnalyzer::freeze() {
    state = PlaybackState::FROZEN;
    if (logger) logger->log("ACOUSTIC", "Frozen at position " + std::to_string(currentPos.load()));
}

void AcousticAnalyzer::stop() {
    state = PlaybackState::STOPPED;
    shouldStop = true;
    
    // Stop all active notes to prevent hanging notes
    if (audioEngine) {
        try {
            audioEngine->stopAllNotes();
        } catch (const std::exception& e) {
            if (logger) logger->log("ACOUSTIC", std::string("Exception in stopAllNotes(): ") + e.what());
        } catch (...) {
            if (logger) logger->log("ACOUSTIC", "Unknown exception in stopAllNotes()");
        }
    }
    
    // Safely join audio thread with exception handling
    if (audioThread.joinable()) {
        try {
            audioThread.join();
        } catch (const std::system_error& e) {
            if (logger) logger->log("ACOUSTIC", std::string("System error joining audio thread: ") + e.what());
            // Thread may be in invalid state - try to detach instead
            try {
                audioThread.detach();
            } catch (...) {
                if (logger) logger->log("ACOUSTIC", "Failed to detach audio thread");
            }
        } catch (const std::exception& e) {
            if (logger) logger->log("ACOUSTIC", std::string("Exception joining audio thread: ") + e.what());
        } catch (...) {
            if (logger) logger->log("ACOUSTIC", "Unknown exception joining audio thread");
        }
    }
    shouldStop = false;
    currentPos = 0;  // Reset position to start
    
#if defined(_WIN32)
    try {
        closeAudioDevice();
    } catch (const std::exception& e) {
        if (logger) logger->log("ACOUSTIC", std::string("Exception in closeAudioDevice(): ") + e.what());
    } catch (...) {
        if (logger) logger->log("ACOUSTIC", "Unknown exception in closeAudioDevice()");
    }
#endif
    
    if (logger) logger->log("ACOUSTIC", "Stopped and reset to start");
}

void AcousticAnalyzer::movePosition(int delta) {
    if (measurementData.empty()) return;
    
    size_t pos = currentPos.load();
    int newPos = static_cast<int>(pos) + delta;
    int dataSize = static_cast<int>(measurementData.size());
    
    // Implement rotation: wrap around when going past boundaries
    while (newPos < 0) {
        newPos += dataSize;
    }
    while (newPos >= dataSize) {
        newPos -= dataSize;
    }
    
    currentPos = static_cast<size_t>(newPos);
    
    if (logger) {
        logger->log("ACOUSTIC", "Position moved to " + std::to_string(newPos) + " (rotated)");
    }
}

int AcousticAnalyzer::movePositionWithBoundaryCheck(int delta) {
    if (measurementData.empty()) return 0;
    
    // Check if loop mode is enabled
    bool loop = loopEnabled.load();
    
    if (loop) {
        size_t pos = currentPos.load();
        int dataSize = static_cast<int>(measurementData.size());
        
        // Get loop boundaries
        size_t left = loopLeft.load();
        size_t right = loopRight.load();
        
        // Ensure valid loop markers
        if (left >= measurementData.size()) left = 0;
        if (right >= measurementData.size()) right = dataSize - 1;
        if (left > right) std::swap(left, right);
        
        int loopSize = static_cast<int>(right - left + 1);
        int adjustedDelta = delta;
        
        // Special case: if loop has only one point, no movement is possible
        if (loopSize == 1) {
            currentPos = left;
            if (logger) {
                logger->log("ACOUSTIC", "Cannot move within single-point loop at position " + std::to_string(left));
            }
            return 0;  // No movement possible
        }
        
        // Check if step size is larger than or equal to loop size and adjust if needed
        // Use loopSize - 1 to ensure we move to a different position and don't wrap back to the same spot
        // (e.g., with loopSize=10 and delta=10, we'd move 10 steps and wrap back to start)
        if (std::abs(delta) >= loopSize) {
            // Adjust delta to maximum meaningful step within loop, using only valid step values
            // Valid step values are: 1, 10, 100, 500, 1000 (settable with arrow keys)
            const int validSteps[] = {1, 10, 100, 500, 1000};
            int maxValidStep = loopSize - 1;
            
            // Find the largest valid step that fits in the loop
            int selectedStep = 1;  // Default to minimum step
            for (int step : validSteps) {
                if (step <= maxValidStep) {
                    selectedStep = step;
                } else {
                    break;  // Steps are in ascending order, no need to check further
                }
            }
            
            adjustedDelta = (delta > 0) ? selectedStep : -selectedStep;
        }
        
        // If current position is outside loop, move it inside first
        if (pos < left || pos > right) {
            pos = left;
        }
        
        // Calculate new position within loop boundaries using modulo arithmetic
        int loopPos = static_cast<int>(pos) - static_cast<int>(left);
        loopPos += adjustedDelta;
        
        // Wrap within loop boundaries efficiently using modulo
        // Handle negative values correctly: ((loopPos % loopSize) + loopSize) % loopSize
        loopPos = ((loopPos % loopSize) + loopSize) % loopSize;
        
        currentPos = static_cast<size_t>(left + loopPos);
        
        if (logger) {
            logger->log("ACOUSTIC", "Position moved to " + std::to_string(currentPos.load()) + 
                       " (wrapped within loop [" + std::to_string(left) + ", " + std::to_string(right) + "])");
        }
        
        // Return adjusted delta (will be different from delta if adjustment was needed)
        return adjustedDelta;
    } else {
        // No loop mode, use standard wrapping via movePosition
        movePosition(delta);
        return delta;  // No adjustment needed in non-loop mode
    }
}

void AcousticAnalyzer::setPosition(size_t pos) {
    if (measurementData.empty()) return;
    if (pos >= measurementData.size()) pos = measurementData.size() - 1;
    currentPos = pos;
}

void AcousticAnalyzer::setLoopLeft(size_t pos) {
    if (pos >= measurementData.size()) return;
    loopLeft = pos;
    needsPointSelection.store(true);  // Recalculate point selection
    if (logger) logger->log("ACOUSTIC", "Loop left marker set to " + std::to_string(pos));
}

void AcousticAnalyzer::setLoopRight(size_t pos) {
    if (pos >= measurementData.size()) return;
    loopRight = pos;
    needsPointSelection.store(true);  // Recalculate point selection
    if (logger) logger->log("ACOUSTIC", "Loop right marker set to " + std::to_string(pos));
}

void AcousticAnalyzer::toggleLoop() {
    loopEnabled = !loopEnabled.load();
    needsPointSelection.store(true);  // Recalculate point selection
    
    if (logger) {
        logger->log("ACOUSTIC", std::string("Loop ") + (loopEnabled.load() ? "enabled" : "disabled"));
    }
    
    // Display loop information when enabling loop
    if (loopEnabled.load() && translation && !measurementData.empty()) {
        size_t left = loopLeft.load();
        size_t right = loopRight.load();
        
        // Ensure valid loop range
        if (left <= right && right < measurementData.size()) {
            // Calculate loop statistics
            size_t numPoints = right - left + 1;
            double startFreq = measurementData[left].freq;
            double endFreq = measurementData[right].freq;
            double bandwidth = endFreq - startFreq;
            
            // Display loop information
            std::cout << "\n" << translation->get("LOOP_INFO_ACTIVATED", "Loop activated:") << "\n";
            std::cout << translation->format("LOOP_INFO_POINTS", "- {0} measurement points", numPoints) << "\n";
            std::cout << translation->format("LOOP_INFO_FREQ_RANGE", "- Frequency range: {0} Hz - {1} Hz", 
                                           static_cast<long long>(startFreq), static_cast<long long>(endFreq)) << "\n";
            std::cout << translation->format("LOOP_INFO_BANDWIDTH", "- Bandwidth: {0} Hz", 
                                           static_cast<long long>(bandwidth)) << "\n\n";
        }
    }
}

void AcousticAnalyzer::toggleContinuousReplay() {
    continuousReplay = !continuousReplay.load();
    if (logger) {
        logger->log("ACOUSTIC", std::string("Continuous replay ") + (continuousReplay ? "enabled" : "disabled"));
    }
}

void AcousticAnalyzer::toggleLoopZoom() {
    loopZoomEnabled = !loopZoomEnabled.load();
    needsPointSelection.store(true);  // Recalculate point selection
    if (logger) {
        logger->log("ACOUSTIC", std::string("Loop zoom ") + (loopZoomEnabled ? "enabled" : "disabled"));
    }
}

void AcousticAnalyzer::toggleLoopInvert() {
    loopInverted = !loopInverted.load();
    needsPointSelection.store(true);  // Recalculate point selection
    if (logger) {
        logger->log("ACOUSTIC", std::string("Loop invert ") + (loopInverted ? "enabled" : "disabled"));
    }
}

void AcousticAnalyzer::setLoopPauseMs(int pauseMs) {
    if (pauseMs >= 0 && pauseMs <= 5000) {
        loopPauseMs.store(pauseMs);
        if (logger) {
            logger->log("ACOUSTIC", "Loop pause set to " + std::to_string(pauseMs) + " ms");
        }
    }
}

void AcousticAnalyzer::setInvertedLoopGapMs(int gapMs) {
    if (gapMs >= 0 && gapMs <= 5000) {
        invertedLoopGapMs.store(gapMs);
        if (logger) {
            logger->log("ACOUSTIC", "Inverted loop gap duration set to " + std::to_string(gapMs) + " ms");
        }
    }
}

void AcousticAnalyzer::toggleCurve(int curveIndex) {
    if (curveIndex < 0 || curveIndex >= 5) return;
    
    std::lock_guard<std::mutex> lock(curveMutex);
    curves[curveIndex].enabled = !curves[curveIndex].enabled;
    needsPointSelection.store(true);  // Recalculate point selection when curves change
    
    // Track last enabled curve for ruler "follow last curve" mode
    if (curves[curveIndex].enabled) {
        lastEnabledCurve = curveIndex;
    }
    
    // If curve is being disabled, stop its note to prevent hanging
    if (!curves[curveIndex].enabled && audioEngine) {
        audioEngine->stopCurveNote(curveIndex);
    }
    
    if (logger) {
        logger->log("ACOUSTIC", curves[curveIndex].name + " " + 
                    (curves[curveIndex].enabled ? "enabled" : "disabled"));
    }
    
#if defined(_WIN32)
    // In smooth mode, flush and pause/resume for immediate response
    // Flushing audio buffers ensures curve changes are heard immediately
    if (smoothMode.load()) {
        PlaybackState currentState = state.load();
        if (currentState == PlaybackState::PLAYING) {
            flushAudioBuffers();  // Flush pending audio for immediate response
            pause();  // Pause playback
            platform_sleep_ms(CURVE_TOGGLE_PAUSE_DELAY_MS);  // Brief delay to ensure pause takes effect
            play();   // Resume playback
        }
    }
#endif
}

bool AcousticAnalyzer::isCurveEnabled(int curveIndex) const {
    if (curveIndex < 0 || curveIndex >= 5) return false;
    return curves[curveIndex].enabled;
}

void AcousticAnalyzer::setCurveVolume(int curveIndex, int volumePercent) {
    if (curveIndex < 0 || curveIndex >= 5) return;
    if (volumePercent < 0) volumePercent = 0;
    if (volumePercent > 200) volumePercent = 200;
    
    std::lock_guard<std::mutex> lock(curveMutex);
    curveVolumes[curveIndex] = volumePercent;
    
    if (logger) {
        logger->log("ACOUSTIC", curves[curveIndex].name + " volume set to " + std::to_string(volumePercent) + "%");
    }
    
#if defined(_WIN32)
    // In smooth mode, pause and resume for immediate response
    if (smoothMode.load()) {
        PlaybackState currentState = state.load();
        if (currentState == PlaybackState::PLAYING) {
            pause();  // Pause playback
            platform_sleep_ms(CURVE_TOGGLE_PAUSE_DELAY_MS);  // Brief delay to ensure pause takes effect
            play();   // Resume playback
        }
    }
#endif
}

int AcousticAnalyzer::getCurveVolume(int curveIndex) const {
    if (curveIndex < 0 || curveIndex >= 5) return 100;
    return curveVolumes[curveIndex];
}

void AcousticAnalyzer::setMasterVolume(int volumePercent) {
    if (volumePercent < 0) volumePercent = 0;
    if (volumePercent > 100) volumePercent = 100;
    
    std::lock_guard<std::mutex> lock(curveMutex);
    masterVolume = volumePercent;
    
    if (logger) {
        logger->log("ACOUSTIC", "Master volume set to " + std::to_string(volumePercent) + "%");
    }
}

int AcousticAnalyzer::getMasterVolume() const {
    return masterVolume;
}

void AcousticAnalyzer::setSmoothMode(bool smooth) {
    smoothMode = smooth;
    if (logger) {
        logger->log("ACOUSTIC", std::string("Playback mode: ") + (smooth ? "smooth" : "dotted"));
    }
}

void AcousticAnalyzer::setPlaybackTimeSeconds(int seconds) {
    if (seconds < 1) seconds = 1;
    // Removed upper limit - allow any playback time to accommodate long scans
    playbackTimeSeconds = seconds;
    needsPointSelection.store(true);  // Recalculate point selection
    if (logger) {
        logger->log("ACOUSTIC", "Playback time set to " + std::to_string(seconds) + " seconds per sweep");
    }
}

void AcousticAnalyzer::setFrequencyRange(int minHz, int maxHz) {
    // Validate and clamp values
    if (minHz < SYNTH_MIN_FREQ_HZ_LIMIT) minHz = SYNTH_MIN_FREQ_HZ_LIMIT;
    if (minHz > SYNTH_MAX_FREQ_HZ_LIMIT) minHz = SYNTH_MAX_FREQ_HZ_LIMIT;
    if (maxHz < SYNTH_MIN_FREQ_HZ_LIMIT) maxHz = SYNTH_MIN_FREQ_HZ_LIMIT;
    if (maxHz > SYNTH_MAX_FREQ_HZ_LIMIT) maxHz = SYNTH_MAX_FREQ_HZ_LIMIT;
    
    // Ensure min < max by swapping if needed
    if (minHz >= maxHz) {
        if (logger) {
            logger->log("ACOUSTIC", "Warning: Swapping frequency range values (min >= max)");
        }
        std::swap(minHz, maxHz);
    }
    
    minFreqHz = minHz;
    maxFreqHz = maxHz;
    
    if (logger) {
        logger->log("ACOUSTIC", "Frequency range set to " + std::to_string(minHz) + 
                    " - " + std::to_string(maxHz) + " Hz");
    }
}

void AcousticAnalyzer::setDottedDurationMs(int durationMs) {
    // Validate and clamp values (30-500ms range)
    if (durationMs < 30) durationMs = 30;
    if (durationMs > 500) durationMs = 500;
    
    dottedDurationMs = durationMs;
    needsPointSelection.store(true);  // Recalculate point selection
    
    if (logger) {
        logger->log("ACOUSTIC", "Dotted duration set to " + std::to_string(durationMs) + " ms");
    }
}

void AcousticAnalyzer::setDottedPauseMs(int pauseMs) {
    // Validate and clamp values (10-500ms range)
    if (pauseMs < 10) pauseMs = 10;
    if (pauseMs > 500) pauseMs = 500;
    
    dottedPauseMs = pauseMs;
    needsPointSelection.store(true);  // Recalculate point selection
    
    if (logger) {
        logger->log("ACOUSTIC", "Dotted pause duration set to " + std::to_string(pauseMs) + " ms");
    }
}

void AcousticAnalyzer::setFreezePointPauseMs(int pauseMs) {
    // Validate and clamp values (50-2000ms range)
    if (pauseMs < 50) pauseMs = 50;
    if (pauseMs > 2000) pauseMs = 2000;
    
    freezePointPauseMs = pauseMs;
    
    if (logger) {
        logger->log("ACOUSTIC", "Freeze point pause duration set to " + std::to_string(pauseMs) + " ms");
    }
}

const MeasurementPoint* AcousticAnalyzer::getCurrentMeasurement() const {
    size_t pos = currentPos.load();
    if (pos >= measurementData.size()) return nullptr;
    return &measurementData[pos];
}

void AcousticAnalyzer::audioThreadFunc() {
    const int frameDurationMs = 20;  // Reduced from 50ms to 20ms for faster response
    double smoothFractionalPos = 0.0;  // Fractional progress (0.0-1.0) between current and next point
    
    while (!shouldStop.load()) {
        PlaybackState currentState = state.load();
        
        if (currentState == PlaybackState::STOPPED) {
            platform_sleep_ms(10);
            smoothFractionalPos = 0.0;
            buffersWereFlushed.store(false);
            continue;
        }
        
        if (currentState == PlaybackState::PAUSED) {
            platform_sleep_ms(10);
            smoothFractionalPos = 0.0;
            buffersWereFlushed.store(false);
            continue;
        }
        
        if (measurementData.empty()) {
            platform_sleep_ms(10);
            smoothFractionalPos = 0.0;
            buffersWereFlushed.store(false);
            continue;
        }
        
        // Check if buffers were flushed - if so, reset immediately for instant response
        if (buffersWereFlushed.exchange(false)) {
            smoothFractionalPos = 0.0;
        }
        
        // Determine effective data size for timing calculations
        // If loop zoom is enabled and loop is active, use loop range instead of full dataset
        size_t dataSize = measurementData.size();
        bool loop = loopEnabled.load();
        bool loopZoom = loopZoomEnabled.load();
        size_t effectiveDataSize = dataSize;
        
        if (loop && loopZoom) {
            size_t left = loopLeft.load();
            size_t right = loopRight.load();
            if (left < dataSize && right < dataSize && left <= right) {
                effectiveDataSize = (right - left + 1);
            }
        }
        
        // Ensure we have at least 1 point for timing calculations
        effectiveDataSize = std::max(effectiveDataSize, size_t{1});
        
        int totalSweepTimeSeconds = playbackTimeSeconds.load();
        
        // Calculate how fast to move through points based on desired sweep time
        // totalSweepTimeSeconds = time for entire sweep (or loop if zoom enabled)
        // effectiveDataSize = number of points to play
        // We want to move through all points in totalSweepTimeSeconds
        
        bool isSmooth = smoothMode.load();
        
        // Advance position if playing (not if frozen)
        if (currentState == PlaybackState::PLAYING) {
            if (isSmooth) {
                // Smooth mode: continuous gliding transitions with point skipping when needed
                // Calculate time budget per point (same as dotted mode)
                double timePerPointMs = (totalSweepTimeSeconds * 1000.0) / static_cast<double>(effectiveDataSize);
                
                // Determine if we need to skip points to maintain timing (same logic as dotted mode)
                int skipFactor = 1;  // How many points to skip (1 = play all)
                
                if (timePerPointMs < MIN_SMOOTH_TRANSITION_TIME_MS) {
                    // Need to skip points to maintain overall timing
                    skipFactor = static_cast<int>(std::ceil(MIN_SMOOTH_TRANSITION_TIME_MS / timePerPointMs));
                }
                
                // Calculate actual time per played point
                double actualTimePerPointMs = timePerPointMs * skipFactor;
                double incrementPerFrame = frameDurationMs / actualTimePerPointMs;
                
                // Play current position with fractional interpolation, interpolating across skipFactor points
                playCurrentPositionSmooth(smoothFractionalPos, skipFactor);
                
                smoothFractionalPos += incrementPerFrame;
                
                // When we've fully transitioned to next point, advance and reset
                if (smoothFractionalPos >= 1.0) {
                    // Advance by skip factor (same as dotted mode for timing consistency)
                    for (int i = 0; i < skipFactor && currentState == PlaybackState::PLAYING; ++i) {
                        advancePosition();
                    }
                    smoothFractionalPos = 0.0;
                }
                
                // No sleep needed - audio playback buffering provides natural timing
                // The double-buffering system will block when buffers are full
            } else {
                // Dotted mode: individual dots at timed intervals with intelligent point selection
                // Use user-configurable dot duration
                int desiredDotDurationMs = dottedDurationMs.load();
                int desiredPauseDurationMs = dottedPauseMs.load();
                int desiredTotalDurationPerPoint = desiredDotDurationMs + desiredPauseDurationMs;  // Total time per point
                
                // Calculate time budget per point
                double timePerPointMs = (totalSweepTimeSeconds * 1000.0) / static_cast<double>(effectiveDataSize);
                
                // Determine how many points we can play
                int maxPointsToPlay = effectiveDataSize;  // Default: play all points
                
                if (timePerPointMs < desiredTotalDurationPerPoint) {
                    // Need to skip some points to maintain desired total duration (point + pause)
                    // Calculate how many points we can actually play
                    maxPointsToPlay = static_cast<int>((totalSweepTimeSeconds * 1000.0) / desiredTotalDurationPerPoint);
                    if (maxPointsToPlay < 2) maxPointsToPlay = 2;  // Always play at least start and end
                }
                
                // Determine effective range based on loop state
                size_t rangeStart = 0;
                size_t rangeEnd = dataSize - 1;
                bool isLoopInverted = loopInverted.load();
                
                if (loop && loopZoom && !isLoopInverted) {
                    size_t left = loopLeft.load();
                    size_t right = loopRight.load();
                    if (left < dataSize && right < dataSize && left <= right) {
                        rangeStart = left;
                        rangeEnd = right;
                    }
                }
                // Note: For inverted loops, we use the full range [0, dataSize-1]
                // and let selectPointsForDottedMode exclude the loop markers internally
                
                // Check if we need to recalculate point selection
                if (needsPointSelection.load()) {
                    // Pass loop inversion info to point selection
                    if (loop && isLoopInverted) {
                        size_t left = loopLeft.load();
                        size_t right = loopRight.load();
                        if (left < dataSize && right < dataSize && left <= right) {
                            selectPointsForDottedModeInverted(left, right, maxPointsToPlay);
                        } else {
                            selectPointsForDottedMode(rangeStart, rangeEnd, maxPointsToPlay);
                        }
                    } else {
                        selectPointsForDottedMode(rangeStart, rangeEnd, maxPointsToPlay);
                    }
                    needsPointSelection.store(false);
                }
                
                // Calculate actual time per selected point based on maxPointsToPlay
                // This ensures timing is always correct even if cache is being updated
                double actualTimePerDotMs = (totalSweepTimeSeconds * 1000.0) / static_cast<double>(maxPointsToPlay);
                
                // Find current position in the selected points cache (with thread safety)
                size_t currentPosVal = currentPos.load();
                bool hasValidPoint = false;
                size_t nextSelectedPoint = currentPosVal;
                
                {
                    std::lock_guard<std::mutex> lock(selectedPointsCacheMutex);
                    
                    // Check if cache is empty (shouldn't happen but be defensive)
                    if (selectedPointsCache.empty()) {
                        // Fall back to advancing position normally
                        advancePosition();
                        needsPointSelection.store(true);
                        platform_sleep_ms(10);
                        continue;  // Skip to next iteration
                    }
                    
                    // Find the next point to play from our selected points
                    auto it = std::lower_bound(selectedPointsCache.begin(), selectedPointsCache.end(), currentPosVal);
                    
                    if (it != selectedPointsCache.end()) {
                        nextSelectedPoint = *it;
                        hasValidPoint = true;
                    }
                }
                
                if (hasValidPoint) {
                    // Jump to the next selected point if we're not already on it
                    if (nextSelectedPoint > currentPosVal) {
                        currentPos.store(nextSelectedPoint);
                        currentPosVal = nextSelectedPoint;
                    }
                    
                    // Calculate dot and pause durations respecting timing constraints
                    // Use user-configured point and pause durations
                    int dotDurationMs = desiredDotDurationMs;
                    int desiredPauseDurationMs = dottedPauseMs.load();  // Get configured pause duration
                    int silenceDurationMs = desiredPauseDurationMs;
                    
                    // Total duration per point = point duration + pause duration
                    int desiredTotalDurationMs = dotDurationMs + silenceDurationMs;
                    
                    // Check if we have enough time budget for both point and pause
                    if (desiredTotalDurationMs > actualTimePerDotMs) {
                        // Not enough time budget - need to scale down
                        // Handle very tight time budgets
                        if (actualTimePerDotMs < MIN_DOT_DURATION_THRESHOLD_MS) {
                            // Very tight time budget - use fraction of available time
                            dotDurationMs = static_cast<int>(actualTimePerDotMs * DOTTED_SOUND_FRACTION);
                            if (dotDurationMs < ABSOLUTE_MIN_DOT_DURATION_MS) dotDurationMs = ABSOLUTE_MIN_DOT_DURATION_MS;
                            silenceDurationMs = static_cast<int>(actualTimePerDotMs) - dotDurationMs;
                            if (silenceDurationMs < ABSOLUTE_MIN_SILENCE_MS) {
                                // Ensure total doesn't exceed budget by reducing dot duration if needed
                                silenceDurationMs = ABSOLUTE_MIN_SILENCE_MS;
                                dotDurationMs = static_cast<int>(actualTimePerDotMs) - silenceDurationMs;
                                if (dotDurationMs < ABSOLUTE_MIN_DOT_DURATION_MS) dotDurationMs = ABSOLUTE_MIN_DOT_DURATION_MS;
                            }
                        } else {
                            // Scale down proportionally while maintaining ratio
                            double scale = actualTimePerDotMs / desiredTotalDurationMs;
                            dotDurationMs = static_cast<int>(desiredDotDurationMs * scale);
                            silenceDurationMs = static_cast<int>(desiredPauseDurationMs * scale);
                            
                            // Enforce minimum values
                            if (dotDurationMs < ABSOLUTE_MIN_DOT_DURATION_MS) dotDurationMs = ABSOLUTE_MIN_DOT_DURATION_MS;
                            if (silenceDurationMs < ABSOLUTE_MIN_SILENCE_MS) silenceDurationMs = ABSOLUTE_MIN_SILENCE_MS;
                            
                            // Ensure total doesn't exceed budget
                            int total = dotDurationMs + silenceDurationMs;
                            if (total > actualTimePerDotMs) {
                                // Reduce silence first
                                silenceDurationMs = static_cast<int>(actualTimePerDotMs) - dotDurationMs;
                                if (silenceDurationMs < ABSOLUTE_MIN_SILENCE_MS) {
                                    silenceDurationMs = ABSOLUTE_MIN_SILENCE_MS;
                                    dotDurationMs = static_cast<int>(actualTimePerDotMs) - silenceDurationMs;
                                    if (dotDurationMs < ABSOLUTE_MIN_DOT_DURATION_MS) dotDurationMs = ABSOLUTE_MIN_DOT_DURATION_MS;
                                }
                            }
                        }
                    } else {
                        // We have enough time budget - use configured dot duration and extend pause to fill time
                        // This ensures the playback actually takes the full configured playback time
                        dotDurationMs = desiredDotDurationMs;
                        
                        // Calculate pause to fill remaining time budget
                        // Since we're in the branch where desiredTotalDurationMs <= actualTimePerDotMs,
                        // and desiredTotalDurationMs = desiredDotDurationMs + desiredPauseDurationMs (pause > 0),
                        // we know actualTimePerDotMs > dotDurationMs, so this is always positive
                        silenceDurationMs = static_cast<int>(actualTimePerDotMs) - dotDurationMs;
                        
                        // Ensure minimum pause duration for clarity between dots
                        if (silenceDurationMs < ABSOLUTE_MIN_SILENCE_MS) {
                            silenceDurationMs = ABSOLUTE_MIN_SILENCE_MS;
                            // Reduce dot duration to maintain time budget if needed
                            int total = dotDurationMs + silenceDurationMs;
                            if (total > actualTimePerDotMs) {
                                dotDurationMs = static_cast<int>(actualTimePerDotMs) - silenceDurationMs;
                                if (dotDurationMs < ABSOLUTE_MIN_DOT_DURATION_MS) {
                                    dotDurationMs = ABSOLUTE_MIN_DOT_DURATION_MS;
                                }
                            }
                        }
                    }
                    
                    // Play dot at current position with calculated duration
                    playCurrentPosition(dotDurationMs);
                    
                    // Find next selected point after current one
                    size_t nextPoint = currentPosVal;
                    bool foundNext = false;
                    
                    {
                        std::lock_guard<std::mutex> lock(selectedPointsCacheMutex);
                        
                        if (!selectedPointsCache.empty()) {
                            auto it = std::upper_bound(selectedPointsCache.begin(), selectedPointsCache.end(), currentPosVal);
                            
                            if (it != selectedPointsCache.end()) {
                                nextPoint = *it;
                                foundNext = true;
                            }
                        }
                    }
                    
                    if (foundNext) {
                        // Check loop boundaries before setting position
                        bool loop = loopEnabled.load();
                        bool isLoopInverted = loopInverted.load();
                        
                        if (loop && isLoopInverted) {
                            size_t left = loopLeft.load();
                            size_t right = loopRight.load();
                            size_t dataSize = measurementData.size();
                            
                            // Ensure valid loop markers
                            if (left >= dataSize) left = 0;
                            if (right >= dataSize) right = dataSize - 1;
                            if (left > right) std::swap(left, right);
                            
                            // Inverted loop: play everything EXCEPT the loop range
                            // When we reach end of data, wrap to beginning
                            if (nextPoint >= dataSize) {
                                // Apply loop pause before wrapping
                                int pauseMs = loopPauseMs.load();
                                if (pauseMs > 0) {
                                    platform_sleep_ms(pauseMs);
                                }
                                
                                nextPoint = 0;
                                // Stop all notes when wrapping to prevent hanging notes
                                if (audioEngine) {
                                    audioEngine->stopAllNotes();
                                }
                                needsPointSelection.store(true);  // Recalculate after loop wrap
                            }
                            // Note: The gap between range1 and range2 is handled by the point selection
                            // which excludes points in [left, right]. We need to add the inverted loop gap
                            // when transitioning from the end of range1 to the start of range2
                            else if (currentPosVal < left && nextPoint > right) {
                                // Transitioning from range1 [0, left-1] to range2 [right+1, dataSize-1]
                                // Play the inverted loop gap with accelerated X-axis ruler clicks
                                
                                // Stop all curve notes during the gap
                                if (audioEngine) {
                                    audioEngine->stopAllNotes();
                                }
                                
                                int gapMs = invertedLoopGapMs.load();
                                size_t skippedPoints = (right - left + 1);
                                
                                if (gapMs > 0 && skippedPoints > 0 && xAxisRulerEnabled.load()) {
                                    // Play accelerated X-axis ruler clicks for the skipped points
                                    int msPerClick = gapMs / static_cast<int>(skippedPoints);
                                    if (msPerClick < MIN_INVERTED_LOOP_CLICK_INTERVAL_MS) msPerClick = MIN_INVERTED_LOOP_CLICK_INTERVAL_MS;
                                    
                                    // Calculate how many clicks we can fit in the gap duration
                                    int numClicks = gapMs / msPerClick;
                                    if (numClicks > static_cast<int>(skippedPoints)) numClicks = static_cast<int>(skippedPoints);
                                    
                                    // Generate accelerated clicks for the skipped measurement points
                                    for (int i = 0; i < numClicks && audioEngine && audioEngine->isOpen(); ++i) {
                                        size_t clickPos = left + (i * skippedPoints) / numClicks;
                                        double frac = (dataSize > 1) ? static_cast<double>(clickPos) / static_cast<double>(dataSize - 1) : 0.5;
                                        
                                        // Calculate blip duration with proper constraints
                                        // Goal: blipDuration + sleepTime = msPerClick
                                        // Constraints: blipDuration should be >= ABSOLUTE_MIN_DOT_DURATION_MS for audibility
                                        //              blipDuration should be <= msPerClick (can't exceed available time)
                                        //              sleepTime should ideally be >= INVERTED_LOOP_CLICK_GAP_MS for separation
                                        
                                        int desiredBlipDuration = xAxisRulerBlipDurationMs;
                                        int blipDuration;
                                        
                                        // Case 1: Desired duration fits with minimum gap
                                        if (desiredBlipDuration + INVERTED_LOOP_CLICK_GAP_MS <= msPerClick) {
                                            blipDuration = desiredBlipDuration;
                                        }
                                        // Case 2: Need to reduce duration to fit in available time
                                        else if (ABSOLUTE_MIN_DOT_DURATION_MS + INVERTED_LOOP_CLICK_GAP_MS <= msPerClick) {
                                            // Can fit minimum duration with gap
                                            blipDuration = std::max(ABSOLUTE_MIN_DOT_DURATION_MS, msPerClick - INVERTED_LOOP_CLICK_GAP_MS);
                                        }
                                        // Case 3: msPerClick is too small for minimum + gap, use all available time
                                        else {
                                            blipDuration = msPerClick;
                                        }
                                        
                                        int blipSamples = (SAMPLE_RATE * blipDuration) / 1000;
                                        std::vector<int16_t> blipBuffer(blipSamples * CHANNELS, 0);
                                        
                                        int blipVolume = (xAxisRulerVolume * masterVolume) / 100;
                                        audioEngine->generateXAxisRulerAudio(blipBuffer, blipSamples, frac, blipVolume);
                                        
#if defined(_WIN32)
                                        playAudioBuffer(blipBuffer);
#endif
                                        // Sleep for remaining time to maintain msPerClick timing
                                        int sleepTime = msPerClick - blipDuration;
                                        if (sleepTime > 0) {
                                            platform_sleep_ms(sleepTime);
                                        }
                                    }
                                } else if (gapMs > 0) {
                                    // Just play the silent gap without clicks
                                    platform_sleep_ms(gapMs);
                                }
                            }
                        } else if (loop) {
                            size_t left = loopLeft.load();
                            size_t right = loopRight.load();
                            size_t dataSize = measurementData.size();
                            
                            // Ensure valid loop markers
                            if (left >= dataSize) left = 0;
                            if (right >= dataSize) right = dataSize - 1;
                            if (left > right) std::swap(left, right);
                            
                            // Normal loop: if nextPoint exceeds right marker, wrap to left marker
                            if (nextPoint > right) {
                                // Apply loop pause before wrapping
                                int pauseMs = loopPauseMs.load();
                                if (pauseMs > 0) {
                                    platform_sleep_ms(pauseMs);
                                }
                                
                                nextPoint = left;
                                // Stop all notes when wrapping to prevent hanging notes
                                if (audioEngine) {
                                    audioEngine->stopAllNotes();
                                }
                                needsPointSelection.store(true);  // Recalculate after loop wrap
                            }
                        }
                        currentPos.store(nextPoint);
                    } else {
                        // Reached end of selected points, advance normally to trigger loop/boundary logic
                        advancePosition();
                        needsPointSelection.store(true);  // Recalculate on next iteration
                    }
                    
                    smoothFractionalPos = 0.0;
                    
                    // Silence between dots
                    platform_sleep_ms(silenceDurationMs);
                } else {
                    // No more selected points, advance normally
                    advancePosition();
                    needsPointSelection.store(true);  // Recalculate on next iteration
                    platform_sleep_ms(10);
                }
            }
        } else {
            // Frozen mode: stay at current point and play continuously
            if (isSmooth) {
                playCurrentPositionSmooth(0.0);  // No interpolation in frozen mode
                platform_sleep_ms(1);
            } else {
                // In dotted mode (freeze), use configured freeze point pause timing
                int dotDurationMs = dottedDurationMs.load();
                int freezePointPauseDurationMs = freezePointPauseMs.load();
                
                playCurrentPosition(dotDurationMs);
                platform_sleep_ms(freezePointPauseDurationMs);
            }
        }
    }
}

void AcousticAnalyzer::advancePosition() {
    size_t pos = currentPos.load();
    size_t dataSize = measurementData.size();
    
    if (dataSize == 0) return;
    
    bool loop = loopEnabled.load();
    bool continuous = continuousReplay.load();
    bool isLoopInverted = loopInverted.load();
    size_t left = loopLeft.load();
    size_t right = loopRight.load();
    
    // Ensure valid loop markers
    if (left >= dataSize) left = 0;
    if (right >= dataSize) right = dataSize - 1;
    if (left > right) std::swap(left, right);
    
    pos++;
    
    if (loop && isLoopInverted) {
        // Inverted loop: play everything EXCEPT the loop range
        // Skip over the loop range when we reach it
        if (pos >= left && pos <= right) {
            // Stop all curve notes to prevent hanging tones during the gap
            // This does not affect X-axis ruler (uses separate drum channel)
            if (audioEngine) {
                audioEngine->stopAllNotes();
            }
            
            // Calculate gap duration and play accelerated X-axis ruler clicks
            int gapMs = invertedLoopGapMs.load();
            size_t skippedPoints = (right - left + 1);
            
            if (gapMs > 0 && skippedPoints > 0 && xAxisRulerEnabled.load()) {
                // Play accelerated X-axis ruler clicks for the skipped points
                int msPerClick = gapMs / static_cast<int>(skippedPoints);
                if (msPerClick < MIN_INVERTED_LOOP_CLICK_INTERVAL_MS) msPerClick = MIN_INVERTED_LOOP_CLICK_INTERVAL_MS;  // Minimum for audibility
                
                // Calculate how many clicks we can fit in the gap duration
                int numClicks = gapMs / msPerClick;
                if (numClicks > skippedPoints) numClicks = static_cast<int>(skippedPoints);
                
                // Generate accelerated clicks for the skipped measurement points
                for (int i = 0; i < numClicks && audioEngine && audioEngine->isOpen(); ++i) {
                    size_t clickPos = left + (i * skippedPoints) / numClicks;
                    double frac = (dataSize > 1) ? static_cast<double>(clickPos) / static_cast<double>(dataSize - 1) : 0.5;
                    
                    // Create short blip buffer
                    int blipDuration = xAxisRulerBlipDurationMs;
                    if (blipDuration > msPerClick) blipDuration = msPerClick - INVERTED_LOOP_CLICK_GAP_MS;  // Leave small gap
                    if (blipDuration < ABSOLUTE_MIN_DOT_DURATION_MS) blipDuration = ABSOLUTE_MIN_DOT_DURATION_MS;
                    
                    int blipSamples = (SAMPLE_RATE * blipDuration) / 1000;
                    std::vector<int16_t> blipBuffer(blipSamples * CHANNELS, 0);
                    
                    int blipVolume = (xAxisRulerVolume * masterVolume) / 100;
                    audioEngine->generateXAxisRulerAudio(blipBuffer, blipSamples, frac, blipVolume);
                    
#if defined(_WIN32)
                    playAudioBuffer(blipBuffer);
#endif
                    platform_sleep_ms(msPerClick - blipDuration);
                }
            } else if (gapMs > 0) {
                // Just play the silent gap without clicks
                platform_sleep_ms(gapMs);
            }
            
            pos = right + 1;  // Jump to just after the loop range
        }
        
        // Wrap around at end of data
        if (pos >= dataSize) {
            // Apply loop pause before wrapping
            int pauseMs = loopPauseMs.load();
            if (pauseMs > 0) {
                platform_sleep_ms(pauseMs);
            }
            
            pos = 0;
            // Stop all notes when wrapping to prevent hanging notes
            if (audioEngine) {
                audioEngine->stopAllNotes();
            }
        }
    } else if (loop) {
        // Normal loop: play between markers
        if (pos > right) {
            // Apply loop pause before wrapping
            int pauseMs = loopPauseMs.load();
            if (pauseMs > 0) {
                platform_sleep_ms(pauseMs);
            }
            
            pos = left;
            // Stop all notes when wrapping to prevent hanging notes
            if (audioEngine) {
                audioEngine->stopAllNotes();
            }
        }
    } else if (continuous) {
        // Continuous replay of entire range
        if (pos >= dataSize) {
            // Apply loop pause before wrapping (also applies to continuous replay)
            int pauseMs = loopPauseMs.load();
            if (pauseMs > 0) {
                platform_sleep_ms(pauseMs);
            }
            
            pos = 0;
            // Stop all notes when wrapping to prevent hanging notes
            if (audioEngine) {
                audioEngine->stopAllNotes();
            }
        }
    } else {
        // Just pause at end (don't stop completely)
        if (pos >= dataSize) {
            pos = dataSize - 1;
            state = PlaybackState::PAUSED;
            // Stop all notes when pausing at boundary to prevent hanging notes
            if (audioEngine) {
                audioEngine->stopAllNotes();
            }
        }
    }
    
    currentPos = pos;
}

void AcousticAnalyzer::playCurrentPosition(int durationMs) {
    size_t pos = currentPos.load();
    size_t dataSize = measurementData.size();
    
    if (pos >= dataSize) return;
    if (!audioEngine || !audioEngine->isOpen()) return;
    
    const MeasurementPoint& pt = measurementData[pos];
    
    // Log audio generation context if math logger is enabled
    if (mathLogger && mathLogger->isEnabled()) {
        std::ostringstream oss;
        oss << "Position " << pos << "/" << (dataSize - 1) 
            << " | Freq: " << pt.freq << " Hz"
            << " | Duration: " << durationMs << " ms"
            << " | Engine: " << (audioEngine ? audioEngine->getName() : "none");
        mathLogger->logSeparator("ACOUSTIC AUDIO GENERATION");
        mathLogger->logDataFlow("AUDIO_GEN_START", oss.str());
    }
    
    // Calculate stereo pan based on position
    // If loop zoom is enabled and loop is active, center the loop in stereo field
    // If ruler is playing, pan curves to the right for ping-pong stereo effect
    double frac;
    bool loop = loopEnabled.load();
    bool loopZoom = loopZoomEnabled.load();
    bool isRulerActive = rulerPlaying.load();
    
    if (isRulerActive) {
        // Ruler is playing on the left, pan curves to the right for ping-pong effect
        frac = 1.0;
    } else if (loop && loopZoom) {
        size_t left = loopLeft.load();
        size_t right = loopRight.load();
        if (left < dataSize && right < dataSize && left <= right && pos >= left && pos <= right) {
            // Map position within loop to full stereo field (0.0 = left, 1.0 = right)
            // Safe subtraction: we know left <= right from check above
            size_t loopSize = right - left;
            if (loopSize > 0) {
                // Multiple points in loop - spread across stereo field
                frac = static_cast<double>(pos - left) / static_cast<double>(loopSize);
            } else {
                // Single point loop (left == right) - center in stereo field
                frac = 0.5;
            }
        } else {
            // Fallback if position is outside loop (shouldn't happen)
            frac = dataSize > 1 ? static_cast<double>(pos) / static_cast<double>(dataSize - 1) : 0.5;
        }
    } else {
        // Normal mode - map position in entire dataset to stereo field
        frac = dataSize > 1 ? static_cast<double>(pos) / static_cast<double>(dataSize - 1) : 0.5;
    }
    
    // Create audio buffer with configurable duration (default 20ms for smooth mode, user-configurable for dotted mode)
    const int samples = (SAMPLE_RATE * durationMs) / 1000;
    std::vector<int16_t> mixBuffer(samples * CHANNELS, 0);
    
    // Mix enabled curves using audio engine
    std::lock_guard<std::mutex> lock(curveMutex);
    
    // Apply master volume to curve volumes
    if (curves[0].enabled) {  // SWR
        int effectiveVolume = (curveVolumes[0] * masterVolume) / 100;
        audioEngine->generateAudio(mixBuffer, samples, 0, calcSWRPitch(pt), frac, effectiveVolume);
    }
    if (curves[1].enabled) {  // Return Loss
        int effectiveVolume = (curveVolumes[1] * masterVolume) / 100;
        audioEngine->generateAudio(mixBuffer, samples, 1, calcRLPitch(pt), frac, effectiveVolume);
    }
    if (curves[2].enabled) {  // Impedance Magnitude
        int effectiveVolume = (curveVolumes[2] * masterVolume) / 100;
        audioEngine->generateAudio(mixBuffer, samples, 2, calcZPitch(pt), frac, effectiveVolume);
    }
    if (curves[3].enabled) {  // Reactance
        int effectiveVolume = (curveVolumes[3] * masterVolume) / 100;
        audioEngine->generateAudio(mixBuffer, samples, 3, calcXPitch(pt), frac, effectiveVolume);
    }
    if (curves[4].enabled) {  // Phase
        int effectiveVolume = (curveVolumes[4] * masterVolume) / 100;
        audioEngine->generateAudio(mixBuffer, samples, 4, calcPhasePitch(pt), frac, effectiveVolume);
    }
    
    // X-Axis Ruler: Add blip sound when moving to a new measurement point
    if (xAxisRulerEnabled.load() && pos != lastXAxisBlipPosition) {
        lastXAxisBlipPosition = pos;
        
        // Generate a short noise impulse to mark the measurement point
        int blipDuration = xAxisRulerBlipDurationMs;
        int blipSamples = (SAMPLE_RATE * blipDuration) / 1000;
        if (blipSamples > samples) blipSamples = samples;  // Limit to buffer size
        
        // Create blip buffer
        std::vector<int16_t> blipBuffer(blipSamples * CHANNELS, 0);
        
        // Use noise impulse for X-axis ruler
        double blipPan = frac;  // Follow curve panning
        int blipVolume = (xAxisRulerVolume * masterVolume) / 100;
        
        // Generate noise impulse using audio engine
        // For MIDI: use percussion sound (drum)
        // For Synth: use noise waveform
        audioEngine->generateXAxisRulerAudio(blipBuffer, blipSamples, blipPan, blipVolume);
        
        // Mix blip into main buffer
        for (int i = 0; i < blipSamples * CHANNELS && i < mixBuffer.size(); i++) {
            int32_t mixed = static_cast<int32_t>(mixBuffer[i]) + static_cast<int32_t>(blipBuffer[i]);
            // Clamp to prevent overflow
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;
            mixBuffer[i] = static_cast<int16_t>(mixed);
        }
    }
    
#if defined(_WIN32)
    // Play the mixed buffer using persistent audio device
    playAudioBuffer(mixBuffer);
#endif
}

void AcousticAnalyzer::playCurrentPositionSmooth(double fractionalProgress, int skipFactor) {
    size_t pos = currentPos.load();
    size_t dataSize = measurementData.size();
    
    if (pos >= dataSize || dataSize == 0) return;
    if (!audioEngine || !audioEngine->isOpen()) return;
    
    const MeasurementPoint& pt1 = measurementData[pos];
    
    // Get next point for interpolation, accounting for skipFactor
    // When skipFactor > 1, we interpolate across multiple points to maintain smooth transitions
    // even when skipping to maintain timing
    size_t nextPos = pos + skipFactor;
    
    // Load loop state once
    bool loop = loopEnabled.load();
    size_t loopLeftVal = 0, loopRightVal = 0;
    if (loop) {
        loopLeftVal = loopLeft.load();
        loopRightVal = loopRight.load();
    }
    
    // Handle looping and wrapping
    // IMPORTANT: Do NOT interpolate between loop end and loop start - just wrap position
    if (loop) {
        // Check if current position is at or past loop end
        if (pos >= loopRightVal) {
            // At loop end - do NOT interpolate to loop start
            // Just use the current point's value and prepare to wrap
            nextPos = pos;  // Don't interpolate, stay at right edge
        } else if (nextPos > loopRightVal) {
            // Next position would exceed loop end - clamp to loop end
            // This prevents interpolation between loop end and start
            nextPos = loopRightVal;  // Clamp to loop end, no wrapping interpolation
        }
    } else if (nextPos >= dataSize) {
        if (continuousReplay.load() && dataSize > 0) {
            // Wrap around to start, maintaining skip spacing
            nextPos = nextPos % dataSize;
        } else {
            nextPos = pos;  // Stay at last point
        }
    }
    
    // Final safety check
    if (nextPos >= dataSize) nextPos = dataSize - 1;
    
    const MeasurementPoint& pt2 = measurementData[nextPos];
    
    // Calculate stereo pan positions (interpolated)
    // If loop zoom is enabled and loop is active, center the loop in stereo field
    // If ruler is playing, pan curves to the right for ping-pong effect
    double frac1, frac2, currentFrac;
    bool loopZoom = loopZoomEnabled.load();
    bool isRulerActive = rulerPlaying.load();
    
    if (isRulerActive) {
        // Ruler is playing on the left, pan curves to the right for ping-pong effect
        frac1 = frac2 = 1.0;
    } else if (loop && loopZoom) {
        if (loopLeftVal < dataSize && loopRightVal < dataSize && loopLeftVal <= loopRightVal) {
            // Map positions within loop to full stereo field
            // Safe subtraction: we know loopLeftVal <= loopRightVal from check above
            size_t loopSize = loopRightVal - loopLeftVal;
            if (loopSize > 0) {
                // Multiple points in loop - spread across stereo field
                frac1 = (pos >= loopLeftVal && pos <= loopRightVal) ? 
                        static_cast<double>(pos - loopLeftVal) / static_cast<double>(loopSize) : 0.5;
                frac2 = (nextPos >= loopLeftVal && nextPos <= loopRightVal) ? 
                        static_cast<double>(nextPos - loopLeftVal) / static_cast<double>(loopSize) : 0.5;
            } else {
                // Single point loop (loopLeftVal == loopRightVal) - center in stereo field
                frac1 = frac2 = 0.5;
            }
        } else {
            // Fallback
            frac1 = dataSize > 1 ? static_cast<double>(pos) / static_cast<double>(dataSize - 1) : 0.5;
            frac2 = dataSize > 1 ? static_cast<double>(nextPos) / static_cast<double>(dataSize - 1) : 0.5;
        }
    } else {
        // Normal mode - map position in entire dataset to stereo field
        frac1 = dataSize > 1 ? static_cast<double>(pos) / static_cast<double>(dataSize - 1) : 0.5;
        frac2 = dataSize > 1 ? static_cast<double>(nextPos) / static_cast<double>(dataSize - 1) : 0.5;
    }
    
    // Detect wrap-around condition (nextPos wrapped back to start)
    // In this case, keep panning at current position (frac1) instead of interpolating
    // to prevent audible stereo shift back to left
    bool wrappedAround = false;
    if (loop) {
        // Check if we wrapped from right marker back to left marker
        if (pos == loopRightVal && nextPos == loopLeftVal) {
            wrappedAround = true;
        }
    } else if (continuousReplay.load()) {
        // Check if we wrapped from end back to start in continuous mode
        if (pos == dataSize - 1 && nextPos == 0) {
            wrappedAround = true;
        }
    }
    
    // If wrapped around, use frac1 (no interpolation) to prevent stereo pan-back
    // Otherwise, interpolate normally for smooth panning
    if (wrappedAround) {
        currentFrac = frac1;  // Keep at current position, jump will happen on next frame
    } else {
        currentFrac = frac1 + (frac2 - frac1) * fractionalProgress;
    }
    
    // Create audio buffer (reduced from 50ms to 20ms for faster response)
    const int durationMs = 20;
    const int samples = (SAMPLE_RATE * durationMs) / 1000;
    std::vector<int16_t> mixBuffer(samples * CHANNELS, 0);
    
    // Mix enabled curves with interpolation based on fractional progress
    std::lock_guard<std::mutex> lock(curveMutex);
    
    // Interpolate pitch values for smooth transitions
    // If wrapped around, don't interpolate pitch - stay at current position to avoid false auditory impression
    // Apply master volume to curve volumes
    double pitchInterpolation = wrappedAround ? 0.0 : fractionalProgress;
    
    if (curves[0].enabled) {  // SWR
        double pitch = calcSWRPitch(pt1) + (calcSWRPitch(pt2) - calcSWRPitch(pt1)) * pitchInterpolation;
        int effectiveVolume = (curveVolumes[0] * masterVolume) / 100;
        audioEngine->generateAudio(mixBuffer, samples, 0, pitch, currentFrac, effectiveVolume);
    }
    if (curves[1].enabled) {  // Return Loss
        double pitch = calcRLPitch(pt1) + (calcRLPitch(pt2) - calcRLPitch(pt1)) * pitchInterpolation;
        int effectiveVolume = (curveVolumes[1] * masterVolume) / 100;
        audioEngine->generateAudio(mixBuffer, samples, 1, pitch, currentFrac, effectiveVolume);
    }
    if (curves[2].enabled) {  // Impedance Magnitude
        double pitch = calcZPitch(pt1) + (calcZPitch(pt2) - calcZPitch(pt1)) * pitchInterpolation;
        int effectiveVolume = (curveVolumes[2] * masterVolume) / 100;
        audioEngine->generateAudio(mixBuffer, samples, 2, pitch, currentFrac, effectiveVolume);
    }
    if (curves[3].enabled) {  // Reactance
        double pitch = calcXPitch(pt1) + (calcXPitch(pt2) - calcXPitch(pt1)) * pitchInterpolation;
        int effectiveVolume = (curveVolumes[3] * masterVolume) / 100;
        audioEngine->generateAudio(mixBuffer, samples, 3, pitch, currentFrac, effectiveVolume);
    }
    if (curves[4].enabled) {  // Phase
        double pitch = calcPhasePitch(pt1) + (calcPhasePitch(pt2) - calcPhasePitch(pt1)) * pitchInterpolation;
        int effectiveVolume = (curveVolumes[4] * masterVolume) / 100;
        audioEngine->generateAudio(mixBuffer, samples, 4, pitch, currentFrac, effectiveVolume);
    }
    
    // X-Axis Ruler: Add blip sound in smooth mode when reaching a new measurement point
    // Trigger blip at the correct time position within the buffer to synchronize with the curve signal
    if (xAxisRulerEnabled.load() && pos != lastXAxisBlipPosition) {
        lastXAxisBlipPosition = pos;
        
        // Generate a short noise impulse to mark the measurement point
        int blipDuration = xAxisRulerBlipDurationMs;
        int blipSamples = (SAMPLE_RATE * blipDuration) / 1000;
        if (blipSamples > samples) blipSamples = samples;  // Limit to buffer size
        
        // Calculate the time offset within the buffer where the curve actually reaches this position
        // The curve is interpolated from fractionalProgress, so the "current position" is reached
        // at time fractionalProgress * bufferDuration
        int offsetSamples = static_cast<int>(fractionalProgress * samples);
        if (offsetSamples + blipSamples > samples) {
            offsetSamples = samples - blipSamples;  // Ensure blip fits in buffer
        }
        if (offsetSamples < 0) offsetSamples = 0;
        
        // Create blip buffer
        std::vector<int16_t> blipBuffer(blipSamples * CHANNELS, 0);
        
        // Use noise impulse for X-axis ruler
        double blipPan = currentFrac;  // Follow curve panning
        int blipVolume = (xAxisRulerVolume * masterVolume) / 100;
        
        // Generate noise impulse using audio engine
        // For MIDI: use percussion sound (drum)
        // For Synth: use noise waveform
        audioEngine->generateXAxisRulerAudio(blipBuffer, blipSamples, blipPan, blipVolume);
        
        // Mix blip into main buffer at the calculated offset position
        int bufferStartIdx = offsetSamples * CHANNELS;
        for (int i = 0; i < blipSamples * CHANNELS && (bufferStartIdx + i) < mixBuffer.size(); i++) {
            int32_t mixed = static_cast<int32_t>(mixBuffer[bufferStartIdx + i]) + static_cast<int32_t>(blipBuffer[i]);
            // Clamp to prevent overflow
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;
            mixBuffer[bufferStartIdx + i] = static_cast<int16_t>(mixed);
        }
    }
    
#if defined(_WIN32)
    // Play the mixed buffer using persistent audio device
    playAudioBuffer(mixBuffer);
#endif
}

void AcousticAnalyzer::synthSWR(const MeasurementPoint& pt, std::vector<int16_t>& buffer, int samples, double frac) {
    // Pure sine tone, panned left-right based on position
    double swr = pt.swr;
    if (swr < 1.0) swr = 1.0;
    if (swr > 10.0) swr = 10.0;
    
    // Map SWR to pitch (400-2200 Hz)
    double pitch = 400.0 + ((swr - 1.0) / 9.0) * 1800.0;
    
    double panL = 1.0 - frac;
    double panR = frac;
    
    // Apply volume control (curve volume * master volume)
    double volumeFactor = (curveVolumes[0] / 100.0) * (masterVolume / 100.0);
    
    double phase = 0.0;
    double phaseInc = pitch / SAMPLE_RATE;
    
    for (int i = 0; i < samples; i++) {
        double sample = std::sin(2.0 * PI * phase);
        phase += phaseInc;
        if (phase >= 1.0) phase -= 1.0;
        
        int16_t left = static_cast<int16_t>(sample * panL * 8000.0 * volumeFactor);
        int16_t right = static_cast<int16_t>(sample * panR * 8000.0 * volumeFactor);
        
        buffer[i * 2 + 0] += left;
        buffer[i * 2 + 1] += right;
    }
}

void AcousticAnalyzer::synthReturnLoss(const MeasurementPoint& pt, std::vector<int16_t>& buffer, int samples, double frac) {
    // Pure sine wave - pitch based on RL value (Y-axis represented only by pitch)
    double rl = pt.rl;
    if (rl < 0.0) rl = 0.0;
    if (rl > 40.0) rl = 40.0;
    
    // Map RL to pitch: better RL (higher dB) = higher pitch
    double pitch = 400.0 + (rl / 40.0) * 1800.0;  // 400-2200 Hz range
    
    // Stereo panning based on position
    double panL = 1.0 - frac;
    double panR = frac;
    
    // Apply volume control (only for user volume setting, not for Y-axis) (curve volume * master volume)
    double volumeFactor = (curveVolumes[1] / 100.0) * (masterVolume / 100.0);
    
    double phase = 0.0;
    double phaseInc = pitch / SAMPLE_RATE;
    
    for (int i = 0; i < samples; i++) {
        double sample = std::sin(2.0 * PI * phase);
        
        phase += phaseInc;
        if (phase >= 1.0) phase -= 1.0;
        
        int16_t left = static_cast<int16_t>(sample * panL * 8000.0 * volumeFactor);
        int16_t right = static_cast<int16_t>(sample * panR * 8000.0 * volumeFactor);
        buffer[i * 2 + 0] += left;
        buffer[i * 2 + 1] += right;
    }
}

void AcousticAnalyzer::synthImpedanceMag(const MeasurementPoint& pt, std::vector<int16_t>& buffer, int samples, double frac) {
    // Triangle wave, pitch proportional to |Z|
    double zMag = pt.impedance_mag;
    if (zMag < 1.0) zMag = 1.0;
    if (zMag > 500.0) zMag = 500.0;
    
    // Map |Z| to pitch (200-1500 Hz)
    double pitch = 200.0 + ((zMag - 1.0) / 499.0) * 1300.0;
    
    // Stereo panning based on position
    double panL = 1.0 - frac;
    double panR = frac;
    
    // Apply volume control (curve volume * master volume)
    double volumeFactor = (curveVolumes[2] / 100.0) * (masterVolume / 100.0);
    
    double phase = 0.0;
    double phaseInc = pitch / SAMPLE_RATE;
    
    for (int i = 0; i < samples; i++) {
        // Triangle wave
        double t = phase - std::floor(phase);
        double sample = 2.0 * std::fabs(2.0 * (t - std::floor(t + 0.5))) - 1.0;
        
        phase += phaseInc;
        if (phase >= 1.0) phase -= 1.0;
        
        int16_t left = static_cast<int16_t>(sample * panL * 8000.0 * volumeFactor);
        int16_t right = static_cast<int16_t>(sample * panR * 8000.0 * volumeFactor);
        buffer[i * 2 + 0] += left;
        buffer[i * 2 + 1] += right;
    }
}

void AcousticAnalyzer::synthReactance(const MeasurementPoint& pt, std::vector<int16_t>& buffer, int samples, double frac) {
    // Sawtooth wave - direction depends on sign of X
    // Rising sawtooth for inductive (X > 0), falling for capacitive (X < 0)
    double x = pt.X;
    
    // Map |X| to pitch
    double absX = std::fabs(x);
    if (absX < 1.0) absX = 1.0;
    if (absX > 300.0) absX = 300.0;
    
    double pitch = 300.0 + ((absX - 1.0) / 299.0) * 800.0;  // 300-1100 Hz
    
    bool inductive = (x >= 0.0);
    
    // Stereo panning based on position
    double panL = 1.0 - frac;
    double panR = frac;
    
    // Apply volume control (curve volume * master volume)
    double volumeFactor = (curveVolumes[3] / 100.0) * (masterVolume / 100.0);
    
    double phase = 0.0;
    double phaseInc = pitch / SAMPLE_RATE;
    
    for (int i = 0; i < samples; i++) {
        double t = phase - std::floor(phase);
        double sample;
        
        if (inductive) {
            // Rising sawtooth
            sample = 2.0 * t - 1.0;
        } else {
            // Falling sawtooth
            sample = 1.0 - 2.0 * t;
        }
        
        // No amplitude modulation - Y-axis represented only by pitch
        
        phase += phaseInc;
        if (phase >= 1.0) phase -= 1.0;
        
        int16_t left = static_cast<int16_t>(sample * panL * 8000.0 * volumeFactor);
        int16_t right = static_cast<int16_t>(sample * panR * 8000.0 * volumeFactor);
        buffer[i * 2 + 0] += left;
        buffer[i * 2 + 1] += right;
    }
}

void AcousticAnalyzer::synthPhase(const MeasurementPoint& pt, std::vector<int16_t>& buffer, int samples, double frac) {
    // Sine with stereo phase offset (chorus effect) AND position-based panning
    double phaseDeg = pt.phase_deg;
    
    // Map phase to pitch (400-1200 Hz)
    double normalizedPhase = (phaseDeg + 180.0) / 360.0;  // Normalize -180..180 to 0..1
    double pitch = 400.0 + normalizedPhase * 800.0;
    
    // Stereo phase offset based on phase angle (chorus effect)
    double stereoOffset = (phaseDeg / 180.0) * 0.1;  // Slight phase difference
    
    // Position-based panning
    double panL = 1.0 - frac;
    double panR = frac;
    
    // Apply volume control (curve volume * master volume)
    double volumeFactor = (curveVolumes[4] / 100.0) * (masterVolume / 100.0);
    
    double phaseL = 0.0;
    double phaseR = stereoOffset;
    double phaseInc = pitch / SAMPLE_RATE;
    
    for (int i = 0; i < samples; i++) {
        double sampleL = std::sin(2.0 * PI * phaseL);
        double sampleR = std::sin(2.0 * PI * phaseR);
        
        phaseL += phaseInc;
        if (phaseL >= 1.0) phaseL -= 1.0;
        phaseR += phaseInc;
        if (phaseR >= 1.0) phaseR -= 1.0;
        
        buffer[i * 2 + 0] += static_cast<int16_t>(sampleL * panL * 8000.0 * volumeFactor);
        buffer[i * 2 + 1] += static_cast<int16_t>(sampleR * panR * 8000.0 * volumeFactor);
    }
}

// Interpolated synthesis functions for smooth mode
void AcousticAnalyzer::synthSWRInterpolated(const MeasurementPoint& pt1, const MeasurementPoint& pt2, 
                                           std::vector<int16_t>& buffer, int samples, double frac1, double frac2, double progress) {
    double swr1 = std::max(1.0, std::min(10.0, pt1.swr));
    double swr2 = std::max(1.0, std::min(10.0, pt2.swr));
    
    double pitch1 = 400.0 + ((swr1 - 1.0) / 9.0) * 1800.0;
    double pitch2 = 400.0 + ((swr2 - 1.0) / 9.0) * 1800.0;
    
    double volumeFactor = (curveVolumes[0] / 100.0) * (masterVolume / 100.0);
    
    for (int i = 0; i < samples; i++) {
        // Interpolate based on progress through transition + within buffer
        double t = progress + (static_cast<double>(i) / static_cast<double>(samples - 1)) / 20.0;  // Small increment within this buffer
        if (t > 1.0) t = 1.0;
        
        double pitch = pitch1 + (pitch2 - pitch1) * t;
        double frac = frac1 + (frac2 - frac1) * t;
        
        double panL = 1.0 - frac;
        double panR = frac;
        
        double sample = std::sin(2.0 * PI * swrPhase);
        swrPhase += pitch / SAMPLE_RATE;
        while (swrPhase >= 1.0) swrPhase -= 1.0;
        
        buffer[i * 2 + 0] += static_cast<int16_t>(sample * panL * 8000.0 * volumeFactor);
        buffer[i * 2 + 1] += static_cast<int16_t>(sample * panR * 8000.0 * volumeFactor);
    }
}

void AcousticAnalyzer::synthReturnLossInterpolated(const MeasurementPoint& pt1, const MeasurementPoint& pt2,
                                                   std::vector<int16_t>& buffer, int samples, double frac1, double frac2, double progress) {
    double rl1 = std::max(0.0, std::min(40.0, pt1.rl));
    double rl2 = std::max(0.0, std::min(40.0, pt2.rl));
    
    // Pure sine, pitch based on RL value (removed tremolo - pitch-only as requested)
    double pitch1 = 400.0 + (rl1 / 40.0) * 1800.0;
    double pitch2 = 400.0 + (rl2 / 40.0) * 1800.0;
    
    double volumeFactor = curveVolumes[1] / 100.0;
    
    for (int i = 0; i < samples; i++) {
        double t = progress + (static_cast<double>(i) / static_cast<double>(samples - 1)) / 20.0;
        if (t > 1.0) t = 1.0;
        
        double pitch = pitch1 + (pitch2 - pitch1) * t;
        double frac = frac1 + (frac2 - frac1) * t;
        
        double panL = 1.0 - frac;
        double panR = frac;
        
        double sample = std::sin(2.0 * PI * rlPhase);
        rlPhase += pitch / SAMPLE_RATE;
        while (rlPhase >= 1.0) rlPhase -= 1.0;
        
        buffer[i * 2 + 0] += static_cast<int16_t>(sample * panL * 8000.0 * volumeFactor);
        buffer[i * 2 + 1] += static_cast<int16_t>(sample * panR * 8000.0 * volumeFactor);
    }
}

void AcousticAnalyzer::synthImpedanceMagInterpolated(const MeasurementPoint& pt1, const MeasurementPoint& pt2,
                                                     std::vector<int16_t>& buffer, int samples, double frac1, double frac2, double progress) {
    double z1 = std::max(1.0, std::min(300.0, pt1.impedance_mag));
    double z2 = std::max(1.0, std::min(300.0, pt2.impedance_mag));
    
    double pitch1 = 200.0 + (std::log10(z1) / std::log10(300.0)) * 1300.0;
    double pitch2 = 200.0 + (std::log10(z2) / std::log10(300.0)) * 1300.0;
    
    double volumeFactor = curveVolumes[2] / 100.0;
    
    for (int i = 0; i < samples; i++) {
        double t = progress + (static_cast<double>(i) / static_cast<double>(samples - 1)) / 20.0;
        if (t > 1.0) t = 1.0;
        
        double pitch = pitch1 + (pitch2 - pitch1) * t;
        double frac = frac1 + (frac2 - frac1) * t;
        
        double panL = 1.0 - frac;
        double panR = frac;
        
        // Triangle wave
        double triangleSample = 2.0 * std::abs(2.0 * (zPhase - std::floor(zPhase + 0.5))) - 1.0;
        
        zPhase += pitch / SAMPLE_RATE;
        while (zPhase >= 1.0) zPhase -= 1.0;
        
        buffer[i * 2 + 0] += static_cast<int16_t>(triangleSample * panL * 8000.0 * volumeFactor);
        buffer[i * 2 + 1] += static_cast<int16_t>(triangleSample * panR * 8000.0 * volumeFactor);
    }
}

void AcousticAnalyzer::synthReactanceInterpolated(const MeasurementPoint& pt1, const MeasurementPoint& pt2,
                                                  std::vector<int16_t>& buffer, int samples, double frac1, double frac2, double progress) {
    double x1 = std::max(-300.0, std::min(300.0, pt1.X));
    double x2 = std::max(-300.0, std::min(300.0, pt2.X));
    
    double volumeFactor = (curveVolumes[3] / 100.0) * (masterVolume / 100.0);
    
    for (int i = 0; i < samples; i++) {
        double t = progress + (static_cast<double>(i) / static_cast<double>(samples - 1)) / 20.0;
        if (t > 1.0) t = 1.0;
        
        double x = x1 + (x2 - x1) * t;
        double frac = frac1 + (frac2 - frac1) * t;
        
        // Map reactance to pitch (pitch-only, no modulation)
        double absX = std::abs(x);
        double pitch = 300.0 + (absX / 300.0) * 1200.0;
        
        double panL = 1.0 - frac;
        double panR = frac;
        
        // Sawtooth direction based on reactance sign
        double sawtoothSample;
        if (x >= 0) {
            // Inductive: rising sawtooth
            sawtoothSample = 2.0 * (xPhase - std::floor(xPhase)) - 1.0;
        } else {
            // Capacitive: falling sawtooth
            sawtoothSample = 1.0 - 2.0 * (xPhase - std::floor(xPhase));
        }
        
        xPhase += pitch / SAMPLE_RATE;
        while (xPhase >= 1.0) xPhase -= 1.0;
        
        buffer[i * 2 + 0] += static_cast<int16_t>(sawtoothSample * panL * 8000.0 * volumeFactor);
        buffer[i * 2 + 1] += static_cast<int16_t>(sawtoothSample * panR * 8000.0 * volumeFactor);
    }
}

void AcousticAnalyzer::synthPhaseInterpolated(const MeasurementPoint& pt1, const MeasurementPoint& pt2,
                                              std::vector<int16_t>& buffer, int samples, double frac1, double frac2, double progress) {
    double phase1_deg = pt1.phase_deg;
    double phase2_deg = pt2.phase_deg;
    
    while (phase1_deg < -180.0) phase1_deg += 360.0;
    while (phase1_deg > 180.0) phase1_deg -= 360.0;
    while (phase2_deg < -180.0) phase2_deg += 360.0;
    while (phase2_deg > 180.0) phase2_deg -= 360.0;
    
    // Pure sine, pitch based on phase angle (removed chorus - pitch only as requested)
    double pitch1 = 500.0 + ((phase1_deg + 180.0) / 360.0) * 1000.0;
    double pitch2 = 500.0 + ((phase2_deg + 180.0) / 360.0) * 1000.0;
    
    double volumeFactor = (curveVolumes[4] / 100.0) * (masterVolume / 100.0);
    
    for (int i = 0; i < samples; i++) {
        double t = progress + (static_cast<double>(i) / static_cast<double>(samples - 1)) / 20.0;
        if (t > 1.0) t = 1.0;
        
        double pitch = pitch1 + (pitch2 - pitch1) * t;
        double frac = frac1 + (frac2 - frac1) * t;
        
        double panL = 1.0 - frac;
        double panR = frac;
        
        // Pure sine for both channels (same frequency, no chorus)
        double sample = std::sin(2.0 * PI * phasePhaseL);
        
        phasePhaseL += pitch / SAMPLE_RATE;
        while (phasePhaseL >= 1.0) phasePhaseL -= 1.0;
        
        buffer[i * 2 + 0] += static_cast<int16_t>(sample * panL * 8000.0 * volumeFactor);
        buffer[i * 2 + 1] += static_cast<int16_t>(sample * panR * 8000.0 * volumeFactor);
    }
}

#if defined(_WIN32)
// Windows audio device management for continuous playback
bool AcousticAnalyzer::openAudioDevice() {
    std::lock_guard<std::mutex> lock(audioMutex);
    
    if (audioDeviceOpen) return true;
    
    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = CHANNELS;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = BITS;
    wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    
    MMRESULT res = waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    
    if (res != MMSYSERR_NOERROR) {
        if (logger) logger->log("ACOUSTIC", "Failed to open audio device");
        return false;
    }
    
    audioDeviceOpen = true;
    if (logger) logger->log("ACOUSTIC", "Audio device opened");
    return true;
}

void AcousticAnalyzer::closeAudioDevice() {
    std::lock_guard<std::mutex> lock(audioMutex);
    
    if (!audioDeviceOpen) return;
    
    if (hWaveOut) {
        waveOutReset(hWaveOut);
        waveOutClose(hWaveOut);
        hWaveOut = nullptr;
    }
    
    audioDeviceOpen = false;
    if (logger) logger->log("ACOUSTIC", "Audio device closed");
}

void AcousticAnalyzer::initializeBuffers() {
    // Allocate buffer headers using RAII-safe vector
    playbackHeaders.resize(NUM_AUDIO_BUFFERS);
    
    // Initialize headers to zero
    for (int i = 0; i < NUM_AUDIO_BUFFERS; i++) {
        memset(&playbackHeaders[i], 0, sizeof(WAVEHDR));
    }
    
    // Pre-allocate buffer data storage
    bufferData.resize(NUM_AUDIO_BUFFERS);
    for (int i = 0; i < NUM_AUDIO_BUFFERS; i++) {
        bufferData[i].reserve(AUDIO_BUFFER_SIZE_BYTES / sizeof(int16_t));
    }
    
    nextBufferToQueue = 0;
    nextBufferToCheck = 0;
    buffersInFlight = 0;
    
    if (logger) logger->log("ACOUSTIC", "Initialized " + std::to_string(NUM_AUDIO_BUFFERS) + " audio buffers");
}

void AcousticAnalyzer::cleanupBuffers() {
    if (!playbackHeaders.empty()) {
        // Unprepare any prepared headers before clearing
        if (audioDeviceOpen && hWaveOut) {
            for (int i = 0; i < NUM_AUDIO_BUFFERS; i++) {
                if (playbackHeaders[i].dwFlags & WHDR_PREPARED) {
                    waveOutUnprepareHeader(hWaveOut, &playbackHeaders[i], sizeof(WAVEHDR));
                }
            }
        }
        
        playbackHeaders.clear();  // RAII-safe cleanup
    }
    
    bufferData.clear();
    nextBufferToQueue = 0;
    nextBufferToCheck = 0;
    buffersInFlight = 0;
    
    if (logger) logger->log("ACOUSTIC", "Cleaned up audio buffers");
}

void AcousticAnalyzer::flushAudioBuffers() {
    std::lock_guard<std::mutex> lock(audioMutex);
    
    if (!audioDeviceOpen || !hWaveOut) return;
    
    // Reset stops playback and marks all pending buffers as done
    waveOutReset(hWaveOut);
    
    // Unprepare all headers that were prepared
    for (int i = 0; i < NUM_AUDIO_BUFFERS; i++) {
        if (playbackHeaders[i].dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(hWaveOut, &playbackHeaders[i], sizeof(WAVEHDR));
        }
    }
    
    // Reset buffer tracking
    nextBufferToQueue = 0;
    nextBufferToCheck = 0;
    buffersInFlight = 0;
    
    // Signal audio thread to restart immediately
    buffersWereFlushed.store(true);
    
    if (logger) logger->log("ACOUSTIC", "Flushed audio buffers for immediate curve toggle response");
}

void AcousticAnalyzer::playAudioBuffer(const std::vector<int16_t>& buffer) {
    if (!audioDeviceOpen) {
        if (!openAudioDevice()) return;
    }
    
    // DOUBLE-BUFFERING IMPLEMENTATION
    // This implements a circular buffer queue for seamless audio streaming.
    // Key improvements over the old single-buffer approach:
    // - Maintains a queue of buffers ready to play
    // - Only blocks when all buffer slots are full
    // - Quickly returns to allow audio thread to generate next buffer
    // - Eliminates gaps between buffers for smooth, continuous audio
    
    std::lock_guard<std::mutex> lock(audioMutex);
    
    // First, check and free any completed buffers
    // This is non-blocking - we just check the status
    while (buffersInFlight > 0) {
        WAVEHDR& header = playbackHeaders[nextBufferToCheck];
        
        // Check if this buffer is done (non-blocking check)
        if (header.dwFlags & WHDR_DONE) {
            // Unprepare the completed buffer
            waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
            buffersInFlight--;
            nextBufferToCheck = (nextBufferToCheck + 1) % NUM_AUDIO_BUFFERS;
        } else {
            // This buffer is still playing, stop checking
            // (buffers complete in order)
            break;
        }
    }
    
    // Now wait if all buffer slots are full
    // This provides backpressure to prevent generating audio faster than it can play
    while (buffersInFlight >= NUM_AUDIO_BUFFERS) {
        WAVEHDR& header = playbackHeaders[nextBufferToCheck];
        
        // Wait for the oldest buffer to complete
        while (!(header.dwFlags & WHDR_DONE)) {
            platform_sleep_ms(PLAYBACK_POLLING_INTERVAL_MS);
        }
        
        // Unprepare the completed buffer
        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        buffersInFlight--;
        nextBufferToCheck = (nextBufferToCheck + 1) % NUM_AUDIO_BUFFERS;
    }
    
    // Queue the new buffer
    // We know we have space now (buffersInFlight < NUM_AUDIO_BUFFERS)
    WAVEHDR& header = playbackHeaders[nextBufferToQueue];
    
    // Copy data to our persistent buffer storage
    // Note: Copy is necessary (not move) because the buffer data must persist
    // until waveOut finishes playing it, which happens asynchronously
    bufferData[nextBufferToQueue] = buffer;
    
    // Setup header
    header.lpData = reinterpret_cast<LPSTR>(bufferData[nextBufferToQueue].data());
    header.dwBufferLength = static_cast<DWORD>(buffer.size() * sizeof(int16_t));
    header.dwFlags = 0;
    
    // Prepare and queue the buffer
    waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
    waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));
    
    nextBufferToQueue = (nextBufferToQueue + 1) % NUM_AUDIO_BUFFERS;
    buffersInFlight++;
}
#endif

// Pitch calculation helper functions
double AcousticAnalyzer::calcSWRPitch(const MeasurementPoint& pt) {
    // Use configurable frequency range
    // SWR: 1.0 (best) to 20.0 (worst) - linear mapping
    double swr = std::max(1.0, std::min(20.0, pt.swr));
    double normalizedValue = (swr - 1.0) / (20.0 - 1.0);  // 0.0 to 1.0
    
    int minHz = minFreqHz.load();
    int maxHz = maxFreqHz.load();
    double pitchHz = minHz + normalizedValue * (maxHz - minHz);
    
    // Log the calculation if math logger is enabled
    if (mathLogger && mathLogger->isEnabled()) {
        std::ostringstream oss;
        oss << "SWR=" << pt.swr << " (clamped=" << swr << ") "
            << "-> normalized=" << normalizedValue << " "
            << "-> pitch=" << pitchHz << " Hz "
            << "(range " << minHz << "-" << maxHz << " Hz)";
        mathLogger->logAudioOutput(currentPos.load(), "SWR", swr, pitchHz, 0.0, "pitch_calculation");
        mathLogger->logDataFlow("SWR_PITCH_CALC", oss.str());
    }
    
    return pitchHz;
}

double AcousticAnalyzer::calcRLPitch(const MeasurementPoint& pt) {
    // Use configurable frequency range
    // Return Loss: 0 dB (worst) to 40 dB (best) - linear mapping
    double rl = std::max(0.0, std::min(40.0, pt.rl));
    double normalizedValue = rl / 40.0;  // 0.0 to 1.0
    
    int minHz = minFreqHz.load();
    int maxHz = maxFreqHz.load();
    double pitchHz = minHz + normalizedValue * (maxHz - minHz);
    
    // Log the calculation if math logger is enabled
    if (mathLogger && mathLogger->isEnabled()) {
        std::ostringstream oss;
        oss << "RL=" << pt.rl << " dB (clamped=" << rl << " dB) "
            << "-> normalized=" << normalizedValue << " "
            << "-> pitch=" << pitchHz << " Hz "
            << "(range " << minHz << "-" << maxHz << " Hz)";
        mathLogger->logAudioOutput(currentPos.load(), "Return Loss", rl, pitchHz, 0.0, "pitch_calculation");
        mathLogger->logDataFlow("RL_PITCH_CALC", oss.str());
    }
    
    return pitchHz;
}

double AcousticAnalyzer::calcZPitch(const MeasurementPoint& pt) {
    // Use configurable frequency range
    // Impedance Magnitude: 1 to 500 Ohms - linear mapping
    double zMag = std::max(1.0, std::min(500.0, pt.impedance_mag));
    double normalizedValue = (zMag - 1.0) / (500.0 - 1.0);  // 0.0 to 1.0
    
    int minHz = minFreqHz.load();
    int maxHz = maxFreqHz.load();
    double pitchHz = minHz + normalizedValue * (maxHz - minHz);
    
    // Log the calculation if math logger is enabled
    if (mathLogger && mathLogger->isEnabled()) {
        std::ostringstream oss;
        oss << "|Z|=" << pt.impedance_mag << " Ω (clamped=" << zMag << " Ω) "
            << "-> normalized=" << normalizedValue << " "
            << "-> pitch=" << pitchHz << " Hz "
            << "(range " << minHz << "-" << maxHz << " Hz)";
        mathLogger->logAudioOutput(currentPos.load(), "Impedance Magnitude", zMag, pitchHz, 0.0, "pitch_calculation");
        mathLogger->logDataFlow("Z_PITCH_CALC", oss.str());
    }
    
    return pitchHz;
}

double AcousticAnalyzer::calcXPitch(const MeasurementPoint& pt) {
    // Use configurable frequency range
    // Reactance: -300 to +300 Ohms - linear mapping
    double x = std::max(-300.0, std::min(300.0, pt.X));
    double normalizedValue = (x + 300.0) / 600.0;  // 0.0 to 1.0
    
    int minHz = minFreqHz.load();
    int maxHz = maxFreqHz.load();
    double pitchHz = minHz + normalizedValue * (maxHz - minHz);
    
    // Log the calculation if math logger is enabled
    if (mathLogger && mathLogger->isEnabled()) {
        std::ostringstream oss;
        oss << "X=" << pt.X << " Ω (clamped=" << x << " Ω) "
            << "-> normalized=" << normalizedValue << " "
            << "-> pitch=" << pitchHz << " Hz "
            << "(range " << minHz << "-" << maxHz << " Hz)";
        mathLogger->logAudioOutput(currentPos.load(), "Reactance", x, pitchHz, 0.0, "pitch_calculation");
        mathLogger->logDataFlow("X_PITCH_CALC", oss.str());
    }
    
    return pitchHz;
}

double AcousticAnalyzer::calcPhasePitch(const MeasurementPoint& pt) {
    // Use configurable frequency range
    // Phase: -180° to +180° - linear mapping
    double phase = std::max(-180.0, std::min(180.0, pt.phase_deg));
    double normalizedValue = (phase + 180.0) / 360.0;  // 0.0 to 1.0
    
    int minHz = minFreqHz.load();
    int maxHz = maxFreqHz.load();
    double pitchHz = minHz + normalizedValue * (maxHz - minHz);
    
    // Log the calculation if math logger is enabled
    if (mathLogger && mathLogger->isEnabled()) {
        std::ostringstream oss;
        oss << "Phase=" << pt.phase_deg << "° (clamped=" << phase << "°) "
            << "-> normalized=" << normalizedValue << " "
            << "-> pitch=" << pitchHz << " Hz "
            << "(range " << minHz << "-" << maxHz << " Hz)";
        mathLogger->logAudioOutput(currentPos.load(), "Phase", phase, pitchHz, 0.0, "pitch_calculation");
        mathLogger->logDataFlow("PHASE_PITCH_CALC", oss.str());
    }
    
    return pitchHz;
}

std::string AcousticAnalyzer::getCurveName(int curveIndex) const {
    if (curveIndex < 0 || curveIndex >= 5) {
        return "";
    }
    
    if (translation) {
        // curves[].name stores the translation key, get the actual translated name
        return translation->get(curves[curveIndex].name, curves[curveIndex].name);
    }
    
    // Fallback if no translation manager
    return curves[curveIndex].name;
}

int AcousticAnalyzer::getCurrentSkipFactor() const {
    // Early return if no data
    size_t dataSize = measurementData.size();
    if (dataSize == 0) return 1;
    
    // Get current settings
    int totalSweepTimeSeconds = playbackTimeSeconds.load();
    bool isSmooth = smoothMode.load();
    
    // Determine effective data size (same logic as audioThreadFunc)
    size_t effectiveDataSize = dataSize;
    
    bool loop = loopEnabled.load();
    if (loop) {
        bool loopZoom = loopZoomEnabled.load();
        if (loopZoom) {
            size_t left = loopLeft.load();
            size_t right = loopRight.load();
            if (left < dataSize && right < dataSize && left <= right) {
                effectiveDataSize = right - left + 1;
            }
        }
    }
    
    // Ensure we have at least 1 point for timing calculations
    effectiveDataSize = std::max(effectiveDataSize, size_t{1});
    
    // Calculate time budget per point
    double timePerPointMs = (totalSweepTimeSeconds * 1000.0) / static_cast<double>(effectiveDataSize);
    
    // Determine skipFactor based on mode
    int skipFactor = 1;
    
    if (isSmooth) {
        // Smooth mode: use class constant for minimum transition time
        if (timePerPointMs < MIN_SMOOTH_TRANSITION_TIME_MS) {
            skipFactor = static_cast<int>(std::ceil(MIN_SMOOTH_TRANSITION_TIME_MS / timePerPointMs));
        }
    } else {
        // Dotted mode: use class constant for minimum dot duration
        if (timePerPointMs < MIN_DOTTED_DURATION_MS) {
            skipFactor = static_cast<int>(std::ceil(MIN_DOTTED_DURATION_MS / timePerPointMs));
        }
    }
    
    return skipFactor;
}

// Get skip factor using ONLY dotted mode logic (for consistent braille export)
// This ensures braille export always uses the same point selection regardless of playback mode
int AcousticAnalyzer::getDottedModeSkipFactor() const {
    // Early return if no data
    size_t dataSize = measurementData.size();
    if (dataSize == 0) return 1;
    
    // Get current settings (but ignore smooth mode - always use dotted logic)
    int totalSweepTimeSeconds = playbackTimeSeconds.load();
    
    // Determine effective data size (same logic as audioThreadFunc)
    size_t effectiveDataSize = dataSize;
    
    bool loop = loopEnabled.load();
    if (loop) {
        bool loopZoom = loopZoomEnabled.load();
        if (loopZoom) {
            size_t left = loopLeft.load();
            size_t right = loopRight.load();
            if (left < dataSize && right < dataSize && left <= right) {
                effectiveDataSize = right - left + 1;
            }
        }
    }
    
    // Ensure we have at least 1 point for timing calculations
    effectiveDataSize = std::max(effectiveDataSize, size_t{1});
    
    // Calculate time budget per point
    double timePerPointMs = (totalSweepTimeSeconds * 1000.0) / static_cast<double>(effectiveDataSize);
    
    // Always use dotted mode logic for braille export consistency
    int skipFactor = 1;
    if (timePerPointMs < MIN_DOTTED_DURATION_MS) {
        skipFactor = static_cast<int>(std::ceil(MIN_DOTTED_DURATION_MS / timePerPointMs));
    }
    
    return skipFactor;
}

// Helper to get the value of a specific curve at a given point
static double getCurveValue(const MeasurementPoint& pt, int curveIndex) {
    // Map curve index to measurement value
    // 0: SWR, 1: Return Loss, 2: Impedance Magnitude, 3: Reactance, 4: Phase
    switch (curveIndex) {
        case 0: return pt.swr;
        case 1: return pt.rl;
        case 2: return pt.impedance_mag;
        case 3: return pt.X;
        case 4: return pt.phase_deg;
        default: return 0.0;
    }
}

// Check if a point is a local extremum (minimum or maximum) for any enabled curve
bool AcousticAnalyzer::isLocalExtremum(size_t idx, int curveIndex) const {
    if (idx == 0 || idx >= measurementData.size() - 1) {
        return true;  // Boundary points are always important
    }
    
    double prev = getCurveValue(measurementData[idx - 1], curveIndex);
    double curr = getCurveValue(measurementData[idx], curveIndex);
    double next = getCurveValue(measurementData[idx + 1], curveIndex);
    
    // Check if this is a local minimum or maximum
    bool isMax = (curr >= prev && curr >= next);
    bool isMin = (curr <= prev && curr <= next);
    
    return isMax || isMin;
}

// Check if a point has a sharp direction change (inflection point) for any enabled curve
bool AcousticAnalyzer::hasSharpDirectionChange(size_t idx, int curveIndex) const {
    if (idx < 2 || idx >= measurementData.size() - 2) {
        return false;  // Need at least 2 points on each side
    }
    
    double prev2 = getCurveValue(measurementData[idx - 2], curveIndex);
    double prev1 = getCurveValue(measurementData[idx - 1], curveIndex);
    double curr = getCurveValue(measurementData[idx], curveIndex);
    double next1 = getCurveValue(measurementData[idx + 1], curveIndex);
    double next2 = getCurveValue(measurementData[idx + 2], curveIndex);
    
    // Calculate slopes before and after
    double slopeBefore = (curr - prev2) / 2.0;
    double slopeAfter = (next2 - curr) / 2.0;
    
    // Check if direction changes (slope sign change)
    if ((slopeBefore > 0 && slopeAfter < 0) || (slopeBefore < 0 && slopeAfter > 0)) {
        // Calculate change magnitude
        double changeRatio = std::abs(slopeAfter - slopeBefore) / (std::abs(slopeBefore) + std::abs(slopeAfter) + EPSILON_SLOPE);
        return changeRatio > DIRECTION_CHANGE_THRESHOLD;
    }
    
    return false;
}

// Calculate importance score for a point (higher = more important)
double AcousticAnalyzer::getPointImportance(size_t idx, int curveIndex) const {
    if (idx >= measurementData.size()) {
        return 0.0;
    }
    
    double importance = 0.0;
    
    // Boundary points are always important
    if (idx == 0 || idx == measurementData.size() - 1) {
        importance += IMPORTANCE_BOUNDARY;
    }
    
    // Check if it's a local extremum
    if (isLocalExtremum(idx, curveIndex)) {
        importance += IMPORTANCE_EXTREMUM;
        
        // Add extra importance for significant extrema
        if (idx > 0 && idx < measurementData.size() - 1) {
            double prev = getCurveValue(measurementData[idx - 1], curveIndex);
            double curr = getCurveValue(measurementData[idx], curveIndex);
            double next = getCurveValue(measurementData[idx + 1], curveIndex);
            
            double deviation = std::abs(2.0 * curr - prev - next);
            importance += deviation * IMPORTANCE_EXTREMUM_WEIGHT;
        }
    }
    
    // Check for sharp direction changes
    if (hasSharpDirectionChange(idx, curveIndex)) {
        importance += IMPORTANCE_DIRECTION_CHANGE;
    }
    
    // Add small base importance to all points for tie-breaking
    importance += IMPORTANCE_BASE;
    
    return importance;
}

// Select which points to play in dotted mode based on curve features
// Uses LTTB algorithm enhanced with importance-based refinement
void AcousticAnalyzer::selectPointsForDottedMode(size_t startIdx, size_t endIdx, int maxPoints) {
    // Create temporary working vector
    std::vector<size_t> tempCache;
    
    if (measurementData.empty() || startIdx >= measurementData.size()) {
        std::lock_guard<std::mutex> lock(selectedPointsCacheMutex);
        selectedPointsCache.clear();
        return;
    }
    
    // Clamp end index
    if (endIdx >= measurementData.size()) {
        endIdx = measurementData.size() - 1;
    }
    
    size_t rangeSize = endIdx - startIdx + 1;
    
    // If we can play all points, just create a sequential list
    if (rangeSize <= static_cast<size_t>(maxPoints)) {
        for (size_t i = startIdx; i <= endIdx; i++) {
            tempCache.push_back(i);
        }
        
        // Atomically update the cache
        std::lock_guard<std::mutex> lock(selectedPointsCacheMutex);
        selectedPointsCache = std::move(tempCache);
        return;
    }
    
    // Use LTTB algorithm as the primary downsampling method
    // LTTB preserves the visual shape of curves better than simple decimation
    tempCache = selectPointsUsingLTTB(startIdx, endIdx, maxPoints);
    
    // If LTTB returned fewer points than expected (edge case), fall back to importance-based selection
    if (tempCache.size() < static_cast<size_t>(maxPoints) * LTTB_FALLBACK_THRESHOLD) {
        // Create importance scores for all points in range, considering all enabled curves
        std::vector<std::pair<double, size_t>> pointScores;  // (importance, index)
        
        {
            std::lock_guard<std::mutex> lock(curveMutex);
            
            for (size_t i = startIdx; i <= endIdx; i++) {
                double maxImportance = 0.0;
                
                // Get maximum importance across all enabled curves
                for (int c = 0; c < NUM_CURVES; c++) {
                    if (curves[c].enabled) {
                        double importance = getPointImportance(i, c);
                        maxImportance = std::max(maxImportance, importance);
                    }
                }
                
                pointScores.push_back({maxImportance, i});
            }
        }
        
        // Sort by importance (descending)
        std::sort(pointScores.begin(), pointScores.end(), 
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        
        // Select the top maxPoints points
        tempCache.clear();
        for (int i = 0; i < maxPoints && i < static_cast<int>(pointScores.size()); i++) {
            tempCache.push_back(pointScores[i].second);
        }
        
        // Sort selected points by index to maintain temporal order
        std::sort(tempCache.begin(), tempCache.end());
        
        // Always ensure first and last points are included if they're in range
        // Since we sorted by importance first, startIdx and endIdx should already be there
        // But add them explicitly if missing (they had IMPORTANCE_BOUNDARY)
        bool hasStart = !tempCache.empty() && tempCache.front() == startIdx;
        bool hasEnd = !tempCache.empty() && tempCache.back() == endIdx;
        
        if (!hasStart) {
            tempCache.insert(tempCache.begin(), startIdx);
            // If we exceeded maxPoints, remove the last element (which should be least important after sorting)
            if (tempCache.size() > static_cast<size_t>(maxPoints)) {
                tempCache.pop_back();
            }
        }
        
        if (!hasEnd && (tempCache.empty() || tempCache.back() != endIdx)) {
            tempCache.push_back(endIdx);
            // If we exceeded maxPoints, we need to remove one point
            if (tempCache.size() > static_cast<size_t>(maxPoints) && tempCache.size() > 2) {
                // Remove second-to-last (the point just before the end we just added)
                // This is safer than removing from middle
                tempCache.erase(tempCache.end() - 2);
            }
        }
    }
    
    // Check downsampling quality and warn user if necessary
    double timePerPointMs = 0.0;
    int totalSweepTimeSeconds = playbackTimeSeconds.load();
    if (maxPoints > 0) {
        timePerPointMs = (totalSweepTimeSeconds * 1000.0) / static_cast<double>(maxPoints);
    }
    checkAndWarnDownsamplingQuality(rangeSize, tempCache.size(), timePerPointMs);
    
    // Atomically update the cache
    std::lock_guard<std::mutex> lock(selectedPointsCacheMutex);
    selectedPointsCache = std::move(tempCache);
}

// Select points for dotted mode with inverted loop (play outside loop markers)
void AcousticAnalyzer::selectPointsForDottedModeInverted(size_t loopLeft, size_t loopRight, int maxPoints) {
    // Create temporary working vector
    std::vector<size_t> tempCache;
    
    if (measurementData.empty() || loopLeft >= measurementData.size()) {
        std::lock_guard<std::mutex> lock(selectedPointsCacheMutex);
        selectedPointsCache.clear();
        return;
    }
    
    size_t dataSize = measurementData.size();
    
    // Clamp loop markers
    if (loopRight >= dataSize) {
        loopRight = dataSize - 1;
    }
    if (loopLeft > loopRight) {
        std::swap(loopLeft, loopRight);
    }
    
    // Calculate sizes of the two ranges (before and after loop)
    // Range 1: [0, loopLeft-1] - points from index 0 to loopLeft-1 (count: loopLeft)
    // Range 2: [loopRight+1, dataSize-1] - points from loopRight+1 to dataSize-1
    size_t range1Size = loopLeft;
    size_t range2Size = (loopRight < dataSize - 1) ? (dataSize - loopRight - 1) : 0;
    size_t totalInvertedSize = range1Size + range2Size;
    
    // If inverted range is empty (loop covers entire data), return empty cache
    if (totalInvertedSize == 0) {
        std::lock_guard<std::mutex> lock(selectedPointsCacheMutex);
        selectedPointsCache.clear();
        return;
    }
    
    // If we can play all points in inverted range, just create a sequential list
    if (totalInvertedSize <= static_cast<size_t>(maxPoints)) {
        // Add all points from range 1 [0, loopLeft-1]
        for (size_t i = 0; i < loopLeft; i++) {
            tempCache.push_back(i);
        }
        // Add all points from range 2 [loopRight+1, dataSize-1]
        for (size_t i = loopRight + 1; i < dataSize; i++) {
            tempCache.push_back(i);
        }
        
        // Atomically update the cache
        std::lock_guard<std::mutex> lock(selectedPointsCacheMutex);
        selectedPointsCache = std::move(tempCache);
        return;
    }
    
    // Need to downsample - distribute maxPoints between the two ranges proportionally
    int pointsForRange1 = 0;
    int pointsForRange2 = 0;
    
    if (range1Size > 0 && range2Size > 0) {
        // Both ranges have points - distribute proportionally with minimum of 1 point each
        double range1Fraction = static_cast<double>(range1Size) / static_cast<double>(totalInvertedSize);
        
        // Calculate proportional distribution
        pointsForRange1 = static_cast<int>(std::round(maxPoints * range1Fraction));
        pointsForRange2 = maxPoints - pointsForRange1;
        
        // Ensure both ranges get at least 1 point if they have any points
        if (pointsForRange1 < 1) {
            pointsForRange1 = 1;
            pointsForRange2 = maxPoints - 1;
        }
        if (pointsForRange2 < 1) {
            pointsForRange2 = 1;
            pointsForRange1 = maxPoints - 1;
        }
        
        // Edge case: if maxPoints < 2, give one point to the larger range
        if (maxPoints < 2) {
            if (range1Size >= range2Size) {
                pointsForRange1 = maxPoints;
                pointsForRange2 = 0;
            } else {
                pointsForRange1 = 0;
                pointsForRange2 = maxPoints;
            }
        }
    } else if (range1Size > 0) {
        // Only range 1 has points
        pointsForRange1 = maxPoints;
        pointsForRange2 = 0;
    } else {
        // Only range 2 has points
        pointsForRange1 = 0;
        pointsForRange2 = maxPoints;
    }
    
    // Select points from range 1 using existing logic
    std::vector<size_t> range1Points;
    if (pointsForRange1 > 0 && range1Size > 0) {
        // Use LTTB algorithm for range 1
        range1Points = selectPointsUsingLTTB(0, loopLeft - 1, pointsForRange1);
        
        // If LTTB didn't return enough points, fall back to importance-based selection
        if (range1Points.size() < static_cast<size_t>(pointsForRange1) * LTTB_FALLBACK_THRESHOLD) {
            range1Points.clear();
            
            // Create importance scores for range 1
            std::vector<std::pair<double, size_t>> pointScores;
            
            {
                std::lock_guard<std::mutex> lock(curveMutex);
                
                for (size_t i = 0; i < loopLeft; i++) {
                    double maxImportance = 0.0;
                    
                    for (int c = 0; c < NUM_CURVES; c++) {
                        if (curves[c].enabled) {
                            double importance = getPointImportance(i, c);
                            maxImportance = std::max(maxImportance, importance);
                        }
                    }
                    
                    pointScores.push_back({maxImportance, i});
                }
            }
            
            // Sort by importance (descending)
            std::sort(pointScores.begin(), pointScores.end(), 
                      [](const auto& a, const auto& b) { return a.first > b.first; });
            
            // Select the top pointsForRange1 points
            for (int i = 0; i < pointsForRange1 && i < static_cast<int>(pointScores.size()); i++) {
                range1Points.push_back(pointScores[i].second);
            }
            
            // Sort by index to maintain temporal order
            std::sort(range1Points.begin(), range1Points.end());
        }
    }
    
    // Select points from range 2 using existing logic
    std::vector<size_t> range2Points;
    if (pointsForRange2 > 0 && range2Size > 0) {
        // Use LTTB algorithm for range 2
        range2Points = selectPointsUsingLTTB(loopRight + 1, dataSize - 1, pointsForRange2);
        
        // If LTTB didn't return enough points, fall back to importance-based selection
        if (range2Points.size() < static_cast<size_t>(pointsForRange2) * LTTB_FALLBACK_THRESHOLD) {
            range2Points.clear();
            
            // Create importance scores for range 2
            std::vector<std::pair<double, size_t>> pointScores;
            
            {
                std::lock_guard<std::mutex> lock(curveMutex);
                
                for (size_t i = loopRight + 1; i < dataSize; i++) {
                    double maxImportance = 0.0;
                    
                    for (int c = 0; c < NUM_CURVES; c++) {
                        if (curves[c].enabled) {
                            double importance = getPointImportance(i, c);
                            maxImportance = std::max(maxImportance, importance);
                        }
                    }
                    
                    pointScores.push_back({maxImportance, i});
                }
            }
            
            // Sort by importance (descending)
            std::sort(pointScores.begin(), pointScores.end(), 
                      [](const auto& a, const auto& b) { return a.first > b.first; });
            
            // Select the top pointsForRange2 points
            for (int i = 0; i < pointsForRange2 && i < static_cast<int>(pointScores.size()); i++) {
                range2Points.push_back(pointScores[i].second);
            }
            
            // Sort by index to maintain temporal order
            std::sort(range2Points.begin(), range2Points.end());
        }
    }
    
    // Combine both ranges into tempCache (already sorted by index)
    tempCache.insert(tempCache.end(), range1Points.begin(), range1Points.end());
    tempCache.insert(tempCache.end(), range2Points.begin(), range2Points.end());
    
    // Check downsampling quality
    double timePerPointMs = 0.0;
    int totalSweepTimeSeconds = playbackTimeSeconds.load();
    if (maxPoints > 0) {
        timePerPointMs = (totalSweepTimeSeconds * 1000.0) / static_cast<double>(maxPoints);
    }
    checkAndWarnDownsamplingQuality(totalInvertedSize, tempCache.size(), timePerPointMs);
    
    // Atomically update the cache
    std::lock_guard<std::mutex> lock(selectedPointsCacheMutex);
    selectedPointsCache = std::move(tempCache);
}

// Largest Triangle Three Buckets (LTTB) algorithm for downsampling
// This algorithm preserves the visual shape of curves by selecting points that form the largest triangles
std::vector<size_t> AcousticAnalyzer::selectPointsUsingLTTB(size_t startIdx, size_t endIdx, int threshold) {
    std::vector<size_t> selectedIndices;
    
    // Validate inputs - need at least 3 points for LTTB to work (first, middle, last)
    if (measurementData.empty() || startIdx >= measurementData.size() || threshold < 3) {
        return selectedIndices;
    }
    
    // Clamp end index
    if (endIdx >= measurementData.size()) {
        endIdx = measurementData.size() - 1;
    }
    
    size_t rangeSize = endIdx - startIdx + 1;
    
    // If we can keep all points, just return sequential list
    if (rangeSize <= static_cast<size_t>(threshold)) {
        for (size_t i = startIdx; i <= endIdx; i++) {
            selectedIndices.push_back(i);
        }
        return selectedIndices;
    }
    
    // Always include first point
    selectedIndices.push_back(startIdx);
    
    // Calculate bucket size (excluding first and last points)
    // threshold - 2 is guaranteed to be >= 1 because we validated threshold >= 3
    double bucketSize = static_cast<double>(rangeSize - 2) / static_cast<double>(threshold - 2);
    
    // Lock mutex once for the entire LTTB calculation
    std::lock_guard<std::mutex> lock(curveMutex);
    
    // For each bucket (except first and last which are already handled)
    size_t selectedSoFar = 1; // We've selected the first point
    
    for (int bucketIdx = 0; bucketIdx < threshold - 2; bucketIdx++) {
        // Calculate range for current bucket
        size_t bucketStart = startIdx + 1 + static_cast<size_t>(bucketIdx * bucketSize);
        size_t bucketEnd = startIdx + 1 + static_cast<size_t>((bucketIdx + 1) * bucketSize);
        if (bucketEnd > endIdx - 1) bucketEnd = endIdx - 1;
        
        // Ensure bucketStart <= bucketEnd to avoid issues
        if (bucketStart > bucketEnd) {
            bucketStart = bucketEnd;
        }
        
        // Get the average point in the next bucket (for triangle calculation)
        size_t nextBucketStart = startIdx + 1 + static_cast<size_t>((bucketIdx + 1) * bucketSize);
        size_t nextBucketEnd = startIdx + 1 + static_cast<size_t>((bucketIdx + 2) * bucketSize);
        // Clamp to endIdx to prevent out-of-bounds (handles last bucket case)
        if (nextBucketEnd > endIdx) nextBucketEnd = endIdx;
        
        // Calculate average of next bucket across all enabled curves
        // Note: This is used for triangle area calculation, not point selection
        double avgNextX = 0.0;
        double avgNextY = 0.0;
        int pointCount = 0;
        
        // Loop through next bucket to calculate average
        // Uses [nextBucketStart, nextBucketEnd) range (half-open interval, standard for ranges)
        for (size_t i = nextBucketStart; i < nextBucketEnd && i < measurementData.size(); i++) {
            avgNextX += static_cast<double>(i);
            
            // Average across all enabled curves
            double valueSum = 0.0;
            int enabledCount = 0;
            for (int c = 0; c < NUM_CURVES; c++) {
                if (curves[c].enabled) {
                    valueSum += getCurveValue(measurementData[i], c);
                    enabledCount++;
                }
            }
            if (enabledCount > 0) {
                avgNextY += valueSum / enabledCount;
            }
            pointCount++;
        }
        
        if (pointCount > 0) {
            avgNextX /= pointCount;
            avgNextY /= pointCount;
        }
        
        // Find point in current bucket that forms largest triangle
        size_t bestPointIdx = bucketStart;
        double maxArea = -1.0;
        
        // Previous selected point
        size_t prevIdx = selectedIndices[selectedSoFar - 1];
        double prevX = static_cast<double>(prevIdx);
        double prevY = 0.0;
        
        // Get average value of previous point across enabled curves
        int enabledCount = 0;
        for (int c = 0; c < NUM_CURVES; c++) {
            if (curves[c].enabled) {
                prevY += getCurveValue(measurementData[prevIdx], c);
                enabledCount++;
            }
        }
        if (enabledCount > 0) {
            prevY /= enabledCount;
        }
        
        // Check each point in current bucket to find the one forming the largest triangle
        // Uses [bucketStart, bucketEnd] range (closed interval, to check all candidates)
        for (size_t i = bucketStart; i <= bucketEnd && i < measurementData.size(); i++) {
            double currX = static_cast<double>(i);
            double currY = 0.0;
            
            // Get average value across enabled curves
            enabledCount = 0;
            for (int c = 0; c < NUM_CURVES; c++) {
                if (curves[c].enabled) {
                    currY += getCurveValue(measurementData[i], c);
                    enabledCount++;
                }
            }
            if (enabledCount > 0) {
                currY /= enabledCount;
            }
            
            // Calculate triangle area using the formula:
            // Area = 0.5 * |x1(y2 - y3) + x2(y3 - y1) + x3(y1 - y2)|
            double area = std::abs(
                prevX * (currY - avgNextY) +
                currX * (avgNextY - prevY) +
                avgNextX * (prevY - currY)
            ) * 0.5;
            
            if (area > maxArea) {
                maxArea = area;
                bestPointIdx = i;
            }
        }
        
        selectedIndices.push_back(bestPointIdx);
        selectedSoFar++;
    }
    
    // Always include last point
    selectedIndices.push_back(endIdx);
    
    // Sort to maintain temporal order
    std::sort(selectedIndices.begin(), selectedIndices.end());
    
    return selectedIndices;
}

// Check downsampling quality and warn user if necessary
void AcousticAnalyzer::checkAndWarnDownsamplingQuality(size_t originalCount, size_t selectedCount, double timePerPointMs) const {
    if (!logger) return;
    
    // Calculate downsampling ratio
    double ratio = static_cast<double>(selectedCount) / static_cast<double>(originalCount);
    
    // Check if we're dropping too many points
    if (selectedCount < MIN_POINTS_FOR_ACCURATE_REPRESENTATION) {
        logger->log("ACOUSTIC", "WARNING: Time window too small - only " + std::to_string(selectedCount) + 
                   " points will be played. Curve characteristics may not be accurately preserved.");
        logger->log("ACOUSTIC", "  Recommendation: Increase playback time (currently " + 
                   std::to_string(static_cast<int>(timePerPointMs * selectedCount / 1000.0)) + 
                   " seconds) to allow for more detail.");
    } else if (ratio < WARNING_DOWNSAMPLING_RATIO) {
        logger->log("ACOUSTIC", "INFO: Aggressive downsampling - keeping " + 
                   std::to_string(static_cast<int>(ratio * 100)) + "% of points (" + 
                   std::to_string(selectedCount) + " of " + std::to_string(originalCount) + 
                   "). Some fine details may be simplified.");
        logger->log("ACOUSTIC", "  Tip: Increase playback time for better detail, or use loop zoom for focused analysis.");
    }
}

void AcousticAnalyzer::playYAxisRuler() {
    // Toggle behavior: if already playing, stop it; otherwise start it
    if (rulerPlaying.load()) {
        stopYAxisRuler();
        return;
    }
    
    if (!audioEngine || !audioEngine->isOpen()) {
        if (logger) {
            logger->log("ACOUSTIC", "Y-Axis Ruler: Audio engine not available");
        }
        return;
    }
    
#if !defined(_WIN32)
    if (logger) {
        logger->log("ACOUSTIC", "Y-Axis Ruler: Feature only available on Windows platform");
    }
    return;
#endif
    
    // Save current playback state
    stateBeforeRuler = state.load();
    
    // Temporarily freeze or pause playback during ruler
    // In MIDI gliding mode, we need to stop curve notes to prevent overlap with ruler
    // The ruler uses a dedicated MIDI channel (channel 6) but we want ping-pong effect
    if (stateBeforeRuler == PlaybackState::PLAYING) {
        // Stop all curve notes to ensure clean ruler playback
        if (audioEngine) {
            audioEngine->stopAllNotes();
        }
        freeze();
    }
    
    // Set ruler playing flag and reset stop flag
    rulerPlaying.store(true);
    rulerShouldStop.store(false);
    
    // Start ruler in a separate thread
    // Join any existing ruler thread before creating new one - with exception handling
    if (rulerThread.joinable()) {
        try {
            rulerThread.join();
        } catch (const std::system_error& e) {
            if (logger) logger->log("ACOUSTIC", std::string("System error joining old ruler thread: ") + e.what());
            // Try to detach if join fails
            try {
                rulerThread.detach();
            } catch (...) {
                if (logger) logger->log("ACOUSTIC", "Failed to detach old ruler thread");
            }
        } catch (const std::exception& e) {
            if (logger) logger->log("ACOUSTIC", std::string("Exception joining old ruler thread: ") + e.what());
        } catch (...) {
            if (logger) logger->log("ACOUSTIC", "Unknown exception joining old ruler thread");
        }
    }
    rulerThread = std::thread(&AcousticAnalyzer::rulerThreadFunc, this);
    
    if (logger) {
        logger->log("ACOUSTIC", "Y-axis ruler started");
    }
}

void AcousticAnalyzer::stopYAxisRuler() {
    if (!rulerPlaying.load()) {
        return;
    }
    
    // Signal ruler thread to stop
    rulerShouldStop.store(true);
    
    // Wait for ruler thread to finish - with exception handling
    if (rulerThread.joinable()) {
        try {
            rulerThread.join();
        } catch (const std::system_error& e) {
            if (logger) logger->log("ACOUSTIC", std::string("System error joining ruler thread: ") + e.what());
            // Thread may be in invalid state - try to detach instead
            try {
                rulerThread.detach();
            } catch (...) {
                if (logger) logger->log("ACOUSTIC", "Failed to detach ruler thread");
            }
        } catch (const std::exception& e) {
            if (logger) logger->log("ACOUSTIC", std::string("Exception joining ruler thread: ") + e.what());
        } catch (...) {
            if (logger) logger->log("ACOUSTIC", "Unknown exception joining ruler thread");
        }
    }
    
    rulerPlaying.store(false);
    
    if (logger) {
        logger->log("ACOUSTIC", "Y-axis ruler stopped");
    }
}

void AcousticAnalyzer::setRulerVolume(int volumePercent) {
    if (volumePercent < 0) volumePercent = 0;
    if (volumePercent > 100) volumePercent = 100;
    rulerVolume = volumePercent;
}

int AcousticAnalyzer::getRulerVolume() const {
    return rulerVolume;
}

void AcousticAnalyzer::setRulerBlipDuration(int durationMs) {
    if (durationMs < 30) durationMs = 30;
    if (durationMs > 500) durationMs = 500;
    rulerBlipDurationMs = durationMs;
}

int AcousticAnalyzer::getRulerBlipDuration() const {
    return rulerBlipDurationMs;
}

void AcousticAnalyzer::setRulerLengtheningFactor(int percentFactor) {
    if (percentFactor < 100) percentFactor = 100;
    if (percentFactor > 500) percentFactor = 500;
    rulerLengtheningFactorPercent = percentFactor;
}

int AcousticAnalyzer::getRulerLengtheningFactor() const {
    return rulerLengtheningFactorPercent;
}

// X-Axis Ruler implementation
void AcousticAnalyzer::toggleXAxisRuler() {
    bool wasEnabled = xAxisRulerEnabled.load();
    xAxisRulerEnabled.store(!wasEnabled);
    
    if (!wasEnabled) {
        // Enabling: reset to force blip on first point
        lastXAxisBlipPosition = SIZE_MAX;  // Use SIZE_MAX as sentinel
    }
    
    if (logger) {
        logger->log("ACOUSTIC", xAxisRulerEnabled.load() ? 
            "X-axis ruler enabled" : "X-axis ruler disabled");
    }
}

void AcousticAnalyzer::setXAxisRulerVolume(int volumePercent) {
    if (volumePercent < 0) volumePercent = 0;
    if (volumePercent > 100) volumePercent = 100;
    xAxisRulerVolume = volumePercent;
}

int AcousticAnalyzer::getXAxisRulerVolume() const {
    return xAxisRulerVolume;
}

void AcousticAnalyzer::setXAxisRulerBlipDuration(int durationMs) {
    if (durationMs < 30) durationMs = 30;
    if (durationMs > 200) durationMs = 200;
    xAxisRulerBlipDurationMs = durationMs;
}

int AcousticAnalyzer::getXAxisRulerBlipDuration() const {
    return xAxisRulerBlipDurationMs;
}

void AcousticAnalyzer::setXAxisRulerMidiDrum(int drumNote) {
    // MIDI drum notes typically range from 35-81
    if (drumNote < 35) drumNote = 35;
    if (drumNote > 81) drumNote = 81;
    xAxisRulerMidiDrum = drumNote;
}

int AcousticAnalyzer::getXAxisRulerMidiDrum() const {
    return xAxisRulerMidiDrum;
}

// Status line implementation
void AcousticAnalyzer::toggleStatusLine() {
    statusLineEnabled.store(!statusLineEnabled.load());
    
    if (logger) {
        logger->log("ACOUSTIC", statusLineEnabled.load() ? 
            "Status line enabled" : "Status line disabled");
    }
}

std::string AcousticAnalyzer::getStatusLineText() const {
    // Read shared data under lock, then format outside lock
    size_t pos;
    size_t dataSize;
    MeasurementPoint pt;
    
    {
        std::lock_guard<std::mutex> lock(statusLineMutex);
        
        if (measurementData.empty()) {
            return "[No data]";
        }
        
        pos = currentPos.load();
        dataSize = measurementData.size();
        if (pos >= dataSize) {
            pos = dataSize - 1;
        }
        
        pt = measurementData[pos];
    }
    
    // Format string outside lock to minimize critical section
    std::ostringstream oss;
    bool firstItem = true;
    
    // Use individual toggles to build status line
    if (statusLineShowPosition) {
        oss << "Pos: " << pos << "/" << (dataSize - 1);
        firstItem = false;
    }
    
    if (statusLineShowFrequency) {
        if (!firstItem) oss << " | ";
        oss << "Freq: " << pt.freq << " Hz";
        firstItem = false;
    }
    
    if (statusLineShowSWR) {
        if (!firstItem) oss << " | ";
        oss << "SWR: " << std::fixed << std::setprecision(2) << pt.swr;
        firstItem = false;
    }
    
    if (statusLineShowRL) {
        if (!firstItem) oss << " | ";
        oss << "RL: " << std::fixed << std::setprecision(2) << pt.rl << " dB";
        firstItem = false;
    }
    
    if (statusLineShowImpedance) {
        if (!firstItem) oss << " | ";
        oss << "|Z|: " << std::fixed << std::setprecision(2) << pt.impedance_mag << " Ω";
        firstItem = false;
    }
    
    if (statusLineShowReactance) {
        if (!firstItem) oss << " | ";
        oss << "X: " << std::fixed << std::setprecision(2) << pt.X << " Ω";
        firstItem = false;
    }
    
    if (statusLineShowPhase) {
        if (!firstItem) oss << " | ";
        oss << "Phase: " << std::fixed << std::setprecision(1) << pt.phase_deg << "°";
        firstItem = false;
    }
    
    return oss.str();
}

void AcousticAnalyzer::rulerThreadFunc() {
    // Play an ascending sequence of tones representing the Y-axis scale
    // Full integer values get distinctive blips, tenths get smaller blips
    // Sound is positioned on the left stereo channel for ping-pong effect with frozen playback
    
    if (logger) {
        logger->log("ACOUSTIC", "Playing Y-axis ruler");
    }
    
    // Determine the Y-axis range based on enabled curves
    // For simplicity, we'll use a standard range that covers common values
    // SWR typically ranges from 1.0 to 20.0
    double minValue = 1.0;
    double maxValue = 20.0;
    
    // Get frequency range for mapping
    int minHz = minFreqHz.load();
    int maxHz = maxFreqHz.load();
    
    // Duration constants for different blip types (relative to base duration)
    // Use the lengthening factor to scale the durations
    const int baseBlipMs = rulerBlipDurationMs;  // Shortest blip (half integers)
    const double lengtheningFactor = rulerLengtheningFactorPercent / 100.0;  // Convert % to factor
    
    // Duration multipliers relative to the lengthening factor
    constexpr double FULL_INTEGER_MULTIPLIER = 1.0;      // Full integers: 1.0x lengthening factor
    constexpr double FIVE_FIFTEEN_MULTIPLIER = 1.33;     // At 5 and 15: 1.33x lengthening factor
    constexpr double TEN_MULTIPLIER = 1.67;              // At 10: 1.67x lengthening factor
    
    const int fullBlipMs = static_cast<int>(baseBlipMs * lengtheningFactor * FULL_INTEGER_MULTIPLIER);  // Full integers
    const int fiveBlipMs = static_cast<int>(baseBlipMs * lengtheningFactor * FIVE_FIFTEEN_MULTIPLIER);  // At 5 and 15
    const int tenBlipMs = static_cast<int>(baseBlipMs * lengtheningFactor * TEN_MULTIPLIER);   // At 10
    const int pauseMs = 50;  // Pause between blips
    const int halfValueVolumePercent = 60;  // Volume percentage for half values
    
    // In synth mode, use a harmonic approach to avoid intermodulation
    // Use octaves and perfect fifths (harmonically related frequencies)
    bool useSynthHarmonic = (audioEngine && audioEngine->getEngineType() == AudioEngineType::SYNTHESIZER);
    
    // Generate and play blips from bottom to top
    // We'll play from 1.0 to 20.0 in 0.5 increments
    for (double value = minValue; value <= maxValue && !rulerShouldStop.load(); value += 0.5) {
        // Check if ruler should stop
        if (rulerShouldStop.load()) {
            break;
        }
        
        // Determine if this is a full integer or a half value
        bool isFullInteger = (static_cast<int>(value * 2) % 2 == 0);
        
        // Determine blip duration based on special positions
        int blipDuration = baseBlipMs;  // Default for half integers
        if (isFullInteger) {
            int intValue = static_cast<int>(value);
            if (intValue == 10) {
                blipDuration = tenBlipMs;  // Longest at 10
            } else if (intValue == 5 || intValue == 15) {
                blipDuration = fiveBlipMs;  // Longer at 5 and 15
            } else {
                blipDuration = fullBlipMs;  // Standard full integer
            }
        }
        
        // Normalize value to 0.0-1.0 range
        double normalizedValue = (value - minValue) / (maxValue - minValue);
        
        // Calculate pitch - use harmonic series in synth mode for clean sound
        double pitchHz;
        if (useSynthHarmonic) {
            // Use exponential scaling for harmonic distribution
            // This creates musically related frequencies that don't clash
            double logMin = std::log2(static_cast<double>(minHz));
            double logMax = std::log2(static_cast<double>(maxHz));
            double logPitch = logMin + normalizedValue * (logMax - logMin);
            pitchHz = std::pow(2.0, logPitch);
        } else {
            // Linear scaling for MIDI mode
            pitchHz = minHz + normalizedValue * (maxHz - minHz);
        }
        
        // Number of samples for this blip
        int samples = (SAMPLE_RATE * blipDuration) / 1000;
        
        // Create audio buffer
        std::vector<int16_t> buffer(samples * 2, 0);  // Stereo
        
        // Position completely on the left (pan = 0.0) for ping-pong stereo effect
        double panFraction = 0.0;
        
        // Use higher volume for full integers, lower for half values
        int baseVolume = (rulerVolume * masterVolume) / 100;  // Apply both ruler and master volume
        int volume = isFullInteger ? baseVolume : (baseVolume * halfValueVolumePercent) / 100;
        
        // Determine curve index for audio generation based on sound mode
        int curveIndex = 0;
        if (rulerSoundMode == RulerSoundMode::FOLLOW_LAST_CURVE) {
            // Use the last enabled curve
            std::lock_guard<std::mutex> lock(curveMutex);
            // Find first enabled curve as fallback
            for (int i = 0; i < 5; i++) {
                if (curves[i].enabled) {
                    curveIndex = i;
                    lastEnabledCurve = i;  // Remember for next iteration
                    break;
                }
            }
            // If no curve is enabled, use last remembered curve
            if (!curves[curveIndex].enabled && lastEnabledCurve >= 0 && lastEnabledCurve < 5) {
                curveIndex = lastEnabledCurve;
            }
        } else {  // RulerSoundMode::CUSTOM_SOUND
            // Use custom sound setting based on engine type
            if (audioEngine->getEngineType() == AudioEngineType::MIDI) {
                // For MIDI, use custom MIDI instruments
                // The actual instrument selection based on gliding/dotted mode will be handled in generateRulerAudio
                // For now, pass a special value to indicate custom mode
                curveIndex = -1;  // Special marker for custom MIDI mode
            } else {
                // For Synthesizer, use custom waveform
                curveIndex = rulerCustomSoundSynth;
            }
        }
        
        // Generate the blip sound using dedicated ruler audio method
        // This avoids conflicts with curve channels in MIDI mode
        audioEngine->generateRulerAudio(buffer, samples, pitchHz, panFraction, volume, curveIndex);
        
#if defined(_WIN32)
        // Play the buffer
        playAudioBuffer(buffer);
        
        // Wait for the blip to finish
        platform_sleep_ms(blipDuration);
        
        // Stop the ruler note after the blip duration (important for MIDI mode)
        // This ensures clean, distinct blips with proper duration
        if (audioEngine) {
            audioEngine->stopRulerNote();
        }
        
        // Check again if we should stop before the pause
        if (rulerShouldStop.load()) {
            break;
        }
        
        // Small pause between blips - longer when curves are playing
        platform_sleep_ms(pauseMs);
#endif
    }
    
    // Ensure all ruler sounds are stopped
    if (audioEngine) {
        audioEngine->stopRulerNote();
        // Also stop any curve notes that might still be active
        // This prevents hanging notes when switching back to playing state
        audioEngine->stopAllNotes();
    }
    
#if defined(_WIN32)
    // Flush any pending audio buffers to stop sounds immediately
    flushAudioBuffers();
#endif
    
    // Restore previous playback state
    PlaybackState restoreState = stateBeforeRuler;
    if (restoreState == PlaybackState::PLAYING) {
        // Resume playing
        play();
    }
    
    rulerPlaying.store(false);
    
    if (logger) {
        logger->log("ACOUSTIC", "Y-axis ruler playback complete");
    }
}
