/**
 * @file cess_calculator.c
 * @brief Coffee Environment Stability Score (CESS) Calculator - ENV Score
 * Implementation
 */

#include "cess_calculator.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h> // for NULL

// [Helper] Clamp
static double clamp_double(double v, double min_v, double max_v) {
  if (v < min_v)
    return min_v;
  if (v > max_v)
    return max_v;
  return v;
}

// [내부 헬퍼] 습도 심각도 계산
static double env_calc_h_severity(const cess_config_t *cfg, double H) {
  double s_H = 0.0;

  // 1) 최적 구간: 0.0
  if (H >= cfg->h_opt_min && H <= cfg->h_opt_max) {
    s_H = 0.0;
  }
  // 2) 약간 건조: 0.0 ~ 0.5
  else if (H >= cfg->h_hard_min && H < cfg->h_opt_min) {
    double ratio = (cfg->h_opt_min - H) / (cfg->h_opt_min - cfg->h_hard_min);
    s_H = 0.5 * clamp_double(ratio, 0.0, 1.0);
  }
  // 3) 많이 건조: 0.5 ~ 1.0
  else if (H < cfg->h_hard_min) {
    double ratio = (cfg->h_hard_min - H) / cfg->h_hard_min; // 0~1+
    s_H = 0.5 + 0.5 * clamp_double(ratio, 0.0, 1.0);
  }
  // 4) 약간 습함: 0.0 ~ 0.5
  else if (H > cfg->h_opt_max && H <= cfg->h_hard_max) {
    double ratio = (H - cfg->h_opt_max) / (cfg->h_hard_max - cfg->h_opt_max);
    s_H = 0.5 * clamp_double(ratio, 0.0, 1.0);
  }
  // 5) 많이 습함: 0.5 ~ 1.0
  else { // H > h_hard_max
    double ratio = (H - cfg->h_hard_max) / (100.0 - cfg->h_hard_max); // 0~1+
    s_H = 0.5 + 0.5 * clamp_double(ratio, 0.0, 1.0);
  }

  return clamp_double(s_H, 0.0, 1.0);
}

// [내부 헬퍼] 온도 심각도 계산
static double env_calc_t_severity(const cess_config_t *cfg, double T) {
  double s_T = 0.0;

  // 1) 최적 구간: 0.0
  if (T >= cfg->t_opt_min && T <= cfg->t_opt_max) {
    s_T = 0.0;
  }
  // 2) 약간 낮음: 0.0 ~ 0.5
  else if (T >= cfg->t_hard_min && T < cfg->t_opt_min) {
    double ratio = (cfg->t_opt_min - T) / (cfg->t_opt_min - cfg->t_hard_min);
    s_T = 0.5 * clamp_double(ratio, 0.0, 1.0);
  }
  // 3) 많이 낮음: 0.5 ~ 1.0
  else if (T < cfg->t_hard_min) {
    double ratio = (cfg->t_hard_min - T) / cfg->t_hard_min; // 0~1+
    s_T = 0.5 + 0.5 * clamp_double(ratio, 0.0, 1.0);
  }
  // 4) 약간 높음: 0.0 ~ 0.5
  else if (T > cfg->t_opt_max && T <= cfg->t_hard_max) {
    double ratio = (T - cfg->t_opt_max) / (cfg->t_hard_max - cfg->t_opt_max);
    s_T = 0.5 * clamp_double(ratio, 0.0, 1.0);
  }
  // 5) 많이 높음: 0.5 ~ 1.0
  else { // T > t_hard_max
    // 상한 40도 가정
    double ratio = (T - cfg->t_hard_max) / (40.0 - cfg->t_hard_max);
    s_T = 0.5 + 0.5 * clamp_double(ratio, 0.0, 1.0);
  }

  return clamp_double(s_T, 0.0, 1.0);
}

// [구조체 초기화 함수]
void cess_init_config(cess_config_t *config) {
  if (config == NULL)
    return;

  config->t_opt_min = ENV_DEFAULT_T_OPT_MIN;
  config->t_opt_max = ENV_DEFAULT_T_OPT_MAX;
  config->t_hard_min = ENV_DEFAULT_T_HARD_MIN;
  config->t_hard_max = ENV_DEFAULT_T_HARD_MAX;

  config->h_opt_min = ENV_DEFAULT_H_OPT_MIN;
  config->h_opt_max = ENV_DEFAULT_H_OPT_MAX;
  config->h_hard_min = ENV_DEFAULT_H_HARD_MIN;
  config->h_hard_max = ENV_DEFAULT_H_HARD_MAX;

  config->weight_h = ENV_DEFAULT_WEIGHT_H;
  config->weight_t = ENV_DEFAULT_WEIGHT_T;
}

// [메인 계산 함수] (Custom Config)
double cess_calculate_custom(const cess_config_t *config, double current_temp,
                             double current_hum) {
  if (config == NULL)
    return 0.0;

  double s_H = env_calc_h_severity(config, current_hum);
  double s_T = env_calc_t_severity(config, current_temp);

  double S = config->weight_h * s_H + config->weight_t * s_T; // 0~1
  double score = 100.0 * (1.0 - S);

  return clamp_double(score, 0.0, 100.0);
}

// [기본 계산 함수] (Default Config)
double cess_calculate(double current_temp, double current_hum) {
  static cess_config_t default_config;
  static int initialized = 0;

  if (!initialized) {
    cess_init_config(&default_config);
    initialized = 1;
  }

  return cess_calculate_custom(&default_config, current_temp, current_hum);
}
