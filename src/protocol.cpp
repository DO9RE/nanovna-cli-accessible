#include "protocol.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <thread>
#include <vector>
#include <cctype>

static inline void str_trim(std::string &s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
}

bool CommandTemplates::loadFromFile(const std::string& path, std::string& err) {
    map.clear();
    std::error_code ec;
    std::filesystem::path p = std::filesystem::u8path(path);

    if (!std::filesystem::exists(p, ec)) {
        err = "File not found: " + std::filesystem::absolute(p, ec).string();
        return false;
    }
    if (!std::filesystem::is_regular_file(p, ec)) {
        err = "Path exists but is not a regular file: " + std::filesystem::absolute(p, ec).string();
        return false;
    }

    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) {
        err = "Cannot open file: " + std::filesystem::absolute(p, ec).string();
        return false;
    }

    std::string content;
    ifs.seekg(0, std::ios::end);
    std::streampos size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    if (size > 0) {
        content.resize((size_t)size);
        ifs.read(&content[0], size);
    }

    if (content.size() >= 3 &&
        (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB &&
        (unsigned char)content[2] == 0xBF) {
        content = content.substr(3);
        err = "Removed UTF-8 BOM.";
    } else {
        err.clear();
    }

    std::istringstream iss(content);
    std::string line;
    auto trim = [](std::string &s){
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    };

    size_t entries = 0;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0,pos);
        std::string v = line.substr(pos+1);
        trim(k); trim(v);
        if (!k.empty() && !v.empty()) {
            map[k] = v;
            ++entries;
        }
    }

    if (entries == 0) {
        if (err.empty()) err = "No valid entries in: " + std::filesystem::absolute(p, ec).string();
        return false;
    }
    return true;
}

static std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
    return s;
}

std::string CommandTemplates::format(const std::string& key, const std::map<std::string,std::string>& vars) const {
    auto it = map.find(key);
    if (it == map.end()) return "";
    std::string tmpl = it->second;
    for (auto &p : vars) {
        tmpl = replace_all(tmpl, "{" + p.first + "}", p.second);
    }
    return tmpl;
}

NanoVNAProtocol::NanoVNAProtocol(IComm* comm_, const CommandTemplates& templates_)
: comm(comm_), templates(templates_) {}

void NanoVNAProtocol::setComm(IComm* newComm) { comm = newComm; }

