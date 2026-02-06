/**
 * @file epd_ui.c
 * @brief E-Paper UI 구현 - 커피 환경 모니터링 인터페이스
 *
 * 2.13인치 BW E-Paper 세로형 UI
 * 해상도: 122 x 250 픽셀
 */

#include "epd_ui.h"
#include "cess_calculator.h"
#include "eml_calculator.h"
#include "epd_driver.h"
#include "epd_icons.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h> // for abs()
#include <string.h>

/*
 * [UI Coordinate Policy]
 * UI uses logical Panel Coordinates: (0,0) to (121, 249).
 * - X range: 0 .. UI_WIDTH-1 (121)
 * - Y range: 0 .. UI_HEIGHT-1 (249)
 * Driver handles hardware offsets (6px dummy) and flipping.
 * We draw to s_fb_new assuming 0-based packed pixels.
 */

static const char *TAG = "EPD_UI";

// ============================================================
// 📊 전역 UI 상태
// ============================================================
static ui_state_t s_ui_state = {0};
static bool s_ui_initialized = false;

// ============================================================
// 🏷️ Zone별 프리셋 데이터
// ============================================================
/*
 * 각 Zone의 센서 값과 UI 동작 정의
 * Zone 1 (OPTIMAL):  최적 상태
 * Zone 2 (HUMID):    습한 상태
 * Zone 3 (DANGER):   위험 상태
 * Zone 4 (DRY):      건조 상태
 * Zone 5 (VERY_DRY): 매우 건조
 */
typedef struct {
  int16_t temp_x10;        // 온도 x10
  int16_t humidity_x10;    // 습도 x10
  int16_t cess;            // CESS 인덱스
  int16_t reference;       // 기준값
  int16_t delta;           // 델타
  ui_status_label_t label; // 상태 레이블
} zone_preset_t;

static const zone_preset_t zone_presets[UI_ZONE_COUNT] = {
    // Zone 1: OPTIMAL (최적)
    {.temp_x10 = 230,
     .humidity_x10 = 500,
     .cess = 90,
     .reference = 90,
     .delta = 0,
     .label = UI_STATUS_OPTIMAL},

    // Zone 2: HUMID (습함)
    {.temp_x10 = 240,
     .humidity_x10 = 650,
     .cess = 50,
     .reference = 90,
     .delta = -40,
     .label = UI_STATUS_HUMID_MODERATE},

    // Zone 3: DANGER (위험 - 고습)
    {.temp_x10 = 280,
     .humidity_x10 = 850,
     .cess = 20,
     .reference = 90,
     .delta = -70,
     .label = UI_STATUS_HUMID_EXTREME},

    // Zone 4: DRY (건조)
    {.temp_x10 = 200,
     .humidity_x10 = 350,
     .cess = 50,
     .reference = 90,
     .delta = -40,
     .label = UI_STATUS_DRY_MODERATE},

    // Zone 5: VERY_DRY (매우 건조)
    {.temp_x10 = 180,
     .humidity_x10 = 200,
     .cess = 20,
     .reference = 90,
     .delta = -70,
     .label = UI_STATUS_DRY_EXTREME}};

// ------------------------------------------------------------
// 🎨 Graphic Helpers (Industrial)
// ------------------------------------------------------------
// No rounded rects. Standard rects only.

