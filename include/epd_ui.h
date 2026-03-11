/**
 * @file epd_ui.h
 * @brief E-Paper UI 레이어 - Mellow Air 발효 모니터링 인터페이스
 *
 * 2.13인치 BW E-Paper용 세로형 UI (122 x 250 픽셀)
 *
 * 레이아웃:
 * ┌─────────────────────────┐ Y=0
 * │ TOP BAR  [BT] [BAT]     │ 24px
 * ├─────────────────────────┤ Y=25
 * │ TEMP         HUMIDITY   │
 * │ 24.5°C       68.0%      │ 86px (VALUES)
 * ├─────────────────────────┤ Y=114
 * │ Target                   │
 * │ 25.0C / 70%         (L)  │ 80px (TARGET/DIFF)
 * │ Diff                     │
 * │ -0.5C / -2.0%       (L)  │
 * ├─────────────────────────┤ Y=197
 * │ PROOFING   42:30 left   │
 * │ [=====>       ] 55%     │ 53px (PROOFING)
 * └─────────────────────────┘ Y=250
 */

#ifndef EPD_UI_H
#define EPD_UI_H

#include "app_config.h"
#include "epd_driver.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 레이아웃 상수
// ============================================================
#ifndef EPD_PANEL_WIDTH
#error "EPD_PANEL_WIDTH not defined in epd_driver.h"
#endif

#define UI_WIDTH          EPD_PANEL_WIDTH  // 122
#define UI_HEIGHT         EPD_HEIGHT       // 250
#define UI_PADDING_X      0
#define UI_CONTENT_WIDTH  (UI_WIDTH - (UI_PADDING_X * 2))
#define UI_DIVIDER_HEIGHT 3

// 1. Top Bar (배터리 + BLE 아이콘)
#define UI_TOP_BAR_Y      0
#define UI_TOP_BAR_HEIGHT 24

// 2. Values Section (현재 온도 + 습도)
#define UI_VALUES_Y      (UI_TOP_BAR_Y + UI_TOP_BAR_HEIGHT + 1)  // 25
#define UI_VALUES_HEIGHT 86

// 3. Target/Diff Section (목표값 + 편차)
#define UI_TARGET_Y      (UI_VALUES_Y + UI_VALUES_HEIGHT + UI_DIVIDER_HEIGHT)  // 114
#define UI_TARGET_HEIGHT 80

// 4. Proofing Section (공정 타이머 + 프로그레스 바)
#define UI_PROOF_Y      (UI_TARGET_Y + UI_TARGET_HEIGHT + UI_DIVIDER_HEIGHT)  // 197
#define UI_PROOF_HEIGHT 53

#if ((UI_PROOF_Y + UI_PROOF_HEIGHT) > UI_HEIGHT)
#error "UI Layout exceeds Display Height (250px)!"
#endif

// ============================================================
// UI 상태 구조체
// ============================================================
typedef struct {
  // 센서 데이터
  int16_t temperature_x10;  // 온도 × 10 (예: 245 = 24.5°C)
  int16_t humidity_x10;     // 습도 × 10 (예: 680 = 68.0%)
  bool has_valid_data;       // 유효한 센서 데이터 여부
  bool high_temp_warn;       // 80°C 이상 고온 경고

  // 공정 목표값
  int16_t target_temp_x10;  // 목표 온도 × 10
  int16_t target_humi_x10;  // 목표 습도 × 10
  int16_t tol_temp_x10;     // 온도 허용 편차 × 10
  int16_t tol_humi_x10;     // 습도 허용 편차 × 10

  // 편차 (현재 - 목표)
  int16_t diff_temp_x10;    // 온도 차이 × 10
  int16_t diff_humi_x10;    // 습도 차이 × 10
  bool temp_out_of_range;   // 온도 편차 초과
  bool humi_out_of_range;   // 습도 편차 초과

  // 공정 타이머
  bool process_active;     // 공정 설정 여부
  char process_name[32];   // 공정명
  uint32_t elapsed_sec;    // 경과 시간 (초)
  uint32_t duration_sec;   // 전체 공정 시간 (초)
  uint8_t progress_pct;    // 진행률 (0~100)

  // 시스템 상태
  bool is_ble_connected;
  int battery_level;
  bool is_charging;        // USB 충전 중
} ui_state_t;

// ============================================================
// API
// ============================================================

/** @brief UI 시스템 초기화 */
int ui_init(void);

/** @brief 부분 렌더링 (센서+공정 데이터) */
void ui_render_partial(void);

/**
 * @brief 센서 데이터로 UI 상태 업데이트
 * @param temp_x10      온도 × 10
 * @param humidity_x10  습도 × 10
 * @param high_temp_warn 고온 경고 플래그
 */
void ui_update_from_sensors(int16_t temp_x10, int16_t humidity_x10,
                            bool high_temp_warn);

/**
 * @brief 공정 컨텍스트로 UI 상태 업데이트
 * @param ctx         공정 컨텍스트 (NULL = 미설정)
 * @param elapsed_sec 경과 시간 (초)
 */
void ui_update_process(const process_context_t *ctx, uint32_t elapsed_sec);

/** @brief 배터리 레벨 설정 (0~100) */
void ui_set_battery_level(int level);

/** @brief BLE 연결 상태 설정 */
void ui_set_ble_connected(bool connected);

/** @brief 충전 상태 설정 */
void ui_set_charging(bool charging);

/** @brief 센서 데이터 무효 설정 (화면에 '--' 표시) */
void ui_set_sensor_invalid(void);

/** @brief 저전압 경고 화면 표시 */
void ui_show_low_battery(void);

/** @brief 전원 오프 화면 표시 */
void ui_show_power_off(void);

/** @brief BLE 페어링 모드 화면 표시 (카운트다운) */
void ui_show_ble_pairing(int remaining_sec);

#if APP_ENABLE_OTA
/** @brief OTA 펌웨어 업데이트 화면 표시 (부분 갱신, 첫 호출만 전체 갱신) */
void ui_show_ota_progress(uint8_t state, uint8_t progress_pct);
/** @brief OTA 렌더 상태 리셋 (다음 OTA 시 전체 갱신부터 시작) */
void ui_reset_ota_render(void);
#endif

// 내부 헬퍼 (epd_ui.c 내부 공유)
void ui_draw_line(int x0, int y0, int x1, int y1, epd_color_t color);

#ifdef __cplusplus
}
#endif

#endif  // EPD_UI_H
