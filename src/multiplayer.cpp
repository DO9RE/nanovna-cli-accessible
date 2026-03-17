#include "multiplayer.h"

#ifdef WITH_HAM_SPIRIT

#include "platform/interface/audio_backend.h"
#include "tts_interface.h"
#include "gamepad_interface.h"
#include "hamspirit_game.h"
#include "logger.h"
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <algorithm>
#include <cstring>

#if defined(PLATFORM_MACOS) || defined(PLATFORM_LINUX)
#include <portaudio.h>
#endif

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#include <mmsystem.h>
#endif

namespace HamSpirit {

// ─── PlayerContext ──────────────────────────────────────────────────────────

PlayerContext::~PlayerContext() = default;

PlayerContext::PlayerContext(PlayerContext&& other) noexcept
    : playerIndex(other.playerIndex),
      callsign(std::move(other.callsign)),
      playerName(std::move(other.playerName)),
      inputAssignment(other.inputAssignment),
      audioDeviceIndex(other.audioDeviceIndex),
      audioBackend(other.audioBackend),
      antennaNetwork(std::move(other.antennaNetwork)),
      playerAngle(other.playerAngle),
      playerSpeed(other.playerSpeed),
      maxSpeed(other.maxSpeed),
      playerLateralOffset(other.playerLateralOffset),
      aimAngle(other.aimAngle),
      paHealth(other.paHealth),
      stats(other.stats),
      noiseBlankerCooldown(other.noiseBlankerCooldown),
      lapsCompleted(other.lapsCompleted),
      lastLapAngle(other.lastLapAngle),
      collectedChars(std::move(other.collectedChars)),
      hamSpiritBonusAchieved(other.hamSpiritBonusAchieved),
      morseMissCount(other.morseMissCount),
      prevNoiseBlankerBtn(other.prevNoiseBlankerBtn),
      prevTunerY(other.prevTunerY),
      prevTunerX(other.prevTunerX),
      prevTunerB(other.prevTunerB),
      prevTunerA(other.prevTunerA),
      tunerHoldTimerY(other.tunerHoldTimerY),
      tunerHoldTimerX(other.tunerHoldTimerX),
      tunerHoldTimerB(other.tunerHoldTimerB),
      tunerHoldTimerA(other.tunerHoldTimerA),
      prevTunerCheat(other.prevTunerCheat),
      prevDpadUp(other.prevDpadUp),
      prevDpadDown(other.prevDpadDown),
      prevDpadLeft(other.prevDpadLeft),
      prevDpadRight(other.prevDpadRight),
      currentBandName(std::move(other.currentBandName)),
      collisionCooldown(other.collisionCooldown),
      collisionSpeedDelta(other.collisionSpeedDelta),
      collisionLateralPush(other.collisionLateralPush),
      collisionSoundFrames(other.collisionSoundFrames),
      collisionIntensity(other.collisionIntensity),
      collisionPan(other.collisionPan),
      hasBrailleContext(other.hasBrailleContext),
      trackPositionMeters(other.trackPositionMeters),
      lateralPositionMeters(other.lateralPositionMeters),
      morseCannonActive(other.morseCannonActive),
      morseCannonIsDash(other.morseCannonIsDash),
      morseCannonTimestamp(other.morseCannonTimestamp),
      audioState(std::move(other.audioState)),
      audioBuf(std::move(other.audioBuf)),
      mixBuf(std::move(other.mixBuf)),
      roughnessPhase(other.roughnessPhase),
      rattlePhase(other.rattlePhase),
      morsePatternPhase(other.morsePatternPhase),
      smoothedPan(other.smoothedPan),
      swrAlertPhase(other.swrAlertPhase) {
    other.audioBackend = nullptr;
    other.stats = nullptr;
    std::memcpy(enginePhases, other.enginePhases, sizeof(enginePhases));
    std::memcpy(morsePhases, other.morsePhases, sizeof(morsePhases));
    std::memcpy(pairCollisionCooldown, other.pairCollisionCooldown, sizeof(pairCollisionCooldown));
}

PlayerContext& PlayerContext::operator=(PlayerContext&& other) noexcept {
    if (this != &other) {
        playerIndex = other.playerIndex;
        callsign = std::move(other.callsign);
        playerName = std::move(other.playerName);
        inputAssignment = other.inputAssignment;
        audioDeviceIndex = other.audioDeviceIndex;
        audioBackend = other.audioBackend;
        antennaNetwork = std::move(other.antennaNetwork);
        playerAngle = other.playerAngle;
        playerSpeed = other.playerSpeed;
        maxSpeed = other.maxSpeed;
        playerLateralOffset = other.playerLateralOffset;
        aimAngle = other.aimAngle;
        paHealth = other.paHealth;
        stats = other.stats;
        noiseBlankerCooldown = other.noiseBlankerCooldown;
        lapsCompleted = other.lapsCompleted;
        lastLapAngle = other.lastLapAngle;
        collectedChars = std::move(other.collectedChars);
        hamSpiritBonusAchieved = other.hamSpiritBonusAchieved;
        morseMissCount = other.morseMissCount;
        prevNoiseBlankerBtn = other.prevNoiseBlankerBtn;
        prevTunerY = other.prevTunerY;
        prevTunerX = other.prevTunerX;
        prevTunerB = other.prevTunerB;
        prevTunerA = other.prevTunerA;
        tunerHoldTimerY = other.tunerHoldTimerY;
        tunerHoldTimerX = other.tunerHoldTimerX;
        tunerHoldTimerB = other.tunerHoldTimerB;
        tunerHoldTimerA = other.tunerHoldTimerA;
        prevTunerCheat = other.prevTunerCheat;
        prevDpadUp = other.prevDpadUp;
        prevDpadDown = other.prevDpadDown;
        prevDpadLeft = other.prevDpadLeft;
        prevDpadRight = other.prevDpadRight;
        currentBandName = std::move(other.currentBandName);
        collisionCooldown = other.collisionCooldown;
        collisionSpeedDelta = other.collisionSpeedDelta;
        collisionLateralPush = other.collisionLateralPush;
        collisionSoundFrames = other.collisionSoundFrames;
        collisionIntensity = other.collisionIntensity;
        collisionPan = other.collisionPan;
        hasBrailleContext = other.hasBrailleContext;
        trackPositionMeters = other.trackPositionMeters;
        lateralPositionMeters = other.lateralPositionMeters;
        morseCannonActive = other.morseCannonActive;
        morseCannonIsDash = other.morseCannonIsDash;
        morseCannonTimestamp = other.morseCannonTimestamp;
        audioState = std::move(other.audioState);
        audioBuf = std::move(other.audioBuf);
        mixBuf = std::move(other.mixBuf);
        roughnessPhase = other.roughnessPhase;
        rattlePhase = other.rattlePhase;
        morsePatternPhase = other.morsePatternPhase;
        smoothedPan = other.smoothedPan;
        swrAlertPhase = other.swrAlertPhase;
        std::memcpy(enginePhases, other.enginePhases, sizeof(enginePhases));
        std::memcpy(morsePhases, other.morsePhases, sizeof(morsePhases));
        std::memcpy(pairCollisionCooldown, other.pairCollisionCooldown, sizeof(pairCollisionCooldown));
        other.audioBackend = nullptr;
        other.stats = nullptr;
    }
    return *this;
}

// ─── SpatialPlayerAudio ─────────────────────────────────────────────────────

SpatialPlayerAudio::SpatialPlayerAudio() = default;

SpatialRelation SpatialPlayerAudio::calculate(
    float listenerAngle, float listenerLateral, float listenerSpeed,
    float sourceAngle,   float sourceLateral,   float sourceSpeed,
    float trackRadiusMeters) const
{
    SpatialRelation rel;

    // Convert angular positions to linear positions on circular track
    float listenerPos = listenerAngle * trackRadiusMeters;
    float sourcePos   = sourceAngle * trackRadiusMeters;

    // Angular difference (shortest path around the circle)
    float angleDiff = sourceAngle - listenerAngle;
    // Normalize to [-π, +π]
    while (angleDiff >  M_PI) angleDiff -= 2.0f * static_cast<float>(M_PI);
    while (angleDiff < -M_PI) angleDiff += 2.0f * static_cast<float>(M_PI);

    // Distance along track (arc length)
    float trackDist = std::abs(angleDiff) * trackRadiusMeters;

    // Lateral distance
    float lateralDist = (sourceLateral - listenerLateral) * TRACK_WIDTH_METERS * 0.5f;

    // Total Euclidean distance
    rel.distance = std::sqrt(trackDist * trackDist + lateralDist * lateralDist);
    rel.distance = std::max(MIN_DISTANCE, rel.distance);

    // Bearing: positive = source is to the right, negative = to the left
    // Combine track-ahead/behind with lateral offset for stereo imaging
    if (rel.distance > MIN_DISTANCE) {
        // Pan is primarily determined by lateral offset
        // but also influenced by track position (slightly left/right when ahead/behind)
        rel.bearing = std::atan2(lateralDist, angleDiff * trackRadiusMeters);
    }
    rel.pan = std::sin(rel.bearing);
    rel.pan = std::max(-1.0f, std::min(1.0f, rel.pan));

    // Volume attenuation — inverse-square law with clamping
    if (rel.distance >= MAX_AUDIBLE_DISTANCE) {
        rel.volume = 0.0f;
    } else {
        rel.volume = REFERENCE_DISTANCE / (REFERENCE_DISTANCE + rel.distance);
        // Smooth falloff near max distance
        float falloff = 1.0f - (rel.distance / MAX_AUDIBLE_DISTANCE);
        rel.volume *= falloff * falloff;
    }

    // Doppler effect — calculate relative speed along the line connecting the two players
    // Convert angular speeds to linear speeds (m/s)
    float listenerSpeedMs = listenerSpeed * trackRadiusMeters;
    float sourceSpeedMs   = sourceSpeed * trackRadiusMeters;

    // Radial velocity: positive = approaching, negative = receding
    // Project velocities onto the line connecting the two players
    float cosAngle = (angleDiff * trackRadiusMeters) / std::max(MIN_DISTANCE, rel.distance);
    cosAngle = std::max(-1.0f, std::min(1.0f, cosAngle));

    float listenerRadialSpeed = listenerSpeedMs * cosAngle;
    float sourceRadialSpeed   = sourceSpeedMs * cosAngle;

    // Doppler formula: f_observed = f_source * (v_sound + v_listener) / (v_sound + v_source)
    // Where v_listener is positive when moving toward source
    // and v_source is positive when moving away from listener
    float vNumerator   = SPEED_OF_SOUND + listenerRadialSpeed;
    float vDenominator = SPEED_OF_SOUND + sourceRadialSpeed;

    // Guard against division by zero or extreme values
    if (std::abs(vDenominator) < 1.0f) {
        vDenominator = 1.0f;  // Always positive to avoid sign-flip artifacts
    }

    rel.dopplerFactor = vNumerator / vDenominator;
    // Clamp to reasonable range (avoid extreme pitch shifts)
    rel.dopplerFactor = std::max(0.5f, std::min(2.0f, rel.dopplerFactor));

    rel.relativeSpeed = sourceRadialSpeed - listenerRadialSpeed;

    return rel;
}

float SpatialPlayerAudio::applyDoppler(float sourceFreqHz, float dopplerFactor) {
    return sourceFreqHz * dopplerFactor;
}

void SpatialPlayerAudio::applySpatialEffect(
    int16_t* buffer,
    int sampleCount,
    const SpatialRelation& relation) const
{
    if (!buffer || sampleCount <= 0) return;

    float vol = relation.volume;
    float pan = relation.pan;

    // Convert pan (-1..+1) to left/right gain
    // Equal-power panning
    float angle = (pan + 1.0f) * 0.25f * static_cast<float>(M_PI); // 0..π/2
    float leftGain  = vol * std::cos(angle);
    float rightGain = vol * std::sin(angle);

    // Apply to interleaved stereo buffer
    for (int i = 0; i < sampleCount; i++) {
        int idx = i * 2;
        buffer[idx]     = static_cast<int16_t>(buffer[idx]     * leftGain);
        buffer[idx + 1] = static_cast<int16_t>(buffer[idx + 1] * rightGain);
    }
}

// ─── TTS multi-output support check ────────────────────────────────────────

bool ttsSupportsMultiOutput(int ttsEngineType) {
    TTSEngineType type = static_cast<TTSEngineType>(ttsEngineType);
    switch (type) {
        case TTSEngineType::ESPEAK_NG:
            // espeak-ng can be directed to specific audio devices via environment
            return true;
        case TTSEngineType::MACOS_SAY:
            // macOS 'say' command can use --audio-device flag (macOS 12+)
            return true;
        case TTSEngineType::WINDOWS_SAPI:
            // SAPI can be configured per audio output
            return true;
        case TTSEngineType::NVDA:
        case TTSEngineType::MACOS_VOICEOVER:
            // Screen readers output to their own audio context — not multi-output capable
            return false;
        default:
            return false;
    }
}

// ─── MultiplayerManager ────────────────────────────────────────────────────

MultiplayerManager::MultiplayerManager()
    : spatialAudio(std::make_unique<SpatialPlayerAudio>()) {
    std::memset(spatialRelations, 0, sizeof(spatialRelations));
}

MultiplayerManager::~MultiplayerManager() {
    shutdown();
}

void MultiplayerManager::setLogger(Logger* logger) {
    debugLogger = logger;
    // Forward to all owned backends
    for (auto& backend : ownedBackends) {
        if (backend) backend->setLogger(logger);
    }
}

// Internal helper: log via Logger if available, otherwise discard.
static void multiLog(Logger* logger, const char* fmt, ...) {
    if (!logger) return;
    va_list args;
    va_start(args, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    logger->log("MULTIPLAYER", std::string(buf));
}

bool MultiplayerManager::initialize(
    const MultiplayerConfig& cfg,
    const std::vector<TrackPoint>& /*track*/)
{
    multiLog(debugLogger, "initialize() called, playerCount=%d", cfg.playerCount);
    config = cfg;
    players.clear();
    ownedBackends.clear();
    playerStats.clear();
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        eventQueue.clear();
    }

    // Create per-player GameStats objects
    for (int i = 0; i < config.playerCount; i++) {
        playerStats.push_back(std::make_unique<GameStats>());
    }

    for (int i = 0; i < config.playerCount; i++) {
        multiLog(debugLogger, "Setting up player %d: deviceIndex=%d",
                     i, config.audioDeviceIndices[i]);
        PlayerContext ctx;
        ctx.playerIndex = i;
        ctx.inputAssignment = config.inputAssignments[i];
        ctx.audioDeviceIndex = config.audioDeviceIndices[i];
        ctx.hasBrailleContext = (i == config.braillePlayerIndex);
        ctx.callsign = config.playerCallsigns[i];
        ctx.playerName = config.playerNames[i];

        // Assign per-player stats (owned by playerStats vector)
        ctx.stats = playerStats[i].get();

        // Each player gets their own antenna network for individual tuning
        ctx.antennaNetwork = std::make_unique<AntennaNetwork>();

        // Distribute starting lateral positions side-by-side across the start line.
        // All players start at the same track angle (0) but with different lateral offsets.
        // Range [-0.6, +0.6] to leave margin from track edges.
        if (config.playerCount > 1) {
            float span = 1.2f; // total lateral spread
            float step = span / static_cast<float>(config.playerCount - 1);
            ctx.playerLateralOffset = -0.6f + step * i;
        } else {
            ctx.playerLateralOffset = 0.0f;
        }

        // Audio backend allocation: Player 0 does NOT get its own audio backend
        // here — the game's primary audioBackend already serves player 0.
        // This is purely an audio device management decision, NOT a game logic
        // privilege. In the server-authoritative model, all players are
        // treated identically for game state — the audio routing distinction
        // is a rendering/presentation concern only.
        // 
        // Creating a second backend for the same device on Windows causes
        // MMSYSERR_ALLOCATED (device already open), and on macOS/Linux it
        // wastes resources.
        if (i > 0) {
            multiLog(debugLogger, "Creating audio backend for player %d", i);
            IAudioBackend* backend = createAudioBackend();
            if (backend && backend->initialize()) {
                backend->setLogger(debugLogger);
                // Route audio to the selected device for this player
                if (ctx.audioDeviceIndex >= 0) {
                    multiLog(debugLogger, "Calling selectDevice(%d) for player %d",
                                 ctx.audioDeviceIndex, i);
                    if (!backend->selectDevice(ctx.audioDeviceIndex)) {
                        multiLog(debugLogger, "Failed to select audio device %d for player %d",
                                     ctx.audioDeviceIndex, i);
                    } else {
                        multiLog(debugLogger, "selectDevice(%d) succeeded for player %d",
                                     ctx.audioDeviceIndex, i);
                    }
                }
                ctx.audioBackend = backend;
                ownedBackends.emplace_back(backend);
            } else {
                multiLog(debugLogger, "Failed to create audio backend for player %d", i);
                delete backend;
            }
        }
        // Player 0's audioBackend is left null — the game's own
        // audioBackend handles playback for player 0.

        players.push_back(std::move(ctx));
        multiLog(debugLogger, "Player %d setup complete", i);
    }

    multiLog(debugLogger, "Initialized %d player(s)", config.playerCount);
    return true;
}

void MultiplayerManager::shutdown() {
    players.clear();
    playerStats.clear();
    // Audio backends are destroyed via ownedBackends unique_ptrs
    for (auto& backend : ownedBackends) {
        if (backend) backend->shutdown();
    }
    ownedBackends.clear();
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        eventQueue.clear();
    }
}

