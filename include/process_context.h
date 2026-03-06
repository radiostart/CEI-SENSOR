/**
 * @file process_context.h
 * @brief 발효 공정 컨텍스트 관리 (NVS 저장/복원)
 */

#ifndef PROCESS_CONTEXT_H
#define PROCESS_CONTEXT_H

#include "app_config.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NVS에서 공정 컨텍스트 로드 (부팅 시 복원)
 * @param ctx 로드할 컨텍스트 포인터
 * @return ESP_OK 성공, ESP_ERR_NOT_FOUND 저장된 컨텍스트 없음
 */
esp_err_t process_context_load(process_context_t *ctx);

/**
 * @brief 공정 컨텍스트를 NVS에 저장
 * @param ctx 저장할 컨텍스트 포인터
 * @return ESP_OK 성공
 */
esp_err_t process_context_save(const process_context_t *ctx);

/**
 * @brief 현재 경과 시간(초) 저장 (재부팅 복원용)
 * @param elapsed_sec 경과 초
 * @return ESP_OK 성공
 */
esp_err_t process_context_save_elapsed(uint32_t elapsed_sec);

/**
 * @brief 저장된 경과 시간(초) 로드
 * @param elapsed_sec 복원할 경과 초 포인터
 * @return ESP_OK 성공
 */
esp_err_t process_context_load_elapsed(uint32_t *elapsed_sec);

/**
 * @brief 공정 컨텍스트를 기본값(비활성)으로 초기화
 * @param ctx 초기화할 포인터
 */
void process_context_reset(process_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif  // PROCESS_CONTEXT_H
