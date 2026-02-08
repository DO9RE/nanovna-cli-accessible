#include "math_logger.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

bool MathLogger::open(const std::string& filename) {
    std::lock_guard<std::mutex> l(mtx);
    ofs.open(filename, std::ios::out | std::ios::app);
    if (ofs.is_open()) {
        ofs << "\n========================================\n";
        ofs << "Math Debug Log Started: " << timestr() << "\n";
        ofs << "========================================\n\n";
        ofs.flush();
    }
    return ofs.is_open();
}

void MathLogger::close() {
    std::lock_guard<std::mutex> l(mtx);
    if (ofs.is_open()) {
        ofs << "\n========================================\n";
        ofs << "Math Debug Log Closed: " << timestr() << "\n";
        ofs << "========================================\n";
        ofs.close();
    }
}

std::string MathLogger::timestr() {
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

void MathLogger::logRawMeasurement(size_t index, uint64_t frequency, 
                                  double s11_re, double s11_im, 
                                  const std::string& context) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    
    ofs << timestr() << " [RAW_MEASUREMENT]";
    if (!context.empty()) ofs << " [" << context << "]";
    ofs << "\n";
    ofs << "  Point Index: " << index << "\n";
    ofs << "  Frequency: " << frequency << " Hz\n";
    ofs << "  S11 (complex): " << s11_re << " + j" << s11_im << "\n";
    ofs.flush();
}

void MathLogger::logGammaCalculation(double s11_re, double s11_im, 
                                    double gamma_mag, double gamma_phase_deg,
                                    const std::string& method) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    
    ofs << timestr() << " [GAMMA_CALC]";
    if (!method.empty()) ofs << " [Method: " << method << "]";
    ofs << "\n";
    ofs << "  Input S11: " << s11_re << " + j" << s11_im << "\n";
    ofs << "  => Gamma magnitude: " << gamma_mag << "\n";
    ofs << "  => Gamma phase: " << gamma_phase_deg << " degrees\n";
    ofs << "  Explanation: S11 directly represents reflection coefficient (Gamma)\n";
    ofs << "               |Gamma| = sqrt(re^2 + im^2)\n";
    ofs << "               phase = atan2(im, re) * 180/pi\n";
    ofs.flush();
}

void MathLogger::logImpedanceCalculation(double gamma_re, double gamma_im,
                                        double z0, double R, double X,
                                        const std::string& context) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    
    ofs << timestr() << " [IMPEDANCE_CALC]";
    if (!context.empty()) ofs << " [" << context << "]";
    ofs << "\n";
    ofs << "  Input Gamma: " << gamma_re << " + j" << gamma_im << "\n";
    ofs << "  Reference Z0: " << z0 << " Ohm\n";
    ofs << "  => Impedance R (Resistance): " << R << " Ohm\n";
    ofs << "  => Impedance X (Reactance): " << X << " Ohm\n";
    ofs << "  => Impedance Z: " << R << " + j" << X << " Ohm\n";
    ofs << "  Explanation: Z = Z0 * (1 + Gamma) / (1 - Gamma)\n";
    ofs << "               Where Gamma is the reflection coefficient\n";
    ofs.flush();
}

void MathLogger::logSWRCalculation(double gamma_mag, double swr,
                                  const std::string& context) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    
    ofs << timestr() << " [SWR_CALC]";
    if (!context.empty()) ofs << " [" << context << "]";
    ofs << "\n";
    ofs << "  Input |Gamma|: " << gamma_mag << "\n";
    ofs << "  => SWR: " << swr << "\n";
    ofs << "  Explanation: SWR = (1 + |Gamma|) / (1 - |Gamma|)\n";
    ofs << "               Perfect match (|Gamma|=0) => SWR=1.0\n";
    ofs << "               Poor match (|Gamma|->1) => SWR->infinity\n";
    ofs.flush();
}

void MathLogger::logReturnLossCalculation(double gamma_mag, double return_loss_db,
                                         const std::string& context) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    
    ofs << timestr() << " [RETURN_LOSS_CALC]";
    if (!context.empty()) ofs << " [" << context << "]";
    ofs << "\n";
    ofs << "  Input |Gamma|: " << gamma_mag << "\n";
    ofs << "  => Return Loss: " << return_loss_db << " dB\n";
    ofs << "  Explanation: RL = -20 * log10(|Gamma|)\n";
    ofs << "               Higher dB = better match (less reflection)\n";
    ofs << "               Perfect match => infinite dB\n";
    ofs.flush();
}

void MathLogger::logPhaseCalculation(double im, double re, double phase_deg,
                                    const std::string& context) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    
    ofs << timestr() << " [PHASE_CALC]";
    if (!context.empty()) ofs << " [" << context << "]";
    ofs << "\n";
    ofs << "  Input complex: " << re << " + j" << im << "\n";
    ofs << "  => Phase: " << phase_deg << " degrees\n";
    ofs << "  Explanation: Phase = atan2(im, re) * 180/pi\n";
    ofs.flush();
}

void MathLogger::logMagnitudeCalculation(double re, double im, double magnitude,
                                        const std::string& context) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    
    ofs << timestr() << " [MAGNITUDE_CALC]";
    if (!context.empty()) ofs << " [" << context << "]";
    ofs << "\n";
    ofs << "  Input complex: " << re << " + j" << im << "\n";
    ofs << "  => Magnitude: " << magnitude << "\n";
    ofs << "  Explanation: |Z| = sqrt(re^2 + im^2)\n";
    ofs.flush();
}

void MathLogger::logAudioOutput(size_t position, const std::string& curve_name,
                               double value, double pitch_hz, double pan,
                               const std::string& waveform_type) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    
    ofs << timestr() << " [AUDIO_OUTPUT] Position " << position << "\n";
    ofs << "  Curve: " << curve_name << "\n";
    ofs << "  Value: " << value << "\n";
    ofs << "  => Audio Pitch: " << pitch_hz << " Hz\n";
    ofs << "  => Stereo Pan: " << pan << " (-1.0=left, 0.0=center, +1.0=right)\n";
    if (!waveform_type.empty()) {
        ofs << "  Waveform: " << waveform_type << "\n";
    }
    ofs << "  Explanation: Value is mapped to pitch frequency\n";
    ofs << "               Position is mapped to stereo panning\n";
    ofs << "               Higher values => higher pitch (easier to distinguish)\n";
    ofs.flush();
}

void MathLogger::logUserOutput(const std::string& output_type, 
                              const std::string& content) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    
    ofs << timestr() << " [USER_OUTPUT] Type: " << output_type << "\n";
    ofs << "  Content: " << content << "\n";
    ofs.flush();
}

void MathLogger::logDataFlow(const std::string& stage, const std::string& description) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    
    ofs << timestr() << " [DATA_FLOW] Stage: " << stage << "\n";
    ofs << "  " << description << "\n";
    ofs.flush();
}

void MathLogger::logSeparator(const std::string& section_name) {
    std::lock_guard<std::mutex> l(mtx);
    if (!ofs.is_open()) return;
    
    ofs << "\n----------------------------------------\n";
    if (!section_name.empty()) {
        ofs << "  " << section_name << "\n";
        ofs << "----------------------------------------\n";
    }
    ofs.flush();
}
