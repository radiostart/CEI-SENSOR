/**
 * @file battery_monitor.c
 * @brief Battery monitoring implementation for ESP32-C3/C2
 */

#include "battery_monitor.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "BATTERY";

// ADC Configuration
// GPIO 2 is ADC1 Channel 2 on ESP32-C3/C2
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_2
#define BATTERY_ADC_UNIT ADC_UNIT_1
#define BATTERY_ADC_ATTEN                                                      \
  ADC_ATTEN_DB_12 // 11dB or 12dB for full range (up to ~3.1V on standard ESP32,
                  // C3 supports up to 2.5V+ depending on config)
// Wait, C3 11dB -> up to ~2500mV.
// We have a divider of 1/2.
// Max battery 4.2V -> 2.1V at pin.
// 2.1V is within range of 11dB/12dB attenuation (approx 0 ~ 2500mV or 3100mV on
// some) ESP32-C3 ADC1 Attenuation: 11dB: 150mV ~ 2450mV recommended. 2.1V fits
// perfectly.

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool do_calibration = false;
static bool s_initialized = false;

void battery_monitor_init(void) {
  if (s_initialized) return;

  ESP_LOGI(TAG, "Initializing Battery Monitor (GPIO 2 / ADC1 CH2)...");

  // 1. ADC Unit Config
  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = BATTERY_ADC_UNIT,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

  // 2. ADC Channel Config
  adc_oneshot_chan_cfg_t config = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = BATTERY_ADC_ATTEN,
  };
  ESP_ERROR_CHECK(
      adc_oneshot_config_channel(adc1_handle, BATTERY_ADC_CHANNEL, &config));

  // 3. Calibration Init (Curve Fitting)
  ESP_LOGI(TAG, "Setting up ADC calibration scheme...");
  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = BATTERY_ADC_UNIT,
      .atten = BATTERY_ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  esp_err_t ret =
      adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
  if (ret == ESP_OK) {
    do_calibration = true;
    ESP_LOGI(TAG, "Calibration Success");
  } else {
    ESP_LOGE(TAG, "Calibration Fail or Not Supported. Using raw.");
  }

  s_initialized = true;
}

void battery_monitor_deinit(void) {
  if (!s_initialized) return;

  if (do_calibration && adc_cali_handle) {
    adc_cali_delete_scheme_curve_fitting(adc_cali_handle);
    adc_cali_handle = NULL;
    do_calibration = false;
  }
  if (adc1_handle) {
    adc_oneshot_del_unit(adc1_handle);
    adc1_handle = NULL;
  }

  s_initialized = false;
  ESP_LOGD(TAG, "Battery monitor deinitialized");
}

uint32_t battery_read_voltage(void) {
  if (adc1_handle == NULL) {
    return 0;
  }

  int adc_raw;
  ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BATTERY_ADC_CHANNEL, &adc_raw));

  int voltage_mv = 0;
  if (do_calibration) {
    ESP_ERROR_CHECK(
        adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage_mv));
  } else {
    // Fallback or approximate if cali failed.
    // Usually calibration works on C3.
    // Rough estimate if raw: (raw / 4095) * 2500 ?
    voltage_mv =
        adc_raw; // Just return raw if cali fails (should ideally map it)
  }

  // Divider: 100k + 100k -> Ratio 1/2.
  // Pin Voltage = Battery * (100 / (100+100)) = Battery / 2
  // Battery = Pin * 2
  uint32_t battery_voltage = voltage_mv * 2;

  ESP_LOGD(TAG, "Raw: %d, Pin Voltage: %d mV, Battery: %d mV", adc_raw,
           voltage_mv, (int)battery_voltage);

  return battery_voltage;
}

int battery_get_percentage(void) {
  uint32_t voltage = battery_read_voltage();

  // LiPo 비선형 방전 곡선 기반 LUT (Look-Up Table)
  // 실제 LiPo 배터리 방전 특성을 반영
  static const struct {
    uint32_t mv;
    int pct;
  } lut[] = {
      {4200, 100}, {4100, 90}, {4000, 80}, {3900, 70}, {3800, 60}, {3700, 50},
      {3600, 35},  {3500, 20}, {3400, 10}, {3300, 5},  {3200, 0},
  };
  static const int lut_size = sizeof(lut) / sizeof(lut[0]);

  // 범위 초과 처리
  if (voltage >= lut[0].mv)
    return 100;
  if (voltage <= lut[lut_size - 1].mv)
    return 0;

  // LUT에서 선형 보간
  for (int i = 0; i < lut_size - 1; i++) {
    if (voltage >= lut[i + 1].mv) {
      // voltage가 lut[i+1].mv ~ lut[i].mv 사이에 있음
      uint32_t v_range = lut[i].mv - lut[i + 1].mv;
      int pct_range = lut[i].pct - lut[i + 1].pct;
      return lut[i + 1].pct +
             (int)((voltage - lut[i + 1].mv) * pct_range / v_range);
    }
  }

  return 0;
}
