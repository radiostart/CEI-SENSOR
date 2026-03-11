/**
 * @file ota_update.c
 * @brief BLE OTA 펌웨어 업데이트 구현
 *
 * FreeRTOS 태스크에서 OTA 이벤트 처리:
 * - BLE 콜백은 큐에 이벤트 전송만 (ISR-safe)
 * - OTA 태스크가 esp_ota_ops로 플래시 기록
 * - SHA256 검증 후 부팅 파티션 교체
 */

#include "ota_update.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include <string.h>

static const char *TAG = "OTA";

// ============================================================
// OTA 이벤트 (큐 전송용)
// ============================================================
typedef enum {
  OTA_EVT_START,
  OTA_EVT_DATA,
  OTA_EVT_COMPLETE,
  OTA_EVT_CANCEL,
  OTA_EVT_DISCONNECT,
} ota_evt_type_t;

// 청크 데이터 최대 크기 (BLE MTU 247 - ATT overhead 5 - seq 2 = 240)
#define OTA_MAX_CHUNK_SIZE 240

typedef struct {
  ota_evt_type_t type;
  union {
    struct {
      uint32_t total_size;
      uint8_t sha256[32];
    } start;
    struct {
      uint16_t seq;
      uint16_t len;
      uint8_t data[OTA_MAX_CHUNK_SIZE];
    } data;
  };
} ota_event_t;

// ============================================================
// 상태
// ============================================================
#define OTA_QUEUE_DEPTH     8
#define OTA_TASK_STACK_SIZE 4096
#define OTA_TIMEOUT_SEC     30

static QueueHandle_t s_ota_queue = NULL;
static TaskHandle_t s_ota_task = NULL;

static volatile ota_status_pkt_t s_status = {0};
static volatile bool s_active = false;

// Notify 콜백 (ble_server.c에서 등록)
static void (*s_notify_cb)(const ota_status_pkt_t *status) = NULL;

// ============================================================
// 상태 업데이트 + Notify
// ============================================================
static void update_status(ota_state_t state, uint8_t progress,
                          ota_error_t error, uint32_t received,
                          uint32_t total) {
  ota_status_pkt_t st;
  st.state = (uint8_t)state;
  st.progress_pct = progress;
  st.error_code = (uint8_t)error;
  st._pad = 0;
  st.bytes_received = received;
  st.total_bytes = total;

  // volatile 구조체에 복사 (원자적 업데이트)
  memcpy((void *)&s_status, &st, sizeof(st));

  if (s_notify_cb) {
    s_notify_cb(&st);
  }
}

