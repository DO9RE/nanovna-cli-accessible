/**
 * @file game_authority.cpp
 * @brief Implementation of the central authoritative game instance.
 *
 * The GameAuthority manages all game state, validates player actions,
 * and distributes events. It treats all players identically with no
 * Player0 special logic.
 *
 * @see game_authority.h
 * @see doc/hamspirit_multiplayer_architecture.md
 */

#ifdef WITH_HAM_SPIRIT

#include "game_authority.h"
#include "hamspirit_game.h"

#include <algorithm>
#include <cmath>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr float TWO_PI = 2.0f * static_cast<float>(M_PI);

// ============================================================================
// Game Balance Constants
// ============================================================================

// Weapon constants
static constexpr float HIT_CONE_RADIANS = 0.15f;           ///< ~8.6 degrees aim cone for hits
static constexpr float NOISE_BLANKER_COOLDOWN_SECS = 0.5f;  ///< Normal cooldown between shots
static constexpr float FIRE_RATE_POWERUP_COOLDOWN = 0.15f;  ///< Reduced cooldown with fire-rate power-up

// Score constants — "73" is ham radio tradition meaning "best regards"
static constexpr int MORSE_COLLECTION_SCORE = 73;
static constexpr int NOISE_ENEMY_DESTROY_SCORE = 50;
static constexpr int POWERUP_COLLECTION_SCORE = 25;
static constexpr int HAMSPIRIT_BONUS_SCORE = 500;
static constexpr int LAP_COMPLETION_SCORE = 100;

// PA health constants
static constexpr float PA_REPAIR_PER_MORSE = 0.05f;         ///< Health restored per morse collection
static constexpr float SWR_DAMAGE_THRESHOLD = 3.0f;         ///< SWR above this causes damage
static constexpr float SWR_DAMAGE_RATE = 0.01f;             ///< Damage rate per SWR unit above threshold

// QSO Störer constants
static constexpr float QSO_STOERER_DAMAGE_PER_HIT = 0.3f;   ///< Health reduction per hit
static constexpr float QSO_STOERER_MIN_HEALTH = 0.1f;       ///< Minimum health (can't be fully destroyed)
static constexpr float QSO_STOERER_RECOVERY_RATE = 0.05f;   ///< Health recovery per second

// Power-up constants
static constexpr float DEFAULT_POWERUP_DURATION_SECS = 20.0f;

// Morse collection constants
static constexpr float MAX_MORSE_COLLECTION_DISTANCE_FACTOR = 0.5f;  ///< ~30 degrees proximity

// Lap detection constants (detect wrap-around near 2π)
static constexpr float LAP_DETECTION_OLD_ANGLE_MIN = 5.0f;  ///< Old angle must be > this
static constexpr float LAP_DETECTION_NEW_ANGLE_MAX = 1.0f;  ///< New angle must be < this

// Physics constants (unified for ALL players — no Player0 exceptions)
static constexpr float STANDSTILL_THRESHOLD = 0.005f;       ///< Speed below this = standing still
static constexpr float DEFAULT_INPUT_DEADZONE = 0.15f;      ///< Stick deadzone
static constexpr float DEFAULT_ACCEL_SENSITIVITY = 1.0f;    ///< Acceleration sensitivity multiplier
static constexpr float DEFAULT_ACCELERATION = 0.06f;        ///< 6% of maxSpeed per second at full throttle
static constexpr float STEERING_SPEED = 2.0f;               ///< Lateral movement speed
static constexpr float DEFAULT_STEERING_SENSITIVITY = 1.0f; ///< Steering sensitivity multiplier
static constexpr float AIM_SENSITIVITY = 2.0f;              ///< Aim rotation speed (rad/s at full stick)
static constexpr float THROTTLE_SMOOTH_RATE = 80.0f;        ///< Low-pass filter rate (~12ms time constant)

