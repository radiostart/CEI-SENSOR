/**
 * @file spiffs_logger.h
 * @brief SPIFFS 기반 센서 데이터 로거 (링 버퍼)
 *
 * 최대 25920개 레코드 (10초 간격 × 72시간)
 * 레코드 크기: 16 bytes (4+4+4+1+3 padding)
 */

#ifndef SPIFFS_LOGGER_H
#define SPIFFS_LOGGER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 레코드 구조체 (16 bytes, SPIFFS 이진 파일)
// ============================================================
typedef struct __attribute__((packed)) {
  uint32_t timestamp;  // unix timestamp (0=미설정)
  float temp;          // 온도 (°C)
  float humidity;      // 습도 (%)
  uint8_t sent;        // 앱 전송 완료 여부 (0=미전송, 1=전송됨)
  uint8_t _pad[3];     // 정렬 패딩
} log_record_t;        // 16 bytes

// ============================================================
// API
// ============================================================

/**
 * @brief SPIFFS 마운트 및 로거 초기화
 * @return ESP_OK 성공
 */
esp_err_t spiffs_logger_init(void);

/**
 * @brief 센서 데이터 1건 추가 (링 버퍼)
 * @param temp  온도
 * @param humidity  습도
 * @param timestamp  unix timestamp
 * @return ESP_OK 성공
 */
esp_err_t spiffs_logger_append(float temp, float humidity, uint32_t timestamp);

/**
 * @brief 미전송 레코드 수 조회
 * @return 미전송 레코드 수
 */
uint32_t spiffs_logger_unsent_count(void);

/**
 * @brief 저장된 총 레코드 수 조회
 * @return 총 레코드 수
 */
uint32_t spiffs_logger_total_count(void);

/**
 * @brief SPIFFS 사용량 조회 (bytes)
 * @param used  사용 바이트 (OUT)
 * @param total 전체 바이트 (OUT)
 */
void spiffs_logger_get_usage(size_t *used, size_t *total);

/**
 * @brief seq번째 미전송 레코드 읽기
 *
 * seq=0부터 순서대로 호출하여 청크 전송에 사용
 * @param seq     전송 순서 (0-based, 미전송 레코드 중)
 * @param record  읽은 레코드 (OUT)
 * @param log_idx 해당 레코드의 내부 인덱스 (OUT, mark_sent에 사용)
 * @return ESP_OK 성공, ESP_ERR_NOT_FOUND 없음
 */
esp_err_t spiffs_logger_read_unsent(uint32_t seq, log_record_t *record,
                                    uint32_t *log_idx);

/**
 * @brief 특정 인덱스 레코드를 sent=1로 표시
 * @param log_idx  내부 인덱스
 * @return ESP_OK 성공
 */
esp_err_t spiffs_logger_mark_sent(uint32_t log_idx);

#ifdef __cplusplus
}
#endif

#endif  // SPIFFS_LOGGER_H
