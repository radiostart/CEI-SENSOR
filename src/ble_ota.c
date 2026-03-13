/**
 * @file ble_ota.c
 * @brief BLE OTA 펌웨어 업데이트 구현
 *
 * BLE 콜백 → 링버퍼 복사 (논블로킹)
 * 별도 FreeRTOS 태스크가 링버퍼에서 읽어 esp_ota_write (flash write)
 * → BLE 수신이 flash erase에 의해 블로킹되지 않음
 */

#include "ble_ota.h"
#include "battery_monitor.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "BLE_OTA";

// OTA 상태
static ota_state_t s_state = OTA_STATE_IDLE;
static ota_error_t s_error = OTA_ERR_NONE;
static bool s_status_changed = false;

// esp_ota 핸들
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_update_partition = NULL;

// 진행률 추적
static uint32_t s_total_size = 0;
static uint32_t s_received_size = 0;  // BLE에서 수신한 총 바이트
static uint32_t s_written_size = 0;   // flash에 기록한 총 바이트
static uint8_t s_last_progress = 0;

// 타임아웃 (15초간 데이터 미수신 시 자동 취소)
#define OTA_TIMEOUT_US  (15 * 1000000LL)
static int64_t s_last_activity_us = 0;

// 링버퍼 + 쓰기 태스크
#define OTA_RINGBUF_SIZE  (16 * 1024)  // 16KB 링버퍼
#define OTA_TASK_STACK    4096
#define OTA_TASK_PRIORITY 5

static RingbufHandle_t s_ringbuf = NULL;
static TaskHandle_t s_write_task = NULL;

static void set_state(ota_state_t state, ota_error_t error) {
    s_state = state;
    s_error = error;
    s_status_changed = true;
    s_last_activity_us = esp_timer_get_time();
}

// ============================================================
// Flash 쓰기 태스크 (BLE 콜백과 별도 스레드)
// ============================================================
static void ota_write_task(void *arg) {
    ESP_LOGI(TAG, "OTA write task started");

    while (true) {
        size_t item_size = 0;
        void *item = xRingbufferReceive(s_ringbuf, &item_size, pdMS_TO_TICKS(1000));

        if (item == NULL) {
            // 타임아웃: OTA가 끝났으면 태스크 종료
            if (s_state != OTA_STATE_READY && s_state != OTA_STATE_RECEIVING) {
                break;
            }
            continue;
        }

        esp_err_t ret = esp_ota_write(s_ota_handle, item, item_size);
        vRingbufferReturnItem(s_ringbuf, item);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed at %u: %s",
                     (unsigned)s_written_size, esp_err_to_name(ret));
            esp_ota_abort(s_ota_handle);
            set_state(OTA_STATE_ERROR, OTA_ERR_WRITE_FAIL);
            break;
        }

        s_written_size += item_size;

        // 진행률 1% 단위로 상태 알림 (written 기준)
        uint8_t progress = (uint8_t)(s_total_size > 0
            ? ((uint64_t)s_written_size * 100 / s_total_size) : 0);
        if (progress != s_last_progress) {
            s_last_progress = progress;
            s_status_changed = true;
            if (progress % 10 == 0) {
                ESP_LOGI(TAG, "OTA progress: %u%% (%u/%u)",
                         progress, (unsigned)s_written_size, (unsigned)s_total_size);
            }
        }
    }

    ESP_LOGI(TAG, "OTA write task exiting (written=%u)", (unsigned)s_written_size);
    s_write_task = NULL;
    vTaskDelete(NULL);
}

static void start_write_task(void) {
    if (s_ringbuf == NULL) {
        s_ringbuf = xRingbufferCreate(OTA_RINGBUF_SIZE, RINGBUF_TYPE_NOSPLIT);
        if (s_ringbuf == NULL) {
            ESP_LOGE(TAG, "Failed to create ring buffer");
            set_state(OTA_STATE_ERROR, OTA_ERR_BEGIN_FAIL);
            return;
        }
    }

    if (s_write_task == NULL) {
        BaseType_t ret = xTaskCreate(
            ota_write_task, "ota_write", OTA_TASK_STACK,
            NULL, OTA_TASK_PRIORITY, &s_write_task);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create OTA write task");
            set_state(OTA_STATE_ERROR, OTA_ERR_BEGIN_FAIL);
        }
    }
}

