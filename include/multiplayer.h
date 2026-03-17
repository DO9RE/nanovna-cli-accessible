#pragma once

#ifdef WITH_HAM_SPIRIT

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <cmath>
#include <functional>
#include <chrono>
#include <deque>
#include <atomic>

// Forward declarations
class IAudioBackend;
class ITTSEngine;
class IGamepadInput;
class KeyboardGamepadEmulator;
class SynthesizerEngine;
class Logger;

namespace HamSpirit {

// Forward declarations
class AntennaNetwork;
struct GameConfig;
struct GameStats;
struct TrackPoint;

/**
 * Maximum number of players in multiplayer mode.
 */
static constexpr int MAX_PLAYERS = 4;

/**
 * Game event types for the multiplayer event propagation system.
 * All gameplay-relevant actions are represented as events with player ID
 * and timestamp, enabling future network replication.
 */
enum class GameEventType {
    NOISE_BLANKER_FIRE,      // Player fired noise blanker
    NOISE_ENEMY_HIT,         // Noise enemy was hit
    NOISE_ENEMY_DESTROYED,   // Noise enemy was destroyed
    QSO_STOERER_HIT,         // QSO Störer was hit
    MORSE_COLLECTED,         // Morse signal collected
    MORSE_MISSED,            // Morse miss event
    POWERUP_COLLECTED,       // Power-up collected
    POWERUP_EXPLODED,        // Power-up shot/exploded
    PA_REPAIRED,             // PA health restored
    PA_DAMAGED,              // PA took damage
    PLAYER_COLLISION,        // Player-player collision
    LAP_COMPLETED            // Player completed a lap
};

/**
 * A timestamped game event with player attribution.
 * Designed for local processing and future network replication.
 */
struct GameEvent {
    GameEventType type;
    int playerIndex = 0;             // Which player caused this event
    float trackAngle = 0.0f;         // World position (radians on track)
    float lateralOffset = 0.0f;      // Lateral position on track
    float value = 0.0f;              // Event-specific value (damage, score, etc.)
    std::chrono::steady_clock::time_point timestamp;
    
    GameEvent() : type(GameEventType::NOISE_BLANKER_FIRE),
                  timestamp(std::chrono::steady_clock::now()) {}
    GameEvent(GameEventType t, int player, float angle, float lateral = 0.0f, float val = 0.0f)
        : type(t), playerIndex(std::max(0, std::min(player, MAX_PLAYERS - 1))),
          trackAngle(angle), lateralOffset(lateral),
          value(val), timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * Screen split orientation for 2 or 3 players.
 */
enum class SplitOrientation {
    HORIZONTAL,  // Side-by-side (left | right)
    VERTICAL     // Top-bottom (top / bottom)
};

/**
 * Input source type for a player.
 */
enum class InputSourceType {
    GAMEPAD,     // Hardware game controller (index 0-3)
    KEYBOARD     // Keyboard emulator
};

/**
 * Audio device descriptor returned by backend enumeration.
 */
struct AudioDeviceInfo {
    int deviceIndex;            // Backend-specific device index
    std::string name;           // Human-readable device name
    int maxOutputChannels;      // Maximum output channels supported
    int defaultSampleRate;      // Preferred sample rate

    AudioDeviceInfo() : deviceIndex(-1), maxOutputChannels(2), defaultSampleRate(44100) {}
};

/**
 * Input assignment for a single player.
 */
struct PlayerInputAssignment {
    InputSourceType type = InputSourceType::KEYBOARD;
    int gamepadIndex = 0;       // Index into IGamepadInput (0-3), only for GAMEPAD type
};

/**
 * Per-player audio parameters — a lightweight copy of the audio state
 * needed to render a complete audio frame from one player's perspective.
 *
 * The game loop computes this for every player each frame, based on the
 * player's individual position, speed, SWR, and proximity to game objects.
 * The audio thread reads this to generate a fully independent audio stream
 * for each player (just like an online game client would).
 */
struct PlayerAudioParams {
    // 6.5: Event timestamp for sample-accurate rendering (shared time basis)
    std::chrono::steady_clock::time_point eventTimestamp{};
    
