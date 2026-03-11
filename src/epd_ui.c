/**
 * @file epd_ui.c
 * @brief E-Paper UI 구현 - Mellow Air 발효 모니터링 인터페이스
 *
 * 2.13인치 BW E-Paper 세로형 UI (122 x 250 픽셀)
 *
 * Section layout:
 *   [TOP BAR  24px] [VALUES 86px] [TARGET/DIFF 80px] [PROOFING 53px]
 */

#include "epd_ui.h"
#include "app_config.h"
#include "epd_driver.h"
#include "epd_icons.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "EPD_UI";

// ============================================================
// 전역 UI 상태
// ============================================================
static ui_state_t s_ui_state = {0};
static bool s_ui_initialized  = false;

// ============================================================
// 그래픽 헬퍼
// ============================================================

// Bresenham 직선
void ui_draw_line(int x0, int y0, int x1, int y1, epd_color_t color) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy, e2;
  while (1) {
    epd_draw_pixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// ============================================================
// 센서 글리프 (24px 높이) - 온도/습도 값 표시용
// ============================================================
#include "sensor_glyphs_24px.h"

// 16×16 비트맵 아이콘 렌더링
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

// sensor_glyph_24px 텍스트 렌더링
static void ui_draw_sensor_text(int x, int y, const char *text) {
  if (!text) return;
  int cursor_x = x;
  while (*text) {
    char c = *text++;
    if (c == ' ') { cursor_x += 6; continue; }

    sensor_glyph_id_t id = sensor_glyph_lookup(c);
    const sensor_glyph_t *g = sensor_glyph_get(id);
    if (!g) continue;

    int y_off = 0;
    if (id == SENSOR_GLYPH_C18 || id == SENSOR_GLYPH_PCT18) y_off = 6;
    else if (id == SENSOR_GLYPH_DEG18) y_off = 1;

    int row_bytes = (g->w + 7) / 8;
    for (int j = 0; j < g->h; j++) {
      const uint8_t *row = &g->data[j * row_bytes];
      for (int i = 0; i < g->w; i++) {
        if (row[i >> 3] & (0x80 >> (i & 7))) {
          epd_draw_pixel(cursor_x + i, y + y_off + j, EPD_COLOR_BLACK);
        }
      }
    }
    cursor_x += g->w + 2;
  }
}

// ============================================================
// Section 1: TOP BAR (Y=0..23)
// ============================================================
static void ui_draw_top_bar(const ui_state_t *state) {
  epd_fill_rect(0, UI_TOP_BAR_Y, UI_WIDTH, UI_TOP_BAR_HEIGHT, EPD_COLOR_WHITE);

  // BLE 아이콘 (연결 시만 표시, 세로선 2px + 대각선 1px)
  if (state->is_ble_connected) {
    int bx = 2, by = UI_TOP_BAR_Y + 6;
    // 중심 세로선 (2px 굵기)
    epd_draw_vline(bx + 3, by, 12, EPD_COLOR_BLACK);
    epd_draw_vline(bx + 4, by, 12, EPD_COLOR_BLACK);
    // 우측 상단 화살표
    ui_draw_line(bx + 4, by,     bx + 7, by + 3, EPD_COLOR_BLACK);
    ui_draw_line(bx + 7, by + 3, bx + 4, by + 6, EPD_COLOR_BLACK);
    // 우측 하단 화살표
    ui_draw_line(bx + 4, by + 6,  bx + 7, by + 9,  EPD_COLOR_BLACK);
    ui_draw_line(bx + 7, by + 9,  bx + 4, by + 12, EPD_COLOR_BLACK);
    // 좌측 화살표
    ui_draw_line(bx, by + 3, bx + 3, by + 6, EPD_COLOR_BLACK);
    ui_draw_line(bx, by + 9, bx + 3, by + 6, EPD_COLOR_BLACK);
  }

  // 충전 아이콘 ⚡ (배터리 왼쪽, USB 연결 시 표시)
  int chg_w = 0;
  if (state->is_charging) {
    chg_w = 10;  // 아이콘 폭 + 간격
    int cx = UI_WIDTH - 22 - 2 - chg_w;  // 배터리 아이콘 왼쪽
    int cy = UI_TOP_BAR_Y + 6;
    // 번개 모양 (7×12px)
    ui_draw_line(cx + 4, cy,     cx + 1, cy + 5, EPD_COLOR_BLACK);
    ui_draw_line(cx + 5, cy,     cx + 2, cy + 5, EPD_COLOR_BLACK);
    epd_draw_hline(cx + 1, cy + 5, 5, EPD_COLOR_BLACK);
    epd_draw_hline(cx + 1, cy + 6, 5, EPD_COLOR_BLACK);
    ui_draw_line(cx + 4, cy + 6,  cx + 1, cy + 11, EPD_COLOR_BLACK);
    ui_draw_line(cx + 5, cy + 6,  cx + 2, cy + 11, EPD_COLOR_BLACK);
  }

  // 배터리 아이콘 (우측)
  int bat_w = 22, bat_h = 12;
  int bat_x = UI_WIDTH - bat_w - 2;
  int bat_y = UI_TOP_BAR_Y + 6;
  epd_draw_rect(bat_x, bat_y, bat_w, bat_h, EPD_COLOR_BLACK);
  epd_fill_rect(bat_x + bat_w, bat_y + 3, 2, 5, EPD_COLOR_BLACK);
  int level = state->battery_level;
  if (level > 100) level = 100;
  if (level < 0)   level = 0;
  int fill_w = (int)((bat_w - 4) * (level / 100.0));
  if (fill_w > 0) {
    epd_fill_rect(bat_x + 2, bat_y + 2, fill_w, bat_h - 4, EPD_COLOR_BLACK);
  }
}

// ============================================================
// Section 2: VALUES (Y=25..110) - 현재 온도 + 습도
// ============================================================
static void ui_draw_values_section(const ui_state_t *state) {
  epd_fill_rect(0, UI_VALUES_Y, UI_WIDTH, UI_VALUES_HEIGHT, EPD_COLOR_WHITE);

  // 두 행으로 배치: TEMP (위), HUMIDITY (아래)
  // 각 행: 아이콘(16×16) + 24px 텍스트, 총 행 높이 ~34px
  int row1_y = UI_VALUES_Y + 8;   // TEMP 행
  int row2_y = UI_VALUES_Y + 50;  // HUMIDITY 행
  int icon_x = 8;
  int num_x  = 30;

  if (!state->has_valid_data) {
    // 센서 무효 시 아이콘 + "--" 표시 (각 행)
    ui_draw_bitmap_icon(icon_x, row1_y + 4, icon_thermometer, 16, 16, EPD_COLOR_BLACK);
    ui_draw_sensor_text(num_x, row1_y, "-- ^C");
    ui_draw_bitmap_icon(icon_x, row2_y + 4, icon_droplet, 16, 16, EPD_COLOR_BLACK);
    ui_draw_sensor_text(num_x, row2_y, "-- %");
    return;
  }

  // TEMP 행
  ui_draw_bitmap_icon(icon_x, row1_y + 4, icon_thermometer, 16, 16, EPD_COLOR_BLACK);
  char temp_str[20];
  int t_abs = abs(state->temperature_x10);
  snprintf(temp_str, sizeof(temp_str), "%s%d.%d ^C",
           state->temperature_x10 < 0 ? "-" : "",
           t_abs / 10, t_abs % 10);
  ui_draw_sensor_text(num_x, row1_y, temp_str);

  // HUMIDITY 행
  ui_draw_bitmap_icon(icon_x, row2_y + 4, icon_droplet, 16, 16, EPD_COLOR_BLACK);
  char hum_str[20];
  int h_abs = abs(state->humidity_x10);
  snprintf(hum_str, sizeof(hum_str), "%d.%d %%", h_abs / 10, h_abs % 10);
  ui_draw_sensor_text(num_x, row2_y, hum_str);
}

// 온도 값 렌더링: 숫자(LARGE) + °C(SMALL)
// 반환값: 총 렌더링 폭 (px)
static int ui_draw_temp_with_unit(int x, int y, const char *num_str, epd_color_t color) {
  epd_draw_text(x, y, num_str, EPD_FONT_LARGE, color);
  int nw = epd_get_text_width(num_str, EPD_FONT_LARGE);
  // ° 기호 (4×4 빈 사각형 — 저해상도에서 원처럼 보임)
  int deg_x = x + nw + 1;
  epd_draw_rect(deg_x, y + 1, 4, 4, color);
  // "C" in SMALL (바닥 정렬)
  epd_draw_text(deg_x + 5, y + 8, "C", EPD_FONT_SMALL, color);
  return nw + 5 + 6;  // num + gap+deg + C
}

// ============================================================
// Section 3: TARGET/DIFF (Y=114..193)
// ============================================================
static void ui_draw_target_section(const ui_state_t *state) {
  epd_fill_rect(0, UI_TARGET_Y, UI_WIDTH, UI_TARGET_HEIGHT, EPD_COLOR_WHITE);

  // 고온 경고 전용 화면
  if (state->high_temp_warn) {
    // 반전: 검정 배경 + 흰 텍스트
    epd_fill_rect(0, UI_TARGET_Y, UI_WIDTH, UI_TARGET_HEIGHT, EPD_COLOR_BLACK);
    int warn_y = UI_TARGET_Y + 14;
    int w1 = epd_get_text_width("WARN:", EPD_FONT_LARGE);
    epd_draw_text((UI_WIDTH - w1) / 2, warn_y, "WARN:", EPD_FONT_LARGE, EPD_COLOR_WHITE);
    epd_draw_text((UI_WIDTH - w1) / 2 + 1, warn_y, "WARN:", EPD_FONT_LARGE, EPD_COLOR_WHITE);
    int w2 = epd_get_text_width("HIGH TEMP", EPD_FONT_LARGE);
    epd_draw_text((UI_WIDTH - w2) / 2, warn_y + 22, "HIGH TEMP", EPD_FONT_LARGE, EPD_COLOR_WHITE);
    epd_draw_text((UI_WIDTH - w2) / 2 + 1, warn_y + 22, "HIGH TEMP", EPD_FONT_LARGE, EPD_COLOR_WHITE);
    return;
  }

  if (!state->process_active) {
    // 공정 미설정: Target/Diff를 "--"로 표시
    int tgt_label_y = UI_TARGET_Y + 4;
    int tgt_value_y = UI_TARGET_Y + 16;
    epd_draw_text(4, tgt_label_y, "Target", EPD_FONT_SMALL, EPD_COLOR_BLACK);
    epd_draw_text(4, tgt_value_y, "--", EPD_FONT_LARGE, EPD_COLOR_BLACK);
    int hw = epd_get_text_width("--%", EPD_FONT_LARGE);
    epd_draw_text(UI_WIDTH - hw - 4, tgt_value_y, "--%", EPD_FONT_LARGE, EPD_COLOR_BLACK);

    int mid_y = UI_TARGET_Y + 38;
    epd_draw_hline(4, mid_y, UI_WIDTH - 8, EPD_COLOR_BLACK);

    int diff_label_y = UI_TARGET_Y + 42;
    int diff_value_y = UI_TARGET_Y + 54;
    epd_draw_text(4, diff_label_y, "Diff", EPD_FONT_SMALL, EPD_COLOR_BLACK);
    epd_draw_text(4, diff_value_y, "--", EPD_FONT_LARGE, EPD_COLOR_BLACK);
    int dhw = epd_get_text_width("--%", EPD_FONT_LARGE);
    epd_draw_text(UI_WIDTH - dhw - 4, diff_value_y, "--%", EPD_FONT_LARGE, EPD_COLOR_BLACK);
    return;
  }

  // --- Target 영역 (상단 절반) ---
  // 온도/습도 모두 LARGE (12px/char) — "27.0C"(60px) + "75%"(36px) + 여백 = 104px
  int tgt_label_y = UI_TARGET_Y + 4;
  int tgt_value_y = UI_TARGET_Y + 16;

  epd_draw_text(4, tgt_label_y, "Target", EPD_FONT_SMALL, EPD_COLOR_BLACK);

  char tgt_temp[12], tgt_humi[12];
  snprintf(tgt_temp, sizeof(tgt_temp), "%d.%d",
           state->target_temp_x10 / 10, abs(state->target_temp_x10 % 10));
  snprintf(tgt_humi, sizeof(tgt_humi), "%d%%",
           state->target_humi_x10 / 10);
  ui_draw_temp_with_unit(4, tgt_value_y, tgt_temp, EPD_COLOR_BLACK);
  int hw = epd_get_text_width(tgt_humi, EPD_FONT_LARGE);
  epd_draw_text(UI_WIDTH - hw - 4, tgt_value_y, tgt_humi, EPD_FONT_LARGE, EPD_COLOR_BLACK);

  // --- 구분선 ---
  int mid_y = UI_TARGET_Y + 38;
  epd_draw_hline(4, mid_y, UI_WIDTH - 8, EPD_COLOR_BLACK);

  // --- Diff 영역 (하단 절반) ---
  // 습도 편차는 정수 표시 (공간 절약): "+2%" "-5%"
  int diff_label_y = UI_TARGET_Y + 42;
  int diff_value_y = UI_TARGET_Y + 54;

  epd_draw_text(4, diff_label_y, "Diff", EPD_FONT_SMALL, EPD_COLOR_BLACK);

  char dt_str[12], dh_str[12];
  int dt_int = state->diff_temp_x10 / 10;
  int dt_dec = abs(state->diff_temp_x10 % 10);
  int dh_round = (state->diff_humi_x10 + (state->diff_humi_x10 >= 0 ? 5 : -5)) / 10;
  const char *t_sign = (state->diff_temp_x10 >= 0) ? "+" : "";
  const char *h_sign = (dh_round >= 0) ? "+" : "";
  if (abs(dt_int) >= 10) {
    snprintf(dt_str, sizeof(dt_str), "%s%d", t_sign, dt_int);
  } else {
    snprintf(dt_str, sizeof(dt_str), "%s%d.%d", t_sign, dt_int, dt_dec);
  }
  snprintf(dh_str, sizeof(dh_str), "%s%d%%", h_sign, dh_round);

  bool diff_warn = state->temp_out_of_range || state->humi_out_of_range;
  if (diff_warn) {
    epd_fill_rect(0, diff_value_y - 2, UI_WIDTH, 20, EPD_COLOR_BLACK);
    ui_draw_temp_with_unit(4, diff_value_y, dt_str, EPD_COLOR_WHITE);
    int dhw = epd_get_text_width(dh_str, EPD_FONT_LARGE);
    epd_draw_text(UI_WIDTH - dhw - 4, diff_value_y, dh_str, EPD_FONT_LARGE, EPD_COLOR_WHITE);
  } else {
    ui_draw_temp_with_unit(4, diff_value_y, dt_str, EPD_COLOR_BLACK);
    int dhw = epd_get_text_width(dh_str, EPD_FONT_LARGE);
    epd_draw_text(UI_WIDTH - dhw - 4, diff_value_y, dh_str, EPD_FONT_LARGE, EPD_COLOR_BLACK);
  }
}

// ============================================================
// Section 4: PROOFING (Y=197..249)
// ============================================================
static void ui_draw_proofing_section(const ui_state_t *state) {
  epd_fill_rect(0, UI_PROOF_Y, UI_WIDTH, UI_PROOF_HEIGHT, EPD_COLOR_WHITE);

  int line1_y = UI_PROOF_Y + 5;
  int line2_y = UI_PROOF_Y + 20;
  int line3_y = UI_PROOF_Y + 36;

  if (state->duration_sec == 0 || state->process_name[0] == '\0') {
    // 타이머 미설정: 경과 시간 프로그레스 바 (0~100%)
    epd_draw_text(4, line1_y, "Elapsed", EPD_FONT_SMALL, EPD_COLOR_BLACK);

    // 경과 시간 텍스트 (우측)
    uint32_t total_min = state->elapsed_sec / 60;
    uint32_t h = total_min / 60;
    uint32_t m = total_min % 60;
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02u:%02u",
             (unsigned)h, (unsigned)m);
    int tw = epd_get_text_width(time_str, EPD_FONT_SMALL);
    epd_draw_text(UI_WIDTH - tw - 2, line1_y, time_str, EPD_FONT_SMALL, EPD_COLOR_BLACK);

    // 프로그레스 바 (빈 바 0%)
    int bar_x = 2, bar_y = line2_y;
    int bar_w = UI_WIDTH - 4, bar_h = 10;
    epd_draw_rect(bar_x, bar_y, bar_w, bar_h, EPD_COLOR_BLACK);

    // 퍼센트 텍스트
    epd_draw_text_centered(UI_WIDTH / 2, line3_y, "0%", EPD_FONT_SMALL, EPD_COLOR_BLACK);
    return;
  }

  // 남은 시간 (중앙 표시)
  char time_buf[20];
  if (state->elapsed_sec < state->duration_sec) {
    uint32_t remaining = state->duration_sec - state->elapsed_sec;
    uint32_t rm = (remaining + 59) / 60;  // 올림: 1초라도 남으면 1분 표시
    uint32_t rh = rm / 60;
    rm = rm % 60;
    if (rh > 0) {
      snprintf(time_buf, sizeof(time_buf), "%uh %02um left", (unsigned)rh, (unsigned)rm);
    } else {
      snprintf(time_buf, sizeof(time_buf), "%um left", (unsigned)rm);
    }
  } else {
    snprintf(time_buf, sizeof(time_buf), "Done!");
  }
  epd_draw_text_centered(UI_WIDTH / 2, line1_y, time_buf, EPD_FONT_SMALL, EPD_COLOR_BLACK);

  // 프로그레스 바 "[=====>      ] XX%"
  // 바 영역: x=2..119 (118px), 높이 10px
  int bar_x = 2, bar_y = line2_y;
  int bar_w = UI_WIDTH - 4, bar_h = 10;

  epd_draw_rect(bar_x, bar_y, bar_w, bar_h, EPD_COLOR_BLACK);

  int pct = state->progress_pct;
  if (pct > 100) pct = 100;
  int fill_w = (int)((bar_w - 2) * (pct / 100.0f));
  if (fill_w > 0) {
    epd_fill_rect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, EPD_COLOR_BLACK);
  }

  // 퍼센트 텍스트
  char pct_str[8];
  snprintf(pct_str, sizeof(pct_str), "%d%%", pct);
  int pw = epd_get_text_width(pct_str, EPD_FONT_SMALL);
  epd_draw_text((UI_WIDTH - pw) / 2, line3_y, pct_str, EPD_FONT_SMALL, EPD_COLOR_BLACK);
}

