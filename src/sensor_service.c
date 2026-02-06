/**
 * @file sensor_service.c
 * @brief 센서 데이터 수집 서비스 구현
 */

#include "sensor_service.h"
#include "cess_calculator.h"
#include "eml_calculator.h"
#include "i2c_manager.h"
#include "sht45.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "SENSOR_SVC";

// 마지막 측정값 (변화 감지용)
static float s_last_temp = -999.0f;
static float s_last_hum = -999.0f;

esp_err_t sensor_service_init(void) {
  ESP_LOGI(TAG, "Sensor service initialized");
  return ESP_OK;
}

esp_err_t sensor_service_read(sensor_data_t *data) {
  if (data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // 기본값 설정
  data->temperature = 0.0f;
  data->humidity = 0.0f;
  data->cess = 0.0f;
  data->eml = 0;
  data->valid = false;

#if APP_USE_DUMMY_SENSOR
  // 더미 데이터 (테스트용)
  data->temperature = 22.0f + ((float)(esp_random() % 100) / 10.0f - 5.0f);
  data->humidity = 50.0f + ((float)(esp_random() % 200) / 10.0f - 10.0f);
  data->valid = true;
#else
  // I2C 초기화
  esp_err_t ret = i2c_manager_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C init failed");
    return ret;
  }

  // 센서 스캔
  ret = sht45_scan();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Sensor scan failed, retrying...");
    vTaskDelay(pdMS_TO_TICKS(50));
    sht45_scan();
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
  data->humidity = sht_data.humidity;
  data->valid = true;
#endif

  // CESS 계산
  data->cess = (float)cess_calculate(data->temperature, data->humidity);

  // EML 계산
  data->eml = (int)eml_classify_moisture(data->temperature, data->humidity);

  ESP_LOGI(TAG, "Sensor: T=%.2f°C, H=%.2f%%, CESS=%.1f, EML=%d",
           data->temperature, data->humidity, data->cess, data->eml);

  return ESP_OK;
}

bool sensor_service_is_significant_change(const sensor_data_t *data) {
  if (data == NULL || !data->valid) {
    return false;
  }

  // 첫 측정
  if (s_last_temp < -900.0f) {
    return true;
  }

  // 온도 변화 확인
  float temp_diff = fabsf(data->temperature - s_last_temp);
  if (temp_diff >= APP_TEMP_CHANGE_THRESHOLD) {
    ESP_LOGD(TAG, "Temp change: %.2f >= %.2f", temp_diff, APP_TEMP_CHANGE_THRESHOLD);
    return true;
  }

  // 습도 변화 확인
  float hum_diff = fabsf(data->humidity - s_last_hum);
  if (hum_diff >= APP_HUM_CHANGE_THRESHOLD) {
    ESP_LOGD(TAG, "Humidity change: %.2f >= %.2f", hum_diff, APP_HUM_CHANGE_THRESHOLD);
    return true;
  }

  return false;
}

void sensor_service_update_last(const sensor_data_t *data) {
  if (data != NULL && data->valid) {
    s_last_temp = data->temperature;
    s_last_hum = data->humidity;
  }
}