    float pan = 0.0f;                    // Stereo pan from player's lateral position (-1..+1)
    float motorFreq = 220.0f;           // Motor frequency from player's speed
    float motorVolume = 0.0f;           // Motor volume
    float motorRoughness = 0.0f;        // Road roughness from SWR at player's position
    float swrFreq = 880.0f;             // SWR alert frequency
    float swrVolume = 0.0f;             // SWR alert volume
    bool swrAlertActive = false;        // SWR needs attention
    float swrAlertRate = 0.0f;          // Beep rate for SWR alert
    float reactanceAtPlayer = 0.0f;     // Reactance at this player's position
    float paDamageLevel = 0.0f;         // PA damage for audio distortion

    // Morse cannon state (this player's own keying)
    bool morseCannonActive = false;
    bool morseCannonIsDash = false;

    // Morse signals visible/audible from this player's position
    struct MorseSignalAudio {
        float pan;       // 0..1 stereo position relative to this player
        int volume;      // 0..40
        std::string pattern;
    };
    std::vector<MorseSignalAudio> morseSignals;

    // Noise enemies audible from this player's position
    struct NoiseEnemyAudio {
        float pan;
        int volume;
        float intensity;
    };
    std::vector<NoiseEnemyAudio> noiseEnemies;

    // QSO Störer audio from this player's position
    float qsoStoererPan = 0.5f;
    int qsoStoererVolume = 0;
    bool qsoStoererActive = false;
    float qsoStoererBuzzFreq = 0.0f;
    bool qsoStoererBehind = false;

    // Power-up zones from this player's position
    struct PowerUpAudio {
        float pan;
        int volume;
        bool inZone;
        float zoneDepth;
        int type;  // PowerUpType as int
    };
    std::vector<PowerUpAudio> powerUpZones;

    // Power-up collection progress (both triggers held)
    bool powerUpCollecting = false;
    float powerUpCollectProgress = 0.0f;

    // One-shot sound events (decremented by audio thread)
    int paDamageSoundFrames = 0;
    int paRepairSoundFrames = 0;
    int collectSoundFrames = 0;
    int missAimSoundFrames = 0;
    int missMorseSoundFrames = 0;
    int borderWarningSoundFrames = 0;
    int borderScrapeSoundFrames = 0;
    int borderCrashSoundFrames = 0;
    float borderWarningSide = 0.5f;
    float borderCollisionSide = 0.5f;
    int emergencyBrakeSoundFrames = 0;
    int noiseBlankerFireFrames = 0;
    int noiseHitSoundFrames = 0;
    int noiseHitVariation = 0;           // 0-3 pitch variation for multi-hits
    int noiseDestroyedFrames = 0;
    int aimResetSoundFrames = 0;

    // Tuner feedback sounds (matching singleplayer handleAntennaNetworkInput)
    int adjustSoundFrames = 0;           // Tuner knob adjustment click
    bool adjustSoundUp = true;           // true=ascending, false=descending
    float adjustSoundPan = 0.5f;         // Pan position for adjustment sound
    int bumperSoundFrames = 0;           // Tuner limit hit bumper sound

    // QSO Störer event sounds
    int qsoStoererCollisionFrames = 0;   // Störer collision impact
    int qsoStoererOvertakeFrames = 0;    // Störer overtake sweep sound

    // Band crossing jingle
    int bandJingleFrames = 0;            // Band crossing jingle remaining frames
    bool bandJingleAscending = true;     // true=entering band, false=leaving

    // Traffic report whistle
    int trafficBeepFrames = 0;           // Traffic report whistle before announcement

    // Power-up event sounds
    int powerUpActivateFrames = 0;       // Power-up activation fanfare
    int powerUpActivationType = 0;       // Type of power-up activated
    int powerUpExpireFrames = 0;         // Power-up expiration deflating tone
    int powerUpExplodeFrames = 0;        // Power-up explosion noise burst
    float powerUpExplodePan = 0.5f;      // Pan of power-up explosion
    float powerUpExplodeIntensity = 1.0f; // Intensity of explosion

    // Aim lock strengths (for directional audio cues)
    float aimLockStrength = 0.0f;
    float aimLockMorse = 0.0f;
    float aimLockNoise = 0.0f;
    float aimLockStoerer = 0.0f;
    float aimLockPowerUp = 0.0f;

