/**
 * @file power_manager.h
 * @brief 전원 및 슬립 관리
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "app_config.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 웨이크업 원인
typedef enum {
  WAKEUP_CAUSE_NONE = 0,
  WAKEUP_CAUSE_TIMER,
  WAKEUP_CAUSE_BUTTON,
  WAKEUP_CAUSE_OTHER
} wakeup_cause_t;

/**
 * @brief 전원 관리자 초기화
 */
void power_manager_init(void);

/**
 * @brief 현재 애플리케이션 상태 반환
 */
app_state_t power_manager_get_state(void);

/**
 * @brief 애플리케이션 상태 설정
 */
void power_manager_set_state(app_state_t state);

/**
 * @brief 웨이크업 원인 확인
 */
wakeup_cause_t power_manager_get_wakeup_cause(void);

/**
 * @brief Light Sleep 진입
 * @return 웨이크업 후 원인
 */
wakeup_cause_t power_manager_enter_sleep(void);

/**
 * @brief 전원 OFF 모드 진입 (버튼으로만 깨어남)
 */
void power_manager_enter_off_mode(void);

/**
 * @brief 버튼 이벤트 처리 및 상태 전이
 * @return true = 상태가 변경됨 (루프 재시작 필요)
 */
bool power_manager_handle_button(void);

/**
 * @brief 강제 업데이트 플래그 설정
 */
void power_manager_request_update(void);

/**
 * @brief 강제 업데이트 플래그 확인 및 클리어
 */
bool power_manager_consume_update_request(void);

#ifdef __cplusplus
}
#endif

#endif // POWER_MANAGER_H