// ============================================================
// 구분선 렌더링
// ============================================================
static void ui_draw_dividers(void) {
  // Values 상단 구분선
  epd_fill_rect(0, UI_VALUES_Y - 1, UI_WIDTH, 1, EPD_COLOR_BLACK);
  // Target 상단 구분선 (3px — UI_DIVIDER_HEIGHT와 일치)
  epd_fill_rect(0, UI_TARGET_Y - 3, UI_WIDTH, 3, EPD_COLOR_BLACK);
  // Proofing 상단 구분선 (3px)
  epd_fill_rect(0, UI_PROOF_Y - 3, UI_WIDTH, 3, EPD_COLOR_BLACK);
  // 하단 구분선
  epd_fill_rect(0, UI_PROOF_Y + UI_PROOF_HEIGHT - 2, UI_WIDTH, 2, EPD_COLOR_BLACK);
}

// ============================================================
// 저전압 경고 화면
// ============================================================
void ui_show_low_battery(void) {
  epd_clear(EPD_COLOR_WHITE);
  epd_draw_hline(0, 20, EPD_WIDTH, EPD_COLOR_BLACK);
  int cx = EPD_WIDTH / 2;
  int cy = EPD_HEIGHT / 2 - 24;
  epd_draw_text_centered(cx, cy, "LOW", EPD_FONT_LARGE, EPD_COLOR_BLACK);
  epd_draw_text_centered(cx, cy + 18, "BATTERY", EPD_FONT_LARGE, EPD_COLOR_BLACK);
  epd_draw_text_centered(cx, cy + 44, "Connect Power", EPD_FONT_SMALL, EPD_COLOR_BLACK);
  epd_draw_hline(0, EPD_HEIGHT - 20, EPD_WIDTH, EPD_COLOR_BLACK);
  epd_refresh();
}

