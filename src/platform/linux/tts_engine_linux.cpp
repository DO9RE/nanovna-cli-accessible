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
#include <functional>

/**
 * Detected Linux TTS backend
 */
enum class LinuxTTSBackend {
    NONE,
    ESPEAK_NG,    // espeak-ng (preferred)
    ESPEAK,       // legacy espeak
    SPD_SAY       // speech-dispatcher client
};

/**
 * Linux TTS implementation with runtime backend detection,
 * fork/exec process management, and voice enumeration.
 */
class LinuxTTSEngine : public ITTSEngine {
public:
    LinuxTTSEngine()
        : initialized(false), backend(LinuxTTSBackend::NONE),
          speaking(false), childPid(-1), volume(100), rate(TTSRate::NORMAL) {
    }

    ~LinuxTTSEngine() override {
        shutdown();
    }

    bool initialize() override {
        if (initialized) return true;

        // Detect available backends in preference order
        if (commandExists("espeak-ng")) {
            backend = LinuxTTSBackend::ESPEAK_NG;
            initialized = true;
        } else if (commandExists("espeak")) {
            backend = LinuxTTSBackend::ESPEAK;
            initialized = true;
        } else if (commandExists("spd-say")) {
            backend = LinuxTTSBackend::SPD_SAY;
            initialized = true;
        }

        if (initialized) {
            cachedVoices = queryVoices();
        }
        return initialized;
    }

    void shutdown() override {
        stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        initialized = false;
    }

    bool isAvailable() const override { return initialized; }

    bool speak(const std::string& text, bool interrupt = false) override {
        if (!isAvailable()) return false;
        // eSpeak/spd-say runs as an external process — cannot queue speech.
        // Always stop the previous utterance to prevent parallel processes.
        stop();

        speaking.store(true);
        std::thread([this, text]() {
            speakSync(text);
            speaking.store(false);
        }).detach();
        return true;
    }

