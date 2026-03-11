/**
 * @file ble_server.c
 * @brief Mellow Air BLE GATT Server 구현
 *
 * Service:
 *   BA5E0001-0000-1000-8000-00805F9B34FB
 * Characteristics:
 *   BA5E0002: REALTIME_DATA  (Notify)
 *   BA5E0003: UNSENT_DATA    (Indicate + Write for ACK)
 *   BA5E0004: PROCESS_CONFIG (Write)
 *   BA5E0005: DEVICE_STATUS  (Read)
 *   BA5E0008: TIME_SYNC      (Write) - 앱 시간 동기화
 */

#include "ble_server.h"
#if APP_ENABLE_OTA
#include "ble_ota.h"
#endif
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "spiffs_logger.h"
#include <string.h>

static const char *TAG = "BLE_SERVER";

// 전방 선언
static void set_adv_data(void);

// ============================================================
// UUID 정의 (Little-Endian): BA5E00xx-0000-1000-8000-00805F9B34FB
// ============================================================
#define BA5E_UUID128(id)                                                       \
  {                                                                            \
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00,  \
        (id), 0x00, 0x5E, 0xBA                                                \
  }

static const uint8_t svc_uuid[16] = BA5E_UUID128(0x01);
static const uint8_t uuid_realtime[16] = BA5E_UUID128(0x02);
static const uint8_t uuid_unsent[16] = BA5E_UUID128(0x03);
static const uint8_t uuid_proc_cfg[16] = BA5E_UUID128(0x04);
static const uint8_t uuid_dev_status[16] = BA5E_UUID128(0x05);
static const uint8_t uuid_dev_name[16] = BA5E_UUID128(0x06);
static const uint8_t uuid_elapsed_sync[16] = BA5E_UUID128(0x07);
static const uint8_t uuid_time_sync[16] = BA5E_UUID128(0x08);

#if APP_ENABLE_OTA
static const uint8_t uuid_ota_control[16] = BA5E_UUID128(0x09);
static const uint8_t uuid_ota_data[16] = BA5E_UUID128(0x0A);
static const uint8_t uuid_ota_status[16] = BA5E_UUID128(0x0B);
#endif