// Bresenham's Line Algorithm
void ui_draw_line(int x0, int y0, int x1, int y1, epd_color_t color) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy, e2;

  while (1) {
    epd_draw_pixel(x0, y0, color);
    if (x0 == x1 && y0 == y1)
      break;
    e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// Huge Digit Rendering (1bpp Bitmap)
// Size: 32w x 64h
// Gap: 8px

#include "noto_semicondensed_extrabold_digits_32x64.h"
#include "noto_semicondensed_extrabold_digits_32x64_metrics.h"

// 1. 1bpp Glyph Structure
typedef struct {
  uint16_t w, h;
  const uint8_t *data; // 1bpp, row-major, MSB-first
} glyph_1bpp_t;

// 2. Glyph Drawing Helper
static inline void draw_glyph_1bpp(int x, int y, const glyph_1bpp_t *g,
                                   epd_color_t color) {
  int byte_w = (g->w + 7) / 8;
  for (int j = 0; j < g->h; j++) {
    const uint8_t *row = &g->data[j * byte_w];
    for (int i = 0; i < g->w; i++) {
      uint8_t b = row[i >> 3];
      if (b & (0x80 >> (i & 7))) {
        epd_draw_pixel(x + i, y + j, color);
      }
    }
  }
}

// 3. Digit Glyphs
#define DIGIT_W 32
#define DIGIT_H 64

static const glyph_1bpp_t kHugeDigits[10] = {
    {DIGIT_W, DIGIT_H, NOTO_SCD_XB_DIGIT_0_32x64},
    {DIGIT_W, DIGIT_H, NOTO_SCD_XB_DIGIT_1_32x64},
    {DIGIT_W, DIGIT_H, NOTO_SCD_XB_DIGIT_2_32x64},
    {DIGIT_W, DIGIT_H, NOTO_SCD_XB_DIGIT_3_32x64},
    {DIGIT_W, DIGIT_H, NOTO_SCD_XB_DIGIT_4_32x64},
    {DIGIT_W, DIGIT_H, NOTO_SCD_XB_DIGIT_5_32x64},
    {DIGIT_W, DIGIT_H, NOTO_SCD_XB_DIGIT_6_32x64},
    {DIGIT_W, DIGIT_H, NOTO_SCD_XB_DIGIT_7_32x64},
    {DIGIT_W, DIGIT_H, NOTO_SCD_XB_DIGIT_8_32x64},
    {DIGIT_W, DIGIT_H, NOTO_SCD_XB_DIGIT_9_32x64},
};

// 4. Centered Score Drawing Logic with Tight Metrics
static int num_digits_0_999(int v) {
  if (v >= 100)
    return 3;
  if (v >= 10)
    return 2;
  return 1; // includes 0~9
}

void ui_draw_score_0_999_centered(int x, int y, int container_w, int value,
                                  epd_color_t color) {
  if (value < 0)
    value = 0;
  if (value > 999)
    value = 999;

  int d_count = num_digits_0_999(value);
  int digits[3] = {0};

  if (d_count == 1) {
    digits[0] = value;
  } else if (d_count == 2) {
    digits[0] = value / 10;
    digits[1] = value % 10;
  } else {
    digits[0] = value / 100;
    digits[1] = (value / 10) % 10;
    digits[2] = value % 10;
  }

// Calculate Total Width
// Tracking (gap) between digits
#define SCORE_TRACKING_PX 4
  int total_w = 0;
  for (int i = 0; i < d_count; i++) {
    // Determine advance width
    int adv = 0;
    if (i < d_count - 1) {
      adv = noto_scd_xb_advance_32x64(digits[i], SCORE_TRACKING_PX);
    } else {
      // Last digit only contributes its visual width
      adv = NOTO_SCD_XB_WIDTH_32x64[digits[i]];
    }
    total_w += adv;
  }

  // Start X to center the block
  int x_cursor = x + (container_w - total_w) / 2;

  // Draw Digits
  for (int i = 0; i < d_count; i++) {
    int digit = digits[i];
    int left_padding = NOTO_SCD_XB_LEFT_32x64[digit];

    // Shift left to compensate for the empty space in the bitmap
    // ensuring x_cursor aligns with the first ink pixel.
    draw_glyph_1bpp(x_cursor - left_padding, y, &kHugeDigits[digit], color);

    // Advance cursor
    x_cursor += noto_scd_xb_advance_32x64(digit, SCORE_TRACKING_PX);
  }
}

// ------------------------------------------------------------
// 🖌️ Rendering Functions
// ------------------------------------------------------------

void ui_draw_top_bar(const ui_state_t *state) {
  const int section_y = UI_TOP_BAR_Y;
  const int section_h = UI_TOP_BAR_HEIGHT;

  // Background: White
  epd_fill_rect(0, section_y, UI_WIDTH, section_h, EPD_COLOR_WHITE);

  // ==========================
  // 1. Bluetooth Icon (Left)
  // ==========================
  // Behavior: Show ONLY if Connected (Device Mode Indicator)
  if (state->is_ble_connected) {
    int bt_x = 0; // Absolute Left Edge
    int bt_y = section_y + 6;
    int bt_h = 12;

    // Vertical Spine (2px)
    epd_draw_vline(bt_x + 3, bt_y, bt_h, EPD_COLOR_BLACK);
    epd_draw_vline(bt_x + 4, bt_y, bt_h, EPD_COLOR_BLACK);

    // < Shape (Left wings)
    // Top-Left
    ui_draw_line(bt_x + 3, bt_y + 3, bt_x, bt_y + 6, EPD_COLOR_BLACK);
    ui_draw_line(bt_x + 3, bt_y + 4, bt_x, bt_y + 7, EPD_COLOR_BLACK); // Thick
    // Bottom-Left
    ui_draw_line(bt_x, bt_y + 6, bt_x + 3, bt_y + 9, EPD_COLOR_BLACK);
    ui_draw_line(bt_x, bt_y + 7, bt_x + 3, bt_y + 10, EPD_COLOR_BLACK); // Thick

    // > Shape (Right wings) - The Loop
    // Top-Right
    ui_draw_line(bt_x + 3, bt_y, bt_x + 6, bt_y + 3, EPD_COLOR_BLACK);
    ui_draw_line(bt_x + 3, bt_y + 1, bt_x + 6, bt_y + 4,
                 EPD_COLOR_BLACK); // Thick
    // Mid-Right-Down
    ui_draw_line(bt_x + 6, bt_y + 3, bt_x + 3, bt_y + 6, EPD_COLOR_BLACK);
    ui_draw_line(bt_x + 6, bt_y + 4, bt_x + 3, bt_y + 7,
                 EPD_COLOR_BLACK); // Thick
    // Mid-Right-Up
    ui_draw_line(bt_x + 3, bt_y + 6, bt_x + 6, bt_y + 9, EPD_COLOR_BLACK);
    ui_draw_line(bt_x + 3, bt_y + 7, bt_x + 6, bt_y + 10,
                 EPD_COLOR_BLACK); // Thick
    // Bottom-Right
    ui_draw_line(bt_x + 6, bt_y + 9, bt_x + 3, bt_y + 12, EPD_COLOR_BLACK);
    ui_draw_line(bt_x + 6, bt_y + 10, bt_x + 3, bt_y + 13,
                 EPD_COLOR_BLACK); // Thick

    // Connection Indicator (Waves) - Essential part of the "Connected" state
    // Inner )
    epd_fill_rect(bt_x + 9, bt_y + 4, 2, 2, EPD_COLOR_BLACK);
    epd_fill_rect(bt_x + 10, bt_y + 5, 2, 2, EPD_COLOR_BLACK);
    epd_fill_rect(bt_x + 9, bt_y + 7, 2, 2, EPD_COLOR_BLACK);

    // Outer )
    epd_fill_rect(bt_x + 12, bt_y + 3, 2, 2, EPD_COLOR_BLACK);
    epd_fill_rect(bt_x + 13, bt_y + 4, 2, 2, EPD_COLOR_BLACK);
    epd_fill_rect(bt_x + 13, bt_y + 7, 2, 2, EPD_COLOR_BLACK);
    epd_fill_rect(bt_x + 12, bt_y + 8, 2, 2, EPD_COLOR_BLACK);
  }
  // If NOT connected, draw NOTHING (Clean Minimalist)

  // ==========================
  // 2. Battery Icon (Right)
  // ==========================
  // Resized to 22x12 (Smaller as requested)
  // Shifted right (Padding 2px)
  int bat_w = 22;
  int bat_h = 12;
  int bat_x = UI_WIDTH - bat_w - 2;
  int bat_y =
      section_y + 6; // Center in 24px (12 top, 6 pad? no. 24-12=12. 6 top/bot)

  // Body Outline (Black)
  epd_draw_rect(bat_x, bat_y, bat_w, bat_h, EPD_COLOR_BLACK);

  // Positive Terminal (Nub)
  // Center vertically relative to bat_h (12) -> y+3..y+8 (H=5)
  epd_fill_rect(bat_x + bat_w, bat_y + 3, 2, 5, EPD_COLOR_BLACK);

  // Fill Level (Black)
  // 1px gap inside
  int level = state->battery_level;
  if (level > 100)
    level = 100;
  if (level < 0)
    level = 0;

  // Max fill width = bat_w - 2 (borders) - 2 (gap) = bat_w - 4
  int max_fill_w = bat_w - 4;
  int fill_w = (int)(max_fill_w * (level / 100.0));

  if (fill_w > 0) {
    epd_fill_rect(bat_x + 2, bat_y + 2, fill_w, bat_h - 4, EPD_COLOR_BLACK);
  }
}

static void ui_set_zone(ui_zone_t zone) {
  const zone_preset_t *preset = &zone_presets[zone];

  // 상태 업데이트
  s_ui_state.temperature_x10 = preset->temp_x10;
  s_ui_state.humidity_x10 = preset->humidity_x10;
  s_ui_state.cess_index = preset->cess;
  s_ui_state.cess_reference = preset->reference;
  s_ui_state.cess_delta = preset->delta;
  s_ui_state.action = preset->label;
  s_ui_state.current_zone = zone;
}

void ui_update_from_sensors(int16_t temp_x10, int16_t humidity_x10,
                            int16_t cess, int16_t reference) {
  s_ui_state.temperature_x10 = temp_x10;
  s_ui_state.humidity_x10 = humidity_x10;
  s_ui_state.cess_index = cess;
  s_ui_state.cess_reference = reference;
  s_ui_state.cess_delta = cess - reference;

  // EML 계산
  // int16_t x10 -> double
  double temp = (double)temp_x10 / 10.0;
  double hum = (double)humidity_x10 / 10.0;
  s_ui_state.eml_level = eml_classify_moisture(temp, hum);

  // 상태 매핑 (eml_level -> ui_status_label_t)
  switch (s_ui_state.eml_level) {
  case ENV_MOISTURE_VERY_DRY:
    s_ui_state.action = UI_STATUS_DRY_EXTREME;
    break;
  case ENV_MOISTURE_DRY:
    s_ui_state.action = UI_STATUS_DRY_MODERATE;
    break;
  case ENV_MOISTURE_SLIGHT_DRY:
    s_ui_state.action = UI_STATUS_DRY_SLIGHTLY;
    break;
  case ENV_MOISTURE_NEUTRAL:
    s_ui_state.action = UI_STATUS_OPTIMAL;
    break;
  case ENV_MOISTURE_SLIGHT_HUMID:
    s_ui_state.action = UI_STATUS_HUMID_SLIGHTLY;
    break;
  case ENV_MOISTURE_HUMID:
    s_ui_state.action = UI_STATUS_HUMID_MODERATE;
    break;
  case ENV_MOISTURE_VERY_HUMID:
    s_ui_state.action = UI_STATUS_HUMID_EXTREME;
    break;
  default:
    s_ui_state.action = UI_STATUS_OPTIMAL;
    break;
  }
}

void ui_set_battery_level(int level) {
  if (level < 0)
    level = 0;
  if (level > 100)
    level = 100;
  s_ui_state.battery_level = level;
}

// ============================================================
// 🎨 UI 초기화/렌더링 함수 구현
// ============================================================

// ============================================================
// 🔌 POWER OFF Screen
// ============================================================
void ui_show_power_off(void) {
  epd_clear(EPD_COLOR_WHITE);

  // 상단 라인
  epd_draw_hline(0, 20, EPD_WIDTH, EPD_COLOR_BLACK);

  // 중앙 "POWER OFF" 텍스트
  int center_x = EPD_WIDTH / 2;
  int center_y = EPD_HEIGHT / 2 - 8;
  epd_draw_text_centered(center_x, center_y, "POWER OFF", EPD_FONT_LARGE,
                         EPD_COLOR_BLACK);

  // 하단 라인
  epd_draw_hline(0, EPD_HEIGHT - 20, EPD_WIDTH, EPD_COLOR_BLACK);

  epd_refresh();
}

int ui_init(void) {
  epd_init();

  memset(&s_ui_state, 0, sizeof(s_ui_state));
  s_ui_initialized = true;
  ui_set_zone(UI_ZONE_OPTIMAL);
  s_ui_state.battery_level = 100;
  s_ui_state.is_ble_connected = false;
  ESP_LOGI(TAG, "UI Initialized");
  return 0;
}

// Helper: Draw 16x16 Bitmap Icon
// Data format: 1bpp, row-major, MSB first
static void ui_draw_bitmap_icon(int x, int y, const uint8_t *bitmap, int w,
                                int h, epd_color_t color) {
  int stride = (w + 7) / 8;
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      if (bitmap[j * stride + (i / 8)] & (0x80 >> (i % 8))) {
        epd_draw_pixel(x + i, y + j, color);
      }
    }
  }
}

