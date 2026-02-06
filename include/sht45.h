#ifndef SHT45_H
#define SHT45_H

#include "driver/i2c.h"
#include "esp_err.h"
#include <stdint.h>

// SHT-45 I2C 주소 (SHT4x 시리즈 공통)
#define SHT45_I2C_ADDR_A 0x44           // 기본 주소
#define SHT45_I2C_ADDR_B 0x45           // 대체 주소
#define SHT45_I2C_ADDR SHT45_I2C_ADDR_A // 호환성 유지

// SHT-45 명령어
#define SHT45_CMD_MEASURE_HIGH 0xFD     // 고정밀도 측정
#define SHT45_CMD_MEASURE_MED 0xF6      // 중간 정밀도 측정
#define SHT45_CMD_MEASURE_LOW 0xE0      // 저정밀도 측정
#define SHT45_CMD_SOFT_RESET 0x94       // 소프트 리셋
#define SHT45_CMD_HEATER_200MW_1S 0x39  // 히터 200mW 1초
#define SHT45_CMD_HEATER_200MW_01S 0x32 // 히터 200mW 0.1초
#define SHT45_CMD_HEATER_110MW_1S 0x2F  // 히터 110mW 1초
#define SHT45_CMD_HEATER_110MW_01S 0x24 // 히터 110mW 0.1초
#define SHT45_CMD_HEATER_20MW_1S 0x1E   // 히터 20mW 1초
#define SHT45_CMD_HEATER_20MW_01S 0x15  // 히터 20mW 0.1초
#define SHT45_CMD_READ_SERIAL 0x89      // 시리얼 번호 읽기

// I2C 설정 구조체
typedef struct {
  i2c_port_t i2c_port;
  gpio_num_t sda_pin;
  gpio_num_t scl_pin;
  uint32_t clk_speed;
} sht45_config_t;

// 센서 데이터 구조체
typedef struct {
  float temperature; // 온도 (°C)
  float humidity;    // 상대습도 (%)
} sht45_data_t;

/**
 * @brief SHT-45 센서 초기화
 *
 * @param config I2C 설정 구조체
 * @return esp_err_t ESP_OK 성공, 그 외 실패
 */
esp_err_t sht45_init(const sht45_config_t *config);

/**
 * @brief SHT4x 센서 주소 스캔 (0x44, 0x45)
 *
 * 연결된 센서의 주소를 자동으로 감지하여 내부적으로 설정합니다.
 * @return esp_err_t ESP_OK(감지됨), ESP_ERR_NOT_FOUND(감지 안됨), 그 외 오류
 */
esp_err_t sht45_scan(void);

/**
 * @brief SHT-45 소프트 리셋
 *
 * @return esp_err_t ESP_OK 성공, 그 외 실패
 */
esp_err_t sht45_soft_reset(void);

/**
 * @brief SHT-45 온습도 측정 (고정밀도)
 *
 * @param data 측정된 데이터를 저장할 구조체 포인터
 * @return esp_err_t ESP_OK 성공, 그 외 실패
 */
esp_err_t sht45_read_temperature_humidity(sht45_data_t *data);

/**
 * @brief SHT-45 시리얼 번호 읽기
 *
 * @param serial 시리얼 번호를 저장할 배열 (4바이트)
 * @return esp_err_t ESP_OK 성공, 그 외 실패
 */
esp_err_t sht45_read_serial_number(uint32_t *serial);

/**
 * @brief CRC-8 체크섬 검증
 *
 * @param data 데이터 배열
 * @param len 데이터 길이
 * @param crc 검증할 CRC 값
 * @return true 검증 성공
 * @return false 검증 실패
 */
bool sht45_check_crc(const uint8_t *data, uint8_t len, uint8_t crc);

#endif // SHT45_H