// ============================================================
// 광고 파라미터
// ============================================================
// Fast: UPDATE 시 / 재연결 직후 (100-200ms)
static esp_ble_adv_params_t s_adv_params_fast = {
    .adv_int_min       = 0xA0,  // 100ms
    .adv_int_max       = 0x140, // 200ms
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Slow: idle 탐색용 절전 모드 (800-1600ms)
static esp_ble_adv_params_t s_adv_params_slow = {
    .adv_int_min       = 0x500,  // 800ms
    .adv_int_max       = 0xA00,  // 1600ms
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// 현재 사용 중인 광고 파라미터 (disconnect 재시작용)
static esp_ble_adv_params_t *s_current_adv_params = &s_adv_params_fast;

// ============================================================
// 상태 변수
// ============================================================
static bool     s_adv_data_ready  = false;
static bool     s_scan_rsp_ready  = false;
static bool     s_adv_enabled     = false;
static bool     s_is_connected    = false;
static uint16_t s_conn_id         = 0xFFFF;
static esp_gatt_if_t s_gatts_if   = ESP_GATT_IF_NONE;
static esp_bd_addr_t s_remote_bda = {0};  // 연결 파라미터 변경용

// 서비스 핸들
static uint16_t s_svc_handle        = 0;

// Characteristic 핸들
static uint16_t s_hdl_realtime      = 0;
static uint16_t s_hdl_realtime_cccd = 0;
static uint16_t s_hdl_unsent        = 0;
static uint16_t s_hdl_unsent_cccd   = 0;
static uint16_t s_hdl_proc_cfg      = 0;
static uint16_t s_hdl_dev_status    = 0;
static uint16_t s_hdl_dev_name      = 0;
static uint16_t s_hdl_elapsed_sync  = 0;
static uint16_t s_hdl_time_sync     = 0;
#if APP_ENABLE_OTA
static uint16_t s_hdl_ota_control   = 0;
static uint16_t s_hdl_ota_data      = 0;
static uint16_t s_hdl_ota_status    = 0;
static uint16_t s_hdl_ota_status_cccd = 0;
#endif

// 시간 동기화 오프셋 (unix_ts = boot_sec + offset)
static int64_t s_time_offset = 0;
static bool s_has_time_sync = false;

// Notify/Indicate 활성화 플래그
static bool s_notify_realtime    = false;
static bool s_indicate_unsent    = false;
#if APP_ENABLE_OTA
static bool s_notify_ota_status  = false;
#endif
static bool s_initial_sync_pending = false;  // 앱 구독 시작 → 즉시 데이터 전송 트리거

// 앱 경과 시간 동기화 (ELAPSED_SYNC)
static uint32_t s_app_elapsed_sec = 0;
static bool s_app_paused = false;
static bool s_has_app_elapsed = false;
static bool s_app_elapsed_new = false;  // 새 값 수신 플래그 (consume 패턴)
static int64_t s_elapsed_sync_rx_us = 0;  // BLE 수신 시각 (처리 지연 보상용)

// 기기 이름 (NVS 영구 저장)
#define NVS_NAMESPACE "mellowair"
#define NVS_KEY_NAME  "dev_name"
static char s_device_name[BLE_DEVICE_NAME_MAX_LEN + 1] = BLE_DEVICE_NAME;

// GATT 서비스 핸들 등록 순서 추적
typedef enum {
    CHAR_STEP_REALTIME = 0,
    CHAR_STEP_REALTIME_CCCD,
    CHAR_STEP_UNSENT,
    CHAR_STEP_UNSENT_CCCD,
    CHAR_STEP_PROC_CFG,
    CHAR_STEP_DEV_STATUS,
    CHAR_STEP_DEV_NAME,
    CHAR_STEP_ELAPSED_SYNC,
    CHAR_STEP_TIME_SYNC,
#if APP_ENABLE_OTA
    CHAR_STEP_OTA_CONTROL,
    CHAR_STEP_OTA_DATA,
    CHAR_STEP_OTA_STATUS,
    CHAR_STEP_OTA_STATUS_CCCD,
#endif
    CHAR_STEP_DONE
} char_add_step_t;

static char_add_step_t s_add_step = CHAR_STEP_REALTIME;

// 공정 설정 수신 버퍼
static process_context_t s_new_proc_ctx;
static bool s_new_proc_received = false;

// UNSENT 데이터 동기화 상태
static uint32_t s_unsent_seq = 0;
static bool s_unsent_sync_active = false;
static bool s_unsent_header_pending = false;  // 헤더 ACK 대기 중
static bool s_unsent_needs_processing = false;  // 메인 루프에서 처리 필요
static bool s_unsent_start_pending = false;      // 초기 카운트+헤더 필요
static bool s_reset_sent_pending = false;        // sent 리셋 요청 (메인 루프 처리)
static bool s_clear_data_pending = false;        // 데이터 전체 삭제 요청

// 배치 전송 상태
#define UNSENT_BATCH_SIZE 30
static log_record_t s_batch_records[UNSENT_BATCH_SIZE];
static uint32_t s_batch_log_idx[UNSENT_BATCH_SIZE];
static uint32_t s_batch_count = 0;
static uint32_t s_unsent_ring_offset = 0;  // 링 버퍼 스캔 위치

// 동기화 후 지연 마킹 (mark_sent를 sync 완료 후 일괄 처리)
static bool s_mark_sent_pending = false;
// SLOW 파라미터 전환 플래그 (sync 미진행 시 첫 REALTIME notify에서 전환)
static bool s_conn_params_set = false;
static uint32_t s_mark_from_offset = 0;
static uint32_t s_mark_to_offset = 0;

// ============================================================
// 내부 헬퍼: 다음 Characteristic 등록
// ============================================================
static void register_next_char(esp_gatt_if_t gatts_if) {
    esp_bt_uuid_t uuid = {.len = ESP_UUID_LEN_128};
    uint16_t cccd_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

    switch (s_add_step) {
    case CHAR_STEP_REALTIME:
        memcpy(uuid.uuid.uuid128, uuid_realtime, 16);
        esp_ble_gatts_add_char(s_svc_handle, &uuid,
            ESP_GATT_PERM_READ,
            ESP_GATT_CHAR_PROP_BIT_NOTIFY,
            NULL, NULL);
        break;

    case CHAR_STEP_REALTIME_CCCD: {
        esp_bt_uuid_t cccd = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = cccd_uuid};
        uint8_t val[2] = {0, 0};
        esp_attr_value_t attr = {.attr_max_len = 2, .attr_len = 2, .attr_value = val};
        esp_ble_gatts_add_char_descr(s_svc_handle, &cccd,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, &attr, NULL);
        break;
    }

    case CHAR_STEP_UNSENT:
        memcpy(uuid.uuid.uuid128, uuid_unsent, 16);
        esp_ble_gatts_add_char(s_svc_handle, &uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_INDICATE | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
            NULL, NULL);
        break;

    case CHAR_STEP_UNSENT_CCCD: {
        esp_bt_uuid_t cccd = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = cccd_uuid};
        uint8_t val[2] = {0, 0};
        esp_attr_value_t attr = {.attr_max_len = 2, .attr_len = 2, .attr_value = val};
        esp_ble_gatts_add_char_descr(s_svc_handle, &cccd,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, &attr, NULL);
        break;
    }

    case CHAR_STEP_PROC_CFG:
        memcpy(uuid.uuid.uuid128, uuid_proc_cfg, 16);
        esp_ble_gatts_add_char(s_svc_handle, &uuid,
            ESP_GATT_PERM_WRITE,
            ESP_GATT_CHAR_PROP_BIT_WRITE,
            NULL, NULL);
        break;

    case CHAR_STEP_DEV_STATUS:
        memcpy(uuid.uuid.uuid128, uuid_dev_status, 16);
        esp_ble_gatts_add_char(s_svc_handle, &uuid,
            ESP_GATT_PERM_READ,
            ESP_GATT_CHAR_PROP_BIT_READ,
            NULL, NULL);
        break;

    case CHAR_STEP_DEV_NAME:
        memcpy(uuid.uuid.uuid128, uuid_dev_name, 16);
        esp_ble_gatts_add_char(s_svc_handle, &uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE,
            NULL, NULL);
        break;

    case CHAR_STEP_ELAPSED_SYNC:
        memcpy(uuid.uuid.uuid128, uuid_elapsed_sync, 16);
        esp_ble_gatts_add_char(s_svc_handle, &uuid,
            ESP_GATT_PERM_WRITE,
            ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
            NULL, NULL);
        break;

    case CHAR_STEP_TIME_SYNC:
        memcpy(uuid.uuid.uuid128, uuid_time_sync, 16);
        esp_ble_gatts_add_char(s_svc_handle, &uuid,
            ESP_GATT_PERM_WRITE,
            ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
            NULL, NULL);
        break;

#if APP_ENABLE_OTA
    case CHAR_STEP_OTA_CONTROL:
        memcpy(uuid.uuid.uuid128, uuid_ota_control, 16);
        esp_ble_gatts_add_char(s_svc_handle, &uuid,
            ESP_GATT_PERM_WRITE,
            ESP_GATT_CHAR_PROP_BIT_WRITE,
            NULL, NULL);
        break;

    case CHAR_STEP_OTA_DATA:
        memcpy(uuid.uuid.uuid128, uuid_ota_data, 16);
        esp_ble_gatts_add_char(s_svc_handle, &uuid,
            ESP_GATT_PERM_WRITE,
            ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
            NULL, NULL);
        break;

    case CHAR_STEP_OTA_STATUS:
        memcpy(uuid.uuid.uuid128, uuid_ota_status, 16);
        esp_ble_gatts_add_char(s_svc_handle, &uuid,
            ESP_GATT_PERM_READ,
            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
            NULL, NULL);
        break;

    case CHAR_STEP_OTA_STATUS_CCCD: {
        esp_bt_uuid_t cccd = {.len = ESP_UUID_LEN_16, .uuid.uuid16 = cccd_uuid};
        uint8_t val[2] = {0, 0};
        esp_attr_value_t attr = {.attr_max_len = 2, .attr_len = 2, .attr_value = val};
        esp_ble_gatts_add_char_descr(s_svc_handle, &cccd,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, &attr, NULL);
        break;
    }
#endif

    default:
        break;
    }
}

