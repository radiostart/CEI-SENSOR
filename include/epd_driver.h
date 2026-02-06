/**
 * @file epd_driver.h
 * @brief SSD1680 2.13" BW (250x122) E-Paper driver
 *
 * 핵심:
 * - BW 전용 패널(SSD1680)에서 0x24 = NEW, 0x26 = OLD(부분갱신용)로 사용
 * - 부분갱신은 반드시 (OLD + NEW) 둘 다 같은 윈도우에 써준 뒤 트리거해야 함
 */

#ifndef EPD_DRIVER_H
#define EPD_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ✅ legacy compatibility (epd_ui.c 등 기존 코드가 사용)
#define EPD_WIDTH EPD_PANEL_WIDTH

// ============================================================
// Panel / RAM geometry
// ============================================================
#define EPD_RAM_WIDTH 128   // Controller RAM width (pixels)
#define EPD_PANEL_WIDTH 122 // Visible panel width (pixels)
#define EPD_HEIGHT 250

// 남는 6px(128-122). 보드마다 0~6 튜닝 필요.
// "오른쪽으로 밀림" => 값을 줄여라(6->5->4...)
// "왼쪽으로 밀림"   => 값을 늘려라(0->1->2...)
#ifndef EPD_X_OFFSET_PX
#define EPD_X_OFFSET_PX 0 // Force 0 to align to Left (RAM 0)
#endif

// 거울처럼 좌우가 뒤집히면 1
#ifndef EPD_FLIP_X
#define EPD_FLIP_X 0
#endif

// 상하가 뒤집히면 1
#ifndef EPD_FLIP_Y
#define EPD_FLIP_Y 0
#endif

// BUSY 폴라리티: 대부분 SSD1680 모듈은 1=busy, 0=idle
#ifndef EPD_BUSY_IS_HIGH
#define EPD_BUSY_IS_HIGH 1
#endif

// ============================================================
// GPIO (ESP32-C3)
// ============================================================
#define EPD_PIN_BUSY 10
#define EPD_PIN_RST 3
#define EPD_PIN_DC 4
#define EPD_PIN_CS 5
#define EPD_PIN_CLK 6
#define EPD_PIN_DIN 7

// ============================================================
// Color
// ============================================================
typedef enum {
  EPD_COLOR_WHITE = 0,
  EPD_COLOR_BLACK = 1,
  EPD_COLOR_INVERT = 2,
} epd_color_t;

// ============================================================
// Font / Icons (기존 유지)
// ============================================================
typedef enum {
  EPD_FONT_SMALL = 0,
  EPD_FONT_MEDIUM = 1,
  EPD_FONT_LARGE = 2
} epd_font_size_t;

#define EPD_FONT_SMALL_WIDTH 6
#define EPD_FONT_SMALL_HEIGHT 8

// ============================================================
// API
// ============================================================
int epd_init(void);
void epd_sleep(void);
void epd_wakeup(void);

void epd_clear(epd_color_t color);

// Full refresh (전체 갱신)
void epd_refresh(void);

// Partial refresh (부분 갱신)
// 1) epd_write_area_to_ram(x,y,w,h) : OLD/NEW를 해당 영역에 써둠(트리거 없음)
// 2) epd_update_display_partial()   : 부분갱신 트리거
// 3) epd_refresh_partial_area(...)  : (1)+(2) 묶음 convenience
void epd_write_area_to_ram(int x, int y, int w, int h);
void epd_update_display_partial(void);
void epd_refresh_partial_area(int x, int y, int w, int h);

// drawing
void epd_draw_pixel(int x, int y, epd_color_t color);
void epd_fill_rect(int x, int y, int w, int h, epd_color_t color);
void epd_draw_rect(int x, int y, int w, int h, epd_color_t color);
void epd_draw_hline(int x, int y, int length, epd_color_t color);
void epd_draw_vline(int x, int y, int length, epd_color_t color);

// text
void epd_draw_text(int x, int y, const char *text, epd_font_size_t size,
                   epd_color_t color);
int epd_get_text_width(const char *text, epd_font_size_t size);
void epd_draw_text_centered(int center_x, int y, const char *text,
                            epd_font_size_t size, epd_color_t color);

#ifdef __cplusplus
}
#endif

#endif // EPD_DRIVER_H