static void stop_write_task(void) {
    // 태스크가 링버퍼를 모두 소진하고 종료할 때까지 대기
    if (s_write_task != NULL) {
        // 최대 10초 대기
        for (int i = 0; i < 100 && s_write_task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    if (s_ringbuf != NULL) {
        vRingbufferDelete(s_ringbuf);
        s_ringbuf = NULL;
    }
}

// ============================================================
// BLE 콜백 핸들러 (논블로킹)
// ============================================================

void ble_ota_handle_control(const uint8_t *data, uint16_t len) {
    if (len < 1) return;
    uint8_t cmd = data[0];

    switch (cmd) {
    case OTA_CMD_START: {
        if (len < 5) {
            ESP_LOGW(TAG, "START packet too short: %d", len);
            return;
        }
        if (s_state != OTA_STATE_IDLE) {
            ESP_LOGW(TAG, "START received in non-idle state: %d", s_state);
            if (s_state == OTA_STATE_RECEIVING || s_state == OTA_STATE_READY) {
                esp_ota_abort(s_ota_handle);
            }
            stop_write_task(); // 이전 태스크/링버퍼 정리 (어떤 상태든)
        }

        uint32_t total_size;
        memcpy(&total_size, &data[1], 4);

        ESP_LOGI(TAG, "OTA START: size=%u bytes", (unsigned)total_size);

        // 배터리 잔량 부족 시 OTA 거부 (USB 연결 시 제외)
        if (!battery_is_usb_connected()) {
            battery_monitor_init();
            int bat_pct = battery_get_percentage();
            battery_monitor_deinit();
            if (bat_pct < 20) {
                ESP_LOGW(TAG, "Battery too low for OTA: %d%%", bat_pct);
                set_state(OTA_STATE_ERROR, OTA_ERR_ABORTED);
                return;
            }
        }

        s_update_partition = esp_ota_get_next_update_partition(NULL);
        if (s_update_partition && total_size > s_update_partition->size) {
            ESP_LOGE(TAG, "Firmware too large: %u > partition %u",
                     (unsigned)total_size, (unsigned)s_update_partition->size);
            set_state(OTA_STATE_ERROR, OTA_ERR_NO_PARTITION);
            return;
        }
        if (!s_update_partition) {
            ESP_LOGE(TAG, "No OTA partition found");
            set_state(OTA_STATE_ERROR, OTA_ERR_NO_PARTITION);
            return;
        }

        ESP_LOGI(TAG, "Writing to partition '%s' at offset 0x%x",
                 s_update_partition->label, (unsigned)s_update_partition->address);

        esp_err_t ret = esp_ota_begin(s_update_partition, total_size, &s_ota_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
            set_state(OTA_STATE_ERROR, OTA_ERR_BEGIN_FAIL);
            return;
        }

        s_total_size = total_size;
        s_received_size = 0;
        s_written_size = 0;
        s_last_progress = 0;

        start_write_task();
        if (s_state == OTA_STATE_ERROR) return; // 태스크 생성 실패

        set_state(OTA_STATE_READY, OTA_ERR_NONE);
        break;
    }

    case OTA_CMD_COMMIT: {
        if (s_state != OTA_STATE_RECEIVING && s_state != OTA_STATE_READY) {
            ESP_LOGW(TAG, "COMMIT in invalid state: %d", s_state);
            return;
        }

        ESP_LOGI(TAG, "OTA COMMIT: received=%u, written=%u/%u bytes",
                 (unsigned)s_received_size, (unsigned)s_written_size, (unsigned)s_total_size);

        set_state(OTA_STATE_VERIFYING, OTA_ERR_NONE);

        // 쓰기 태스크가 링버퍼를 모두 소진할 때까지 대기
        stop_write_task();

        if (s_state == OTA_STATE_ERROR) return; // 쓰기 중 에러 발생

        esp_err_t ret = esp_ota_end(s_ota_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
            set_state(OTA_STATE_ERROR, OTA_ERR_VERIFY_FAIL);
            return;
        }

        ret = esp_ota_set_boot_partition(s_update_partition);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
            set_state(OTA_STATE_ERROR, OTA_ERR_SET_BOOT);
            return;
        }

        ESP_LOGI(TAG, "OTA SUCCESS — reboot pending");
        set_state(OTA_STATE_SUCCESS, OTA_ERR_NONE);
        break;
    }

    case OTA_CMD_ABORT: {
        ESP_LOGW(TAG, "OTA ABORTED by app");
        if (s_state == OTA_STATE_RECEIVING || s_state == OTA_STATE_READY) {
            set_state(OTA_STATE_IDLE, OTA_ERR_ABORTED);
            stop_write_task();
            esp_ota_abort(s_ota_handle);
        } else {
            set_state(OTA_STATE_IDLE, OTA_ERR_ABORTED);
        }
        break;
    }

    default:
        ESP_LOGW(TAG, "Unknown OTA cmd: 0x%02x", cmd);
        break;
    }
}

void ble_ota_handle_data(const uint8_t *data, uint16_t len) {
    if (s_state != OTA_STATE_READY && s_state != OTA_STATE_RECEIVING) {
        return;
    }

    if (s_ringbuf == NULL) {
        ESP_LOGE(TAG, "Ring buffer not initialized");
        return;
    }

    // 링버퍼에 복사 (논블로킹 — BLE 콜백을 블로킹하지 않음)
    // 최대 50ms 대기 (링버퍼가 가득 찬 경우 쓰기 태스크가 소진할 때까지)
    BaseType_t ret = xRingbufferSend(s_ringbuf, data, len, pdMS_TO_TICKS(50));
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Ring buffer full at %u bytes — aborting",
                 (unsigned)s_received_size);
        set_state(OTA_STATE_ERROR, OTA_ERR_WRITE_FAIL);
        stop_write_task();
        esp_ota_abort(s_ota_handle);
        return;
    }

    s_received_size += len;
    s_last_activity_us = esp_timer_get_time();
    if (s_state != OTA_STATE_RECEIVING) {
        s_state = OTA_STATE_RECEIVING;
    }
}