// ============================================================
// 연결 파라미터 전환 (동기화 속도 최적화)
// ============================================================
static void set_conn_params_fast(void) {
    esp_ble_conn_update_params_t p = {0};
    memcpy(p.bda, s_remote_bda, sizeof(esp_bd_addr_t));
    p.min_int = 0x06;   // 7.5ms (최소)
    p.max_int = 0x10;   // 20ms
    p.latency = 0;      // 스킵 없음
    p.timeout = 200;    // 2000ms
    esp_ble_gap_update_conn_params(&p);
    ESP_LOGI(TAG, "Conn params → FAST (7.5-20ms, lat=0)");
}

static void set_conn_params_slow(void) {
    esp_ble_conn_update_params_t p = {0};
    memcpy(p.bda, s_remote_bda, sizeof(esp_bd_addr_t));
    p.min_int = 0x190;  // 500ms (0x190 × 1.25ms)
    p.max_int = 0x1F4;  // 625ms (0x1F4 × 1.25ms)
    p.latency = 4;
    p.timeout = 800;    // 8000ms — must > (1+latency)*max_int*2 = 6250ms
    esp_ble_gap_update_conn_params(&p);
    ESP_LOGI(TAG, "Conn params → SLOW (500-625ms, lat=4)");
}

// ============================================================
// UNSENT 동기화 초기화 헬퍼
// ============================================================
static void start_unsent_sync(void) {
    set_conn_params_fast();
    s_unsent_seq = 0;
    s_batch_count = 0;
    s_unsent_ring_offset = 0;
    s_mark_from_offset = 0;
    s_unsent_sync_active = true;
    s_unsent_start_pending = true;
    s_unsent_needs_processing = true;
}

// ============================================================
// UNSENT 데이터 동기화: 배치 전송 (Notify)
// ============================================================
static void unsent_send_batch(void) {
    if (!s_is_connected || !s_indicate_unsent) return;

    // 1단계: 이전 스캔 위치부터 이어서 읽기 (O(batch) 성능)
    uint32_t prev_ring_offset = s_unsent_ring_offset;  // 되감기용 저장
    spiffs_logger_read_unsent_batch(s_unsent_ring_offset, s_batch_records,
                                    s_batch_log_idx, UNSENT_BATCH_SIZE,
                                    &s_batch_count, &s_unsent_ring_offset);

    if (s_batch_count == 0) {
        // 전송 완료 → end_of_data 먼저 전송, 그 후 절전 파라미터 복원
        ble_unsent_chunk_t end;
        memset(&end, 0, sizeof(end));
        end.type = 1;
        end.seq  = (uint16_t)s_unsent_seq;
        s_unsent_sync_active = false;
        esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_hdl_unsent,
                                    sizeof(end), (uint8_t *)&end, false);
        set_conn_params_slow();
        s_conn_params_set = true;  // SLOW 이미 설정됨
        // 동기화 완료 → 지연 마킹 예약
        s_mark_sent_pending = true;
        s_mark_to_offset = s_unsent_ring_offset;
        ESP_LOGI(TAG, "Unsent sync complete (total=%u), mark deferred (%u-%u)",
                 (unsigned)s_unsent_seq,
                 (unsigned)s_mark_from_offset, (unsigned)s_mark_to_offset);
        return;
    }

    // 2단계: 캐싱된 레코드로 배치 전송 (SPIFFS I/O 없음)
    // ESP32-C3 BLE TX 버퍼 제한 → 3개마다 yield (TX ~10 슬롯)
    // TX 실패 시 yield 후 1회 재시도 → 재실패 시 전체 배치 되감기 (0x80 마커 보장)
    #define TX_FLUSH_INTERVAL 3
    bool tx_failed = false;
    for (uint32_t i = 0; i < s_batch_count; i++) {
        if (i > 0 && (i % TX_FLUSH_INTERVAL) == 0) {
            vTaskDelay(pdMS_TO_TICKS(3));
        }

        ble_unsent_chunk_t chunk;
        memset(&chunk, 0, sizeof(chunk));
        chunk.type      = 0;
        chunk.seq       = (uint16_t)(s_unsent_seq + i);
        chunk.timestamp = s_batch_records[i].timestamp;
        if (s_has_time_sync && chunk.timestamp < 1600000000) {
            chunk.timestamp = (uint32_t)((int64_t)chunk.timestamp + s_time_offset);
        }
        chunk.temp_x10  = (int16_t)(s_batch_records[i].temp * 10);
        chunk.humi_x10  = (int16_t)(s_batch_records[i].humidity * 10);
        chunk.flags     = (i == s_batch_count - 1) ? 0x80 : 0;

        esp_err_t ret = esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                            s_hdl_unsent, sizeof(chunk), (uint8_t *)&chunk, false);
        if (ret != ESP_OK) {
            // TX queue full → yield 10ms 후 1회 재시도
            vTaskDelay(pdMS_TO_TICKS(10));
            ret = esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                        s_hdl_unsent, sizeof(chunk), (uint8_t *)&chunk, false);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "TX failed at %u/%u after retry",
                         (unsigned)i, (unsigned)s_batch_count);
                tx_failed = true;
                break;
            }
        }
    }
    if (tx_failed) {
        // 전체 배치 되감기 → 다음 사이클에서 재시도
        s_unsent_ring_offset = prev_ring_offset;
        s_batch_count = 0;
        s_unsent_needs_processing = true;
        return;
    }
    ESP_LOGI(TAG, "Sent batch of %u (seq=%u)", (unsigned)s_batch_count, (unsigned)s_unsent_seq);
}