// ============================================================
// 전원 OFF 화면
// ============================================================
void ui_show_power_off(void) {
  epd_clear(EPD_COLOR_WHITE);
  epd_draw_hline(0, 20, EPD_WIDTH, EPD_COLOR_BLACK);
  int cx = EPD_WIDTH / 2;
  int cy = EPD_HEIGHT / 2 - 8;
  epd_draw_text_centered(cx, cy, "POWER OFF", EPD_FONT_LARGE, EPD_COLOR_BLACK);
  epd_draw_hline(0, EPD_HEIGHT - 20, EPD_WIDTH, EPD_COLOR_BLACK);
  epd_refresh();
}

// ============================================================
// BLE 페어링 모드 화면
// ============================================================
void ui_show_ble_pairing(int remaining_sec) {
  epd_clear(EPD_COLOR_WHITE);
  epd_draw_hline(0, 20, EPD_WIDTH, EPD_COLOR_BLACK);

  int cx = EPD_WIDTH / 2;

  // "BLE" + "PAIRING" 타이틀
  epd_draw_text_centered(cx, 50, "BLE", EPD_FONT_LARGE, EPD_COLOR_BLACK);
  epd_draw_text_centered(cx, 70, "PAIRING", EPD_FONT_LARGE, EPD_COLOR_BLACK);

  // 카운트다운
  char time_str[8];
  snprintf(time_str, sizeof(time_str), "%ds", remaining_sec);
  epd_draw_text_centered(cx, 110, time_str, EPD_FONT_LARGE, EPD_COLOR_BLACK);

  // 프로그레스 바 (남은 시간 비율)
  int bar_x = 8, bar_y = 140;
  int bar_w = UI_WIDTH - 16, bar_h = 12;
  epd_draw_rect(bar_x, bar_y, bar_w, bar_h, EPD_COLOR_BLACK);
  int fill_w = (remaining_sec * (bar_w - 2)) / 30;
  if (fill_w > 0) {
    epd_fill_rect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, EPD_COLOR_BLACK);
  }

  // 안내 텍스트
  epd_draw_text_centered(cx, 175, "Open app and", EPD_FONT_SMALL, EPD_COLOR_BLACK);
  epd_draw_text_centered(cx, 190, "connect", EPD_FONT_SMALL, EPD_COLOR_BLACK);

  epd_draw_hline(0, EPD_HEIGHT - 20, EPD_WIDTH, EPD_COLOR_BLACK);
  epd_refresh();
}