namespace HamSpirit {

// ============================================================================
// Lifecycle
// ============================================================================

GameAuthority::GameAuthority() = default;
GameAuthority::~GameAuthority() = default;

void GameAuthority::initialize(const std::vector<TrackPoint>& track,
                                const GameConfig& config) {
    std::lock_guard<std::mutex> lock(eventMutex_);
    
    worldState_.track = track;
    worldState_.gameTime = 0.0f;
    worldState_.gameActive = true;
    worldState_.morseSignals.clear();
    worldState_.noiseEnemies.clear();
    worldState_.powerUps.clear();
    
    // Initialize the QSO Störer
    ownedQsoStoerer_ = QSOStoerer();
    ownedQsoStoerer_.active = false;
    worldState_.qsoStoerer = &ownedQsoStoerer_;
    
    config_ = &config;
    initialized_ = true;
    
    players_.clear();
    playerEventQueues_.clear();
    pendingEvents_.clear();
    
    lastMorseSpawnTime_ = 0.0f;
    lastNoiseSpawnTime_ = 0.0f;
    lastPowerUpSpawnTime_ = 0.0f;
    nextEntityId_ = 1;
}

void GameAuthority::shutdown() {
    std::lock_guard<std::mutex> lock(eventMutex_);
    
    worldState_.gameActive = false;
    players_.clear();
    playerEventQueues_.clear();
    pendingEvents_.clear();
    worldState_.morseSignals.clear();
    worldState_.noiseEnemies.clear();
    worldState_.powerUps.clear();
    
    initialized_ = false;
}

// ============================================================================
// Player Management
// ============================================================================

int GameAuthority::registerPlayer(const PlayerInfo& info) {
    std::lock_guard<std::mutex> lock(eventMutex_);
    
    PlayerWorldState pws;
    pws.playerId = static_cast<int>(players_.size());
    pws.playerAngle = 0.0f;
    pws.playerSpeed = 0.0f;
    pws.playerLateralOffset = 0.0f;
    pws.aimAngle = 0.0f;
    pws.paHealth = 1.0f;
    pws.noiseBlankerCooldown = 0.0f;
    pws.lapsCompleted = 0;
    pws.previousAngle = 0.0f;
    pws.alive = true;
    pws.hamSpiritBonusAchieved = false;
    pws.collectedChars.clear();
    pws.activePowerUps.clear();
    pws.powerUpCollectionProgress = 0.0f;
    pws.collectingPowerUpIndex = -1;
    
    // Initialize per-player stats
    pws.stats = GameStats();
    
    // Distribute players laterally at start
    int count = static_cast<int>(players_.size()) + 1;
    if (count > 1) {
        float spacing = 1.2f / static_cast<float>(count);
        for (int i = 0; i < static_cast<int>(players_.size()); ++i) {
            players_[i].playerLateralOffset = -0.6f + spacing * (i + 0.5f);
        }
        pws.playerLateralOffset = -0.6f + spacing * (count - 0.5f);
    }
    
    players_.push_back(std::move(pws));
    playerEventQueues_.emplace_back();
    
    return static_cast<int>(players_.size()) - 1;
}

void GameAuthority::unregisterPlayer(int playerId) {
    std::lock_guard<std::mutex> lock(eventMutex_);
    
    if (playerId >= 0 && playerId < static_cast<int>(players_.size()) &&
        playerId < static_cast<int>(playerEventQueues_.size())) {
        players_.erase(players_.begin() + playerId);
        playerEventQueues_.erase(playerEventQueues_.begin() + playerId);
        
        // Re-number remaining players
        for (int i = 0; i < static_cast<int>(players_.size()); ++i) {
            players_[i].playerId = i;
        }
    }
}

int GameAuthority::getPlayerCount() const {
    return static_cast<int>(players_.size());
}

const PlayerWorldState& GameAuthority::getPlayerState(int playerId) const {
    return players_.at(playerId);
}

PlayerWorldState& GameAuthority::getPlayerStateMutable(int playerId) {
    return players_.at(playerId);
}

const GameStats& GameAuthority::getPlayerStats(int playerId) const {
    return players_.at(playerId).stats;
}

// ============================================================================
// Simulation Tick
// ============================================================================

void GameAuthority::tick(float dt) {
    if (!initialized_ || !worldState_.gameActive) return;
    
    worldState_.gameTime += dt;
    
    // Update game time for all players
    for (auto& player : players_) {
        player.stats.gameTime = worldState_.gameTime;
    }
    
    // 1. Update entity lifecycles
    updateEntityLifecycles(dt);
    
    // 2. Update weapon cooldowns for all players (identical treatment)
    for (auto& player : players_) {
        if (player.noiseBlankerCooldown > 0.0f) {
            player.noiseBlankerCooldown -= dt;
            if (player.noiseBlankerCooldown < 0.0f) {
                player.noiseBlankerCooldown = 0.0f;
            }
        }
    }
    
    // 3. Update active power-ups for all players
    for (int i = 0; i < static_cast<int>(players_.size()); ++i) {
        updateActivePowerUps(i, dt);
    }
    
    // 4. Update QSO Störer (pursues nearest player, not just Player0)
    updateQSOStoerer(dt);
    
    // 5. Apply physics for ALL players from their stored raw input.
    //    This is the ONLY place where speed/position/steering are computed.
    //    All players use identical physics — no Player0 special cases.
    for (int i = 0; i < static_cast<int>(players_.size()); ++i) {
        applyPlayerPhysics(i, dt);
    }
}

// ============================================================================
// Action Processing
// ============================================================================

ActionResult GameAuthority::processAction(const PlayerAction& action) {
    if (!initialized_ || !worldState_.gameActive) {
        return {false, ActionRejectReason::INVALID_STATE, 0.0f, {}};
    }
    
    if (action.playerId < 0 || action.playerId >= static_cast<int>(players_.size())) {
        return {false, ActionRejectReason::INVALID_STATE, 0.0f, {}};
    }
    
    auto& player = players_[action.playerId];
    
    if (!player.alive) {
        return {false, ActionRejectReason::PLAYER_DEAD, 0.0f, {}};
    }
    
    switch (action.type) {
        case PlayerActionType::MOVE_UPDATE:
            updatePlayerPosition(action.playerId, action.angle, action.speed,
                                  action.lateralOffset, action.aimAngle);
            return {true, ActionRejectReason::NONE, 0.0f, {}};
            
        case PlayerActionType::INPUT_UPDATE:
            // Store raw input — physics will be computed in tick()
            player.lastForwardInput = action.forwardInput;
            player.lastSteerInput = action.steerInput;
            player.lastAimInput = action.aimInput;
            return {true, ActionRejectReason::NONE, 0.0f, {}};
            
        case PlayerActionType::NOISE_BLANKER_FIRE:
            return processNoiseBlankerFire(action.playerId, action.angle, action.aimAngle);
            
        case PlayerActionType::MORSE_CANNON_FIRE:
            return processMorseCollection(action.playerId, action.angle, action.morseChar);
            
        case PlayerActionType::POWERUP_COLLECT_START:
        case PlayerActionType::POWERUP_COLLECT_PROGRESS:
            return processPowerUpCollection(action.playerId, action.angle,
                                             action.lateralOffset, action.value);
            
        case PlayerActionType::POWERUP_COLLECT_CANCEL:
            player.powerUpCollectionProgress = 0.0f;
            player.collectingPowerUpIndex = -1;
            return {true, ActionRejectReason::NONE, 0.0f, {}};
            
        case PlayerActionType::ANTENNA_TUNE:
            // Client reports current adjusted SWR after tuning change.
            // Authority stores it and uses it for maxSpeed and PA damage computation.
            player.adjustedSWR = std::max(1.0f, action.value);
            return {true, ActionRejectReason::NONE, 0.0f, {}};
    }
    
    return {false, ActionRejectReason::INVALID_STATE, 0.0f, {}};
}

// ============================================================================
// Interaction Validation
// ============================================================================

ActionResult GameAuthority::processNoiseBlankerFire(int playerId, float playerAngle,
                                                      float aimAngle) {
    auto& player = players_[playerId];
    
    // Check cooldown
    if (player.noiseBlankerCooldown > 0.0f) {
        return {false, ActionRejectReason::COOLDOWN_ACTIVE,
                player.noiseBlankerCooldown, {}};
    }
    
    // Set cooldown (same for all players — no special treatment)
    player.noiseBlankerCooldown = NOISE_BLANKER_COOLDOWN_SECS;
    
    // Check for active fire-rate power-up
    for (const auto& pu : player.activePowerUps) {
        if (pu.type == PowerUpType::FIRE_RATE) {
            player.noiseBlankerCooldown = FIRE_RATE_POWERUP_COOLDOWN;
            break;
        }
    }
    
    ActionResult result;
    result.accepted = true;
    result.reason = ActionRejectReason::NONE;
    
    // Emit fire event
    WorldEvent fireEvent;
    fireEvent.type = WorldEventType::NOISE_BLANKER_FIRED;
    fireEvent.sourcePlayerId = playerId;
    fireEvent.trackAngle = playerAngle;
    fireEvent.priority = EventPriority::NORMAL;
    fireEvent.timestamp = std::chrono::steady_clock::now();
    emitEvent(fireEvent);
    result.immediateEvents.push_back(fireEvent);
    
    // Check hits against noise enemies
    float aimDirection = playerAngle + aimAngle;
    // Normalize
    while (aimDirection < 0) aimDirection += TWO_PI;
    while (aimDirection >= TWO_PI) aimDirection -= TWO_PI;
    
    for (auto& enemy : worldState_.noiseEnemies) {
        if (enemy.destroyed || enemy.markedForRemoval) continue;
        
        float angleDiff = std::abs(aimDirection - enemy.angle);
        if (angleDiff > static_cast<float>(M_PI)) angleDiff = TWO_PI - angleDiff;
        
        if (angleDiff <= HIT_CONE_RADIANS) {
            enemy.health--;
            
            WorldEvent hitEvent;
            hitEvent.type = WorldEventType::NOISE_ENEMY_HIT;
            hitEvent.sourcePlayerId = playerId;
            hitEvent.trackAngle = enemy.angle;
            hitEvent.priority = EventPriority::NORMAL;
            hitEvent.timestamp = std::chrono::steady_clock::now();
            emitEvent(hitEvent);
            result.immediateEvents.push_back(hitEvent);
            
            if (enemy.health <= 0) {
                enemy.destroyed = true;
                enemy.destroyedByPlayer = playerId;
                enemy.markedForRemoval = true;
                
                // Award score to the player who destroyed it
                player.stats.score += NOISE_ENEMY_DESTROY_SCORE;
                
                WorldEvent destroyEvent;
                destroyEvent.type = WorldEventType::NOISE_ENEMY_DESTROYED;
                destroyEvent.sourcePlayerId = playerId;
                destroyEvent.trackAngle = enemy.angle;
                destroyEvent.value = static_cast<float>(NOISE_ENEMY_DESTROY_SCORE);
                destroyEvent.priority = EventPriority::HIGH;
                destroyEvent.timestamp = std::chrono::steady_clock::now();
                emitEvent(destroyEvent);
                result.immediateEvents.push_back(destroyEvent);
            }
        }
    }
    
    // Check hits against QSO Störer
    if (worldState_.qsoStoerer && worldState_.qsoStoerer->active) {
        float angleDiff = std::abs(aimDirection - worldState_.qsoStoerer->angle);
        if (angleDiff > static_cast<float>(M_PI)) angleDiff = TWO_PI - angleDiff;
        
        if (angleDiff <= HIT_CONE_RADIANS) {
            worldState_.qsoStoerer->health -= QSO_STOERER_DAMAGE_PER_HIT;
            if (worldState_.qsoStoerer->health < QSO_STOERER_MIN_HEALTH) {
                worldState_.qsoStoerer->health = QSO_STOERER_MIN_HEALTH;
            }
            
            WorldEvent stoererHitEvent;
            stoererHitEvent.type = WorldEventType::QSO_STOERER_HIT;
            stoererHitEvent.sourcePlayerId = playerId;
            stoererHitEvent.trackAngle = worldState_.qsoStoerer->angle;
            stoererHitEvent.priority = EventPriority::NORMAL;
            stoererHitEvent.timestamp = std::chrono::steady_clock::now();
            emitEvent(stoererHitEvent);
            result.immediateEvents.push_back(stoererHitEvent);
        }
    }
    
    // Check hits against power-ups
    for (auto& pu : worldState_.powerUps) {
        if (pu.collected || pu.destroyed || pu.markedForRemoval) continue;
        
        float angleDiff = std::abs(aimDirection - pu.angle);
        if (angleDiff > static_cast<float>(M_PI)) angleDiff = TWO_PI - angleDiff;
        
        if (angleDiff <= HIT_CONE_RADIANS) {
            pu.destroyed = true;
            pu.markedForRemoval = true;
            
            WorldEvent puDestroyEvent;
            puDestroyEvent.type = WorldEventType::POWERUP_DESTROYED;
            puDestroyEvent.sourcePlayerId = playerId;
            puDestroyEvent.trackAngle = pu.angle;
            puDestroyEvent.priority = EventPriority::NORMAL;
            puDestroyEvent.timestamp = std::chrono::steady_clock::now();
            emitEvent(puDestroyEvent);
            result.immediateEvents.push_back(puDestroyEvent);
        }
    }
    
    return result;
}

ActionResult GameAuthority::processMorseCollection(int playerId, float playerAngle,
                                                     char sentChar) {
    auto& player = players_[playerId];
    ActionResult result;
    
    // Find the nearest uncollected morse signal matching the character
    float bestDist = std::numeric_limits<float>::max();
    int bestIdx = -1;
    
    for (int i = 0; i < static_cast<int>(worldState_.morseSignals.size()); ++i) {
        auto& signal = worldState_.morseSignals[i];
        if (signal.collected || signal.markedForRemoval) continue;
        if (signal.character != sentChar) continue;
        
        float dist = calculateTrackDistance(playerAngle, signal.angle);
        // Only collect within proximity (about 30 degrees on track)
        if (dist < bestDist && dist < TRACK_RADIUS_METERS * MAX_MORSE_COLLECTION_DISTANCE_FACTOR) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    
    if (bestIdx >= 0) {
        auto& signal = worldState_.morseSignals[bestIdx];
        
        // First-come-first-serve: check if already claimed
        if (signal.collected) {
            result.accepted = false;
            result.reason = ActionRejectReason::ENTITY_ALREADY_CLAIMED;
            return result;
        }
        
        // Collect it!
        signal.collected = true;
        signal.collectedByPlayer = playerId;
        signal.markedForRemoval = true;
        
        // Award score and update stats
        player.stats.score += MORSE_COLLECTION_SCORE;
        player.stats.charactersCollected++;
        player.collectedChars.push_back(sentChar);
        
        // PA repair on morse collection
        player.paHealth = std::min(1.0f, player.paHealth + PA_REPAIR_PER_MORSE);
        player.stats.paHealth = player.paHealth;
        
        result.accepted = true;
        result.reason = ActionRejectReason::NONE;
        
        // Generate collection event
        WorldEvent collectEvent;
        collectEvent.type = WorldEventType::MORSE_COLLECTED;
        collectEvent.sourcePlayerId = playerId;
        collectEvent.trackAngle = signal.angle;
        collectEvent.morseChar = sentChar;
        collectEvent.value = static_cast<float>(MORSE_COLLECTION_SCORE);
        collectEvent.priority = EventPriority::NORMAL;
        collectEvent.timestamp = std::chrono::steady_clock::now();
        emitEvent(collectEvent);
        result.immediateEvents.push_back(collectEvent);
        
        // Check for PA repair event
        WorldEvent repairEvent;
        repairEvent.type = WorldEventType::PA_REPAIRED;
        repairEvent.sourcePlayerId = playerId;
        repairEvent.trackAngle = playerAngle;
        repairEvent.value = player.paHealth;
        repairEvent.priority = EventPriority::NORMAL;
        repairEvent.timestamp = std::chrono::steady_clock::now();
        emitEvent(repairEvent);
        
        // Check for HAMSPIRIT bonus
        const std::string hamspirit = "HAMSPIRIT";
        bool bonusCheck = true;
        for (char c : hamspirit) {
            if (std::find(player.collectedChars.begin(),
                          player.collectedChars.end(), c)
                    == player.collectedChars.end()) {
                bonusCheck = false;
                break;
            }
        }
        if (bonusCheck && !player.hamSpiritBonusAchieved) {
            player.hamSpiritBonusAchieved = true;
            player.stats.bonusAchieved = true;
            player.stats.score += HAMSPIRIT_BONUS_SCORE;
            
            WorldEvent bonusEvent;
            bonusEvent.type = WorldEventType::HAMSPIRIT_BONUS;
            bonusEvent.sourcePlayerId = playerId;
            bonusEvent.trackAngle = playerAngle;
            bonusEvent.value = static_cast<float>(HAMSPIRIT_BONUS_SCORE);
            bonusEvent.priority = EventPriority::HIGH;
            bonusEvent.timestamp = std::chrono::steady_clock::now();
            emitEvent(bonusEvent);
            result.immediateEvents.push_back(bonusEvent);
        }
    } else {
        // No matching signal nearby — miss
        result.accepted = false;
        result.reason = ActionRejectReason::OUT_OF_RANGE;
    }
    
    return result;
}

ActionResult GameAuthority::processPowerUpCollection(int playerId, float playerAngle,
                                                       float lateralOffset, float progress) {
    auto& player = players_[playerId];
    ActionResult result;
    
    // Find nearby uncollected power-up
    int bestIdx = -1;
    float bestDist = std::numeric_limits<float>::max();
    
    for (int i = 0; i < static_cast<int>(worldState_.powerUps.size()); ++i) {
        auto& pu = worldState_.powerUps[i];
        if (pu.collected || pu.destroyed || pu.markedForRemoval) continue;
        
        float angleDist = std::abs(playerAngle - pu.angle);
        if (angleDist > static_cast<float>(M_PI)) angleDist = TWO_PI - angleDist;
        
        if (angleDist <= pu.zoneHalfWidth && angleDist < bestDist) {
            bestDist = angleDist;
            bestIdx = i;
        }
    }
    
    if (bestIdx < 0) {
        player.powerUpCollectionProgress = 0.0f;
        player.collectingPowerUpIndex = -1;
        result.accepted = false;
        result.reason = ActionRejectReason::OUT_OF_RANGE;
        return result;
    }
    
    auto& pu = worldState_.powerUps[bestIdx];
    
    // Check if already claimed by another player
    if (pu.collected) {
        result.accepted = false;
        result.reason = ActionRejectReason::ENTITY_ALREADY_CLAIMED;
        return result;
    }
    
    // Update collection progress
    player.collectingPowerUpIndex = bestIdx;
    player.powerUpCollectionProgress = progress;
    
    if (progress >= 1.0f) {
        // Collection complete!
        pu.collected = true;
        pu.collectedByPlayer = playerId;
        pu.markedForRemoval = true;
        
        player.stats.score += POWERUP_COLLECTION_SCORE;
        player.powerUpCollectionProgress = 0.0f;
        player.collectingPowerUpIndex = -1;
        
        // Activate power-up effect
        ActivePowerUp active(pu.type, DEFAULT_POWERUP_DURATION_SECS);
        player.activePowerUps.push_back(active);
        
        WorldEvent collectEvent;
        collectEvent.type = WorldEventType::POWERUP_COLLECTED;
        collectEvent.sourcePlayerId = playerId;
        collectEvent.trackAngle = pu.angle;
        collectEvent.value = static_cast<float>(static_cast<int>(pu.type));
        collectEvent.entityId = pu.uid;
        collectEvent.priority = EventPriority::NORMAL;
        collectEvent.timestamp = std::chrono::steady_clock::now();
        emitEvent(collectEvent);
        result.immediateEvents.push_back(collectEvent);
        
        WorldEvent activateEvent;
        activateEvent.type = WorldEventType::POWERUP_ACTIVATED;
        activateEvent.sourcePlayerId = playerId;
        activateEvent.trackAngle = playerAngle;
        activateEvent.value = static_cast<float>(static_cast<int>(pu.type));
        activateEvent.priority = EventPriority::NORMAL;
        activateEvent.timestamp = std::chrono::steady_clock::now();
        emitEvent(activateEvent);
    }
    
    result.accepted = true;
    result.reason = ActionRejectReason::NONE;
    return result;
}

// ============================================================================
// NPC Management
// ============================================================================

void GameAuthority::updateQSOStoerer(float dt) {
    if (!worldState_.qsoStoerer || !worldState_.qsoStoerer->active) return;
    if (players_.empty()) return;
    
    auto& stoerer = *worldState_.qsoStoerer;
    
    // Find nearest player (replaces old Player0-only targeting)
    int targetPlayer = findNearestPlayer(stoerer.angle);
    if (targetPlayer < 0) return;
    
    const auto& target = players_[targetPlayer];
    
    // Move toward target
    float angleDiff = target.playerAngle - stoerer.angle;
    // Normalize to [-π, π]
    while (angleDiff > static_cast<float>(M_PI)) angleDiff -= TWO_PI;
    while (angleDiff < -static_cast<float>(M_PI)) angleDiff += TWO_PI;
    
    float effectiveSpeed = stoerer.speed * stoerer.health;
    if (angleDiff > 0) {
        stoerer.angle += effectiveSpeed * dt;
    } else {
        stoerer.angle -= effectiveSpeed * dt;
    }
    
    // Normalize angle
    while (stoerer.angle < 0) stoerer.angle += TWO_PI;
    while (stoerer.angle >= TWO_PI) stoerer.angle -= TWO_PI;
    
    // Recover health slowly
    stoerer.health = std::min(1.0f, stoerer.health + QSO_STOERER_RECOVERY_RATE * dt);
}

// ============================================================================
// PA Health
// ============================================================================

void GameAuthority::updatePAHealth(int playerId, float currentSWR, float dt) {
    if (playerId < 0 || playerId >= static_cast<int>(players_.size())) return;
    
    auto& player = players_[playerId];
    if (!player.alive) return;
    
    // Check for SWR immunity power-up
    for (const auto& pu : player.activePowerUps) {
        if (pu.type == PowerUpType::SWR_IMMUNITY) {
            return; // Immune to SWR damage
        }
    }
    
    // Damage proportional to SWR above threshold
    if (currentSWR > SWR_DAMAGE_THRESHOLD) {
        float damage = (currentSWR - SWR_DAMAGE_THRESHOLD) * SWR_DAMAGE_RATE * dt;
        player.paHealth -= damage;
        player.stats.paHealth = player.paHealth;
        
        if (player.paHealth <= 0.0f) {
            player.paHealth = 0.0f;
            player.stats.paHealth = 0.0f;
            player.alive = false;
            
            WorldEvent paDestroyedEvent;
            paDestroyedEvent.type = WorldEventType::PA_DESTROYED;
            paDestroyedEvent.sourcePlayerId = playerId;
            paDestroyedEvent.trackAngle = player.playerAngle;
            paDestroyedEvent.priority = EventPriority::CRITICAL;
            paDestroyedEvent.timestamp = std::chrono::steady_clock::now();
            emitEvent(paDestroyedEvent);
        } else if (damage > 0.001f) {
            WorldEvent paDamageEvent;
            paDamageEvent.type = WorldEventType::PA_DAMAGED;
            paDamageEvent.sourcePlayerId = playerId;
            paDamageEvent.trackAngle = player.playerAngle;
            paDamageEvent.value = player.paHealth;
            paDamageEvent.priority = EventPriority::HIGH;
            paDamageEvent.timestamp = std::chrono::steady_clock::now();
            emitEvent(paDamageEvent);
        }
    }
}

// ============================================================================
// Lap Detection
// ============================================================================

void GameAuthority::checkLapCompletion(int playerId, float newAngle, float oldAngle) {
    if (playerId < 0 || playerId >= static_cast<int>(players_.size())) return;
    
    auto& player = players_[playerId];
    
    // Detect wrap-around from near 2π back to near 0
    if (oldAngle > LAP_DETECTION_OLD_ANGLE_MIN && newAngle < LAP_DETECTION_NEW_ANGLE_MAX) {
        player.lapsCompleted++;
        player.stats.lapsCompleted = player.lapsCompleted;
        
        // Award lap bonus
        player.stats.score += LAP_COMPLETION_SCORE;
        
        WorldEvent lapEvent;
        lapEvent.type = WorldEventType::LAP_COMPLETED;
        lapEvent.sourcePlayerId = playerId;
        lapEvent.trackAngle = newAngle;
        lapEvent.value = static_cast<float>(player.lapsCompleted);
        lapEvent.priority = EventPriority::HIGH;
        lapEvent.timestamp = std::chrono::steady_clock::now();
        emitEvent(lapEvent);
    }
}

// ============================================================================
// Player Position
// ============================================================================

void GameAuthority::updatePlayerPosition(int playerId, float newAngle, float newSpeed,
                                           float newLateralOffset, float newAimAngle) {
    if (playerId < 0 || playerId >= static_cast<int>(players_.size())) return;
    
    auto& player = players_[playerId];
    float oldAngle = player.playerAngle;
    
    player.playerAngle = newAngle;
    player.playerSpeed = newSpeed;
    player.playerLateralOffset = newLateralOffset;
    player.aimAngle = newAimAngle;
    
    // Check for lap completion
    checkLapCompletion(playerId, newAngle, oldAngle);
    
    player.previousAngle = oldAngle;
}

// ============================================================================
// Physics Simulation (unified for ALL players)
// ============================================================================

void GameAuthority::applyPlayerPhysics(int playerId, float dt) {
    if (playerId < 0 || playerId >= static_cast<int>(players_.size())) return;
    auto& player = players_[playerId];
    if (!player.alive) return;
    
    // Use config values if available, otherwise defaults
    float inputDeadzone = config_ ? config_->inputDeadzone : DEFAULT_INPUT_DEADZONE;
    float accelSens = config_ ? config_->accelerationSensitivity : DEFAULT_ACCEL_SENSITIVITY;
    float steerSens = config_ ? config_->steeringSensitivity : DEFAULT_STEERING_SENSITIVITY;
    float aimSens = AIM_SENSITIVITY * (config_ ? config_->aimSensitivity : 1.0f);
    bool swrDamageEnabled = config_ ? config_->elemSwrDamage : true;
    
    // ── SWR → maxSpeed (antenna tuning affects performance) ──
    // Each player's antenna tuning determines their adjustedSWR.
    // Better tuning → lower SWR → higher maxSpeed.
    // This is computed here so ALL players get identical treatment.
    float currentSWR = player.adjustedSWR;
    float swrSpeedFactor = 1.0f;
    if (currentSWR > 1.0f) {
        // Speed factor: 1.0 at SWR=1, drops as SWR rises
        // At SWR 2: ~0.85, at SWR 3: ~0.7, at SWR 5: ~0.5, at SWR 10: ~0.3
        swrSpeedFactor = 1.0f / (1.0f + 0.15f * (currentSWR - 1.0f));
    }
    player.maxSpeed = player.baseMaxSpeed * swrSpeedFactor;
    
    // ── PA thermal/damage model (SWR causes PA stress) ──
    // Reflected power: P_reflected = P_forward * ((SWR-1)/(SWR+1))^2
    float clampedDt = std::min(dt, 0.1f);  // Prevent huge spikes
    float reflectionCoeff = (currentSWR > 1.0f) ? (currentSWR - 1.0f) / (currentSWR + 1.0f) : 0.0f;
    float reflectedPowerFraction = reflectionCoeff * reflectionCoeff;
    
    float transmitSpeedFactor = (player.maxSpeed > 0.0f) ? 
        std::min(std::abs(player.playerSpeed) / player.maxSpeed, 1.0f) : 0.0f;
    
    // Check SWR immunity from power-ups
    player.swrImmunityActive = false;
    for (const auto& pu : player.activePowerUps) {
        if (pu.type == PowerUpType::SWR_IMMUNITY) {
            player.swrImmunityActive = true;
            break;
        }
    }
    
    static constexpr float SAFE_SWR = 2.0f;
    static constexpr float THERMAL_CAPACITY = 5.0f;
    static constexpr float COOLING_RATE = 0.03f;
    
    if (currentSWR > SAFE_SWR && transmitSpeedFactor > 0.01f && !player.swrImmunityActive) {
        // Heat accumulates from reflected power * transmitted power
        float heatInput = reflectedPowerFraction * transmitSpeedFactor / THERMAL_CAPACITY;
        player.paThermalLoad += heatInput * clampedDt;
        player.paReflectedPowerAccum += reflectedPowerFraction * transmitSpeedFactor * clampedDt;
        
        // Permanent damage when thermal load exceeds safe zone
        if (player.paThermalLoad > 0.3f) {
            float excessHeat = player.paThermalLoad - 0.3f;
            float damageRate = 0.02f * excessHeat * excessHeat;
            if (!swrDamageEnabled) damageRate = 0.0f;
            player.paHealth -= damageRate * transmitSpeedFactor * clampedDt;
        }
    } else {
        // Cooling
        player.paThermalLoad -= COOLING_RATE * clampedDt;
        if (player.paThermalLoad < 0.0f) player.paThermalLoad = 0.0f;
        
        // Slow regen when cool
        if (player.paThermalLoad < 0.1f && player.paHealth > 0.0f && player.paHealth < 1.0f) {
            player.paHealth += 0.005f * clampedDt;
            player.paHealth = std::min(1.0f, player.paHealth);
        }
    }
    player.paThermalLoad = std::min(player.paThermalLoad, 2.0f);
    player.stats.paHealth = player.paHealth;
    
    // PA damage → speed restriction (staged)
    int damageStage = 0;
    if (player.paHealth <= 0.1f) damageStage = 5;
    else if (player.paHealth <= 0.3f) damageStage = 4;
    else if (player.paHealth <= 0.5f) damageStage = 3;
    else if (player.paHealth <= 0.7f) damageStage = 2;
    else if (player.paHealth <= 0.9f) damageStage = 1;
    
    player.paDamageSpeedMultiplier = 1.0f;
    switch (damageStage) {
        case 1: player.paDamageSpeedMultiplier = 0.90f; break;
        case 2: player.paDamageSpeedMultiplier = 0.75f; break;
        case 3: player.paDamageSpeedMultiplier = 0.55f; break;
        case 4: player.paDamageSpeedMultiplier = 0.35f; break;
        case 5: player.paDamageSpeedMultiplier = 0.15f; break;
    }
    player.maxSpeed *= player.paDamageSpeedMultiplier;
    player.lastAnnouncedDamageStage = damageStage;
    
    // PA destroyed → game over for this player
    if (player.paHealth <= 0.0f) {
        player.paHealth = 0.0f;
        player.stats.paHealth = 0.0f;
        player.alive = false;
        
        WorldEvent paDestroyedEvent;
        paDestroyedEvent.type = WorldEventType::PA_DESTROYED;
        paDestroyedEvent.sourcePlayerId = playerId;
        paDestroyedEvent.trackAngle = player.playerAngle;
        paDestroyedEvent.priority = EventPriority::CRITICAL;
        paDestroyedEvent.timestamp = std::chrono::steady_clock::now();
        emitEvent(paDestroyedEvent);
    }
    
    float effectiveMaxSpeed = player.maxSpeed;
    if (effectiveMaxSpeed <= 0.0f) return;  // Not yet initialized
    
    // ── Smoothed throttle input (low-pass filter, ~12ms time constant) ──
    float rawForward = player.lastForwardInput;
    float smoothRate = std::min(THROTTLE_SMOOTH_RATE * dt, 1.0f);
    player.smoothedForwardInput += smoothRate * (rawForward - player.smoothedForwardInput);
    float forwardInput = player.smoothedForwardInput;
    
    // ── Steering ──
    float steerInput = player.lastSteerInput;
    float effectiveSteering = STEERING_SPEED * steerSens;
    
    // Steering scales with speed (no steering at standstill)
    float absSpeed = std::abs(player.playerSpeed);
    float minSteeringThreshold = effectiveMaxSpeed * 0.05f;
    float speedFactor = std::min(absSpeed / std::max(minSteeringThreshold, effectiveMaxSpeed * 0.15f), 1.0f);
    speedFactor = std::max(speedFactor, 0.2f);  // Allow limited steering from standstill
    effectiveSteering *= speedFactor;
    
    if (std::abs(steerInput) > inputDeadzone) {
        player.playerLateralOffset += steerInput * effectiveSteering * dt;
        player.playerLateralOffset = std::max(-1.0f, std::min(1.0f, player.playerLateralOffset));
    } else {
        // Auto-center when no steering input
        float centerRate = 0.2f * dt;
        if (player.playerLateralOffset > 0.0f) {
            player.playerLateralOffset -= centerRate;
            if (player.playerLateralOffset < 0.0f) player.playerLateralOffset = 0.0f;
        } else if (player.playerLateralOffset < 0.0f) {
            player.playerLateralOffset += centerRate;
            if (player.playerLateralOffset > 0.0f) player.playerLateralOffset = 0.0f;
        }
    }
    // Auto-steering: automatically center the car to avoid border crashes
    // (accessibility feature — matches single-player behavior)
    if (config_ && config_->elemAutoSteering) {
        float correction = -player.playerLateralOffset * 2.0f * dt;
        player.playerLateralOffset += correction;
    }
    
    // ── Aiming ──
    float aimInput = player.lastAimInput;
    if (std::abs(aimInput) > inputDeadzone) {
        player.aimAngle += aimInput * dt * aimSens;
        while (player.aimAngle > static_cast<float>(M_PI))
            player.aimAngle -= TWO_PI;
        while (player.aimAngle < -static_cast<float>(M_PI))
            player.aimAngle += TWO_PI;
    }
    
    // ── Rolling friction / drag ──
    absSpeed = std::abs(player.playerSpeed);
    if (absSpeed > STANDSTILL_THRESHOLD) {
        float rollingResistance = 0.008f * effectiveMaxSpeed;
        float aeroDrag = 0.015f * absSpeed * absSpeed / std::max(effectiveMaxSpeed, 0.01f);
        float dragForce = rollingResistance + aeroDrag;
        if (player.playerSpeed > 0.0f) {
            player.playerSpeed -= dragForce * dt;
            if (player.playerSpeed < 0.0f) player.playerSpeed = 0.0f;
        }
    }
    
    // ── Acceleration / braking ──
    float effectiveAccel = DEFAULT_ACCELERATION * effectiveMaxSpeed * accelSens;
    
    if (forwardInput > inputDeadzone) {
        // Accelerate: progressive throttle (squared curve)
        float normalizedInput = (forwardInput - inputDeadzone) / (1.0f - inputDeadzone);
        float throttle = normalizedInput * normalizedInput;
        if (player.playerSpeed < 0.0f) player.playerSpeed = 0.0f;
        player.playerSpeed += effectiveAccel * throttle * dt;
        player.playerSpeed = std::min(player.playerSpeed, effectiveMaxSpeed);
    } else if (forwardInput < -inputDeadzone) {
        if (player.playerSpeed > STANDSTILL_THRESHOLD) {
            // Brake: progressive (squared curve), 35% maxSpeed/s at full brake
            float brakeInput = (std::abs(forwardInput) - inputDeadzone) / (1.0f - inputDeadzone);
            brakeInput = std::max(0.0f, std::min(1.0f, brakeInput));
            float brakeThrottle = brakeInput * brakeInput;
            float baseBrakeDecel = 0.35f * effectiveMaxSpeed;
            player.playerSpeed -= baseBrakeDecel * brakeThrottle * dt;
            if (player.playerSpeed < 0.0f) player.playerSpeed = 0.0f;
        } else {
            player.playerSpeed = 0.0f;
        }
    } else {
        // No input: clean stop near zero
        if (std::abs(player.playerSpeed) < STANDSTILL_THRESHOLD) {
            player.playerSpeed = 0.0f;
        }
    }
    
    // Hard clamp: no reverse
    if (player.playerSpeed < 0.0f) player.playerSpeed = 0.0f;
    
    // ── Advance position on track ──
    float oldAngle = player.playerAngle;
    player.playerAngle += player.playerSpeed * dt;
    while (player.playerAngle >= TWO_PI)
        player.playerAngle -= TWO_PI;
    
    // Check for lap completion
    checkLapCompletion(playerId, player.playerAngle, oldAngle);
    player.previousAngle = oldAngle;
}

// ============================================================================
// Event Distribution
// ============================================================================

void GameAuthority::emitEvent(WorldEvent event) {
    event.timestamp = std::chrono::steady_clock::now();
    
    // Add to global pending events
    pendingEvents_.push_back(event);
    if (static_cast<int>(pendingEvents_.size()) > MAX_PENDING_EVENTS) {
        pendingEvents_.pop_front();
    }
    
    // Dispatch to per-player queues based on relevance
    dispatchEventToPlayers(event);
}

void GameAuthority::dispatchEventToPlayers(const WorldEvent& event) {
    for (int i = 0; i < static_cast<int>(players_.size()); ++i) {
        if (isEventRelevantToPlayer(event, i)) {
            if (i < static_cast<int>(playerEventQueues_.size())) {
                playerEventQueues_[i].push_back(event);
                
                // Prevent unbounded queue growth
                while (static_cast<int>(playerEventQueues_[i].size()) > MAX_PENDING_EVENTS) {
                    playerEventQueues_[i].pop_front();
                }
            }
        }
    }
}

std::vector<WorldEvent> GameAuthority::getEventsForPlayer(int playerId) {
    std::lock_guard<std::mutex> lock(eventMutex_);
    
    std::vector<WorldEvent> events;
    if (playerId >= 0 && playerId < static_cast<int>(playerEventQueues_.size())) {
        events.assign(playerEventQueues_[playerId].begin(),
                       playerEventQueues_[playerId].end());
        playerEventQueues_[playerId].clear();
    }
    return events;
}

std::vector<WorldEvent> GameAuthority::getAllEvents() {
    std::lock_guard<std::mutex> lock(eventMutex_);
    
    std::vector<WorldEvent> events(pendingEvents_.begin(), pendingEvents_.end());
    pendingEvents_.clear();
    return events;
}

// ============================================================================
// Relevance Calculation
// ============================================================================

bool GameAuthority::isEventRelevantToPlayer(const WorldEvent& event, int playerId) const {
    if (playerId < 0 || playerId >= static_cast<int>(players_.size())) return false;
    
    // Own events are always relevant
    if (event.sourcePlayerId == playerId) return true;
    
    // Critical events are always relevant (game over, PA destroyed)
    if (event.priority == EventPriority::CRITICAL) return true;
    
    // High-priority events are always delivered
    if (event.priority == EventPriority::HIGH) return true;
    
    // For normal/low priority, check distance
    float distance = calculateTrackDistance(event.trackAngle,
                                            players_[playerId].playerAngle);
    
    // Different relevance radii by event type
    float relevanceRadius = MAX_AUDIBLE_DISTANCE;
    
    switch (event.type) {
        case WorldEventType::NOISE_BLANKER_FIRED:
            relevanceRadius = 500.0f;
            break;
        case WorldEventType::NOISE_ENEMY_HIT:
            relevanceRadius = 300.0f;
            break;
        case WorldEventType::NOISE_ENEMY_DESTROYED:
            relevanceRadius = 500.0f;
            break;
        case WorldEventType::MORSE_COLLECTED:
            relevanceRadius = 200.0f;
            break;
        case WorldEventType::POWERUP_COLLECTED:
            relevanceRadius = 300.0f;
            break;
        case WorldEventType::ENTITY_SPAWNED:
            relevanceRadius = 300.0f;
            break;
        default:
            relevanceRadius = MAX_AUDIBLE_DISTANCE;
            break;
    }
    
    return distance <= relevanceRadius;
}

// ============================================================================
// Entity Lifecycle
// ============================================================================

void GameAuthority::updateEntityLifecycles(float dt) {
    float gameTime = worldState_.gameTime;
    
    // Remove expired morse signals
    auto& morse = worldState_.morseSignals;
    morse.erase(std::remove_if(morse.begin(), morse.end(),
        [gameTime](const MorseSignal& s) {
            return s.markedForRemoval ||
                   (gameTime - s.spawnTime > s.lifetime);
        }), morse.end());
    
    // Remove expired/destroyed noise enemies
    auto& enemies = worldState_.noiseEnemies;
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
        [gameTime](const NoiseEnemy& e) {
            return e.markedForRemoval || e.destroyed ||
                   (gameTime - e.spawnTime > e.lifetime);
        }), enemies.end());
    
