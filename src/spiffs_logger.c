/**
 * @file spiffs_logger.c
 * @brief SPIFFS 기반 센서 데이터 로거 구현
 *
 * 링 버퍼 구조:
 * - /spiffs/bake.log : 이진 레코드 파일 (25920 × 16 bytes = 414720 bytes)
 * - 링 버퍼 인덱스는 NVS에 저장 (write_idx, record_count)
 */

#include "spiffs_logger.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "SPIFFS_LOG";

#define LOG_FILE_PATH    "/spiffs/bake.log"
#define NVS_NAMESPACE    "baketrack"
#define NVS_KEY_WRITE    "log_widx"
#define NVS_KEY_COUNT    "log_cnt"

// 파일 크기 (바이트)
#define LOG_FILE_SIZE (APP_LOG_MAX_RECORDS * (uint32_t)sizeof(log_record_t))

// 인메모리 링 버퍼 상태
static uint32_t s_write_idx = 0;    // 다음 쓰기 위치 (0..MAX-1)
static uint32_t s_record_count = 0; // 저장된 총 레코드 수
static bool s_initialized = false;

// ============================================================
// NVS 헬퍼
// ============================================================
static void load_state_from_nvs(void) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
  nvs_get_u32(h, NVS_KEY_WRITE, &s_write_idx);
  nvs_get_u32(h, NVS_KEY_COUNT, &s_record_count);
  nvs_close(h);

  if (s_write_idx >= APP_LOG_MAX_RECORDS) s_write_idx = 0;
  if (s_record_count > APP_LOG_MAX_RECORDS) s_record_count = APP_LOG_MAX_RECORDS;
}

static void save_state_to_nvs(void) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u32(h, NVS_KEY_WRITE, s_write_idx);
  nvs_set_u32(h, NVS_KEY_COUNT, s_record_count);
  nvs_commit(h);
  nvs_close(h);
}

// ============================================================
// 파일 초기화 (파일 없으면 0으로 채운 파일 생성)
// ============================================================
static esp_err_t ensure_log_file(void) {
  FILE *f = fopen(LOG_FILE_PATH, "rb");
  if (f != NULL) {
    fclose(f);
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Creating log file (%u bytes)...", (unsigned)LOG_FILE_SIZE);
  f = fopen(LOG_FILE_PATH, "wb");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to create log file");
    return ESP_FAIL;
  }

  // 0으로 초기화 (청크 단위, 워치독 방지를 위해 주기적 yield)
  uint8_t zero_chunk[256] = {0};
  uint32_t remaining = LOG_FILE_SIZE;
  uint32_t yield_cnt = 0;
  while (remaining > 0) {
    uint32_t to_write = remaining < sizeof(zero_chunk) ? remaining : sizeof(zero_chunk);
    if (fwrite(zero_chunk, 1, to_write, f) != to_write) {
      fclose(f);
      return ESP_FAIL;
    }
    remaining -= to_write;
    // 64회(16KB)마다 yield → 워치독 리셋
    if (++yield_cnt >= 64) {
      vTaskDelay(pdMS_TO_TICKS(1));
      yield_cnt = 0;
    }
  }
  fclose(f);
  ESP_LOGI(TAG, "Log file created");
  return ESP_OK;
}

// ============================================================
// API 구현
// ============================================================
esp_err_t spiffs_logger_init(void) {
  esp_vfs_spiffs_conf_t conf = {
      .base_path = APP_SPIFFS_BASE_PATH,
      .partition_label = APP_SPIFFS_PARTITION,
      .max_files = 4,
      .format_if_mount_failed = true,
  };

  esp_err_t ret = esp_vfs_spiffs_register(&conf);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    return ret;
  }

  load_state_from_nvs();
  ret = ensure_log_file();
  if (ret != ESP_OK) return ret;

  s_initialized = true;
  ESP_LOGI(TAG, "Logger ready: write_idx=%u, count=%u",
           (unsigned)s_write_idx, (unsigned)s_record_count);
  return ESP_OK;
}

