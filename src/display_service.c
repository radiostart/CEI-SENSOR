/**
 * @file display_service.c
 * @brief 디스플레이 서비스 구현 - Mellow Air
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

void display_service_update(const sensor_data_t *data, int battery_pct,
                            const process_context_t *ctx, uint32_t elapsed_sec) {
  ui_set_battery_level(battery_pct);
  ui_update_process(ctx, elapsed_sec);

  if (data != NULL && data->valid) {
    int16_t temp_x10 = (int16_t)(data->temperature * 10);
    int16_t hum_x10  = (int16_t)(data->humidity * 10);
    ui_update_from_sensors(temp_x10, hum_x10, data->high_temp_warn);
    ESP_LOGI(TAG, "Display: T=%d.%d H=%d.%d batt=%d warn=%d",
             temp_x10 / 10, temp_x10 % 10, hum_x10 / 10, hum_x10 % 10,
             battery_pct, data->high_temp_warn);
  } else {
    ESP_LOGW(TAG, "No valid sensor data, showing '--'");
    ui_set_sensor_invalid();
  }

  ui_render_partial();
}

void display_service_set_ble_connected(bool connected) {
  ui_set_ble_connected(connected);
}

void display_service_set_charging(bool charging) {
  ui_set_charging(charging);
}

void display_service_show_low_battery(void) {
  ESP_LOGI(TAG, "Showing low battery screen");
  ui_show_low_battery();
}

void display_service_show_power_off(void) {
  ESP_LOGI(TAG, "Showing power off screen");
  ui_show_power_off();
}

void display_service_show_ble_pairing(int remaining_sec) {
  ui_show_ble_pairing(remaining_sec);
}

void display_service_full_refresh(void) {
  ESP_LOGI(TAG, "Performing full refresh");
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
