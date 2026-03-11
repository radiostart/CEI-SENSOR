/**
 * @file main.c
 * @brief Mellow Air 메인 애플리케이션
 *
 * 레이어드 아키텍처 기반 상태 머신
 * - sensor_service: SHT4x 읽기 (히터 포함)
 * - spiffs_logger: SPIFFS 링 버퍼 로깅
 * - process_context: NVS 공정 컨텍스트
 * - display_service: E-Paper UI
 * - ble_server: GATT Server (4 characteristics)
 */

#include "app_config.h"
#include "battery_monitor.h"
#include "button_handler.h"
#include "driver/gpio.h"
#include "ble_server.h"
#include "display_service.h"
#include "epd_ui.h"
#include "power_manager.h"
#include "process_context.h"
#include "sensor_service.h"
#include "spiffs_logger.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

// ============================================================
// 공정 컨텍스트 (전역)
// ============================================================
static process_context_t s_proc_ctx;
static int64_t s_boot_uptime_us = 0;       // 부팅 시 타이머 기준점
static uint32_t s_elapsed_at_boot_sec = 0; // 부팅 전 누적 경과 시간
static bool s_process_paused = false;      // 앱에서 일시정지 상태
static bool s_done_notified = false;      // 공정 완료 알림 플래그
static bool s_last_usb_connected = false;  // USB 연결 상태 추적
static uint8_t s_ble_stale_count = 0;     // 유령 연결 감지 카운터
static bool s_was_ble_connected = false;   // BLE 연결 해제 감지용
static bool s_last_sensor_valid = false;   // 센서 연결 상태 변화 감지용

// 현재 경과 시간 계산 (초)
// 로컬 타이머 항상 진행, 앱이 ELAPSED_SYNC 전송 시 로컬 기준 보정
static uint32_t get_elapsed_sec(void) {
  if (!s_proc_ctx.is_active || s_proc_ctx.start_time == 0) return 0;
  if (s_process_paused) return s_elapsed_at_boot_sec;

  uint32_t since_boot_sec = (uint32_t)(
      (esp_timer_get_time() - s_boot_uptime_us) / 1000000LL);
  return s_elapsed_at_boot_sec + since_boot_sec;
}

// 앱 ELAPSED_SYNC 수신 시 보정 (매 사이클 호출)
// - 기기 로컬 타이머가 기본 (매 10초 독립 진행)
// - 기기가 앱보다 뒤처질 때만 앱 값으로 보정 (앱 우선)
// - 앱이 뒤처지는 건 BLE 통신 지연(~10초)으로 정상 → 보정 안함
// - 일시정지/재개 전환 시에는 항상 앱 값으로 보정
static void sync_app_elapsed(void) {
#if APP_ENABLE_BLE
  uint32_t app_elapsed;
  bool app_paused;
  if (ble_server_consume_new_elapsed(&app_elapsed, &app_paused)) {
    // 일시정지 설정 전에 현재 로컬 elapsed 계산 (정확한 running 값)
    uint32_t local_elapsed = get_elapsed_sec();
    bool was_paused = s_process_paused;
    s_process_paused = app_paused;

    ESP_LOGI(TAG, "Elapsed sync: app=%us, local=%us, paused=%d",
             (unsigned)app_elapsed, (unsigned)local_elapsed, app_paused);

    // 보정 조건:
    // 1. 일시정지 → 앱 값으로 고정
    // 2. 재개 (paused→running) → 정지 시간 제외 위해 기준점 재설정
    // 3. 기기가 앱보다 뒤처짐 (app > local) → 앱 값으로 보정
    // 통신 지연으로 app < local인 경우는 정상 → 로컬 유지
    bool should_correct = app_paused
        || (was_paused && !app_paused)
        || (app_elapsed > local_elapsed);

    if (should_correct) {
      s_elapsed_at_boot_sec = app_elapsed;
      // BLE 수신 시각을 기준점으로 사용 → 처리 지연 자동 보상
      int64_t rx_time = ble_server_get_elapsed_sync_rx_time();
      s_boot_uptime_us = (rx_time > 0) ? rx_time : esp_timer_get_time();
      ESP_LOGI(TAG, "→ corrected to %us", (unsigned)app_elapsed);
    }
  }
#endif
}

