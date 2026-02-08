#include "logger.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cctype>

bool Logger::open(const std::string& filename) {
    std::lock_guard<std::mutex> l(mtx);
    ofs.open(filename, std::ios::out | std::ios::app);
    return ofs.is_open();
}

void Logger::openCommLog(const std::string& filename) {
    std::lock_guard<std::mutex> l(mtx);
    if (ofsComm.is_open()) ofsComm.close();
    ofsComm.open(filename, std::ios::out | std::ios::app);
}

void Logger::closeCommLog() {
    std::lock_guard<std::mutex> l(mtx);
    if (ofsComm.is_open()) ofsComm.close();
}

void Logger::close() {
    std::lock_guard<std::mutex> l(mtx);
    if (ofs.is_open()) ofs.close();
    if (ofsComm.is_open()) ofsComm.close();
}

std::string Logger::timestr() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void Logger::log(const std::string& module, const std::string& message) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    ofs << timestr() << " [" << module << "] " << message << std::endl;
    ofs.flush();
}

static std::string to_hex(const std::string& data) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char c : data) {
        ss << std::setw(2) << (int)c << " ";
    }
    return ss.str();
}

static std::string to_printable(const std::string& data) {
    std::string out;
    out.reserve(data.size());
    for (unsigned char c : data) {
        if (std::isprint(c)) out.push_back(c);
        else out.push_back('.');
    }
    return out;
}

void Logger::logComm(const std::string& module, const std::string& direction, const std::string& data) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofsComm.is_open()) return;
    ofsComm << timestr() << " [" << module << "] " << direction << " bytes=" << data.size() << "\n";
    ofsComm << "  ASCII: " << to_printable(data) << "\n";
    ofsComm << "  HEX:   " << to_hex(data) << "\n";
    ofsComm.flush();
}