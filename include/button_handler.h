/**
 * @file button_handler.h
 * @brief 버튼 입력 처리
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 버튼 이벤트 타입
typedef enum {
  BUTTON_EVENT_NONE = 0,   // 이벤트 없음
  BUTTON_EVENT_SHORT,      // 짧은 누름 (< 3초)
  BUTTON_EVENT_LONG        // 긴 누름 (>= 3초)
} button_event_t;

/**
 * @brief 버튼 핸들러 초기화
 */
void button_handler_init(void);

/**
 * @brief 버튼 현재 상태 확인
 * @return true = 눌림, false = 안 눌림
 */
bool button_is_pressed(void);

/**
 * @brief 버튼 이벤트 폴링 (논블로킹)
 * @return 감지된 이벤트 타입
 */
button_event_t button_poll_event(void);

/**
 * @brief 버튼 홀드 대기 (블로킹)
 * 
 * 버튼이 눌린 상태에서 지정 시간 홀드 여부 확인
 * @param hold_ms 홀드 시간 (밀리초)
 * @return true = 홀드 완료, false = 도중 해제
 */
bool button_wait_hold(uint32_t hold_ms);

/**
 * @brief 버튼 해제 대기 (블로킹)
 */
void button_wait_release(void);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_HANDLER_H