// ============================================================
// GATTS 이벤트 핸들러
// ============================================================
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {
    switch (event) {

    case ESP_GATTS_REG_EVT:
        s_gatts_if = gatts_if;
        esp_ble_gap_set_device_name(s_device_name);
        {
            esp_gatt_srvc_id_t svc_id = {
                .is_primary = true,
                .id.inst_id = 0x00,
                .id.uuid.len = ESP_UUID_LEN_128,
            };
            memcpy(svc_id.id.uuid.uuid.uuid128, svc_uuid, 16);
#if APP_ENABLE_OTA
            // 26 핸들: 서비스(1) + 10chars×2 + 3CCCDs + 여유2
            esp_ble_gatts_create_service(gatts_if, &svc_id, 26);
#else
            // 18 핸들: 서비스(1) + 7chars×2 + 2CCCDs + 여유1
            esp_ble_gatts_create_service(gatts_if, &svc_id, 18);
#endif
        }
        break;

    case ESP_GATTS_CREATE_EVT:
        s_svc_handle = param->create.service_handle;
        esp_ble_gatts_start_service(s_svc_handle);
        s_add_step = CHAR_STEP_REALTIME;
        register_next_char(gatts_if);
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        switch (s_add_step) {
        case CHAR_STEP_REALTIME:
            s_hdl_realtime = param->add_char.attr_handle;
            s_add_step = CHAR_STEP_REALTIME_CCCD;
            break;
        case CHAR_STEP_UNSENT:
            s_hdl_unsent = param->add_char.attr_handle;
            s_add_step = CHAR_STEP_UNSENT_CCCD;
            break;
        case CHAR_STEP_PROC_CFG:
            s_hdl_proc_cfg = param->add_char.attr_handle;
            s_add_step = CHAR_STEP_DEV_STATUS;
            break;
        case CHAR_STEP_DEV_STATUS:
            s_hdl_dev_status = param->add_char.attr_handle;
            s_add_step = CHAR_STEP_DEV_NAME;
            break;
        case CHAR_STEP_DEV_NAME:
            s_hdl_dev_name = param->add_char.attr_handle;
            s_add_step = CHAR_STEP_ELAPSED_SYNC;
            break;
        case CHAR_STEP_ELAPSED_SYNC:
            s_hdl_elapsed_sync = param->add_char.attr_handle;
            s_add_step = CHAR_STEP_TIME_SYNC;
            break;
        case CHAR_STEP_TIME_SYNC:
            s_hdl_time_sync = param->add_char.attr_handle;
#if APP_ENABLE_OTA
            s_add_step = CHAR_STEP_OTA_CONTROL;
#else
            s_add_step = CHAR_STEP_DONE;
            ESP_LOGI(TAG, "All chars registered");
#endif
            break;
#if APP_ENABLE_OTA
        case CHAR_STEP_OTA_CONTROL:
            s_hdl_ota_control = param->add_char.attr_handle;
            s_add_step = CHAR_STEP_OTA_DATA;
            break;
        case CHAR_STEP_OTA_DATA:
            s_hdl_ota_data = param->add_char.attr_handle;
            s_add_step = CHAR_STEP_OTA_STATUS;
            break;
        case CHAR_STEP_OTA_STATUS:
            s_hdl_ota_status = param->add_char.attr_handle;
            s_add_step = CHAR_STEP_OTA_STATUS_CCCD;
            break;
#endif
        default:
            break;
        }
        if (s_add_step != CHAR_STEP_DONE) {
            register_next_char(gatts_if);
        }
        break;

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        switch (s_add_step) {
        case CHAR_STEP_REALTIME_CCCD:
            s_hdl_realtime_cccd = param->add_char_descr.attr_handle;
            s_add_step = CHAR_STEP_UNSENT;
            break;
        case CHAR_STEP_UNSENT_CCCD:
            s_hdl_unsent_cccd = param->add_char_descr.attr_handle;
            s_add_step = CHAR_STEP_PROC_CFG;
            break;
#if APP_ENABLE_OTA
        case CHAR_STEP_OTA_STATUS_CCCD:
            s_hdl_ota_status_cccd = param->add_char_descr.attr_handle;
            s_add_step = CHAR_STEP_DONE;
            ESP_LOGI(TAG, "All chars registered (incl. OTA)");
            break;
#endif
        default:
            break;
        }
        if (s_add_step < CHAR_STEP_DONE) {
            register_next_char(gatts_if);
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        s_is_connected = true;
        s_conn_id = param->connect.conn_id;
        memcpy(s_remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        s_notify_realtime = false;
        s_indicate_unsent  = false;
        s_unsent_seq = 0;
        s_unsent_ring_offset = 0;
        s_unsent_sync_active = false;
        s_unsent_header_pending = false;
        s_conn_params_set = false;
        ESP_LOGI(TAG, "BLE Connected (conn_id=%d)", s_conn_id);
        // 연결 파라미터: OS 기본값 유지 (~30ms, iOS/Android)
        // SLOW 전환은 sync 완료 후 또는 첫 REALTIME notify 시 수행
        // (CONNECT 시 SLOW 설정 → FAST 협상 지연 문제 방지)
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        s_is_connected = false;
        s_conn_id = 0xFFFF;
        s_notify_realtime = false;
        s_indicate_unsent  = false;
        s_unsent_sync_active = false;
        s_unsent_header_pending = false;
        s_unsent_needs_processing = false;
        s_unsent_start_pending = false;
#if APP_ENABLE_OTA
        s_notify_ota_status = false;
        ble_ota_reset();
#endif
        // 앱 elapsed 상태는 유지 (main.c에서 disconnect 전환 시 참조)
        ESP_LOGI(TAG, "BLE Disconnected");
        // 재연결 대비: fast 광고로 자동 재시작
        s_current_adv_params = &s_adv_params_fast;
        if (s_adv_enabled) {
            esp_ble_gap_start_advertising(&s_adv_params_fast);
        }
        break;

    case ESP_GATTS_WRITE_EVT: {
        uint16_t handle = param->write.handle;
        uint8_t *val = param->write.value;
        uint16_t len = param->write.len;

        if (!param->write.is_prep) {
            // REALTIME_DATA CCCD
            if (handle == s_hdl_realtime_cccd && len >= 2) {
                uint16_t cccd = val[0] | (val[1] << 8);
                s_notify_realtime = (cccd == 0x0001);
                if (s_notify_realtime) {
                    s_initial_sync_pending = true;  // 즉시 센서 데이터 전송 트리거
                }
                ESP_LOGI(TAG, "REALTIME notify %s", s_notify_realtime ? "ON" : "OFF");
            }
            // UNSENT_DATA CCCD (Notify=0x01 또는 Indicate=0x02 모두 수용)
            else if (handle == s_hdl_unsent_cccd && len >= 2) {
                uint16_t cccd = val[0] | (val[1] << 8);
                s_indicate_unsent = ((cccd & 0x0003) != 0);
                ESP_LOGI(TAG, "UNSENT subscribe %s (cccd=0x%04x)",
                         s_indicate_unsent ? "ON" : "OFF", cccd);
                if (s_indicate_unsent && !s_unsent_sync_active) {
                    start_unsent_sync();
                    ESP_LOGI(TAG, "UNSENT sync requested (deferred to main loop)");
                }
            }
            // UNSENT_DATA Write (앱 ACK) — 플래그만 설정, SPIFFS I/O는 메인 루프
            else if (handle == s_hdl_unsent && len >= 1 && val[0] == 0xAC) {
                s_unsent_needs_processing = true;
            }
            // UNSENT_DATA Write (sent 플래그 리셋 명령: 0x52 = 'R')
            else if (handle == s_hdl_unsent && len >= 1 && val[0] == 0x52) {
                s_reset_sent_pending = true;
                ESP_LOGI(TAG, "Sent flags reset requested (deferred)");
            }
            // UNSENT_DATA Write (데이터 전체 삭제: 0x44 = 'D')
            else if (handle == s_hdl_unsent && len >= 1 && val[0] == 0x44) {
                s_clear_data_pending = true;
                ESP_LOGI(TAG, "Data clear requested (deferred)");
            }
            // UNSENT_DATA Write (재동기화 명령: 0x53 = 'S')
            else if (handle == s_hdl_unsent && len >= 1 && val[0] == 0x53) {
                if (s_indicate_unsent && !s_unsent_sync_active) {
                    start_unsent_sync();
                    ESP_LOGI(TAG, "UNSENT resync requested (0x53)");
                }
            }
            // DEVICE_NAME Write
            else if (handle == s_hdl_dev_name && len > 0) {
                uint16_t copy_len = len < BLE_DEVICE_NAME_MAX_LEN ? len : BLE_DEVICE_NAME_MAX_LEN;
                memcpy(s_device_name, val, copy_len);
                s_device_name[copy_len] = '\0';

                // NVS에 영구 저장
                nvs_handle_t nvs;
                if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
                    nvs_set_str(nvs, NVS_KEY_NAME, s_device_name);
                    nvs_commit(nvs);
                    nvs_close(nvs);
                }

                // GAP 이름 + advertising 즉시 갱신
                esp_ble_gap_set_device_name(s_device_name);
                set_adv_data();
                ESP_LOGI(TAG, "DEVICE_NAME updated: '%s'", s_device_name);
            }
            // ELAPSED_SYNC Write (앱 경과 시간 동기화)
            else if (handle == s_hdl_elapsed_sync &&
                     len >= sizeof(ble_elapsed_sync_pkt_t)) {
                const ble_elapsed_sync_pkt_t *epkt =
                    (const ble_elapsed_sync_pkt_t *)val;
                s_app_elapsed_sec = epkt->elapsed_sec;
                s_app_paused = (epkt->flags & 0x01) != 0;
                s_has_app_elapsed = true;
                s_app_elapsed_new = true;
                s_elapsed_sync_rx_us = esp_timer_get_time();
                ESP_LOGI(TAG, "ELAPSED_SYNC rx: %us, paused=%d",
                         (unsigned)s_app_elapsed_sec, s_app_paused);
            }
            // TIME_SYNC Write (앱 시간 동기화)
            else if (handle == s_hdl_time_sync &&
                     len >= sizeof(ble_time_sync_pkt_t)) {
                const ble_time_sync_pkt_t *tpkt =
                    (const ble_time_sync_pkt_t *)val;
                uint32_t boot_sec = (uint32_t)(esp_timer_get_time() / 1000000LL);
                s_time_offset = (int64_t)tpkt->unix_timestamp - (int64_t)boot_sec;
                s_has_time_sync = true;
                ESP_LOGI(TAG, "TIME_SYNC: app=%u, boot=%u, offset=%lld",
                         (unsigned)tpkt->unix_timestamp, (unsigned)boot_sec,
                         (long long)s_time_offset);
            }
#if APP_ENABLE_OTA
            // OTA_CONTROL Write
            else if (handle == s_hdl_ota_control && len >= 1) {
                ble_ota_handle_control(val, len);
            }
            // OTA_DATA Write (Write Without Response)
            else if (handle == s_hdl_ota_data && len > 0) {
                ble_ota_handle_data(val, len);
            }
            // OTA_STATUS CCCD
            else if (handle == s_hdl_ota_status_cccd && len >= 2) {
                uint16_t cccd = val[0] | (val[1] << 8);
                s_notify_ota_status = (cccd == 0x0001);
                ESP_LOGI(TAG, "OTA_STATUS notify %s",
                         s_notify_ota_status ? "ON" : "OFF");
            }
#endif
            // PROCESS_CONFIG Write
            else if (handle == s_hdl_proc_cfg &&
                     len >= sizeof(ble_process_config_pkt_t)) {
                const ble_process_config_pkt_t *pkt =
                    (const ble_process_config_pkt_t *)val;

                memset(&s_new_proc_ctx, 0, sizeof(s_new_proc_ctx));
                snprintf(s_new_proc_ctx.process_name,
                         sizeof(s_new_proc_ctx.process_name),
                         "%.*s", 31, pkt->process_name);
                s_new_proc_ctx.target_temp    = pkt->target_temp;
                s_new_proc_ctx.target_humi    = pkt->target_humi;
                s_new_proc_ctx.tolerance_temp = pkt->tolerance_temp;
                s_new_proc_ctx.tolerance_humi = pkt->tolerance_humi;
                s_new_proc_ctx.duration_min   = pkt->duration_min;
                s_new_proc_ctx.start_time     = pkt->start_time;
                s_new_proc_ctx.is_active      = true;
                s_new_proc_received = true;
                ESP_LOGI(TAG, "PROCESS_CONFIG received: '%s'",
                         s_new_proc_ctx.process_name);
            }

            // Write Response
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id,
                                            ESP_GATT_OK, NULL);
            }
        }
        break;
    }

    case ESP_GATTS_READ_EVT: {
        if (param->read.handle == s_hdl_dev_name) {
            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(rsp));
            uint16_t name_len = (uint16_t)strlen(s_device_name);
            rsp.attr_value.len = name_len;
            memcpy(rsp.attr_value.value, s_device_name, name_len);
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                        param->read.trans_id,
                                        ESP_GATT_OK, &rsp);
        }
