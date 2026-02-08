#pragma once
#include "measurement.h"
#include <string>
#include <vector>

class ImportModule {
public:
    // List available export files in the Export directory
    static std::vector<std::string> listExportFiles(std::string& err);
    
    // Import measurements from a file
    static bool importFile(const std::string& filename, std::vector<MeasurementPoint>& pts, std::string& err);
};
