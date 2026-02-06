#include "ble_server.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include <string.h>

#define TAG "BLE_SERVER"

// Service UUID (128-bit) - Little Endian
// 4fafc201-1fb5-459e-8fcc-c5c9c331914b
static const uint8_t service_uuid[16] = {
    0x4b, 0x91, 0x33, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
    0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f};

// Characteristic UUID (128-bit) - Little Endian
// beb5483e-36e1-4688-b7f5-ea07361b26ab
static const uint8_t char_uuid[16] = {
    0xab, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
    0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe};

// 광고 파라미터
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// 광고 데이터 설정 완료 플래그
static bool adv_data_ready = false;
static bool scan_rsp_ready = false;

void ble_server_pause(void) {
  esp_ble_gap_stop_advertising();
  ESP_LOGI(TAG, "BLE Advertising Stopped");
}

void ble_server_resume(void) {
  esp_ble_gap_start_advertising(&adv_params);
  ESP_LOGI(TAG, "BLE Advertising Resumed");
}

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param) {
  switch (event) {
  case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
    adv_data_ready = true;
    if (scan_rsp_ready) {
      esp_ble_gap_start_advertising(&adv_params);
    }
    break;
  case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
    scan_rsp_ready = true;
    if (adv_data_ready) {
      esp_ble_gap_start_advertising(&adv_params);
    }
    break;
  case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
    if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
      ESP_LOGI(TAG, "BLE Advertising Started");
    } else {
      ESP_LOGE(TAG, "Advertising Failed: %d", param->adv_start_cmpl.status);
    }
    break;
  default:
    break;
  }
}

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {
  switch (event) {
  case ESP_GATTS_REG_EVT: {
    esp_ble_gap_set_device_name(BLE_DEVICE_NAME);

    // 서비스 생성
    esp_gatt_srvc_id_t service_id = {
        .is_primary = true,
        .id.inst_id = 0x00,
        .id.uuid.len = ESP_UUID_LEN_128,
    };
    memcpy(service_id.id.uuid.uuid.uuid128, service_uuid, 16);
    esp_ble_gatts_create_service(gatts_if, &service_id, 4);
    break;
  }

  case ESP_GATTS_CREATE_EVT: {
    esp_ble_gatts_start_service(param->create.service_handle);

    // Characteristic 추가
    esp_bt_uuid_t c_uuid = {.len = ESP_UUID_LEN_128};
    memcpy(c_uuid.uuid.uuid128, char_uuid, 16);

    esp_ble_gatts_add_char(param->create.service_handle, &c_uuid,
                           ESP_GATT_PERM_READ,
                           ESP_GATT_CHAR_PROP_BIT_READ |
                               ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                           NULL, NULL);
    break;
  }

  case ESP_GATTS_DISCONNECT_EVT:
    esp_ble_gap_start_advertising(&adv_params);
    break;

  default:
    break;
  }
}

esp_err_t ble_server_init(void) {
  esp_err_t ret;

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret)
    return ret;

  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret)
    return ret;

  ret = esp_bluedroid_init();
  if (ret)
    return ret;

  ret = esp_bluedroid_enable();
  if (ret)
    return ret;

  ret = esp_ble_gatts_register_callback(gatts_event_handler);
  if (ret)
    return ret;

  ret = esp_ble_gap_register_callback(gap_event_handler);
  if (ret)
    return ret;

  ret = esp_ble_gatts_app_register(0);
  if (ret)
    return ret;

  // TX Power 설정 (저전력)
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N0);

  // 초기 광고 데이터 설정 (0값으로 시작)
  ble_update_advertising_data(0.0f, 0.0f, 0.0f);

  return ESP_OK;
}

esp_err_t ble_update_advertising_data(float temp, float hum, float cess) {
  // 센서 값을 정수로 변환 (소수점 보존)
  int16_t t_val = (int16_t)(temp * 10);   // 온도 x10
  int16_t h_val = (int16_t)(hum * 10);    // 습도 x10
  int16_t c_val = (int16_t)(cess * 100);  // CESS x100

  // RAW 광고 데이터 (27 bytes)
  // - Flags: 3 bytes
  // - Service Data: 24 bytes (Type + UUID + Data)
  uint8_t raw_adv_data[] = {
      // Flags
      0x02, 0x01, 0x06,
      // Service Data (128-bit UUID) - Length: 23
      0x17, 0x21,
      // UUID (Little Endian)
      0x4b, 0x91, 0x33, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
      0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f,
      // Data (6 bytes: temp, hum, cess)
      (uint8_t)(t_val & 0xFF), (uint8_t)((t_val >> 8) & 0xFF),
      (uint8_t)(h_val & 0xFF), (uint8_t)((h_val >> 8) & 0xFF),
      (uint8_t)(c_val & 0xFF), (uint8_t)((c_val >> 8) & 0xFF)};

  // Scan Response: 디바이스 이름 (12 bytes)
  uint8_t raw_scan_rsp_data[] = {
      0x0B, 0x09, 'C', 'E', 'I', '-', 'S', 'e', 'n', 's', 'o', 'r'};

  esp_err_t ret =
      esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
  if (ret != ESP_OK)
    return ret;

  return esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data,
                                              sizeof(raw_scan_rsp_data));
}
