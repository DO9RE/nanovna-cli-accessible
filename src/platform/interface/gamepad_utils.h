#ifndef GAMEPAD_UTILS_H
#define GAMEPAD_UTILS_H

/**
 * @file gamepad_utils.h
 * @brief Shared gamepad normalization and vibration scaling utilities
 *
 * Centralizes axis/trigger normalization and float-to-native-integer
 * conversion so that all platform backends produce identical behavior.
 */

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace GamepadUtils {

/**
 * Normalize a signed stick axis value to the range [-1.0, +1.0].
 * Applies a configurable deadzone: values within the deadzone return 0.
 * @param rawValue   Raw axis reading
 * @param minRaw     Minimum raw value (e.g. -32767)
 * @param maxRaw     Maximum raw value (e.g. +32767)
 * @param deadzone   Deadzone threshold in normalized space (0.0–1.0)
 */
inline float normalizeAxis(int rawValue, int minRaw, int maxRaw, float deadzone = 0.0f) {
    float mid = (minRaw + maxRaw) * 0.5f;
    float half = (maxRaw - minRaw) * 0.5f;
    if (half == 0.0f) return 0.0f;
    float norm = (static_cast<float>(rawValue) - mid) / half;
    norm = std::max(-1.0f, std::min(1.0f, norm));
    if (std::abs(norm) < deadzone) return 0.0f;
    return norm;
}

/**
 * Normalize an unsigned trigger value to the range [0.0, 1.0].
 * @param rawValue   Raw trigger reading
 * @param minRaw     Minimum raw value (fully released)
 * @param maxRaw     Maximum raw value (fully pressed)
 */
inline float normalizeTrigger(int rawValue, int minRaw, int maxRaw) {
    if (maxRaw == minRaw) return 0.0f;
    float norm = static_cast<float>(rawValue - minRaw) / static_cast<float>(maxRaw - minRaw);
    return std::max(0.0f, std::min(1.0f, norm));
}

/**
 * Convert a float motor intensity (0.0–1.0) to a 16-bit unsigned value.
 * Used by Windows XInput and Linux Force-Feedback APIs.
 */
inline uint16_t floatToU16(float value) {
    value = std::max(0.0f, std::min(1.0f, value));
    return static_cast<uint16_t>(value * 65535.0f);
}

/**
 * Convert a float motor intensity (0.0–1.0) to an 8-bit unsigned value.
 * Used by DualShock 4 and DualSense HID vibration reports.
 */
inline uint8_t floatToU8(float value) {
    value = std::max(0.0f, std::min(1.0f, value));
    return static_cast<uint8_t>(value * 255.0f);
}

} // namespace GamepadUtils

#endif // GAMEPAD_UTILS_H