    // Remove expired/collected/destroyed power-ups
    auto& pups = worldState_.powerUps;
    pups.erase(std::remove_if(pups.begin(), pups.end(),
        [gameTime](const PowerUp& p) {
            return p.markedForRemoval || p.collected || p.destroyed ||
                   (gameTime - p.spawnTime > p.lifetime);
        }), pups.end());
}

void GameAuthority::updateActivePowerUps(int playerId, float dt) {
    if (playerId < 0 || playerId >= static_cast<int>(players_.size())) return;
    
    auto& player = players_[playerId];
    auto& actives = player.activePowerUps;
    
    // Update timers
    for (auto& active : actives) {
        active.remainingTime -= dt;
    }
    
    // Collect expired power-ups first, then emit events after removal.
    // This avoids modifying pendingEvents_ during iteration and
    // keeps event emission properly synchronized.
    std::vector<WorldEvent> expireEvents;
    for (const auto& a : actives) {
        if (a.remainingTime <= 0.0f) {
            WorldEvent expireEvent;
            expireEvent.type = WorldEventType::POWERUP_EXPIRED;
            expireEvent.sourcePlayerId = playerId;
            expireEvent.value = static_cast<float>(static_cast<int>(a.type));
            expireEvent.priority = EventPriority::NORMAL;
            expireEvent.timestamp = std::chrono::steady_clock::now();
            expireEvents.push_back(expireEvent);
        }
    }
    
    // Remove expired power-ups
    actives.erase(std::remove_if(actives.begin(), actives.end(),
        [](const ActivePowerUp& a) { return a.remainingTime <= 0.0f; }),
        actives.end());
    
    // Now emit events safely (no concurrent modification risk)
    for (auto& event : expireEvents) {
        emitEvent(event);
    }
}

