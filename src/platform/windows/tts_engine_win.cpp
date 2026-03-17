#include "tts_interface.h"
#include <windows.h>
#include <initguid.h>
#include <sapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <tlhelp32.h>
#include <fstream>
#include <sstream>
#include <urlmon.h>   // URLDownloadToFile
#include <shellapi.h> // ShellExecuteA

// Simple COM smart pointer for MinGW (ATL not available)
template<typename T>
class ComPtr {
public:
    ComPtr() : ptr(nullptr) {}
    explicit ComPtr(T* p) : ptr(p) {
        if (ptr) ptr->AddRef();
    }
    ~ComPtr() {
        if (ptr) ptr->Release();
    }
    
    ComPtr(const ComPtr& other) : ptr(other.ptr) {
        if (ptr) ptr->AddRef();
    }
    
    ComPtr& operator=(const ComPtr& other) {
        if (this != &other) {
            if (ptr) ptr->Release();
            ptr = other.ptr;
            if (ptr) ptr->AddRef();
        }
        return *this;
    }
    
    ComPtr(ComPtr&& other) noexcept : ptr(other.ptr) {
        other.ptr = nullptr;
    }
    
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            if (ptr) ptr->Release();
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }
    
    T* operator->() const { return ptr; }
    T** operator&() { 
        if (ptr) ptr->Release();
        ptr = nullptr;
        return &ptr;
    }
    operator bool() const { return ptr != nullptr; }
    T* get() const { return ptr; }
    
    void reset() {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }
    
private:
    T* ptr;
};

/**
 * Windows SAPI TTS implementation
 */
class WindowsTTSEngine : public ITTSEngine {
public:
    WindowsTTSEngine() 
        : initialized(false), voice(nullptr), volume(100), rate(TTSRate::NORMAL), 
          comInitializedByUs(false) {
    }
    
    ~WindowsTTSEngine() override {
        shutdown();
    }
    
    bool initialize() override {
        if (initialized) {
            return true;
        }
        
        // Initialize COM
        HRESULT hr = CoInitialize(nullptr);
        if (FAILED(hr)) {
            if (hr == RPC_E_CHANGED_MODE) {
                // COM already initialized by another component - that's OK
                comInitializedByUs = false;
            } else {
                return false;
            }
        } else {
            comInitializedByUs = true;
        }
        
        // Create SAPI voice instance
        hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, 
                              IID_ISpVoice, (void**)&voice);
        if (FAILED(hr)) {
            // Only uninitialize COM if we initialized it
            if (comInitializedByUs) {
                CoUninitialize();
                comInitializedByUs = false;
            }
            return false;
        }
        
        initialized = true;
        
        // Set default volume
        setVolume(volume);
        