    // Border warning
    float borderWarningIntensity = 0.0f;
    bool borderWarningActive = false;

    // Per-category volume factors
    float warningVolume = 0.8f;
    float collisionVolume = 0.8f;
    float enemyVolume = 0.8f;
    float uiVolume = 0.7f;

    // Other players' events heard from this player's position
    // (noise blanker fire, power-up collection, hits)
    struct OtherPlayerEventAudio {
        GameEventType type;
        float pan;           // Stereo pan from this player's perspective
        float volume;        // Volume (0..1) based on distance
        float dopplerFactor; // Doppler shift
        int playerIndex;     // Which player caused the event
    };
    std::vector<OtherPlayerEventAudio> otherPlayerEvents;
};

/**
 * Per-player state in multiplayer mode.
 * Each player has independent audio, TTS, antenna tuning, and game state.
 */
struct PlayerContext {
    int playerIndex = 0;                     // 0-based player index
    std::string callsign;                    // Amateur radio callsign
    std::string playerName;                  // Display name

    // Input
    PlayerInputAssignment inputAssignment;

    // Audio output — each player gets their own audio device
    int audioDeviceIndex = -1;               // Selected audio device (-1 = default)
    IAudioBackend* audioBackend = nullptr;    // Per-player audio backend (owned by multiplayer manager)

    // Individual antenna tuning (each player tunes independently for tactical advantage)
    std::unique_ptr<AntennaNetwork> antennaNetwork;

    // Individual game state
    float playerAngle = 0.0f;               // Position on track (0-2π)
    float playerSpeed = 0.0f;               // Current speed (radians/s)
    float maxSpeed = 0.0f;                  // Maximum speed (affected by individual SWR)
    float playerLateralOffset = 0.0f;       // Lateral position (-1..+1)
    float aimAngle = 0.0f;                  // Aiming direction
    float paHealth = 1.0f;                  // PA health (0..1), per-player
    GameStats* stats = nullptr;              // Per-player statistics (owned by MultiplayerManager)

    // Weapon & interaction state for secondary players
    float noiseBlankerCooldown = 0.0f;      // Cooldown between noise blanker shots
    int lapsCompleted = 0;                  // Laps completed by this player
    float lastLapAngle = 0.0f;              // Track angle at last lap crossing
    std::vector<char> collectedChars;       // Morse characters collected by this player
    bool hamSpiritBonusAchieved = false;    // HAMSPIRIT word collected?
    int morseMissCount = 0;                 // Consecutive misses on current target
    bool prevNoiseBlankerBtn = false;       // Edge detection for noise blanker fire

    // Stick click state for per-player L3/R3 handling in multiplayer
    bool prevLeftStickBtn = false;          // Edge detection for emergency brake (L3)
    bool prevRightStickBtn = false;         // Edge detection for aim centering (R3)
    bool aimSyncToHeading = false;          // R3 toggle: weapon tracks vehicle heading
    float emergencyBrakeTimer = 0.0f;       // Remaining emergency brake time (seconds)
    float emergencyBrakeStartSpeed = 0.0f;  // Speed at emergency brake start

    // Per-player border collision state for multiplayer
    float crashRecoveryTime = 0.0f;         // Remaining crash recovery time (seconds)
    float crashVibrationTimer = 0.0f;       // Remaining crash vibration time (seconds)
    bool borderVibrationActive = false;     // True while border proximity vibration is active
    float trackBorderProximity = 0.0f;      // Current lateral proximity to border (0..1+)
    float borderWarningBeepTimer = 0.0f;    // Per-player border warning beep timer (matches singleplayer)

    // Antenna tuner input state for per-player tuner handling in multiplayer
    bool prevTunerY = false;
    bool prevTunerX = false;
    bool prevTunerB = false;
    bool prevTunerA = false;
    float tunerHoldTimerY = 0.0f;
    float tunerHoldTimerX = 0.0f;
    float tunerHoldTimerB = 0.0f;
    float tunerHoldTimerA = 0.0f;
    bool prevTunerCheat = false;
    // D-pad input state for per-player D-pad handling in multiplayer
    bool prevDpadUp = false;
    bool prevDpadDown = false;
    bool prevDpadLeft = false;
    bool prevDpadRight = false;