// Helper: Draw 1bpp glyph at half size (Sample every 2nd pixel)
// Input (32x64) -> Output (16x32)
#include "sensor_glyphs_24px.h"

// Platform-specific 1bpp drawer for sensor glyphs
// Re-implemented to consume the sensor_glyph_t struct's data format directly
static void ui_draw_sensor_text(int x, int y, const char *text) {
  if (!text)
    return;

  int cursor_x = x;
  while (*text) {
    char c = *text++;
    if (c == ' ') {
      cursor_x += 6; // Space width
      continue;
    }

    sensor_glyph_id_t id = sensor_glyph_lookup(c);
    const sensor_glyph_t *g = sensor_glyph_get(id);
    if (!g)
      continue;

    int y_off = 0;
    // Simple vertical alignment logic based on glyph type
    if (id == SENSOR_GLYPH_C18 || id == SENSOR_GLYPH_PCT18) {
      y_off = 6; // Bottom align 18px nominal in 24 frame (24-18=6)
    } else if (id == SENSOR_GLYPH_DEG18) {
      // Degree is top-aligned, so y_off=0 or small padding
      y_off = 1;
    }

    // Draw 1bpp
    // Calculate row bytes from w: (w + 7) / 8
    int row_bytes = (g->w + 7) / 8;
    for (int j = 0; j < g->h; j++) {
      const uint8_t *row = &g->data[j * row_bytes];
      for (int i = 0; i < g->w; i++) {
        // 1bpp MSB first within byte. Black is 1? Yes usually.
        // User's comment says: 1=INK(black)
        if (row[i >> 3] & (0x80 >> (i & 7))) {
          epd_draw_pixel(cursor_x + i, y + y_off + j, EPD_COLOR_BLACK);
        }
      }
    }

    cursor_x += g->w + 2; // Tracking 2px
  }
}