#if APP_ENABLE_OTA
        else if (param->read.handle == s_hdl_ota_status) {
            ble_ota_status_pkt_t ota_st;
            ble_ota_poll_status(&ota_st);
            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(rsp));
            rsp.attr_value.len = sizeof(ota_st);
            memcpy(rsp.attr_value.value, &ota_st, sizeof(ota_st));
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                        param->read.trans_id,
                                        ESP_GATT_OK, &rsp);
        }
#endif
        else if (param->read.handle == s_hdl_dev_status) {
            ble_device_status_t status;
            memset(&status, 0, sizeof(status));
            status.fw_major = APP_FW_MAJOR;
            status.fw_minor = APP_FW_MINOR;
            status.fw_patch = APP_FW_PATCH;
            size_t used = 0, total = 0;
            spiffs_logger_get_usage(&used, &total);
            status.storage_used  = (uint32_t)used;
            status.storage_total = (uint32_t)total;
            status.uptime_sec    = (uint32_t)(esp_timer_get_time() / 1000000LL);
            snprintf(status.process_name, sizeof(status.process_name),
                     "%s", s_new_proc_ctx.process_name);

            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(rsp));
            rsp.attr_value.len = sizeof(status);
            memcpy(rsp.attr_value.value, &status, sizeof(status));
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                        param->read.trans_id,
                                        ESP_GATT_OK, &rsp);
        }
        break;
    }

    case ESP_GATTS_CONF_EVT:
        // Indication confirmed (GATT-level ACK)
        ESP_LOGD(TAG, "Indication confirmed");
        break;

    default:
        break;
    }
}

