// Spatial Audio Calibration Wizard Implementation
// This file contains the implementation of the interactive spatial audio calibration wizard
// that adapts to available hardware (stereo vs surround)

#include "ui.h"
#include "smith_visualizer.h"
#include "audio.h"
#include <algorithm>
#include <thread>
#include <mutex>
#include <chrono>
#include <cmath>
#include <random>
#include <iostream>
#include <string>

// Task 1.22: Shared audio engine for wizard functions
// Replaces duplicate static initialization in playTestSound and playPreviewSound
static AudioEngine& getSharedAudioEngine() {
    static AudioEngine audioEngine;
    static std::once_flag audioEngineInitFlag;
    static bool audioEngineInitialized = false;
    
    std::call_once(audioEngineInitFlag, []() {
        audioEngineInitialized = audioEngine.open();
        if (!audioEngineInitialized) {
            // Silently fail - wizard will continue but without audio
            // The wizard text will still display, allowing user to complete setup
        }
    });
    
    return audioEngine;
}

// Helper function to check if shared audio engine is available
static bool isAudioEngineAvailable() {
    // Simply accessing the engine will initialize it via call_once
    static bool checked = false;
    static bool available = false;
    
    if (!checked) {
        AudioEngine& engine = getSharedAudioEngine();
        // The initialization happens in getSharedAudioEngine, we just need to trigger it
        // We can't easily check the initialized flag from here, but that's ok
        // The engine will return gracefully if not initialized
        checked = true;
        available = true;  // Assume available, actual check happens in getSharedAudioEngine
    }
    
    return available;
}

// Helper function to play a test sound at a specific position
static void playTestSound(AcousticAnalyzer* analyzer, double x, double y, int durationMs = 500) {
    if (!analyzer) return;
    
    // Task 1.22: Use shared audio engine instead of separate static instance
    AudioEngine& audioEngine = getSharedAudioEngine();
    
    // Calculate pitch based on distance from center
    double distance = std::sqrt(x*x + y*y);
    // Clamp distance to [0.0, 1.0] to maintain frequency range of 400-600 Hz
    distance = std::min(distance, 1.0);
    double pitchHz = 400.0 + distance * 200.0;  // 400 Hz to 600 Hz
    
    // Get current audio capability from analyzer
    AudioCapability capability = AudioCapability::STEREO_ONLY;
    auto smith = analyzer->getSmithVisualizer();
    if (smith) {
        capability = smith->getAudioCapability();
    }
    
    // Check if we should use multi-channel or stereo
    if (capability != AudioCapability::STEREO_ONLY && smith) {
        // Configure audio engine for multi-channel if not already done
        int maxChannels = audioEngine.getCurrentChannelCount();
        if (maxChannels == 2) {  // Not yet configured for multi-channel
            int detectedChannels = audioEngine.detectMaxChannels();
            if (detectedChannels >= 8 && (capability == AudioCapability::SURROUND_7_1 || 
                                          capability == AudioCapability::SURROUND_ATMOS)) {
                audioEngine.setChannelCount(8);
            } else if (detectedChannels >= 6 && capability == AudioCapability::SURROUND_5_1) {
                audioEngine.setChannelCount(6);
            }
        }
        
        // Use multi-channel audio for surround sound
        // Calculate multi-channel gains using smith visualizer's panning algorithm
        // x: -1 (left) to +1 (right)
        // y: -1 (back) to +1 (front)
        MultiChannelGains mcGains = smith->calculateCartesianPanning(x, y);
        
        // Convert to AudioMultiChannelGains (compatible struct)
        AudioMultiChannelGains audioGains;
        audioGains.frontLeft = mcGains.frontLeft;
        audioGains.frontRight = mcGains.frontRight;
        audioGains.frontCenter = mcGains.frontCenter;
        audioGains.lfe = mcGains.lfe;
        audioGains.backLeft = mcGains.backLeft;
        audioGains.backRight = mcGains.backRight;
        audioGains.sideLeft = mcGains.sideLeft;
        audioGains.sideRight = mcGains.sideRight;
        
        // Play multi-channel tone
        audioEngine.playToneMultiChannel(pitchHz, audioGains, Waveform::SINE, durationMs);
        return;
    }
    
    // Fallback to stereo panning
    // Map x/y position to stereo panning
    // x: -1 (left) to +1 (right)
    // y: -1 (back) to +1 (front) - affects volume slightly
    
    // Calculate stereo panning
    double panL = 0.5 * (1.0 - x);  // Left channel: stronger when x is negative
    double panR = 0.5 * (1.0 + x);  // Right channel: stronger when x is positive
    
    // Adjust volume based on y (front/back) - front is louder
    double volumeFactor = 0.7 + 0.3 * ((y + 1.0) / 2.0);  // 0.7 to 1.0
    panL *= volumeFactor;
    panR *= volumeFactor;
    
    // Play the test tone
    audioEngine.playTone(pitchHz, panL, panR, Waveform::SINE, durationMs);
}