void ui_draw_data_section(const ui_state_t *state) {
  // Clear Data Area
  epd_fill_rect(0, UI_DATA_Y, UI_WIDTH, UI_DATA_HEIGHT, EPD_COLOR_WHITE);

  // Layout Constants
  // Data Section Height: 80px (112 ~ 192)
  // Content: 2 rows of 24px. Total content: 48px.
  // Remaining: 32px. Split 3 ways: 11px Top, 10px Gap, 11px Bottom.
  int row1_y = UI_DATA_Y + 11;
  int row2_y = row1_y + 24 + 10;

  int icon_x = 8;
  int num_x = 34; // Align numbers

  // 1. Temperature Row
  // Icon: Thermometer (16x16) -> Centered in 24px height (Offset +4)
  ui_draw_bitmap_icon(icon_x, row1_y + 4, icon_thermometer, 16, 16,
                      EPD_COLOR_BLACK);

  // Value: "23.4 ^C" (Space added)
  char temp_str[16];
  snprintf(temp_str, sizeof(temp_str), "%d.%d ^C", state->temperature_x10 / 10,
           state->temperature_x10 % 10);
  ui_draw_sensor_text(num_x, row1_y, temp_str);

  // 2. Humidity Row
  // Icon: Droplet (16x16) -> Centered in 24px height (Offset +4)
  ui_draw_bitmap_icon(icon_x, row2_y + 4, icon_droplet, 16, 16,
                      EPD_COLOR_BLACK);

  // Value: "40.8 %" (Space added)
  char hum_str[16];
  snprintf(hum_str, sizeof(hum_str), "%d.%d %%", state->humidity_x10 / 10,
           state->humidity_x10 % 10);
  ui_draw_sensor_text(num_x, row2_y, hum_str);
}

