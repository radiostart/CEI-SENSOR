/**
 * @file i2c_manager.c
 * @brief I2C 버스 관리 구현
 */

#include "i2c_manager.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "I2C_MGR";
static bool s_initialized = false;

// I2C 버스 복구: SDA가 LOW에 고정된 경우 SCL 클럭으로 해제
static void i2c_bus_recover(void) {
  int sda = APP_I2C_SDA_PIN;
  int scl = APP_I2C_SCL_PIN;

  // GPIO 모드로 전환
  gpio_set_direction(sda, GPIO_MODE_INPUT);
  gpio_set_pull_mode(sda, GPIO_PULLUP_ONLY);
  gpio_set_direction(scl, GPIO_MODE_OUTPUT_OD);
  gpio_set_pull_mode(scl, GPIO_PULLUP_ONLY);

  // SDA가 HIGH이면 복구 불필요
  if (gpio_get_level(sda) == 1) return;

  ESP_LOGW(TAG, "SDA stuck LOW, recovering bus...");

  // SCL을 최대 9회 토글하여 슬레이브의 미완료 전송 해제
  for (int i = 0; i < 9; i++) {
    gpio_set_level(scl, 0);
    esp_rom_delay_us(5);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(5);
    if (gpio_get_level(sda) == 1) break;
  }

  // STOP 조건 생성 (SDA LOW→HIGH while SCL HIGH)
  gpio_set_direction(sda, GPIO_MODE_OUTPUT_OD);
  gpio_set_level(sda, 0);
  esp_rom_delay_us(5);
  gpio_set_level(scl, 1);
  esp_rom_delay_us(5);
  gpio_set_level(sda, 1);
  esp_rom_delay_us(5);

  ESP_LOGI(TAG, "Bus recovery done, SDA=%d", gpio_get_level(sda));
}

esp_err_t i2c_manager_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }

  // 드라이버 설치 전 버스 복구 (핫스왑 대응)
  i2c_bus_recover();

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
