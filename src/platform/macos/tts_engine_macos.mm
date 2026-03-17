#include "tts_interface.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>
#include <fcntl.h>
#include <functional>

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#endif

// environ is a POSIX global but Apple headers don't always declare it.
extern char **environ;

/**
 * macOS TTS implementation with native 'say' command and VoiceOver integration.
 *
 * Two distinct engine modes:
 *  1. MACOS_SAY (default) — uses the macOS 'say' command. Always available.
 *     Does NOT use VoiceOver even if it's running, to avoid dual-speech.
 *  2. MACOS_VOICEOVER — uses AppleScript to speak through VoiceOver.
 *     Only works when VoiceOver is actually running.
 *
 * The user selects the engine via the Speech Settings menu.
 * Auto-detection at init: if VoiceOver is running and no explicit choice
 * was made, we default to VoiceOver mode.
 */

static bool isVoiceOverRunning() {
    // Primary check: use the Accessibility API to query VoiceOver status.
    // This is more reliable than pgrep because VoiceOver may be launched
    // under different process names depending on the macOS version.
    @autoreleasepool {
        // AXIsProcessTrusted() returns true if we have accessibility permissions.
        // To check VoiceOver itself, we query the system preference.
        CFStringRef voKey = CFSTR("voiceOverOnOffKey");
        CFStringRef appId = CFSTR("com.apple.universalaccess");
        CFPropertyListRef val = CFPreferencesCopyAppValue(voKey, appId);
        if (val) {
            bool running = false;
            if (CFGetTypeID(val) == CFBooleanGetTypeID()) {
                running = CFBooleanGetValue(static_cast<CFBooleanRef>(val));
            }
            CFRelease(val);
            if (running) return true;
        }
    }
    // Fallback: check if VoiceOver process is running via pgrep return code
    FILE* pipe = popen("pgrep -x VoiceOver 2>/dev/null", "r");
    if (!pipe) return false;
    int status = pclose(pipe);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/**
 * Check if the app has AppleScript automation permission to control VoiceOver.
 * On first use macOS shows a consent dialog; if the user has denied it
 * we inform them via the 'say' command and return false.
 */
static bool checkVoiceOverPermission() {
    // Try a harmless osascript command that talks to VoiceOver.
    // If automation permission has not been granted, osascript fails with exit code 1.
    // NOTE: the first time this runs, macOS will pop up a system dialog asking the user
    // to allow the app to control VoiceOver — that's exactly what we want.
    //
    // Use posix_spawn() rather than system() for safety and consistency.
    char* const checkArgv[] = {
        const_cast<char*>("/usr/bin/osascript"),
        const_cast<char*>("-e"),
        const_cast<char*>("tell application \"VoiceOver\" to get name"),
        nullptr
    };
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawn_file_actions_t fileActions;
    posix_spawn_file_actions_init(&fileActions);
    // Redirect stdout and stderr to /dev/null
    posix_spawn_file_actions_addopen(&fileActions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fileActions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid = -1;
    int spawnRet = posix_spawn(&pid, "/usr/bin/osascript", &fileActions, &attr, checkArgv, environ);
    posix_spawn_file_actions_destroy(&fileActions);
    posix_spawnattr_destroy(&attr);

    if (spawnRet != 0) {
        std::fprintf(stderr, "[TTS_MACOS] posix_spawn(/usr/bin/osascript) failed: %d\n", spawnRet);
        return false;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return true;
    }

    // Permission not granted — inform user via NSSpeechSynthesizer (no process spawn needed)
    std::fprintf(stderr, "[TTS_MACOS] AppleScript automation permission for VoiceOver not granted\n");
    @autoreleasepool {
        NSSpeechSynthesizer* tmpSynth = [[NSSpeechSynthesizer alloc] initWithVoice:nil];
        if (tmpSynth) {
            [tmpSynth startSpeakingString:
                @"Please allow this application to control VoiceOver. "
                 "A system dialog should appear. If not, open System Settings, "
                 "Privacy and Security, Automation, and enable VoiceOver for this application."];
            // Don't block — let it speak in the background
        }
    }
    return false;
}

class MacOSTTSEngine : public ITTSEngine {
public:
    explicit MacOSTTSEngine(bool voiceOverMode = false)
        : initialized(false), useVoiceOver(voiceOverMode), speaking(false),
          childPid(-1), synth(nil), volume(100), rate(TTSRate::NORMAL) {
    }

    ~MacOSTTSEngine() override {
        shutdown();
    }

    bool initialize() override {
        if (initialized) return true;

        if (useVoiceOver) {
            // VoiceOver mode — verify it's actually running
            if (!isVoiceOverRunning()) {
                std::fprintf(stderr, "[TTS_MACOS] VoiceOver mode requested but VoiceOver is not running\n");
                return false;
            }
            // Verify we have AppleScript automation permission to control VoiceOver.
            // This may trigger a macOS consent dialog on first use.
            if (!checkVoiceOverPermission()) {
                std::fprintf(stderr, "[TTS_MACOS] Cannot control VoiceOver — automation permission denied\n");
                return false;
            }
            std::fprintf(stderr, "[TTS_MACOS] Initialized in VoiceOver mode\n");
        } else {
            // NSSpeechSynthesizer mode — create the synthesizer on the main thread
            @autoreleasepool {
                synth = [[NSSpeechSynthesizer alloc] initWithVoice:nil];
                if (!synth) {
                    std::fprintf(stderr, "[TTS_MACOS] Failed to create NSSpeechSynthesizer\n");
                    return false;
                }
                // Apply initial rate
                [synth setRate:static_cast<float>(getRateValue())];
            }
            std::fprintf(stderr, "[TTS_MACOS] Initialized with NSSpeechSynthesizer (no process spawn latency)\n");
        }

        initialized = true;

        // Pre-populate voices
        if (!useVoiceOver) {
            cachedVoices = queryVoices();
        }
        return true;
    }

    void shutdown() override {
        stop();
        @autoreleasepool {
            if (synth) {
                [synth stopSpeaking];
                synth = nil;
            }
        }
        initialized = false;
    }

    bool isAvailable() const override { return initialized; }

    bool speak(const std::string& text, bool interrupt = false) override {
        if (!isAvailable()) return false;
        if (interrupt) stop();

        if (!useVoiceOver && synth) {
            // NSSpeechSynthesizer: speak asynchronously with no thread overhead
            @autoreleasepool {
                NSString* nsText = [NSString stringWithUTF8String:text.c_str()];
                if (!nsText) return false;
                speaking.store(true);
                [synth startSpeakingString:nsText];
            }
            // Monitor for completion in a joinable background thread
            if (speakThread.joinable()) {
                speakThread.join();
            }
            unsigned int gen = ++speakGeneration;
            speakThread = std::thread([this, gen]() {
                @autoreleasepool {
                    while (synth && [synth isSpeaking]) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    }
                    if (speakGeneration.load() == gen) {
                        speaking.store(false);
                    }
                }
            });
            return true;
        }

        // VoiceOver mode: use joinable thread + speakSync
        if (speakThread.joinable()) {
            speakThread.join();
        }
        unsigned int gen = ++speakGeneration;
        speaking.store(true);
        speakThread = std::thread([this, text, gen]() {
            speakSync(text);
            if (speakGeneration.load() == gen) {
                speaking.store(false);
            }
        });
        return true;
    }

    bool speakSync(const std::string& text) override {
        if (!isAvailable()) return false;

        if (useVoiceOver) {
            std::string escaped = escapeShell(text);
            return speakViaVoiceOver(escaped);
        }

        // NSSpeechSynthesizer synchronous speak
        if (synth) {
            @autoreleasepool {
                NSString* nsText = [NSString stringWithUTF8String:text.c_str()];
                if (!nsText) return false;
                [synth startSpeakingString:nsText];
                // Block until speech finishes
                while ([synth isSpeaking]) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            return true;
        }

        // Fallback: spawn /usr/bin/say directly
        return spawnSayDirect(text);
    }

    void stop() override {
        // Stop NSSpeechSynthesizer
        if (synth) {
            @autoreleasepool {
                [synth stopSpeaking];
            }
        }
        // Also stop any VoiceOver posix_spawn child
        pid_t pidToKill = -1;
        {
            std::lock_guard<std::mutex> lock(pidMutex);
            pidToKill = childPid;
            childPid = -1;
        }
        if (pidToKill > 0) {
            kill(pidToKill, SIGTERM);
            for (int i = 0; i < 10; i++) {
                int st = 0;
                if (waitpid(pidToKill, &st, WNOHANG) != 0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        // Join the speak/monitor thread to ensure it has finished
        if (speakThread.joinable()) {
            speakThread.join();
        }
        speaking.store(false);
    }

    bool isSpeaking() const override {
        if (synth) {
            @autoreleasepool {
                return [synth isSpeaking];
            }
        }
        return speaking.load();
    }

    void setRate(TTSRate r) override {
        rate = r;
        if (synth) {
            @autoreleasepool {
                [synth setRate:static_cast<float>(getRateValue())];
            }
        }
    }

    void setVolume(int vol) override {
        volume = std::max(0, std::min(100, vol));
        if (synth) {
            @autoreleasepool {
                [synth setVolume:static_cast<float>(volume) / 100.0f];
            }
        }
    }

    bool setLanguage(const std::string& langCode) override {
        if (langCode == "en" || langCode == "eng" || langCode == "en-US") {
            currentVoice = "";
            languageCode = "en";
        } else if (langCode == "de" || langCode == "deu" || langCode == "de-DE") {
            // Resolve best German voice from available voices instead of hardcoding "Anna"
            currentVoice = "";
            for (const auto& v : cachedVoices) {
                // Prefer voices containing "de" locale markers
                if (v.find("de_DE") != std::string::npos || v.find("de-DE") != std::string::npos) {
                    currentVoice = v;
                    break;
                }
            }
            languageCode = "de";
        } else {
            currentVoice = "";
            languageCode = langCode;
        }
        // Apply voice change to NSSpeechSynthesizer
        applyVoiceToSynth();
        return true;
    }

    std::vector<std::string> getAvailableVoices() const override {
        return cachedVoices;
    }

    bool setVoice(const std::string& voiceName) override {
        currentVoice = voiceName;
        applyVoiceToSynth();
        return true;
    }

    void setStatusCallback(std::function<void(TTSStatus)> callback) override {
        statusCallback = callback;
    }

private:
    bool initialized;
    bool useVoiceOver;
    std::atomic<bool> speaking;
    std::atomic<unsigned int> speakGeneration{0}; // incremented each speak() call
    pid_t childPid;
    std::mutex pidMutex;
    std::thread speakThread;  // Joinable thread for async speak/monitor
    NSSpeechSynthesizer* synth;  // Native macOS speech synthesizer (nil in VoiceOver mode)
    int volume;
    TTSRate rate;
    std::string currentVoice;
    std::string languageCode;
    std::vector<std::string> cachedVoices;
    std::function<void(TTSStatus)> statusCallback;

    /// Apply the current voice to the NSSpeechSynthesizer instance
    void applyVoiceToSynth() {
        if (!synth) return;
        @autoreleasepool {
            if (!currentVoice.empty()) {
                // Try to find the full voice identifier from the short name
                NSString* targetName = [NSString stringWithUTF8String:currentVoice.c_str()];
                NSArray* voices = [NSSpeechSynthesizer availableVoices];
                for (NSString* voiceId in voices) {
                    NSDictionary* attrs = [NSSpeechSynthesizer attributesForVoice:voiceId];
                    NSString* name = attrs[NSVoiceName];
                    if ([name isEqualToString:targetName]) {
                        [synth setVoice:voiceId];
                        return;
                    }
                }
                // If no match, try using the name as a voice identifier directly
                [synth setVoice:targetName];
            } else {
                // Reset to default voice
                [synth setVoice:nil];
            }
        }
    }

    static std::string escapeShell(const std::string& text) {
        std::string escaped;
        escaped.reserve(text.size() + 16);
        for (char c : text) {
            if (c == '\'') {
                escaped += "'\\''";
            } else {
                escaped += c;
            }
        }
        return escaped;
    }

    static std::string escapeAppleScript(const std::string& text) {
        std::string escaped;
        escaped.reserve(text.size() + 16);
        for (char c : text) {
            if (c == '"') {
                escaped += "\\\"";
            } else if (c == '\\') {
                escaped += "\\\\";
            } else {
                escaped += c;
            }
        }
        return escaped;
    }

    int getRateValue() const {
        switch (rate) {
            case TTSRate::VERY_SLOW: return 100;
            case TTSRate::SLOW:      return 140;
            case TTSRate::NORMAL:    return 175;
            case TTSRate::FAST:      return 220;
            case TTSRate::VERY_FAST: return 280;
        }
        return 175;
    }

    std::string buildSayCommand(const std::string& escapedText) const {
        std::string cmd = "say";

        if (!currentVoice.empty()) {
            cmd += " -v '" + currentVoice + "'";
        }

        cmd += " -r " + std::to_string(getRateValue());
        cmd += " '" + escapedText + "'";
        return cmd;
    }

    bool speakViaVoiceOver(const std::string& escapedText) {
        // Use osascript to tell VoiceOver to output text
        std::string asEscaped = escapeAppleScript(escapedText);
        std::string cmd = "osascript -e 'tell application \"VoiceOver\" to output \"" +
                          asEscaped + "\"' 2>/dev/null";
        return runCommand(cmd);
    }

    /**
     * Spawn /usr/bin/say directly, bypassing /bin/sh.
     * This eliminates one process spawn (~50-100ms latency reduction).
     */
    bool spawnSayDirect(const std::string& text) {
        std::string rateStr = std::to_string(getRateValue());

        // Build argv for direct /usr/bin/say invocation.
        // posix_spawn() requires char* const[] for argv (POSIX doesn't
        // declare argv as const char*). The const_casts are safe because
        // posix_spawn does not modify the argv strings; it only reads
        // them to set up the child process. All pointed-to strings
        // outlive the posix_spawn call (they are local variables or
        // member strings with lifetime exceeding this scope).
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>("/usr/bin/say"));
        if (!currentVoice.empty()) {
            argv.push_back(const_cast<char*>("-v"));
            argv.push_back(const_cast<char*>(currentVoice.c_str()));
        }
        argv.push_back(const_cast<char*>("-r"));
        argv.push_back(const_cast<char*>(rateStr.c_str()));
        argv.push_back(const_cast<char*>(text.c_str()));
        argv.push_back(nullptr);

        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);

        pid_t pid = -1;
        int ret = posix_spawn(&pid, "/usr/bin/say", nullptr, &attr,
                              argv.data(), environ);
        posix_spawnattr_destroy(&attr);

        if (ret != 0) {
            std::fprintf(stderr, "[TTS_MACOS] posix_spawn(/usr/bin/say) failed: error %d\n", ret);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(pidMutex);
            childPid = pid;
        }

        int status = 0;
        waitpid(pid, &status, 0);

        {
            std::lock_guard<std::mutex> lock(pidMutex);
            childPid = -1;
        }

        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    bool runCommand(const std::string& command) {
        // Use posix_spawn() instead of fork()+exec() because fork() is unsafe
        // after [NSApplication sharedApplication] — AppKit's internal threads
        // may hold locks (malloc, ObjC runtime, GCD) that the child inherits
        // permanently locked, causing deadlock before execl() completes.
        //
        // NOTE: Do NOT use POSIX_SPAWN_SETSID here. On macOS the 'say' command
        // requires access to the user's audio session, which is tied to the
        // login session. Starting the child in a new session (setsid) severs
        // that connection and causes 'say' to run silently with no audio output.
        //
        // argv pointers are only dereferenced during posix_spawn() itself
        // (synchronous setup), so command.c_str() lifetime is fine here.
        char* const argv[] = {
            const_cast<char*>("/bin/sh"),
            const_cast<char*>("-c"),
            const_cast<char*>(command.c_str()),
            nullptr
        };

        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);

        pid_t pid = -1;
        // nullptr for file_actions: no I/O redirections needed.
        // environ (from <unistd.h>): inherit the parent's environment.
        int ret = posix_spawn(&pid, "/bin/sh", nullptr, &attr, argv, environ);

        posix_spawnattr_destroy(&attr);

        if (ret != 0) {
            std::fprintf(stderr, "[TTS_MACOS] posix_spawn failed (error %d): %s\n",
                         ret, command.c_str());
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(pidMutex);
            childPid = pid;
        }

#ifdef TTS_MACOS_DEBUG
        std::fprintf(stderr, "[TTS_MACOS] spawned pid=%d cmd=%s\n",
                     (int)pid, command.c_str());
#endif

        int status = 0;
        waitpid(pid, &status, 0);

#ifdef TTS_MACOS_DEBUG
        std::fprintf(stderr, "[TTS_MACOS] pid=%d finished: wifexited=%d exitcode=%d\n",
                     (int)pid, WIFEXITED(status),
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1);
#endif

        {
            std::lock_guard<std::mutex> lock(pidMutex);
            childPid = -1;
        }

        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    std::vector<std::string> queryVoices() const {
        std::vector<std::string> voices;
        @autoreleasepool {
            NSArray* availableVoices = [NSSpeechSynthesizer availableVoices];
            for (NSString* voiceId in availableVoices) {
                NSDictionary* attrs = [NSSpeechSynthesizer attributesForVoice:voiceId];
                NSString* name = attrs[NSVoiceName];
                if (name) {
                    voices.push_back([name UTF8String]);
                }
            }
        }
        if (voices.empty()) {
            voices.push_back("default");
        }
        return voices;
    }
};

// ─── espeak-NG engine for macOS ──────────────────────────────────────────────

static bool isEspeakNgInstalled() {
    // Check common Homebrew locations
    const char* paths[] = {
        "/opt/homebrew/bin/espeak-ng",   // Apple Silicon
        "/usr/local/bin/espeak-ng",      // Intel Mac
        nullptr
    };
    for (int i = 0; paths[i]; i++) {
        if (access(paths[i], X_OK) == 0) return true;
    }
    return false;
}

static std::string findEspeakNgBinary() {
    const char* paths[] = {
        "/opt/homebrew/bin/espeak-ng",
        "/usr/local/bin/espeak-ng",
        nullptr
    };
    for (int i = 0; paths[i]; i++) {
        if (access(paths[i], X_OK) == 0) return paths[i];
    }
    return "";
}

class EspeakNgTTSEngine : public ITTSEngine {
public:
    EspeakNgTTSEngine() : initialized(false), speaking(false), childPid(-1),
                           volume(100), rate(TTSRate::NORMAL) {}

    ~EspeakNgTTSEngine() override { shutdown(); }

    bool initialize() override {
        if (initialized) return true;

        espeakPath = findEspeakNgBinary();
        if (espeakPath.empty()) {
            std::fprintf(stderr, "[TTS_MACOS] espeak-ng not found.\n"
                                  "  Install via: brew install espeak-ng\n");
            return false;
        }

        std::fprintf(stderr, "[TTS_MACOS] Initialized with espeak-ng: %s\n", espeakPath.c_str());
        initialized = true;
        return true;
    }

    void shutdown() override {
        stop();
        initialized = false;
    }

    bool isAvailable() const override { return initialized; }

    bool speak(const std::string& text, bool interrupt = false) override {
        if (!initialized) return false;
        // eSpeak-NG runs as an external process — cannot queue speech.
        // Always stop the previous utterance to prevent parallel processes.
        stop();

        // Join any previous speak thread before starting a new one
        if (speakThread.joinable()) {
            speakThread.join();
        }

        unsigned int gen = ++speakGeneration;
        speaking.store(true);
        speakThread = std::thread([this, text, gen]() {
            speakSync(text);
            if (speakGeneration.load() == gen) speaking.store(false);
        });
        return true;
    }

    bool speakSync(const std::string& text) override {
        if (!initialized) return false;

        std::string rateStr = std::to_string(getEspeakRate());
        std::string volStr = std::to_string(volume);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(espeakPath.c_str()));
        argv.push_back(const_cast<char*>("-s"));
        argv.push_back(const_cast<char*>(rateStr.c_str()));
        argv.push_back(const_cast<char*>("-a"));
        argv.push_back(const_cast<char*>(volStr.c_str()));
        if (!languageCode.empty()) {
            argv.push_back(const_cast<char*>("-v"));
            argv.push_back(const_cast<char*>(languageCode.c_str()));
        }
        argv.push_back(const_cast<char*>(text.c_str()));
        argv.push_back(nullptr);

        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);

        pid_t pid = -1;
        int ret = posix_spawn(&pid, espeakPath.c_str(), nullptr, &attr,
                               argv.data(), environ);
        posix_spawnattr_destroy(&attr);

        if (ret != 0) {
            std::fprintf(stderr, "[TTS_MACOS] espeak-ng spawn failed: %d\n", ret);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(pidMutex);
            childPid = pid;
        }

        int status = 0;
        waitpid(pid, &status, 0);

        {
            std::lock_guard<std::mutex> lock(pidMutex);
            childPid = -1;
        }

        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    void stop() override {
        pid_t pidToKill = -1;
        {
            std::lock_guard<std::mutex> lock(pidMutex);
            pidToKill = childPid;
            childPid = -1;
        }
        if (pidToKill > 0) {
            kill(pidToKill, SIGTERM);
            for (int i = 0; i < 10; i++) {
                int st = 0;
                if (waitpid(pidToKill, &st, WNOHANG) != 0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        // Join the speak thread to ensure it has finished before engine destruction
        if (speakThread.joinable()) {
            speakThread.join();
        }
        speaking.store(false);
    }

    bool isSpeaking() const override { return speaking.load(); }

    void setRate(TTSRate r) override { rate = r; }

    void setVolume(int vol) override { volume = std::max(0, std::min(100, vol)); }

    bool setLanguage(const std::string& langCode) override {
        if (langCode == "de" || langCode == "deu" || langCode == "de-DE")
            languageCode = "de";
        else if (langCode == "en" || langCode == "eng" || langCode == "en-US")
            languageCode = "en";
        else
            languageCode = langCode;
        return true;
    }

    std::vector<std::string> getAvailableVoices() const override {
        // espeak-ng voice enumeration
        std::vector<std::string> voices;
        std::string cmd = espeakPath + " --voices 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char line[256];
            bool header = true;
            while (fgets(line, sizeof(line), pipe)) {
                if (header) { header = false; continue; } // skip header line
                // Format: "Pty Language Age/Gender VoiceName ..."
                char name[128] = {};
                if (std::sscanf(line, " %*d %*s %*s %127s", name) == 1 && name[0]) {
                    voices.push_back(name);
                }
            }
            pclose(pipe);
        }
        if (voices.empty()) voices.push_back("default");
        return voices;
    }

    bool setVoice(const std::string& voiceName) override {
        languageCode = voiceName;
        return true;
    }

    void setStatusCallback(std::function<void(TTSStatus)> callback) override {
        (void)callback;
    }

private:
    bool initialized;
    std::atomic<bool> speaking;
    std::atomic<unsigned int> speakGeneration{0};
    pid_t childPid;
    std::mutex pidMutex;
    std::thread speakThread;  // Joinable thread for async speak
    int volume;
    TTSRate rate;
    std::string espeakPath;
    std::string languageCode;

    int getEspeakRate() const {
        switch (rate) {
            case TTSRate::VERY_SLOW: return 80;
            case TTSRate::SLOW:      return 120;
            case TTSRate::NORMAL:    return 175;
            case TTSRate::FAST:      return 260;
            case TTSRate::VERY_FAST: return 350;
        }
        return 175;
    }
};

// Factory function for macOS
std::unique_ptr<ITTSEngine> createTTSEngine(TTSEngineType type) {
    if (type == TTSEngineType::MACOS_VOICEOVER) {
        return std::make_unique<MacOSTTSEngine>(true);
    }
    if (type == TTSEngineType::ESPEAK_NG) {
        auto engine = std::make_unique<EspeakNgTTSEngine>();
        if (engine->initialize()) {
            return engine;
        }
        std::fprintf(stderr, "[TTS_MACOS] espeak-ng not available, falling back to macOS Say\n");
        // Fall through to default
    }
    // MACOS_SAY is default; also handle WINDOWS_SAPI/NVDA gracefully
    return std::make_unique<MacOSTTSEngine>(false);
}

// NVDA is Windows-only
bool isNvdaScreenReaderRunning() { return false; }
bool downloadNvdaControllerClientDll() { return false; }
void openNvdaDllDownloadPage() {}
std::string getNvdaDllTargetDirectory() { return {}; }

// macOS: VoiceOver braille integration
bool sendNvdaBrailleMessage(const std::string& text) {
    // On macOS, try to output to VoiceOver braille display if VoiceOver is running
    if (!isVoiceOverRunning()) return false;

    // Escape text for AppleScript
    std::string escaped;
    for (char c : text) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else escaped += c;
    }

    std::string cmd = "osascript -e 'tell application \"VoiceOver\" to output \"" +
                      escaped + "\"' 2>/dev/null";

    // Use posix_spawn() — safe to call after AppKit is initialized.
    char* const argv[] = {
        const_cast<char*>("/bin/sh"),
        const_cast<char*>("-c"),
        const_cast<char*>(cmd.c_str()),
        nullptr
    };

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // Do NOT use POSIX_SPAWN_SETSID — osascript needs the user's login session
    // to reach VoiceOver; a new session would sever that connection.

    pid_t pid = -1;
    int ret = posix_spawn(&pid, "/bin/sh", nullptr, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);

    if (ret != 0) {
        std::fprintf(stderr, "[TTS_MACOS] braille posix_spawn failed (error %d)\n", ret);
        return false;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
