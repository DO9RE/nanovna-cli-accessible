/**
 * @file game_authority.h
 * @brief Central authoritative game instance for server-authoritative architecture.
 *
 * The GameAuthority is the single source of truth for all game state.
 * It manages the world state, validates player actions, generates events,
 * and distributes them to relevant players based on proximity and priority.
 *
 * Design principles:
 * - No player has special privileges (no Player0 logic)
 * - All game state changes go through GameAuthority
 * - Players are client entities that send ActionRequests and receive WorldEvents
 * - Architecture is offline-first but structurally online-ready
 * - Communication uses a logical transport layer (EventQueue/MessageDispatcher)
 *
 * @see doc/hamspirit_multiplayer_architecture.md
 */

#ifndef GAME_AUTHORITY_H
#define GAME_AUTHORITY_H

#ifdef WITH_HAM_SPIRIT

#include "hamspirit_game.h"

#include <vector>
#include <deque>
#include <string>
#include <chrono>
#include <mutex>
#include <functional>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace HamSpirit {

// ============================================================================
// Communication Types: Client → Server
// ============================================================================

/**
 * Types of actions a player client can request from the server.
 */
enum class PlayerActionType {
    // Movement
    MOVE_UPDATE,                ///< Position/speed update from client (legacy, for transition)
    INPUT_UPDATE,               ///< Raw controller input from client (authority computes physics)
    
    // Weapons
    NOISE_BLANKER_FIRE,         ///< Fire noise blanker weapon
    MORSE_CANNON_FIRE,          ///< Send morse character
    
    // Interaction
    POWERUP_COLLECT_START,      ///< Begin power-up collection (dual-trigger)
    POWERUP_COLLECT_PROGRESS,   ///< Collection progress update
    POWERUP_COLLECT_CANCEL,     ///< Cancel collection
    
    // State
    ANTENNA_TUNE,               ///< Antenna tuning adjustment (L/C values)
};

/**
 * A player action request sent from client to server.
 * Clients never modify world state directly — they send action requests.
 */
struct PlayerAction {
    PlayerActionType type;
    int playerId = -1;
    std::chrono::steady_clock::time_point timestamp;
    
    // Action-specific data
    float angle = 0.0f;             ///< Player position on track (radians)
    float lateralOffset = 0.0f;     ///< Lateral position (-1..+1)
    float speed = 0.0f;             ///< Current speed
    float aimAngle = 0.0f;          ///< Aim direction (for weapons)
    float value = 0.0f;             ///< Action-specific value
    char morseChar = '\0';          ///< Sent morse character (for MORSE_CANNON_FIRE)
    
    // Raw controller input (for INPUT_UPDATE — authority computes physics)
    float forwardInput = 0.0f;      ///< LEFT_Y axis: -1 (back/brake) to +1 (forward/accel)
    float steerInput = 0.0f;        ///< LEFT_X axis: -1 (left) to +1 (right)
    float aimInput = 0.0f;          ///< RIGHT_Y axis: aim direction input
};

// ============================================================================
// Communication Types: Server → Client
// ============================================================================

/**
 * Types of events the server generates and distributes to clients.
 */
enum class WorldEventType {
    // Weapon events
    NOISE_BLANKER_FIRED,        ///< Player fired noise blanker
    NOISE_ENEMY_HIT,            ///< Noise enemy was hit
    NOISE_ENEMY_DESTROYED,      ///< Noise enemy was destroyed
    QSO_STOERER_HIT,            ///< QSO Störer was hit
    
    // Collection events
    MORSE_COLLECTED,            ///< Morse character collected
    MORSE_EXPIRED,              ///< Morse character expired (timeout)
    POWERUP_COLLECTED,          ///< Power-up collected
    POWERUP_ACTIVATED,          ///< Power-up effect activated
    POWERUP_EXPIRED,            ///< Power-up effect expired
    POWERUP_DESTROYED,          ///< Power-up destroyed (shot)
    
    // State changes
    PA_DAMAGED,                 ///< PA took damage (SWR)
    PA_REPAIRED,                ///< PA was healed (morse collection)
    PA_DESTROYED,               ///< PA health reached 0 (game over for player)
    PLAYER_COLLISION,           ///< Player-player collision
    LAP_COMPLETED,              ///< Player completed a lap
    BORDER_COLLISION,           ///< Player touched track border
    
    // Spawn events
    ENTITY_SPAWNED,             ///< New entity created (morse, noise, powerup)
    ENTITY_REMOVED,             ///< Entity removed (lifecycle)
    