static bool execCommandUntilPrompt(IComm* comm,
                                  const std::string& command,
                                  std::vector<std::string>& outLines,
                                  double waitSeconds,
                                  std::string& err) {
    outLines.clear();
    if (!comm) { err = "No communication interface"; return false; }

    if (!comm->write(command, err)) return false;

    int waitMs = (int)(waitSeconds * 1000.0);
    if (waitMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));

    // Reduced timeouts to prevent excessive waiting when device is not connected
    // Previous: 500 × 1200ms = 600s (10 minutes)
    // New: 25 attempts × 1200ms per readLine = 30s (30 seconds maximum wait)
    int timeouts = 0;
    const int max_timeouts = 25;  // Maximum 25 retry attempts (30 seconds total)
    while (true) {
        std::string line;
        if (!comm->readLine(line, 1200, err)) {
            if (err == "Timeout") {
                err.clear();
                if (++timeouts > max_timeouts) { 
                    err = "Timeout waiting for prompt - device may not be connected or responding"; 
                    return false; 
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            return false;
        }

        std::string trimmed = line;
        str_trim(trimmed);
        if (trimmed.empty()) continue;

        // Prompt marker: "ch>"
        if (trimmed.rfind("ch>", 0) == 0) break;

        // skip echo
        if (trimmed == command) continue;

        outLines.push_back(trimmed);
    }
    return true;
}

bool NanoVNAProtocol::scan(uint64_t startHz, uint64_t stopHz, uint32_t points, uint32_t outmask, std::string& out, std::string& err) {
    out.clear();
    if (!comm) { err = "No communication interface"; return false; }
    if (stopHz < startHz) { err = "stopHz < startHz"; return false; }
    
    // Special case: allow 1 point when start == stop (single frequency measurement)
    if (points < 1) { err = "points must be >= 1"; return false; }
    if (points < 2 && startHz != stopHz) { err = "points must be >= 2 when start != stop"; return false; }

    std::map<std::string,std::string> vars;
    vars["start"] = std::to_string(startHz);
    vars["stop"] = std::to_string(stopHz);
    vars["points"] = std::to_string(points);
    vars["outmask"] = std::to_string(outmask);

    std::string cmd = templates.format("SCAN", vars);
    if (cmd.empty()) cmd = "scan " + vars["start"] + " " + vars["stop"] + " " + vars["points"] + " " + vars["outmask"];

    std::vector<std::string> lines;
    if (!execCommandUntilPrompt(comm, cmd, lines, 0.05, err)) return false;

    std::ostringstream oss;
    for (size_t i=0;i<lines.size();++i) {
        if (i) oss << "\n";
        oss << lines[i];
    }
    out = oss.str();
    return true;
}

bool NanoVNAProtocol::scanByStep(uint64_t startHz, uint64_t stopHz, uint64_t stepHz, uint32_t outmask, std::string& out, std::string& err, uint32_t maxPointsPerSweep) {
    out.clear();
    if (stopHz < startHz) { err = "stopHz < startHz"; return false; }
    if (stepHz == 0) { err = "stepHz == 0"; return false; }
    if (maxPointsPerSweep < 2) { err = "maxPointsPerSweep must be >= 2"; return false; }

    uint64_t totalPoints = 1 + (stopHz - startHz) / stepHz;
    
    // Special case: single point measurement (start == stop)
    if (startHz == stopHz) {
        totalPoints = 1;
        if (progressCallback) progressCallback(0, 1);
        bool result = scan(startHz, stopHz, (uint32_t)totalPoints, outmask, out, err);
        if (result && progressCallback) progressCallback(1, 1);
        return result;
    }
    
    // Ensure minimum 2 points for range measurements
    if (totalPoints < 2) totalPoints = 2;

    // fits in one sweep
    if (totalPoints <= maxPointsPerSweep) {
        if (progressCallback) progressCallback(0, 1);
        bool result = scan(startHz, stopHz, (uint32_t)totalPoints, outmask, out, err);
        if (result && progressCallback) progressCallback(1, 1);
        return result;
    }

    // Multi-sweep stitching (NanoVNA Saver style).
    // Use 1-point overlap between segments.
    
    // Calculate total number of segments for progress reporting
    uint32_t totalSegments = 0;
    uint64_t tempStart = startHz;
    while (tempStart < stopHz) {
        totalSegments++;
        uint64_t segSpan = stepHz * (maxPointsPerSweep - 1);
        uint64_t segStop = tempStart + segSpan;
        if (segStop >= stopHz) break;
        if (segStop >= stepHz) tempStart = segStop - stepHz;
        else tempStart = segStop;
    }
    
    std::ostringstream accum;
    bool firstSegment = true;
    uint32_t currentSegment = 0;

    uint64_t segStart = startHz;
    while (segStart < stopHz) {
        // Check for cancellation before starting next segment
        if (isCancelled) {
            err = "Measurement cancelled by user";
            return false;
        }
        
        // Report progress before each segment
        if (progressCallback) {
            progressCallback(currentSegment, totalSegments);
        }
        
        uint64_t segPoints = maxPointsPerSweep;
        uint64_t segSpan = stepHz * (segPoints - 1);
        uint64_t segStop = segStart + segSpan;

        if (segStop > stopHz) {
            segStop = stopHz;
            segPoints = 1 + (segStop - segStart) / stepHz;
            if (segPoints < 2) segPoints = 2;
        }

        std::string segment;
        if (!scan(segStart, segStop, (uint32_t)segPoints, outmask, segment, err)) return false;

        // Stitch: skip first line of every segment except first to avoid duplicate frequency.
        std::istringstream iss(segment);
        std::string line;
        bool firstLine = true;
        while (std::getline(iss, line)) {
            str_trim(line);
            if (line.empty()) continue;
            if (!firstSegment && firstLine) {
                firstLine = false;
                continue;
            }
            firstLine = false;
            if (accum.tellp() > 0) accum << "\n";
            accum << line;
        }

        firstSegment = false;
        currentSegment++;
        
        if (segStop >= stopHz) break;

        // overlap one point
        if (segStop >= stepHz) segStart = segStop - stepHz;
        else segStart = segStop;
    }

    // Report final progress
    if (progressCallback) {
        progressCallback(totalSegments, totalSegments);
    }

    out = accum.str();
    return true;
}

bool NanoVNAProtocol::sendCal(const std::string& calType, std::string& err) {
    if (!comm) { err = "No communication interface"; return false; }
    std::map<std::string,std::string> vars;
    vars["caltype"] = calType;
    std::string cmd = templates.format("CAL", vars);
    if (cmd.empty()) cmd = "cal " + calType;
    return comm->write(cmd, err);
}

bool NanoVNAProtocol::saveCal(std::string& err) {
    if (!comm) { err = "No communication interface"; return false; }
    std::string cmd = templates.format("SAVE_CAL", {});
    if (cmd.empty()) cmd = "save";
    return comm->write(cmd, err);
}

bool NanoVNAProtocol::saveCal(int bankNumber, std::string& err) {
    if (!comm) { err = "No communication interface"; return false; }
    std::map<std::string,std::string> vars;
    vars["bank"] = std::to_string(bankNumber);
    std::string cmd = templates.format("SAVE_CAL_BANK", vars);
    if (cmd.empty()) cmd = "save " + std::to_string(bankNumber);
    return comm->write(cmd, err);
}

bool NanoVNAProtocol::loadCal(std::string& err) {
    if (!comm) { err = "No communication interface"; return false; }
    std::string cmd = templates.format("LOAD_CAL", {});
    if (cmd.empty()) cmd = "recall";
    return comm->write(cmd, err);
}

bool NanoVNAProtocol::loadCal(int bankNumber, std::string& err) {
    if (!comm) { err = "No communication interface"; return false; }
    std::map<std::string,std::string> vars;
    vars["bank"] = std::to_string(bankNumber);
    std::string cmd = templates.format("LOAD_CAL_BANK", vars);
    if (cmd.empty()) cmd = "recall " + std::to_string(bankNumber);
    
    // Execute command and check for error response
    std::vector<std::string> lines;
    if (!execCommandUntilPrompt(comm, cmd, lines, 0.05, err)) return false;
    
    // Check if response contains error pattern indicating bank has no data
    // Get error pattern from templates, default to "Err, default load"
    std::string errorPattern = templates.format("ERROR_BANK_EMPTY", {});
    if (errorPattern.empty()) errorPattern = "Err, default load";
    
    for (const auto& line : lines) {
        if (line.find(errorPattern) != std::string::npos) {
            err = "Bank " + std::to_string(bankNumber) + " contains no calibration data";
            return false;
        }
    }
    
    return true;
}

bool NanoVNAProtocol::calReset(std::string& err) {
    if (!comm) { err = "No communication interface"; return false; }
    std::string cmd = templates.format("CAL_RESET", {});
    if (cmd.empty()) cmd = "cal reset";
    return comm->write(cmd, err);
}

bool NanoVNAProtocol::calDone(std::string& err) {
    if (!comm) { err = "No communication interface"; return false; }
    std::string cmd = templates.format("CAL_DONE", {});
    if (cmd.empty()) cmd = "cal done";
    return comm->write(cmd, err);
}

bool NanoVNAProtocol::getInfo(std::string& out, std::string& err) {
    out.clear();
    if (!comm) { err = "No communication interface"; return false; }
    std::string cmd = templates.format("GET_INFO", {});
    if (cmd.empty()) cmd = "info";

    std::vector<std::string> lines;
    if (!execCommandUntilPrompt(comm, cmd, lines, 0.05, err)) return false;

    std::ostringstream oss;
    for (size_t i=0;i<lines.size();++i) {
        if (i) oss << "\n";
        oss << lines[i];
    }
    out = oss.str();
    return true;
}

bool NanoVNAProtocol::getBattery(std::string& out, std::string& err) {
    out.clear();
    if (!comm) { err = "No communication interface"; return false; }
    std::string cmd = templates.format("VBAT", {});
    if (cmd.empty()) cmd = "vbat";

    std::vector<std::string> lines;
    if (!execCommandUntilPrompt(comm, cmd, lines, 0.05, err)) return false;

    std::ostringstream oss;
    for (size_t i=0;i<lines.size();++i) {
        if (i) oss << "\n";
        oss << lines[i];
    }
    out = oss.str();
    return true;
}

bool NanoVNAProtocol::validateDevice(std::string& err) {
    // Try to get device info with a short wait time to validate connection
    // This prevents long hangs when device is not connected
    if (!comm) { 
        err = "No communication interface"; 
        return false; 
    }
    
    std::string cmd = templates.format("GET_INFO", {});
    if (cmd.empty()) cmd = "info";
    
    std::vector<std::string> lines;
    // Use short wait time (0.05s) for validation - device should respond quickly
    if (!execCommandUntilPrompt(comm, cmd, lines, 0.05, err)) {
        // More descriptive error for validation failure
        if (err.find("Timeout") != std::string::npos) {
            err = "Device not responding - no NanoVNA detected at this port. Please check:\n"
                  "  1. Device is connected and powered on\n"
                  "  2. Correct COM port is selected\n"
                  "  3. No other application is using the port";
        }
        return false;
    }
    
    // Device responded, validation successful
    if (lines.empty()) {
        // Device responded but gave no output - still valid connection
        return true;
    }
    
    return true;
}