// ============================================================
// OTA 펌웨어 업데이트 화면
// ============================================================
#if APP_ENABLE_OTA
void ui_show_ota_progress(uint8_t state, uint8_t progress_pct) {
  epd_clear(EPD_COLOR_WHITE);
  epd_draw_hline(0, 20, EPD_WIDTH, EPD_COLOR_BLACK);

  int cx = EPD_WIDTH / 2;

  // 타이틀
  epd_draw_text_centered(cx, 50, "FIRMWARE", EPD_FONT_LARGE, EPD_COLOR_BLACK);
  epd_draw_text_centered(cx, 70, "UPDATE", EPD_FONT_LARGE, EPD_COLOR_BLACK);

  // 상태 텍스트
  const char *status_text = "Preparing...";
  if (state == 1) status_text = "Ready";
  else if (state == 2) status_text = "Receiving...";
  else if (state == 3) status_text = "Verifying...";
  else if (state == 4) status_text = "Rebooting...";
  else if (state == 5) status_text = "Error!";
  epd_draw_text_centered(cx, 100, status_text, EPD_FONT_SMALL, EPD_COLOR_BLACK);

  // 퍼센트
  char pct_str[8];
  snprintf(pct_str, sizeof(pct_str), "%d%%", progress_pct > 100 ? 100 : progress_pct);
  epd_draw_text_centered(cx, 118, pct_str, EPD_FONT_LARGE, EPD_COLOR_BLACK);

  // 프로그레스 바
  int bar_x = 8, bar_y = 140;
  int bar_w = UI_WIDTH - 16, bar_h = 12;
  epd_draw_rect(bar_x, bar_y, bar_w, bar_h, EPD_COLOR_BLACK);
  int pct = progress_pct > 100 ? 100 : progress_pct;
  int fill_w = (int)((bar_w - 2) * (pct / 100.0f));
  if (fill_w > 0) {
    epd_fill_rect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, EPD_COLOR_BLACK);
  }

  // 경고 텍스트
  epd_draw_text_centered(cx, 175, "Do not", EPD_FONT_SMALL, EPD_COLOR_BLACK);
  epd_draw_text_centered(cx, 190, "power off", EPD_FONT_SMALL, EPD_COLOR_BLACK);

  epd_draw_hline(0, EPD_HEIGHT - 20, EPD_WIDTH, EPD_COLOR_BLACK);
  epd_refresh();
}
#endif

