#include "comm_serial.h"
#include "logger.h"
#include <sstream>
#include <chrono>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <SetupAPI.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")
#else
// POSIX headers for macOS and Linux
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <dirent.h>
#include <cstring>
#include <cerrno>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/usb/IOUSBLib.h>

// Handle deprecated kIOMasterPortDefault (macOS 12.0+)
// In macOS 12.0+, kIOMainPortDefault is the new constant and kIOMasterPortDefault is deprecated
// For older macOS versions (< 12.0), kIOMainPortDefault doesn't exist, so we need to define it
// We check SDK version to avoid redefining the constant if it already exists
#if defined(__APPLE__)
  #include <Availability.h>
  // kIOMainPortDefault was introduced in macOS 12.0 SDK
  // If we're building with an older SDK, define it as the old constant
  #if !defined(MAC_OS_VERSION_12_0) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_VERSION_12_0
    #ifndef kIOMainPortDefault
      #define kIOMainPortDefault kIOMasterPortDefault
    #endif
  #endif
#endif

#endif
#endif

SerialComm::SerialComm(): 
#if defined(_WIN32)
    handle(nullptr), 
#endif
    logger(nullptr) {}

SerialComm::~SerialComm(){ closePort(); }

void SerialComm::setLogger(Logger* lg) { logger = lg; }

#if defined(_WIN32)
// Windows implementation
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
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

#else
// POSIX implementation for macOS and Linux
bool SerialComm::openPort(const std::string& portName, unsigned int baudRate, std::string& err) {
    if (logger) logger->log("SERIAL", "Opening port " + portName + " @ " + std::to_string(baudRate));
    
    // Open the serial port
    int fd = open(portName.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        err = "Failed to open port: " + std::string(strerror(errno));
        if (logger) logger->log("SERIAL", "open() failed: " + err);
        return false;
    }
    
    // Configure port for blocking reads with timeouts
    // O_NDELAY is used during open() to prevent blocking if no carrier signal
    // We then switch to blocking mode to allow proper timeout handling via VTIME/VMIN
    fcntl(fd, F_SETFL, 0);
    
    struct termios options;
    if (tcgetattr(fd, &options) != 0) {
        err = "Failed to get port attributes: " + std::string(strerror(errno));
        close(fd);
        if (logger) logger->log("SERIAL", "tcgetattr() failed: " + err);
        return false;
    }
    
    // Set baud rate
    speed_t speed;
    switch (baudRate) {
        case 9600:   speed = B9600; break;
        case 19200:  speed = B19200; break;
        case 38400:  speed = B38400; break;
        case 57600:  speed = B57600; break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        #ifdef B460800
        case 460800: speed = B460800; break;
        #endif
        #ifdef B921600
        case 921600: speed = B921600; break;
        #endif
        default:
            err = "Unsupported baud rate: " + std::to_string(baudRate);
            close(fd);
            if (logger) logger->log("SERIAL", err);
            return false;
    }
    
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    
    // Configure 8N1 (8 data bits, no parity, 1 stop bit)
    options.c_cflag &= ~PARENB;  // No parity
    options.c_cflag &= ~CSTOPB;  // 1 stop bit
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;      // 8 data bits
    options.c_cflag |= CREAD | CLOCAL;  // Enable receiver, ignore modem control lines
    
    // Disable hardware flow control
    options.c_cflag &= ~CRTSCTS;
    
    // Raw input mode (non-canonical)
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    
    // Disable software flow control
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    
    // Raw output mode
    options.c_oflag &= ~OPOST;
    
    // Set read timeouts (deciseconds)
    // VMIN=0, VTIME=2: Inter-character timer. Wait up to 0.2 seconds for each character.
    // If no character arrives within the timeout, read() returns with what it has.
    options.c_cc[VMIN] = 0;   // Return immediately if no data available
    options.c_cc[VTIME] = 2;  // 0.2 second inter-character timeout
    
    // Apply settings
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        err = "Failed to set port attributes: " + std::string(strerror(errno));
        close(fd);
        if (logger) logger->log("SERIAL", "tcsetattr() failed: " + err);
        return false;
    }
    
    // Flush any existing data
    tcflush(fd, TCIOFLUSH);
    
    handle = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
    if (logger) logger->log("SERIAL", "Port opened successfully");
    return true;
}

void SerialComm::closePort() {
    if (handle) {
        if (logger) logger->log("SERIAL", "Closing port");
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(handle));
        close(fd);
        handle = nullptr;
    }
}