        return true;
    }
    
    void shutdown() override {
        if (!initialized) {
            return;
        }
        
        // Synchronous purge: ensures speech is fully stopped before releasing COM object
        if (voice) {
            try { voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr); } catch (...) {}
        }
        
        if (voice) {
            try { voice->Release(); } catch (...) {}
            voice = nullptr;
        }
        
        // Only uninitialize COM if we initialized it
        if (comInitializedByUs) {
            try { CoUninitialize(); } catch (...) {}
            comInitializedByUs = false;
        }
        
        initialized = false;
    }
    
    bool isAvailable() const override {
        return initialized && voice != nullptr;
    }
    
    bool speak(const std::string& text, bool interrupt = false) override {
        if (!isAvailable() || !voice) {
            return false;
        }
        
        try {
            // Convert UTF-8 to wide string
            std::wstring wtext = utf8ToWide(text);
            if (wtext.empty() && !text.empty()) {
                return false;
            }
            
            // Use SPF_PURGEBEFORESPEAK as a single atomic SAPI operation
            // instead of separate stop() + speak() which can deadlock
            // when SAPI's purge blocks waiting for WAVE_MAPPER
            DWORD flags = SPF_ASYNC | SPF_IS_NOT_XML;
            if (interrupt) {
                flags |= SPF_PURGEBEFORESPEAK;
            }
            
            HRESULT hr = voice->Speak(wtext.c_str(), flags, nullptr);
            return SUCCEEDED(hr);
        } catch (...) {
            return false;
        }
    }
    
    bool speakSync(const std::string& text) override {
        if (!isAvailable() || !voice) {
            return false;
        }
        
        try {
            // Convert UTF-8 to wide string
            std::wstring wtext = utf8ToWide(text);
            if (wtext.empty() && !text.empty()) {
                return false;
            }
            
            HRESULT hr = voice->Speak(wtext.c_str(), SPF_IS_NOT_XML, nullptr);
            return SUCCEEDED(hr);
        } catch (...) {
            return false;
        }
    }
    
    void stop() override {
        if (isAvailable() && voice) {
            try {
                voice->Speak(nullptr, SPF_PURGEBEFORESPEAK | SPF_ASYNC, nullptr);
            } catch (...) {}
        }
    }
    
    bool isSpeaking() const override {
        if (!isAvailable() || !voice) {
            return false;
        }
        
        try {
            SPVOICESTATUS status;
            HRESULT hr = voice->GetStatus(&status, nullptr);
            if (FAILED(hr)) {
                return false;
            }
            
            return (status.dwRunningState == SPRS_IS_SPEAKING);
        } catch (...) {
            return false;
        }
    }
    
    void setRate(TTSRate rate) override {
        this->rate = rate;
        if (isAvailable()) {
            // SAPI rate range is -10 to +10
            long sapiRate = static_cast<long>(rate) * 2;
            voice->SetRate(sapiRate);
        }
    }
    
    void setVolume(int vol) override {
        volume = std::max(0, std::min(100, vol));
        if (isAvailable()) {
            voice->SetVolume(static_cast<USHORT>(volume));
        }
    }
    
    bool setLanguage(const std::string& languageCode) override {
        if (!isAvailable()) {
            return false;
        }
        
        // Convert language code to LCID (e.g., "en-US" -> 0x0409)
        // This is a simplified implementation
        LCID lcid = 0;
        if (languageCode == "en-US" || languageCode == "en" || languageCode == "eng") {
            lcid = 0x0409;
        } else if (languageCode == "de-DE" || languageCode == "de" || languageCode == "deu") {
            lcid = 0x0407;
        }
        
        if (lcid == 0) {
            return false; // Unsupported language
        }
        
        // Get voice token with matching language
        ComPtr<ISpObjectToken> cpVoiceToken;
        ComPtr<IEnumSpObjectTokens> cpEnum;
        
        WCHAR szRequired[256];
        swprintf(szRequired, 256, L"Language=%x", lcid);
        
        // Get token category for voices
        ComPtr<ISpObjectTokenCategory> cpTokenCategory;
        HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                      IID_ISpObjectTokenCategory, (void**)&cpTokenCategory);
        if (FAILED(hr)) {
            return false;
        }
        
        hr = cpTokenCategory->SetId(SPCAT_VOICES, FALSE);
        if (FAILED(hr)) {
            return false;
        }
        
        hr = cpTokenCategory->EnumTokens(szRequired, nullptr, &cpEnum);
        if (FAILED(hr)) {
            return false;
        }
        
        // Get first matching voice
        hr = cpEnum->Next(1, &cpVoiceToken, nullptr);
        if (SUCCEEDED(hr) && cpVoiceToken) {
            voice->SetVoice(cpVoiceToken.get());
            return true;
        }
        
        return false;
    }
    
    std::vector<std::string> getAvailableVoices() const override {
        std::vector<std::string> voices;
        
        if (!isAvailable()) {
            return voices;
        }
        
        ComPtr<IEnumSpObjectTokens> cpEnum;
        
        // Get token category for voices
        ComPtr<ISpObjectTokenCategory> cpTokenCategory;
        HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                      IID_ISpObjectTokenCategory, (void**)&cpTokenCategory);
        if (FAILED(hr)) {
            return voices;
        }
        
        hr = cpTokenCategory->SetId(SPCAT_VOICES, FALSE);
        if (FAILED(hr)) {
            return voices;
        }
        
        hr = cpTokenCategory->EnumTokens(nullptr, nullptr, &cpEnum);
        if (FAILED(hr)) {
            return voices;
        }
        
        ULONG count = 0;
        hr = cpEnum->GetCount(&count);
        if (FAILED(hr)) {
            return voices;
        }
        
        for (ULONG i = 0; i < count; i++) {
            ComPtr<ISpObjectToken> cpVoiceToken;
            hr = cpEnum->Next(1, &cpVoiceToken, nullptr);
            if (SUCCEEDED(hr) && cpVoiceToken) {
                LPWSTR pszDescription = nullptr;
                hr = cpVoiceToken->GetStringValue(nullptr, &pszDescription);
                if (SUCCEEDED(hr) && pszDescription) {
                    voices.push_back(wideToUtf8(pszDescription));
                    CoTaskMemFree(pszDescription);
                }
            }
        }
        
        return voices;
    }
    
    bool setVoice(const std::string& voiceName) override {
        if (!isAvailable()) {
            return false;
        }
        
        ComPtr<IEnumSpObjectTokens> cpEnum;
        
        // Get token category for voices
        ComPtr<ISpObjectTokenCategory> cpTokenCategory;
        HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                      IID_ISpObjectTokenCategory, (void**)&cpTokenCategory);
        if (FAILED(hr)) {
            return false;
        }
        
        hr = cpTokenCategory->SetId(SPCAT_VOICES, FALSE);
        if (FAILED(hr)) {
            return false;
        }
        
        hr = cpTokenCategory->EnumTokens(nullptr, nullptr, &cpEnum);
        if (FAILED(hr)) {
            return false;
        }
        
        ULONG count = 0;
        hr = cpEnum->GetCount(&count);
        if (FAILED(hr)) {
            return false;
        }
        
        for (ULONG i = 0; i < count; i++) {
            ComPtr<ISpObjectToken> cpVoiceToken;
            hr = cpEnum->Next(1, &cpVoiceToken, nullptr);
            if (SUCCEEDED(hr) && cpVoiceToken) {
                LPWSTR pszDescription = nullptr;
                hr = cpVoiceToken->GetStringValue(nullptr, &pszDescription);
                if (SUCCEEDED(hr) && pszDescription) {
                    std::string desc = wideToUtf8(pszDescription);
                    CoTaskMemFree(pszDescription);
                    
                    if (desc.find(voiceName) != std::string::npos) {
                        voice->SetVoice(cpVoiceToken.get());
                        return true;
                    }
                }
            }
        }
        
        return false;
    }
    
    void setStatusCallback(std::function<void(TTSStatus)> callback) override {
        statusCallback = callback;
        // Note: Full callback implementation would require event handling
        // This is a simplified version
    }
    
private:
    bool initialized;
    ISpVoice* voice;
    int volume;
    TTSRate rate;
    bool comInitializedByUs;
    std::function<void(TTSStatus)> statusCallback;
    
    // Helper to convert UTF-8 to wide string
    std::wstring utf8ToWide(const std::string& utf8) const {
        if (utf8.empty()) {
            return std::wstring();
        }
        
        int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        if (size == 0) {
            return std::wstring();
        }
        
        std::wstring result(size - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], size);
        return result;
    }
    
    // Helper to convert wide string to UTF-8
    std::string wideToUtf8(const std::wstring& wide) const {
        if (wide.empty()) {
            return std::string();
        }
        
        int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size == 0) {
            return std::string();
        }
        
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &result[0], size, nullptr, nullptr);
        return result;
    }
};