// ============================================================
// OTA 태스크
// ============================================================
static void ota_task(void *arg) {
  ota_event_t evt;
  esp_ota_handle_t ota_handle = 0;
  const esp_partition_t *target_part = NULL;
  mbedtls_sha256_context sha_ctx;
  uint8_t expected_sha256[32] = {0};
  uint32_t total_size = 0;
  uint32_t received = 0;
  uint16_t expected_seq = 0;
  uint8_t last_progress = 0;
  int64_t last_data_us = 0;
  bool ota_begun = false;

  mbedtls_sha256_init(&sha_ctx);

  while (1) {
    // 타임아웃 체크: OTA 진행 중이면 30초 대기, 아니면 무한 대기
    TickType_t wait = s_active ? pdMS_TO_TICKS(1000) : portMAX_DELAY;
    BaseType_t got = xQueueReceive(s_ota_queue, &evt, wait);

    // 타임아웃 체크 (OTA 진행 중 30초 무수신)
    if (!got) {
      if (s_active && last_data_us > 0) {
        int64_t elapsed_us = esp_timer_get_time() - last_data_us;
        if (elapsed_us > (int64_t)OTA_TIMEOUT_SEC * 1000000LL) {
          ESP_LOGW(TAG, "OTA timeout (%ds no data)", OTA_TIMEOUT_SEC);
          if (ota_begun) {
            esp_ota_abort(ota_handle);
            ota_begun = false;
          }
          s_active = false;
          update_status(OTA_STATE_ERROR, 0, OTA_ERR_TIMEOUT, received,
                        total_size);
        }
      }
      continue;
    }

    switch (evt.type) {

    case OTA_EVT_START: {
      // 이전 OTA 진행 중이면 먼저 정리
      if (ota_begun) {
        esp_ota_abort(ota_handle);
        ota_begun = false;
      }

      total_size = evt.start.total_size;
      memcpy(expected_sha256, evt.start.sha256, 32);
      received = 0;
      expected_seq = 0;
      last_progress = 0;

      // 대상 파티션 찾기
      target_part = esp_ota_get_next_update_partition(NULL);
      if (!target_part) {
        ESP_LOGE(TAG, "No OTA partition found");
        update_status(OTA_STATE_ERROR, 0, OTA_ERR_PARTITION, 0, total_size);
        break;
      }

      ESP_LOGI(TAG, "OTA start: size=%u, partition=%s",
               (unsigned)total_size, target_part->label);

      esp_err_t ret = esp_ota_begin(target_part, total_size, &ota_handle);
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        update_status(OTA_STATE_ERROR, 0, OTA_ERR_PARTITION, 0, total_size);
        break;
      }

      ota_begun = true;
      s_active = true;
      mbedtls_sha256_free(&sha_ctx);
      mbedtls_sha256_init(&sha_ctx);
      mbedtls_sha256_starts(&sha_ctx, 0);  // SHA-256 (not SHA-224)
      last_data_us = esp_timer_get_time();
      update_status(OTA_STATE_RECEIVING, 0, OTA_ERR_NONE, 0, total_size);
      break;
    }

    case OTA_EVT_DATA: {
      if (!s_active || !ota_begun) break;

      // 시퀀스 검사 (갭 감지)
      if (evt.data.seq != expected_seq) {
        ESP_LOGW(TAG, "Seq mismatch: expected=%u got=%u",
                 expected_seq, evt.data.seq);
        // 갭은 허용하지 않음 → 에러
        esp_ota_abort(ota_handle);
        ota_begun = false;
        s_active = false;
        update_status(OTA_STATE_ERROR, 0, OTA_ERR_INTERNAL, received,
                      total_size);
        break;
      }
      expected_seq++;

      // 플래시 기록
      esp_err_t ret =
          esp_ota_write(ota_handle, evt.data.data, evt.data.len);
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
        esp_ota_abort(ota_handle);
        ota_begun = false;
        s_active = false;
        update_status(OTA_STATE_ERROR, 0, OTA_ERR_FLASH_WRITE, received,
                      total_size);
        break;
      }

      // SHA256 업데이트
      mbedtls_sha256_update(&sha_ctx, evt.data.data, evt.data.len);

      received += evt.data.len;
      last_data_us = esp_timer_get_time();

      // 진행률 (10% 단위로 Notify)
      uint8_t pct =
          total_size > 0 ? (uint8_t)((uint64_t)received * 100 / total_size)
                         : 0;
      if (pct > 100) pct = 100;
      if (pct >= last_progress + 10 || pct == 100) {
        last_progress = pct;
        update_status(OTA_STATE_RECEIVING, pct, OTA_ERR_NONE, received,
                      total_size);
        ESP_LOGI(TAG, "OTA progress: %u%% (%u/%u)", pct, (unsigned)received,
                 (unsigned)total_size);
      }
      break;
    }

    case OTA_EVT_COMPLETE: {
      if (!s_active || !ota_begun) break;

      update_status(OTA_STATE_VERIFYING, 100, OTA_ERR_NONE, received,
                    total_size);

      // 크기 검증
      if (received != total_size) {
        ESP_LOGE(TAG, "Size mismatch: received=%u expected=%u",
                 (unsigned)received, (unsigned)total_size);
        esp_ota_abort(ota_handle);
        ota_begun = false;
        s_active = false;
        update_status(OTA_STATE_ERROR, 0, OTA_ERR_SIZE_MISMATCH, received,
                      total_size);
        break;
      }

      // SHA256 검증
      uint8_t computed_sha256[32];
      mbedtls_sha256_finish(&sha_ctx, computed_sha256);

      if (memcmp(computed_sha256, expected_sha256, 32) != 0) {
        ESP_LOGE(TAG, "SHA256 mismatch!");
        esp_ota_abort(ota_handle);
        ota_begun = false;
        s_active = false;
        update_status(OTA_STATE_ERROR, 0, OTA_ERR_SHA256_MISMATCH, received,
                      total_size);
        break;
      }

      // OTA 완료
      esp_err_t ret = esp_ota_end(ota_handle);
      ota_begun = false;
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
        s_active = false;
        update_status(OTA_STATE_ERROR, 0, OTA_ERR_INTERNAL, received,
                      total_size);
        break;
      }

      // 부팅 파티션 교체
      ret = esp_ota_set_boot_partition(target_part);
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(ret));
        s_active = false;
        update_status(OTA_STATE_ERROR, 0, OTA_ERR_PARTITION, received,
                      total_size);
        break;
      }

      ESP_LOGI(TAG, "OTA complete! Rebooting in 1s...");
      s_active = false;
      update_status(OTA_STATE_COMPLETE, 100, OTA_ERR_NONE, received,
                    total_size);

      // 1초 후 재시작 (앱이 COMPLETE 상태를 수신할 시간 확보)
      vTaskDelay(pdMS_TO_TICKS(1000));
      esp_restart();
      break;  // unreachable
    }

    case OTA_EVT_CANCEL:
    case OTA_EVT_DISCONNECT:
      if (ota_begun) {
        esp_ota_abort(ota_handle);
        ota_begun = false;
        ESP_LOGW(TAG, "OTA %s",
                 evt.type == OTA_EVT_CANCEL ? "cancelled" : "disconnect");
      }
      s_active = false;
      received = 0;
      total_size = 0;
      update_status(OTA_STATE_IDLE, 0, OTA_ERR_NONE, 0, 0);
      break;
    }
  }
}

