/**
 * @file battery_monitor.h
 * @brief Battery voltage and percentage monitoring
 */

#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the battery monitor (ADC)
 */
void battery_monitor_init(void);

/**
 * @brief Read current battery voltage in mV
 * @return Voltage in millivolts
 */
uint32_t battery_read_voltage(void);

/**
 * @brief Calculate battery percentage (0-100)
 * @return Battery percentage
 */
int battery_get_percentage(void);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_MONITOR_H
