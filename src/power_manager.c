/**
 * @file power_manager.c
 * @brief 전원 및 슬립 관리 구현
 */

#include "power_manager.h"
#include "app_config.h"
#include "battery_monitor.h"
#include "button_handler.h"
#include "display_service.h"
#include "sensor_service.h"
#include "driver/gpio.h"
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
static bool s_pairing_requested = false;

// 딥슬립 OFF 모드 플래그 (RTC 메모리: 딥슬립에서도 유지)
static RTC_DATA_ATTR bool s_off_mode_deep_sleep = false;

// 더블클릭 감지 (400ms 윈도우)
static bool wait_for_double_click(void) {
  for (int i = 0; i < 20; i++) { // 20 × 20ms = 400ms
    vTaskDelay(pdMS_TO_TICKS(20));
    if (button_is_pressed()) {
      vTaskDelay(pdMS_TO_TICKS(20)); // 디바운스
      if (button_is_pressed()) {
        button_wait_release();
        return true;
      }
    }
  }
  return false;
}

// 딥슬립 OFF 모드 진입 (GPIO3 버튼 웨이크업만)
// 주의: GPIO10(USB_PGOOD)은 RTC GPIO가 아님 (ESP32-C3은 GPIO0-5만 RTC)
//       딥슬립 웨이크업 마스크에 포함 불가 → 버튼으로만 기동
static void enter_off_deep_sleep(void) {
  s_off_mode_deep_sleep = true;
  esp_deep_sleep_enable_gpio_wakeup(
      (1ULL << APP_BUTTON_PIN),
      ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
  // 칩 리셋 — 이후 코드 실행 안됨
}

void power_manager_init(void) {
  button_handler_init();

  // 딥슬립 OFF 모드에서 깨어난 경우 확인 (버튼 웨이크업만 가능)
  if (s_off_mode_deep_sleep) {
    s_off_mode_deep_sleep = false;

    // 딥슬립 웨이크업 원인 확인 (GPIO = 버튼)
    // 부팅에 ~300ms 걸리므로 button_is_pressed()로 확인하면 짧은 누름을 놓침
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup == ESP_SLEEP_WAKEUP_GPIO) {
      ESP_LOGI(TAG, "OFF deep sleep → button wakeup, power ON");

      // 배터리 전압 확인
      battery_usb_gpio_init();
      battery_monitor_init();
      uint32_t voltage = battery_read_voltage();
      battery_monitor_deinit();

      if (voltage > 0 && voltage < APP_BATTERY_RECOVERY_MV) {
        ESP_LOGW(TAG, "Battery too low (%lumV) → back to deep sleep",
                 (unsigned long)voltage);
        enter_off_deep_sleep();
      }

      // 버튼이 아직 눌려있으면 릴리즈 대기 (깔끔한 전원 ON)
      if (button_is_pressed()) {
        button_wait_release();
      }
      ESP_LOGI(TAG, "Power ON confirmed");
    } else {
      // 스퓨리어스 웨이크업 → 다시 딥슬립
      ESP_LOGI(TAG, "OFF deep sleep → spurious → back to sleep");
      enter_off_deep_sleep();
    }
  }

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

wakeup_cause_t power_manager_enter_sleep_us(uint64_t duration_us) {
  if (duration_us == 0) duration_us = APP_SLEEP_DURATION_US;

  // USB 연결 시 light sleep 대신 딜레이 (JTAG 플래시 안정성 보장)
  if (battery_is_usb_connected()) {
    ESP_LOGI(TAG, "USB connected - skipping light sleep (using delay)");
    int loops = (int)(duration_us / 100000);
    for (int i = 0; i < loops; i++) {
      if (button_is_pressed()) {
        return WAKEUP_CAUSE_BUTTON;
      }
      // USB 제거 시 즉시 반환 → 충전 표시 빠른 갱신
      if (!battery_is_usb_connected()) {
        ESP_LOGI(TAG, "USB disconnected during delay");
        return WAKEUP_CAUSE_TIMER;
      }
#if APP_ENABLE_BLE
      // 앱 구독 시작 → 즉시 사이클 시작 (센서 데이터 전송)
      if (ble_server_consume_initial_sync()) {
        ESP_LOGI(TAG, "BLE subscribed during sleep → immediate wakeup");
        return WAKEUP_CAUSE_TIMER;
      }
#endif
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    return WAKEUP_CAUSE_TIMER;
  }

  ESP_LOGI(TAG, "Entering light sleep (%llu ms)...",
           (unsigned long long)(duration_us / 1000));

  // 타이머 웨이크업 설정
  esp_sleep_enable_timer_wakeup(duration_us);

  // 슬립 진입
  esp_err_t ret = esp_light_sleep_start();

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Light sleep failed: %s", esp_err_to_name(ret));
    // 슬립 실패 시 딜레이로 대체
    int fallback_loops = (int)(duration_us / 100000);
    if (fallback_loops < 10) fallback_loops = 10;
    for (int i = 0; i < fallback_loops; i++) {
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

  // 화면 업데이트 (슬립 상태일 수 있으므로 먼저 웨이크업)
  display_service_wakeup();
  display_service_show_power_off();
  vTaskDelay(pdMS_TO_TICKS(2000));
  display_service_sleep();

#if APP_ENABLE_BLE
  ble_server_pause();
#endif

  button_wait_release();

  // USB 연결 여부와 무관하게 즉시 딥슬립 진입
  // GPIO3(버튼) LOW 웨이크업만 (GPIO10은 RTC GPIO 아님)
  // 깨어나면 칩 리셋 → app_main() 재시작 → power_manager_init()에서 처리
  ESP_LOGI(TAG, "Entering deep sleep OFF mode...");
  enter_off_deep_sleep();
  // 칩 리셋 — 이후 코드 실행 안됨
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

  // 짧은 누름 감지 — 더블클릭 대기 (400ms 윈도우)
  if (wait_for_double_click()) {
    ESP_LOGI(TAG, "Double-click detected -> BLE pairing mode");
    s_pairing_requested = true;
  } else {
    ESP_LOGI(TAG, "Short press detected -> Force update");
    s_force_update = true;
    sensor_service_force_rescan();
  }
  return true;
}

void power_manager_handle_button_event(void) {
  // 슬립에서 버튼으로 깨어난 경우 호출
  // 버튼이 이미 놓여있어도 이벤트로 처리 (GPIO 레벨 의존 없음)

  // 아직 눌려있으면 기존 handle_button 로직 사용
  if (button_is_pressed()) {
    power_manager_handle_button();
    return;
  }

  // 이미 놓여있음 → 짧은 누름으로 간주, 더블클릭 대기
  ESP_LOGI(TAG, "Button wakeup (released). Waiting for double-click...");
  if (wait_for_double_click()) {
    ESP_LOGI(TAG, "Double-click detected -> BLE pairing mode");
    s_pairing_requested = true;
  } else {
    ESP_LOGI(TAG, "Short press detected -> Force update");
    s_force_update = true;
    sensor_service_force_rescan();
  }
}

void power_manager_request_update(void) {
  s_force_update = true;
}

bool power_manager_consume_update_request(void) {
  bool result = s_force_update;
  s_force_update = false;
  return result;
}

bool power_manager_consume_pairing_request(void) {
  bool result = s_pairing_requested;
  s_pairing_requested = false;
  return result;
}
