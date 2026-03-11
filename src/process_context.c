/**
 * @file process_context.c
 * @brief 발효 공정 컨텍스트 NVS 저장/복원 구현
 */

#include "process_context.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "PROC_CTX";

#define NVS_NAMESPACE "mellowair"
#define NVS_KEY_CTX   "proc_ctx"
#define NVS_KEY_ELAPSED "elapsed"

esp_err_t process_context_load(process_context_t *ctx) {
  if (ctx == NULL) return ESP_ERR_INVALID_ARG;

  nvs_handle_t handle;
  esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (ret != ESP_OK) {
    ESP_LOGD(TAG, "NVS namespace not found: %s", esp_err_to_name(ret));
    process_context_reset(ctx);
    return ESP_ERR_NOT_FOUND;
  }

  size_t size = sizeof(process_context_t);
  ret = nvs_get_blob(handle, NVS_KEY_CTX, ctx, &size);
  nvs_close(handle);

  if (ret != ESP_OK) {
    ESP_LOGD(TAG, "No process context in NVS");
    process_context_reset(ctx);
    return ESP_ERR_NOT_FOUND;
  }

  ESP_LOGI(TAG, "Process context loaded: name='%s', active=%d",
           ctx->process_name, ctx->is_active);
  return ESP_OK;
}

esp_err_t process_context_save(const process_context_t *ctx) {
  if (ctx == NULL) return ESP_ERR_INVALID_ARG;

  nvs_handle_t handle;
  esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = nvs_set_blob(handle, NVS_KEY_CTX, ctx, sizeof(process_context_t));
  if (ret == ESP_OK) {
    ret = nvs_commit(handle);
  }
  nvs_close(handle);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Process context saved: name='%s'", ctx->process_name);
  } else {
    ESP_LOGE(TAG, "NVS save failed: %s", esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t process_context_save_elapsed(uint32_t elapsed_sec) {
  nvs_handle_t handle;
  esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (ret != ESP_OK) return ret;

  ret = nvs_set_u32(handle, NVS_KEY_ELAPSED, elapsed_sec);
  if (ret == ESP_OK) {
    ret = nvs_commit(handle);
  }
  nvs_close(handle);
  return ret;
}

esp_err_t process_context_load_elapsed(uint32_t *elapsed_sec) {
  if (elapsed_sec == NULL) return ESP_ERR_INVALID_ARG;

  nvs_handle_t handle;
  esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (ret != ESP_OK) {
    *elapsed_sec = 0;
    return ret;
  }

  ret = nvs_get_u32(handle, NVS_KEY_ELAPSED, elapsed_sec);
  nvs_close(handle);

  if (ret != ESP_OK) {
    *elapsed_sec = 0;
  }
  return ret;
}

void process_context_reset(process_context_t *ctx) {
  if (ctx == NULL) return;
  memset(ctx, 0, sizeof(process_context_t));
  // 모든 값 0, is_active = false → 디스플레이에 "--" 표시
  // 앱 연결 시 PROCESS_CONFIG로 공정 설정 수신
}
