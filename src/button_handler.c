/**
 * @file button_handler.c
 * @brief 버튼 입력 처리 구현
 */

#include "button_handler.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUTTON";

void button_handler_init(void) {
  gpio_reset_pin(APP_BUTTON_PIN);

  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = (1ULL << APP_BUTTON_PIN),
      .pull_down_en = 0,
      .pull_up_en = 1,  // 내부 풀업 활성화
  };
  gpio_config(&io_conf);

  // 슬립 웨이크업 설정
  esp_sleep_enable_gpio_wakeup();
  gpio_wakeup_enable(APP_BUTTON_PIN, GPIO_INTR_LOW_LEVEL);

  ESP_LOGI(TAG, "Button handler initialized (GPIO %d)", APP_BUTTON_PIN);
}

bool button_is_pressed(void) {
  return gpio_get_level(APP_BUTTON_PIN) == 0;
}

button_event_t button_poll_event(void) {
  if (!button_is_pressed()) {
    return BUTTON_EVENT_NONE;
  }

  // 디바운스
  vTaskDelay(pdMS_TO_TICKS(20));
  if (!button_is_pressed()) {
    return BUTTON_EVENT_NONE;
  }

  // 홀드 시간 측정
  uint32_t hold_ms = 0;
  while (button_is_pressed()) {
    vTaskDelay(pdMS_TO_TICKS(100));
    hold_ms += 100;

    if (hold_ms >= APP_BUTTON_HOLD_TIME_MS) {
      return BUTTON_EVENT_LONG;
    }
  }

  return BUTTON_EVENT_SHORT;
}

bool button_wait_hold(uint32_t hold_ms) {
  uint32_t elapsed = 0;

  while (button_is_pressed() && elapsed < hold_ms) {
    vTaskDelay(pdMS_TO_TICKS(100));
    elapsed += 100;

    // 1초마다 로그
    if (elapsed % 1000 == 0) {
      ESP_LOGI(TAG, "Holding... %lu ms", (unsigned long)elapsed);
    }
  }

  return elapsed >= hold_ms;
}

void button_wait_release(void) {
  while (button_is_pressed()) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
