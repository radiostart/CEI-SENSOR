/**
 * @file display_service.c
 * @brief 디스플레이 서비스 구현
 */

#include "display_service.h"
#include "epd_driver.h"
#include "epd_ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DISPLAY_SVC";

esp_err_t display_service_init(void) {
  int ret = ui_init();
  if (ret != 0) {
    ESP_LOGE(TAG, "UI init failed: %d", ret);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Display service initialized");
  return ESP_OK;
}

void display_service_update(const sensor_data_t *data, int battery_pct) {
  if (data == NULL || !data->valid) {
    ESP_LOGW(TAG, "Invalid sensor data, skipping display update");
    return;
  }

  // 배터리 레벨 설정
  ui_set_battery_level(battery_pct);

  // 센서 데이터를 UI 포맷으로 변환
  int16_t temp_x10 = (int16_t)(data->temperature * 10);
  int16_t hum_x10 = (int16_t)(data->humidity * 10);
  int16_t cess_int = (int16_t)data->cess;

  // UI 업데이트
  ui_update_from_sensors(temp_x10, hum_x10, cess_int, 50);
  ui_render_partial();

  ESP_LOGD(TAG, "Display updated: T=%d.%d, H=%d.%d, CESS=%d",
           temp_x10 / 10, temp_x10 % 10, hum_x10 / 10, hum_x10 % 10, cess_int);
}

void display_service_show_power_off(void) {
  ESP_LOGI(TAG, "Showing power off screen");
  ui_show_power_off();
}

void display_service_full_refresh(void) {
  ESP_LOGI(TAG, "Performing full refresh (ghosting removal)");
  
  // 전체 갱신 1회만 수행
  epd_clear(EPD_COLOR_WHITE);
  epd_refresh();
  
  ESP_LOGI(TAG, "Full refresh complete");
}

void display_service_sleep(void) {
  epd_sleep();
  ESP_LOGD(TAG, "Display sleep");
}

void display_service_wakeup(void) {
  epd_wakeup();
  ESP_LOGD(TAG, "Display wakeup");
}
