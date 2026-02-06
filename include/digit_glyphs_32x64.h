/**
 * @file digit_glyphs_32x64.h
 * @brief 1bpp digit glyphs (0-9) sized 32x64, MSB-first, row-major.
 * Generated from source image on 2025-12-16.
 *
 * Each glyph is 32x64 => 4 bytes/row * 64 rows = 256 bytes.
 * Pixel bit = 1 means "ink" (draw pixel).
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIGIT_GLYPH_W 32
#define DIGIT_GLYPH_H 64
#define DIGIT_GLYPH_BYTES_PER_ROW 4
#define DIGIT_GLYPH_SIZE_BYTES 256

extern const uint8_t DIGIT_0_32x64[DIGIT_GLYPH_SIZE_BYTES];
extern const uint8_t DIGIT_1_32x64[DIGIT_GLYPH_SIZE_BYTES];
extern const uint8_t DIGIT_2_32x64[DIGIT_GLYPH_SIZE_BYTES];
extern const uint8_t DIGIT_3_32x64[DIGIT_GLYPH_SIZE_BYTES];
extern const uint8_t DIGIT_4_32x64[DIGIT_GLYPH_SIZE_BYTES];
extern const uint8_t DIGIT_5_32x64[DIGIT_GLYPH_SIZE_BYTES];
extern const uint8_t DIGIT_6_32x64[DIGIT_GLYPH_SIZE_BYTES];
extern const uint8_t DIGIT_7_32x64[DIGIT_GLYPH_SIZE_BYTES];
extern const uint8_t DIGIT_8_32x64[DIGIT_GLYPH_SIZE_BYTES];
extern const uint8_t DIGIT_9_32x64[DIGIT_GLYPH_SIZE_BYTES];

#ifdef __cplusplus
}
#endif
