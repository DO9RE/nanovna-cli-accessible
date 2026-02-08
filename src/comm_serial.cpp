#include "comm_serial.h"
#include "logger.h"
#include <windows.h>
#include <sstream>
#include <SetupAPI.h>
#include <devguid.h>

#pragma comment(lib, "setupapi.lib")

SerialComm::SerialComm(): handle(nullptr), logger(nullptr) {}
SerialComm::~SerialComm(){ closePort(); }

void SerialComm::setLogger(Logger* lg) { logger = lg; }

bool SerialComm::openPort(const std::string& portName, unsigned int baudRate, std::string& err) {
    std::string n = portName;
    std::string winName = n;
    if (n.size() > 3) {
        if (n.substr(0,3) == "COM" && n.size() > 4) {
            winName = std::string("\\\\.\\") + n;
        }
    }
    if (logger) logger->log("SERIAL", "Opening port " + winName + " @ " + std::to_string(baudRate));
    HANDLE h = CreateFileA(winName.c_str(), GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        err = "CreateFile failed, check port and permissions";
        if (logger) logger->log("SERIAL", std::string("CreateFile failed: ") + err);
        return false;
    }
    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(h, &dcb)) {
        err = "GetCommState failed";
        CloseHandle(h);
        if (logger) logger->log("SERIAL", std::string("GetCommState failed"));
        return false;
    }
    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(h, &dcb)) {
        err = "SetCommState failed";
        CloseHandle(h);
        if (logger) logger->log("SERIAL", std::string("SetCommState failed"));
        return false;
    }
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 200;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 500;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(h, &timeouts);
    handle = h;
    if (logger) logger->log("SERIAL", "Port opened successfully");
    return true;
}

void SerialComm::closePort() {
    if (handle) {
        if (logger) logger->log("SERIAL", "Closing port");
        CloseHandle((HANDLE)handle);
        handle = nullptr;
    }
}

static void purge_input(HANDLE h) {
    // Purge input and output queues to remove old data
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

bool SerialComm::writeData(const std::string& data, std::string& err) {
    if (!handle) { err = "Port not open"; if (logger) logger->log("SERIAL", "Write failed: Port not open"); return false; }
    DWORD written = 0;
    // Use CR as terminator (as pynanovna / device expects)
    std::string out = data;
    if (!out.empty() && out.back() != '\r') out.push_back('\r');

    // Drain / purge incoming buffer before writing to avoid reading old echoes
    purge_input((HANDLE)handle);
    if (logger) logger->log("SERIAL", "Drained input queue before write");

    if (logger) logger->logComm("SERIAL", "TX", out);
    if (!WriteFile((HANDLE)handle, out.c_str(), (DWORD)out.size(), &written, NULL)) {
        err = "WriteFile failed";
        if (logger) logger->log("SERIAL", std::string("WriteFile failed: ") + err);
        return false;
    }
    if (logger) logger->log("SERIAL", "Wrote bytes: " + std::to_string(written));
    return true;
}

bool SerialComm::readLine(std::string& outLine, int timeoutMs, std::string& err) {
    if (!handle) { err = "Port not open"; if (logger) logger->log("SERIAL", "Read failed: Port not open"); return false; }
    CHAR ch;
    DWORD read = 0;
    outLine.clear();
    DWORD start = GetTickCount();
    while (true) {
        if (!ReadFile((HANDLE)handle, &ch, 1, &read, NULL)) {
            err = "ReadFile failed";
            if (logger) logger->log("SERIAL", std::string("ReadFile failed: ") + err);
            return false;
        }
        if (read == 1) {
            if (ch == '\r') continue;
            if (ch == '\n') break;
            outLine.push_back(ch);
        } else {
            // no byte read this iteration
            if ((int)(GetTickCount() - start) > timeoutMs) {
                // If we have some partial data, return it as a valid line (device may not send LF)
                if (!outLine.empty()) {
                    // Log partial as a normal RX line (not an error)
                    if (logger) {
                        logger->log("SERIAL", "Read line (partial, no LF) bytes: " + std::to_string(outLine.size()));
                        logger->logComm("SERIAL", "RX", outLine);
                    }
                    err.clear();
                    return true;
                }
                err = "Timeout";
                if (logger) {
                    logger->log("SERIAL", "Read timeout, bytes read: " + std::to_string(outLine.size()));
                    logger->logComm("SERIAL", "RX-partial", outLine);
                }
                return false;
            }
            Sleep(1);
        }
    }
    if (logger) {
        logger->log("SERIAL", "Read line bytes: " + std::to_string(outLine.size()));
        logger->logComm("SERIAL", "RX", outLine);
    }
    return true;
}

bool SerialComm::isOpen() const { return handle != nullptr; }

std::vector<std::string> SerialComm::listAvailablePorts() {
    std::vector<std::string> res;
    for (int i = 1; i <= 64; ++i) {
        std::ostringstream ss;
        ss << "COM" << i;
        std::string name = ss.str();
        std::string winName = name;
        if (name.size() > 4) winName = std::string("\\\\.\\") + name;
        HANDLE h = CreateFileA(winName.c_str(), GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            res.push_back(name);
            CloseHandle(h);
        }
    }
    return res;
}

std::vector<SerialComm::PortInfo> SerialComm::listAvailablePortsWithNames() {
    std::vector<PortInfo> result;
    
    // Get device information set for all COM ports
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        // Fallback to basic listing
        auto ports = listAvailablePorts();
        for (const auto& port : ports) {
            result.push_back({port, "Unknown Device"});
        }
        return result;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        char friendlyName[256] = {0};
        char portName[256] = {0};
        
        // Get friendly name
        if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME,
                                              NULL, (PBYTE)friendlyName, sizeof(friendlyName), NULL)) {
            // Extract COM port name from friendly name (usually in format "Device Name (COMx)")
            std::string friendly(friendlyName);
            size_t comPos = friendly.find("(COM");
            if (comPos != std::string::npos) {
                size_t endPos = friendly.find(")", comPos);
                if (endPos != std::string::npos) {
                    std::string port = friendly.substr(comPos + 1, endPos - comPos - 1);
                    std::string device = friendly.substr(0, comPos);
                    // Trim trailing space
                    if (!device.empty() && device.back() == ' ') {
                        device.pop_back();
                    }
                    
                    // Verify port is accessible
                    std::string winName = port;
                    if (port.size() > 4) winName = std::string("\\\\.\\") + port;
                    HANDLE h = CreateFileA(winName.c_str(), GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
                    if (h != INVALID_HANDLE_VALUE) {
                        CloseHandle(h);
                        result.push_back({port, device.empty() ? friendly : device});
                    }
                }
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    
    // If no ports found via SetupAPI, fall back to basic scan
    if (result.empty()) {
        auto ports = listAvailablePorts();
        for (const auto& port : ports) {
            result.push_back({port, "Serial Device"});
        }
    }
    
    return result;
}