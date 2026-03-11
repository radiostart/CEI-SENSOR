/**
 * @file ota_update.h
 * @brief BLE OTA 펌웨어 업데이트 모듈
 *
 * BLE GATT를 통한 펌웨어 청크 수신, SHA256 검증, 파티션 교체.
 * 별도 FreeRTOS 태스크에서 실행 (센서/디스플레이 루프 비차단).
 *
 * BLE Characteristics:
 *   OTA_CONTROL  (BA5E0009) — Write: 시작/완료/취소 명령
 *   OTA_DATA     (BA5E000A) — Write Without Response: 펌웨어 청크
 *   OTA_STATUS   (BA5E000B) — Read/Notify: 진행 상태
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// OTA 상태
typedef enum {
  OTA_STATE_IDLE       = 0x00,
  OTA_STATE_RECEIVING  = 0x01,
  OTA_STATE_VERIFYING  = 0x02,
  OTA_STATE_COMPLETE   = 0x03,
  OTA_STATE_ERROR      = 0xFF,
} ota_state_t;

// OTA 에러 코드
typedef enum {
  OTA_ERR_NONE           = 0x00,
  OTA_ERR_FLASH_WRITE    = 0x01,
  OTA_ERR_SHA256_MISMATCH = 0x02,
  OTA_ERR_SIZE_MISMATCH  = 0x03,
  OTA_ERR_PARTITION      = 0x04,
  OTA_ERR_TIMEOUT        = 0x05,
  OTA_ERR_INTERNAL       = 0x06,
} ota_error_t;

// OTA_STATUS 패킷 (12 bytes, Read/Notify)
typedef struct __attribute__((packed)) {
  uint8_t  state;          // ota_state_t
  uint8_t  progress_pct;   // 0-100
  uint8_t  error_code;     // ota_error_t
  uint8_t  _pad;
  uint32_t bytes_received;
  uint32_t total_bytes;
} ota_status_pkt_t;

// OTA_CONTROL 명령 코드
#define OTA_CMD_START    0x01  // {cmd(1), total_size(4), sha256[32]} = 37 bytes
#define OTA_CMD_COMPLETE 0x02  // {cmd(1)} = 1 byte (검증 트리거)
#define OTA_CMD_CANCEL   0x03  // {cmd(1)} = 1 byte

/**
 * @brief OTA 모듈 초기화 (FreeRTOS 태스크 + 큐 생성)
 * @return ESP_OK 성공
 */
esp_err_t ota_update_init(void);

/**
 * @brief OTA_CONTROL 쓰기 핸들러 (BLE 콜백에서 호출)
 * @param data  수신 데이터
 * @param len   데이터 길이
 */
void ota_update_handle_control(const uint8_t *data, uint16_t len);

/**
 * @brief OTA_DATA 쓰기 핸들러 (BLE 콜백에서 호출)
 * @param data  청크 데이터 (seq(2) + payload)
 * @param len   데이터 길이
 */
void ota_update_handle_data(const uint8_t *data, uint16_t len);

/**
 * @brief OTA 진행 중 여부
 * @return true = OTA 수신/검증 중
 */
bool ota_update_is_active(void);

/**
 * @brief 현재 OTA 상태 조회 (Read 핸들러용)
 * @param out  상태 패킷 출력
 */
void ota_update_get_status(ota_status_pkt_t *out);

/**
 * @brief BLE 연결 해제 시 호출 (타임아웃 대기 없이 즉시 취소)
 */
void ota_update_on_disconnect(void);

/**
 * @brief OTA 상태 변경 시 Notify 전송 콜백 등록
 * @param cb  BLE notify 전송 함수 포인터
 */
void ota_update_set_notify_cb(void (*cb)(const ota_status_pkt_t *status));

#ifdef __cplusplus
}
#endif

#endif  // OTA_UPDATE_H