std::wstring utf8ToWideString(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (size == 0) {
        DWORD err = GetLastError();
        std::string msg = "NVDA UTF-8 conversion failed, error=" + std::to_string(err);
        OutputDebugStringA(msg.c_str());
        return std::wstring();
    }
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], size);
    return result;
}

/**
 * NVDA controller client TTS implementation
 *
 * The NVDA Controller Client DLL is a redistributable component that is NOT
 * installed into the system directory by NVDA itself.  Applications must ship
 * the DLL alongside their executable or locate it through other means.
 *
 * Detection strategy (in order):
 *  1. Standard LoadLibrary search (app directory, system dirs, PATH).
 *  2. "lib/" sub-directory next to the running executable.
 *  3. Direct NVDA registry key lookup (fast path).
 *  4. Full registry enumeration of Uninstall keys.
 *  5. Well-known default installation paths (expanded via %ProgramFiles%).
 *  6. NVDA_PATH environment variable (user override).
 *  7. Download from official NVDA release server as last resort (if NVDA
 *     process is running but DLL was not found anywhere).
 */
static bool isNvdaProcessRunning() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{ sizeof(PROCESSENTRY32W) };
    bool found = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"nvda.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

static HMODULE tryLoadNvdaDll(const std::string& dir, const char* dllName) {
    if (dir.empty()) return nullptr;
    std::string path = dir;
    if (path.back() != '\\' && path.back() != '/') path += '\\';
    path += dllName;
    return LoadLibraryA(path.c_str());
}

// Fast path: open NVDA's well-known uninstall registry key directly
static std::string getNvdaInstallPathDirect() {
    const char* directKeys[] = {
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NVDA",
        "SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NVDA"
    };
    for (const char* keyPath : directKeys) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char installLoc[MAX_PATH] = {};
            DWORD size = sizeof(installLoc);
            DWORD type = 0;
            if (RegQueryValueExA(hKey, "InstallLocation", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(installLoc), &size) == ERROR_SUCCESS && installLoc[0]) {
                RegCloseKey(hKey);
                return std::string(installLoc);
            }
            RegCloseKey(hKey);
        }
    }
    return {};
}

// Slow path: enumerate all Uninstall subkeys looking for NVDA
static std::string getNvdaInstallPathFromRegistry() {
    // Try the fast direct lookup first
    std::string directResult = getNvdaInstallPathDirect();
    if (!directResult.empty()) return directResult;

    const char* uninstallKeys[] = {
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    };
    for (const char* baseKey : uninstallKeys) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, baseKey, 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hKey) != ERROR_SUCCESS)
            continue;
        char subKeyName[256];
        for (DWORD idx = 0; RegEnumKeyA(hKey, idx, subKeyName, sizeof(subKeyName)) == ERROR_SUCCESS; ++idx) {
            HKEY hSubKey = nullptr;
            if (RegOpenKeyExA(hKey, subKeyName, 0, KEY_READ, &hSubKey) != ERROR_SUCCESS)
                continue;
            char displayName[256] = {};
            DWORD size = sizeof(displayName);
            DWORD type = 0;
            bool isNvda = false;
            if (RegQueryValueExA(hSubKey, "DisplayName", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(displayName), &size) == ERROR_SUCCESS) {
                // Case-insensitive check for "NVDA" in the display name
                std::string dn(displayName);
                std::transform(dn.begin(), dn.end(), dn.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (dn.find("nvda") != std::string::npos) isNvda = true;
            }
            if (isNvda) {
                char installLoc[MAX_PATH] = {};
                size = sizeof(installLoc);
                if (RegQueryValueExA(hSubKey, "InstallLocation", nullptr, &type,
                                     reinterpret_cast<LPBYTE>(installLoc), &size) == ERROR_SUCCESS && installLoc[0]) {
                    RegCloseKey(hSubKey);
                    RegCloseKey(hKey);
                    return std::string(installLoc);
                }
            }
            RegCloseKey(hSubKey);
        }
        RegCloseKey(hKey);
    }
    return {};
}

static std::string getExeDirectory() {
    char buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0) return {};
    std::string path(buf, len);
    auto pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos) : path;
}

static std::string expandEnvPath(const char* envVar) {
    char buf[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA(envVar, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) return std::string(buf, len);
    return {};
}

// ============================================================================
// NVDA Controller Client DLL Download — Multiple fallback strategies
// The DLL is LGPL-licensed and explicitly intended for redistribution.
// Note: dllName is always a compile-time constant from our code, never user input.
// ============================================================================

static constexpr DWORD NVDA_DOWNLOAD_TIMEOUT_MS = 45000;  // 45 seconds max for download+extract

// Known direct URL for the latest stable controllerClient package from NV Access
// Note: This is versioned; we also try discovering the URL from the directory listing
static constexpr const char* NVDA_DIRECT_DOWNLOAD_URL =
    "https://download.nvaccess.org/releases/stable/nvda_2025.3.2_controllerClient.zip";
// Fallback: NV Access official stable release directory (for URL discovery)
static constexpr const char* NVDA_STABLE_BASE_URL =
    "https://download.nvaccess.org/releases/stable/";
// Web page for manual download (browser fallback)
static constexpr const char* NVDA_CONTROLLER_DOWNLOAD_PAGE =
    "https://www.nvaccess.org/files/nvda/releases/stable/";

// Run a command silently and wait for completion with timeout.
// Returns true if the process exited with code 0.
static bool runSilentCommand(const std::string& cmdLine, DWORD timeoutMs = NVDA_DOWNLOAD_TIMEOUT_MS) {
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }

    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        // Timeout — terminate the process
        TerminateProcess(pi.hProcess, 1);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (exitCode == 0);
}

