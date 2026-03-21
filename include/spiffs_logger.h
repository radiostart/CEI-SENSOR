/**
 * @file spiffs_logger.h
 * @brief SPIFFS 기반 센서 데이터 로거 (링 버퍼)
 *
 * 최대 8640개 레코드 (10초 간격 × 24시간)
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
 * @brief 첫 번째 미전송 레코드의 링 버퍼 오프셋 반환 (O(1))
 * sent 레코드는 앞, unsent는 뒤에 있다는 가정 기반
 */
uint32_t spiffs_logger_first_unsent_offset(void);

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

/**
 * @brief 여러 레코드를 한 번의 파일 열기로 sent=1 표시
 * @param log_indices  내부 인덱스 배열
 * @param count        배열 크기
 * @return ESP_OK 성공
 */
esp_err_t spiffs_logger_mark_sent_batch(const uint32_t *log_indices, uint32_t count);

/**
 * @brief 미전송 레코드를 한 번의 파일 스캔으로 최대 max_count개 읽기
 *
 * ring_offset으로 스캔 시작 위치를 지정하여 O(batch_size) 성능 보장.
 * @param ring_offset    링 버퍼 내 스캔 시작 오프셋 (첫 호출 시 0)
 * @param records        읽은 레코드 배열 (OUT)
 * @param log_indices    각 레코드의 내부 인덱스 (OUT, mark_sent에 사용)
 * @param max_count      최대 읽을 개수 (배열 크기)
 * @param out_count      실제 읽은 개수 (OUT)
 * @param next_offset    다음 호출 시 사용할 오프셋 (OUT)
 * @return ESP_OK 성공
 */
esp_err_t spiffs_logger_read_unsent_batch(uint32_t ring_offset,
                                          log_record_t *records,
                                          uint32_t *log_indices,
                                          uint32_t max_count,
                                          uint32_t *out_count,
                                          uint32_t *next_offset);

/**
 * @brief 모든 레코드의 sent 플래그를 0으로 리셋 (테스트/디버그용)
 * @return ESP_OK 성공
 */
esp_err_t spiffs_logger_reset_range_sent(uint32_t from_offset, uint32_t to_offset);
esp_err_t spiffs_logger_reset_sent_flags(void);
void spiffs_logger_flush(void);

/**
 * @brief 링 버퍼 오프셋 범위의 레코드를 sent=1로 일괄 표시
 *
 * 동기화 완료 후 호출. 16레코드(256B) 청크 단위로 읽기/수정/쓰기하여
 * SPIFFS 페이지 연산 횟수를 최소화.
 * @param from_offset  시작 링 오프셋 (inclusive)
 * @param to_offset    종료 링 오프셋 (exclusive)
 * @return ESP_OK 성공
 */
esp_err_t spiffs_logger_mark_range_sent(uint32_t from_offset, uint32_t to_offset);

/**
 * @brief 모든 로그 데이터 삭제 (파일 재생성 + 인덱스 리셋)
 * @return ESP_OK 성공
 */
esp_err_t spiffs_logger_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif  // SPIFFS_LOGGER_H
