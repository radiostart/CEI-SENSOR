/**
 * @file display_service.h
 * @brief 디스플레이 서비스 (UI 상위 레이어) - Mellow Air
 */

#ifndef DISPLAY_SERVICE_H
#define DISPLAY_SERVICE_H

#include "app_config.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 디스플레이 서비스 초기화
 */
esp_err_t display_service_init(void);

/**
 * @brief 센서 데이터 + 공정 컨텍스트로 디스플레이 업데이트
 * @param data         센서 데이터
 * @param battery_pct  배터리 잔량 (%)
 * @param ctx          공정 컨텍스트 (NULL = 미설정)
 * @param elapsed_sec  경과 시간 (초)
 */
void display_service_update(const sensor_data_t *data, int battery_pct,
                            const process_context_t *ctx, uint32_t elapsed_sec);

/**
 * @brief BLE 연결 상태 갱신
 */
void display_service_set_ble_connected(bool connected);

/**
 * @brief 충전 상태 갱신 (USB PGOOD)
 */
void display_service_set_charging(bool charging);

/**
 * @brief 저전압 경고 화면 표시
 */
void display_service_show_low_battery(void);

/**
 * @brief 전원 OFF 화면 표시
 */
void display_service_show_power_off(void);

/**
 * @brief BLE 페어링 모드 화면 표시
 * @param remaining_sec 남은 시간 (초)
 */
void display_service_show_ble_pairing(int remaining_sec);

/**
 * @brief 디스플레이 전체 갱신 (고스팅 제거)
 */
void display_service_full_refresh(void);

/**
 * @brief 디스플레이 슬립
 */
void display_service_sleep(void);

/**
 * @brief 디스플레이 웨이크업
 */
void display_service_wakeup(void);

#ifdef __cplusplus
}
#endif

#endif  // DISPLAY_SERVICE_H