// -------------------------------------------------------------------------
// Score Section
// -------------------------------------------------------------------------

void ui_draw_score_section(const ui_state_t *state) {
  // Clear Score Area
  epd_fill_rect(0, UI_SCORE_Y, UI_WIDTH, UI_SCORE_HEIGHT, EPD_COLOR_WHITE);

  // Real Score from State
  int score_to_draw = state->cess_index;
  // Clamp for safety
  if (score_to_draw < 0)
    score_to_draw = 0;
  if (score_to_draw > 999)
    score_to_draw = 999;

  // Draw Score using new 1bpp centered logic with tight spacing
  // Y position adjustment: 11px padding for 64px height in 86px area
  // Shifted -5px left per user request
  ui_draw_score_0_999_centered(-5, UI_SCORE_Y + 11, UI_WIDTH, score_to_draw,
                               EPD_COLOR_BLACK);

  // Divider Removed (Moved to global divider logic)
}

// Status Section (Verdict Only - Minimalist)
void ui_draw_status_section(const ui_state_t *state) {
  const int section_y = UI_STATUS_Y;
  const int section_h = UI_STATUS_HEIGHT;
  const char *eml_str = eml_get_level_str(state->eml_level);

  // 1. Determine Group (for Border/Invert logic)
  typedef enum { GROUP_BALANCED, GROUP_WARNING, GROUP_EXTREME } status_group_t;
  status_group_t group = GROUP_WARNING;

  if (state->eml_level == ENV_MOISTURE_NEUTRAL) {
    group = GROUP_BALANCED;
  } else if (state->eml_level == ENV_MOISTURE_VERY_DRY ||
             state->eml_level == ENV_MOISTURE_VERY_HUMID) {
    group = GROUP_EXTREME;
  } else {
    group = GROUP_WARNING;
  }

  // 2. Draw Background & Frame
  epd_color_t fg_color = EPD_COLOR_BLACK;

  if (group == GROUP_EXTREME) {
    // EXTREME: Inverted (Black BG, White FG)
    epd_fill_rect(0, section_y, UI_WIDTH, section_h, EPD_COLOR_BLACK);
    fg_color = EPD_COLOR_WHITE;
  } else {
    // BALANCED & WARNING: White BG
    epd_fill_rect(0, section_y, UI_WIDTH, section_h, EPD_COLOR_WHITE);
    // Dividers are drawn globally (Fixed 2px)
    fg_color = EPD_COLOR_BLACK;
  }

  // 3. Draw Text (Dynamic Single/Double Line)
  // Check for space to detect multi-word status
  const char *space_ptr = strchr(eml_str, ' ');

  if (space_ptr != NULL) {
    // === Two Lines (e.g., "VERY WET", "SLIGHTLY DRY") ===
    // Split into two strings
    int split_idx = (int)(space_ptr - eml_str);
    char line1[20];
    char line2[20];

    // Safety copy
    if (split_idx < sizeof(line1)) {
      snprintf(line1, sizeof(line1), "%.*s", split_idx, eml_str);
    } else {
      snprintf(line1, sizeof(line1), "%s", eml_str); // Fallback
    }
    snprintf(line2, sizeof(line2), "%s", space_ptr + 1);

    // Calc Y positions (Center is section_y + 28)
    int line1_y = section_y + 10;
    int line2_y = section_y + 32;

    // Draw Line 1 (Large)
    int w1 = epd_get_text_width(line1, EPD_FONT_LARGE);
    int x1 = (UI_WIDTH - w1) / 2;
    epd_draw_text(x1, line1_y, line1, EPD_FONT_LARGE, fg_color);
    epd_draw_text(x1 + 1, line1_y, line1, EPD_FONT_LARGE, fg_color); // Bold

    // Draw Line 2 (Large)
    int w2 = epd_get_text_width(line2, EPD_FONT_LARGE);
    int x2 = (UI_WIDTH - w2) / 2;
    epd_draw_text(x2, line2_y, line2, EPD_FONT_LARGE, fg_color);
    epd_draw_text(x2 + 1, line2_y, line2, EPD_FONT_LARGE, fg_color); // Bold

  } else {
    // === Single Line (e.g., "BALANCED", "WET", "DRY") ===
    int text_y = section_y + 18;

    // Use Large Font for Impact
    int text_w = epd_get_text_width(eml_str, EPD_FONT_LARGE);
    int text_x = (UI_WIDTH - text_w) / 2;

    epd_draw_text(text_x, text_y, eml_str, EPD_FONT_LARGE, fg_color);
    epd_draw_text(text_x + 1, text_y, eml_str, EPD_FONT_LARGE,
                  fg_color); // Bold

    // "DRY" and "WET" -> make even bolder (Extra Bold request)
    if (strlen(eml_str) <= 4) {
      epd_draw_text(text_x, text_y + 1, eml_str, EPD_FONT_LARGE, fg_color);
      epd_draw_text(text_x + 1, text_y + 1, eml_str, EPD_FONT_LARGE, fg_color);
    }
  }
}