esp_err_t spiffs_logger_append(float temp, float humidity, uint32_t timestamp) {
  if (!s_initialized) return ESP_ERR_INVALID_STATE;

  log_record_t rec = {
      .timestamp = timestamp,
      .temp = temp,
      .humidity = humidity,
      .sent = 0,
      ._pad = {0, 0, 0},
  };

  FILE *f = fopen(LOG_FILE_PATH, "r+b");
  if (f == NULL) return ESP_FAIL;

  long offset = (long)(s_write_idx * sizeof(log_record_t));
  if (fseek(f, offset, SEEK_SET) != 0) {
    fclose(f);
    return ESP_FAIL;
  }
  if (fwrite(&rec, sizeof(log_record_t), 1, f) != 1) {
    fclose(f);
    return ESP_FAIL;
  }
  fclose(f);

  // 링 버퍼 인덱스 업데이트
  s_write_idx = (s_write_idx + 1) % APP_LOG_MAX_RECORDS;
  if (s_record_count < APP_LOG_MAX_RECORDS) s_record_count++;

  // NVS에 주기적으로 저장 (매 10회)
  static uint8_t save_counter = 0;
  if (++save_counter >= 10) {
    save_state_to_nvs();
    save_counter = 0;
  }
  return ESP_OK;
}

uint32_t spiffs_logger_unsent_count(void) {
  if (!s_initialized) return 0;

  FILE *f = fopen(LOG_FILE_PATH, "rb");
  if (f == NULL) return 0;

  uint32_t unsent = 0;
  log_record_t rec;
  uint32_t start = (s_write_idx + APP_LOG_MAX_RECORDS - s_record_count) % APP_LOG_MAX_RECORDS;

  for (uint32_t i = 0; i < s_record_count; i++) {
    uint32_t idx = (start + i) % APP_LOG_MAX_RECORDS;
    fseek(f, (long)(idx * sizeof(log_record_t)), SEEK_SET);
    if (fread(&rec, sizeof(log_record_t), 1, f) == 1 && !rec.sent) {
      unsent++;
    }
  }
  fclose(f);
  return unsent;
}

uint32_t spiffs_logger_total_count(void) {
  return s_record_count;
}

void spiffs_logger_get_usage(size_t *used, size_t *total) {
  if (used) *used = 0;
  if (total) *total = 0;
  esp_spiffs_info(APP_SPIFFS_PARTITION, total, used);
}

esp_err_t spiffs_logger_read_unsent(uint32_t seq, log_record_t *record,
                                    uint32_t *log_idx) {
  if (!s_initialized || record == NULL || log_idx == NULL)
    return ESP_ERR_INVALID_ARG;

  FILE *f = fopen(LOG_FILE_PATH, "rb");
  if (f == NULL) return ESP_FAIL;

  uint32_t start = (s_write_idx + APP_LOG_MAX_RECORDS - s_record_count) % APP_LOG_MAX_RECORDS;
  uint32_t found = 0;
  esp_err_t ret = ESP_ERR_NOT_FOUND;

  for (uint32_t i = 0; i < s_record_count; i++) {
    uint32_t idx = (start + i) % APP_LOG_MAX_RECORDS;
    fseek(f, (long)(idx * sizeof(log_record_t)), SEEK_SET);
    log_record_t rec;
    if (fread(&rec, sizeof(log_record_t), 1, f) != 1) break;

    if (!rec.sent) {
      if (found == seq) {
        *record = rec;
        *log_idx = idx;
        ret = ESP_OK;
        break;
      }
      found++;
    }
  }
  fclose(f);
  return ret;
}

esp_err_t spiffs_logger_mark_sent(uint32_t log_idx) {
  if (!s_initialized || log_idx >= APP_LOG_MAX_RECORDS)
    return ESP_ERR_INVALID_ARG;

  FILE *f = fopen(LOG_FILE_PATH, "r+b");
  if (f == NULL) return ESP_FAIL;

  long offset = (long)(log_idx * sizeof(log_record_t)) +
                (long)offsetof(log_record_t, sent);
  if (fseek(f, offset, SEEK_SET) != 0) {
    fclose(f);
    return ESP_FAIL;
  }

  uint8_t sent = 1;
  bool ok = (fwrite(&sent, 1, 1, f) == 1);
  fclose(f);
  return ok ? ESP_OK : ESP_FAIL;
}