// ============================================================
// GAP 이벤트 핸들러
// ============================================================
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        s_adv_data_ready = true;
        if (s_scan_rsp_ready && s_adv_enabled) {
            esp_ble_gap_start_advertising(s_current_adv_params);
        }
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        s_scan_rsp_ready = true;
        if (s_adv_data_ready && s_adv_enabled) {
            esp_ble_gap_start_advertising(s_current_adv_params);
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        // 광고 중지 완료 → s_adv_enabled이면 새 파라미터로 재시작
        if (s_adv_enabled && !s_is_connected &&
            s_adv_data_ready && s_scan_rsp_ready) {
            esp_ble_gap_start_advertising(s_current_adv_params);
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Advertising started");
        }
        break;

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "Conn params: interval=%d latency=%d timeout=%d",
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;

    default:
        break;
    }
}

// ============================================================
// 광고 데이터 설정
// ============================================================
static void set_adv_data(void) {
    // Advertising data: Flags(3) + Complete Local Name(2 + name_len)
    uint8_t name_len = (uint8_t)strlen(s_device_name);
    uint8_t raw_adv[31];
    uint8_t pos = 0;

    // Flags
    raw_adv[pos++] = 0x02;  // length
    raw_adv[pos++] = 0x01;  // type: Flags
    raw_adv[pos++] = 0x06;  // LE General Discoverable, BR/EDR not supported

    // Complete Local Name
    raw_adv[pos++] = name_len + 1;  // length (type byte + name)
    raw_adv[pos++] = 0x09;          // type: Complete Local Name
    memcpy(&raw_adv[pos], s_device_name, name_len);
    pos += name_len;

    uint8_t raw_scan_rsp[] = {
        0x11, 0x07,  // Length=17, Type=Complete UUID128
        0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x01, 0x00, 0x5E, 0xBA
    };
    esp_ble_gap_config_adv_data_raw(raw_adv, pos);
    esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp, sizeof(raw_scan_rsp));
}