// Strategy A: Download a file using URLDownloadToFile (urlmon.dll — native Win32, no PowerShell)
static bool downloadFileUrlmon(const std::string& url, const std::string& destPath) {
    OutputDebugStringA(("NVDA DLL download: Trying URLDownloadToFile for " + url).c_str());
    // Use MultiByteToWideChar for proper UTF-8 path handling
    int urlLen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    int destLen = MultiByteToWideChar(CP_UTF8, 0, destPath.c_str(), -1, nullptr, 0);
    if (urlLen <= 0 || destLen <= 0) return false;
    std::wstring wUrl(urlLen, L'\0');
    std::wstring wDest(destLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wUrl[0], urlLen);
    MultiByteToWideChar(CP_UTF8, 0, destPath.c_str(), -1, &wDest[0], destLen);
    HRESULT hr = URLDownloadToFileW(nullptr, wUrl.c_str(), wDest.c_str(), 0, nullptr);
    if (SUCCEEDED(hr)) {
        DWORD attrs = GetFileAttributesA(destPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) return true;
    }
    OutputDebugStringA("URLDownloadToFile failed.");
    return false;
}

// Strategy B: Download a file using curl.exe (ships with Windows 10 1803+)
static bool downloadFileCurl(const std::string& url, const std::string& destPath) {
    OutputDebugStringA(("NVDA DLL download: Trying curl.exe for " + url).c_str());
    std::string cmd = "curl.exe -fsSL --connect-timeout 15 --max-time 30 -o \"" + destPath + "\" \"" + url + "\"";
    if (runSilentCommand(cmd)) {
        DWORD attrs = GetFileAttributesA(destPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) return true;
    }
    OutputDebugStringA("curl.exe download failed.");
    DeleteFileA(destPath.c_str());
    return false;
}

// Strategy C: PowerShell download (Invoke-WebRequest)
static bool downloadFilePowerShell(const std::string& url, const std::string& destPath) {
    OutputDebugStringA(("NVDA DLL download: Trying PowerShell for " + url).c_str());
    std::string cmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \""
        "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
        "Invoke-WebRequest -Uri '" + url + "' -OutFile '" + destPath + "' -UseBasicParsing\"";
    if (runSilentCommand(cmd)) {
        DWORD attrs = GetFileAttributesA(destPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) return true;
    }
    OutputDebugStringA("PowerShell download failed.");
    DeleteFileA(destPath.c_str());
    return false;
}

// Strategy D: bitsadmin (legacy but available on all Windows versions)
static bool downloadFileBitsadmin(const std::string& url, const std::string& destPath) {
    OutputDebugStringA(("NVDA DLL download: Trying bitsadmin for " + url).c_str());
    std::string cmd = "bitsadmin /transfer NVDADownload /priority foreground \"" + url + "\" \"" + destPath + "\"";
    if (runSilentCommand(cmd, 60000)) {  // bitsadmin can be slow
        DWORD attrs = GetFileAttributesA(destPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) return true;
    }
    OutputDebugStringA("bitsadmin download failed.");
    DeleteFileA(destPath.c_str());
    return false;
}

// Download a file trying all strategies in order
static bool downloadFileMultiStrategy(const std::string& url, const std::string& destPath) {
    // Remove any existing partial file
    DeleteFileA(destPath.c_str());

    if (downloadFileUrlmon(url, destPath)) return true;
    if (downloadFileCurl(url, destPath)) return true;
    if (downloadFilePowerShell(url, destPath)) return true;
    if (downloadFileBitsadmin(url, destPath)) return true;
    return false;
}

// Extract a specific DLL from a ZIP using PowerShell, then try tar as fallback
static bool extractDllFromZip(const std::string& zipPath, const std::string& dllName,
                               const std::string& dllDestPath, const std::string& libDir) {
    std::string extractDir = libDir + "\\nvda_extract";

    // Try PowerShell Expand-Archive first
    {
        std::string psScript = libDir + "\\nvda_extract.ps1";
        {
            std::ofstream ofs(psScript);
            if (ofs) {
                ofs << "$ErrorActionPreference = 'Stop'\n"
                    << "Expand-Archive -Path '" << zipPath << "' -DestinationPath '" << extractDir << "' -Force\n"
                    << "$src = Get-ChildItem -Path '" << extractDir << "' -Recurse -Filter '" << dllName << "' | Select-Object -First 1\n"
                    << "if ($src) { Copy-Item $src.FullName '" << dllDestPath << "' -Force }\n";
                ofs.close();
                if (ofs.good()) {
                    std::string cmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + psScript + "\"";
                    runSilentCommand(cmd);
                }
            }
            DeleteFileA(psScript.c_str());
        }

        // Check if extraction succeeded
        DWORD attrs = GetFileAttributesA(dllDestPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            // Clean up
            std::string cleanCmd = "cmd.exe /c rd /s /q \"" + extractDir + "\"";
            runSilentCommand(cleanCmd, 5000);
            DeleteFileA(zipPath.c_str());
            return true;
        }
    }

    // Fallback: try tar (available on Windows 10 1803+)
    {
        CreateDirectoryA(extractDir.c_str(), nullptr);
        std::string tarCmd = "tar.exe -xf \"" + zipPath + "\" -C \"" + extractDir + "\"";
        runSilentCommand(tarCmd, 15000);

        // Search for the DLL recursively with dir /s
        std::string searchCmd = "cmd.exe /c dir /b /s \"" + extractDir + "\\" + dllName + "\" > \"" + libDir + "\\nvda_found.txt\"";
        runSilentCommand(searchCmd, 5000);

        // Read the found path
        std::string foundFile = libDir + "\\nvda_found.txt";
        std::ifstream ifs(foundFile);
        std::string foundPath;
        if (ifs && std::getline(ifs, foundPath) && !foundPath.empty()) {
            ifs.close();
            CopyFileA(foundPath.c_str(), dllDestPath.c_str(), FALSE);
        }
        DeleteFileA(foundFile.c_str());

        // Clean up
        std::string cleanCmd = "cmd.exe /c rd /s /q \"" + extractDir + "\"";
        runSilentCommand(cleanCmd, 5000);
        DeleteFileA(zipPath.c_str());

        DWORD attrs = GetFileAttributesA(dllDestPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) return true;
    }

    return false;
}