// unix timestamp 계산
// 1) 공정 활성: start_time + elapsed (정확)
// 2) 시간 동기화 완료: boot_sec + offset (앱에서 수신한 unix 기준)
// 3) 미동기화: boot_sec (앱에서 필터링)
// SPIFFS 저장용: 항상 boot_sec (원시값) 반환
// → 전송 시 ble_server가 현재 offset으로 일괄 변환
// → resync해도 동일한 unix timestamp 보장
static uint32_t get_raw_timestamp(void) {
  return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

// 실시간 BLE 전송용: unix timestamp 반환 (앱에 직접 전달)
static uint32_t get_unix_timestamp(void) {
  if (s_proc_ctx.is_active && s_proc_ctx.start_time > 0) {
    return s_proc_ctx.start_time + get_elapsed_sec();
  }
  uint32_t boot_sec = get_raw_timestamp();
#if APP_ENABLE_BLE
  if (ble_server_has_time_sync()) {
    return (uint32_t)((int64_t)boot_sec + ble_server_get_time_offset());
  }
#endif
  return boot_sec;
}

// ============================================================
// 초기화
// ============================================================
static esp_err_t app_init_nvs(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS erasing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  return ret;
}

static void app_init_all(void) {
  // 배터리 모드: 로그 WARN으로 제한 (UART 절전)
  // USB 연결 시 INFO로 복원 (디버깅 편의)
  esp_log_level_set("*", ESP_LOG_WARN);
  ESP_LOGW(TAG, "====================================");
  ESP_LOGW(TAG, "  Mellow Air Starting...");
  ESP_LOGW(TAG, "====================================");

  s_boot_uptime_us = esp_timer_get_time();

  // NVS (power_manager가 RTC 변수 확인에 필요)
  ESP_ERROR_CHECK(app_init_nvs());

  // 전원 관리자 (딥슬립 복귀 체크를 최대한 빨리 실행)
  // SPIFFS/BLE 등 무거운 초기화 전에 버튼 홀드를 확인해야
  // 사용자가 짧게 눌러도 전원 ON 가능
  power_manager_init();

  // SPIFFS 로거
  if (spiffs_logger_init() != ESP_OK) {
    ESP_LOGW(TAG, "SPIFFS init failed (logging disabled)");
  }

  // 공정 컨텍스트 초기화 (부팅 시 항상 IDLE 상태로 시작)
  // 앱 연결 시 앱에서 진행 중인 공정이 PROCESS_CONFIG + ELAPSED_SYNC로 전송됨
  process_context_reset(&s_proc_ctx);
  s_elapsed_at_boot_sec = 0;

  // USB PGOOD GPIO 초기화 (light sleep wakeup 포함, ADC와 독립)
  battery_usb_gpio_init();
  s_last_usb_connected = battery_is_usb_connected();
  // USB 연결 시 로그 레벨 INFO로 전환 (디버깅 편의)
  if (s_last_usb_connected) {
    esp_log_level_set("*", ESP_LOG_INFO);
  }
  ESP_LOGW(TAG, "USB PGOOD: GPIO10=%d → usb_connected=%d",
           gpio_get_level(APP_USB_PGOOD_PIN), s_last_usb_connected);

  // 센서 서비스
  sensor_service_init();

  // 디스플레이 서비스
  display_service_init();

#if APP_ENABLE_BLE
  ESP_LOGI(TAG, "Initializing BLE GATT server...");
  if (ble_server_init() != ESP_OK) {
    ESP_LOGW(TAG, "BLE init failed (continuing without BLE)");
  }
#endif

  ESP_LOGI(TAG, "All systems initialized");
}

// ============================================================
// BLE 페어링 모드 (30초 Fast Advertising)
// ============================================================
#if APP_ENABLE_BLE
static void handle_ble_pairing_mode(void) {
  ESP_LOGI(TAG, "=== BLE PAIRING MODE (30s) ===");

  // 디스플레이 웨이크업 + 초기 페어링 화면
  display_service_wakeup();
  display_service_show_ble_pairing(30);

  // Fast advertising 시작
  ble_server_resume();

  int64_t start_us = esp_timer_get_time();
  const int duration_sec = 30;
  int last_shown_sec = duration_sec;

  while (1) {
    int elapsed_sec = (int)((esp_timer_get_time() - start_us) / 1000000LL);
    int remaining = duration_sec - elapsed_sec;

    // 타임아웃
    if (remaining <= 0) {
      ESP_LOGI(TAG, "Pairing mode timeout");
      break;
    }

    // 연결 완료
    if (ble_server_is_connected()) {
      ESP_LOGI(TAG, "BLE connected during pairing mode!");
      break;
    }

    // 버튼 누르면 페어링 모드 취소
    if (button_is_pressed()) {
      ESP_LOGI(TAG, "Pairing mode cancelled by button");
      button_wait_release();
      break;
    }

    // 5초마다 화면 갱신 (e-paper 부담 경감)
    if (remaining != last_shown_sec && (remaining % 5 == 0)) {
      display_service_show_ble_pairing(remaining);
      last_shown_sec = remaining;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // 페어링 모드 종료
  display_service_sleep();

  if (!ble_server_is_connected()) {
    // 미연결 시 원래 광고 모드로 복귀
    if (battery_is_usb_connected()) {
      ble_server_resume();
    } else {
      ble_server_pause();
    }
  }

  // 페어링 종료 후 즉시 화면 갱신
  power_manager_request_update();
  ESP_LOGI(TAG, "=== BLE PAIRING MODE END ===");
}
#endif

// ============================================================
// 메인 루프 핸들러
// ============================================================
static void handle_active_state(void) {
  sensor_data_t data;

  // OTA 진행 중이면 센서/디스플레이 처리 건너뛰고 OTA 폴링만 수행
#if APP_ENABLE_OTA
  if (ble_server_is_ota_active()) {
    static uint8_t s_ota_last_disp_pct = 0xFF;
    for (int t = 0; t < 10000; ) {
      if (power_manager_handle_button()) { /* 버튼 무시, 이벤트만 소비 */ }
      uint8_t ota_st, ota_pct;
      if (ble_server_process_ota(&ota_st, &ota_pct)) {
        // 상태 변경 또는 5% 단위로만 디스플레이 갱신
        bool disp_update = (ota_st != 2)  // 상태 변경 (READY/VERIFY/SUCCESS/ERROR)
            || (ota_pct / 5 != s_ota_last_disp_pct / 5);  // 5% 단위
        if (disp_update) {
          display_service_wakeup();
          ui_show_ota_progress(ota_st, ota_pct);
          s_ota_last_disp_pct = ota_pct;
        }
      }
      if (!ble_server_is_ota_active()) {
        s_ota_last_disp_pct = 0xFF;
        // ERROR 화면을 3초간 표시 후 측정 화면으로 복귀
        if (ota_st == 5) {  // OTA_STATE_ERROR
          display_service_wakeup();
          ui_show_ota_progress(ota_st, ota_pct);
          vTaskDelay(pdMS_TO_TICKS(3000));
        }
        ui_reset_ota_render();
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      t += 20;
    }
    return;
  }
#endif

  // BLE 페어링 모드 확인 (더블클릭)
#if APP_ENABLE_BLE
  if (power_manager_consume_pairing_request()) {
    handle_ble_pairing_mode();
    return;
  }

  // BLE 연결 해제 감지 → 앱 상태 정리, 일시정지 해제
  if (!ble_server_is_connected() && s_was_ble_connected) {
    uint32_t cur_elapsed = get_elapsed_sec();
    process_context_save_elapsed(cur_elapsed);
    ble_server_clear_app_elapsed();
    s_process_paused = false;  // 연결 해제 시 일시정지 해제
    ESP_LOGI(TAG, "BLE disconnect: elapsed=%us saved", (unsigned)cur_elapsed);
    s_was_ble_connected = false;
  }
  if (ble_server_is_connected()) s_was_ble_connected = true;
#endif

  ESP_LOGD(TAG, "GPIO10=%d usb=%d last_usb=%d",
           gpio_get_level(APP_USB_PGOOD_PIN),
           battery_is_usb_connected(),
           s_last_usb_connected);

  // 센서 읽기
  esp_err_t ret = sensor_service_read(&data);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Sensor read failed");
  }

  // SPIFFS 로깅 (매 사이클) — boot_sec 원시값 저장
  if (data.valid) {
    uint32_t ts = get_raw_timestamp();
    spiffs_logger_append(data.temperature, data.humidity, ts);
  }

  // BLE REALTIME_DATA: 앱 구독 중이면 매 사이클 전송 — unix timestamp 사용
#if APP_ENABLE_BLE
  if (data.valid && ble_server_is_connected()) {
    ble_server_notify_realtime(&data, get_unix_timestamp(), get_elapsed_sec());
  }
#endif

  // 앱이 새 공정 설정을 전송했는지 확인
#if APP_ENABLE_BLE
  {
    process_context_t new_ctx;
    if (ble_server_poll_process_config(&new_ctx)) {
      s_proc_ctx = new_ctx;
      s_done_notified = false;  // 새 공정 → 완료 플래그 리셋

      // 앱이 ELAPSED_SYNC를 함께 전송했으면 시작값으로 사용 (0 리셋 방지)
      uint32_t start_elapsed = 0;
      bool start_paused = false;
      if (ble_server_has_app_elapsed()) {
        ble_server_get_app_elapsed(&start_elapsed, &start_paused);
      }

      s_elapsed_at_boot_sec = start_elapsed;
      // BLE 수신 시각 기준 → 처리 지연 자동 보상
      int64_t rx_time = ble_server_get_elapsed_sync_rx_time();
      s_boot_uptime_us = (rx_time > 0) ? rx_time : esp_timer_get_time();
      s_process_paused = start_paused;
      process_context_save_elapsed(start_elapsed);
      process_context_save(&s_proc_ctx);
      power_manager_request_update();
      ESP_LOGI(TAG, "New process: '%s' (elapsed=%us)",
               s_proc_ctx.process_name, (unsigned)start_elapsed);
    }
  }

  // 앱 ELAPSED_SYNC 수신 → 로컬 기준 보정 (PROCESS_CONFIG 이후 처리)
  sync_app_elapsed();
#endif

  // 경과 시간 NVS 주기 갱신 (매 30사이클 ≈ 300초, 플래시 쓰기 절감)
  {
    static uint8_t elapsed_save_cnt = 0;
    if (++elapsed_save_cnt >= 30) {
      elapsed_save_cnt = 0;
      if (s_proc_ctx.is_active) {
        process_context_save_elapsed(get_elapsed_sec());
      }
    }
  }

  // 저전압 자동 종료 (30사이클 ≈ 300초마다 체크, 배터리 모드만)
  {
    static uint8_t batt_check_cnt = 0;
    if (++batt_check_cnt >= 30) {
      batt_check_cnt = 0;
      if (!battery_is_usb_connected()) {
        battery_monitor_init();
        uint32_t voltage = battery_read_voltage();
        battery_monitor_deinit();
        if (voltage > 0 && voltage < APP_BATTERY_LOW_MV) {
          ESP_LOGW(TAG, "Low battery (%lumV < %dmV), auto shutdown",
                   (unsigned long)voltage, APP_BATTERY_LOW_MV);
          display_service_wakeup();
          display_service_show_low_battery();
          vTaskDelay(pdMS_TO_TICKS(3000));
          display_service_sleep();
          power_manager_set_state(APP_STATE_OFF);
          return;
        }
      }
    }
  }

  // USB 연결 상태 변경 감지 → 즉시 갱신 트리거 + 로그 레벨 전환
  bool usb_now = battery_is_usb_connected();
  if (usb_now != s_last_usb_connected) {
    if (usb_now) {
      esp_log_level_set("*", ESP_LOG_INFO);  // USB: 디버깅용 INFO
    } else {
      esp_log_level_set("*", ESP_LOG_WARN);  // 배터리: 절전 WARN
    }
    ESP_LOGW(TAG, "USB %s", usb_now ? "connected" : "disconnected");
    s_last_usb_connected = usb_now;
    power_manager_request_update();
  }

  // 센서 연결 상태 변화 감지 (연결↔해제 전환 시 즉시 화면 갱신)
  bool sensor_state_changed = (data.valid != s_last_sensor_valid);
  s_last_sensor_valid = data.valid;
  if (sensor_state_changed) {
    ESP_LOGI(TAG, "Sensor state changed: %s",
             data.valid ? "connected" : "disconnected");
  }

  // 변화 확인 또는 강제 업데이트
  bool force_update = power_manager_consume_update_request();
  bool significant_change = sensor_service_is_significant_change(&data);
  bool high_temp = data.valid && data.high_temp_warn;

#if APP_ENABLE_BLE
  // BLE 미연결 시 advertising 활성화 (앱 탐색 허용)
  // USB: fast (100-200ms) — 전원 충분, 빠른 탐색 우선
  // 배터리: slow (800-1600ms) — 절전 우선
  if (!ble_server_is_connected()) {
    if (battery_is_usb_connected()) {
      ble_server_resume();
    } else {
      ble_server_resume_slow();
    }
  }
#endif

  // 공정 진행 중이면 매 사이클 화면 갱신 (진행률/타이머 반영)
  bool process_running = s_proc_ctx.is_active && s_proc_ctx.duration_min > 0;

  // 디스플레이 갱신 필요 여부 (센서 변화, 상태 전환, 공정 진행 시만)
  bool need_display = significant_change || force_update || high_temp
                      || process_running || sensor_state_changed;

  if (need_display) {
    ESP_LOGI(TAG, ">>> UPDATE (change=%d, force=%d, hightemp=%d, proc=%d, sensor=%d) <<<",
             significant_change, force_update, high_temp, process_running,
             sensor_state_changed);

    sensor_service_update_last(&data);

    // 디스플레이 웨이크업
    display_service_wakeup();

#if APP_ENABLE_BLE
    ble_server_resume();
    display_service_set_ble_connected(ble_server_is_connected());
#endif

    // 배터리 측정 + 충전 상태
    battery_monitor_init();
    int battery_pct = battery_get_percentage();
    bool usb_charging = battery_is_usb_connected();
    battery_monitor_deinit();
    display_service_set_charging(usb_charging);
    ESP_LOGI(TAG, "Battery: %d%% %s", battery_pct,
             usb_charging ? "(charging)" : "");

    // 디스플레이 업데이트 (센서 + 공정 컨텍스트)
    uint32_t elapsed = get_elapsed_sec();

    // 공정 완료 감지 → 전체 갱신 2회 깜빡임으로 시각적 알림
    {
      uint32_t dur_sec = (uint32_t)s_proc_ctx.duration_min * 60;
      bool is_done = s_proc_ctx.is_active && dur_sec > 0 && elapsed >= dur_sec;

      if (is_done && !s_done_notified) {
        s_done_notified = true;
        ESP_LOGI(TAG, "Process DONE → full refresh blink");
        display_service_full_refresh();
        vTaskDelay(pdMS_TO_TICKS(300));
        display_service_update(&data, battery_pct, &s_proc_ctx, elapsed);
        display_service_full_refresh();
        vTaskDelay(pdMS_TO_TICKS(300));
      }
    }

    display_service_update(&data, battery_pct, &s_proc_ctx, elapsed);

#if APP_ENABLE_BLE
    // 고온 경고 시 즉시 처리 후 복귀
    if (high_temp) {
      ble_server_pause();
      display_service_sleep();
      return;
    }

    // 일반 BLE 브로드캐스트 창 (1.5초, 동기화 중이면 타이머 정지)
    for (int t = 0; t < APP_BLE_BROADCAST_MS; ) {
      if (power_manager_handle_button()) {
        ble_server_pause();
        display_service_sleep();
        return;
      }
      ble_server_process_unsent();
      ble_server_process_deferred_mark();
      ble_server_process_clear_data();
#if APP_ENABLE_OTA
      {
        uint8_t ota_st, ota_pct;
        if (ble_server_process_ota(&ota_st, &ota_pct)) {
          display_service_wakeup();
          ui_show_ota_progress(ota_st, ota_pct);
        }
      }
#endif
      bool syncing = ble_server_is_syncing();
      bool ota_active = ble_server_is_ota_active();
      int delay = (syncing || ota_active) ? 20 : 100;
      vTaskDelay(pdMS_TO_TICKS(delay));
      if (!syncing && !ota_active) t += delay;
    }
    if (!ble_server_is_syncing() && !ble_server_is_ota_active()) ble_server_pause();
    vTaskDelay(pdMS_TO_TICKS(100));
#endif

    display_service_sleep();
  } else {
    ESP_LOGI(TAG, ">>> SKIP (no significant change) <<<");

#if APP_ENABLE_BLE
    // BLE 미연결: 절전 탐색 윈도우 (1.5초, slow 인터벌)
    if (!ble_server_is_connected()) {
      for (int i = 0; i < (APP_BLE_DISCOVERY_MS / 100); i++) {
        if (power_manager_handle_button()) {
          ble_server_pause();
          return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
#endif
  }

  // 슬립 전 BLE 관리
#if APP_ENABLE_BLE
  if (ble_server_is_connected()) {
    // 유령 연결 감지: 연결됐지만 앱이 구독하지 않으면 stale (OTA 중 제외)
    if (!ble_server_is_subscribed() && !ble_server_is_ota_active()) {
      s_ble_stale_count++;
      ESP_LOGI(TAG, "BLE connected but not subscribed (%d/3)", s_ble_stale_count);
      if (s_ble_stale_count >= 3) {
        ESP_LOGW(TAG, "Stale BLE connection detected, forcing disconnect");
        ble_server_disconnect();
        s_ble_stale_count = 0;
        // 연결 해제 후 정상 슬립/광고 흐름으로 진행
      }
    } else {
      s_ble_stale_count = 0;
    }

    // 아직 연결 중이면 딜레이로 유지
    if (ble_server_is_connected()) {
      ESP_LOGI(TAG, "BLE connected - skipping sleep (delay instead)");
      // UNSENT 동기화 중: 20ms 폴링 (BLE ACK 즉시 처리)
      // 평시: 100ms 폴링 (절전)
      int total_ms = (int)(APP_SLEEP_DURATION_US / 1000);
      for (int t = 0; t < total_ms; ) {
        if (power_manager_handle_button()) return;
        // 앱 구독 시작 → 즉시 다음 사이클로 (센서 데이터 즉시 전송)
        if (ble_server_consume_initial_sync()) {
          ESP_LOGI(TAG, "App subscribed → immediate cycle");
          break;
        }
        ble_server_process_unsent();
        ble_server_process_reset_sent();
        ble_server_process_deferred_mark();
        ble_server_process_clear_data();
#if APP_ENABLE_OTA
        {
          uint8_t ota_st, ota_pct;
          if (ble_server_process_ota(&ota_st, &ota_pct)) {
            display_service_wakeup();
            ui_show_ota_progress(ota_st, ota_pct);
          }
        }
#endif
        bool syncing = ble_server_is_syncing();
        bool ota_active = ble_server_is_ota_active();
        int delay = (syncing || ota_active) ? 20 : 100;
        vTaskDelay(pdMS_TO_TICKS(delay));
        // 동기화/OTA 중이면 타이머 정지 (다음 사이클 진입 방지 → watchdog 방지)
        if (!syncing && !ota_active) t += delay;
      }
      return; // 다음 사이클로
    }
  }

  if (battery_is_usb_connected()) {
    // USB 충전 중: CPU가 깨어있으므로 BLE fast 유지 (빠른 탐색)
    if (!ble_server_is_connected()) {
      ble_server_resume();
    }
  } else {
    // 배터리 모드: light sleep 진입 전 BLE 중지 필수
    ble_server_pause();
  }
#endif

  // 슬립 전 버튼 체크
  if (power_manager_handle_button()) {
    return;
  }

  // 슬립 진입 (적응형 주기)
  // - USB 연결: 항상 10초 (전력 여유, 센서 탈착 즉시 감지)
  // - 배터리 유휴 (공정 미실행 + BLE 미연결): 30초 (절전)
  // - 배터리 활성 (공정 실행 중 또는 BLE 연결): 10초
  bool is_idle = !battery_is_usb_connected()
                 && !s_proc_ctx.is_active && !ble_server_is_connected();
  uint64_t sleep_us = is_idle ? APP_SLEEP_IDLE_DURATION_US : APP_SLEEP_DURATION_US;
  wakeup_cause_t cause = power_manager_enter_sleep_us(sleep_us);

  if (cause == WAKEUP_CAUSE_BUTTON) {
    power_manager_request_update();
    // 슬립에서 버튼으로 깨어남 → 이미 놓여있어도 이벤트 처리 필요
    power_manager_handle_button_event();
    return;
  }
}

// ============================================================
// 메인 엔트리
// ============================================================
void app_main(void) {
  app_init_all();

  while (1) {
    app_state_t state = power_manager_get_state();

    switch (state) {
    case APP_STATE_ACTIVE:
      handle_active_state();
      break;

    case APP_STATE_OFF:
      power_manager_enter_off_mode();
      break;

    default:
      power_manager_set_state(APP_STATE_ACTIVE);
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