bool ble_ota_poll_status(ble_ota_status_pkt_t *status_out) {
    // 타임아웃 체크: 15초간 데이터 미수신 시 자동 취소
    if (ble_ota_is_active() && s_last_activity_us > 0) {
        int64_t elapsed = esp_timer_get_time() - s_last_activity_us;
        if (elapsed > OTA_TIMEOUT_US) {
            ESP_LOGW(TAG, "OTA timeout (%lld sec), aborting", elapsed / 1000000LL);
            set_state(OTA_STATE_ERROR, OTA_ERR_ABORTED);
            stop_write_task();
            esp_ota_abort(s_ota_handle);
        }
    }

    if (!s_status_changed) return false;
    s_status_changed = false;

    if (status_out) {
        status_out->state = (uint8_t)s_state;
        status_out->progress_pct = s_last_progress;
        status_out->error_code = (uint8_t)s_error;
        status_out->_pad = 0;
    }
    return true;
}

bool ble_ota_is_active(void) {
    return s_state == OTA_STATE_READY
        || s_state == OTA_STATE_RECEIVING
        || s_state == OTA_STATE_VERIFYING;
}

bool ble_ota_needs_reboot(void) {
    return s_state == OTA_STATE_SUCCESS;
}

void ble_ota_reboot(void) {
    ESP_LOGI(TAG, "Rebooting to new firmware in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

void ble_ota_reset(void) {
    if (s_state == OTA_STATE_RECEIVING || s_state == OTA_STATE_READY) {
        set_state(OTA_STATE_IDLE, OTA_ERR_NONE);
        stop_write_task();
        esp_ota_abort(s_ota_handle);
        ESP_LOGW(TAG, "OTA aborted due to reset (disconnect)");
    }
    s_ota_handle = 0;
    s_update_partition = NULL;
    s_state = OTA_STATE_IDLE;
    s_error = OTA_ERR_NONE;
    s_status_changed = false;
    s_total_size = 0;
    s_received_size = 0;
    s_written_size = 0;
    s_last_progress = 0;
    s_last_activity_us = 0;
}