bool SerialComm::writeData(const std::string& data, std::string& err) {
    if (!handle) {
        err = "Port not open";
        if (logger) logger->log("SERIAL", "Write failed: Port not open");
        return false;
    }
    
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(handle));
    
    // Add CR terminator if not present (as pynanovna / device expects)
    std::string out = data;
    if (!out.empty() && out.back() != '\r') out.push_back('\r');
    
    // Flush input buffer before writing to avoid reading stale echoes or old responses
    // This is necessary because the device may echo commands or have buffered data
    // Note: Only flushing input (TCIFLUSH), output buffer is preserved
    tcflush(fd, TCIFLUSH);
    if (logger) logger->log("SERIAL", "Flushed input queue before write");
    
    if (logger) logger->logComm("SERIAL", "TX", out);
    
    ssize_t written = write(fd, out.c_str(), out.size());
    if (written < 0) {
        err = "Write failed: " + std::string(strerror(errno));
        if (logger) logger->log("SERIAL", err);
        return false;
    }
    if (static_cast<size_t>(written) != out.size()) {
        err = "Incomplete write: " + std::to_string(written) + "/" + std::to_string(out.size());
        if (logger) logger->log("SERIAL", err);
        return false;
    }
    
    // Wait for data to be transmitted
    tcdrain(fd);
    
    if (logger) logger->log("SERIAL", "Wrote bytes: " + std::to_string(written));
    return true;
}

bool SerialComm::readLine(std::string& outLine, int timeoutMs, std::string& err) {
    if (!handle) {
        err = "Port not open";
        if (logger) logger->log("SERIAL", "Read failed: Port not open");
        return false;
    }
    
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(handle));
    outLine.clear();
    
    auto start = std::chrono::steady_clock::now();
    
    while (true) {
        // Check timeout
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed > timeoutMs) {
            // Timeout occurred
            if (!outLine.empty()) {
                // If we have some partial data, return it as a valid line (device may not send LF)
                // Log partial as a normal RX line (not an error)
                if (logger) {
                    logger->log("SERIAL", "Read line (partial, no LF) bytes: " + std::to_string(outLine.size()));
                    logger->logComm("SERIAL", "RX", outLine);
                }
                err.clear();
                return true;
            } else {
                err = "Timeout: no data received";
                if (logger) logger->log("SERIAL", err);
            }
            return false;
        }
        
        // Use select to check if data is available
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10000;  // 10ms
        
        int result = select(fd + 1, &readfds, nullptr, nullptr, &tv);
        if (result < 0) {
            err = "Select failed: " + std::string(strerror(errno));
            if (logger) logger->log("SERIAL", err);
            return false;
        }
        
        if (result == 0) {
            // No data available, continue loop
            continue;
        }
        
        // Read one byte
        char ch;
        ssize_t n = read(fd, &ch, 1);
        if (n < 0) {
            err = "Read failed: " + std::string(strerror(errno));
            if (logger) logger->log("SERIAL", err);
            return false;
        }
        
        if (n == 1) {
            if (ch == '\r') continue;  // Skip CR
            if (ch == '\n') break;     // End of line
            outLine.push_back(ch);
        }
    }
    
    if (logger) {
        logger->log("SERIAL", "Read line bytes: " + std::to_string(outLine.size()));
        logger->logComm("SERIAL", "RX", outLine);
    }
    return true;
}

bool SerialComm::isOpen() const {
    return handle != nullptr;
}

#ifdef __APPLE__
// macOS-specific port enumeration using IOKit
std::vector<std::string> SerialComm::listAvailablePorts() {
    std::vector<std::string> result;
    
    // Get matching services for serial ports
    CFMutableDictionaryRef classesToMatch = IOServiceMatching(kIOSerialBSDServiceValue);
    if (classesToMatch == nullptr) {
        return result;
    }
    
    CFDictionarySetValue(classesToMatch, CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDAllTypes));
    
    io_iterator_t matchingServices;
    kern_return_t kernResult = IOServiceGetMatchingServices(kIOMainPortDefault, classesToMatch, &matchingServices);
    if (kernResult != KERN_SUCCESS) {
        return result;
    }
    
    io_object_t service;
    while ((service = IOIteratorNext(matchingServices))) {
        CFTypeRef devicePathAsCFString = IORegistryEntryCreateCFProperty(
            service, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
        
        if (devicePathAsCFString) {
            char devicePath[256];
            if (CFStringGetCString((CFStringRef)devicePathAsCFString, devicePath, 
                                  sizeof(devicePath), kCFStringEncodingUTF8)) {
                result.push_back(devicePath);
            }
            CFRelease(devicePathAsCFString);
        }
        
        IOObjectRelease(service);
    }
    
    IOObjectRelease(matchingServices);
    return result;
}

