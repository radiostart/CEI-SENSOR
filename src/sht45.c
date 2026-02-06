#include "sht45.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SHT45";
static i2c_port_t g_i2c_port = I2C_NUM_0;
static uint8_t s_sht45_addr =
    SHT45_I2C_ADDR_A; // Default to 0x44, updated by scan

// CRC-8 다항식: 0x31 (x^8 + x^5 + x^4 + 1)
#define CRC8_POLYNOMIAL 0x31
#define CRC8_INIT 0xFF

/**
 * @brief CRC-8 계산
 */
static uint8_t sht45_calculate_crc(const uint8_t *data, uint8_t len) {
  uint8_t crc = CRC8_INIT;

  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 8; bit > 0; --bit) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ CRC8_POLYNOMIAL;
      } else {
        crc = (crc << 1);
      }
    }
  }

  return crc;
}

bool sht45_check_crc(const uint8_t *data, uint8_t len, uint8_t crc) {
  return (sht45_calculate_crc(data, len) == crc);
}

esp_err_t sht45_init(const sht45_config_t *config) {
  if (config == NULL) {
    ESP_LOGE(TAG, "설정이 NULL입니다");
    return ESP_ERR_INVALID_ARG;
  }

  g_i2c_port = config->i2c_port;

  // I2C 마스터 설정
  i2c_config_t i2c_conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = config->sda_pin,
      .scl_io_num = config->scl_pin,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = config->clk_speed,
  };

  esp_err_t ret = i2c_param_config(config->i2c_port, &i2c_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C 파라미터 설정 실패: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = i2c_driver_install(config->i2c_port, I2C_MODE_MASTER, 0, 0, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C 드라이버 설치 실패: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "SHT-45 초기화 완료 (SDA: GPIO%d, SCL: GPIO%d)",
           config->sda_pin, config->scl_pin);

  // Scan address implicitly via soft reset later, need to call scan first
  // explicitly in main Or we can just default to 0x44 here.

  return ESP_OK;
}

esp_err_t sht45_scan(void) {
  esp_err_t ret;
  i2c_cmd_handle_t cmd;

  // Try 0x44 (AD0)
  cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (SHT45_I2C_ADDR_A << 1) | I2C_MASTER_WRITE, true);
  i2c_master_stop(cmd);
  ret = i2c_master_cmd_begin(g_i2c_port, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);

  if (ret == ESP_OK) {
    s_sht45_addr = SHT45_I2C_ADDR_A;
    ESP_LOGI(TAG, "SHT4x Sensor detected at address 0x44");
    return ESP_OK;
  }

  // Try 0x45 (AD1)
  cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (SHT45_I2C_ADDR_B << 1) | I2C_MASTER_WRITE, true);
  i2c_master_stop(cmd);
  ret = i2c_master_cmd_begin(g_i2c_port, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);

  if (ret == ESP_OK) {
    s_sht45_addr = SHT45_I2C_ADDR_B;
    ESP_LOGI(TAG, "SHT4x Sensor detected at address 0x45");
    return ESP_OK;
  }

  ESP_LOGW(TAG, "SHT4x Sensor NOT detected at 0x44 or 0x45");
  return ESP_ERR_NOT_FOUND;
}

esp_err_t sht45_soft_reset(void) {
  uint8_t cmd = SHT45_CMD_SOFT_RESET;

  i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
  i2c_master_start(cmd_handle);
  i2c_master_write_byte(cmd_handle, (s_sht45_addr << 1) | I2C_MASTER_WRITE,
                        true);
  i2c_master_write_byte(cmd_handle, cmd, true);
  i2c_master_stop(cmd_handle);

  esp_err_t ret =
      i2c_master_cmd_begin(g_i2c_port, cmd_handle, pdMS_TO_TICKS(1000));
  i2c_cmd_link_delete(cmd_handle);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "소프트 리셋 실패 (Addr 0x%02X): %s", s_sht45_addr,
             esp_err_to_name(ret));
    return ret;
  }

  vTaskDelay(pdMS_TO_TICKS(10)); // 리셋 대기
  ESP_LOGI(TAG, "소프트 리셋 완료");

  return ESP_OK;
}

