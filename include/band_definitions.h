#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>

// Amateur radio band definitions
struct AmateurBand {
    std::string name;
    uint64_t start_hz;
    uint64_t end_hz;
    
    uint64_t center() const {
        return (start_hz + end_hz) / 2;
    }
    
    uint64_t bandwidth() const {
        return end_hz - start_hz;
    }
};

// Load band plan from file
inline bool loadBandPlan(const std::string& filename, std::vector<AmateurBand>& bands, std::string& error) {
    bands.clear();
    
    std::error_code ec;
    std::filesystem::path filepath = std::filesystem::u8path(filename);
    
    if (!std::filesystem::exists(filepath, ec)) {
        error = "Band plan file not found: " + filename;
        return false;
    }
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        error = "Failed to open band plan file: " + filename;
        return false;
    }
    
    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
        
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Parse line: band_name=start_hz,end_hz
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;  // Skip malformed lines
        }
        
        std::string band_name = line.substr(0, eq_pos);
        std::string values = line.substr(eq_pos + 1);
        
        size_t comma_pos = values.find(',');
        if (comma_pos == std::string::npos) {
            continue;  // Skip malformed lines
        }
        
        std::string start_str = values.substr(0, comma_pos);
        std::string end_str = values.substr(comma_pos + 1);
        
        try {
            uint64_t start_hz = std::stoull(start_str);
            uint64_t end_hz = std::stoull(end_str);
            
            if (start_hz < end_hz) {
                bands.push_back({band_name, start_hz, end_hz});
            }
        } catch (...) {
            // Skip invalid numeric values
            continue;
        }
    }
    
    if (bands.empty()) {
        error = "No valid band definitions found in: " + filename;
        return false;
    }
    
    return true;
}

// Get list of available band plans
inline std::vector<std::pair<std::string, std::string>> getAvailableBandPlans(std::string& error) {
    std::vector<std::pair<std::string, std::string>> plans;
    
    std::error_code ec;
    std::filesystem::path bandplans_dir = std::filesystem::u8path("bandplans");
    
    if (!std::filesystem::exists(bandplans_dir, ec)) {
        error = "Band plans directory not found";
        return plans;
    }
    
    // Map of known band plans with friendly names
    std::map<std::string, std::string> plan_names = {
        {"usa", "USA (FCC)"},
        {"deu", "Germany (DARC/IARU Region 1)"}
    };
    
    for (const auto& entry : std::filesystem::directory_iterator(bandplans_dir, ec)) {
        if (ec) {
            error = "Error iterating band plans directory: " + ec.message();
            break;
        }
        
        if (entry.is_regular_file(ec)) {
            std::string filename = entry.path().filename().string();
            std::string stem = entry.path().stem().string();
            
            if (entry.path().extension() == ".ini") {
                std::string friendly_name = stem;
                auto it = plan_names.find(stem);
                if (it != plan_names.end()) {
                    friendly_name = it->second;
                }
                plans.push_back({stem, friendly_name});
            }
        }
    }
    
    return plans;
}

// Standard amateur radio bands (HF and VHF/UHF) - fallback default (German)
inline std::vector<AmateurBand> getDefaultAmateurBands() {
    return {
        // HF Bands
        {"160m", 1810000, 2000000},
        {"80m", 3500000, 3800000},
        {"60m", 5351500, 5366500},
        {"40m", 7000000, 7200000},
        {"30m", 10100000, 10150000},
        {"20m", 14000000, 14350000},
        {"17m", 18068000, 18168000},
        {"15m", 21000000, 21450000},
        {"12m", 24890000, 24990000},
        {"10m", 28000000, 29700000},
        
        // VHF/UHF Bands
        {"6m", 50000000, 52000000},
        {"4m", 70000000, 70500000},
        {"2m", 144000000, 146000000},
        {"70cm", 430000000, 440000000},
        {"23cm", 1240000000, 1300000000},
    };
}

// Load band plan or return default
inline std::vector<AmateurBand> getAmateurBands(const std::string& bandplan = "deu") {
    std::vector<AmateurBand> bands;
    std::string error;
    
    std::string filename = "bandplans/" + bandplan + ".ini";
    if (loadBandPlan(filename, bands, error)) {
        return bands;
    }
    
    // Fallback to default if loading fails
    return getDefaultAmateurBands();
}

// Find band by frequency
inline const AmateurBand* findBandForFrequency(uint64_t freq_hz, const std::vector<AmateurBand>& bands) {
    for (const auto& band : bands) {
        if (freq_hz >= band.start_hz && freq_hz <= band.end_hz) {
            return &band;
        }
    }
    return nullptr;
}

// Get band with margin
inline void getBandWithMargin(const AmateurBand& band, double margin_percent, uint64_t& start_out, uint64_t& end_out) {
    uint64_t margin = static_cast<uint64_t>(band.bandwidth() * margin_percent / 100.0);
    start_out = (band.start_hz > margin) ? (band.start_hz - margin) : 0;
    end_out = band.end_hz + margin;
}
