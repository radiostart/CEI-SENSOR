#pragma once
#include <stdint.h>

typedef struct {
  uint8_t w;
  uint8_t h;
  const uint8_t *data; // 1bpp, row-major, MSB first, black=1
} sensor_glyph_t;

typedef enum {
  SENSOR_GLYPH_0 = 0,
  SENSOR_GLYPH_1 = 1,
  SENSOR_GLYPH_2 = 2,
  SENSOR_GLYPH_3 = 3,
  SENSOR_GLYPH_4 = 4,
  SENSOR_GLYPH_5 = 5,
  SENSOR_GLYPH_6 = 6,
  SENSOR_GLYPH_7 = 7,
  SENSOR_GLYPH_8 = 8,
  SENSOR_GLYPH_9 = 9,
  SENSOR_GLYPH_DOT = 10,
  SENSOR_GLYPH_DEG18 = 11,
  SENSOR_GLYPH_C18 = 12,
  SENSOR_GLYPH_PCT18 = 13,
  SENSOR_GLYPH_HYPHEN = 14,
  SENSOR_GLYPH_COUNT = 15
} sensor_glyph_id_t;

const sensor_glyph_t *sensor_glyph_get(sensor_glyph_id_t id);
sensor_glyph_id_t sensor_glyph_lookup(char c);

// Layout helpers (recommended)
#define SENSOR_VALUE_H 24
#define SENSOR_UNIT_H 18
#define SENSOR_ICON_H 22
#define SENSOR_UNIT_PAD_X 3

// Draw helper signatures (implement these in your UI layer using
// epd_draw_bitmap or similar) These only compute suggested y-offsets for
// alignment.
static inline int sensor_icon_y_in_row(int row_y) {
  return row_y + (SENSOR_VALUE_H - SENSOR_ICON_H) / 2;
}
static inline int sensor_unit_y_in_row(int row_y) {
  return row_y + (SENSOR_VALUE_H - SENSOR_UNIT_H);
}
