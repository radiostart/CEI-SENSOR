/**
 * @file battery_monitor.c
 * @brief Battery monitoring implementation for ESP32-C3/C2
 */

#include "battery_monitor.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "BATTERY";

// ADC: GPIO2 = ADC1_CH2, 12dB atten (0~2500mV)
// Battery divider 47k+47k (1:2) → max 4.2V battery = 2.1V at pin
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_2
#define BATTERY_ADC_UNIT ADC_UNIT_1
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool do_calibration = false;
static bool s_initialized = false;
static bool s_usb_gpio_initialized = false;

void battery_usb_gpio_init(void) {
  if (s_usb_gpio_initialized) return;

  // USB PGOOD GPIO 초기화 (BQ24075: LOW=USB OK, 47kΩ 외부 풀업)
  gpio_config_t pgood_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = (1ULL << APP_USB_PGOOD_PIN),
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
  };
  gpio_config(&pgood_conf);

  // light sleep 중에도 정상 GPIO 설정 유지 (입력 버퍼 활성 상태 보장)
  gpio_sleep_sel_dis(APP_USB_PGOOD_PIN);

  // USB 연결 시 light sleep에서 깨어나도록 wakeup 등록
  // GPIO10 LOW = USB 연결됨 → LOW_LEVEL로 wakeup 트리거
  gpio_wakeup_enable(APP_USB_PGOOD_PIN, GPIO_INTR_LOW_LEVEL);

  s_usb_gpio_initialized = true;
  ESP_LOGI(TAG, "USB PGOOD GPIO%d initialized (sleep_sel_dis + wakeup)", APP_USB_PGOOD_PIN);
}

void battery_monitor_init(void) {
  if (s_initialized) return;

  ESP_LOGI(TAG, "Initializing Battery Monitor (GPIO 2 / ADC1 CH2)...");

  // 1. ADC Unit Config
  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = BATTERY_ADC_UNIT,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc1_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(ret));
    adc1_handle = NULL;
    return;
  }

  // 2. ADC Channel Config
  adc_oneshot_chan_cfg_t config = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = BATTERY_ADC_ATTEN,
  };
  ret = adc_oneshot_config_channel(adc1_handle, BATTERY_ADC_CHANNEL, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ADC channel config failed: %s", esp_err_to_name(ret));
    adc_oneshot_del_unit(adc1_handle);
    adc1_handle = NULL;
    return;
  }

  // 3. Calibration Init (Curve Fitting)
  ESP_LOGI(TAG, "Setting up ADC calibration scheme...");
  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = BATTERY_ADC_UNIT,
      .atten = BATTERY_ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
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
  esp_err_t ret = adc_oneshot_read(adc1_handle, BATTERY_ADC_CHANNEL, &adc_raw);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(ret));
    return 0;
  }

  int voltage_mv = 0;
  if (do_calibration) {
    ret = adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage_mv);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "ADC calibration failed: %s", esp_err_to_name(ret));
      voltage_mv = adc_raw;  // fallback to raw
    }
  } else {
    // Fallback or approximate if cali failed.
    voltage_mv = adc_raw;  // fallback to raw if calibration failed
  }

  // 47k+47k divider (1:2): battery = pin voltage × 2
  uint32_t battery_voltage = voltage_mv * 2;

  ESP_LOGD(TAG, "Raw: %d, Pin Voltage: %d mV, Battery: %d mV", adc_raw,
           voltage_mv, (int)battery_voltage);

  return battery_voltage;
}

bool battery_is_usb_connected(void) {
  // BQ24075 PGOOD: Active LOW (open-drain with pull-up)
  // LOW = USB 전원 정상 (충전 가능)
  // HIGH = USB 미연결 또는 전원 불량
  //
  // light sleep 후 GPIO 입력 버퍼가 불안정할 수 있으므로
  // 방향을 재설정하고 안정화 대기 후 읽기
  gpio_set_direction(APP_USB_PGOOD_PIN, GPIO_MODE_INPUT);
  esp_rom_delay_us(100);  // 100µs 안정화 (비블로킹)
  return gpio_get_level(APP_USB_PGOOD_PIN) == 0;
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
