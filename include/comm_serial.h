#pragma once
#include <string>
#include <vector>

class Logger; // forward

class SerialComm {
public:
    SerialComm();
    ~SerialComm();

    bool openPort(const std::string& portName, unsigned int baudRate, std::string& err);
    void closePort();
    bool writeData(const std::string& data, std::string& err); // now sends CR-terminated command
    bool readLine(std::string& outLine, int timeoutMs, std::string& err);
    bool isOpen() const;

    // List COM ports (tries COM1..COM64) with device descriptions
    struct PortInfo {
        std::string portName;
        std::string deviceName;
    };
    static std::vector<PortInfo> listAvailablePortsWithNames();
    
    // Legacy function for backward compatibility
    static std::vector<std::string> listAvailablePorts();

    // Debug logger
    void setLogger(Logger* logger);

private:
    void* handle;
    Logger* logger;
};