// Global Divider Drawer
// Divider Thickness Constants
#define DIVIDER_THICK_TOPBAR 1
#define DIVIDER_THICK_SCORE 2
#define DIVIDER_THICK_STATUS 3

static void ui_draw_dividers(void) {
  // 1. Top Bar Bottom
  epd_fill_rect(0, UI_SCORE_Y - 1, UI_WIDTH, DIVIDER_THICK_TOPBAR,
                EPD_COLOR_BLACK);

  // 2. Score Bottom (Data Above)
  epd_fill_rect(0, UI_DATA_Y - 2, UI_WIDTH, DIVIDER_THICK_SCORE,
                EPD_COLOR_BLACK);

  // 3. Status Top (Data Below)
  epd_fill_rect(0, UI_STATUS_Y - 3, UI_WIDTH, DIVIDER_THICK_STATUS,
                EPD_COLOR_BLACK);

  // 4. Status Bottom
  epd_fill_rect(0, UI_STATUS_Y + UI_STATUS_HEIGHT - 3, UI_WIDTH,
                DIVIDER_THICK_STATUS, EPD_COLOR_BLACK);
}

// Auto-refresh counter
static int s_update_counter = 0;
static bool s_has_done_initial_refresh = false;
const int kFullRefreshInterval = 10; // Perform full refresh every 10 updates

void ui_render_partial(void) {
  if (!s_ui_initialized)
    return;

  // Update dynamic sections (Buffer Update)
  ui_draw_top_bar(&s_ui_state);
  ui_draw_score_section(&s_ui_state);
  ui_draw_data_section(&s_ui_state);
  ui_draw_status_section(&s_ui_state);

  // Re-draw Dividers to ensure they persist over any cleared areas
  ui_draw_dividers();

  s_update_counter++;

  // Determine Refresh Type
  bool do_full_refresh =
      (!s_has_done_initial_refresh || s_update_counter >= kFullRefreshInterval);

  if (do_full_refresh) {
    if (!s_has_done_initial_refresh) {
      ESP_LOGI("UI", "First Data Rendering -> Full Refresh for Sharpness");
    } else {
      ESP_LOGI("UI", "Auto Full Refresh triggered (Count: %d)",
               s_update_counter);
    }
    epd_refresh();
    s_update_counter = 0;
    s_has_done_initial_refresh = true;

  } else {
    // Optimize: Use partial refresh on the active UI area
    // This updates the RAM with the current buffer state and triggers a fast
    // update.
    epd_refresh_partial_area(0, 0, UI_WIDTH, UI_HEIGHT);
  }
}
