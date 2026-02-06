/**
 * @file epd_ui.h
 * @brief E-Paper UI 레이어 - 커피 환경 모니터링 인터페이스
 *
 * 2.13인치 BW E-Paper용 세로형 UI 레이아웃
 * - 해상도: 122 x 250 픽셀 (Panel Coordinates 0..121, 0..249)
 * - 좌표계: 드라이버가 Hardware Offset/Flip을 처리하므로, UI는 0-based Panel
 * 좌표 사용
 */

#ifndef EPD_UI_H
#define EPD_UI_H

#include "eml_calculator.h"
#include "epd_driver.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 📐 레이아웃 상수 (픽셀 단위)
// ============================================================
/*
 * 전체 디스플레이: 122 x 250 픽셀 (Total Height: 250px)
 *
 * ┌────────────────────┐ Y=0
 * │ TOP BAR (24px)     │ H=24
 * ├────────────────────┤ Y=24 (Gap 1px) -> Start 25
 * │                    │
 * │ SCORE (86px)       │ H=86
 * │                    │
 * ├────────────────────┤ Y=111 (Divider 3px)
 * │                    │
 * │ DATA (80px)        │ H=80
 * │                    │
 * ├────────────────────┤ Y=194 (Divider 3px)
 * │                    │
 * │ STATUS (53px)      │ H=53
 * └────────────────────┘ Y=250
 */

// Driver Macro Consistency Check
#ifndef EPD_PANEL_WIDTH
#error "EPD_PANEL_WIDTH not defined in epd_driver.h"
#endif

// UI Dimensions (Panel based)
#define UI_WIDTH EPD_PANEL_WIDTH // 122
#define UI_HEIGHT EPD_HEIGHT     // 250

// Padding (0 margin for full-width usage)
#define UI_PADDING_X 0
#define UI_CONTENT_WIDTH (UI_WIDTH - (UI_PADDING_X * 2))

// Section Layout (Calculated to ensure no overlap)
#define UI_DIVIDER_HEIGHT 3

// 1. Top Bar
#define UI_TOP_BAR_Y 0
#define UI_TOP_BAR_HEIGHT 24

// 2. Score Section
#define UI_SCORE_Y (UI_TOP_BAR_Y + UI_TOP_BAR_HEIGHT + 1) // 25
#define UI_SCORE_HEIGHT 86

// 3. Data Section
#define UI_DATA_Y (UI_SCORE_Y + UI_SCORE_HEIGHT + UI_DIVIDER_HEIGHT) // 114
#define UI_DATA_HEIGHT 80

// 4. Status Section
#define UI_STATUS_Y (UI_DATA_Y + UI_DATA_HEIGHT + UI_DIVIDER_HEIGHT) // 197
#define UI_STATUS_HEIGHT 53

// Layout Validation (Compile-time check)
#if ((UI_STATUS_Y + UI_STATUS_HEIGHT) > UI_HEIGHT)
#error "UI Layout exceeds Display Height (250px)!"
#endif

// ============================================================
// 🌡️ 최적 범위 임계값
// ============================================================
#define UI_TEMP_OPTIMAL_MIN 20     // 최적 온도 최소값 (°C)
#define UI_TEMP_OPTIMAL_MAX 24     // 최적 온도 최대값 (°C)
#define UI_HUMIDITY_OPTIMAL_MIN 40 // 최적 습도 최소값 (%)
#define UI_HUMIDITY_OPTIMAL_MAX 60 // 최적 습도 최대값 (%)

// ============================================================
// 🔄 Zone (상태) 정의
// ============================================================
// Logic States: 환경 평가 결과 (5단계)
typedef enum {
  UI_ZONE_OPTIMAL = 0, // Zone 1: 최적 상태
  UI_ZONE_HUMID,       // Zone 2: 습한 상태
  UI_ZONE_DANGER,      // Zone 3: 위험 (고습)
  UI_ZONE_DRY,         // Zone 4: 건조 상태
  UI_ZONE_VERY_DRY,    // Zone 5: 매우 건조 (위험)
  UI_ZONE_COUNT        // Zone 개수 (5)
} ui_zone_t;

// ============================================================
// 🏷️ 상태 레이블 정의 (Status Labels)
// ============================================================
// Display Labels: 화면에 표시되는 텍스트/아이콘 상태 (7단계 세분화)
typedef enum {
  UI_STATUS_OPTIMAL = 0, // 최적 (Optimal)

  // 건조 (Dry)
  UI_STATUS_DRY_SLIGHTLY, // 약간 건조 (Slightly Dry)
  UI_STATUS_DRY_MODERATE, // 건조 (Dry)
  UI_STATUS_DRY_EXTREME,  // 매우 건조 (Very Dry)

  // 습함 (Humid)
  UI_STATUS_HUMID_SLIGHTLY, // 약간 습함 (Slightly Humid)
  UI_STATUS_HUMID_MODERATE, // 습함 (Humid)
  UI_STATUS_HUMID_EXTREME   // 매우 습함 (Very Humid)
} ui_status_label_t;

// Action은 Status Label과 1:1 매핑되어 사용됨 (Alias)
typedef ui_status_label_t ui_action_t;

// ============================================================
// 📊 UI 상태 구조체
// ============================================================
typedef struct {
  // 센서 데이터 (정수형으로 처리, x10 스케일)
  int16_t temperature_x10; // 온도 x10 (예: 23.5°C = 235)
  int16_t humidity_x10;    // 습도 x10 (예: 48.0% = 480)

  // CESS 점수
  int16_t cess_index;     // CESS 값 (0~100)
  int16_t cess_reference; // 기준 CESS 값
  int16_t cess_delta;     // 델타 (현재 - 기준)

  // 액션 정보
  ui_action_t action; // 권장 액션 타입

  // 현재 Zone
  ui_zone_t current_zone;

  // EML (Environment Moisture Level)
  env_moisture_level_t eml_level; // EML 레벨 (-3 ~ +3)

  // Status Bar / 시스템 상태
  bool is_ble_connected; // BLE 연결 상태
  int battery_level;     // 배터리 레벨 (0~100)

  // 데이터 유효성 플래그 (초기화 화면용)
  bool has_valid_data; // 센서 데이터 유효 여부
} ui_state_t;

// ============================================================
// 🎨 UI 초기화/렌더링 함수
// ============================================================

/**
 * @brief UI 시스템 초기화
 * @return 성공 시 0, 실패 시 음수
 */
int ui_init(void);

/**
 * @brief UI 부분 렌더링 (센서 데이터만)
 */
void ui_render_partial(void);

/**
 * @brief 실시간 센서 데이터로 UI 상태 업데이트
 * @param temp_x10 온도 x10 (예: 235 = 23.5°C)
 * @param humidity_x10 습도 x10 (예: 480 = 48.0%)
 * @param cess CESS 점수 값
 * @param reference 기준 CESS 값
 *
 * 이 함수는 Zone 프리셋 대신 실제 센서 값으로 UI를 업데이트합니다.
 * 경고 플래그와 액션은 자동으로 계산됩니다.
 */
void ui_update_from_sensors(int16_t temp_x10, int16_t humidity_x10,
                            int16_t cess, int16_t reference);

/**
 * @brief 배터리 레벨 설정 (0-100)
 * @param level 배터리 잔량 퍼센트
 */
void ui_set_battery_level(int level);

/**
 * @brief 전원 오프 화면 표시
 */
void ui_show_power_off(void);


#ifdef __cplusplus
}
#endif

#endif // EPD_UI_H
