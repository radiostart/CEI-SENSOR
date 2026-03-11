/**
 * @file spiffs_logger.c
 * @brief SPIFFS 기반 센서 데이터 로거 구현
 *
 * 링 버퍼 구조:
 * - /spiffs/bake.log : 이진 레코드 파일 (25920 × 16 bytes = 414720 bytes)
 * - 링 버퍼 인덱스는 NVS에 저장 (write_idx, record_count, unsent_count)
 *
 * 파일 핸들 캐싱:
 * - 초기화 후 "r+b"로 1회 open, 이후 모든 I/O에 재사용
 * - SPIFFS lookup table 스캔(fopen)이 ~2-5초 걸리므로 반복 호출 방지
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
#define NVS_NAMESPACE    "mellowair"
#define NVS_KEY_WRITE    "log_widx"
#define NVS_KEY_COUNT    "log_cnt"
#define NVS_KEY_UNSENT   "log_unsent"

// 파일 크기 (바이트)
#define LOG_FILE_SIZE (APP_LOG_MAX_RECORDS * (uint32_t)sizeof(log_record_t))

// 청크 마킹 단위 (16레코드 = 256바이트 = SPIFFS 페이지 크기)
#define MARK_CHUNK 16

// 인메모리 링 버퍼 상태
static uint32_t s_write_idx = 0;    // 다음 쓰기 위치 (0..MAX-1)
static uint32_t s_record_count = 0; // 저장된 총 레코드 수
static uint32_t s_unsent_count = 0; // 미전송 레코드 수 (인메모리 캐시)
static bool s_initialized = false;

// 캐시된 파일 핸들 (fopen 비용 제거)
static FILE *s_log_fp = NULL;

// ============================================================
// 파일 핸들 캐시 헬퍼
// ============================================================
static FILE *log_file_get(void) {
  if (s_log_fp == NULL) {
    s_log_fp = fopen(LOG_FILE_PATH, "r+b");
  }
  return s_log_fp;
}

static void log_file_flush(void) {
  if (s_log_fp) fflush(s_log_fp);
}

static void log_file_close(void) {
  if (s_log_fp) {
    fclose(s_log_fp);
    s_log_fp = NULL;
  }
}

// ============================================================
// NVS 헬퍼
// ============================================================
static void load_state_from_nvs(void) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
  nvs_get_u32(h, NVS_KEY_WRITE, &s_write_idx);
  nvs_get_u32(h, NVS_KEY_COUNT, &s_record_count);
  nvs_get_u32(h, NVS_KEY_UNSENT, &s_unsent_count);
  nvs_close(h);

  if (s_write_idx >= APP_LOG_MAX_RECORDS) s_write_idx = 0;
  if (s_record_count > APP_LOG_MAX_RECORDS) s_record_count = APP_LOG_MAX_RECORDS;
  if (s_unsent_count > s_record_count) s_unsent_count = s_record_count;
}

static void save_state_to_nvs(void) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u32(h, NVS_KEY_WRITE, s_write_idx);
  nvs_set_u32(h, NVS_KEY_COUNT, s_record_count);
  nvs_set_u32(h, NVS_KEY_UNSENT, s_unsent_count);
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

  // 파일 핸들 캐시 (이후 fopen 불필요)
  s_log_fp = fopen(LOG_FILE_PATH, "r+b");
  if (s_log_fp == NULL) {
    ESP_LOGE(TAG, "Failed to open log file for caching");
    return ESP_FAIL;
  }

  s_initialized = true;

  ESP_LOGI(TAG, "Logger ready: write_idx=%u, count=%u, unsent=%u",
           (unsigned)s_write_idx, (unsigned)s_record_count, (unsigned)s_unsent_count);

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

  FILE *f = log_file_get();
  if (f == NULL) return ESP_FAIL;

  // 링 버퍼가 꽉 찬 경우: 덮어쓸 레코드가 unsent였으면 카운터 감소
  if (s_record_count >= APP_LOG_MAX_RECORDS) {
    log_record_t old_rec;
    long old_off = (long)(s_write_idx * sizeof(log_record_t));
    fseek(f, old_off, SEEK_SET);
    if (fread(&old_rec, sizeof(log_record_t), 1, f) == 1 && !old_rec.sent) {
      if (s_unsent_count > 0) s_unsent_count--;
    }
  }

  long offset = (long)(s_write_idx * sizeof(log_record_t));
  if (fseek(f, offset, SEEK_SET) != 0) {
    return ESP_FAIL;
  }
  if (fwrite(&rec, sizeof(log_record_t), 1, f) != 1) {
    return ESP_FAIL;
  }
  log_file_flush();

  // 새 레코드는 항상 unsent
  s_unsent_count++;

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
  return s_unsent_count;
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

  FILE *f = log_file_get();
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
  return ret;
}

esp_err_t spiffs_logger_read_unsent_batch(uint32_t ring_offset,
                                          log_record_t *records,
                                          uint32_t *log_indices,
                                          uint32_t max_count,
                                          uint32_t *out_count,
                                          uint32_t *next_offset) {
  if (!s_initialized || records == NULL || log_indices == NULL ||
      out_count == NULL || next_offset == NULL)
    return ESP_ERR_INVALID_ARG;

  *out_count = 0;
  *next_offset = ring_offset;

  if (ring_offset >= s_record_count) return ESP_OK;

  FILE *f = log_file_get();
  if (f == NULL) return ESP_FAIL;

  uint32_t start = (s_write_idx + APP_LOG_MAX_RECORDS - s_record_count) % APP_LOG_MAX_RECORDS;
  uint32_t collected = 0;

  // 청크 단위 순차 읽기 (16레코드 = 256바이트, SPIFFS 페이지 크기 일치)
  // 개별 fseek+fread 대비 ~8-10배 빠름
  log_record_t chunk[MARK_CHUNK];

  for (uint32_t i = ring_offset; i < s_record_count && collected < max_count; ) {
    uint32_t remaining_records = s_record_count - i;
    uint32_t chunk_n = remaining_records < MARK_CHUNK ? remaining_records : MARK_CHUNK;

    uint32_t first_idx = (start + i) % APP_LOG_MAX_RECORDS;
    bool contiguous = (first_idx + chunk_n <= APP_LOG_MAX_RECORDS);

    if (contiguous) {
      long off = (long)(first_idx * sizeof(log_record_t));
      fseek(f, off, SEEK_SET);
      size_t rd = fread(chunk, sizeof(log_record_t), chunk_n, f);
      for (uint32_t j = 0; j < rd && collected < max_count; j++) {
        if (!chunk[j].sent) {
          records[collected] = chunk[j];
          log_indices[collected] = (first_idx + j) % APP_LOG_MAX_RECORDS;
          collected++;
        }
      }
      *next_offset = i + rd;
      i += rd;
    } else {
      // 링 버퍼 경계 교차: 개별 읽기 (드문 경우)
      for (uint32_t j = 0; j < chunk_n && collected < max_count; j++) {
        uint32_t idx = (start + i + j) % APP_LOG_MAX_RECORDS;
        fseek(f, (long)(idx * sizeof(log_record_t)), SEEK_SET);
        log_record_t rec;
        if (fread(&rec, sizeof(log_record_t), 1, f) != 1) break;
        if (!rec.sent) {
          records[collected] = rec;
          log_indices[collected] = idx;
          collected++;
        }
      }
      *next_offset = i + chunk_n;
      i += chunk_n;
    }

    // Watchdog 방지: 매 128레코드마다 yield
    if ((i - ring_offset) >= 128 && ((i - ring_offset) & 127) < MARK_CHUNK) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  *out_count = collected;
  return ESP_OK;
}

esp_err_t spiffs_logger_mark_sent(uint32_t log_idx) {
  if (!s_initialized || log_idx >= APP_LOG_MAX_RECORDS)
    return ESP_ERR_INVALID_ARG;

  FILE *f = log_file_get();
  if (f == NULL) return ESP_FAIL;

  long offset = (long)(log_idx * sizeof(log_record_t)) +
                (long)offsetof(log_record_t, sent);
  if (fseek(f, offset, SEEK_SET) != 0) {
    return ESP_FAIL;
  }

  uint8_t sent_val = 1;
  bool ok = (fwrite(&sent_val, 1, 1, f) == 1);
  log_file_flush();
  if (ok && s_unsent_count > 0) s_unsent_count--;
  return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t spiffs_logger_mark_sent_batch(const uint32_t *log_indices, uint32_t count) {
  if (!s_initialized || log_indices == NULL || count == 0)
    return ESP_ERR_INVALID_ARG;

  FILE *f = log_file_get();
  if (f == NULL) return ESP_FAIL;

  uint8_t sent_val = 1;
  uint32_t marked = 0;
  for (uint32_t i = 0; i < count; i++) {
    if (log_indices[i] >= APP_LOG_MAX_RECORDS) continue;
    long offset = (long)(log_indices[i] * sizeof(log_record_t)) +
                  (long)offsetof(log_record_t, sent);
    fseek(f, offset, SEEK_SET);
    fwrite(&sent_val, 1, 1, f);
    marked++;
  }
  log_file_flush();
  if (s_unsent_count >= marked) s_unsent_count -= marked;
  else s_unsent_count = 0;
  return ESP_OK;
}

esp_err_t spiffs_logger_mark_range_sent(uint32_t from_offset, uint32_t to_offset) {
  if (!s_initialized || s_record_count == 0) return ESP_ERR_INVALID_STATE;
  if (from_offset >= to_offset) return ESP_OK;
  if (to_offset > s_record_count) to_offset = s_record_count;

  FILE *f = log_file_get();
  if (f == NULL) return ESP_FAIL;

  uint32_t start = (s_write_idx + APP_LOG_MAX_RECORDS - s_record_count) % APP_LOG_MAX_RECORDS;
  uint32_t marked = 0;

  log_record_t chunk[MARK_CHUNK];

  for (uint32_t i = from_offset; i < to_offset; ) {
    uint32_t chunk_n = to_offset - i;
    if (chunk_n > MARK_CHUNK) chunk_n = MARK_CHUNK;

    uint32_t first_idx = (start + i) % APP_LOG_MAX_RECORDS;
    bool contiguous = (first_idx + chunk_n <= APP_LOG_MAX_RECORDS);

    if (contiguous) {
      long off = (long)(first_idx * sizeof(log_record_t));
      fseek(f, off, SEEK_SET);
      size_t rd = fread(chunk, sizeof(log_record_t), chunk_n, f);
      bool modified = false;
      for (uint32_t j = 0; j < rd; j++) {
        if (!chunk[j].sent) {
          chunk[j].sent = 1;
          modified = true;
          marked++;
        }
      }
      if (modified) {
        fseek(f, off, SEEK_SET);
        fwrite(chunk, sizeof(log_record_t), rd, f);
      }
    } else {
      for (uint32_t j = 0; j < chunk_n; j++) {
        uint32_t idx = (start + i + j) % APP_LOG_MAX_RECORDS;
        long off = (long)(idx * sizeof(log_record_t));
        fseek(f, off, SEEK_SET);
        log_record_t rec;
        if (fread(&rec, sizeof(log_record_t), 1, f) == 1 && !rec.sent) {
          rec.sent = 1;
          fseek(f, off, SEEK_SET);
          fwrite(&rec, sizeof(log_record_t), 1, f);
          marked++;
        }
      }
    }
    i += chunk_n;
    if ((i & 63) == 0) vTaskDelay(pdMS_TO_TICKS(1));
  }

  log_file_flush();
  if (s_unsent_count >= marked) s_unsent_count -= marked;
  else s_unsent_count = 0;
  save_state_to_nvs();
  ESP_LOGI(TAG, "mark_range_sent: %u marked (offset %u-%u), unsent=%u",
           (unsigned)marked, (unsigned)from_offset, (unsigned)to_offset,
           (unsigned)s_unsent_count);
  return ESP_OK;
}

esp_err_t spiffs_logger_reset_sent_flags(void) {
  if (!s_initialized || s_record_count == 0) return ESP_ERR_INVALID_STATE;

  FILE *f = log_file_get();
  if (f == NULL) return ESP_FAIL;

  uint32_t start = (s_write_idx + APP_LOG_MAX_RECORDS - s_record_count) % APP_LOG_MAX_RECORDS;

  log_record_t chunk[MARK_CHUNK];

  for (uint32_t i = 0; i < s_record_count; ) {
    uint32_t chunk_n = s_record_count - i;
    if (chunk_n > MARK_CHUNK) chunk_n = MARK_CHUNK;

    uint32_t first_idx = (start + i) % APP_LOG_MAX_RECORDS;
    bool contiguous = (first_idx + chunk_n <= APP_LOG_MAX_RECORDS);

    if (contiguous) {
      long off = (long)(first_idx * sizeof(log_record_t));
      fseek(f, off, SEEK_SET);
      size_t rd = fread(chunk, sizeof(log_record_t), chunk_n, f);
      bool modified = false;
      for (uint32_t j = 0; j < rd; j++) {
        if (chunk[j].sent) {
          chunk[j].sent = 0;
          modified = true;
        }
      }
      if (modified) {
        fseek(f, off, SEEK_SET);
        fwrite(chunk, sizeof(log_record_t), rd, f);
      }
    } else {
      for (uint32_t j = 0; j < chunk_n; j++) {
        uint32_t idx = (start + i + j) % APP_LOG_MAX_RECORDS;
        long off = (long)(idx * sizeof(log_record_t));
        fseek(f, off, SEEK_SET);
        log_record_t rec;
        if (fread(&rec, sizeof(log_record_t), 1, f) == 1 && rec.sent) {
          rec.sent = 0;
          fseek(f, off, SEEK_SET);
          fwrite(&rec, sizeof(log_record_t), 1, f);
        }
      }
    }
    i += chunk_n;
    if ((i & 63) == 0) vTaskDelay(pdMS_TO_TICKS(1));
  }

  log_file_flush();
  s_unsent_count = s_record_count;
  save_state_to_nvs();
  ESP_LOGW(TAG, "Reset all %u sent flags to 0", (unsigned)s_record_count);
  return ESP_OK;
}

esp_err_t spiffs_logger_clear_all(void) {
  if (!s_initialized) return ESP_ERR_INVALID_STATE;

  // 파일 핸들 닫기 → 삭제 → 재생성 → 재오픈
  log_file_close();

  remove(LOG_FILE_PATH);
  esp_err_t ret = ensure_log_file();
  if (ret != ESP_OK) return ret;

  s_log_fp = fopen(LOG_FILE_PATH, "r+b");
  if (s_log_fp == NULL) return ESP_FAIL;

  s_write_idx = 0;
  s_record_count = 0;
  s_unsent_count = 0;
  save_state_to_nvs();

  ESP_LOGW(TAG, "All log data cleared");
  return ESP_OK;
}