PlayerContext* MultiplayerManager::getPlayer(int index) {
    if (index < 0 || index >= static_cast<int>(players.size())) return nullptr;
    return &players[index];
}

const PlayerContext* MultiplayerManager::getPlayer(int index) const {
    if (index < 0 || index >= static_cast<int>(players.size())) return nullptr;
    return &players[index];
}

void MultiplayerManager::pushEvent(const GameEvent& event) {
    std::lock_guard<std::mutex> lock(eventMutex);
    eventQueue.push_back(event);
    // Prevent unbounded growth
    while (eventQueue.size() > MAX_EVENT_QUEUE_SIZE) {
        eventQueue.pop_front();
    }
}

std::vector<GameEvent> MultiplayerManager::consumeEvents() {
    std::lock_guard<std::mutex> lock(eventMutex);
    std::vector<GameEvent> events(eventQueue.begin(), eventQueue.end());
    eventQueue.clear();
    return events;
}

GameStats* MultiplayerManager::getPlayerStats(int playerIndex) {
    if (playerIndex < 0 || playerIndex >= static_cast<int>(playerStats.size())) return nullptr;
    return playerStats[playerIndex].get();
}

SpatialRelation MultiplayerManager::calculateSpatialRelation(
    int listenerIndex, int sourceIndex) const
{
    std::lock_guard<std::mutex> lock(spatialMutex);
    if (listenerIndex < 0 || listenerIndex >= config.playerCount ||
        sourceIndex < 0 || sourceIndex >= config.playerCount) {
        return SpatialRelation();
    }
    return spatialRelations[listenerIndex * MAX_PLAYERS + sourceIndex];
}