// ============================================================
// 공개 API
// ============================================================
esp_err_t ble_server_init(void) {
    esp_err_t ret;

    // NVS에서 기기 이름 로드 (없으면 기본값 "Mellow Air" 유지)
    {
        nvs_handle_t nvs;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
            size_t len = sizeof(s_device_name);
            if (nvs_get_str(nvs, NVS_KEY_NAME, s_device_name, &len) == ESP_OK) {
                ESP_LOGI(TAG, "Device name loaded from NVS: '%s'", s_device_name);
            }
            nvs_close(nvs);
        }
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) return ret;

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) return ret;

    ret = esp_bluedroid_init();
    if (ret) return ret;

    ret = esp_bluedroid_enable();
    if (ret) return ret;

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) return ret;

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) return ret;

    ret = esp_ble_gatts_app_register(0);
    if (ret) return ret;

    // TX 파워 설정: 모든 타입 +9 dBm (광고, 스캔, 연결 모두)
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);

    set_adv_data();
    s_adv_enabled = false;  // 첫 데이터 업데이트 전까지 광고 비활성

    // BT 컨트롤러 슬립: 모든 설정 완료 후 활성화
    esp_bt_sleep_enable();

    ESP_LOGI(TAG, "BLE GATT server initialized (Mellow Air)");
    return ESP_OK;
}

void ble_server_pause(void) {
    s_adv_enabled = false;
    esp_ble_gap_stop_advertising();
    ESP_LOGI(TAG, "BLE Advertising paused");
}

void ble_server_resume(void) {
    s_adv_enabled = true;
    s_current_adv_params = &s_adv_params_fast;
    // USB/fast 모드: TX 파워 복원 (+9dBm)
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    if (s_is_connected) return;
    // 중지 후 콜백에서 새 파라미터로 재시작 (파라미터 변경 보장)
    esp_ble_gap_stop_advertising();
    ESP_LOGD(TAG, "BLE Advertising resumed (fast, +9dBm)");
}

void ble_server_resume_slow(void) {
    s_adv_enabled = true;
    s_current_adv_params = &s_adv_params_slow;
    // 배터리 모드: TX 파워 절감 (+3dBm, 같은 방 안에서 충분)
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P3);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P3);
    if (s_is_connected) return;
    esp_ble_gap_stop_advertising();
    ESP_LOGD(TAG, "BLE Advertising resumed (slow, +3dBm)");
}

void ble_server_notify_realtime(const sensor_data_t *data, uint32_t timestamp,
                                uint32_t elapsed_sec) {
    if (!s_is_connected || !s_notify_realtime || data == NULL) return;
    if (s_hdl_realtime == 0) return;

    // 첫 REALTIME notify 시 SLOW 전환
    // 단, sync 중이거나 미전송 100건 이상이면 보류 (FAST 협상 지연 방지)
    if (!s_conn_params_set && !s_unsent_sync_active &&
        spiffs_logger_unsent_count() < 100) {
        s_conn_params_set = true;
        set_conn_params_slow();
    }

    ble_realtime_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.timestamp   = timestamp;
    pkt.temp_x10    = (int16_t)(data->temperature * 10);
    pkt.humi_x10    = (int16_t)(data->humidity * 10);
    pkt.flags       = data->high_temp_warn ? 0x01 : 0x00;
    pkt.elapsed_sec = elapsed_sec;

    esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_hdl_realtime,
                                sizeof(pkt), (uint8_t *)&pkt, false);
    ESP_LOGD(TAG, "REALTIME notify sent T=%d H=%d elapsed=%us",
             pkt.temp_x10, pkt.humi_x10, (unsigned)elapsed_sec);
}

bool ble_server_is_connected(void) {
    return s_is_connected;
}

bool ble_server_is_subscribed(void) {
    return s_notify_realtime || s_indicate_unsent;
}

bool ble_server_consume_initial_sync(void) {
    bool result = s_initial_sync_pending;
    s_initial_sync_pending = false;
    return result;
}

void ble_server_disconnect(void) {
    if (s_is_connected && s_gatts_if != ESP_GATT_IF_NONE && s_conn_id != 0xFFFF) {
        ESP_LOGW(TAG, "Force disconnect conn_id=%d", s_conn_id);
        esp_ble_gatts_close(s_gatts_if, s_conn_id);
    }
}

bool ble_server_poll_process_config(process_context_t *ctx) {
    if (!s_new_proc_received || ctx == NULL) return false;
    *ctx = s_new_proc_ctx;
    s_new_proc_received = false;
    return true;
}

const char *ble_server_get_device_name(void) {
    return s_device_name;
}

