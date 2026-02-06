/**
 * @file app_config.h
 * @brief 애플리케이션 전역 설정
 *
 * 모든 하드웨어 핀 설정, 타이밍 상수, 기능 플래그를 한 곳에서 관리
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 기능 플래그
// ============================================================
#define APP_ENABLE_BLE 1         // BLE 활성화 (0=비활성화)
#define APP_USE_DUMMY_SENSOR 0   // 더미 센서 모드 (1=테스트용)

// ============================================================
// I2C 설정
// ============================================================
#define APP_I2C_PORT I2C_NUM_0
#define APP_I2C_SDA_PIN GPIO_NUM_0
#define APP_I2C_SCL_PIN GPIO_NUM_1
#define APP_I2C_FREQ_HZ 50000    // 50kHz

// ============================================================
// 버튼 설정
// ============================================================
#define APP_BUTTON_PIN GPIO_NUM_20
#define APP_BUTTON_HOLD_TIME_MS 3000  // 전원 ON/OFF 홀드 시간

// ============================================================
// 타이밍 설정
// ============================================================
#define APP_SLEEP_DURATION_US (10 * 1000000ULL)  // 슬립 시간 (10초)
#define APP_BLE_BROADCAST_MS 3000                 // BLE 브로드캐스트 시간

// ============================================================
// 센서 임계값
// ============================================================
#define APP_TEMP_CHANGE_THRESHOLD 0.2f   // 온도 변화 임계값 (°C)
#define APP_HUM_CHANGE_THRESHOLD 1.5f    // 습도 변화 임계값 (%)

// ============================================================
// E-Paper 설정
// ============================================================
#define APP_EPD_FULL_REFRESH_INTERVAL 10  // N회 부분 갱신마다 전체 갱신 (0=비활성화)

// ============================================================
// 애플리케이션 상태
// ============================================================
typedef enum {
  APP_STATE_INIT = 0,    // 초기화 중
  APP_STATE_ACTIVE,      // 활성 (측정/표시)
  APP_STATE_SLEEP,       // 슬립 모드
  APP_STATE_OFF,         // 전원 OFF 대기
  APP_STATE_WAKEUP       // 웨이크업 처리 중
} app_state_t;

// ============================================================
// 센서 데이터 구조체
// ============================================================
typedef struct {
  float temperature;     // 온도 (°C)
  float humidity;        // 습도 (%)
  float cess;            // CESS 점수
  int eml;               // EML 레벨 (-3 ~ +3)
  bool valid;            // 데이터 유효성
} sensor_data_t;

#ifdef __cplusplus
}
#endif

#endif // APP_CONFIG_H
