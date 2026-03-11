/**
 * @file app_config.h
 * @brief Mellow Air 애플리케이션 전역 설정
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
#define APP_ENABLE_BLE 1        // BLE GATT Server 활성화 (0=비활성화)
#define APP_ENABLE_OTA 1        // BLE OTA 펌웨어 업데이트 (4MB flash)
#define APP_USE_DUMMY_SENSOR 0  // 더미 센서 모드 (1=테스트용)

// ============================================================
// I2C 설정
// ============================================================
#define APP_I2C_PORT    I2C_NUM_0
#define APP_I2C_SDA_PIN GPIO_NUM_0
#define APP_I2C_SCL_PIN GPIO_NUM_1
#define APP_I2C_FREQ_HZ 50000  // 50kHz

// ============================================================
// 버튼 설정 (GPIO3 = RTC GPIO, 딥슬립 웨이크업 지원)
// ============================================================
#define APP_BUTTON_PIN          GPIO_NUM_3
#define APP_BUTTON_HOLD_TIME_MS 3000  // 전원 OFF 홀드 시간
#define APP_BUTTON_POWERON_MS   200   // 전원 ON 홀드 시간 (부팅 시간이 디바운스 역할)

// ============================================================
// USB 전원 감지 (BQ24075 PGOOD)
// ============================================================
#define APP_USB_PGOOD_PIN GPIO_NUM_10  // LOW=USB 연결됨, HIGH=미연결

// ============================================================
// 타이밍 설정
// ============================================================
#define APP_SLEEP_DURATION_US      (10 * 1000000ULL)  // 활성 슬립 시간 (10초)
#define APP_SLEEP_IDLE_DURATION_US (30 * 1000000ULL)  // 유휴 슬립 시간 (30초, 공정 미실행+BLE 미연결)
#define APP_BLE_BROADCAST_MS  1500               // BLE 브로드캐스트 창 (UPDATE 시, 1.5초)
#define APP_BLE_DISCOVERY_MS  1500               // BLE 탐색 창 (SKIP 시, 절전)

// ============================================================
// 센서 임계값
// ============================================================
#define APP_TEMP_CHANGE_THRESHOLD 0.2f  // 온도 변화 임계값 (°C)
#define APP_HUM_CHANGE_THRESHOLD  1.5f  // 습도 변화 임계값 (%)
#define APP_HIGH_TEMP_WARN_THRESHOLD 80.0f  // 고온 경고 임계값 (°C)

// ============================================================
// 배터리 임계값
// ============================================================
#define APP_BATTERY_LOW_MV      3300  // 자동 종료 임계값 (mV)
#define APP_BATTERY_RECOVERY_MV 3500  // 충전 후 자동 복귀 임계값 (mV)

// ============================================================
// E-Paper 설정
// ============================================================
#define APP_EPD_FULL_REFRESH_INTERVAL 10  // N회 부분 갱신마다 전체 갱신

// ============================================================
// SPIFFS 설정
// ============================================================
#define APP_SPIFFS_BASE_PATH    "/spiffs"
#define APP_SPIFFS_PARTITION    "spiffs"
#define APP_LOG_MAX_RECORDS     8640   // 10초 간격 24시간 (24 * 3600 / 10)

// ============================================================
// 펌웨어 버전 (Git 태그에서 자동 추출, 폴백: 0.0.0)
// ============================================================
#ifndef APP_FW_MAJOR
#define APP_FW_MAJOR 0
#endif
#ifndef APP_FW_MINOR
#define APP_FW_MINOR 0
#endif
#ifndef APP_FW_PATCH
#define APP_FW_PATCH 0
#endif

// ============================================================
// 애플리케이션 상태
// ============================================================
typedef enum {
  APP_STATE_INIT = 0,  // 초기화 중
  APP_STATE_ACTIVE,    // 활성 (측정/표시)
  APP_STATE_OFF,       // 전원 OFF 대기
} app_state_t;

// ============================================================
// 센서 데이터 구조체
// ============================================================
typedef struct {
  float temperature;   // 온도 (°C)
  float humidity;      // 습도 (%)
  bool valid;          // 데이터 유효성
  bool high_temp_warn; // 80°C 이상 고온 경고
} sensor_data_t;

// ============================================================
// 공정 컨텍스트 구조체 (NVS 저장/복원)
// ============================================================
typedef struct {
  char process_name[32];  // 공정명 (null-terminated)
  float target_temp;      // 목표 온도 (°C)
  float target_humi;      // 목표 습도 (%)
  float tolerance_temp;   // 온도 허용 편차 (°C)
  float tolerance_humi;   // 습도 허용 편차 (%)
  uint16_t duration_min;  // 공정 시간 (분)
  uint16_t _pad;          // 정렬 패딩
  uint32_t start_time;    // 시작 시각 (unix timestamp, app에서 설정)
  bool is_active;         // 공정 활성화 여부
} process_context_t;

#ifdef __cplusplus
}
#endif

#endif  // APP_CONFIG_H