    // Game events
    GAME_STARTED,               ///< Game has started
    GAME_OVER,                  ///< Game ended (for one or all players)
    HAMSPIRIT_BONUS,            ///< Player achieved HAMSPIRIT bonus
    
    // NPC events
    QSO_STOERER_TARGET_CHANGED, ///< QSO Störer changed target
    QSO_STOERER_APPROACHING,    ///< QSO Störer approaching a player
    
    // Score events
    SCORE_CHANGED,              ///< Player score changed
};

/**
 * Priority levels for event delivery.
 */
enum class EventPriority {
    LOW,        ///< Can be dropped under load
    NORMAL,     ///< Standard delivery
    HIGH,       ///< Always delivered (hits, collisions)
    CRITICAL    ///< Immediately delivered (game over, PA destruction)
};

/**
 * A world event generated by the server and distributed to clients.
 * Clients render events (audio, visual, haptic) but never generate world events.
 */
struct WorldEvent {
    WorldEventType type;
    int sourcePlayerId = -1;         ///< Player who caused event (-1 = world/system)
    float trackAngle = 0.0f;        ///< Position on track (radians)
    float lateralOffset = 0.0f;     ///< Lateral position (-1..+1)
    float value = 0.0f;             ///< Event-specific value (score, damage, etc.)
    uint32_t entityId = 0;          ///< Affected entity ID (if relevant)
    char morseChar = '\0';          ///< Morse character (for MORSE_COLLECTED)
    std::chrono::steady_clock::time_point timestamp;
    EventPriority priority = EventPriority::NORMAL;
};

// ============================================================================
// Action Response
// ============================================================================

/**
 * Reasons an action might be rejected by the server.
 */
enum class ActionRejectReason {
    NONE,                       ///< Action accepted
    COOLDOWN_ACTIVE,            ///< Weapon still on cooldown
    OUT_OF_RANGE,               ///< Target out of range
    ENTITY_ALREADY_CLAIMED,     ///< Entity already claimed by another player
    PLAYER_DEAD,                ///< Player PA destroyed
    INVALID_STATE,              ///< Invalid game state
};

/**
 * Result of processing a player action.
 */
struct ActionResult {
    bool accepted = false;
    ActionRejectReason reason = ActionRejectReason::NONE;
    float cooldownRemaining = 0.0f;
    std::vector<WorldEvent> immediateEvents;    ///< Events to deliver immediately
};

// ============================================================================
// World State Structures
// ============================================================================

/**
 * Information needed to register a player with the authority.
 */
struct PlayerInfo {
    std::string callsign;
    std::string playerName;
    int inputAssignmentIndex = 0;
    int audioDeviceIndex = -1;
};

/**
 * Per-player authoritative state maintained by the server.
 * This is the server's view of each player — clients have their own local state.
 */
struct PlayerWorldState {
    int playerId = -1;
    float playerAngle = 0.0f;
    float playerSpeed = 0.0f;
    float playerLateralOffset = 0.0f;
    float maxSpeed = 0.0f;
    float baseMaxSpeed = 0.0f;
    float aimAngle = 0.0f;
    float paHealth = 1.0f;
    float noiseBlankerCooldown = 0.0f;
    int lapsCompleted = 0;
    float previousAngle = 0.0f;      ///< For lap detection
    std::vector<char> collectedChars;
    bool hamSpiritBonusAchieved = false;
    std::vector<ActivePowerUp> activePowerUps;
    GameStats stats;
    bool alive = true;                ///< False if PA destroyed
    
    // Physics state (computed by authority, not client)
    float smoothedForwardInput = 0.0f;  ///< Low-pass filtered throttle input
    float lastForwardInput = 0.0f;      ///< Raw input from last INPUT_UPDATE
    float lastSteerInput = 0.0f;        ///< Raw steer input from last INPUT_UPDATE
    float lastAimInput = 0.0f;          ///< Raw aim input from last INPUT_UPDATE
    
    // Antenna / SWR state (computed by authority from client-reported SWR)
    float adjustedSWR = 1.0f;           ///< Current SWR after antenna tuning (client reports)
    float paThermalLoad = 0.0f;         ///< Thermal load on PA (authority computes)
    float paReflectedPowerAccum = 0.0f; ///< Accumulated reflected power
    float paDamageSpeedMultiplier = 1.0f; ///< Speed reduction from PA damage
    int lastAnnouncedDamageStage = 0;   ///< For staged damage announcements
    bool swrImmunityActive = false;     ///< SWR immunity power-up active
    
    // Power-up collection state
    float powerUpCollectionProgress = 0.0f;
    int collectingPowerUpIndex = -1;
};