void MultiplayerManager::updateSpatialAudio(float trackRadiusMeters) {
    trackRadiusM = trackRadiusMeters;

    std::lock_guard<std::mutex> lock(spatialMutex);
    for (int listener = 0; listener < config.playerCount; listener++) {
        for (int source = 0; source < config.playerCount; source++) {
            if (listener == source) {
                // Self-relation: full volume, center pan, no Doppler
                auto& rel = spatialRelations[listener * MAX_PLAYERS + source];
                rel.distance = 0.0f;
                rel.pan = 0.0f;
                rel.volume = 1.0f;
                rel.dopplerFactor = 1.0f;
                continue;
            }

            const auto& l = players[listener];
            const auto& s = players[source];

            spatialRelations[listener * MAX_PLAYERS + source] =
                spatialAudio->calculate(
                    l.playerAngle, l.playerLateralOffset, l.playerSpeed,
                    s.playerAngle, s.playerLateralOffset, s.playerSpeed,
                    trackRadiusMeters);
        }
    }
}

void MultiplayerManager::getPlayerViewport(
    int playerIndex,
    float& outX, float& outY,
    float& outW, float& outH) const
{
    int n = config.playerCount;

    if (n <= 1) {
        // Single player: full screen
        outX = 0.0f; outY = 0.0f; outW = 1.0f; outH = 1.0f;
        return;
    }

    if (n == 2) {
        if (config.splitOrientation == SplitOrientation::HORIZONTAL) {
            // Side by side
            outW = 0.5f; outH = 1.0f;
            outX = playerIndex * 0.5f; outY = 0.0f;
        } else {
            // Top/bottom
            outW = 1.0f; outH = 0.5f;
            outX = 0.0f; outY = playerIndex * 0.5f;
        }
        return;
    }

    if (n == 3) {
        if (config.splitOrientation == SplitOrientation::HORIZONTAL) {
            // Three columns
            outW = 1.0f / 3.0f; outH = 1.0f;
            outX = playerIndex * (1.0f / 3.0f); outY = 0.0f;
        } else {
            // Three rows
            outW = 1.0f; outH = 1.0f / 3.0f;
            outX = 0.0f; outY = playerIndex * (1.0f / 3.0f);
        }
        return;
    }

    // 4 players: 2×2 grid (no split orientation choice)
    outW = 0.5f; outH = 0.5f;
    outX = (playerIndex % 2) * 0.5f;
    outY = (playerIndex / 2) * 0.5f;
}

std::vector<AudioDeviceInfo> MultiplayerManager::enumerateAudioDevices() {
    std::vector<AudioDeviceInfo> devices;

#if defined(PLATFORM_MACOS) || defined(PLATFORM_LINUX)
    // Use PortAudio device enumeration.
    // PortAudio uses reference counting internally — Pa_Initialize/Pa_Terminate
    // are safe to nest. We init+terminate locally so enumeration works even
    // when no audio backend has been created yet, and the matching Terminate
    // does not tear down streams opened by other backends.
    PaError err = Pa_Initialize();
    if (err != paNoError) return devices;

    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxOutputChannels < 2) continue;

        AudioDeviceInfo dev;
        dev.deviceIndex = i;
        dev.name = info->name ? info->name : "Unknown Device";
        dev.maxOutputChannels = info->maxOutputChannels;
        dev.defaultSampleRate = static_cast<int>(info->defaultSampleRate);
        devices.push_back(dev);
    }

    Pa_Terminate();
#endif

#ifdef PLATFORM_WINDOWS
    // Use waveOut device enumeration — indices match those used by the audio backend's
    // selectDevice() which calls waveOutOpen() with these same device IDs
    {
        UINT numDevs = waveOutGetNumDevs();
        for (UINT i = 0; i < numDevs; i++) {
            WAVEOUTCAPSA caps = {};
            if (waveOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
                AudioDeviceInfo dev;
                dev.deviceIndex = static_cast<int>(i);
                dev.name = caps.szPname;
                dev.maxOutputChannels = caps.wChannels;
                dev.defaultSampleRate = 44100;
                devices.push_back(dev);
            }
        }
    }
#endif

    return devices;
}

bool MultiplayerManager::hasEnoughAudioDevices(int requiredCount) {
    auto devices = enumerateAudioDevices();
    return static_cast<int>(devices.size()) >= requiredCount;
}

// ─── Player-to-player collision detection and physics ───────────────────────

std::vector<MultiplayerManager::CollisionResult>
MultiplayerManager::checkPlayerCollisions(float trackRadiusMeters, float dt) {
    std::vector<CollisionResult> results;

    if (config.playerCount < 2) return results;

    for (int a = 0; a < config.playerCount; a++) {
        for (int b = a + 1; b < config.playerCount; b++) {
            auto& pA = players[a];
            auto& pB = players[b];

            // Skip if this specific pair is on collision cooldown
            if (pA.pairCollisionCooldown[b] > 0.0f) {
                pA.pairCollisionCooldown[b] = std::max(0.0f, pA.pairCollisionCooldown[b] - dt);
                pB.pairCollisionCooldown[a] = std::max(0.0f, pB.pairCollisionCooldown[a] - dt);
                continue;
            }

            // Calculate distance between players
            float angleDiff = pB.playerAngle - pA.playerAngle;
            while (angleDiff >  static_cast<float>(M_PI)) angleDiff -= 2.0f * static_cast<float>(M_PI);
            while (angleDiff < -static_cast<float>(M_PI)) angleDiff += 2.0f * static_cast<float>(M_PI);

            float trackDist = std::abs(angleDiff) * trackRadiusMeters;
            float lateralDist = std::abs(pB.playerLateralOffset - pA.playerLateralOffset)
                                * SpatialPlayerAudio::TRACK_WIDTH_METERS * 0.5f;

            float totalDist = std::sqrt(trackDist * trackDist + lateralDist * lateralDist);

            // Check if within collision threshold
            if (totalDist > COLLISION_DISTANCE) continue;

            // ── Collision detected! ──

            CollisionResult cr;
            cr.collided = true;

            // Relative speed at impact
            float speedA = pA.playerSpeed * trackRadiusMeters;
            float speedB = pB.playerSpeed * trackRadiusMeters;
            cr.impactSpeed = std::abs(speedA - speedB);

            // Impact angle (0 = head-on, π/2 = side-swipe)
            if (totalDist > 0.01f) {
                cr.impactAngle = std::atan2(lateralDist, trackDist);
            }

            // Damage proportional to impact speed and head-on-ness
            float headOnFactor = std::cos(cr.impactAngle);
            headOnFactor = std::max(0.2f, std::abs(headOnFactor)); // minimum damage for glancing
            cr.damage = std::min(1.0f, cr.impactSpeed * COLLISION_DAMAGE_FACTOR * headOnFactor);

            // ── Speed exchange (elastic collision) ──
            // Simplified 1D elastic collision with restitution
            float avgSpeed = (speedA + speedB) * 0.5f;
            float relSpeed = speedA - speedB;
            float newSpeedA = avgSpeed - relSpeed * COLLISION_ELASTICITY * 0.5f;
            float newSpeedB = avgSpeed + relSpeed * COLLISION_ELASTICITY * 0.5f;

            cr.speedDeltaA = (newSpeedA - speedA) / std::max(1.0f, trackRadiusMeters);
            cr.speedDeltaB = (newSpeedB - speedB) / std::max(1.0f, trackRadiusMeters);

            // ── Lateral push ──
            float lateralSign = (pB.playerLateralOffset > pA.playerLateralOffset) ? 1.0f : -1.0f;
            float pushMagnitude = COLLISION_PUSH_FACTOR * (cr.impactSpeed / 20.0f);
            pushMagnitude = std::min(0.5f, pushMagnitude);

            cr.pushAngleA = -lateralSign * pushMagnitude; // Push A away from B
            cr.pushAngleB =  lateralSign * pushMagnitude; // Push B away from A

            // Collision sound properties
            cr.intensity = std::min(1.0f, cr.impactSpeed / 30.0f);
            // Pan from perspective of mid-point
            cr.pan = 0.0f;

            // ── Apply physics to player state ──
            pA.playerSpeed += cr.speedDeltaA;
            pB.playerSpeed += cr.speedDeltaB;

            // Clamp speeds to non-negative
            pA.playerSpeed = std::max(0.0f, pA.playerSpeed);
            pB.playerSpeed = std::max(0.0f, pB.playerSpeed);

            // Apply lateral push
            pA.playerLateralOffset += cr.pushAngleA;
            pB.playerLateralOffset += cr.pushAngleB;

            // Clamp lateral to track bounds
            pA.playerLateralOffset = std::max(-1.0f, std::min(1.0f, pA.playerLateralOffset));
            pB.playerLateralOffset = std::max(-1.0f, std::min(1.0f, pB.playerLateralOffset));

            // Apply PA damage
            pA.paHealth = std::max(0.0f, pA.paHealth - cr.damage * 0.5f);
            pB.paHealth = std::max(0.0f, pB.paHealth - cr.damage * 0.5f);
            // Sync PA damage to per-player stats
            if (pA.stats) pA.stats->paHealth = pA.paHealth;
            if (pB.stats) pB.stats->paHealth = pB.paHealth;

            // Set collision state for audio and pair-specific cooldowns
            pA.pairCollisionCooldown[b] = COLLISION_COOLDOWN;
            pB.pairCollisionCooldown[a] = COLLISION_COOLDOWN;
            // Legacy global cooldown kept for audio state queries (collisionSoundFrames > 0)
            pA.collisionCooldown = COLLISION_COOLDOWN;
            pB.collisionCooldown = COLLISION_COOLDOWN;
            pA.collisionSoundFrames = static_cast<int>(44100 * 0.3f); // 300ms collision sound
            pB.collisionSoundFrames = static_cast<int>(44100 * 0.3f);
            pA.collisionIntensity = cr.intensity;
            pB.collisionIntensity = cr.intensity;

            // Pan: from each player's perspective, collision comes from direction of other player
            float panDir = (pB.playerLateralOffset > pA.playerLateralOffset) ? 0.5f : -0.5f;
            pA.collisionPan =  panDir;
            pB.collisionPan = -panDir;

            multiLog(debugLogger, "Collision: P%d vs P%d, speed=%.1f m/s, damage=%.2f",
                          a + 1, b + 1, cr.impactSpeed, cr.damage);

            results.push_back(cr);
        }
    }

    // Decay cooldowns for non-colliding players
    for (int i = 0; i < config.playerCount; i++) {
        players[i].collisionCooldown = std::max(0.0f, players[i].collisionCooldown - dt);
        for (int j = 0; j < config.playerCount; j++) {
            players[i].pairCollisionCooldown[j] = std::max(0.0f, players[i].pairCollisionCooldown[j] - dt);
        }
    }

    return results;
}

