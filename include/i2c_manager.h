/**
 * @file i2c_manager.h
 * @brief I2C 버스 관리
 */

#ifndef I2C_MANAGER_H
#define I2C_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C 버스 초기화
 * @return ESP_OK 성공
 */
esp_err_t i2c_manager_init(void);

/**
 * @brief I2C 버스 해제 (저전력용)
 */
void i2c_manager_deinit(void);

/**
 * @brief I2C 버스 상태 확인
 * @return true = 초기화됨
 */
bool i2c_manager_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif // I2C_MANAGER_H
