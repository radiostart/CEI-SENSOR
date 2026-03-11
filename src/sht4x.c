#include "sht4x.h"
#include "app_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SHT4x";
static i2c_port_t g_i2c_port = I2C_NUM_0;
static uint8_t s_sht4x_addr = SHT4X_I2C_ADDR_A;

// CRC-8 (polynomial 0x31)
static uint8_t sht4x_calculate_crc(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 8; bit > 0; --bit) {
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
  }
  return crc;
}

static bool check_crc(const uint8_t *data, uint8_t len, uint8_t crc) {
  return (sht4x_calculate_crc(data, len) == crc);
}

esp_err_t sht4x_scan(void) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (SHT4X_I2C_ADDR_A << 1) | I2C_MASTER_WRITE, true);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(g_i2c_port, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Sensor detected at 0x%02X", SHT4X_I2C_ADDR_A);
    return ESP_OK;
  }

  ESP_LOGW(TAG, "Sensor NOT detected at 0x%02X", SHT4X_I2C_ADDR_A);
  return ESP_ERR_NOT_FOUND;
}

esp_err_t sht4x_read_temperature_humidity(sht4x_data_t *data) {
  if (data == NULL) return ESP_ERR_INVALID_ARG;

  uint8_t cmd = SHT4X_CMD_MEASURE_HIGH;
  uint8_t rx_data[6] = {0};

  // Send measurement command
  i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
  i2c_master_start(cmd_handle);
  i2c_master_write_byte(cmd_handle, (s_sht4x_addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd_handle, cmd, true);
  i2c_master_stop(cmd_handle);

  esp_err_t ret = i2c_master_cmd_begin(g_i2c_port, cmd_handle, pdMS_TO_TICKS(1000));
  i2c_cmd_link_delete(cmd_handle);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Measure cmd failed (0x%02X): %s", s_sht4x_addr, esp_err_to_name(ret));
    return ret;
  }

  // Wait for measurement (high precision: max 8.3ms)
  vTaskDelay(pdMS_TO_TICKS(20));

  // Read data
  cmd_handle = i2c_cmd_link_create();
  i2c_master_start(cmd_handle);
  i2c_master_write_byte(cmd_handle, (s_sht4x_addr << 1) | I2C_MASTER_READ, true);
  i2c_master_read(cmd_handle, rx_data, 6, I2C_MASTER_LAST_NACK);
  i2c_master_stop(cmd_handle);

  ret = i2c_master_cmd_begin(g_i2c_port, cmd_handle, pdMS_TO_TICKS(1000));
  i2c_cmd_link_delete(cmd_handle);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Data read failed (0x%02X): %s", s_sht4x_addr, esp_err_to_name(ret));
    return ret;
  }

  // CRC verification
  if (!check_crc(&rx_data[0], 2, rx_data[2])) {
    ESP_LOGE(TAG, "Temperature CRC failed");
    return ESP_ERR_INVALID_CRC;
  }
  if (!check_crc(&rx_data[3], 2, rx_data[5])) {
    ESP_LOGE(TAG, "Humidity CRC failed");
    return ESP_ERR_INVALID_CRC;
  }

  // Convert: T = -45 + 175 * (raw / 65535)
  uint16_t temp_raw = (rx_data[0] << 8) | rx_data[1];
  data->temperature = -45.0f + 175.0f * ((float)temp_raw / 65535.0f);

  // Convert: RH = -6 + 125 * (raw / 65535)
  uint16_t humi_raw = (rx_data[3] << 8) | rx_data[4];
  data->humidity = -6.0f + 125.0f * ((float)humi_raw / 65535.0f);

  if (data->humidity < 0.0f) data->humidity = 0.0f;
  if (data->humidity > 100.0f) data->humidity = 100.0f;

  ESP_LOGD(TAG, "T=%.2fC H=%.2f%%", data->temperature, data->humidity);
  return ESP_OK;
}

esp_err_t sht4x_run_heater_high_power(void) {
  uint8_t cmd = SHT4X_CMD_HEATER_200MW_01S;

  i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
  i2c_master_start(cmd_handle);
  i2c_master_write_byte(cmd_handle, (s_sht4x_addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd_handle, cmd, true);
  i2c_master_stop(cmd_handle);

  esp_err_t ret = i2c_master_cmd_begin(g_i2c_port, cmd_handle, pdMS_TO_TICKS(200));
  i2c_cmd_link_delete(cmd_handle);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Heater cmd failed: %s", esp_err_to_name(ret));
    return ret;
  }

  // Wait for heater (100ms + margin)
  vTaskDelay(pdMS_TO_TICKS(110));

  // Read and discard heater measurement
  uint8_t rx_data[6] = {0};
  cmd_handle = i2c_cmd_link_create();
  i2c_master_start(cmd_handle);
  i2c_master_write_byte(cmd_handle, (s_sht4x_addr << 1) | I2C_MASTER_READ, true);
  i2c_master_read(cmd_handle, rx_data, 6, I2C_MASTER_LAST_NACK);
  i2c_master_stop(cmd_handle);
  i2c_master_cmd_begin(g_i2c_port, cmd_handle, pdMS_TO_TICKS(200));
  i2c_cmd_link_delete(cmd_handle);

  ESP_LOGI(TAG, "Heater done (200mW/100ms)");
  return ESP_OK;
}
