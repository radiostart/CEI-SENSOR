/**
 * @file display_service.h
 * @brief 디스플레이 서비스 (UI 상위 레이어)
 */

#ifndef DISPLAY_SERVICE_H
#define DISPLAY_SERVICE_H

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 디스플레이 서비스 초기화
 * @return ESP_OK 성공
 */
esp_err_t display_service_init(void);

/**
 * @brief 센서 데이터로 디스플레이 업데이트
 * @param data 센서 데이터
 * @param battery_pct 배터리 잔량 (%)
 */
void display_service_update(const sensor_data_t *data, int battery_pct);

/**
 * @brief 전원 OFF 화면 표시
 */
void display_service_show_power_off(void);

/**
 * @brief 디스플레이 전체 갱신 (고스팅 제거)
 * 
 * E-Paper의 흐려짐(고스팅)을 해결하기 위해 전체 화면을 갱신합니다.
 */
void display_service_full_refresh(void);

/**
 * @brief 디스플레이 슬립 모드
 */
void display_service_sleep(void);

/**
 * @brief 디스플레이 웨이크업
 */
void display_service_wakeup(void);

/**
 * @brief 전체 갱신 필요 여부 확인 (주기적 갱신용)
 * @return true = 전체 갱신 필요
 */
bool display_service_needs_full_refresh(void);

/**
 * @brief 부분 갱신 카운터 리셋
 */
void display_service_reset_refresh_counter(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_SERVICE_H