    // Per-player band crossing tracking
    std::string currentBandName;            // Current band for this player (for band crossing jingle)

    // Pair-specific collision cooldowns (indexed by other player index)
    float pairCollisionCooldown[MAX_PLAYERS] = {};

    // Collision state
    float collisionCooldown = 0.0f;         // Cooldown after collision (seconds)
    float collisionSpeedDelta = 0.0f;       // Speed change from last collision
    float collisionLateralPush = 0.0f;      // Lateral push from last collision
    int   collisionSoundFrames = 0;         // Remaining frames of collision sound
    float collisionIntensity = 0.0f;        // 0..1 intensity of last collision (for sound volume)
    float collisionPan = 0.0f;              // Pan of last collision (-1..+1)

    // Braille context
    bool hasBrailleContext = false;          // Only one player gets braille output

    // Spatial position for inter-player audio
    float trackPositionMeters = 0.0f;       // Linear position in meters (for Doppler)
    float lateralPositionMeters = 0.0f;     // Lateral offset in meters

    // Morse cannon state (for spatial inter-player audio)
    bool morseCannonActive = false;         // True while this player's Morse paddle is keyed
    bool morseCannonIsDash = false;         // True if current keying is a dash
    // 6.5: Timestamp of the last morse cannon state change (for latency tracking)
    std::chrono::steady_clock::time_point morseCannonTimestamp{};

    // SWR roughness — visible to other players for spatial motor sound
    float currentMotorRoughness = 0.0f;    // Road roughness from SWR (0=smooth, 1=rough)

    // Per-player audio parameters — computed each frame by the game loop
    // from this player's perspective (position, speed, proximity to objects).
    // Read by the audio thread to generate an independent audio stream.
    PlayerAudioParams audioState;
    std::mutex audioStateMtx;                // Protects audioState read/write

    // Per-player audio rendering state (owned by audio thread, not shared)
    std::vector<int16_t> audioBuf;           // Scratch buffer for audio rendering
    std::vector<int16_t> mixBuf;             // Scratch buffer for mixing
    float roughnessPhase = 0.0f;             // Motor roughness oscillator
    float rattlePhase = 0.0f;                // Motor rattle oscillator
    float morsePatternPhase = 0.0f;          // Morse signal pattern timing
    float smoothedPan = 0.0f;                // Smoothed pan for anti-crackle
    float swrAlertPhase = 0.0f;              // SWR alert beep phase
    float morseCannonPhase = 0.0f;           // Phase accumulator for this player's morse sidetone
    float enginePhases[MAX_PLAYERS] = {};    // Phase accumulators for other players' engine sounds
    float morsePhases[MAX_PLAYERS] = {};     // Phase accumulators for other players' morse sidetones
    int audioFrameCount = 0;                 // Audio frame counter (matches Player 0's frameCount for noise freq variation)
    int puZonePhase = 0;                     // Power-up zone arpeggio phase counter (matches Player 0's puZonePhase)

    PlayerContext() = default;
    ~PlayerContext();
    PlayerContext(PlayerContext&&) noexcept;
    PlayerContext& operator=(PlayerContext&&) noexcept;

    // Non-copyable (owns unique_ptrs and mutexes)
    PlayerContext(const PlayerContext&) = delete;
    PlayerContext& operator=(const PlayerContext&) = delete;
};

/**
 * Spatial audio relationship between two players.
 * Used to calculate how player B perceives sounds from player A.
 */
struct SpatialRelation {
    float distance = 0.0f;          // Distance in meters between the two players
    float bearing = 0.0f;           // Angle from listener to source (-π to +π, 0=ahead)
    float relativeSpeed = 0.0f;     // Speed of source relative to listener (m/s, positive=approaching)
    float pan = 0.0f;               // Stereo pan (-1=left, 0=center, +1=right)
    float volume = 1.0f;            // Volume attenuation from distance (0..1)
    float dopplerFactor = 1.0f;     // Doppler frequency multiplier
};

/**
 * Doppler effect and 3D spatial audio processor.
 *
 * Implements realistic audio physics:
 *  - Distance-based volume attenuation (inverse-square law)
 *  - Stereo panning based on relative bearing
 *  - Doppler frequency shift based on relative velocity
 *
 * Speed of sound: 343 m/s (at 20°C, sea level)
 */
class SpatialPlayerAudio {
public:
    SpatialPlayerAudio();