/**
 * Complete world state maintained by the authority.
 * This represents the single source of truth for the game world.
 */
struct WorldState {
    std::vector<TrackPoint> track;
    std::vector<MorseSignal> morseSignals;
    std::vector<NoiseEnemy> noiseEnemies;
    std::vector<PowerUp> powerUps;
    QSOStoerer* qsoStoerer = nullptr;   ///< Owned by GameAuthority
    float gameTime = 0.0f;
    bool gameActive = false;
};

// ============================================================================
// GameAuthority: Central Authoritative Instance
// ============================================================================

/**
 * The GameAuthority is the central authoritative instance that manages
 * all game state. It acts as the "server" in a client-server architecture.
 *
 * In offline mode, it runs in the same process as the clients (direct method calls).
 * The architecture is designed so the transport layer can be swapped to
 * network sockets, RPC, WebSocket, or UDP without changing game logic.
 *
 * Key responsibilities:
 * - Owns the complete world state (entities, track, game time)
 * - Validates and processes all player actions
 * - Generates and distributes world events
 * - Manages entity spawning relative to ALL players
 * - Runs the simulation tick
 * - Calculates event relevance per player
 * - Resolves conflicts deterministically
 * - Treats all players identically (no Player0 special logic)
 */
class GameAuthority {
public:
    GameAuthority();
    ~GameAuthority();
    
    // ---- Lifecycle ----
    
    /**
     * Initialize the authority with track data and game configuration.
     * Must be called before any other method.
     */
    void initialize(const std::vector<TrackPoint>& track, const GameConfig& config);
    
    /**
     * Shut down the authority, clearing all state.
     */
    void shutdown();
    
    /**
     * Check if the authority is initialized and active.
     */
    bool isActive() const { return worldState_.gameActive; }
    
    // ---- Player Management ----
    
    /**
     * Register a player with the authority.
     * All players are treated identically — no special Player0 logic.
     * @return The assigned player ID (0-based, sequential)
     */
    int registerPlayer(const PlayerInfo& info);
    
    /**
     * Unregister a player from the authority.
     */
    void unregisterPlayer(int playerId);
    
    /**
     * Get the number of registered players.
     */
    int getPlayerCount() const;
    
    /**
     * Get the authoritative state for a specific player.
     */
    const PlayerWorldState& getPlayerState(int playerId) const;
    
    /**
     * Get mutable player state (for internal use / authority updates).
     */
    PlayerWorldState& getPlayerStateMutable(int playerId);
    
    /**
     * Get per-player game statistics.
     */
    const GameStats& getPlayerStats(int playerId) const;
    
    // ---- Simulation Tick ----
    
    /**
     * Execute one simulation tick. This is the deterministic update cycle.
     * Called once per frame with fixed or variable dt.
     *
     * During a tick, the authority:
     * 1. Updates entity lifecycles (expiry, removal)
     * 2. Runs spawn management (morse signals, noise enemies, power-ups)
     * 3. Updates NPC behavior (QSO Störer)
     * 4. Checks PA health for all players
     * 5. Checks lap completion for all players
     * 6. Generates world events for all state changes
     */
    void tick(float dt);
    
    // ---- Action Processing ----
    
    /**
     * Process a player action request.
     * The authority validates the action and generates appropriate events.
     * This is the ONLY way clients can affect the world state.
     *
     * @param action The action request from a client
     * @return Result indicating acceptance/rejection and any immediate events
     */
    ActionResult processAction(const PlayerAction& action);
    
    // ---- Event Distribution ----
    
    /**
     * Get pending events for a specific player.
     * Events are filtered by relevance (distance, priority, context).
     * After retrieval, events are consumed (not returned again).
     *
     * @param playerId The player to get events for
     * @return Vector of relevant world events
     */
    std::vector<WorldEvent> getEventsForPlayer(int playerId);
    
    /**
     * Get all pending events (for debug/replay purposes).
     */
    std::vector<WorldEvent> getAllEvents();
    
    // ---- World State Access ----
    
    /**
     * Get read-only access to the world state.
     * Clients should use events, not poll the world state directly.
     * This is primarily for the Game class during the transition period.
     */
    const WorldState& getWorldState() const { return worldState_; }
    
    /**
     * Get mutable world state (for authority-internal operations).
     */
    WorldState& getWorldStateMutable() { return worldState_; }
    
    // ---- Relevance Configuration ----
    
    /** Maximum distance (in track meters) at which events are audible */
    static constexpr float MAX_AUDIBLE_DISTANCE = 500.0f;
    
    /** Maximum pending events before oldest are dropped */
    static constexpr int MAX_PENDING_EVENTS = 512;
    
