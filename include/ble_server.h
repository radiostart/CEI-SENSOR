#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include "esp_err.h"

// BLE 디바이스 이름 (앱에서 보이는 이름)
#define BLE_DEVICE_NAME "CEI-Sensor"

/**
 * @brief BLE 서버 초기화
 *
 * @return esp_err_t ESP_OK 성공, 그 외 실패
 */
esp_err_t ble_server_init(void);

/**
 * @brief Advertising 데이터 업데이트 (Broadcaster 모드)
 *
 * @param temp 온도 (°C)
 * @param hum 습도 (%)
 * @param cess CESS 환경 지수
 * @return esp_err_t ESP_OK 성공, 그 외 실패
 */
esp_err_t ble_update_advertising_data(float temp, float hum, float cess);

/**
 * @brief BLE 광고 일시 중지 (슬립 모드용)
 */
void ble_server_pause(void);

/**
 * @brief BLE 광고 재개
 */
void ble_server_resume(void);

#endif // BLE_SERVER_H