esp_err_t sht45_read_temperature_humidity(sht45_data_t *data) {
  if (data == NULL) {
    ESP_LOGE(TAG, "데이터 포인터가 NULL입니다");
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t cmd = SHT45_CMD_MEASURE_HIGH;
  uint8_t rx_data[6] = {0}; // 온도(2B) + CRC(1B) + 습도(2B) + CRC(1B)

  // 측정 명령 전송
  i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
  i2c_master_start(cmd_handle);
  i2c_master_write_byte(cmd_handle, (s_sht45_addr << 1) | I2C_MASTER_WRITE,
                        true);
  i2c_master_write_byte(cmd_handle, cmd, true);
  i2c_master_stop(cmd_handle);

  esp_err_t ret =
      i2c_master_cmd_begin(g_i2c_port, cmd_handle, pdMS_TO_TICKS(1000));
  i2c_cmd_link_delete(cmd_handle);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "측정 명령 전송 실패 (Addr 0x%02X): %s", s_sht45_addr,
             esp_err_to_name(ret));
    return ret;
  }

  // 측정 완료 대기 (고정밀도: 최대 8.3ms)
  // FreeRTOS Tick이 10ms(100Hz) 단위일 경우 1 tick은 보장되지 않을 수 있으므로
  // 20ms로 넉넉하게 설정
  vTaskDelay(pdMS_TO_TICKS(20));

  // 데이터 읽기
  cmd_handle = i2c_cmd_link_create();
  i2c_master_start(cmd_handle);
  i2c_master_write_byte(cmd_handle, (s_sht45_addr << 1) | I2C_MASTER_READ,
                        true);
  i2c_master_read(cmd_handle, rx_data, 6, I2C_MASTER_LAST_NACK);
  i2c_master_stop(cmd_handle);

  ret = i2c_master_cmd_begin(g_i2c_port, cmd_handle, pdMS_TO_TICKS(1000));
  i2c_cmd_link_delete(cmd_handle);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "데이터 읽기 실패 (Addr 0x%02X): %s", s_sht45_addr,
             esp_err_to_name(ret));
    return ret;
  }

  // CRC 검증
  if (!sht45_check_crc(&rx_data[0], 2, rx_data[2])) {
    ESP_LOGE(TAG, "온도 CRC 검증 실패");
    return ESP_ERR_INVALID_CRC;
  }

  if (!sht45_check_crc(&rx_data[3], 2, rx_data[5])) {
    ESP_LOGE(TAG, "습도 CRC 검증 실패");
    return ESP_ERR_INVALID_CRC;
  }

  // 온도 변환: T = -45 + 175 * (raw / 65535)
  uint16_t temp_raw = (rx_data[0] << 8) | rx_data[1];
  data->temperature = -45.0f + 175.0f * ((float)temp_raw / 65535.0f);

  // 습도 변환: RH = -6 + 125 * (raw / 65535) (SHT4x Datasheet)
  uint16_t humi_raw = (rx_data[3] << 8) | rx_data[4];
  data->humidity = -6.0f + 125.0f * ((float)humi_raw / 65535.0f);

  // Clamp Humidity to 0.0 ~ 100.0
  if (data->humidity < 0.0f)
    data->humidity = 0.0f;
  if (data->humidity > 100.0f)
    data->humidity = 100.0f;

  ESP_LOGD(TAG, "온도: %.2f°C, 습도: %.2f%%", data->temperature,
           data->humidity);

  return ESP_OK;
}

esp_err_t sht45_read_serial_number(uint32_t *serial) {
  if (serial == NULL) {
    ESP_LOGE(TAG, "시리얼 번호 포인터가 NULL입니다");
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t cmd = SHT45_CMD_READ_SERIAL;
  uint8_t rx_data[6] = {0}; // 시리얼(4B) + CRC(2B)

  // 시리얼 번호 읽기 명령 전송
  i2c_cmd_handle_t cmd_handle = i2c_cmd_link_create();
  i2c_master_start(cmd_handle);
  i2c_master_write_byte(cmd_handle, (s_sht45_addr << 1) | I2C_MASTER_WRITE,
                        true);
  i2c_master_write_byte(cmd_handle, cmd, true);
  i2c_master_stop(cmd_handle);

  esp_err_t ret =
      i2c_master_cmd_begin(g_i2c_port, cmd_handle, pdMS_TO_TICKS(1000));
  i2c_cmd_link_delete(cmd_handle);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "시리얼 번호 명령 전송 실패 (Addr 0x%02X): %s", s_sht45_addr,
             esp_err_to_name(ret));
    return ret;
  }

  vTaskDelay(pdMS_TO_TICKS(10));

  // 데이터 읽기
  cmd_handle = i2c_cmd_link_create();
  i2c_master_start(cmd_handle);
  i2c_master_write_byte(cmd_handle, (s_sht45_addr << 1) | I2C_MASTER_READ,
                        true);
  i2c_master_read(cmd_handle, rx_data, 6, I2C_MASTER_LAST_NACK);
  i2c_master_stop(cmd_handle);

  ret = i2c_master_cmd_begin(g_i2c_port, cmd_handle, pdMS_TO_TICKS(1000));
  i2c_cmd_link_delete(cmd_handle);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "시리얼 번호 읽기 실패 (Addr 0x%02X): %s", s_sht45_addr,
             esp_err_to_name(ret));
    return ret;
  }

  // CRC 검증 (시리얼 번호는 2바이트씩 CRC 확인)
  if (!sht45_check_crc(&rx_data[0], 2, rx_data[2])) {
    ESP_LOGE(TAG, "시리얼 번호 CRC 검증 실패 (상위)");
    return ESP_ERR_INVALID_CRC;
  }

  if (!sht45_check_crc(&rx_data[3], 2, rx_data[5])) {
    ESP_LOGE(TAG, "시리얼 번호 CRC 검증 실패 (하위)");
    return ESP_ERR_INVALID_CRC;
  }

  *serial = ((uint32_t)rx_data[0] << 24) | ((uint32_t)rx_data[1] << 16) |
            ((uint32_t)rx_data[3] << 8) | rx_data[4];

  ESP_LOGI(TAG, "시리얼 번호: 0x%08X", (unsigned int)*serial);

  return ESP_OK;
}
