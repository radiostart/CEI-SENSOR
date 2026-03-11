/**
 * @file ble_ota.c
 * @brief BLE OTA 펌웨어 업데이트 구현
 *
 * esp_ota_ops API를 사용하여 OTA 파티션에 펌웨어 기록.
 * BLE 콜백에서 호출되는 핸들러와 메인 루프에서 폴링하는 상태 관리.
 */

#include "ble_ota.h"
#include "battery_monitor.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
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
static uint32_t s_received_size = 0;
static uint8_t s_last_progress = 0;

static void set_state(ota_state_t state, ota_error_t error) {
    s_state = state;
    s_error = error;
    s_status_changed = true;
}

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
            // 이전 OTA가 진행 중이면 중단
            if (s_state == OTA_STATE_RECEIVING || s_state == OTA_STATE_READY) {
                esp_ota_abort(s_ota_handle);
            }
        }

        uint32_t total_size;
        memcpy(&total_size, &data[1], 4);  // little-endian

        ESP_LOGI(TAG, "OTA START: size=%u bytes", (unsigned)total_size);

        // 배터리 잔량 부족 시 OTA 거부 (USB 연결 시 제외)
        if (!battery_is_usb_connected()) {
            int bat_pct = battery_get_percentage();
            if (bat_pct < 20) {
                ESP_LOGW(TAG, "Battery too low for OTA: %d%%", bat_pct);
                set_state(OTA_STATE_ERROR, OTA_ERR_ABORTED);
                return;
            }
        }

        s_update_partition = esp_ota_get_next_update_partition(NULL);
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
        s_last_progress = 0;
        set_state(OTA_STATE_READY, OTA_ERR_NONE);
        break;
    }

    case OTA_CMD_COMMIT: {
        if (s_state != OTA_STATE_RECEIVING && s_state != OTA_STATE_READY) {
            ESP_LOGW(TAG, "COMMIT in invalid state: %d", s_state);
            return;
        }

        ESP_LOGI(TAG, "OTA COMMIT: received=%u/%u bytes",
                 (unsigned)s_received_size, (unsigned)s_total_size);

        set_state(OTA_STATE_VERIFYING, OTA_ERR_NONE);

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
            esp_ota_abort(s_ota_handle);
        }
        set_state(OTA_STATE_IDLE, OTA_ERR_ABORTED);
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

    esp_err_t ret = esp_ota_write(s_ota_handle, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed at %u: %s",
                 (unsigned)s_received_size, esp_err_to_name(ret));
        esp_ota_abort(s_ota_handle);
        set_state(OTA_STATE_ERROR, OTA_ERR_WRITE_FAIL);
        return;
    }

    s_received_size += len;
    if (s_state != OTA_STATE_RECEIVING) {
        s_state = OTA_STATE_RECEIVING;
    }

    // 진행률 1% 단위로 상태 알림
    uint8_t progress = (uint8_t)(s_total_size > 0
        ? ((uint64_t)s_received_size * 100 / s_total_size) : 0);
    if (progress != s_last_progress) {
        s_last_progress = progress;
        s_status_changed = true;
        if (progress % 10 == 0) {
            ESP_LOGI(TAG, "OTA progress: %u%% (%u/%u)",
                     progress, (unsigned)s_received_size, (unsigned)s_total_size);
        }
    }
}

bool ble_ota_poll_status(ble_ota_status_pkt_t *status_out) {
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
        esp_ota_abort(s_ota_handle);
        ESP_LOGW(TAG, "OTA aborted due to reset (disconnect)");
    }
    s_state = OTA_STATE_IDLE;
    s_error = OTA_ERR_NONE;
    s_status_changed = false;
    s_total_size = 0;
    s_received_size = 0;
    s_last_progress = 0;
}
