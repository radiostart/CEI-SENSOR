/**
 * @file ble_server.h
 * @brief BakeTrack BLE GATT Server
 *
 * Service UUID: BA5E0001-0000-1000-8000-00805F9B34FB
 *
 * Characteristics:
 *   REALTIME_DATA  (Notify)   - 10초마다 현재 온습도 + 타임스탬프
 *   UNSENT_DATA    (Indicate) - 미전송 로그 청크 전송 (앱 ACK 필요)
 *   PROCESS_CONFIG (Write)    - 공정 설정값 수신
 *   DEVICE_STATUS  (Read)     - 펌웨어 버전, 저장소 사용량, 업타임
 *   DEVICE_NAME    (Read/Write) - 기기 이름
 *   ELAPSED_SYNC   (Write)    - 앱 경과 시간 동기화 (앱 우선)
 */

#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include "app_config.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_DEVICE_NAME     "BakeTrack"
#define BLE_DEVICE_NAME_MAX_LEN 20  // advertising 31바이트 제한 고려

// ============================================================
// BLE 패킷 구조체
// ============================================================

// REALTIME_DATA notify (16 bytes)
typedef struct __attribute__((packed)) {
  uint32_t timestamp;     // unix timestamp 근사값
  int16_t  temp_x10;     // 온도 × 10
  int16_t  humi_x10;     // 습도 × 10
  uint8_t  flags;         // bit0 = high_temp_warn
  uint8_t  _pad[3];
  uint32_t elapsed_sec;   // 공정 경과 시간 (초), 비활성 시 0
} ble_realtime_pkt_t;

// UNSENT_DATA 청크 (20 bytes)
typedef struct __attribute__((packed)) {
  uint8_t  type;        // 0=data, 1=end_of_data
  uint16_t seq;         // 시퀀스 번호
  uint32_t timestamp;
  int16_t  temp_x10;
  int16_t  humi_x10;
  uint8_t  flags;
  uint8_t  _pad[8];
} ble_unsent_chunk_t;

// PROCESS_CONFIG write (56 bytes)
typedef struct __attribute__((packed)) {
  char     process_name[32];
  float    target_temp;
  float    target_humi;
  float    tolerance_temp;
  float    tolerance_humi;
  uint16_t duration_min;
  uint16_t _pad;
  uint32_t start_time;  // unix timestamp
} ble_process_config_pkt_t;

// DEVICE_STATUS read (48 bytes)
typedef struct __attribute__((packed)) {
  uint8_t  fw_major;
  uint8_t  fw_minor;
  uint16_t fw_patch;
  uint32_t storage_used;
  uint32_t storage_total;
  char     process_name[32];
  uint32_t uptime_sec;
} ble_device_status_t;

// ELAPSED_SYNC write (5 bytes) — 앱 → 펌웨어 경과 시간 동기화
typedef struct __attribute__((packed)) {
  uint32_t elapsed_sec;  // 앱의 경과 시간 (초)
  uint8_t  flags;        // bit0 = paused
} ble_elapsed_sync_pkt_t;

// ============================================================
// API
// ============================================================

/** @brief BLE GATT 서버 초기화 */
esp_err_t ble_server_init(void);

/** @brief 광고 일시 중지 */
void ble_server_pause(void);

/** @brief 광고 재개 (Fast: 100-200ms 인터벌, UPDATE/재연결용) */
void ble_server_resume(void);

/** @brief 저전력 광고 재개 (Slow: 800-1600ms 인터벌, idle 탐색용) */
void ble_server_resume_slow(void);

/**
 * @brief REALTIME_DATA notify 전송
 * @param data        현재 센서 데이터
 * @param timestamp   unix timestamp 근사값
 * @param elapsed_sec 공정 경과 시간 (초), 비활성 시 0
 */
void ble_server_notify_realtime(const sensor_data_t *data, uint32_t timestamp,
                                uint32_t elapsed_sec);

/** @brief BLE 연결 상태 확인 */
bool ble_server_is_connected(void);

/** @brief 앱이 characteristic을 구독했는지 확인 (유령 연결 감지용) */
bool ble_server_is_subscribed(void);

/** @brief 앱 구독 시작 시 즉시 데이터 전송 필요 여부 (consume 패턴) */
bool ble_server_consume_initial_sync(void);

/** @brief 현재 연결 강제 해제 (유령 연결 정리용) */
void ble_server_disconnect(void);

/**
 * @brief 앱이 전송한 새 공정 설정 폴링
 * @param ctx  새 설정 출력 (OUT)
 * @return true = 새 설정 수신됨
 */
bool ble_server_poll_process_config(process_context_t *ctx);

/** @brief 현재 기기 이름 반환 (NVS에서 로드된 값 또는 기본값) */
const char *ble_server_get_device_name(void);

/** @brief 앱이 ELAPSED_SYNC를 전송한 적 있는지 확인 (현재 연결 세션) */
bool ble_server_has_app_elapsed(void);

/**
 * @brief 앱이 전송한 경과 시간 조회
 * @param elapsed  앱의 경과 시간 (초, OUT)
 * @param paused   앱의 일시정지 상태 (OUT)
 * @return true = 유효한 값 있음
 */
bool ble_server_get_app_elapsed(uint32_t *elapsed, bool *paused);

/** @brief 앱 elapsed 상태 리셋 (연결 해제 전환 완료 후 호출) */
void ble_server_clear_app_elapsed(void);

/**
 * @brief 앱이 전송한 새 경과 시간 소비 (1회만 반환)
 * @param elapsed  앱의 경과 시간 (초, OUT)
 * @param paused   앱의 일시정지 상태 (OUT)
 * @return true = 새 값 수신됨
 */
bool ble_server_consume_new_elapsed(uint32_t *elapsed, bool *paused);

/** @brief ELAPSED_SYNC BLE 수신 시각 (처리 지연 보상용) */
int64_t ble_server_get_elapsed_sync_rx_time(void);

#ifdef __cplusplus
}
#endif

#endif  // BLE_SERVER_H
