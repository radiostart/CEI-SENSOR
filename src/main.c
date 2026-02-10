/**
 * @file main.c
 * @brief CEI-SENSOR 메인 애플리케이션
 *
 * 레이어드 아키텍처 기반 상태 머신
 */

#if !defined(ENABLE_EPD_DEMO) || !ENABLE_EPD_DEMO

#include "app_config.h"
#include "battery_monitor.h"
#include "display_service.h"
#include "power_manager.h"
#include "sensor_service.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if APP_ENABLE_BLE
#include "ble_server.h"
#endif

static const char *TAG = "MAIN";

// ============================================================
// 초기화 함수
// ============================================================
static esp_err_t app_init_nvs(void) {
  ESP_LOGI(TAG, "Initializing NVS...");

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition erasing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  return ret;
}

static void app_init_all(void) {
  ESP_LOGI(TAG, "====================================");
  ESP_LOGI(TAG, "  CEI-SENSOR Starting...");
  ESP_LOGI(TAG, "====================================");

  // NVS (BLE 필수)
  ESP_ERROR_CHECK(app_init_nvs());

  // 전원 관리자 (버튼 포함)
  power_manager_init();

  // 센서 서비스
  sensor_service_init();

  // 디스플레이 서비스
  display_service_init();

#if APP_ENABLE_BLE
  // BLE 서버
  ESP_LOGI(TAG, "Initializing BLE...");
  if (ble_server_init() != ESP_OK) {
    ESP_LOGW(TAG, "BLE init failed (continuing without BLE)");
  }
#endif

  ESP_LOGI(TAG, "All systems initialized");
}

// ============================================================
// 메인 루프 핸들러
// ============================================================
static void handle_active_state(void) {
  sensor_data_t data;

  // 센서 데이터 읽기 (I2C만 사용, 디스플레이/BLE 미활성)
  esp_err_t ret = sensor_service_read(&data);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Sensor read failed");
  }

  // 변화 확인 또는 강제 업데이트
  bool force_update = power_manager_consume_update_request();
  bool significant_change = sensor_service_is_significant_change(&data);

  if (significant_change || force_update) {
    ESP_LOGI(TAG, ">>> UPDATE (change=%d, force=%d) <<<",
             significant_change, force_update);

    // 마지막 값 저장
    sensor_service_update_last(&data);

    // 디스플레이 웨이크업 (변화 시에만)
    display_service_wakeup();

#if APP_ENABLE_BLE
    ble_server_resume();
#endif

    // 배터리 상태 (ADC on-demand)
    battery_monitor_init();
    int battery_pct = battery_get_percentage();
    battery_monitor_deinit();
    ESP_LOGI(TAG, "Battery: %d%%", battery_pct);

    // 디스플레이 업데이트
    display_service_update(&data, battery_pct);

#if APP_ENABLE_BLE
    // BLE 광고 데이터 업데이트
    ble_update_advertising_data(data.temperature, data.humidity, data.cess);

    // 브로드캐스트 대기 (버튼 모니터링 포함)
    ESP_LOGI(TAG, "Broadcasting...");
    for (int i = 0; i < (APP_BLE_BROADCAST_MS / 100); i++) {
      if (power_manager_handle_button()) {
        ble_server_pause();
        display_service_sleep();
        return;  // 상태 변경됨
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    // BLE 일시 중지
    ble_server_pause();
    vTaskDelay(pdMS_TO_TICKS(100));
#endif

    // 디스플레이 슬립
    display_service_sleep();
  } else {
    ESP_LOGI(TAG, ">>> SKIP (no significant change) <<<");
  }

  // 슬립 전 버튼 체크
  if (power_manager_handle_button()) {
    return;
  }

  // 슬립 진입
  wakeup_cause_t cause = power_manager_enter_sleep();

  // 웨이크업 후 버튼 체크
  if (cause == WAKEUP_CAUSE_BUTTON) {
    power_manager_request_update();
  }

  if (power_manager_handle_button()) {
    return;
  }
}

// ============================================================
// 메인 엔트리
// ============================================================
void app_main(void) {
  // 시스템 초기화
  app_init_all();

  // 메인 루프
  while (1) {
    app_state_t state = power_manager_get_state();

    switch (state) {
    case APP_STATE_ACTIVE:
      handle_active_state();
      break;

    case APP_STATE_OFF:
      power_manager_enter_off_mode();
      break;

    default:
      // INIT, SLEEP, WAKEUP은 ACTIVE로 전이
      power_manager_set_state(APP_STATE_ACTIVE);
      break;
    }

    // 짧은 안정화 딜레이
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

#endif // !ENABLE_EPD_DEMO
