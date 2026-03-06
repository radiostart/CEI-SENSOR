#ifndef SHT45_H
#define SHT45_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SHT-45 I2C 주소 (SHT4x 시리즈 공통)
#define SHT45_I2C_ADDR_A 0x44           // 기본 주소
#define SHT45_I2C_ADDR_B 0x45           // 대체 주소

// SHT-45 명령어
#define SHT45_CMD_MEASURE_HIGH 0xFD     // 고정밀도 측정
#define SHT45_CMD_HEATER_200MW_01S 0x32 // 히터 200mW 0.1초

// 센서 데이터 구조체
typedef struct {
  float temperature; // 온도 (°C)
  float humidity;    // 상대습도 (%)
} sht45_data_t;

/**
 * @brief SHT4x 센서 주소 스캔 (0x44, 0x45)
 *
 * 연결된 센서의 주소를 자동으로 감지하여 내부적으로 설정합니다.
 * @return esp_err_t ESP_OK(감지됨), ESP_ERR_NOT_FOUND(감지 안됨)
 */
esp_err_t sht45_scan(void);

/**
 * @brief SHT-45 온습도 측정 (고정밀도)
 *
 * @param data 측정된 데이터를 저장할 구조체 포인터
 * @return esp_err_t ESP_OK 성공, 그 외 실패
 */
esp_err_t sht45_read_temperature_humidity(sht45_data_t *data);

/**
 * @brief SHT-45 고출력 히터 실행 (결로 방지)
 *
 * 200mW 100ms 히터 명령(0x32) 전송 후 결과를 읽어 버립니다.
 * 이후 2초 안정화 대기는 호출자(sensor_service)가 담당합니다.
 *
 * @return esp_err_t ESP_OK 성공, 그 외 실패
 */
esp_err_t sht45_run_heater_high_power(void);

#ifdef __cplusplus
}
#endif

#endif // SHT45_H
