/**
 * @file battery_monitor.h
 * @brief Battery voltage, percentage, and USB power monitoring
 */

#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief USB PGOOD GPIO 초기화 (한 번만 호출, light sleep wakeup 포함)
 *
 * ADC init/deinit 사이클과 독립적으로 GPIO10을 설정하고
 * USB 연결 시 light sleep에서 깨어나도록 wakeup source를 등록한다.
 */
void battery_usb_gpio_init(void);

/**
 * @brief Initialize the battery monitor (ADC only)
 */
void battery_monitor_init(void);

/**
 * @brief Deinitialize the battery monitor (ADC 해제, 슬립 중 전력 절약)
 */
void battery_monitor_deinit(void);

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

/**
 * @brief USB 전원 연결 상태 확인 (BQ24075 PGOOD)
 * @return true = USB 연결됨 (충전 중), false = USB 미연결
 */
bool battery_is_usb_connected(void);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_MONITOR_H