// ============================================================================
// Spawn Management
// ============================================================================

void GameAuthority::spawnMorseSignals(float gameTime) {
    // Stub — actual spawn logic is delegated from the Game class
    // This method exists for the authority to manage spawns in the future
    // Currently, spawn calls are forwarded through the existing Game methods
    // during the transition period
}

void GameAuthority::spawnNoiseEnemy(float gameTime) {
    // Stub — same as above
}

void GameAuthority::spawnPowerUp() {
    // Stub — same as above
}

// ============================================================================
// Utility
// ============================================================================

float GameAuthority::calculateTrackDistance(float angle1, float angle2) {
    float diff = std::abs(angle1 - angle2);
    if (diff > static_cast<float>(M_PI)) {
        diff = TWO_PI - diff;
    }
    // Convert angular distance to meters using track radius
    return diff * TRACK_RADIUS_METERS;
}

int GameAuthority::findNearestPlayer(float trackAngle) const {
    int nearest = -1;
    float bestDist = std::numeric_limits<float>::max();
    
    for (int i = 0; i < static_cast<int>(players_.size()); ++i) {
        if (!players_[i].alive) continue;
        
        float dist = calculateTrackDistance(trackAngle, players_[i].playerAngle);
        if (dist < bestDist) {
            bestDist = dist;
            nearest = i;
        }
    }
    
    return nearest;
}

std::vector<float> GameAuthority::getAllPlayerPositions() const {
    std::vector<float> positions;
    positions.reserve(players_.size());
    for (const auto& player : players_) {
        positions.push_back(player.playerAngle);
    }
    return positions;
}

} // namespace HamSpirit

#endif // WITH_HAM_SPIRIT