bool ble_server_process_unsent(void) {
    if (!s_unsent_needs_processing || !s_unsent_sync_active) return false;
    if (!s_is_connected || !s_indicate_unsent) {
        s_unsent_needs_processing = false;
        return false;
    }
    s_unsent_needs_processing = false;

    // 초기 시작: 미전송 레코드 수 + 헤더 전송
    // unsent_count()는 O(n) 스캔이지만 동기화 시작 시 1회만 호출
    if (s_unsent_start_pending) {
        s_unsent_start_pending = false;
        uint32_t total = spiffs_logger_unsent_count();
        if (total > 0) {
            // FAST 전환은 CCCD 구독 시점에 이미 요청됨
            ble_unsent_chunk_t hdr;
            memset(&hdr, 0, sizeof(hdr));
            hdr.type = 2;  // header
            hdr.seq  = 0;
            hdr.timestamp = total;
            s_unsent_header_pending = true;
            ESP_LOGI(TAG, "Unsent sync start: total=%u", (unsigned)total);
            esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                s_hdl_unsent, sizeof(hdr), (uint8_t *)&hdr, true);
        } else {
            // 미전송 없음 → 즉시 end_of_data
            ble_unsent_chunk_t end;
            memset(&end, 0, sizeof(end));
            end.type = 1;
            end.seq  = 0;
            s_unsent_sync_active = false;
            esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_hdl_unsent,
                                        sizeof(end), (uint8_t *)&end, false);
            ESP_LOGI(TAG, "Unsent sync: no data");
        }
        return true;
    }

    // 헤더 ACK → 첫 배치 전송
    if (s_unsent_header_pending) {
        s_unsent_header_pending = false;
        unsent_send_batch();
        return true;
    }

    // 배치 ACK → mark_sent 생략 (sync 완료 후 일괄 처리) → 다음 배치 즉시 전송
    {
        s_unsent_seq += s_batch_count;
        s_batch_count = 0;
        unsent_send_batch();
        ESP_LOGI(TAG, "Batch ACK → next batch (seq=%u)", (unsigned)s_unsent_seq);
    }
    return true;
}

bool ble_server_process_reset_sent(void) {
    if (!s_reset_sent_pending) return false;
    s_reset_sent_pending = false;
    ESP_LOGW(TAG, "Executing sent flags reset...");
    esp_err_t rc = spiffs_logger_reset_sent_flags();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Sent flags reset failed: %s", esp_err_to_name(rc));
    } else {
        ESP_LOGW(TAG, "Sent flags reset complete");
    }

    // 리셋 완료 알림: UNSENT_DATA에 0x52 응답 (앱이 재구독 타이밍 판단)
    if (s_is_connected && s_indicate_unsent && s_hdl_unsent != 0) {
        uint8_t ack = 0x52;
        esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_hdl_unsent,
                                    1, &ack, false);
        ESP_LOGI(TAG, "Reset complete notification sent");
    }
    return true;
}

bool ble_server_process_clear_data(void) {
    if (!s_clear_data_pending) return false;
    s_clear_data_pending = false;
    ESP_LOGW(TAG, "Executing data clear...");
    esp_err_t rc2 = spiffs_logger_clear_all();
    if (rc2 != ESP_OK) {
        ESP_LOGE(TAG, "Data clear failed: %s", esp_err_to_name(rc2));
    } else {
        ESP_LOGW(TAG, "Data clear complete");
    }

    if (s_is_connected && s_indicate_unsent && s_hdl_unsent != 0) {
        uint8_t ack = 0x44;
        esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_hdl_unsent,
                                    1, &ack, false);
    }
    return true;
}

bool ble_server_process_deferred_mark(void) {
    if (!s_mark_sent_pending) return false;
    s_mark_sent_pending = false;
    ESP_LOGI(TAG, "Deferred mark_sent: range %u-%u",
             (unsigned)s_mark_from_offset, (unsigned)s_mark_to_offset);
    int64_t t0 = esp_timer_get_time();
    spiffs_logger_mark_range_sent(s_mark_from_offset, s_mark_to_offset);
    int64_t t1 = esp_timer_get_time();
    ESP_LOGI(TAG, "Deferred mark_sent done: %lldms", (t1 - t0) / 1000);
    return true;
}

bool ble_server_is_syncing(void) {
    return s_unsent_sync_active || s_mark_sent_pending;
}

bool ble_server_has_app_elapsed(void) {
    return s_has_app_elapsed;
}

bool ble_server_get_app_elapsed(uint32_t *elapsed, bool *paused) {
    if (!s_has_app_elapsed) return false;
    if (elapsed) *elapsed = s_app_elapsed_sec;
    if (paused)  *paused  = s_app_paused;
    return true;
}

void ble_server_clear_app_elapsed(void) {
    s_has_app_elapsed = false;
    s_app_elapsed_new = false;
}

bool ble_server_consume_new_elapsed(uint32_t *elapsed, bool *paused) {
    if (!s_app_elapsed_new) return false;
    s_app_elapsed_new = false;
    if (elapsed) *elapsed = s_app_elapsed_sec;
    if (paused)  *paused  = s_app_paused;
    return true;
}

int64_t ble_server_get_elapsed_sync_rx_time(void) {
    return s_elapsed_sync_rx_us;
}

bool ble_server_has_time_sync(void) {
    return s_has_time_sync;
}

int64_t ble_server_get_time_offset(void) {
    return s_time_offset;
}

#if APP_ENABLE_OTA
bool ble_server_process_ota(uint8_t *out_state, uint8_t *out_progress) {
    ble_ota_status_pkt_t pkt;
    if (!ble_ota_poll_status(&pkt)) return false;

    if (out_state) *out_state = pkt.state;
    if (out_progress) *out_progress = pkt.progress_pct;

    // OTA 상태 변경 시 Notify 전송
    if (s_is_connected && s_notify_ota_status && s_hdl_ota_status) {
        esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_hdl_ota_status,
                                    sizeof(pkt), (uint8_t *)&pkt, false);
    }

    // OTA 성공 → 3초 후 재부팅
    if (ble_ota_needs_reboot()) {
        ESP_LOGI(TAG, "OTA complete — rebooting...");
        ble_ota_reboot();
    }
    return true;
}

bool ble_server_is_ota_active(void) {
    return ble_ota_is_active();
}
#else
bool ble_server_process_ota(uint8_t *out_state, uint8_t *out_progress) {
    (void)out_state; (void)out_progress; return false;
}
bool ble_server_is_ota_active(void) { return false; }
#endif
