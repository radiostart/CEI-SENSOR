/**
 * @file eml_calculator.h
 * @brief Environment Moisture Level (EML) Calculator
 *
 * Calculates a 7-step moisture level (-3 to +3) based on temperature and
 * humidity.
 */

#ifndef EML_CALCULATOR_H
#define EML_CALCULATOR_H

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// EML Level Definitions
// ============================================================
typedef enum {
  ENV_MOISTURE_VERY_DRY = -3,    // 매우 건조
  ENV_MOISTURE_DRY = -2,         // 건조
  ENV_MOISTURE_SLIGHT_DRY = -1,  // 약간 건조
  ENV_MOISTURE_NEUTRAL = 0,      // 적정
  ENV_MOISTURE_SLIGHT_HUMID = 1, // 약간 습함
  ENV_MOISTURE_HUMID = 2,        // 습함
  ENV_MOISTURE_VERY_HUMID = 3    // 매우 습함
} env_moisture_level_t;

// ============================================================
// Function Declarations
// ============================================================

/**
 * @brief Classify environment moisture level based on temperature and humidity.
 *
 * Logic:
 * 1. Base level determined by humidity thresholds (25/35/45/55/65/75%).
 * 2. Temperature correction:
 *    - If Hot (>= 27°C) and Humid (>= 45%): Shift +1 (More Humid)
 *    - If Cold (<= 18°C) and Dry (<= 55%): Shift -1 (More Dry)
 * 3. Clamped to -3 ~ +3 range.
 *
 * @param temp_c Current temperature in Celsius
 * @param hum_percent Current humidity in %
 * @return env_moisture_level_t Calculated level (-3 to +3)
 */
env_moisture_level_t eml_classify_moisture(double temp_c, double hum_percent);

/**
 * @brief Convert EML level to English string.
 * @param lvl EML level
 * @return String (e.g., "VERY DRY", "NEUTRAL", etc.)
 */
const char *eml_get_level_str(env_moisture_level_t lvl);

#ifdef __cplusplus
}
#endif

#endif // EML_CALCULATOR_H
