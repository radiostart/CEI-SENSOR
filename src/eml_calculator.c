/**
 * @file eml_calculator.c
 * @brief Environment Moisture Level (EML) Calculator Implementation
 */

#include "eml_calculator.h"

// Helper: Clamp integer value
static int clamp_int(int v, int min_v, int max_v) {
  if (v < min_v)
    return min_v;
  if (v > max_v)
    return max_v;
  return v;
}

env_moisture_level_t eml_classify_moisture(double temp_c, double hum_percent) {
  int level = 0;

  // 1) 습도 기반 기본 레벨
  double H = hum_percent;

  if (H <= 25.0) {
    level = -3; // 매우건조 (ENV_MOISTURE_VERY_DRY)
  } else if (H <= 35.0) {
    level = -2; // 건조 (ENV_MOISTURE_DRY)
  } else if (H < 45.0) {
    level = -1; // 약간건조 (ENV_MOISTURE_SLIGHT_DRY)
  } else if (H <= 55.0) {
    level = 0; // 적정 (ENV_MOISTURE_NEUTRAL)
  } else if (H <= 65.0) {
    level = 1; // 약간습함 (ENV_MOISTURE_SLIGHT_HUMID)
  } else if (H <= 75.0) {
    level = 2; // 습함 (ENV_MOISTURE_HUMID)
  } else {
    level = 3; // 매우습함 (ENV_MOISTURE_VERY_HUMID)
  }

  // 2) 온도 보정: 더우면 습한 쪽, 추우면 건조한 쪽으로 한 단계 이동
  double T = temp_c;

  // 더운 + 이미 적정 이상 습도인 경우 → 더 습하게
  if (T >= 27.0 && H >= 45.0) {
    level += 1;
  }

  // 차가운 + 적정 이하 습도인 경우 → 더 건조하게
  if (T <= 18.0 && H <= 55.0) {
    level -= 1;
  }

  // 레벨 클램프
  level = clamp_int(level, -3, 3);
  return (env_moisture_level_t)level;
}

const char *eml_get_level_str(env_moisture_level_t lvl) {
  switch (lvl) {
  case ENV_MOISTURE_VERY_DRY:
    return "VERY DRY";
  case ENV_MOISTURE_DRY:
    return "DRY";
  case ENV_MOISTURE_SLIGHT_DRY:
    return "SLIGHTLY DRY";
  case ENV_MOISTURE_NEUTRAL:
    return "BALANCED";
  case ENV_MOISTURE_SLIGHT_HUMID:
    return "SLIGHTLY HUMID";
  case ENV_MOISTURE_HUMID:
    return "WET";
  case ENV_MOISTURE_VERY_HUMID:
    return "VERY WET";
  default:
    return "UNKNOWN";
  }
}
