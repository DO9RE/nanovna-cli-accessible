#include "import.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cmath> // Für mathematische Funktionen wie sqrt und atan2

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

std::vector<std::string> ImportModule::listExportFiles(std::string& err) {
    std::vector<std::string> files;
    std::string exportDir = "Export";
    
    try {
        if (!std::filesystem::exists(exportDir)) {
            err = "Export directory does not exist";
            return files;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(exportDir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                // Accept both .csv and .txt files
                if (ext == ".csv" || ext == ".txt") {
                    files.push_back(entry.path().filename().string());
                }
            }
        }
        
        // Sort files by name (most recent first if using timestamp naming)
        std::sort(files.rbegin(), files.rend());
        
    } catch (const std::exception& e) {
        err = std::string("Error listing files: ") + e.what();
    }
    
    return files;
}

bool ImportModule::importFile(const std::string& filename, std::vector<MeasurementPoint>& pts, std::string& err) {
    pts.clear();
    
    std::string filepath = "Export/" + filename;
    std::ifstream ifs(filepath);
    
    if (!ifs) {
        err = "Cannot open file: " + filepath;
        return false;
    }
    
    std::string line;
    bool isCSV = (filename.find(".csv") != std::string::npos);
    
    // Skip header line for CSV
    if (isCSV) {
        std::getline(ifs, line);
    }
    
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        
        MeasurementPoint pt;
        
        if (isCSV) {
            // Parse CSV: freq_hz,s11_re,s11_im,swr,return_loss_db,r_ohm,x_ohm,s21_re,s21_im
            std::istringstream ss(line);
            std::string token;
            int field = 0;
            
            while (std::getline(ss, token, ',')) {
                try {
                    switch(field) {
                        case 0: pt.freq = std::stoull(token); break;
                        case 1: pt.s11_re = std::stod(token); break;
                        case 2: pt.s11_im = std::stod(token); break;
                        case 3: pt.swr = std::stod(token); break;
                        case 4: pt.rl = std::stod(token); break;
                        case 5: pt.R = std::stod(token); break;
                        case 6: pt.X = std::stod(token); break;
                        case 7: 
                            if (!token.empty()) {
                                pt.s21_re = std::stod(token);
                                pt.hasS21 = true;
                            }
                            break;
                        case 8:
                            if (!token.empty()) {
                                pt.s21_im = std::stod(token);
                            }
                            break;
                    }
                } catch (...) {
                    // Skip invalid fields
                }
                field++;
            }
            
            // Calculate derived values
            pt.impedance_mag = std::sqrt(pt.R * pt.R + pt.X * pt.X);
            pt.phase_deg = std::atan2(pt.X, pt.R) * 180.0 / M_PI;
            
            if (field >= 7) {  // At least freq through X
                pts.push_back(pt);
            }
            
        } else {
            // Parse TXT: "Freq: 144000000 Hz  SWR: 1.234  RL: 12.34  R: 50.0  X: 10.0"
            size_t freqPos = line.find("Freq: ");
            size_t swrPos = line.find("SWR: ");
            size_t rlPos = line.find("RL: ");
            size_t rPos = line.find("R: ");
            size_t xPos = line.find("X: ");
            
            if (freqPos != std::string::npos && swrPos != std::string::npos) {
                try {
                    pt.freq = std::stoull(line.substr(freqPos + 6));
                    pt.swr = std::stod(line.substr(swrPos + 5));
                    if (rlPos != std::string::npos) pt.rl = std::stod(line.substr(rlPos + 4));
                    if (rPos != std::string::npos) pt.R = std::stod(line.substr(rPos + 3));
                    if (xPos != std::string::npos) pt.X = std::stod(line.substr(xPos + 3));
                    
                    // Calculate derived values
                    pt.impedance_mag = std::sqrt(pt.R * pt.R + pt.X * pt.X);
                    pt.phase_deg = std::atan2(pt.X, pt.R) * 180.0 / M_PI;
                    
                    pts.push_back(pt);
                } catch (...) {
                    // Skip invalid lines
                }
            }
        }
    }
    
    if (pts.empty()) {
        err = "No valid measurement points found in file";
        return false;
    }
    
    return true;
}