// ============================================================
// 초기화
// ============================================================
int ui_init(void) {
  epd_init();
  memset(&s_ui_state, 0, sizeof(s_ui_state));
  s_ui_state.has_valid_data  = false;
  s_ui_state.battery_level   = 100;
  s_ui_state.is_ble_connected = false;
  s_ui_state.process_active  = false;
  s_ui_initialized = true;
  ESP_LOGI(TAG, "UI initialized (Mellow Air)");
  return 0;
}

// ============================================================
// 상태 업데이트 API
// ============================================================

void ui_set_sensor_invalid(void) {
  s_ui_state.has_valid_data = false;
  s_ui_state.high_temp_warn = false;
}

void ui_update_from_sensors(int16_t temp_x10, int16_t humidity_x10,
                            bool high_temp_warn) {
  s_ui_state.has_valid_data  = true;
  s_ui_state.temperature_x10 = temp_x10;
  s_ui_state.humidity_x10    = humidity_x10;
  s_ui_state.high_temp_warn  = high_temp_warn;

  // Diff 계산 (공정 활성 시)
  if (s_ui_state.process_active) {
    s_ui_state.diff_temp_x10 = temp_x10 - s_ui_state.target_temp_x10;
    s_ui_state.diff_humi_x10 = humidity_x10 - s_ui_state.target_humi_x10;

    s_ui_state.temp_out_of_range =
        (abs(s_ui_state.diff_temp_x10) > abs(s_ui_state.tol_temp_x10));
    s_ui_state.humi_out_of_range =
        (abs(s_ui_state.diff_humi_x10) > abs(s_ui_state.tol_humi_x10));
  }
}

