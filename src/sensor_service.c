/**
 * @file sensor_service.c
 * @brief 센서 데이터 수집 서비스 구현 (BakeTrack)
 *
 * - SHT45 온습도 읽기
 * - 6시간 주기 히터 루틴 (결로 방지)
 * - 80°C 이상 고온 경고 플래그 설정
 */

#include "sensor_service.h"
#include "i2c_manager.h"
#include "sht45.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "SENSOR_SVC";

// 마지막 측정값 (변화 감지용)
static float s_last_temp = -999.0f;
static float s_last_hum  = -999.0f;
// 최초 업데이트 플래그
static bool s_first_update_done = false;
// 센서 주소 스캔 캐시
static bool s_sensor_found = false;
// 히터 마지막 실행 시각 (us)
static int64_t s_last_heater_us = 0;

// 6시간 주기 (마이크로초)
#define HEATER_INTERVAL_US (6LL * 3600LL * 1000000LL)

esp_err_t sensor_service_init(void) {
  ESP_LOGI(TAG, "Sensor service initialized");
  return ESP_OK;
}

esp_err_t sensor_service_read(sensor_data_t *data) {
  if (data == NULL) return ESP_ERR_INVALID_ARG;

  data->temperature    = 0.0f;
  data->humidity       = 0.0f;
  data->valid          = false;
  data->high_temp_warn = false;

#if APP_USE_DUMMY_SENSOR
  data->temperature = 22.0f + ((float)(esp_random() % 100) / 10.0f - 5.0f);
  data->humidity    = 60.0f + ((float)(esp_random() % 200) / 10.0f - 10.0f);
  data->valid       = true;
#else
  // I2C 초기화
  esp_err_t ret = i2c_manager_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C init failed");
    return ret;
  }

  // 센서 스캔 (최초 1회)
  if (!s_sensor_found) {
    ret = sht45_scan();
    if (ret != ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(50));
      ret = sht45_scan();
    }
    if (ret == ESP_OK) s_sensor_found = true;
  }

  // 6시간 주기 히터 루틴 (결로 방지)
  int64_t now_us = esp_timer_get_time();
  bool run_heater = (s_last_heater_us == 0) ||
                    ((now_us - s_last_heater_us) >= HEATER_INTERVAL_US);

  if (run_heater && s_sensor_found) {
    ESP_LOGI(TAG, "Running heater (anti-condensation)...");
    ret = sht45_run_heater_high_power();
    if (ret == ESP_OK) {
      s_last_heater_us = esp_timer_get_time();
      // I2C 해제 후 2초 안정화 대기
      i2c_manager_deinit();
      vTaskDelay(pdMS_TO_TICKS(2000));
      // I2C 재초기화
      ret = i2c_manager_init();
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C re-init after heater failed");
        return ret;
      }
    }
  }

  // 센서 읽기
  sht45_data_t sht_data;
  ret = sht45_read_temperature_humidity(&sht_data);

  // I2C 해제 (저전력)
  i2c_manager_deinit();

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Sensor read failed: %s", esp_err_to_name(ret));
    return ret;
  }

  data->temperature = sht_data.temperature;
  data->humidity    = sht_data.humidity;
  data->valid       = true;
#endif

  // 고온 경고 (80°C 이상)
  if (data->temperature >= APP_HIGH_TEMP_WARN_THRESHOLD) {
    data->high_temp_warn = true;
    ESP_LOGW(TAG, "HIGH TEMP WARNING: %.1f C", data->temperature);
  }

  ESP_LOGI(TAG, "Sensor: T=%.2fC H=%.2f%% warn=%d",
           data->temperature, data->humidity, data->high_temp_warn);
  return ESP_OK;
}

bool sensor_service_is_significant_change(const sensor_data_t *data) {
  // 최초 1회는 무조건 업데이트
  if (!s_first_update_done) {
    s_first_update_done = true;
    return true;
  }

  if (data == NULL || !data->valid) return false;

  // 고온 경고는 즉시 업데이트
  if (data->high_temp_warn) return true;

  if (s_last_temp < -900.0f) return true;

  float temp_diff = fabsf(data->temperature - s_last_temp);
  if (temp_diff >= APP_TEMP_CHANGE_THRESHOLD) return true;

  float hum_diff = fabsf(data->humidity - s_last_hum);
  if (hum_diff >= APP_HUM_CHANGE_THRESHOLD) return true;

  return false;
}

void sensor_service_update_last(const sensor_data_t *data) {
  if (data != NULL && data->valid) {
    s_last_temp = data->temperature;
    s_last_hum  = data->humidity;
  }
}