// Main download function — tries multiple URLs and download strategies
static bool downloadNvdaControllerDll(const std::string& targetDir, const char* dllName) {
    std::string targetPath = targetDir;
    if (targetPath.back() != '\\' && targetPath.back() != '/') targetPath += '\\';
    std::string libDir = targetPath + "lib";

    // Create lib directory if it doesn't exist
    CreateDirectoryA(libDir.c_str(), nullptr);

    std::string dllPath = libDir + "\\" + dllName;

    // Check if already downloaded
    DWORD attrs = GetFileAttributesA(dllPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        return true;  // Already exists
    }

    OutputDebugStringA("NVDA controller client DLL not found locally. Attempting download...");

    std::string tempZip = libDir + "\\nvda_controllerClient.zip";

    // Try URL 1: Direct download of known stable version (no directory listing required)
    if (downloadFileMultiStrategy(NVDA_DIRECT_DOWNLOAD_URL, tempZip)) {
        OutputDebugStringA("Downloaded ZIP from NV Access direct URL.");
        if (extractDllFromZip(tempZip, dllName, dllPath, libDir)) {
            OutputDebugStringA("NVDA controller DLL extracted successfully from direct download.");
            return true;
        }
    }

    // Try URL 2: Discover the versioned URL from NV Access stable directory
    // Use PowerShell to scrape the directory listing and download in one step
    {
        std::string psScript = libDir + "\\nvda_download.ps1";
        {
            std::ofstream ofs(psScript);
            if (ofs) {
                ofs << "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12\n"
                    << "$baseUrl = '" << NVDA_STABLE_BASE_URL << "'\n"
                    << "try {\n"
                    << "  $page = Invoke-WebRequest -Uri $baseUrl -UseBasicParsing\n"
                    << "  $link = ($page.Links | Where-Object { $_.href -match 'controllerClient\\.zip$' } | Select-Object -First 1).href\n"
                    << "  if (-not $link) { exit 1 }\n"
                    << "  if ($link -notmatch '^http') { $link = $baseUrl + $link }\n"
                    << "  Invoke-WebRequest -Uri $link -OutFile '" << tempZip << "' -UseBasicParsing\n"
                    << "} catch { exit 1 }\n";
                ofs.close();
            }
        }
        std::string cmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + psScript + "\"";
        runSilentCommand(cmd);
        DeleteFileA(psScript.c_str());

        attrs = GetFileAttributesA(tempZip.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            OutputDebugStringA("Downloaded ZIP from NV Access stable directory.");
            if (extractDllFromZip(tempZip, dllName, dllPath, libDir)) {
                OutputDebugStringA("NVDA controller DLL extracted successfully from NV Access ZIP.");
                return true;
            }
        }
    }

    // All download strategies failed
    OutputDebugStringA("All NVDA controller DLL download strategies failed.");
    DeleteFileA(tempZip.c_str());
    return false;
}

class NVDATTSEngine : public ITTSEngine {
public:
    NVDATTSEngine() : initialized(false), nvdaLibrary(nullptr),
                      speakText(nullptr), cancelSpeech(nullptr), testIfRunning(nullptr),
                      speakSsml(nullptr), brailleMessage(nullptr), currentRate(TTSRate::NORMAL) {
    }

    ~NVDATTSEngine() override {
        shutdown();
    }

    bool initialize() override {
        if (initialized) {
            return true;
        }
        constexpr const char* kNvdaDll64 = "nvdaControllerClient64.dll";
        constexpr const char* kNvdaDll32 = "nvdaControllerClient32.dll";
        constexpr const char* kNvdaDllCompat = "nvdaControllerClient.dll";
        const char* primaryDll =
#if defined(_WIN64)
            kNvdaDll64;
#else
            kNvdaDll32;
#endif
        const char* dllCandidates[] = { primaryDll, kNvdaDllCompat };

        // --- Phase 1: Standard LoadLibrary search (app dir, system, PATH) ---
        for (const char* dll : dllCandidates) {
            nvdaLibrary = LoadLibraryA(dll);
            if (nvdaLibrary) break;
        }

        // --- Phase 2: "lib/" sub-directory next to executable ---
        std::string exeDir = getExeDirectory();
        if (!nvdaLibrary && !exeDir.empty()) {
            std::string libDir = exeDir + "\\lib";
            for (const char* dll : dllCandidates) {
                nvdaLibrary = tryLoadNvdaDll(libDir, dll);
                if (nvdaLibrary) break;
            }
        }

        // --- Phase 3: NVDA install path from registry (direct key first, then enumeration) ---
        if (!nvdaLibrary) {
            std::string nvdaPath = getNvdaInstallPathFromRegistry();
            if (!nvdaPath.empty()) {
                for (const char* dll : dllCandidates) {
                    nvdaLibrary = tryLoadNvdaDll(nvdaPath, dll);
                    if (nvdaLibrary) break;
                }
            }
        }

        // --- Phase 4: Well-known default install locations (using environment variables) ---
        if (!nvdaLibrary) {
            std::string progFiles = expandEnvPath("ProgramFiles");
            std::string progFilesX86 = expandEnvPath("ProgramFiles(x86)");
            std::vector<std::string> defaultPaths;
            if (!progFiles.empty()) {
                defaultPaths.push_back(progFiles + "\\NVDA");
                defaultPaths.push_back(progFiles + "\\NV Access\\NVDA");
            }
            if (!progFilesX86.empty()) {
                defaultPaths.push_back(progFilesX86 + "\\NVDA");
                defaultPaths.push_back(progFilesX86 + "\\NV Access\\NVDA");
            }
            // Hardcoded fallback if env vars fail
            defaultPaths.push_back("C:\\Program Files\\NVDA");
            defaultPaths.push_back("C:\\Program Files (x86)\\NVDA");
            for (const auto& dir : defaultPaths) {
                for (const char* dll : dllCandidates) {
                    nvdaLibrary = tryLoadNvdaDll(dir, dll);
                    if (nvdaLibrary) break;
                }
                if (nvdaLibrary) break;
            }
        }

        // --- Phase 5: NVDA_PATH environment variable ---
        if (!nvdaLibrary) {
            std::string envPath = expandEnvPath("NVDA_PATH");
            if (!envPath.empty()) {
                for (const char* dll : dllCandidates) {
                    nvdaLibrary = tryLoadNvdaDll(envPath, dll);
                    if (nvdaLibrary) break;
                }
            }
        }

        if (!nvdaLibrary) {
            if (isNvdaProcessRunning()) {
                OutputDebugStringA("NVDA is running but controller client DLL not found. "
                                   "Application will offer download via TTS prompt.");
            }
            return false;
        }
        speakText = reinterpret_cast<SpeakTextFn>(GetProcAddress(nvdaLibrary, "nvdaController_speakText"));
        cancelSpeech = reinterpret_cast<CancelSpeechFn>(GetProcAddress(nvdaLibrary, "nvdaController_cancelSpeech"));
        testIfRunning = reinterpret_cast<TestIfRunningFn>(GetProcAddress(nvdaLibrary, "nvdaController_testIfRunning"));
        // Optional v2 API: SSML support for prosody/rate control (NVDA 2021.1+)
        speakSsml = reinterpret_cast<SpeakSsmlFn>(GetProcAddress(nvdaLibrary, "nvdaController_speakSsml"));
        // Optional: braille display message
        brailleMessage = reinterpret_cast<BrailleMessageFn>(GetProcAddress(nvdaLibrary, "nvdaController_brailleMessage"));
        if (!speakText || !cancelSpeech || !testIfRunning) {
            FreeLibrary(nvdaLibrary);
            nvdaLibrary = nullptr;
            return false;
        }
        // nvdaController_testIfRunning returns 0 on success, non-zero when NVDA is not running.
        if (testIfRunning() != 0) {
            FreeLibrary(nvdaLibrary);
            nvdaLibrary = nullptr;
            return false;
        }
        initialized = true;
        return true;
    }