std::vector<SerialComm::PortInfo> SerialComm::listAvailablePortsWithNames() {
    std::vector<PortInfo> result;
    
    CFMutableDictionaryRef classesToMatch = IOServiceMatching(kIOSerialBSDServiceValue);
    if (classesToMatch == nullptr) {
        return result;
    }
    
    CFDictionarySetValue(classesToMatch, CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDAllTypes));
    
    io_iterator_t matchingServices;
    kern_return_t kernResult = IOServiceGetMatchingServices(kIOMainPortDefault, classesToMatch, &matchingServices);
    if (kernResult != KERN_SUCCESS) {
        return result;
    }
    
    io_object_t service;
    while ((service = IOIteratorNext(matchingServices))) {
        char devicePath[256] = {0};
        char deviceName[256] = {0};
        
        // Get device path (e.g., /dev/cu.usbserial-*)
        CFTypeRef devicePathAsCFString = IORegistryEntryCreateCFProperty(
            service, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
        
        if (devicePathAsCFString) {
            CFStringGetCString((CFStringRef)devicePathAsCFString, devicePath, 
                              sizeof(devicePath), kCFStringEncodingUTF8);
            CFRelease(devicePathAsCFString);
        }
        
        // Get USB product name if available
        io_registry_entry_t parent = 0;
        kern_return_t kr = IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent);
        if (kr == KERN_SUCCESS && parent) {
            CFTypeRef productNameRef = IORegistryEntrySearchCFProperty(
                parent, kIOServicePlane, CFSTR("USB Product Name"),
                kCFAllocatorDefault, kIORegistryIterateRecursively | kIORegistryIterateParents);
            
            if (productNameRef) {
                CFStringGetCString((CFStringRef)productNameRef, deviceName,
                                  sizeof(deviceName), kCFStringEncodingUTF8);
                CFRelease(productNameRef);
            }
            IOObjectRelease(parent);
        }
        
        // If no USB product name, try IOTTYBaseName
        if (strlen(deviceName) == 0) {
            CFTypeRef baseNameRef = IORegistryEntryCreateCFProperty(
                service, CFSTR(kIOTTYBaseNameKey), kCFAllocatorDefault, 0);
            if (baseNameRef) {
                CFStringGetCString((CFStringRef)baseNameRef, deviceName,
                                  sizeof(deviceName), kCFStringEncodingUTF8);
                CFRelease(baseNameRef);
            }
        }
        
        if (strlen(devicePath) > 0) {
            result.push_back({
                devicePath,
                strlen(deviceName) > 0 ? deviceName : "Serial Device"
            });
        }
        
        IOObjectRelease(service);
    }
    
    IOObjectRelease(matchingServices);
    return result;
}

#else
// Linux-specific port enumeration using sysfs
std::vector<std::string> SerialComm::listAvailablePorts() {
    std::vector<std::string> result;
    
    // Check /dev for ttyUSB* and ttyACM* devices
    DIR* dir = opendir("/dev");
    if (!dir) return result;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find("ttyUSB") == 0 || name.find("ttyACM") == 0 || name.find("ttyS") == 0) {
            std::string fullPath = "/dev/" + name;
            // Try to open to verify it's accessible
            int fd = open(fullPath.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
            if (fd != -1) {
                close(fd);
                result.push_back(fullPath);
            }
        }
    }
    closedir(dir);
    
    return result;
}

std::vector<SerialComm::PortInfo> SerialComm::listAvailablePortsWithNames() {
    std::vector<PortInfo> result;
    
    DIR* dir = opendir("/dev");
    if (!dir) return result;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find("ttyUSB") == 0 || name.find("ttyACM") == 0 || name.find("ttyS") == 0) {
            std::string fullPath = "/dev/" + name;
            
            // Try to open to verify it's accessible
            int fd = open(fullPath.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
            if (fd != -1) {
                close(fd);
                
                // Try to get product information from sysfs
                std::string deviceName = "Serial Device";
                std::string sysfsPath = "/sys/class/tty/" + name + "/device/../product";
                
                FILE* f = fopen(sysfsPath.c_str(), "r");
                if (f) {
                    char buffer[256];
                    if (fgets(buffer, sizeof(buffer), f)) {
                        deviceName = buffer;
                        // Remove trailing newline
                        if (!deviceName.empty() && deviceName.back() == '\n') {
                            deviceName.pop_back();
                        }
                    }
                    fclose(f);
                }
                
                result.push_back({fullPath, deviceName});
            }
        }
    }
    closedir(dir);
    
    return result;
}
#endif

#endif