    /**
     * Calculate the spatial relationship from listener to source.
     * @param listenerAngle    Listener position on track (radians)
     * @param listenerLateral  Listener lateral offset (-1..+1)
     * @param listenerSpeed    Listener speed (radians/s)
     * @param sourceAngle      Source position on track (radians)
     * @param sourceLateral    Source lateral offset (-1..+1)
     * @param sourceSpeed      Source speed (radians/s)
     * @param trackRadiusMeters  Radius of circular track (meters)
     * @return Spatial relationship
     */
    SpatialRelation calculate(
        float listenerAngle, float listenerLateral, float listenerSpeed,
        float sourceAngle,   float sourceLateral,   float sourceSpeed,
        float trackRadiusMeters) const;

    /**
     * Apply Doppler effect to a frequency.
     * @param sourceFreqHz   Original frequency in Hz
     * @param dopplerFactor  Factor from SpatialRelation::dopplerFactor
     * @return Shifted frequency in Hz
     */
    static float applyDoppler(float sourceFreqHz, float dopplerFactor);

    /**
     * Apply spatial audio (volume + pan + Doppler) to PCM audio buffer.
     * Modifies the buffer in-place for stereo (2 channels).
     * @param buffer          Interleaved stereo PCM buffer
     * @param sampleCount     Number of samples per channel
     * @param relation        Spatial relationship from calculate()
     * @param sourceFreqHz    Base frequency of the source (for Doppler pitch shift)
     */
    void applySpatialEffect(
        int16_t* buffer,
        int sampleCount,
        const SpatialRelation& relation) const;

    // Audio physics constants
    static constexpr float SPEED_OF_SOUND = 343.0f;        // m/s at 20°C
    static constexpr float MIN_DISTANCE = 0.5f;             // Minimum distance (meters)
    static constexpr float MAX_AUDIBLE_DISTANCE = 500.0f;   // Beyond this, volume = 0
    static constexpr float REFERENCE_DISTANCE = 1.0f;       // Distance at which volume = 1.0
    static constexpr float TRACK_WIDTH_METERS = 10.0f;      // Width of the race track
};

/**
 * Multiplayer configuration set during the multiplayer setup menu.
 */
struct MultiplayerConfig {
    int playerCount = 1;                     // 1 = singleplayer, 2-4 = multiplayer
    SplitOrientation splitOrientation = SplitOrientation::HORIZONTAL;

    // Per-player assignments
    PlayerInputAssignment inputAssignments[MAX_PLAYERS];
    int audioDeviceIndices[MAX_PLAYERS] = {-1, -1, -1, -1};  // -1 = default
    int controllerPresets[MAX_PLAYERS] = {0, 0, 0, 0};       // Per-player: 0=Auto, 1=Xbox, 2=PS
    int braillePlayerIndex = 0;              // Which player gets braille output

    // Per-player identification (callsign + name)
    std::string playerCallsigns[MAX_PLAYERS];
    std::string playerNames[MAX_PLAYERS];

    bool isMultiplayer() const { return playerCount > 1; }
};

/**
 * TTS multi-output capability check.
 * Returns true if the given TTS engine type supports simultaneous output
 * to multiple audio devices (required for independent per-player TTS).
 */
bool ttsSupportsMultiOutput(int ttsEngineType);

/**
 * Multiplayer manager — orchestrates per-player contexts, spatial audio,
 * screen splitting, and shared game state.
 */
class MultiplayerManager {
public:
    MultiplayerManager();
    ~MultiplayerManager();

    /**
     * Initialize multiplayer with the given configuration.
     * Creates per-player audio backends, TTS engines, and antenna networks.
     * @param config  Multiplayer configuration from setup menu
     * @param track   Shared track data
     * @return true on success
     */
    bool initialize(const MultiplayerConfig& config,
                    const std::vector<TrackPoint>& track);