    bool speakSync(const std::string& text) override {
        if (!isAvailable()) return false;

        std::string escaped = escapeShell(text);
        std::string command = buildCommand(escaped);
        if (command.empty()) return false;

        pid_t pid = fork();
        if (pid < 0) return false;

        if (pid == 0) {
            // Child: start new process group for reliable cleanup
            setsid();
            execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
            _exit(127);
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
        std::lock_guard<std::mutex> lock(pidMutex);
        if (childPid > 0) {
            // Kill process group (catches child processes spawned by the shell)
            // and the direct child as fallback
            kill(-childPid, SIGTERM);
            kill(childPid, SIGTERM);
            childPid = -1;
        }
        speaking.store(false);
    }

    bool isSpeaking() const override { return speaking.load(); }

    void setRate(TTSRate r) override { rate = r; }

    void setVolume(int vol) override {
        volume = std::max(0, std::min(100, vol));
    }

    bool setLanguage(const std::string& langCode) override {
        if (langCode == "en" || langCode == "eng" || langCode == "en-US") {
            languageCode = "en";
        } else if (langCode == "de" || langCode == "deu" || langCode == "de-DE") {
            languageCode = "de";
        } else {
            languageCode = langCode;
        }
        return true;
    }

    std::vector<std::string> getAvailableVoices() const override {
        return cachedVoices;
    }

    bool setVoice(const std::string& voiceName) override {
        currentVoice = voiceName;
        return true;
    }

    void setStatusCallback(std::function<void(TTSStatus)> callback) override {
        statusCallback = callback;
    }

private:
    bool initialized;
    LinuxTTSBackend backend;
    std::atomic<bool> speaking;
    pid_t childPid;
    std::mutex pidMutex;
    int volume;
    TTSRate rate;
    std::string languageCode;
    std::string currentVoice;
    std::vector<std::string> cachedVoices;
    std::function<void(TTSStatus)> statusCallback;

    static bool commandExists(const char* cmd) {
        std::string check = std::string("which ") + cmd + " > /dev/null 2>&1";
        return system(check.c_str()) == 0;
    }

    std::string escapeShell(const std::string& text) const {
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

    int getEspeakRate() const {
        switch (rate) {
            case TTSRate::VERY_SLOW: return 100;
            case TTSRate::SLOW:      return 140;
            case TTSRate::NORMAL:    return 175;
            case TTSRate::FAST:      return 220;
            case TTSRate::VERY_FAST: return 280;
        }
        return 175;
    }

    int getSpdRate() const {
        switch (rate) {
            case TTSRate::VERY_SLOW: return -50;
            case TTSRate::SLOW:      return -25;
            case TTSRate::NORMAL:    return 0;
            case TTSRate::FAST:      return 25;
            case TTSRate::VERY_FAST: return 50;
        }
        return 0;
    }

    std::string buildCommand(const std::string& escapedText) const {
        std::string cmd;
        std::string lang = languageCode.empty() ? "en" : languageCode;

        switch (backend) {
            case LinuxTTSBackend::ESPEAK_NG:
                cmd = "espeak-ng";
                cmd += " -v " + lang;
                if (!currentVoice.empty()) {
                    cmd += "+" + currentVoice;
                }
                cmd += " -s " + std::to_string(getEspeakRate());
                cmd += " -a " + std::to_string(volume);
                cmd += " '" + escapedText + "' 2>/dev/null";
                break;
            case LinuxTTSBackend::ESPEAK:
                cmd = "espeak";
                cmd += " -v " + lang;
                if (!currentVoice.empty()) {
                    cmd += "+" + currentVoice;
                }
                cmd += " -s " + std::to_string(getEspeakRate());
                cmd += " -a " + std::to_string(volume);
                cmd += " '" + escapedText + "' 2>/dev/null";
                break;
            case LinuxTTSBackend::SPD_SAY:
                cmd = "spd-say";
                cmd += " -l " + lang;
                cmd += " -r " + std::to_string(getSpdRate());
                cmd += " -i " + std::to_string(volume);
                cmd += " -w";
                cmd += " '" + escapedText + "' 2>/dev/null";
                break;
            default:
                break;
        }
        return cmd;
    }

    std::vector<std::string> queryVoices() const {
        std::vector<std::string> voices;
        std::string cmd;

        if (backend == LinuxTTSBackend::ESPEAK_NG) {
            cmd = "espeak-ng --voices 2>/dev/null";
        } else if (backend == LinuxTTSBackend::ESPEAK) {
            cmd = "espeak --voices 2>/dev/null";
        } else {
            voices.push_back("default");
            return voices;
        }

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            voices.push_back("default");
            return voices;
        }

        char buffer[256];
        bool headerSkipped = false;
        while (fgets(buffer, sizeof(buffer), pipe)) {
            if (!headerSkipped) {
                headerSkipped = true;
                continue;
            }
            // Parse voice name (column 4) from espeak output:
            // Pty Language Age/Gender VoiceName File OtherLanguages
            std::string line(buffer);
            size_t pos = 0;
            int col = 0;
            while (pos < line.size() && col < 3) {
                while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
                while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') pos++;
                col++;
            }
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
            size_t nameStart = pos;
            while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t' && line[pos] != '\n') pos++;
            if (pos > nameStart) {
                std::string voiceName = line.substr(nameStart, pos - nameStart);
                if (!voiceName.empty()) {
                    voices.push_back(voiceName);
                }
            }
        }
        pclose(pipe);

        if (voices.empty()) {
            voices.push_back("default");
        }
        return voices;
    }
};

// Factory function for Linux
std::unique_ptr<ITTSEngine> createTTSEngine(TTSEngineType /*type*/) {
    return std::make_unique<LinuxTTSEngine>();
}

// NVDA is Windows-only
bool isNvdaScreenReaderRunning() { return false; }
bool downloadNvdaControllerClientDll() { return false; }
void openNvdaDllDownloadPage() {}
std::string getNvdaDllTargetDirectory() { return {}; }

// Braille display is NVDA-only (Windows)
bool sendNvdaBrailleMessage(const std::string& /*text*/) { return false; }
