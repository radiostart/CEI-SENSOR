/**
 * @file cess_calculator.h
 * @brief Coffee Environment Stability Score (CESS) Calculator
 *
 * Environment Score (ENV Score) Implementation
 * 환경 난이도 점수 계산 라이브러리 (0~100점, 100점 = 최적)
 */

#ifndef CESS_CALCULATOR_H
#define CESS_CALCULATOR_H

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// ENV Score 기본 설정값 (Defaults)
// ============================================================

// 온도 구간 (°C)
#define ENV_DEFAULT_T_OPT_MIN 20.0
#define ENV_DEFAULT_T_OPT_MAX 24.0
#define ENV_DEFAULT_T_HARD_MIN 18.0
#define ENV_DEFAULT_T_HARD_MAX 26.0

// 습도 구간 (% RH)
#define ENV_DEFAULT_H_OPT_MIN 40.0
#define ENV_DEFAULT_H_OPT_MAX 60.0
#define ENV_DEFAULT_H_HARD_MIN 30.0
#define ENV_DEFAULT_H_HARD_MAX 70.0

// 가중치
#define ENV_DEFAULT_WEIGHT_H 0.7
#define ENV_DEFAULT_WEIGHT_T 0.3

// ============================================================
// 설정 구조체 (Configuration Struct)
// ============================================================
/**
 * @brief CESS 계산 설정 구조체 (ENV Score 파라미터)
 */
typedef struct {
  // 온도 설정
  double t_opt_min;
  double t_opt_max;
  double t_hard_min;
  double t_hard_max;

  // 습도 설정
  double h_opt_min;
  double h_opt_max;
  double h_hard_min;
  double h_hard_max;

  // 가중치
  double weight_h;
  double weight_t;
} cess_config_t;

// ============================================================
// 함수 선언
// ============================================================

/**
 * @brief 기본 설정값으로 구조체를 초기화합니다.
 * @param config 초기화할 구조체 포인터
 */
void cess_init_config(cess_config_t *config);

/**
 * @brief 환경 안정성 점수(CESS)를 계산합니다. (커스텀 설정)
 * @param config 설정 구조체 포인터
 * @param current_temp 현재 온도
 * @param current_hum 현재 습도
 * @return 계산된 점수 (0.0 ~ 100.0, 100이 최적)
 */
double cess_calculate_custom(const cess_config_t *config, double current_temp,
                             double current_hum);

/**
 * @brief 환경 안정성 점수(CESS)를 계산합니다. (기본 설정 사용)
 * @param current_temp 현재 온도
 * @param current_hum 현재 습도
 * @return 계산된 점수 (0.0 ~ 100.0, 100이 최적)
 */
double cess_calculate(double current_temp, double current_hum);

#ifdef __cplusplus
}
#endif

#endif // CESS_CALCULATOR_H