    void shutdown() override {
        if (!initialized) {
            return;
        }
        try { stop(); } catch (...) {}
        // Allow NVDA's async internal processing to settle after cancelSpeech()
        // before we null out the function pointers.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // Do NOT call FreeLibrary(nvdaLibrary). The NVDA controller client DLL
        // may have internal callback threads still running asynchronously after
        // cancelSpeech() returns. Unloading the DLL while those threads are
        // active causes an ACCESS VIOLATION crash. The DLL remains loaded for
        // the process lifetime — this is safe and prevents the crash.
        speakText = nullptr;
        cancelSpeech = nullptr;
        testIfRunning = nullptr;
        speakSsml = nullptr;
        brailleMessage = nullptr;
        initialized = false;
    }

    bool isAvailable() const override {
        return initialized && nvdaLibrary;
    }

    bool speak(const std::string& text, bool interrupt = false) override {
        if (!isAvailable() || !speakText) {
            return false;
        }
        try {
            // Only cancel previous speech when explicitly requested.
            // Unconditional cancel breaks queued speech patterns like
            // announceFullStatus() which speaks status text followed by "Ende."
            // — the second speak() would cancel the first, leaving only "Ende."
            if (interrupt && cancelSpeech) {
                cancelSpeech();
            }
            // If rate is non-default and SSML API is available, use prosody rate control
            if (speakSsml && currentRate != TTSRate::NORMAL) {
                std::string ssml = buildSsmlWithRate(text, currentRate);
                std::wstring wssml = utf8ToWideString(ssml);
                if (!wssml.empty()) {
                    // speakSsml signature: (const wchar_t* ssml, int symbolLevel, int priority)
                    // symbolLevel -1 = use NVDA default, priority 0 = normal
                    if (speakSsml(wssml.c_str(), -1, 0) == 0) {
                        return true;
                    }
                }
                // Fall through to plain speakText if SSML fails
            }
            std::wstring wtext = utf8ToWideString(text);
            if (wtext.empty()) {
                return false;
            }
            return speakText(wtext.c_str()) == 0;
        } catch (...) {
            return false;
        }
    }

    bool speakSync(const std::string& text) override {
        return speak(text, true);
    }

    void stop() override {
        if (isAvailable()) {
            cancelSpeech();
        }
    }

    bool isSpeaking() const override {
        // NVDA Controller Client API does not expose speaking state.
        return false;
    }

    void setRate(TTSRate rate) override {
        currentRate = rate;
    }

    void setVolume(int /*volume*/) override {
        // NVDA handles volume internally; no controller API exposed.
    }

    bool setLanguage(const std::string& /*languageCode*/) override {
        // NVDA manages language per user profile.
        return false;
    }

    std::vector<std::string> getAvailableVoices() const override {
        // NVDA manages voices internally.
        return {};
    }

    bool setVoice(const std::string& /*voiceName*/) override {
        // NVDA manages voice selection internally.
        return false;
    }

    void setStatusCallback(std::function<void(TTSStatus)> /*callback*/) override {}

private:
    using SpeakTextFn = int (__stdcall *)(const wchar_t*);
    using CancelSpeechFn = int (__stdcall *)();
    using TestIfRunningFn = int (__stdcall *)();
    // NVDA controller client v2 API (NVDA 2021.1+): SSML with symbol level and priority
    using SpeakSsmlFn = int (__stdcall *)(const wchar_t*, int, int);
    using BrailleMessageFn = int (__stdcall *)(const wchar_t*);

