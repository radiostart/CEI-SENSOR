/**
 * @file sensor_service.h
 * @brief 센서 데이터 수집 서비스
 */

#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include "app_config.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 센서 서비스 초기화
 * @return ESP_OK 성공
 */
esp_err_t sensor_service_init(void);

/**
 * @brief 센서 데이터 읽기
 * 
 * I2C 초기화 -> 히터 루틴(6h 주기) -> 센서 읽기 -> I2C 해제
 * @param data 결과를 저장할 구조체
 * @return ESP_OK 성공
 */
esp_err_t sensor_service_read(sensor_data_t *data);

/**
 * @brief 데이터 변화가 유의미한지 확인
 * @param data 현재 데이터
 * @return true = 임계값 이상 변화
 */
bool sensor_service_is_significant_change(const sensor_data_t *data);

/**
 * @brief 마지막 데이터를 현재 데이터로 업데이트
 * @param data 저장할 데이터
 */
void sensor_service_update_last(const sensor_data_t *data);

/**
 * @brief 센서 재스캔 강제 실행 (버튼 클릭 시 호출)
 *
 * 재시도 카운터를 리셋하여 다음 read에서 즉시 스캔
 */
void sensor_service_force_rescan(void);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_SERVICE_H