// ============================================================
// 공개 API
// ============================================================
esp_err_t ota_update_init(void) {
  s_ota_queue = xQueueCreate(OTA_QUEUE_DEPTH, sizeof(ota_event_t));
  if (!s_ota_queue) {
    ESP_LOGE(TAG, "Failed to create OTA queue");
    return ESP_ERR_NO_MEM;
  }

  BaseType_t ret = xTaskCreate(ota_task, "ota_task", OTA_TASK_STACK_SIZE,
                               NULL, tskIDLE_PRIORITY + 1, &s_ota_task);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create OTA task");
    return ESP_ERR_NO_MEM;
  }

  memset((void *)&s_status, 0, sizeof(s_status));
  ESP_LOGI(TAG, "OTA module initialized");
  return ESP_OK;
}

void ota_update_handle_control(const uint8_t *data, uint16_t len) {
  if (!s_ota_queue || len < 1) return;

  uint8_t cmd = data[0];
  ota_event_t evt;
  memset(&evt, 0, sizeof(evt));

  switch (cmd) {
  case OTA_CMD_START:
    // {cmd(1), total_size(4), sha256[32]} = 37 bytes
    if (len < 37) {
      ESP_LOGW(TAG, "OTA START too short: %u", len);
      return;
    }
    evt.type = OTA_EVT_START;
    memcpy(&evt.start.total_size, &data[1], 4);
    memcpy(evt.start.sha256, &data[5], 32);
    ESP_LOGI(TAG, "OTA START cmd: size=%u", (unsigned)evt.start.total_size);
    break;

  case OTA_CMD_COMPLETE:
    evt.type = OTA_EVT_COMPLETE;
    ESP_LOGI(TAG, "OTA COMPLETE cmd");
    break;

  case OTA_CMD_CANCEL:
    evt.type = OTA_EVT_CANCEL;
    ESP_LOGI(TAG, "OTA CANCEL cmd");
    break;

  default:
    ESP_LOGW(TAG, "Unknown OTA cmd: 0x%02X", cmd);
    return;
  }

  if (xQueueSend(s_ota_queue, &evt, pdMS_TO_TICKS(100)) != pdPASS) {
    ESP_LOGW(TAG, "OTA queue full, event dropped");
  }
}

void ota_update_handle_data(const uint8_t *data, uint16_t len) {
  if (!s_ota_queue || !s_active) return;
  if (len < 3) return;  // seq(2) + data(1+)

  ota_event_t evt;
  evt.type = OTA_EVT_DATA;
  memcpy(&evt.data.seq, data, 2);
  evt.data.len = len - 2;
  if (evt.data.len > OTA_MAX_CHUNK_SIZE) {
    evt.data.len = OTA_MAX_CHUNK_SIZE;
  }
  memcpy(evt.data.data, &data[2], evt.data.len);

  if (xQueueSend(s_ota_queue, &evt, 0) != pdPASS) {
    // 큐 만원 → 드롭 (시퀀스 갭으로 OTA 태스크가 감지)
    ESP_LOGW(TAG, "OTA data queue full, chunk dropped (seq=%u)", evt.data.seq);
  }
}

bool ota_update_is_active(void) { return s_active; }

void ota_update_get_status(ota_status_pkt_t *out) {
  if (out) {
    memcpy(out, (const void *)&s_status, sizeof(ota_status_pkt_t));
  }
}

void ota_update_on_disconnect(void) {
  if (!s_active || !s_ota_queue) return;
  ota_event_t evt = {.type = OTA_EVT_DISCONNECT};
  xQueueSend(s_ota_queue, &evt, pdMS_TO_TICKS(100));
}

// Notify 콜백 등록 (ble_server.c에서 호출)
void ota_update_set_notify_cb(void (*cb)(const ota_status_pkt_t *status)) {
  s_notify_cb = cb;
}
