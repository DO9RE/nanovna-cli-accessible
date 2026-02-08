#pragma once
#include <string>
#include <map>
#include <cstdint>
#include <functional>

class IComm {
public:
    virtual ~IComm() = default;
    virtual bool write(const std::string& s, std::string& err) = 0;
    virtual bool readLine(std::string& out, int timeoutMs, std::string& err) = 0;
};

struct CommandTemplates {
    std::map<std::string, std::string> map;
    bool loadFromFile(const std::string& path, std::string& err);
    std::string format(const std::string& key, const std::map<std::string,std::string>& vars) const;
};

// Progress callback: takes (current, total) where both are segment counts
using ProgressCallback = std::function<void(uint32_t current, uint32_t total)>;

class NanoVNAProtocol {
public:
    NanoVNAProtocol(IComm* comm, const CommandTemplates& templates);

    // Allow UI to rebind comm after user selects port.
    void setComm(IComm* newComm);

    // One-shot scan: reads lines until prompt "ch>".
    bool scan(uint64_t startHz, uint64_t stopHz, uint32_t points, uint32_t outmask, std::string& out, std::string& err);

    // Simulate fixed step size by stitching multiple scans (like NanoVNA Saver).
    // NanoVNA H4: max 401 points per sweep.
    bool scanByStep(uint64_t startHz, uint64_t stopHz, uint64_t stepHz, uint32_t outmask, std::string& out, std::string& err, uint32_t maxPointsPerSweep = 401);

    // Set progress callback for multi-segment scans
    void setProgressCallback(ProgressCallback callback) { progressCallback = callback; }
    
    // Set/check cancellation flag
    void setCancelled(bool cancelled) { isCancelled = cancelled; }
    bool getCancelled() const { return isCancelled; }

    bool sendCal(const std::string& calType, std::string& err);
    bool saveCal(std::string& err);
    bool saveCal(int bankNumber, std::string& err);  // Save to specific bank
    bool loadCal(std::string& err);
    bool loadCal(int bankNumber, std::string& err);  // Load from specific bank (recall)
    bool calReset(std::string& err);  // Reset calibration
    bool calDone(std::string& err);   // Finalize calibration
    bool getInfo(std::string& out, std::string& err);
    bool getBattery(std::string& out, std::string& err);
    
    // Validate device connection - sends simple command to check if NanoVNA is responding
    bool validateDevice(std::string& err);

private:
    IComm* comm;
    CommandTemplates templates;
    ProgressCallback progressCallback;
    bool isCancelled = false;
};