    /**
     * Shutdown all player contexts and release resources.
     */
    void shutdown();

    /**
     * Get the multiplayer configuration.
     */
    const MultiplayerConfig& getConfig() const { return config; }

    /**
     * Get a specific player context.
     * @param index Player index (0-based)
     * @return Pointer to player context, or nullptr if invalid
     */
    PlayerContext* getPlayer(int index);
    const PlayerContext* getPlayer(int index) const;

    /**
     * Get number of active players.
     */
    int getPlayerCount() const { return config.playerCount; }

    /**
     * Check if multiplayer mode is active.
     */
    bool isMultiplayer() const { return config.isMultiplayer(); }

    /**
     * Calculate spatial relationship between two players.
     * @param listenerIndex  Index of the listening player
     * @param sourceIndex    Index of the source player
     * @return Spatial relationship (distance, pan, Doppler, etc.)
     */
    SpatialRelation calculateSpatialRelation(int listenerIndex, int sourceIndex) const;

    /**
     * Update all inter-player spatial audio for a given frame.
     * Call once per game update tick.
     * @param trackRadiusMeters  Track radius in meters
     */
    void updateSpatialAudio(float trackRadiusMeters);

    /**
     * Get the split screen viewport for a player.
     * Returns normalized coordinates (0..1) for the player's viewport.
     * @param playerIndex  Player index
     * @param outX, outY   Top-left corner (0..1)
     * @param outW, outH   Size (0..1)
     */
    void getPlayerViewport(int playerIndex,
                           float& outX, float& outY,
                           float& outW, float& outH) const;

    /**
     * Enumerate available audio output devices.
     * @return Vector of audio device descriptors
     */
    static std::vector<AudioDeviceInfo> enumerateAudioDevices();

    /**
     * Check if enough audio devices are available for multiplayer.
     * @param requiredCount Number of distinct audio outputs needed
     * @return true if enough devices are available
     */
    static bool hasEnoughAudioDevices(int requiredCount);

    /**
     * Result of a collision check between two players.
     */
    struct CollisionResult {
        bool collided = false;          // True if a collision occurred
        float impactSpeed = 0.0f;       // Relative speed at impact (m/s)
        float impactAngle = 0.0f;       // Angle of impact (radians, 0=head-on)
        float damage = 0.0f;            // Damage factor (0..1)
        float pushAngleA = 0.0f;        // Lateral push for player A (-1..+1)
        float pushAngleB = 0.0f;        // Lateral push for player B (-1..+1)
        float speedDeltaA = 0.0f;       // Speed change for player A (rad/s)
        float speedDeltaB = 0.0f;       // Speed change for player B (rad/s)
        float pan = 0.0f;               // Pan position of collision sound
        float intensity = 0.0f;         // Collision sound intensity (0..1)
    };

    /**
     * Check for and resolve collisions between all player pairs.
     * Updates player positions, speeds, and collision state.
     * @param trackRadiusMeters  Track radius in meters
     * @param dt                  Delta time (seconds)
     * @return Vector of collision results (one per colliding pair)
     */
    std::vector<CollisionResult> checkPlayerCollisions(float trackRadiusMeters, float dt);

    /**
     * Generate a complete audio frame for one secondary player (1+).
     *
     * Unlike the previous approach of copying player 0's audio and overlaying
     * spatial effects, this renders a fully independent audio pipeline from the
     * given player's perspective — identical in structure to player 0's audio
     * generation, but using the player's own position, speed, SWR, etc.
     *
     * Call once per audio frame for each player 1..N-1.
     * Player 0's audio is rendered by the existing Game::audioThreadFunc().
     *
     * @param playerIndex         Player to render for (1..playerCount-1)
     * @param audioEngine         Synthesizer engine for waveform generation
     * @param sampleRate          Sample rate (Hz)
     * @param samplesPerFrame     Samples per channel per frame
     * @param channels            Number of audio channels (2 for stereo)
     * @param bitsPerSample       Bits per sample (16)
     * @param sharedMorsePhase    Shared global morse pattern phase (seconds)
     * @param frameStartTime      Audio frame start timestamp for sample-accurate timing
     */
    void generateAndPlayPlayerAudio(int playerIndex,
                                     SynthesizerEngine* audioEngine,
                                     int sampleRate, int samplesPerFrame,
                                     int channels, int bitsPerSample,
                                     float sharedMorsePhase = 0.0f,
                                     std::chrono::steady_clock::time_point frameStartTime = {},
                                     bool isPlaying = true);

