#ifndef PLATFORM_UTILS_H
#define PLATFORM_UTILS_H

/**
 * @file platform_utils.h
 * @brief Cross-platform utility functions
 * 
 * This file provides platform-independent utility functions and wrappers
 * for common operations that have platform-specific implementations.
 * 
 * Consolidates redundant implementations from:
 * - Timestamp generation (logger.cpp, math_logger.cpp, export.cpp, main.cpp)
 * - String trimming (protocol.cpp, settings.cpp, translation.cpp)
 * - Numeric parsing (measurement.cpp, settings.cpp, import.cpp)
 * - Socket operations (web_server.cpp)
 */

#include <chrono>
#include <thread>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cctype>
#include <stdexcept>

// Platform detection macros
// Note: These may already be defined by CMakeLists.txt via add_definitions()
// Use #ifndef to avoid redefinition warnings
#if defined(_WIN32) || defined(_WIN64)
    #ifndef PLATFORM_WINDOWS
        #define PLATFORM_WINDOWS
    #endif
#elif defined(__APPLE__) && defined(__MACH__)
    #ifndef PLATFORM_MACOS
        #define PLATFORM_MACOS
    #endif
#elif defined(__linux__)
    #ifndef PLATFORM_LINUX
        #define PLATFORM_LINUX
    #endif
#endif

/**
 * Cross-platform sleep function
 * @param milliseconds Time to sleep in milliseconds
 */
inline void platformSleep(unsigned int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// ----------------------------------------------------------------------------
// 1.10 Timestamp Generation
// ----------------------------------------------------------------------------

/**
 * Get current timestamp as formatted string
 * 
 * @param format Format string for strftime (default: "%Y-%m-%d %H:%M:%S")
 * @return Formatted timestamp string
 * 
 * Replaces:
 * - Logger::timestr() in logger.cpp
 * - MathLogger::timestr() in math_logger.cpp
 * - Anonymous function in export.cpp generateFilename()
 * - Inline timestamp generation in main.cpp
 */
inline std::string currentTimestamp(const char* format = "%Y-%m-%d %H:%M:%S") {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, format);
    return ss.str();
}

// ----------------------------------------------------------------------------
// 1.11 String Trimming
// ----------------------------------------------------------------------------

/**
 * Trim whitespace and newlines from string (in-place)
 * 
 * Removes leading and trailing whitespace, \r, and \n characters.
 * 
 * @param s String to trim (modified in-place)
 * 
 * Replaces:
 * - str_trim() in protocol.cpp (removes \r, \n and whitespace)
 * - trim() in settings.cpp (removes whitespace only)
 * - trim() in translation.cpp (removes whitespace only)
 */
inline void trimString(std::string& s) {
    // Remove trailing \r and \n first (as in protocol.cpp)
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
    // Remove leading whitespace
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    // Remove trailing whitespace
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
}

// ----------------------------------------------------------------------------
// 1.13 Numeric Parsing Helpers
// ----------------------------------------------------------------------------

/**
 * Safely parse a double from string
 * 
 * @param s String to parse
 * @param outValue Output value (only modified on success)
 * @return true if parsing succeeded, false otherwise
 * 
 * Replaces:
 * - try_parse_double() in measurement.cpp
 * - Inline try-catch blocks with std::stod() in import.cpp
 */
inline bool tryParseDouble(const std::string& s, double& outValue) {
    try {
        size_t pos = 0;
        double val = std::stod(s, &pos);
        // Ensure entire string was consumed (no trailing garbage)
        if (pos == s.length()) {
            outValue = val;
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

/**
 * Safely parse a uint64_t from string
 * 
 * @param s String to parse
 * @param outValue Output value (only modified on success)
 * @return true if parsing succeeded, false otherwise
 * 
 * Replaces:
 * - try_parse_u64() in measurement.cpp
 * - parseUInt64() in settings.cpp
 * - Inline try-catch blocks with std::stoull() in import.cpp
 */
inline bool tryParseUInt64(const std::string& s, uint64_t& outValue) {
    try {
        size_t pos = 0;
        unsigned long long val = std::stoull(s, &pos);
        // Ensure entire string was consumed (no trailing garbage)
        if (pos == s.length()) {
            outValue = static_cast<uint64_t>(val);
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

// ----------------------------------------------------------------------------
// 1.20 Socket Abstraction (Platform-specific implementations)
// ----------------------------------------------------------------------------

#ifdef PLATFORM_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
    
    /**
     * Close a socket
     * @param socketFd Socket descriptor to close
     */
    inline void closeSocket(socket_t socketFd) {
        if (socketFd != INVALID_SOCKET_VALUE) {
            closesocket(socketFd);
        }
    }
    
    /**
     * Get last socket error as string
     * @return Error message string
     */
    inline std::string getSocketError() {
        int err = WSAGetLastError();
        std::ostringstream oss;
        oss << "Socket error " << err;
        return oss.str();
    }
    
    /**
     * Initialize networking subsystem
     * @return true if successful, false otherwise
     */
    inline bool initializeNetworking() {
        WSADATA wsaData;
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }
    
    /**
     * Cleanup networking subsystem
     */
    inline void cleanupNetworking() {
        WSACleanup();
    }
    
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <errno.h>
    #include <cstring>
    
    using socket_t = int;
    constexpr socket_t INVALID_SOCKET_VALUE = -1;
    
    /**
     * Close a socket
     * @param socketFd Socket descriptor to close
     */
    inline void closeSocket(socket_t socketFd) {
        if (socketFd != INVALID_SOCKET_VALUE) {
            close(socketFd);
        }
    }
    
    /**
     * Get last socket error as string
     * @return Error message string
     */
    inline std::string getSocketError() {
        return std::string(strerror(errno));
    }
    
    /**
     * Initialize networking subsystem (no-op on POSIX)
     * @return true (always successful on POSIX)
     */
    inline bool initializeNetworking() {
        return true;
    }
    
    /**
     * Cleanup networking subsystem (no-op on POSIX)
     */
    inline void cleanupNetworking() {
        // No cleanup needed on POSIX
    }
    
#endif

#endif // PLATFORM_UTILS_H
