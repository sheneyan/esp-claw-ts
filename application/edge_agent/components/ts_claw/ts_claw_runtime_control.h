#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t (*retire_probe)(void *ctx);
    esp_err_t (*destroy)(void *ctx);
    esp_err_t (*set_desired_ip)(void *ctx, uint32_t desired_ip);
    esp_err_t (*start)(void *ctx);
    esp_err_t (*rebind)(void *ctx);
    esp_err_t (*observe_reconnect_status)(void *ctx,
                                          bool *connected,
                                          uint64_t *control_rx_token);
    esp_err_t (*observe_connected)(void *ctx, bool *connected);
    esp_err_t (*observe_exit_active)(void *ctx, bool *active);
} ts_claw_runtime_ops_t;

typedef struct {
    uint32_t selected_exit_node_ip;
    bool exit_active;
    bool rollback_attempted;
    bool rollback_recovered;
} ts_claw_runtime_completion_t;

typedef enum {
    TS_CLAW_RUNTIME_IDLE = 0,
    TS_CLAW_RUNTIME_WAIT_CONNECTED,
    TS_CLAW_RUNTIME_WAIT_EXIT_ACTIVE,
    TS_CLAW_RUNTIME_ROLLBACK_WAIT_CONNECTED,
    TS_CLAW_RUNTIME_ROLLBACK_WAIT_EXIT_ACTIVE,
    TS_CLAW_RUNTIME_RECONNECT_WAIT_CONNECTED,
} ts_claw_runtime_phase_t;

typedef struct {
    const ts_claw_runtime_ops_t *ops;
    void *ops_ctx;
    ts_claw_runtime_phase_t phase;
    uint64_t started_ms;
    uint64_t rollback_started_ms;
    uint64_t reconnect_baseline_ctrl_rx;
    uint32_t timeout_ms;
    uint32_t old_desired_ip;
    uint32_t current_desired_ip;
    esp_err_t operation_error;
    ts_claw_runtime_completion_t completion;
    bool configured;
    bool active;
    bool completion_ready;
} ts_claw_runtime_control_t;

void ts_claw_runtime_control_init(ts_claw_runtime_control_t *control,
                                  const ts_claw_runtime_ops_t *ops,
                                  void *ops_ctx);
esp_err_t ts_claw_runtime_control_begin_set(ts_claw_runtime_control_t *control,
                                            uint32_t old_desired_ip,
                                            uint32_t requested_ip,
                                            uint32_t timeout_ms,
                                            uint64_t current_ms);
esp_err_t ts_claw_runtime_control_begin_reconnect(ts_claw_runtime_control_t *control,
                                                  uint32_t timeout_ms,
                                                  uint64_t current_ms);
void ts_claw_runtime_control_advance(ts_claw_runtime_control_t *control,
                                     uint64_t current_ms);
bool ts_claw_runtime_control_is_active(const ts_claw_runtime_control_t *control);
bool ts_claw_runtime_control_take_completion(
    ts_claw_runtime_control_t *control,
    esp_err_t *operation_result,
    ts_claw_runtime_completion_t *completion);

#ifdef __cplusplus
}
#endif
