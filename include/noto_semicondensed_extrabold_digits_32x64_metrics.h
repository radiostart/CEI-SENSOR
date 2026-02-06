#pragma once
#include <stdint.h>

// Tight metrics for 32x64 digit glyphs (Noto Sans Display SemiCondensed
// ExtraBold) These let you draw digits closer without changing the bitmap data.
//
// Meaning:
// - left[i]  : first ink column (0..31)
// - right[i] : last ink column (0..31)
// - width[i] : (right-left+1)
// - rtrim[i] : (31-right)
extern const uint8_t NOTO_SCD_XB_LEFT_32x64[10];
extern const uint8_t NOTO_SCD_XB_RIGHT_32x64[10];
extern const uint8_t NOTO_SCD_XB_WIDTH_32x64[10];
extern const uint8_t NOTO_SCD_XB_RTRIM_32x64[10];

// Optional helper: compute next x advance given tracking (extra px between
// digits)
static inline int noto_scd_xb_advance_32x64(int digit, int tracking_px) {
  if (digit < 0 || digit > 9)
    return tracking_px;
  return (int)NOTO_SCD_XB_WIDTH_32x64[digit] + tracking_px;
}