void ui_update_process(const process_context_t *ctx, uint32_t elapsed_sec) {
  s_ui_state.elapsed_sec = elapsed_sec;

  if (ctx == NULL || !ctx->is_active) {
    s_ui_state.process_active = false;
    s_ui_state.process_name[0] = '\0';
    s_ui_state.duration_sec  = 0;
    s_ui_state.progress_pct  = 0;
    return;
  }

  // 목표 온도·습도 모두 0이면 미설정으로 판단
  if (ctx->target_temp == 0.0f && ctx->target_humi == 0.0f) {
    s_ui_state.process_active = false;
    s_ui_state.process_name[0] = '\0';
    s_ui_state.duration_sec  = 0;
    s_ui_state.progress_pct  = 0;
    return;
  }

  s_ui_state.process_active = true;
  snprintf(s_ui_state.process_name, sizeof(s_ui_state.process_name),
           "%s", ctx->process_name);

  s_ui_state.target_temp_x10 = (int16_t)(ctx->target_temp * 10);
  s_ui_state.target_humi_x10 = (int16_t)(ctx->target_humi * 10);
  s_ui_state.tol_temp_x10    = (int16_t)(ctx->tolerance_temp * 10);
  s_ui_state.tol_humi_x10    = (int16_t)(ctx->tolerance_humi * 10);
  s_ui_state.duration_sec    = (uint32_t)ctx->duration_min * 60;

  if (s_ui_state.duration_sec > 0) {
    // 10초 단위로 절삭 (10초 간격 갱신에 맞춤)
    uint32_t e = elapsed_sec < s_ui_state.duration_sec ? elapsed_sec : s_ui_state.duration_sec;
    e = (e / 10) * 10;
    s_ui_state.progress_pct = (uint8_t)(e * 100 / s_ui_state.duration_sec);
  } else {
    s_ui_state.progress_pct = 0;
  }
}