// ─── Per-player independent audio generation ────────────────────────────────
//
// Each player gets a fully independent audio pipeline rendered from their own
// perspective.  This is architecturally equivalent to what an online game client
// would do: each client renders its own audio from its own game state.
//
// The game loop computes per-player AudioParams (motor freq, SWR, morse signals,
// noise enemies, etc.) based on each player's position.  The audio thread then
// calls generateAndPlayPlayerAudio() for each secondary player to render and
// play a complete audio buffer using those params — just like player 0's audio
// is rendered by Game::audioThreadFunc().

// Helper: clamp to int16 range
static inline int16_t clampI16(int32_t v) {
    return static_cast<int16_t>(std::max(-32768, std::min(32767, static_cast<int>(v))));
}

// Helper: mix one buffer into another
static void mixBuffers(std::vector<int16_t>& dst, const std::vector<int16_t>& src, size_t count) {
    for (size_t i = 0; i < count; i++) {
        int32_t mixed = static_cast<int32_t>(dst[i]) + static_cast<int32_t>(src[i]);
        dst[i] = clampI16(mixed);
    }
}

void MultiplayerManager::generateAndPlayPlayerAudio(
    int playerIndex,
    SynthesizerEngine* engine,
    int sampleRate, int samplesPerFrame,
    int channels, int bitsPerSample,
    float sharedMorsePhase,
    std::chrono::steady_clock::time_point frameStartTime,
    bool isPlaying)
{
    // Skip player 0 — their audio is rendered by the main Game::audioThreadFunc().
    // This is an audio rendering distinction, not a game logic privilege.
    // In the server-authoritative model, all players are identical for game state.
    // The audio pipeline split exists because player 0 uses the game's primary
    // audio backend while players 1+ have independent backends.
    if (playerIndex < 1 || playerIndex >= config.playerCount) return;
    auto& ctx = players[playerIndex];
    if (!ctx.audioBackend) return;

    const size_t bufSize = static_cast<size_t>(samplesPerFrame) * channels;

    // Ensure per-player scratch buffers are allocated
    if (ctx.audioBuf.size() < bufSize) ctx.audioBuf.resize(bufSize, 0);
    if (ctx.mixBuf.size() < bufSize)   ctx.mixBuf.resize(bufSize, 0);

    // Clear main buffer
    std::fill(ctx.audioBuf.begin(), ctx.audioBuf.end(), 0);

    // When the game is not in PLAYING state, output silence — matching
    // Player 0's behaviour where gameplay layers are gated by isPlaying.
    // This prevents stale audioState values (motor tone, SWR alert, etc.)
    // from producing a continuous tone on secondary players' outputs
    // during menus, pause, or the transition into the race.
    if (!isPlaying) {
        ctx.audioBackend->playBuffer(ctx.audioBuf.data(), samplesPerFrame,
                                     sampleRate, channels, bitsPerSample);
        return;
    }

    // Read this player's audio params (written by game loop each frame)
    PlayerAudioParams params;
    {
        std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
        params = ctx.audioState;
    }

    // Smooth pan to prevent crackle
    ctx.smoothedPan += 0.3f * (params.pan - ctx.smoothedPan);
    double panFraction = static_cast<double>(ctx.smoothedPan + 1.0f) * 0.5;
    panFraction = std::max(0.0, std::min(1.0, panFraction));

    // ── Layer 1: Motor sound ──
    if (params.motorVolume > 0.0f) {
        engine->generateAudio(ctx.audioBuf, samplesPerFrame, 0,
                              static_cast<double>(params.motorFreq), panFraction,
                              static_cast<int>(params.motorVolume));

        // Road roughness modulation (same logic as player 0)
        if (params.motorRoughness > 0.05f) {
            float roughness = params.motorRoughness;
            float modRate = 15.0f + 45.0f * roughness;
            float phaseInc = modRate * 2.0f * static_cast<float>(M_PI) / static_cast<float>(sampleRate);
            float modDepth = 0.7f * roughness;
            float rattle = roughness * roughness;
            float rattleRate = 80.0f + 120.0f * roughness;
            float rattleInc = rattleRate * 2.0f * static_cast<float>(M_PI) / static_cast<float>(sampleRate);

            for (int i = 0; i < samplesPerFrame; i++) {
                float mod = 1.0f - modDepth * (0.5f + 0.5f * std::sin(ctx.roughnessPhase));
                ctx.roughnessPhase += phaseInc;
                float rattleMod = 1.0f - rattle * 0.3f * (0.5f + 0.5f * std::sin(ctx.rattlePhase));
                ctx.rattlePhase += rattleInc;
                float combinedMod = mod * rattleMod;
                int idx = i * channels;
                ctx.audioBuf[idx] = static_cast<int16_t>(ctx.audioBuf[idx] * combinedMod);
                if (channels > 1)
                    ctx.audioBuf[idx + 1] = static_cast<int16_t>(ctx.audioBuf[idx + 1] * combinedMod);
            }
            if (ctx.roughnessPhase > 2.0f * static_cast<float>(M_PI)) ctx.roughnessPhase -= 2.0f * static_cast<float>(M_PI);
            if (ctx.rattlePhase > 2.0f * static_cast<float>(M_PI)) ctx.rattlePhase -= 2.0f * static_cast<float>(M_PI);
        }
    }

    // ── Layer 2: SWR alert ──
    if (params.swrAlertActive && params.swrVolume > 1.0f) {
        float beepHz = 1.0f + 5.0f * params.swrAlertRate;
        float frameSec = static_cast<float>(samplesPerFrame) / static_cast<float>(sampleRate);
        ctx.swrAlertPhase += beepHz * frameSec * 2.0f * static_cast<float>(M_PI);
        if (ctx.swrAlertPhase > 2.0f * static_cast<float>(M_PI))
            ctx.swrAlertPhase -= 2.0f * static_cast<float>(M_PI);

        bool beepOn = (std::sin(ctx.swrAlertPhase) > 0.0f);
        if (beepOn) {
            std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
            engine->generateAudio(ctx.mixBuf, samplesPerFrame, 1,
                                  static_cast<double>(params.swrFreq), 0.5,
                                  static_cast<int>(params.swrVolume));
            mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        }
    }

    // ── Layer 3: Morse signal beeps ──
    {
        // Use the shared global morse pattern phase from player 0's audio
        // thread so that all players hear world morse signals at the same
        // point in their repeating pattern (prevents inter-player offset).
        float frameSec = static_cast<float>(samplesPerFrame) / static_cast<float>(sampleRate);
        ctx.morsePatternPhase += frameSec;
        float effectivePhase = (sharedMorsePhase > 0.0f) ? sharedMorsePhase : ctx.morsePatternPhase;

        // Morse timing constants (must match player 0's MorseCannon constants)
        static constexpr float DOT_DURATION = 0.1f;
        static constexpr float DASH_DURATION = 0.3f;
        static constexpr float ELEMENT_SPACE = 0.1f;
        static constexpr float PATTERN_REPEAT_PAUSE = 0.4f;
        static constexpr float MORSE_SIGNAL_FREQ = 600.0f;

        for (const auto& sig : params.morseSignals) {
            if (sig.volume < 1 || sig.pattern.empty()) continue;

            float patternLen = 0.0f;
            for (char c : sig.pattern) {
                patternLen += (c == '.') ? DOT_DURATION : DASH_DURATION;
                patternLen += ELEMENT_SPACE;
            }
            patternLen += PATTERN_REPEAT_PAUSE;

            float posInPattern = std::fmod(effectivePhase, patternLen);

            bool soundOn = false;
            float pos = 0.0f;
            for (char c : sig.pattern) {
                float elemLen = (c == '.') ? DOT_DURATION : DASH_DURATION;
                if (posInPattern >= pos && posInPattern < pos + elemLen) {
                    soundOn = true;
                    break;
                }
                pos += elemLen + ELEMENT_SPACE;
            }

            if (soundOn) {
                std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
                engine->generateAudio(ctx.mixBuf, samplesPerFrame, 2,
                                      static_cast<double>(MORSE_SIGNAL_FREQ),
                                      static_cast<double>(sig.pan), sig.volume);
                mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
            }
        }
    }

    // ── Layer 4: Morse cannon sidetone (this player's own keying) ──
    // Uses direct square wave generation (same as player 0's audio thread)
    // to avoid shared SynthesizerEngine phase interference.
    if (params.morseCannonActive) {
        static constexpr float MORSE_SIDETONE_FREQ = 600.0f;
        float invSR = 1.0f / static_cast<float>(sampleRate);
        for (int i = 0; i < samplesPerFrame; i++) {
            ctx.morseCannonPhase += MORSE_SIDETONE_FREQ * invSR;
            if (ctx.morseCannonPhase >= 1.0f) ctx.morseCannonPhase -= 1.0f;
            // Square wave for authentic CW sidetone (matching player 0)
            float sample = (ctx.morseCannonPhase < 0.5f) ? 1.0f : -1.0f;
            int16_t mixSample = static_cast<int16_t>(sample * 3000.0f);
            int idx = i * channels;
            ctx.audioBuf[idx]     = clampI16(static_cast<int32_t>(ctx.audioBuf[idx])     + mixSample);
            if (channels > 1)
                ctx.audioBuf[idx + 1] = clampI16(static_cast<int32_t>(ctx.audioBuf[idx + 1]) + mixSample);
        }
    } else {
        ctx.morseCannonPhase = 0.0f;  // Reset phase when tone stops for clean restart
    }

    // ── Layer 5: Noise enemies ──
    // Match Player 0's Layer 21: frame-varying frequency for static/hum effect
    for (const auto& ne : params.noiseEnemies) {
        if (ne.volume < 1) continue;
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float noiseFreq = 60.0f + (ctx.audioFrameCount % 5) * 40.0f + ne.intensity * 200.0f;
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                              static_cast<double>(noiseFreq),
                              static_cast<double>(ne.pan),
                              static_cast<int>(ne.volume * params.enemyVolume));
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
    }

    // ── Layer 6: QSO Störer ──
    // Match Player 0's Layer 31: hornet buzz with psychoacoustic low-pass when behind
    if (params.qsoStoererActive && params.qsoStoererVolume > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float stoererFreq = params.qsoStoererBuzzFreq > 0.0f ? params.qsoStoererBuzzFreq : 350.0f;
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                              static_cast<double>(stoererFreq),
                              static_cast<double>(0.5f + params.qsoStoererPan * 0.5f),
                              static_cast<int>(params.qsoStoererVolume * params.enemyVolume));
        // Apply psychoacoustic low-pass when Störer is behind the player
        // (matches Player 0's head-shadow IIR filter)
        if (params.qsoStoererBehind) {
            static constexpr float LP_ALPHA = 0.35f;
            int16_t prevL = 0, prevR = 0;
            for (int i = 0; i < samplesPerFrame; i++) {
                int idx = i * channels;
                int16_t filtL = static_cast<int16_t>(LP_ALPHA * ctx.mixBuf[idx] + (1.0f - LP_ALPHA) * prevL);
                prevL = filtL;
                ctx.mixBuf[idx] = filtL;
                if (channels > 1) {
                    int16_t filtR = static_cast<int16_t>(LP_ALPHA * ctx.mixBuf[idx + 1] + (1.0f - LP_ALPHA) * prevR);
                    prevR = filtR;
                    ctx.mixBuf[idx + 1] = filtR;
                }
            }
        }
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
    }

    // ── Layer 7: Power-up zone ambient ──
    // Match Player 0's Layer 35: per-type waveform with ascending arpeggio
    {
        bool anyActive = false;
        for (const auto& pz : params.powerUpZones) {
            if (!pz.inZone || pz.volume < 1) continue;
            anyActive = true;
            std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
            // Distinct timbre per power-up type (matching Player 0)
            int wave = 1;
            float baseFreq = 523.25f;
            switch (pz.type) {
                case 0: wave = 1; baseFreq = 523.25f; break; // SPEED_BOOST: bright sine arpeggio
                case 1: wave = 2; baseFreq = 880.0f;  break; // FIRE_RATE: square, urgent
                case 2: wave = 3; baseFreq = 660.0f;  break; // AUTO_FIRE: triangle, steady
                case 3: wave = 4; baseFreq = 440.0f;  break; // SWR_IMMUNITY: saw, gritty
                case 4: wave = 1; baseFreq = 392.0f;  break; // DURATION_EXTEND: warm lower
                default: break;
            }
            float puFreqs[] = {baseFreq, baseFreq * 1.25f, baseFreq * 1.5f, baseFreq * 2.0f};
            float freq = puFreqs[(ctx.puZonePhase / 4) % 4];
            int puVol = static_cast<int>(pz.volume * pz.zoneDepth * params.uiVolume);
            engine->generateAudio(ctx.mixBuf, samplesPerFrame, wave,
                                  static_cast<double>(freq),
                                  static_cast<double>(pz.pan), puVol);
            mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        }
        if (anyActive) ctx.puZonePhase++;
    }

    // ── Layer 8: PA damage distortion ──
    if (params.paDamageLevel > 0.1f) {
        float damageIntensity = (params.paDamageLevel - 0.1f) / 0.9f;
        int skipInterval = std::max(1, static_cast<int>(20 - 18 * damageIntensity));
        for (int i = 0; i < samplesPerFrame; i++) {
            if ((i % skipInterval) == 0) {
                int idx = i * channels;
                float noise = static_cast<float>((i * 7 + 13) % 256) / 128.0f - 1.0f;
                int distVol = static_cast<int>(noise * 30 * damageIntensity * 128.0f);
                ctx.audioBuf[idx] = clampI16(static_cast<int32_t>(ctx.audioBuf[idx]) + distVol);
                if (channels > 1)
                    ctx.audioBuf[idx + 1] = clampI16(static_cast<int32_t>(ctx.audioBuf[idx + 1]) + distVol);
            }
        }
    }

    // ── Layer 9: Border warning ──
    if (params.borderWarningSoundFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        int warnVol = static_cast<int>((40 + params.borderWarningIntensity * 80) * params.warningVolume);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                              100.0, static_cast<double>(params.borderWarningSide), warnVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.borderWarningSoundFrames > 0)
                ctx.audioState.borderWarningSoundFrames--;
        }
    }

    // ── Layer 10: Collision sound (this player's own collision) ──
    if (ctx.collisionSoundFrames > 0 && ctx.collisionIntensity > 0.0f) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float colPan = (ctx.collisionPan + 1.0f) * 0.5f;
        int colVol = static_cast<int>(ctx.collisionIntensity * 100 * params.collisionVolume);
        // Low-frequency thud + noise
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                              80.0, static_cast<double>(colPan), colVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        ctx.collisionSoundFrames = std::max(0, ctx.collisionSoundFrames - samplesPerFrame);
    }

    // ── Layer 10b: Noise blanker fire sound (this player's own fire) ──
    // Descending frequency sweep matching player 0's Layer 24
    if (params.noiseBlankerFireFrames > 0) {
        static constexpr float NB_START_FREQ = 3000.0f;   // Starting frequency of zap sweep
        static constexpr float NB_FREQ_STEP = 312.5f;     // Frequency drop per frame
        static constexpr int   NB_SWEEP_FRAMES = 8;       // Total frames in sweep
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float zapFreq = NB_START_FREQ - NB_FREQ_STEP * (NB_SWEEP_FRAMES - params.noiseBlankerFireFrames);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 3,
                              static_cast<double>(zapFreq), 0.5, 100);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.noiseBlankerFireFrames > 0)
                ctx.audioState.noiseBlankerFireFrames--;
        }
    }

    // ── Layer 10c: Collect chime (ascending arpeggio matching player 0's Layer 7) ──
    if (params.collectSoundFrames > 0) {
        static constexpr int   COLLECT_TOTAL_FRAMES = 8;  // msToFrames(160)
        static constexpr int   COLLECT_STEP_FRAMES  = 2;  // msToFrames(40) per note
        static constexpr float COLLECT_FREQS[4] = {523.25f, 659.25f, 783.99f, 1046.5f}; // C5 E5 G5 C6
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        int stepIdx = std::max(0, std::min(3, (COLLECT_TOTAL_FRAMES - params.collectSoundFrames) / COLLECT_STEP_FRAMES));
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 0,
                              static_cast<double>(COLLECT_FREQS[stepIdx]), 0.5, 90);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.collectSoundFrames > 0) ctx.audioState.collectSoundFrames--;
        }
    }

    // ── Layer 10d: PA repair chime (warm ascending arpeggio matching player 0's Layer 14) ──
    if (params.paRepairSoundFrames > 0) {
        static constexpr int   REPAIR_TOTAL_FRAMES = 8;  // msToFrames(160)
        static constexpr int   REPAIR_STEP_FRAMES  = 2;  // msToFrames(40) per note
        static constexpr float REPAIR_FREQS[4] = {392.0f, 493.9f, 587.3f, 784.0f}; // G4 B4 D5 G5
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        int stepIdx = std::max(0, std::min(3, (REPAIR_TOTAL_FRAMES - params.paRepairSoundFrames) / REPAIR_STEP_FRAMES));
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 0,
                              static_cast<double>(REPAIR_FREQS[stepIdx]), 0.5, 70);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.paRepairSoundFrames > 0) ctx.audioState.paRepairSoundFrames--;
        }
    }

    // ── Layer 10e: Miss morse (low buzz matching player 0's Layer 9) ──
    if (params.missMorseSoundFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4, 200.0, 0.5, 80);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.missMorseSoundFrames > 0) ctx.audioState.missMorseSoundFrames--;
        }
    }

    // ── Layer 10f: Miss aim (descending tone matching player 0's Layer 8) ──
    if (params.missAimSoundFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        // 5-frame sweep: starts at 600 Hz, drops 80 Hz per remaining frame
        float freq = 600.0f - 80.0f * (5 - params.missAimSoundFrames);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                              static_cast<double>(freq), 0.5, 70);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.missAimSoundFrames > 0) ctx.audioState.missAimSoundFrames--;
        }
    }

    // ── Layer 10g: Noise enemy hit (metallic impact matching player 0's Layer 25b) ──
    if (params.noiseHitSoundFrames > 0) {
        static constexpr int NB_HIT_SWEEP_FRAMES = 8;  // msToFrames(160)
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float hitBaseFreq = 1000.0f + static_cast<float>(params.noiseHitVariation) * 300.0f;
        float hitFreq = hitBaseFreq + static_cast<float>(NB_HIT_SWEEP_FRAMES - params.noiseHitSoundFrames) * 200.0f;
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 2,
                              static_cast<double>(hitFreq), 0.5, 150);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.noiseHitSoundFrames > 0) ctx.audioState.noiseHitSoundFrames--;
        }
    }

    // ── Layer 10i: PA damage crackle/pop (matching player 0's Layer 12) ──
    if (params.paDamageSoundFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float freq = (params.paDamageSoundFrames % 2 == 0) ? 100.0f : 2000.0f;
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                              static_cast<double>(freq), 0.5, 120);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.paDamageSoundFrames > 0) ctx.audioState.paDamageSoundFrames--;
        }
    }

    // ── Layer 10j: Noise enemy destroyed explosion (matching player 0's Layer 25) ──
    if (params.noiseDestroyedFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float explFreq = 150.0f + (10 - params.noiseDestroyedFrames) * 80.0f;
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                              static_cast<double>(explFreq), 0.5, 160);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.noiseDestroyedFrames > 0) ctx.audioState.noiseDestroyedFrames--;
        }
    }

    // ── Layer 10k: Emergency brake tire screech (matching player 0's Layer 26) ──
    if (params.emergencyBrakeSoundFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float screechFreq = 1500.0f + 400.0f * params.emergencyBrakeSoundFrames;
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                              static_cast<double>(screechFreq), 0.5, 160);
        // Add noise component for tire-on-asphalt realism
        unsigned int noiseSeed = 54321 + params.emergencyBrakeSoundFrames * 7;
        for (size_t i = 0; i < bufSize; i++) {
            noiseSeed = noiseSeed * 1103515245 + 12345;
            int16_t noise = static_cast<int16_t>((noiseSeed >> 16) & 0x7F) - 64;
            ctx.mixBuf[i] = clampI16(static_cast<int32_t>(ctx.mixBuf[i]) + noise * 3);
        }
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.emergencyBrakeSoundFrames > 0) ctx.audioState.emergencyBrakeSoundFrames--;
        }
    }

    // ── Layer 10l: Aim reset swoosh (matching player 0's Layer 27) ──
    if (params.aimResetSoundFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float swooshFreq = 400.0f + params.aimResetSoundFrames * 150.0f;
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 0,
                              static_cast<double>(swooshFreq), 0.5, 100);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.aimResetSoundFrames > 0) ctx.audioState.aimResetSoundFrames--;
        }
    }

    // ── Layer 10m: Border scrape (matching player 0's Layer 29) ──
    if (params.borderScrapeSoundFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float scrapeFreq = (ctx.audioFrameCount % 2 == 0) ? 800.0f : 1200.0f;
        int scrapeVol = static_cast<int>(30 * params.collisionVolume);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                              static_cast<double>(scrapeFreq),
                              static_cast<double>(params.borderCollisionSide), scrapeVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.borderScrapeSoundFrames > 0) ctx.audioState.borderScrapeSoundFrames--;
        }
    }

    // ── Layer 10n: Border crash (matching player 0's Layer 30) ──
    if (params.borderCrashSoundFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float crashFreq = 80.0f + (params.borderCrashSoundFrames > 2400 ? 200.0f : 0.0f);
        int crashVol = static_cast<int>(50 * params.collisionVolume);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                              static_cast<double>(crashFreq),
                              static_cast<double>(params.borderCollisionSide), crashVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.borderCrashSoundFrames > 0) ctx.audioState.borderCrashSoundFrames--;
        }
    }

    // ── Layer 10h: Aim lock indicators (matching player 0's Layer 20) ──
    // Four independent tones for each target type: morse, noise, störer, power-up.
    // Each plays only when the corresponding aim lock strength is above threshold.
    // Uses identical waveforms, frequencies, volumes, and intervals as Player 0.
    {
        const float lockMorse  = params.aimLockMorse;
        const float lockNoise  = params.aimLockNoise;
        const float lockStoer  = params.aimLockStoerer;
        const float lockPowerUp= params.aimLockPowerUp;

        // Morse aim lock: sawtooth Geiger-click pattern (waveform 4, 2500-3500 Hz)
        if (lockMorse > 0.01f) {
            int clickInterval = std::max(1, static_cast<int>(12.0f - 11.0f * lockMorse));
            if (ctx.audioFrameCount % clickInterval == 0) {
                std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
                float aimFreq = 2500.0f + 1000.0f * lockMorse;
                int aimVol = static_cast<int>(15 + 45 * lockMorse);
                engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                                      static_cast<double>(aimFreq), 0.5, aimVol);
                // Fade out last 60% of buffer (matching Player 0)
                size_t fadeStart = static_cast<size_t>(bufSize * 0.4);
                for (size_t i = fadeStart; i < bufSize; i++) ctx.mixBuf[i] = 0;
                mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
            }
        }

        // Noise aim lock: square wave pulse (waveform 2, 400-800 Hz)
        if (lockNoise > 0.01f) {
            int pulseInterval = std::max(1, static_cast<int>(10.0f - 9.0f * lockNoise));
            if (ctx.audioFrameCount % pulseInterval == 0) {
                std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
                float noiseAimFreq = 400.0f + 400.0f * lockNoise;
                int noiseAimVol = static_cast<int>(20 + 40 * lockNoise);
                engine->generateAudio(ctx.mixBuf, samplesPerFrame, 2,
                                      static_cast<double>(noiseAimFreq), 0.5, noiseAimVol);
                // Fade out last 50% of buffer (matching Player 0)
                size_t fadeStart = static_cast<size_t>(bufSize * 0.5);
                for (size_t i = fadeStart; i < bufSize; i++) ctx.mixBuf[i] = 0;
                mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
            }
        }

        // QSO Störer aim lock: triangle wave rapid pulse (waveform 3, 1000-1500 Hz)
        if (lockStoer > 0.01f) {
            int stoerInterval = std::max(1, static_cast<int>(8.0f - 7.0f * lockStoer));
            if (ctx.audioFrameCount % stoerInterval == 0) {
                std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
                float stoererAimFreq = 1000.0f + 500.0f * lockStoer;
                int stoererAimVol = static_cast<int>(20 + 50 * lockStoer);
                engine->generateAudio(ctx.mixBuf, samplesPerFrame, 3,
                                      static_cast<double>(stoererAimFreq), 0.5, stoererAimVol);
                // Fade out last 65% of buffer (matching Player 0)
                size_t fadeStart = static_cast<size_t>(bufSize * 0.35);
                for (size_t i = fadeStart; i < bufSize; i++) ctx.mixBuf[i] = 0;
                mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
            }
        }

        // Power-up aim lock: sine shimmer (waveform 1, 800-1200 Hz)
        if (lockPowerUp > 0.01f) {
            int puInterval = std::max(1, static_cast<int>(10.0f - 9.0f * lockPowerUp));
            if (ctx.audioFrameCount % puInterval == 0) {
                std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
                float puAimFreq = 800.0f + 400.0f * lockPowerUp;
                int puAimVol = static_cast<int>(15 + 40 * lockPowerUp);
                engine->generateAudio(ctx.mixBuf, samplesPerFrame, 1,
                                      static_cast<double>(puAimFreq), 0.5, puAimVol);
                // Fade out last 40% of buffer (matching Player 0)
                size_t fadeStart = static_cast<size_t>(bufSize * 0.6);
                for (size_t i = fadeStart; i < bufSize; i++) ctx.mixBuf[i] = 0;
                mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
            }
        }
    }

    // ── Layer 10o: Tuner adjustment sound (matching player 0's Layer 5) ──
    if (params.adjustSoundFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float baseFreq = params.adjustSoundUp ? 400.0f : 800.0f;
        float step = params.adjustSoundUp ? 80.0f : -80.0f;
        float freq = baseFreq + step * static_cast<float>(6 - params.adjustSoundFrames);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 0,
                              static_cast<double>(freq), static_cast<double>(params.adjustSoundPan), 80);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.adjustSoundFrames > 0) ctx.audioState.adjustSoundFrames--;
        }
    }

    // ── Layer 10p: Bumper sound (matching player 0's Layer 6) ──
    if (params.bumperSoundFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float bumpFreq = 200.0f + 150.0f * (1.0f - static_cast<float>(params.bumperSoundFrames) /
                         static_cast<float>(std::max(params.bumperSoundFrames, 4)));
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 2,
                              static_cast<double>(bumpFreq), 0.5, 70);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.bumperSoundFrames > 0) ctx.audioState.bumperSoundFrames--;
        }
    }

    // ── Layer 10q: QSO Störer collision impact (matching player 0's Layer 32) ──
    if (params.qsoStoererCollisionFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float collFreq = 150.0f;
        int collVol = static_cast<int>(80 * params.enemyVolume);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 4,
                              static_cast<double>(collFreq),
                              static_cast<double>(params.qsoStoererPan), collVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.qsoStoererCollisionFrames > 0) ctx.audioState.qsoStoererCollisionFrames--;
        }
    }

    // ── Layer 10r: QSO Störer overtake sweep (matching player 0's Layer 33) ──
    if (params.qsoStoererOvertakeFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float sweepProgress = 1.0f - static_cast<float>(params.qsoStoererOvertakeFrames) / 30.0f;
        float sweepFreq = 200.0f + 600.0f * sweepProgress;
        int sweepVol = static_cast<int>(50 * params.enemyVolume);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 3,
                              static_cast<double>(sweepFreq),
                              static_cast<double>(params.qsoStoererPan), sweepVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.qsoStoererOvertakeFrames > 0) ctx.audioState.qsoStoererOvertakeFrames--;
        }
    }

    // ── Layer 10s: Band crossing jingle (matching player 0's Layer 34) ──
    if (params.bandJingleFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float jingleProgress = 1.0f - static_cast<float>(params.bandJingleFrames) / 20.0f;
        float jingleFreq = params.bandJingleAscending
            ? (400.0f + 800.0f * jingleProgress)
            : (1200.0f - 800.0f * jingleProgress);
        int jingleVol = static_cast<int>(60 * params.uiVolume);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 0,
                              static_cast<double>(jingleFreq), 0.5, jingleVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.bandJingleFrames > 0) ctx.audioState.bandJingleFrames--;
        }
    }

    // ── Layer 10t: Traffic report whistle (matching player 0's Layer 17) ──
    if (params.trafficBeepFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float tProgress = 1.0f - static_cast<float>(params.trafficBeepFrames) / 15.0f;
        float tFreq = 800.0f + 400.0f * tProgress;
        int tVol = static_cast<int>(50 * params.uiVolume);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 0,
                              static_cast<double>(tFreq), 0.5, tVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.trafficBeepFrames > 0) ctx.audioState.trafficBeepFrames--;
        }
    }

    // ── Layer 10u: Power-up collection progress tone (matching player 0's Layer 36) ──
    if (params.powerUpCollecting && params.powerUpCollectProgress > 0.0f) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float puCollectFreq = 300.0f + 900.0f * params.powerUpCollectProgress;
        int puCollectVol = static_cast<int>((30 + 40 * params.powerUpCollectProgress) * params.uiVolume);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 0,
                              static_cast<double>(puCollectFreq), 0.5, puCollectVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
    }

    // ── Layer 10v: Power-up activation fanfare (matching player 0's Layer 37) ──
    if (params.powerUpActivateFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float actProgress = 1.0f - static_cast<float>(params.powerUpActivateFrames) / 20.0f;
        float actFreq = 500.0f + 500.0f * actProgress;
        int actVol = static_cast<int>(70 * params.uiVolume);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 0,
                              static_cast<double>(actFreq), 0.5, actVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.powerUpActivateFrames > 0) ctx.audioState.powerUpActivateFrames--;
        }
    }

    // ── Layer 10w: Power-up expiration tone (matching player 0's Layer 38) ──
    if (params.powerUpExpireFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float expProgress = static_cast<float>(params.powerUpExpireFrames) / 15.0f;
        float expFreq = 300.0f + 400.0f * expProgress;
        int expVol = static_cast<int>(50 * params.uiVolume);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 0,
                              static_cast<double>(expFreq), 0.5, expVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.powerUpExpireFrames > 0) ctx.audioState.powerUpExpireFrames--;
        }
    }

    // ── Layer 10x: Power-up explosion (matching player 0's Layer 39) ──
    if (params.powerUpExplodeFrames > 0) {
        std::fill(ctx.mixBuf.begin(), ctx.mixBuf.end(), 0);
        float explodeFreq = 80.0f + static_cast<float>(std::max(0, 10 - params.powerUpExplodeFrames)) * 30.0f;
        int explodeVol = static_cast<int>(100 * params.powerUpExplodeIntensity * params.collisionVolume);
        explodeVol = std::max(explodeVol, 30);
        engine->generateAudio(ctx.mixBuf, samplesPerFrame, 3,
                              static_cast<double>(explodeFreq),
                              static_cast<double>(params.powerUpExplodePan), explodeVol);
        mixBuffers(ctx.audioBuf, ctx.mixBuf, bufSize);
        {
            std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
            if (ctx.audioState.powerUpExplodeFrames > 0) ctx.audioState.powerUpExplodeFrames--;
        }
    }

    // ── Layer 11: Inter-player spatial sounds ──
    // Render engine sounds, Morse sidetones, and collision sounds from other
    // players as heard from THIS player's perspective.
    {
        std::lock_guard<std::mutex> lock(spatialMutex);
        for (int src = 0; src < config.playerCount; src++) {
            if (src == playerIndex) continue;

            const auto& rel = spatialRelations[playerIndex * MAX_PLAYERS + src];
            if (rel.volume < 0.001f) continue;

            const auto& srcCtx = players[src];

            // Other player's engine (with SWR roughness audible spatially)
            if (srcCtx.playerSpeed > 0.001f && srcCtx.maxSpeed > 0.0f) {
                float baseFreq = 220.0f;
                float speedFrac = std::min(1.0f, srcCtx.playerSpeed / srcCtx.maxSpeed);
                float motorFreq = baseFreq + speedFrac * (1320.0f - baseFreq);
                float dopplerFreq = SpatialPlayerAudio::applyDoppler(motorFreq, rel.dopplerFactor);

                float angle = (rel.pan + 1.0f) * 0.25f * static_cast<float>(M_PI);
                float leftGain = rel.volume * std::cos(angle) * 0.3f;
                float rightGain = rel.volume * std::sin(angle) * 0.3f;

                float invSR = 1.0f / static_cast<float>(sampleRate);
                float& phase = ctx.enginePhases[src];

                // SWR roughness of the source player — a poorly tuned antenna
                // makes the motor sound rough/rattly from other players' perspective.
                float roughness = srcCtx.currentMotorRoughness;
                float modDepth = 0.7f * roughness;
                float modRate = 15.0f + 45.0f * roughness;
                float modPhaseInc = modRate * 2.0f * static_cast<float>(M_PI) * invSR;
                // Use engine phase as modulation base (no extra state needed)
                float modPhase = phase * modRate;

                for (int i = 0; i < samplesPerFrame; i++) {
                    phase += dopplerFreq * invSR;
                    if (phase >= 1.0f) phase -= 1.0f;
                    float s = (phase * 2.0f - 1.0f);
                    s += std::sin(phase * 2.0f * static_cast<float>(M_PI) * 3.0f) * 0.2f;
                    // Apply roughness amplitude modulation (matching Layer 1 model)
                    modPhase += modPhaseInc;
                    float roughMod = 1.0f - modDepth * (0.5f + 0.5f * std::sin(modPhase));
                    s *= roughMod;
                    int idx = i * 2;
                    ctx.audioBuf[idx]     = clampI16(static_cast<int32_t>(ctx.audioBuf[idx])     + static_cast<int16_t>(s * leftGain  * 8000.0f));
                    ctx.audioBuf[idx + 1] = clampI16(static_cast<int32_t>(ctx.audioBuf[idx + 1]) + static_cast<int16_t>(s * rightGain * 8000.0f));
                }
            }

            // Other player's Morse cannon
            // 6.4/6.5: Uses morseCannonTimestamp for sample-accurate start
            if (srcCtx.morseCannonActive) {
                float morseFreq = 600.0f;
                float dopplerMorse = SpatialPlayerAudio::applyDoppler(morseFreq, rel.dopplerFactor);
                float angle = (rel.pan + 1.0f) * 0.25f * static_cast<float>(M_PI);
                float leftGain = rel.volume * std::cos(angle) * 0.4f;
                float rightGain = rel.volume * std::sin(angle) * 0.4f;
                float invSR = 1.0f / static_cast<float>(sampleRate);
                float& mPhase = ctx.morsePhases[src];
                
                // Calculate sample offset for sub-frame start using timestamp
                // Use the consistent frame start time (same reference as player 0's
                // audio thread) instead of now() to prevent inter-player timing drift.
                int startSample = 0;
                auto refTime = (frameStartTime != std::chrono::steady_clock::time_point{})
                                   ? frameStartTime
                                   : std::chrono::steady_clock::now();
                if (srcCtx.morseCannonTimestamp != std::chrono::steady_clock::time_point{}) {
                    auto elapsed = std::chrono::duration<double>(srcCtx.morseCannonTimestamp - refTime);
                    int offset = static_cast<int>(elapsed.count() * sampleRate);
                    if (offset < 0) offset = 0;
                    if (offset >= samplesPerFrame) offset = samplesPerFrame - 1;
                    startSample = offset;
                }

                for (int i = 0; i < samplesPerFrame; i++) {
                    if (i < startSample) continue;  // Silence before event
                    mPhase += dopplerMorse * invSR;
                    if (mPhase >= 1.0f) mPhase -= 1.0f;
                    float s = (mPhase < 0.5f) ? 1.0f : -1.0f;
                    int idx = i * 2;
                    ctx.audioBuf[idx]     = clampI16(static_cast<int32_t>(ctx.audioBuf[idx])     + static_cast<int16_t>(s * leftGain  * 6000.0f));
                    ctx.audioBuf[idx + 1] = clampI16(static_cast<int32_t>(ctx.audioBuf[idx + 1]) + static_cast<int16_t>(s * rightGain * 6000.0f));
                }
            }

            // Other player's collision
            if (srcCtx.collisionSoundFrames > 0 && srcCtx.collisionIntensity > 0.0f) {
                float angle = (rel.pan + 1.0f) * 0.25f * static_cast<float>(M_PI);
                float leftGain = rel.volume * std::cos(angle) * srcCtx.collisionIntensity;
                float rightGain = rel.volume * std::sin(angle) * srcCtx.collisionIntensity;
                for (int i = 0; i < samplesPerFrame; i++) {
                    float t = static_cast<float>(i) / static_cast<float>(samplesPerFrame);
                    float envelope = (1.0f - t);
                    float noise = static_cast<float>((i * 7 + 13) % 256) / 128.0f - 1.0f;
                    float thud = std::sin(t * 80.0f * static_cast<float>(M_PI));
                    float s = (noise * 0.6f + thud * 0.4f) * envelope;
                    int idx = i * 2;
                    ctx.audioBuf[idx]     = clampI16(static_cast<int32_t>(ctx.audioBuf[idx])     + static_cast<int16_t>(s * leftGain  * 12000.0f));
                    ctx.audioBuf[idx + 1] = clampI16(static_cast<int32_t>(ctx.audioBuf[idx + 1]) + static_cast<int16_t>(s * rightGain * 12000.0f));
                }
            }
        }
    }

    // ── Layer 12: Other players' game events (noise blanker fire, power-up collection) ──
    // Render spatial audio for events from other players based on the event queue
    {
        std::lock_guard<std::mutex> lock(ctx.audioStateMtx);
        for (const auto& evt : ctx.audioState.otherPlayerEvents) {
            if (evt.volume < 0.01f) continue;
            float angle = (evt.pan + 1.0f) * 0.25f * static_cast<float>(M_PI);
            float leftGain = evt.volume * std::cos(angle);
            float rightGain = evt.volume * std::sin(angle);
            
            // Different sound for different event types
            float freq = 800.0f;
            float amplitude = 4000.0f;
            int renderFrames = std::min(samplesPerFrame, samplesPerFrame / 4);
            
            switch (evt.type) {
                case GameEventType::NOISE_BLANKER_FIRE:
                    freq = 1200.0f;
                    amplitude = 6000.0f;
                    renderFrames = std::min(samplesPerFrame, samplesPerFrame / 3);
                    break;
                case GameEventType::NOISE_ENEMY_DESTROYED:
                    freq = 600.0f;
                    amplitude = 8000.0f;
                    break;
                case GameEventType::MORSE_COLLECTED:
                    freq = 1000.0f;
                    amplitude = 3000.0f;
                    break;
                case GameEventType::POWERUP_COLLECTED:
                    freq = 500.0f;
                    amplitude = 5000.0f;
                    break;
                default:
                    break;
            }
            
            float invSR = 1.0f / static_cast<float>(sampleRate);
            for (int i = 0; i < renderFrames; i++) {
                float t = static_cast<float>(i) * invSR;
                float envelope = 1.0f - static_cast<float>(i) / static_cast<float>(renderFrames);
                float s = std::sin(2.0f * static_cast<float>(M_PI) * freq * t) * envelope;
                int idx = i * 2;
                ctx.audioBuf[idx]     = clampI16(static_cast<int32_t>(ctx.audioBuf[idx])     + static_cast<int16_t>(s * leftGain  * amplitude));
                ctx.audioBuf[idx + 1] = clampI16(static_cast<int32_t>(ctx.audioBuf[idx + 1]) + static_cast<int16_t>(s * rightGain * amplitude));
            }
        }
    }

    // Increment audio frame counter (matches Player 0's frameCount)
    ctx.audioFrameCount++;

    // ── Play to this player's audio device ──
    ctx.audioBackend->playBuffer(ctx.audioBuf.data(), samplesPerFrame,
                                 sampleRate, channels, bitsPerSample);
}