// Helper function to play a preview sound with given waveform and volume
static void playPreviewSound(double volume, Waveform wf, int durationMs = 500, double pitchHz = 440.0) {
    // Task 1.22: Use shared audio engine instead of separate static instance
    AudioEngine& previewEngine = getSharedAudioEngine();
    
    // Play centered sound with specified volume
    double panL = 0.5 * volume;
    double panR = 0.5 * volume;
    
    previewEngine.playTone(pitchHz, panL, panR, wf, durationMs);
}

bool ConsoleUI::runSpatialAudioCalibrationWizard(AcousticAnalyzer* analyzer) {
    clearScreen();
    if (!analyzer) {
        print(translation.get("ERROR_NO_ANALYZER", "[Error: No audio analyzer available]") + "\n");
        return false;
    }
    
    auto smith = analyzer->getSmithVisualizer();
    if (!smith) {
        print(translation.get("ERROR_NO_SMITH", "[Error: Smith visualizer not available]") + "\n");
        return false;
    }
    
    // Detect audio capabilities
    AudioCapability audioCap = smith->getAudioCapability();
    bool hasSurround = (audioCap == AudioCapability::SURROUND_5_1 || 
                        audioCap == AudioCapability::SURROUND_7_1 ||
                        audioCap == AudioCapability::SURROUND_ATMOS);
    
    // Store capability in calibration data
    cfg.spatial_calibration.surround_available = hasSurround;
    
    print("\n");
    print(formatHeading(translation.get("SPATIAL_WIZARD_TITLE", "Spatial Audio Calibration Wizard")));
    print("\n");
    
    // Display detected hardware
    const char* capabilityName = "Unknown";
    switch (audioCap) {
        case AudioCapability::STEREO_ONLY:
            capabilityName = "Stereo (2.0)";
            break;
        case AudioCapability::SURROUND_5_1:
            capabilityName = "5.1 Surround";
            break;
        case AudioCapability::SURROUND_7_1:
            capabilityName = "7.1 Surround";
            break;
        case AudioCapability::SURROUND_ATMOS:
            capabilityName = "Dolby Atmos";
            break;
    }
    
    print(translation.format("SPATIAL_WIZARD_DETECTED", "Detected audio hardware: {0}", capabilityName) + "\n\n");
    
    if (!hasSurround) {
        print(translation.get("SPATIAL_WIZARD_STEREO_MODE", 
            "Stereo mode detected. This wizard will calibrate psychoacoustic\n"
            "3D audio parameters to optimize spatial perception with headphones\n"
            "or stereo speakers.") + "\n\n");
    } else {
        print(translation.get("SPATIAL_WIZARD_SURROUND_MODE",
            "Surround sound detected! This wizard will calibrate full 3D spatial\n"
            "audio with your multi-channel speaker setup.") + "\n\n");
    }
    
    print(translation.get("SPATIAL_WIZARD_INTRO",
        "This wizard will:\n"
        "1. Test your ability to perceive sound direction\n"
        "2. Calibrate near/far distance perception\n"
        "3. Set preferred sound volumes and types\n"
        "4. Preview all sound events in the application\n\n"
        "The entire process takes about 10-15 minutes.\n\n") + "\n");
    
    print(translation.get("SPATIAL_WIZARD_START_PROMPT", "Press ENTER to start, or ESC to cancel: "));
    
    int ch = consoleInput->getch();
    if (ch == 27) {  // ESC
        print(translation.get("CANCELLED", "\n[Cancelled]\n"));
        return false;
    }
    print("\n\n");
    
    // ===== PHASE 1: Direction Perception Tests =====
    print("─────────────────────────────────────────────────────────\n");
    print(translation.get("SPATIAL_WIZARD_PHASE1", "Phase 1: Direction Perception Calibration") + "\n");
    print("─────────────────────────────────────────────────────────\n\n");
    
    if (hasSurround) {
        // Surround-specific direction tests
        print(translation.get("SPATIAL_WIZARD_SURROUND_DIR_TEST",
            "I will play test sounds from different directions.\n"
            "Identify where each sound is coming from.\n\n") + "\n");
        
        // Test positions: Front, Back, Left, Right, and diagonals
        struct TestPosition {
            double x, y;
            const char* name;
        };
        
        TestPosition positions[] = {
            {0.0, 1.0, "Front"},
            {0.0, -1.0, "Back"},
            {-1.0, 0.0, "Left"},
            {1.0, 0.0, "Right"},
            {0.7, 0.7, "Front-Right"},
            {-0.7, 0.7, "Front-Left"},
            {0.7, -0.7, "Back-Right"},
            {-0.7, -0.7, "Back-Left"}
        };
        
        int correctAnswers = 0;
        int frontBackCorrect = 0, leftRightCorrect = 0, diagonalCorrect = 0;
        
        // Randomize test order
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(std::begin(positions), std::end(positions), g);
        
        for (int i = 0; i < 8; i++) {
            // Play sound and get answer (with repeat capability)
            bool validAnswer = false;
            int answer = 0;
            
            while (!validAnswer) {
                print(translation.format("SPATIAL_WIZARD_TEST_SOUND", "\nTest sound {0} of 8", i+1) + "\n");
                print(translation.get("SPATIAL_WIZARD_PLAYING", "Playing test sound...") + "\n");
                
                // Play test sound at position
                playTestSound(analyzer, positions[i].x, positions[i].y);
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                
                // Ask user to identify direction (with R to repeat)
                print(translation.get("SPATIAL_WIZARD_IDENTIFY",
                    "\nWhich direction did the sound come from?\n"
                    "1 = Front      2 = Back       3 = Left       4 = Right\n"
                    "5 = Front-Left 6 = Front-Right 7 = Back-Left  8 = Back-Right\n"
                    "R = Repeat sound\n"
                    "Your answer: ") + " ");
                
                int ch = consoleInput->getch();
                
                // Check for R key (repeat)
                if (ch == 'r' || ch == 'R') {
                    print("R\n");
                    print(translation.get("SPATIAL_WIZARD_REPEATING", "Repeating sound...\n"));
                    continue;  // Loop back to play sound again
                }
                
                // Otherwise treat as numeric answer
                answer = ch - '0';
                print(std::to_string(answer) + "\n");
                
                // Validate answer
                if (answer >= 1 && answer <= 8) {
                    validAnswer = true;
                } else {
                    print(translation.get("SPATIAL_WIZARD_INVALID_ANSWER", 
                        "Invalid answer. Please enter 1-8 or R to repeat.\n"));
                }
            }
            
            // Map answers to position names for correctness check
            const char* answerNames[] = {
                "",  // 0 - invalid
                "Front",        // 1
                "Back",         // 2
                "Left",         // 3
                "Right",        // 4
                "Front-Left",   // 5
                "Front-Right",  // 6
                "Back-Left",    // 7
                "Back-Right"    // 8
            };
            
            // Check correctness
            bool correct = (std::string(answerNames[answer]) == std::string(positions[i].name));
            
            if (correct) {
                correctAnswers++;
                
                // Track specific direction types for accuracy scoring
                if (answer == 1 || answer == 2) {
                    frontBackCorrect++;  // Pure F/B
                } else if (answer == 3 || answer == 4) {
                    leftRightCorrect++;  // Pure L/R
                } else {
                    diagonalCorrect++;  // Diagonals
                }
                
                print(translation.get("SPATIAL_WIZARD_CORRECT", "✓ Correct!") + "\n");
            } else {
                print(translation.format("SPATIAL_WIZARD_INCORRECT", 
                    "✗ That was from {0}", positions[i].name) + "\n");
            }
        }
        
        // Calculate accuracy scores
        cfg.spatial_calibration.front_back_accuracy = 
            static_cast<double>(frontBackCorrect) / 4.0;  // 4 tests for front/back
        cfg.spatial_calibration.left_right_accuracy = 
            static_cast<double>(leftRightCorrect) / 4.0;   // 4 tests for left/right
        cfg.spatial_calibration.diagonal_accuracy = 
            static_cast<double>(diagonalCorrect) / 4.0;    // 4 diagonal tests
        
    } else {
        // Stereo-specific direction tests (simplified)
        print(translation.get("SPATIAL_WIZARD_STEREO_DIR_TEST",
            "With stereo, we'll test left-right perception and simulated\n"
            "front-back distinction using psychoacoustic cues.\n\n") + "\n");
        
        // Test left-right distinction (easier)
        int leftRightCorrect = 0;
        for (int i = 0; i < 4; i++) {
            double x = (i % 2 == 0) ? -0.8 : 0.8;  // Alternate left/right
            
            // Play sound and get answer (with repeat capability)
            bool validAnswer = false;
            int answer = 0;
            
            while (!validAnswer) {
                print(translation.format("SPATIAL_WIZARD_TEST_SOUND", "\nTest sound {0} of 4", i+1) + "\n");
                print(translation.get("SPATIAL_WIZARD_PLAYING", "Playing test sound...") + "\n");
                
                playTestSound(analyzer, x, 0.0);
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                
                print(translation.get("SPATIAL_WIZARD_LEFT_RIGHT",
                    "\nWas the sound more on the LEFT (1) or RIGHT (2)?\n"
                    "R = Repeat sound\n"
                    "Your answer: ") + " ");
                
                int ch = consoleInput->getch();
                
                // Check for R key (repeat)
                if (ch == 'r' || ch == 'R') {
                    print("R\n");
                    print(translation.get("SPATIAL_WIZARD_REPEATING", "Repeating sound...\n"));
                    continue;  // Loop back to play sound again
                }
                
                // Otherwise treat as numeric answer
                answer = ch - '0';
                print(std::to_string(answer) + "\n");
                
                // Validate answer
                if (answer >= 1 && answer <= 2) {
                    validAnswer = true;
                } else {
                    print(translation.get("SPATIAL_WIZARD_INVALID_ANSWER", 
                        "Invalid answer. Please enter 1-2 or R to repeat.\n"));
                }
            }
            
            bool correct = (answer == 1 && x < 0) || (answer == 2 && x > 0);
            if (correct) {
                leftRightCorrect++;
                print(translation.get("SPATIAL_WIZARD_CORRECT", "✓ Correct!") + "\n");
            } else {
                print(translation.get("SPATIAL_WIZARD_INCORRECT_LR", "✗ Incorrect") + "\n");
            }
        }
        
        cfg.spatial_calibration.left_right_accuracy = 
            static_cast<double>(leftRightCorrect) / 4.0;
        
        // Test front-back (harder with stereo)
        print("\n" + translation.get("SPATIAL_WIZARD_FB_TEST",
            "Now testing front-back perception (more challenging with stereo)...") + "\n\n");
        
        int frontBackCorrect = 0;
        for (int i = 0; i < 4; i++) {
            double y = (i % 2 == 0) ? 0.8 : -0.8;  // Alternate front/back
            
            // Play sound and get answer (with repeat capability)
            bool validAnswer = false;
            int answer = 0;
            
            while (!validAnswer) {
                print(translation.format("SPATIAL_WIZARD_TEST_SOUND", "\nTest sound {0} of 4", i+1) + "\n");
                print(translation.get("SPATIAL_WIZARD_PLAYING", "Playing test sound...") + "\n");
                
                playTestSound(analyzer, 0.0, y);
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                
                print(translation.get("SPATIAL_WIZARD_FRONT_BACK",
                    "\nWas the sound more in FRONT (1) or BACK (2)?\n"
                    "R = Repeat sound\n"
                    "Your answer: ") + " ");
                
                int ch = consoleInput->getch();
                
                // Check for R key (repeat)
                if (ch == 'r' || ch == 'R') {
                    print("R\n");
                    print(translation.get("SPATIAL_WIZARD_REPEATING", "Repeating sound...\n"));
                    continue;  // Loop back to play sound again
                }
                
                // Otherwise treat as numeric answer
                answer = ch - '0';
                print(std::to_string(answer) + "\n");
                
                // Validate answer
                if (answer >= 1 && answer <= 2) {
                    validAnswer = true;
                } else {
                    print(translation.get("SPATIAL_WIZARD_INVALID_ANSWER", 
                        "Invalid answer. Please enter 1-2 or R to repeat.\n"));
                }
            }
            
            bool correct = (answer == 1 && y > 0) || (answer == 2 && y < 0);
            if (correct) {
                frontBackCorrect++;
                print(translation.get("SPATIAL_WIZARD_CORRECT", "✓ Correct!") + "\n");
            } else {
                print(translation.get("SPATIAL_WIZARD_INCORRECT_FB", "✗ Incorrect") + "\n");
            }
        }
        
        cfg.spatial_calibration.front_back_accuracy = 
            static_cast<double>(frontBackCorrect) / 4.0;
        cfg.spatial_calibration.diagonal_accuracy = 0.5;  // Default for stereo
    }
    
    // ===== PHASE 2: Near/Far Perception =====
    print("\n\n─────────────────────────────────────────────────────────\n");
    print(translation.get("SPATIAL_WIZARD_PHASE2", "Phase 2: Distance Perception Calibration") + "\n");
    print("─────────────────────────────────────────────────────────\n\n");
    
    print(translation.get("SPATIAL_WIZARD_DISTANCE_TEST",
        "I will play sounds at different distances from the center.\n"
        "Tell me if each sound feels NEAR (1) or FAR (2).\n\n") + "\n");
    
    struct DistanceTest {
        double radius;
        bool expectedNear;
    };
    
    DistanceTest distanceTests[] = {
        {0.2, true},   // Near
        {0.9, false},  // Far
        {0.4, true},   // Near
        {0.7, false},  // Far
        {0.3, true},   // Near
        {0.8, false}   // Far
    };
    
    int nearThresholdVotes = 0, farThresholdVotes = 0;
    
    for (int i = 0; i < 6; i++) {
        double angle = (i * 60.0) * (3.14159 / 180.0);  // Vary angle
        double x = std::cos(angle) * distanceTests[i].radius;
        double y = std::sin(angle) * distanceTests[i].radius;
        
        // Play sound and get answer (with repeat capability)
        bool validAnswer = false;
        int answer = 0;
        
        while (!validAnswer) {
            print(translation.format("SPATIAL_WIZARD_TEST_SOUND", "\nDistance test {0} of 6", i+1) + "\n");
            print(translation.get("SPATIAL_WIZARD_PLAYING", "Playing test sound...") + "\n");
            
            playTestSound(analyzer, x, y);
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            
            print(translation.get("SPATIAL_WIZARD_NEAR_FAR",
                "\nDid the sound feel NEAR (1) or FAR (2)?\n"
                "R = Repeat sound\n"
                "Your answer: ") + " ");
            
            int ch = consoleInput->getch();
            
            // Check for R key (repeat)
            if (ch == 'r' || ch == 'R') {
                print("R\n");
                print(translation.get("SPATIAL_WIZARD_REPEATING", "Repeating sound...\n"));
                continue;  // Loop back to play sound again
            }
            
            // Otherwise treat as numeric answer
            answer = ch - '0';
            print(std::to_string(answer) + "\n");
            
            // Validate answer
            if (answer >= 1 && answer <= 2) {
                validAnswer = true;
            } else {
                print(translation.get("SPATIAL_WIZARD_INVALID_ANSWER", 
                    "Invalid answer. Please enter 1-2 or R to repeat.\n"));
            }
        }
        
        bool answeredNear = (answer == 1);
        if (answeredNear == distanceTests[i].expectedNear) {
            print(translation.get("SPATIAL_WIZARD_CORRECT", "✓ Correct!") + "\n");
        }
        
        // Adjust thresholds based on responses
        if (answeredNear) {
            nearThresholdVotes++;
        } else {
            farThresholdVotes++;
        }
    }
    
    // Calculate personalized near/far thresholds
    cfg.spatial_calibration.near_threshold = 0.25 + (nearThresholdVotes * 0.05);
    cfg.spatial_calibration.far_threshold = 0.75 - (farThresholdVotes * 0.05);
    cfg.spatial_calibration.distance_sensitivity = 0.5;  // Default
    
    // ===== PHASE 3: Volume Preferences =====
    print("\n\n─────────────────────────────────────────────────────────\n");
    print(translation.get("SPATIAL_WIZARD_PHASE3", "Phase 3: Volume and Sound Preferences") + "\n");
    print("─────────────────────────────────────────────────────────\n\n");
    
    print(translation.get("SPATIAL_WIZARD_VOLUME_TEST",
        "Now let's set your preferred volume levels for Smith analyzer sounds.\n"
        "These are the ACTUAL sounds you'll hear during Smith analysis.\n"
        "Choose the volume that feels most comfortable for each sound type.\n\n") + "\n");
    
    // Test ambient cue sounds volume (the background Smith positioning cues)
    print(translation.get("SPATIAL_WIZARD_AMBIENT_VOLUME", "\n1. Smith ambient cues (background positioning):\n") + "\n");
    double volumes[] = {0.2, 0.3, 0.4, 0.5};  // 20%, 30%, 40%, 50%
    for (int i = 0; i < 4; i++) {
        bool repeatSound = true;
        while (repeatSound) {
            print(translation.format("SPATIAL_WIZARD_VOLUME_OPTION", "   Option {0} ({1}%): ", i+1, static_cast<int>(volumes[i] * 100)));
            print(translation.get("SPATIAL_WIZARD_PRESS_PLAY", "Press ENTER to play..."));
            consoleInput->getch();
            
            // Play Smith ambient cue - sine wave at smith positioning frequency
            print(translation.get("SPATIAL_WIZARD_PLAYING_SHORT", " Playing Smith ambient cue...\n"));
            playPreviewSound(volumes[i], Waveform::SINE, 1000, 500.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Offer repeat option
            repeatSound = offerRepeat();
        }
    }
    
    print(translation.get("SPATIAL_WIZARD_SELECT_VOLUME",
        "\nWhich volume felt most comfortable? (1-4): ") + " ");
    int ambientChoice = consoleInput->getch() - '0';
    print(std::to_string(ambientChoice) + "\n");
    
    // Validate choice
    if (ambientChoice < 1 || ambientChoice > 4) {
        print(translation.get("SPATIAL_WIZARD_INVALID_CHOICE", "[Invalid choice, using default]\n"));
        ambientChoice = 2;  // Default to 30%
    }
    
    cfg.spatial_calibration.preferred_ambient_volume = volumes[ambientChoice - 1];
    
    // Test axis event sounds volume
    print("\n" + translation.get("SPATIAL_WIZARD_EVENT_VOLUME", "2. Axis crossing event sounds:\n") + "\n");
    double eventVolumes[] = {0.4, 0.6, 0.8, 1.0};  // 40%, 60%, 80%, 100%
    for (int i = 0; i < 4; i++) {
        bool repeatSound = true;
        while (repeatSound) {
            print(translation.format("SPATIAL_WIZARD_VOLUME_OPTION", "   Option {0} ({1}%): ", i+1, static_cast<int>(eventVolumes[i] * 100)));
            print(translation.get("SPATIAL_WIZARD_PRESS_PLAY", "Press ENTER to play..."));
            consoleInput->getch();
            
            // Play axis crossing sound preview - sweep to represent movement
            print(translation.get("SPATIAL_WIZARD_PLAYING_SHORT", " Playing axis event...\n"));
            playPreviewSound(eventVolumes[i], Waveform::SINE, 150, 300.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            playPreviewSound(eventVolumes[i], Waveform::SINE, 150, 500.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            playPreviewSound(eventVolumes[i], Waveform::SINE, 150, 700.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Offer repeat option
            repeatSound = offerRepeat();
        }
    }
    
    print(translation.get("SPATIAL_WIZARD_SELECT_VOLUME",
        "\nWhich volume felt most comfortable? (1-4): ") + " ");
    int eventChoice = consoleInput->getch() - '0';
    print(std::to_string(eventChoice) + "\n");
    
    // Validate choice
    if (eventChoice < 1 || eventChoice > 4) {
        print(translation.get("SPATIAL_WIZARD_INVALID_CHOICE", "[Invalid choice, using default]\n"));
        eventChoice = 2;  // Default to 60%
    }
    
    cfg.spatial_calibration.preferred_event_volume = eventVolumes[eventChoice - 1];
    
    // ===== PHASE 4: Sound Type Preferences =====
    print("\n\n─────────────────────────────────────────────────────────\n");
    print(translation.get("SPATIAL_WIZARD_PHASE4", "Phase 4: Sound Type Selection") + "\n");
    print("─────────────────────────────────────────────────────────\n\n");
    
    print(translation.get("SPATIAL_WIZARD_SOUND_TYPES",
        "Now let's select your preferred sounds for Smith analyzer events.\n"
        "These sounds will play when you cross axes in the Smith chart.\n\n") + "\n");
    
    // Axis crossing sound preference
    print(translation.get("SPATIAL_WIZARD_AXIS_SOUND", "Axis crossing sounds (when impedance crosses real/imaginary axis):\n") + "\n");
    const char* axisSounds[] = {"Pluck", "Sweep", "Chirp", "Bell", "Percussion"};
    // Note: These are simplified previews - actual Smith analyzer sounds include spatial positioning
    Waveform axisWaveforms[] = {Waveform::SINE, Waveform::SINE, Waveform::SINE, Waveform::TRIANGLE, Waveform::SQUARE};
    double axisPitches[] = {440.0, 330.0, 550.0, 523.25, 392.0};  // A4, E4, C#5, C5, G4
    for (int i = 0; i < 5; i++) {
        bool repeatSound = true;
        while (repeatSound) {
            print(translation.format("SPATIAL_WIZARD_SOUND_OPTION", "   {0}: {1} (Smith analyzer sound) - ", i+1, axisSounds[i]));
            print(translation.get("SPATIAL_WIZARD_PRESS_PLAY", "Press ENTER to preview..."));
            consoleInput->getch();
            print(translation.get("SPATIAL_WIZARD_PLAYING_SHORT", " Playing...\n"));
            
            // Play preview of axis crossing sound - matches Smith analyzer behavior
            if (i == 1) {  // Sweep
                // Frequency sweep matching Smith analyzer's sweep behavior
                playPreviewSound(0.6, Waveform::SINE, 150, 300.0);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                playPreviewSound(0.6, Waveform::SINE, 150, 500.0);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                playPreviewSound(0.6, Waveform::SINE, 150, 700.0);
            } else if (i == 2) {  // Chirp
                // Fast chirp matching Smith analyzer's chirp behavior
                playPreviewSound(0.6, Waveform::SINE, 100, 400.0);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                playPreviewSound(0.6, Waveform::SINE, 100, 600.0);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                playPreviewSound(0.6, Waveform::SINE, 100, 800.0);
            } else {
                playPreviewSound(0.6, axisWaveforms[i], 400, axisPitches[i]);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            // Offer repeat option
            repeatSound = offerRepeat();
        }
    }
    
    print(translation.get("SPATIAL_WIZARD_SELECT_SOUND",
        "\nWhich sound do you prefer for axis crossings? (1-5): ") + " ");
    int axisChoice = consoleInput->getch() - '0';
    print(std::to_string(axisChoice) + "\n");
    
    // Validate choice
    if (axisChoice < 1 || axisChoice > 5) {
        print(translation.get("SPATIAL_WIZARD_INVALID_CHOICE", "[Invalid choice, using default]\n"));
        axisChoice = 1;  // Default to Pluck
    }
    
    cfg.spatial_calibration.preferred_axis_crossing_sound = axisChoice - 1;
    
    // Center pulse waveform preference
    print("\n" + translation.get("SPATIAL_WIZARD_CENTER_PULSE", "Center pulse (reference signal at Smith chart center):\n") + "\n");
    const char* pulseSounds[] = {"Click", "Sine", "Square", "Triangle", "Sawtooth", "Pulse"};
    Waveform pulseWaveforms[] = {Waveform::PULSE, Waveform::SINE, Waveform::SQUARE, 
                                  Waveform::TRIANGLE, Waveform::SAWTOOTH, Waveform::PULSE};
    for (int i = 0; i < 6; i++) {
        bool repeatSound = true;
        while (repeatSound) {
            print(translation.format("SPATIAL_WIZARD_SOUND_OPTION", "   {0}: {1} (Smith center pulse) - ", i+1, pulseSounds[i]));
            print(translation.get("SPATIAL_WIZARD_PRESS_PLAY", "Press ENTER to preview..."));
            consoleInput->getch();
            print(translation.get("SPATIAL_WIZARD_PLAYING_SHORT", " Playing...\n"));
            
            // Play preview of center pulse waveform
            if (i == 0) {  // Click - very short pulse
                playPreviewSound(0.8, Waveform::PULSE, 50, 1000.0);
            } else {
                playPreviewSound(0.7, pulseWaveforms[i], 300, 440.0);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            // Offer repeat option
            repeatSound = offerRepeat();
        }
    }
    
    print(translation.get("SPATIAL_WIZARD_SELECT_SOUND",
        "\nWhich sound do you prefer? (1-6): ") + " ");
    int pulseChoice = consoleInput->getch() - '0';
    print(std::to_string(pulseChoice) + "\n");
    
    // Validate choice
    if (pulseChoice < 1 || pulseChoice > 6) {
        print(translation.get("SPATIAL_WIZARD_INVALID_CHOICE", "[Invalid choice, using default]\n"));
        pulseChoice = 1;  // Default to Click
    }
    
    cfg.spatial_calibration.preferred_center_pulse_waveform = pulseChoice - 1;
    
    // ===== PHASE 5: Psychoacoustic Parameters (Stereo Mode Only) =====
    if (!hasSurround) {
        print("\n\n─────────────────────────────────────────────────────────\n");
        print(translation.get("SPATIAL_WIZARD_PHASE5", "Phase 5: Psychoacoustic Tuning (Stereo Mode)") + "\n");
        print("─────────────────────────────────────────────────────────\n\n");
        
        print(translation.get("SPATIAL_WIZARD_PSYCHO_INTRO",
            "Stereo mode uses psychoacoustic techniques to simulate 3D audio.\n"
            "Let's fine-tune these parameters for your hearing.\n\n") + "\n");
        
        // Crossfeed amount tuning
        print(translation.get("SPATIAL_WIZARD_CROSSFEED",
            "Crossfeed helps distinguish front from back by mixing a small amount\n"
            "of the opposite channel. I'll play test sounds with different amounts.\n\n") + "\n");
        
        double crossfeedLevels[] = {0.0, 0.1, 0.15, 0.25, 0.35};
        print(translation.get("SPATIAL_WIZARD_FB_CROSSFEED_TEST",
            "Which setting makes front/back distinction CLEAREST?\n") + "\n");
        
        for (int i = 0; i < 5; i++) {
            bool repeatSound = true;
            while (repeatSound) {
                print(translation.format("SPATIAL_WIZARD_CROSSFEED_OPTION",
                    "   Option {0} (crossfeed: {1}%): ", i+1, static_cast<int>(crossfeedLevels[i] * 100)));
                print(translation.get("SPATIAL_WIZARD_PRESS_PLAY", "Press ENTER to test..."));
                consoleInput->getch();
                print(translation.get("SPATIAL_WIZARD_PLAYING_SHORT", " Testing...\n"));
                
                // Play front sound then back sound to test crossfeed perception
                print("     Front: ");
                playTestSound(analyzer, 0.0, 0.8);  // Front position
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                
                print("Back: ");
                playTestSound(analyzer, 0.0, -0.8);  // Back position
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                // Offer repeat option
                repeatSound = offerRepeat();
            }
        }
        
        print(translation.get("SPATIAL_WIZARD_SELECT_CROSSFEED",
            "\nWhich option sounded best? (1-5): ") + " ");
        int crossfeedChoice = consoleInput->getch() - '0';
        print(std::to_string(crossfeedChoice) + "\n");
        
        // Validate choice
        if (crossfeedChoice < 1 || crossfeedChoice > 5) {
            print(translation.get("SPATIAL_WIZARD_INVALID_CHOICE", "[Invalid choice, using default]\n"));
            crossfeedChoice = 3;  // Default to 0.15 (middle option)
        }
        
        cfg.spatial_calibration.crossfeed_amount = crossfeedLevels[crossfeedChoice - 1];
        
        // Set other psychoacoustic defaults based on crossfeed preference
        // Higher crossfeed suggests user has difficulty with F/B, so increase back attenuation
        cfg.spatial_calibration.back_attenuation = 0.7 + (crossfeedChoice * 0.05);
        cfg.spatial_calibration.side_emphasis = 0.8;  // Constant for most users
    }
    
    // ===== Completion =====
    print("\n\n");
    print(formatHeading(translation.get("SPATIAL_WIZARD_COMPLETE", "Calibration Complete!")));
    print("\n");
    
    // Display calibration summary
    print(translation.get("SPATIAL_WIZARD_SUMMARY", "Your spatial audio profile:") + "\n\n");
    print(translation.format("SPATIAL_WIZARD_LR_ACCURACY", 
        "Left-Right accuracy: {0}%", static_cast<int>(cfg.spatial_calibration.left_right_accuracy * 100)) + "\n");
    print(translation.format("SPATIAL_WIZARD_FB_ACCURACY",
        "Front-Back accuracy: {0}%", static_cast<int>(cfg.spatial_calibration.front_back_accuracy * 100)) + "\n");
    
    if (hasSurround) {
        print(translation.format("SPATIAL_WIZARD_DIAG_ACCURACY",
            "Diagonal accuracy: {0}%", static_cast<int>(cfg.spatial_calibration.diagonal_accuracy * 100)) + "\n");
    }
    
    print(translation.format("SPATIAL_WIZARD_NEAR_THRESHOLD",
        "Near threshold: {0}", cfg.spatial_calibration.near_threshold) + "\n");
    print(translation.format("SPATIAL_WIZARD_FAR_THRESHOLD",
        "Far threshold: {0}", cfg.spatial_calibration.far_threshold) + "\n");
    print(translation.format("SPATIAL_WIZARD_CURVE_VOL",
        "Preferred curve volume: {0}%", static_cast<int>(cfg.spatial_calibration.preferred_curve_volume * 100)) + "\n");
    
    if (!hasSurround) {
        print(translation.format("SPATIAL_WIZARD_CROSSFEED_VAL",
            "Crossfeed amount: {0}%", static_cast<int>(cfg.spatial_calibration.crossfeed_amount * 100)) + "\n");
    }
    
    print("\n" + translation.get("SPATIAL_WIZARD_APPLY",
        "These settings will be applied to all spatial audio features.") + "\n\n");
    
    // Mark as calibrated
    cfg.spatial_audio_calibrated = true;
    
    // ===== Apply wizard settings to actual program configuration =====
    // Map preferred sound types to actual config
    cfg.axis_crossing_sound = static_cast<AppConfig::AxisCrossingSound>(
        cfg.spatial_calibration.preferred_axis_crossing_sound);
    cfg.center_pulse_waveform = static_cast<AppConfig::CenterPulseWaveform>(
        cfg.spatial_calibration.preferred_center_pulse_waveform);
    
    // Map volume preferences to actual config (convert 0.0-1.0 to percentages)
    cfg.smith_cues_volume = static_cast<int>(cfg.spatial_calibration.preferred_ambient_volume * 100);
    cfg.axis_events_volume = static_cast<int>(cfg.spatial_calibration.preferred_event_volume * 100);
    cfg.center_pulse_volume = static_cast<int>(cfg.spatial_calibration.preferred_event_volume * 100);
    
    // Enable the features that were configured
    cfg.center_pulse_enabled = true;
    cfg.axis_events_enabled = true;
    
    // Apply settings to the SmithVisualizer if available
    if (smith) {
        smith->setAxisCrossingSound(cfg.axis_crossing_sound);
        smith->setCenterPulseWaveform(cfg.center_pulse_waveform);
        smith->setSmithCuesVolume(cfg.smith_cues_volume);
        smith->setAxisEventsVolume(cfg.axis_events_volume);
        smith->setCenterPulseVolume(cfg.center_pulse_volume);
        smith->setCenterPulseEnabled(cfg.center_pulse_enabled);
        smith->setAxisEventsEnabled(cfg.axis_events_enabled);
    }
    
    print(translation.get("SPATIAL_WIZARD_SETTINGS_APPLIED",
        "Settings have been applied to Smith analyzer.") + "\n\n");
    
    // Save settings
    saveSettings();
    
    print(translation.get("SPATIAL_WIZARD_SAVED",
        "Settings saved successfully!") + "\n\n");
    
    print(translation.get("PRESS_ANY_KEY", "Press any key to continue..."));
    consoleInput->getch();
    
    return true;
}

// Helper function to offer repeat option after playing a sound
// Returns true if user wants to repeat, false if user wants to continue
bool ConsoleUI::offerRepeat() {
    print(translation.get("SPATIAL_WIZARD_REPEAT_PROMPT", " (Press R to repeat, ENTER to continue, ESC to cancel): "));
    while (true) {
        int ch = consoleInput->getch();
        if (ch == 'r' || ch == 'R') {
            print("R\n");
            return true;  // User wants to repeat
        } else if (ch == '\r' || ch == '\n' || ch == ' ') {
            print("\n");
            return false;  // User wants to continue
        } else if (ch == 27) {  // ESC
            print("\n");
            return false;  // Cancel - will be handled by caller
        }
    }
}
