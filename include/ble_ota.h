/**
 * @file ble_ota.h
 * @brief BLE OTA (Over-The-Air) 펌웨어 업데이트
 *
 * BLE 특성:
 *   OTA_CONTROL  BA5E0009 (Write)              — 앱→기기 명령
 *   OTA_DATA     BA5E000A (Write No Response)   — 바이너리 청크
 *   OTA_STATUS   BA5E000B (Notify)              — 상태/진행률
 *
 * 프로토콜:
 *   1. 앱 → OTA_CONTROL: START {cmd=0x01, total_size(4)} = 5 bytes
 *   2. 기기 → OTA_STATUS: READY
 *   3. 앱 → OTA_DATA: 청크 반복 (MTU-3 bytes)
 *   4. 기기 → OTA_STATUS: progress 갱신 (1% 단위)
 *   5. 앱 → OTA_CONTROL: COMMIT {cmd=0x02}
 *   6. 기기 → OTA_STATUS: VERIFYING → SUCCESS
 *   7. 기기 자동 재부팅 (3초 후)
 */

#ifndef BLE_OTA_H
#define BLE_OTA_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// OTA 제어 명령 (앱→기기, OTA_CONTROL에 Write)
#define OTA_CMD_START   0x01  // {cmd(1) + total_size(4)} = 5 bytes
#define OTA_CMD_COMMIT  0x02  // 검증 + 부트 파티션 전환
#define OTA_CMD_ABORT   0x03  // OTA 취소

// OTA 상태 (기기→앱, OTA_STATUS Notify)
typedef enum {
    OTA_STATE_IDLE      = 0,  // 대기
    OTA_STATE_READY     = 1,  // START 수신 완료, 청크 수신 대기
    OTA_STATE_RECEIVING = 2,  // 데이터 수신 중
    OTA_STATE_VERIFYING = 3,  // 이미지 검증 중
    OTA_STATE_SUCCESS   = 4,  // 완료, 재부팅 대기
    OTA_STATE_ERROR     = 5,  // 오류 발생
} ota_state_t;

// OTA 오류 코드
typedef enum {
    OTA_ERR_NONE         = 0,
    OTA_ERR_BEGIN_FAIL   = 1,  // esp_ota_begin 실패
    OTA_ERR_WRITE_FAIL   = 2,  // esp_ota_write 실패
    OTA_ERR_VERIFY_FAIL  = 3,  // esp_ota_end 검증 실패
    OTA_ERR_SET_BOOT     = 4,  // 부트 파티션 설정 실패
    OTA_ERR_NO_PARTITION = 5,  // OTA 파티션 없음
    OTA_ERR_ABORTED      = 6,  // 앱에서 취소
} ota_error_t;

// OTA 상태 패킷 (Notify, 4 bytes)
typedef struct __attribute__((packed)) {
    uint8_t  state;         // ota_state_t
    uint8_t  progress_pct;  // 0~100
    uint8_t  error_code;    // ota_error_t
    uint8_t  _pad;
} ble_ota_status_pkt_t;

/** @brief OTA 제어 명령 처리 (BLE 콜백에서 호출) */
void ble_ota_handle_control(const uint8_t *data, uint16_t len);

/** @brief OTA 데이터 청크 수신 (BLE 콜백에서 호출) */
void ble_ota_handle_data(const uint8_t *data, uint16_t len);

/**
 * @brief OTA 상태 폴링 (메인 루프에서 호출)
 * @param status_out  상태 패킷 출력 (OUT)
 * @return true = 상태 변경됨 (Notify 전송 필요)
 */
bool ble_ota_poll_status(ble_ota_status_pkt_t *status_out);

/** @brief OTA 진행 중 여부 (슬립 방지용) */
bool ble_ota_is_active(void);

/** @brief OTA 완료 후 재부팅 필요 여부 */
bool ble_ota_needs_reboot(void);

/** @brief OTA 재부팅 실행 (3초 딜레이 후 esp_restart) */
void ble_ota_reboot(void);

/** @brief OTA 상태 리셋 (연결 해제 시) */
void ble_ota_reset(void);

#ifdef __cplusplus
}
#endif

#endif  // BLE_OTA_H