void MultiplayerManager::renderAllPlayerAudio(
    SynthesizerEngine* audioEngine,
    int sampleRate, int samplesPerFrame,
    int channels, int bitsPerSample,
    float sharedMorsePhase,
    std::chrono::steady_clock::time_point frameStartTime,
    bool isPlaying)
{
    if (config.playerCount < 2) return;

    // Generate and play a fully independent audio stream for each secondary player
    for (int p = 1; p < config.playerCount; p++) {
        generateAndPlayPlayerAudio(p, audioEngine, sampleRate, samplesPerFrame,
                                   channels, bitsPerSample,
                                   sharedMorsePhase, frameStartTime,
                                   isPlaying);
    }
}

// ─── Legacy: distribute shared audio to all player backends ─────────────────
// Used only for non-gameplay states (menus, title screen) where all players
// should hear the same audio.

void MultiplayerManager::distributeAudioToPlayers(
    const int16_t* buffer, int samples,
    int sampleRate, int channels, int bitsPerSample)
{
    if (!buffer || samples <= 0 || config.playerCount < 2) return;

    // Simple pass-through: send the same audio to all secondary players' backends.
    // No spatial processing needed for menu/title audio.
    for (int p = 1; p < config.playerCount; p++) {
        auto& ctx = players[p];
        if (!ctx.audioBackend) continue;
        ctx.audioBackend->playBuffer(buffer, samples, sampleRate, channels, bitsPerSample);
    }
}

} // namespace HamSpirit

#endif // WITH_HAM_SPIRIT
