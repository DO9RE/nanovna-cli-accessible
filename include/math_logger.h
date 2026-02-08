#pragma once
#include <string>
#include <fstream>
#include <mutex>

// Logger specifically for mathematical operations and data flow
// Logs everything mathematical that happens: raw values, transformations, results, outputs
class MathLogger {
public:
    MathLogger() = default;
    
    // Open the math debug log file
    bool open(const std::string& filename);
    
    // Close the log file
    void close();
    
    // Check if logging is enabled
    bool isEnabled() const { return ofs.is_open(); }
    
    // Log raw measurement data received from device
    void logRawMeasurement(size_t index, uint64_t frequency, 
                          double s11_re, double s11_im, 
                          const std::string& context = "");
    
    // Log gamma calculation (from S11 complex or mag/phase)
    void logGammaCalculation(double s11_re, double s11_im, 
                            double gamma_mag, double gamma_phase_deg,
                            const std::string& method = "");
    
    // Log impedance calculation
    void logImpedanceCalculation(double gamma_re, double gamma_im,
                                double z0, double R, double X,
                                const std::string& context = "");
    
    // Log SWR calculation
    void logSWRCalculation(double gamma_mag, double swr,
                          const std::string& context = "");
    
    // Log return loss calculation
    void logReturnLossCalculation(double gamma_mag, double return_loss_db,
                                 const std::string& context = "");
    
    // Log phase calculation
    void logPhaseCalculation(double im, double re, double phase_deg,
                            const std::string& context = "");
    
    // Log magnitude calculation
    void logMagnitudeCalculation(double re, double im, double magnitude,
                                const std::string& context = "");
    
    // Log audio output parameters (for acoustic mode)
    void logAudioOutput(size_t position, const std::string& curve_name,
                       double value, double pitch_hz, double pan,
                       const std::string& waveform_type = "");
    
    // Log text output to user
    void logUserOutput(const std::string& output_type, 
                      const std::string& content);
    
    // Log data processing path (high-level flow)
    void logDataFlow(const std::string& stage, const std::string& description);
    
    // Log section separator for readability
    void logSeparator(const std::string& section_name = "");
    
private:
    std::ofstream ofs;
    std::mutex mtx;
    std::string timestr();
};