    /** Track circumference factor for distance calculation */
    static constexpr float TRACK_RADIUS_METERS = 159.15f; // ~1000m circumference
    
    // ---- Spawn Management ----
    
    /**
     * Spawn morse signals relative to ALL registered players.
     * Called internally during tick().
     */
    void spawnMorseSignals(float gameTime);
    
    /**
     * Spawn noise enemies relative to ALL registered players.
     */
    void spawnNoiseEnemy(float gameTime);
    
    /**
     * Spawn power-ups relative to ALL registered players.
     */
    void spawnPowerUp();
    
    // ---- Interaction Validation ----
    
    /**
     * Process a noise blanker fire action for a specific player.
     * Validates cooldown, calculates hits, updates entities, generates events.
     */
    ActionResult processNoiseBlankerFire(int playerId, float playerAngle, float aimAngle);
    
    /**
     * Process a morse collection request for a specific player.
     * Validates proximity, checks if signal is already claimed, updates score.
     */
    ActionResult processMorseCollection(int playerId, float playerAngle, char sentChar);
    
    /**
     * Process a power-up collection for a specific player.
     * Validates position, checks if power-up is already claimed.
     */
    ActionResult processPowerUpCollection(int playerId, float playerAngle, 
                                           float lateralOffset, float progress);
    
    // ---- NPC Management ----
    
    /**
     * Update QSO Störer behavior. The Störer pursues the nearest player
     * (not just Player 0 as in the old architecture).
     */
    void updateQSOStoerer(float dt);
    
    // ---- PA Health ----
    
    /**
     * Update PA health for a specific player based on SWR.
     * Called for ALL players during tick().
     */
    void updatePAHealth(int playerId, float currentSWR, float dt);
    
    // ---- Lap Detection ----
    
    /**
     * Check if a player has completed a lap based on angle change.
     */
    void checkLapCompletion(int playerId, float newAngle, float oldAngle);
    
    // ---- Event Generation ----
    
    /**
     * Generate and enqueue a world event.
     * Automatically sets timestamp and assigns to per-player queues.
     */
    void emitEvent(WorldEvent event);
    
    // ---- Utility ----
    
    /**
     * Calculate the arc distance between two track angles (in meters).
     */
    static float calculateTrackDistance(float angle1, float angle2);
    
    /**
     * Check if an event is relevant to a player based on distance and priority.
     */
    bool isEventRelevantToPlayer(const WorldEvent& event, int playerId) const;
    
    /**
     * Update the position of a player (server-side tracking).
     * In offline mode, the client is authoritative for its own position.
     */
    void updatePlayerPosition(int playerId, float newAngle, float newSpeed,
                               float newLateralOffset, float newAimAngle);

    /**
     * Apply physics simulation for a single player based on raw input.
     * Called during tick() for ALL players — computes speed, position, steering.
     * This is the ONLY place where player movement is calculated.
     * All players use identical physics regardless of input source.
     */
    void applyPlayerPhysics(int playerId, float dt);

private:
    // ---- World State ----
    WorldState worldState_;
    QSOStoerer ownedQsoStoerer_;      ///< Actual QSOStoerer storage
    std::vector<PlayerWorldState> players_;
    
    // ---- Event Queues ----
    std::deque<WorldEvent> pendingEvents_;
    std::vector<std::deque<WorldEvent>> playerEventQueues_;
    std::mutex eventMutex_;
    
    // ---- Configuration ----
    GameConfig const* config_ = nullptr;
    bool initialized_ = false;
    
    // ---- Spawn Timing ----
    float lastMorseSpawnTime_ = 0.0f;
    float lastNoiseSpawnTime_ = 0.0f;
    float lastPowerUpSpawnTime_ = 0.0f;
    uint32_t nextEntityId_ = 1;
    
    // ---- Internal Helpers ----
    
    /**
     * Remove expired entities from all entity lists.
     */
    void updateEntityLifecycles(float dt);
    
    /**
     * Update active power-up effects for a player.
     */
    void updateActivePowerUps(int playerId, float dt);
    
    /**
     * Find the nearest player to a given track position.
     * Used by QSO Störer targeting (replaces Player0-only targeting).
     */
    int findNearestPlayer(float trackAngle) const;
    
    /**
     * Get all player positions (for spawn calculation).
     */
    std::vector<float> getAllPlayerPositions() const;
    
    /**
     * Dispatch an event to relevant player queues based on relevance.
     */
    void dispatchEventToPlayers(const WorldEvent& event);
};

} // namespace HamSpirit

#endif // WITH_HAM_SPIRIT

#endif // GAME_AUTHORITY_H