    bool initialized;
    HMODULE nvdaLibrary;
    SpeakTextFn speakText;
    CancelSpeechFn cancelSpeech;
    TestIfRunningFn testIfRunning;
    SpeakSsmlFn speakSsml;         // Optional: nullptr if v2 API not available
    BrailleMessageFn brailleMessage; // Optional: nullptr if not available
    TTSRate currentRate;

    // Build SSML string with prosody rate wrapper
    static std::string buildSsmlWithRate(const std::string& text, TTSRate rate) {
        // Map TTSRate to SSML prosody rate percentage
        const char* rateStr = "100%";
        switch (rate) {
            case TTSRate::VERY_SLOW: rateStr = "60%";  break;
            case TTSRate::SLOW:      rateStr = "80%";  break;
            case TTSRate::NORMAL:    rateStr = "100%"; break;
            case TTSRate::FAST:      rateStr = "130%"; break;
            case TTSRate::VERY_FAST: rateStr = "170%"; break;
        }
        // Escape XML special characters in text to prevent malformed SSML
        std::string escaped;
        escaped.reserve(text.size() + 16);
        for (char c : text) {
            switch (c) {
                case '&':  escaped += "&amp;";  break;
                case '<':  escaped += "&lt;";   break;
                case '>':  escaped += "&gt;";   break;
                case '"':  escaped += "&quot;"; break;
                case '\'': escaped += "&apos;"; break;
                default:   escaped += c;        break;
            }
        }
        return std::string("<speak><prosody rate=\"") + rateStr + "\">" + escaped + "</prosody></speak>";
    }
};

// ─── espeak-NG TTS engine for Windows ────────────────────────────────────────

static std::string findEspeakNgBinaryWin() {
    // Standard installation paths for espeak-NG on Windows
    const char* candidates[] = {
        "C:\\Program Files\\eSpeak NG\\espeak-ng.exe",
        "C:\\Program Files (x86)\\eSpeak NG\\espeak-ng.exe",
        "C:\\ProgramData\\chocolatey\\bin\\espeak-ng.exe",
        nullptr
    };
    for (int i = 0; candidates[i]; i++) {
        DWORD attrs = GetFileAttributesA(candidates[i]);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            return candidates[i];
        }
    }

    // Check %USERPROFILE%\scoop\shims
    char userProfile[MAX_PATH] = {};
    if (GetEnvironmentVariableA("USERPROFILE", userProfile, MAX_PATH) > 0) {
        std::string scoopPath = std::string(userProfile) + "\\scoop\\shims\\espeak-ng.exe";
        DWORD attrs = GetFileAttributesA(scoopPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            return scoopPath;
        }
    }

    // Check PATH via SearchPathA
    char foundPath[MAX_PATH] = {};
    if (SearchPathA(nullptr, "espeak-ng.exe", nullptr, MAX_PATH, foundPath, nullptr) > 0) {
        return foundPath;
    }

    return "";
}

static constexpr const char* ESPEAK_NG_DOWNLOAD_URL =
    "https://github.com/espeak-ng/espeak-ng/releases";

class EspeakNgWinTTSEngine : public ITTSEngine {
public:
    EspeakNgWinTTSEngine() = default;

    ~EspeakNgWinTTSEngine() override { shutdown(); }

    bool initialize() override {
        if (initialized) return true;

        espeakPath = findEspeakNgBinaryWin();
        if (espeakPath.empty()) {
            OutputDebugStringA("[TTS_WIN] espeak-ng.exe not found in standard paths.");
            return false;
        }

        OutputDebugStringA(("[TTS_WIN] Found espeak-ng: " + espeakPath).c_str());
        initialized = true;
        return true;
    }

    void shutdown() override {
        stop();
        // Block-join on shutdown to ensure clean exit
        if (speakThread.joinable()) {
            speakThread.join();
        }
        initialized = false;
    }

    bool isAvailable() const override { return initialized; }

    bool speak(const std::string& text, bool interrupt = false) override {
        if (!initialized) return false;
        // eSpeak-NG runs as an external process — it cannot queue speech.
        // Always stop the previous utterance to prevent parallel processes
        // that cause overlapping/stacking speech.
        stop();

        // Detach any previous speak thread — don't block with timed join.
        // The old process was already killed by stop() so the thread will
        // exit on its own.  Blocking here causes input latency and dropped
        // characters during rapid text entry.
        if (speakThread.joinable()) {
            speakThread.detach();
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

        // Build command line: espeak-ng.exe -s <rate> -a <volume> [-v <lang>] "<text>"
        std::string cmdLine = "\"" + espeakPath + "\"";
        cmdLine += " -s " + std::to_string(getEspeakRate());
        cmdLine += " -a " + std::to_string(volume);
        if (!languageCode.empty()) {
            cmdLine += " -v " + languageCode;
        }
        // Sanitize text: remove shell metacharacters to prevent command injection
        std::string safeText;
        safeText.reserve(text.size());
        for (char ch : text) {
            // Allow only safe characters: letters, digits, spaces, basic punctuation
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == ' ' || ch == '.' ||
                ch == ',' || ch == '!' || ch == '?' || ch == '-' ||
                ch == ':' || ch == ';' || ch == '\'' || ch == '(' || ch == ')') {
                safeText += ch;
            }
            // Skip potentially dangerous characters like ", &, |, <, >, ^, etc.
        }
        cmdLine += " \"" + safeText + "\"";

        // CreateProcessA may modify the command line buffer — use a mutable copy
        std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back('\0');

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};

        if (!CreateProcessA(nullptr, cmdBuf.data(),
                            nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(procMutex);
            childProcess = pi.hProcess;
            childThread = pi.hThread;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);

        {
            std::lock_guard<std::mutex> lock(procMutex);
            // Only close handles if stop() hasn't already taken ownership
            if (childProcess == pi.hProcess) {
                CloseHandle(pi.hProcess);
                childProcess = nullptr;
            }
            if (childThread == pi.hThread) {
                CloseHandle(pi.hThread);
                childThread = nullptr;
            }
        }

        return true;
    }

