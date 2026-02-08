#pragma once
#include <string>
#include <fstream>
#include <mutex>

class Logger {
public:
    Logger() = default;
    bool open(const std::string& filename);
    void openCommLog(const std::string& filename);
    void close();
    void closeCommLog();
    void log(const std::string& module, const std::string& message);
    void logComm(const std::string& module, const std::string& direction, const std::string& data);
private:
    std::ofstream ofs;
    std::ofstream ofsComm;
    std::mutex mtx;
    std::string timestr();
};