    /**
     * Distribute audio to all player backends.
     * For player 0: the caller already renders and plays audio.
     * For players 1+: generates a fully independent audio stream from each
     * player's own perspective (their own AudioParams), then plays it to
     * that player's audio backend.
     * @param audioEngine         Synthesizer engine for waveform generation
     * @param sampleRate          Sample rate (Hz)
     * @param samplesPerFrame     Samples per channel per frame
     * @param channels            Number of audio channels
     * @param bitsPerSample       Bits per sample
     * @param sharedMorsePhase    Shared global morse pattern phase (seconds)
     * @param frameStartTime      Audio frame start timestamp for sample-accurate timing
     */
    void renderAllPlayerAudio(SynthesizerEngine* audioEngine,
                              int sampleRate, int samplesPerFrame,
                              int channels, int bitsPerSample,
                              float sharedMorsePhase = 0.0f,
                              std::chrono::steady_clock::time_point frameStartTime = {},
                              bool isPlaying = true);

    /**
     * Legacy: Distribute player 0's audio to all player backends.
     * @deprecated Use renderAllPlayerAudio() instead for per-player audio.
     * Kept for non-gameplay states (menus, title) where all players hear
     * the same audio.
     */
    void distributeAudioToPlayers(const int16_t* buffer, int samples,
                                   int sampleRate, int channels, int bitsPerSample);

    /**
     * Set a Logger instance for debug output on this manager and all owned backends.
     * @param logger Pointer to Logger (may be null)
     */
    void setLogger(Logger* logger);

    /**
     * Push a game event into the global event queue.
     * Events are consumed by computePlayerAudioParams to generate spatial audio
     * for other players. Thread-safe.
     * @param event  The game event to push
     */
    void pushEvent(const GameEvent& event);

    /**
     * Get and clear all pending events since last call.
     * @return Vector of recent game events
     */
    std::vector<GameEvent> consumeEvents();

    /**
     * Get a reference to the per-player GameStats for a player.
     * @param playerIndex  Player index (0-based)
     * @return Pointer to GameStats, or nullptr if invalid
     */
    GameStats* getPlayerStats(int playerIndex);

    // Collision physics constants
    static constexpr float COLLISION_DISTANCE = 3.0f;      // Collision threshold (meters)
    static constexpr float COLLISION_LATERAL_WIDTH = 1.5f;  // Lateral collision width (meters)
    static constexpr float COLLISION_COOLDOWN = 1.0f;       // Seconds between collisions
    static constexpr float COLLISION_ELASTICITY = 0.6f;     // Coefficient of restitution (0=inelastic, 1=elastic)
    static constexpr float COLLISION_DAMAGE_FACTOR = 0.05f; // PA damage per unit of impact speed
    static constexpr float COLLISION_PUSH_FACTOR = 0.3f;    // Lateral push strength

private:
    MultiplayerConfig config;
    std::vector<PlayerContext> players;
    std::vector<std::unique_ptr<IAudioBackend>> ownedBackends;
    std::unique_ptr<SpatialPlayerAudio> spatialAudio;

    // Per-player GameStats storage (owned here, pointers given to PlayerContext)
    std::vector<std::unique_ptr<GameStats>> playerStats;

    // Global event queue for multiplayer event propagation
    std::deque<GameEvent> eventQueue;
    std::mutex eventMutex;
    static constexpr size_t MAX_EVENT_QUEUE_SIZE = 256;

    // Cached spatial relations (listenerIdx * MAX_PLAYERS + sourceIdx)
    SpatialRelation spatialRelations[MAX_PLAYERS * MAX_PLAYERS];
    float trackRadiusM = 100.0f;  // Default track radius

    mutable std::mutex spatialMutex;
    Logger* debugLogger = nullptr;
};

} // namespace HamSpirit

#endif // WITH_HAM_SPIRIT
