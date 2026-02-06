/**
 * @file i2c_manager.c
 * @brief I2C 버스 관리 구현
 */

#include "i2c_manager.h"
#include "app_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "I2C_MGR";
static bool s_initialized = false;

esp_err_t i2c_manager_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }

  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = APP_I2C_SDA_PIN,
      .scl_io_num = APP_I2C_SCL_PIN,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = APP_I2C_FREQ_HZ,
  };

  esp_err_t ret = i2c_param_config(APP_I2C_PORT, &conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = i2c_driver_install(APP_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
    return ret;
  }

  // 버스 안정화 대기
  vTaskDelay(pdMS_TO_TICKS(50));

  s_initialized = true;
  ESP_LOGD(TAG, "I2C initialized");
  return ESP_OK;
}

void i2c_manager_deinit(void) {
  if (!s_initialized) {
    return;
  }

  i2c_driver_delete(APP_I2C_PORT);
  s_initialized = false;
  ESP_LOGD(TAG, "I2C deinitialized");
}

bool i2c_manager_is_initialized(void) {
  return s_initialized;
}