    void stop() override {
        HANDLE procToKill = nullptr;
        HANDLE threadToClose = nullptr;
        {
            std::lock_guard<std::mutex> lock(procMutex);
            procToKill = childProcess;
            threadToClose = childThread;
            childProcess = nullptr;
            childThread = nullptr;
        }
        if (procToKill) {
            TerminateProcess(procToKill, 1);
            // Don't wait — TerminateProcess is asynchronous but the process
            // will exit almost immediately.  Waiting up to 100ms here was
            // blocking the game thread during rapid text input, causing
            // dropped characters and delayed TTS/click feedback.
            CloseHandle(procToKill);
        }
        if (threadToClose) {
            CloseHandle(threadToClose);
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
        std::vector<std::string> voices;
        if (!initialized) { voices.push_back("default"); return voices; }

        // Run: espeak-ng.exe --voices and parse output
        std::string cmdLine = "\"" + espeakPath + "\" --voices";
        std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back('\0');

        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            voices.push_back("default");
            return voices;
        }
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};

        if (CreateProcessA(nullptr, cmdBuf.data(),
                            nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(hWritePipe);
            hWritePipe = nullptr;

            std::string output;
            char buf[256];
            DWORD bytesRead = 0;
            while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                buf[bytesRead] = '\0';
                output += buf;
            }
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            // Parse: skip header, extract voice name (4th column)
            std::istringstream iss(output);
            std::string line;
            bool header = true;
            while (std::getline(iss, line)) {
                if (header) { header = false; continue; }
                // Format: "Pty Language Age/Gender VoiceName ..."
                char name[128] = {};
                if (std::sscanf(line.c_str(), " %*d %*s %*s %127s", name) == 1 && name[0]) {
                    voices.push_back(name);
                }
            }
        }
        if (hWritePipe) CloseHandle(hWritePipe);
        CloseHandle(hReadPipe);

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
    // Shutdown timeout — used only by shutdown()/destructor for clean exit
    static constexpr int SHUTDOWN_JOIN_TIMEOUT_MS = 500;
    bool initialized = false;
    std::atomic<bool> speaking{false};
    std::atomic<unsigned int> speakGeneration{0};
    HANDLE childProcess = nullptr;
    HANDLE childThread = nullptr;
    std::mutex procMutex;
    std::thread speakThread;  // Joinable thread for async speak
    int volume = 100;
    TTSRate rate = TTSRate::NORMAL;
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

// Open espeak-NG download page in the user's default browser
static void openEspeakNgDownloadPage() {
    ShellExecuteA(nullptr, "open", ESPEAK_NG_DOWNLOAD_URL, nullptr, nullptr, SW_SHOWNORMAL);
}

// Factory function for Windows
std::unique_ptr<ITTSEngine> createTTSEngine(TTSEngineType type) {
    if (type == TTSEngineType::NVDA) {
        return std::make_unique<NVDATTSEngine>();
    }
    if (type == TTSEngineType::ESPEAK_NG) {
        auto engine = std::make_unique<EspeakNgWinTTSEngine>();
        if (engine->initialize()) {
            return engine;
        }
        // espeak-NG not found — fall back to SAPI
        OutputDebugStringA("[TTS_WIN] espeak-NG not available, falling back to SAPI.");
        return std::make_unique<WindowsTTSEngine>();
    }
    return std::make_unique<WindowsTTSEngine>();
}

// Public API: Check if NVDA screen reader is running
bool isNvdaScreenReaderRunning() {
    return isNvdaProcessRunning();
}

// Public API: Download the NVDA controller client DLL
bool downloadNvdaControllerClientDll() {
    std::string exeDir = getExeDirectory();
    if (exeDir.empty()) return false;
#if defined(_WIN64)
    const char* dllName = "nvdaControllerClient64.dll";
#else
    const char* dllName = "nvdaControllerClient32.dll";
#endif
    return downloadNvdaControllerDll(exeDir, dllName);
}

// Public API: Open the NVDA controller client download page in the user's default browser
void openNvdaDllDownloadPage() {
    ShellExecuteA(nullptr, "open", NVDA_CONTROLLER_DOWNLOAD_PAGE, nullptr, nullptr, SW_SHOWNORMAL);
}

// Public API: Get the lib directory path where the DLL should be placed
std::string getNvdaDllTargetDirectory() {
    std::string exeDir = getExeDirectory();
    if (exeDir.empty()) return {};
    return exeDir + "\\lib";
}

// Public API: Send text to NVDA braille display
// Uses a static reference to the loaded DLL function.
// We re-load via LoadLibrary if needed (lightweight - DLL is already in memory).
bool sendNvdaBrailleMessage(const std::string& text) {
    // Try to find the NVDA controller client DLL
    using BrailleMessageFn = int (__stdcall *)(const wchar_t*);
    static BrailleMessageFn cachedFn = nullptr;
    static bool tried = false;
    
    if (!tried) {
        tried = true;
#if defined(_WIN64)
        const char* dllNames[] = {"nvdaControllerClient64.dll", "nvdaControllerClient.dll"};
#else
        const char* dllNames[] = {"nvdaControllerClient32.dll", "nvdaControllerClient.dll"};
#endif
        for (const char* dll : dllNames) {
            // First try already-loaded module
            HMODULE mod = GetModuleHandleA(dll);
            // If not loaded yet, try loading it explicitly
            if (!mod) {
                mod = LoadLibraryA(dll);
            }
            if (mod) {
                cachedFn = reinterpret_cast<BrailleMessageFn>(
                    GetProcAddress(mod, "nvdaController_brailleMessage"));
                if (cachedFn) break;
            }
        }
    }
    
    if (!cachedFn) return false;
    
    std::wstring wtext = utf8ToWideString(text);
    if (wtext.empty()) return false;
    return cachedFn(wtext.c_str()) == 0;
}
