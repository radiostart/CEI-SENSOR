/**
 * @file power_manager.c
 * @brief 전원 및 슬립 관리 구현
 */

#include "power_manager.h"
#include "button_handler.h"
#include "display_service.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if APP_ENABLE_BLE
#include "ble_server.h"
#endif

static const char *TAG = "POWER_MGR";

static app_state_t s_state = APP_STATE_INIT;
static bool s_force_update = false;

void power_manager_init(void) {
  button_handler_init();

  // 초기 웨이크업 원인 확인
  wakeup_cause_t cause = power_manager_get_wakeup_cause();
  if (cause == WAKEUP_CAUSE_BUTTON) {
    s_force_update = true;
  }

  s_state = APP_STATE_ACTIVE;
  ESP_LOGI(TAG, "Power manager initialized");
}

app_state_t power_manager_get_state(void) {
  return s_state;
}

void power_manager_set_state(app_state_t state) {
  if (s_state != state) {
    ESP_LOGI(TAG, "State: %d -> %d", s_state, state);
    s_state = state;
  }
}

wakeup_cause_t power_manager_get_wakeup_cause(void) {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  switch (cause) {
  case ESP_SLEEP_WAKEUP_TIMER:
    return WAKEUP_CAUSE_TIMER;
  case ESP_SLEEP_WAKEUP_GPIO:
    return WAKEUP_CAUSE_BUTTON;
  case ESP_SLEEP_WAKEUP_UNDEFINED:
    return WAKEUP_CAUSE_NONE;
  default:
    return WAKEUP_CAUSE_OTHER;
  }
}

wakeup_cause_t power_manager_enter_sleep(void) {
  ESP_LOGI(TAG, "Entering light sleep...");

  // 타이머 웨이크업 설정
  esp_sleep_enable_timer_wakeup(APP_SLEEP_DURATION_US);

  // 슬립 진입
  esp_err_t ret = esp_light_sleep_start();

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Light sleep failed: %s", esp_err_to_name(ret));
    // 슬립 실패 시 딜레이로 대체
    for (int i = 0; i < 100; i++) {
      if (button_is_pressed()) {
        return WAKEUP_CAUSE_BUTTON;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    return WAKEUP_CAUSE_TIMER;
  }

  return power_manager_get_wakeup_cause();
}

void power_manager_enter_off_mode(void) {
  ESP_LOGI(TAG, "Entering OFF mode...");

  // 화면 업데이트
  display_service_show_power_off();
  vTaskDelay(pdMS_TO_TICKS(2000));
  display_service_sleep();

#if APP_ENABLE_BLE
  ble_server_pause();
#endif

  // OFF 루프 - 버튼 웨이크업만 허용
  while (1) {
    ESP_LOGI(TAG, "OFF mode. Waiting for button release...");
    button_wait_release();

    ESP_LOGI(TAG, "OFF mode. Press and hold to power ON...");
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_light_sleep_start();

    // 웨이크업 후 버튼 홀드 확인
    if (button_is_pressed()) {
      ESP_LOGI(TAG, "Button pressed. Checking for 3s hold...");

      if (button_wait_hold(APP_BUTTON_HOLD_TIME_MS)) {
        ESP_LOGI(TAG, "3 seconds reached! Powering ON...");
        esp_sleep_enable_timer_wakeup(APP_SLEEP_DURATION_US);
        s_state = APP_STATE_ACTIVE;
        s_force_update = true;
        return;
      }

      ESP_LOGI(TAG, "Short press ignored in OFF mode.");
    }
  }
}

bool power_manager_handle_button(void) {
  if (!button_is_pressed()) {
    return false;
  }

  // 디바운스
  vTaskDelay(pdMS_TO_TICKS(20));
  if (!button_is_pressed()) {
    return false;
  }

  ESP_LOGI(TAG, "Button pressed. Measuring hold...");

  if (button_wait_hold(APP_BUTTON_HOLD_TIME_MS)) {
    // 3초 홀드 = 전원 OFF
    ESP_LOGI(TAG, "Long press detected -> OFF mode");
    power_manager_enter_off_mode();
    return true;
  }

  // 짧은 누름 = 강제 업데이트
  ESP_LOGI(TAG, "Short press detected -> Force update");
  s_force_update = true;
  return true;
}

void power_manager_request_update(void) {
  s_force_update = true;
}

bool power_manager_consume_update_request(void) {
  bool result = s_force_update;
  s_force_update = false;
  return result;
}