void ui_set_battery_level(int level) {
  if (level < 0) level = 0;
  if (level > 100) level = 100;
  s_ui_state.battery_level = level;
}

void ui_set_ble_connected(bool connected) {
  s_ui_state.is_ble_connected = connected;
}

void ui_set_charging(bool charging) {
  s_ui_state.is_charging = charging;
}

// ============================================================
// 렌더링 (부분 갱신)
// ============================================================
static int s_update_counter = 0;
static bool s_has_done_initial_refresh = false;

void ui_render_partial(void) {
  if (!s_ui_initialized) return;

  ui_draw_top_bar(&s_ui_state);
  ui_draw_values_section(&s_ui_state);
  ui_draw_target_section(&s_ui_state);
  ui_draw_proofing_section(&s_ui_state);
  ui_draw_dividers();

  s_update_counter++;

  bool do_full = (!s_has_done_initial_refresh ||
                  s_update_counter >= APP_EPD_FULL_REFRESH_INTERVAL);

  if (do_full) {
    if (!s_has_done_initial_refresh) {
      ESP_LOGI(TAG, "First render -> full refresh");
    } else {
      ESP_LOGI(TAG, "Auto full refresh (count=%d)", s_update_counter);
    }
    epd_refresh();
    s_update_counter = 0;
    s_has_done_initial_refresh = true;
  } else {
    epd_refresh_partial_area(0, 0, UI_WIDTH, UI_HEIGHT);
  }
}
