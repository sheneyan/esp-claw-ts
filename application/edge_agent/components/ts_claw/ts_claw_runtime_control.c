#include "ts_claw_runtime_control.h"

#include <string.h>

static bool runtime_ops_valid(const ts_claw_runtime_ops_t *ops)
{
    return ops != NULL && ops->retire_probe != NULL && ops->destroy != NULL &&
           ops->set_desired_ip != NULL && ops->start != NULL &&
           ops->rebind != NULL && ops->observe_reconnect_status != NULL &&
           ops->observe_connected != NULL &&
           ops->observe_exit_active != NULL && ops->now_ms != NULL;
}

bool ts_claw_runtime_wifi_pending_record(
    ts_claw_runtime_wifi_pending_t *pending, bool has_ip, void *netif)
{
    if (pending == NULL) {
        return false;
    }
    const bool notify_worker = !pending->pending;
    pending->pending = true;
    pending->has_ip = has_ip;
    pending->netif = has_ip ? netif : NULL;
    return notify_worker;
}

bool ts_claw_runtime_wifi_pending_take(
    ts_claw_runtime_wifi_pending_t *pending, bool runtime_active,
    bool *has_ip, void **netif)
{
    if (pending == NULL || has_ip == NULL || netif == NULL ||
        !pending->pending || (pending->has_ip && runtime_active)) {
        return false;
    }
    *has_ip = pending->has_ip;
    *netif = pending->netif;
    pending->pending = false;
    return true;
}

void ts_claw_runtime_destroy_retry_schedule(
    ts_claw_runtime_destroy_retry_t *retry,
    ts_claw_runtime_destroy_retry_mode_t mode,
    uint64_t current_ms,
    uint32_t delay_ms)
{
    if (retry == NULL) {
        return;
    }
    if (mode == TS_CLAW_RUNTIME_DESTROY_RETRY_STOP ||
        retry->mode == TS_CLAW_RUNTIME_DESTROY_RETRY_NONE) {
        retry->mode = mode;
    }
    retry->scheduled_ms = current_ms;
    retry->delay_ms = delay_ms;
}

bool ts_claw_runtime_destroy_retry_due(
    const ts_claw_runtime_destroy_retry_t *retry, uint64_t current_ms)
{
    return retry != NULL &&
           retry->mode != TS_CLAW_RUNTIME_DESTROY_RETRY_NONE &&
           current_ms - retry->scheduled_ms >= retry->delay_ms;
}

void ts_claw_runtime_destroy_retry_clear(
    ts_claw_runtime_destroy_retry_t *retry)
{
    if (retry != NULL) {
        memset(retry, 0, sizeof(*retry));
    }
}

static bool runtime_deadline_reached(uint64_t started_ms,
                                     uint32_t timeout_ms,
                                     uint64_t current_ms)
{
    return timeout_ms > 0u && current_ms - started_ms >= timeout_ms;
}

static void runtime_finish(ts_claw_runtime_control_t *control,
                           esp_err_t result,
                           bool exit_active,
                           bool rollback_recovered)
{
    control->completion.selected_exit_node_ip = control->current_desired_ip;
    control->completion.exit_active = exit_active;
    control->completion.rollback_recovered = rollback_recovered;
    control->operation_error = result;
    control->phase = TS_CLAW_RUNTIME_IDLE;
    control->active = false;
    control->completion_ready = true;
    control->reconnect_baseline_ctrl_rx = 0u;
    control->reconnect_disconnect_seen = false;
}

static void runtime_start_rollback(ts_claw_runtime_control_t *control,
                                   esp_err_t operation_error)
{
    const esp_err_t retire_error = control->ops->retire_probe(control->ops_ctx);
    const esp_err_t destroy_error = retire_error == ESP_OK ?
        control->ops->destroy(control->ops_ctx) : retire_error;
    const esp_err_t restore_error = control->ops->set_desired_ip(
        control->ops_ctx, control->old_desired_ip);

    control->completion.rollback_attempted = true;
    control->operation_error = operation_error;
    if (restore_error == ESP_OK) {
        control->current_desired_ip = control->old_desired_ip;
    }
    if (retire_error != ESP_OK || destroy_error != ESP_OK ||
        restore_error != ESP_OK) {
        runtime_finish(control, operation_error, false, false);
        return;
    }

    const esp_err_t start_error = control->ops->start(control->ops_ctx);
    if (start_error != ESP_OK) {
        runtime_finish(control, operation_error, false, false);
        return;
    }
    control->rollback_started_ms = control->ops->now_ms(control->ops_ctx);
    control->phase = TS_CLAW_RUNTIME_ROLLBACK_WAIT_CONNECTED;
}

static void runtime_fail_set(ts_claw_runtime_control_t *control,
                             esp_err_t operation_error)
{
    runtime_start_rollback(control, operation_error);
}

void ts_claw_runtime_control_init(ts_claw_runtime_control_t *control,
                                  const ts_claw_runtime_ops_t *ops,
                                  void *ops_ctx)
{
    if (control == NULL) {
        return;
    }
    memset(control, 0, sizeof(*control));
    control->ops = ops;
    control->ops_ctx = ops_ctx;
    control->configured = runtime_ops_valid(ops);
}

esp_err_t ts_claw_runtime_control_begin_set(ts_claw_runtime_control_t *control,
                                            uint32_t old_desired_ip,
                                            uint32_t requested_ip,
                                            uint32_t timeout_ms,
                                            uint64_t current_ms)
{
    if (control == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!control->configured || control->active || control->completion_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    control->active = true;
    control->started_ms = current_ms;
    control->timeout_ms = timeout_ms;
    control->old_desired_ip = old_desired_ip;
    control->current_desired_ip = old_desired_ip;
    control->operation_error = ESP_OK;
    control->reconnect_baseline_ctrl_rx = 0u;
    control->reconnect_disconnect_seen = false;
    memset(&control->completion, 0, sizeof(control->completion));

    esp_err_t error = control->ops->retire_probe(control->ops_ctx);
    if (error != ESP_OK) {
        runtime_fail_set(control, error);
        return ESP_OK;
    }
    error = control->ops->destroy(control->ops_ctx);
    if (error != ESP_OK) {
        runtime_fail_set(control, error);
        return ESP_OK;
    }
    error = control->ops->set_desired_ip(control->ops_ctx, requested_ip);
    if (error != ESP_OK) {
        runtime_fail_set(control, error);
        return ESP_OK;
    }
    control->current_desired_ip = requested_ip;
    error = control->ops->start(control->ops_ctx);
    if (error != ESP_OK) {
        runtime_fail_set(control, error);
        return ESP_OK;
    }

    control->phase = TS_CLAW_RUNTIME_WAIT_CONNECTED;
    return ESP_OK;
}

esp_err_t ts_claw_runtime_control_begin_reconnect(ts_claw_runtime_control_t *control,
                                                  uint32_t timeout_ms,
                                                  uint64_t current_ms)
{
    if (control == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!control->configured || control->active || control->completion_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    control->active = true;
    control->started_ms = current_ms;
    control->timeout_ms = timeout_ms;
    control->operation_error = ESP_OK;
    control->reconnect_baseline_ctrl_rx = 0u;
    control->reconnect_disconnect_seen = false;
    memset(&control->completion, 0, sizeof(control->completion));

    bool connected = false;
    uint64_t control_rx_token = 0u;
    esp_err_t error = control->ops->observe_reconnect_status(
        control->ops_ctx, &connected, &control_rx_token);
    (void)connected;
    if (error != ESP_OK) {
        runtime_finish(control, error, false, false);
        return ESP_OK;
    }
    control->reconnect_baseline_ctrl_rx = control_rx_token;

    error = control->ops->rebind(control->ops_ctx);
    if (error != ESP_OK) {
        runtime_finish(control, error, false, false);
    } else {
        control->phase = TS_CLAW_RUNTIME_RECONNECT_WAIT_CONNECTED;
    }
    return ESP_OK;
}

static bool runtime_observe_connected(ts_claw_runtime_control_t *control,
                                      uint64_t current_ms,
                                      bool rollback)
{
    const uint64_t started_ms = rollback ? control->rollback_started_ms :
                                          control->started_ms;
    if (runtime_deadline_reached(started_ms, control->timeout_ms, current_ms)) {
        if (rollback) {
            runtime_finish(control, control->operation_error, false, false);
        } else {
            runtime_fail_set(control, ESP_ERR_TIMEOUT);
        }
        return false;
    }

    bool connected = false;
    const esp_err_t error = control->ops->observe_connected(
        control->ops_ctx, &connected);
    if (error != ESP_OK) {
        if (rollback) {
            runtime_finish(control, control->operation_error, false, false);
        } else {
            runtime_fail_set(control, error);
        }
        return false;
    }
    if (connected) {
        if (control->current_desired_ip == 0u) {
            runtime_finish(control, rollback ? control->operation_error : ESP_OK,
                           false, rollback);
        } else {
            control->phase = rollback ?
                TS_CLAW_RUNTIME_ROLLBACK_WAIT_EXIT_ACTIVE :
                TS_CLAW_RUNTIME_WAIT_EXIT_ACTIVE;
            return true;
        }
        return false;
    }
    if (control->timeout_ms == 0u) {
        if (rollback) {
            runtime_finish(control, control->operation_error, false, false);
        } else {
            runtime_fail_set(control, ESP_ERR_TIMEOUT);
        }
    }
    return false;
}

static void runtime_observe_exit(ts_claw_runtime_control_t *control,
                                 uint64_t current_ms,
                                 bool rollback)
{
    const uint64_t started_ms = rollback ? control->rollback_started_ms :
                                          control->started_ms;
    if (runtime_deadline_reached(started_ms, control->timeout_ms, current_ms)) {
        if (rollback) {
            runtime_finish(control, control->operation_error, false, false);
        } else {
            runtime_fail_set(control, ESP_ERR_TIMEOUT);
        }
        return;
    }

    bool active = false;
    const esp_err_t error = control->ops->observe_exit_active(
        control->ops_ctx, &active);
    if (error != ESP_OK) {
        if (rollback) {
            runtime_finish(control, control->operation_error, false, false);
        } else {
            runtime_fail_set(control, error);
        }
        return;
    }
    if (active) {
        runtime_finish(control, rollback ? control->operation_error : ESP_OK,
                       true, rollback);
        return;
    }
    if (control->timeout_ms == 0u) {
        if (rollback) {
            runtime_finish(control, control->operation_error, false, false);
        } else {
            runtime_fail_set(control, ESP_ERR_TIMEOUT);
        }
    }
}

static void runtime_observe_reconnect(ts_claw_runtime_control_t *control,
                                      uint64_t current_ms)
{
    if (runtime_deadline_reached(control->started_ms, control->timeout_ms,
                                 current_ms)) {
        runtime_finish(control, ESP_ERR_TIMEOUT, false, false);
        return;
    }

    bool connected = false;
    uint64_t control_rx_token = 0u;
    const esp_err_t error = control->ops->observe_reconnect_status(
        control->ops_ctx, &connected, &control_rx_token);
    if (error != ESP_OK) {
        runtime_finish(control, error, false, false);
        return;
    }
    if (!connected) {
        control->reconnect_disconnect_seen = true;
    }
    /* A still-connected pre-rebind session is not proof of reconnection. */
    if (control->reconnect_disconnect_seen && connected &&
        control_rx_token != 0u &&
        control_rx_token > control->reconnect_baseline_ctrl_rx) {
        runtime_finish(control, ESP_OK, false, false);
        return;
    }
    if (control->timeout_ms == 0u) {
        runtime_finish(control, ESP_ERR_TIMEOUT, false, false);
    }
}

static void runtime_advance_rollback(ts_claw_runtime_control_t *control,
                                     uint64_t current_ms)
{
    if (!control->active) {
        return;
    }
    if (control->phase == TS_CLAW_RUNTIME_ROLLBACK_WAIT_CONNECTED) {
        if (runtime_observe_connected(control, current_ms, true) &&
            control->active) {
            runtime_observe_exit(control, current_ms, true);
        }
    } else if (control->phase == TS_CLAW_RUNTIME_ROLLBACK_WAIT_EXIT_ACTIVE) {
        runtime_observe_exit(control, current_ms, true);
    }
}

void ts_claw_runtime_control_advance(ts_claw_runtime_control_t *control,
                                     uint64_t current_ms)
{
    if (control == NULL || !control->active) {
        return;
    }

    switch (control->phase) {
    case TS_CLAW_RUNTIME_WAIT_CONNECTED:
        if (runtime_observe_connected(control, current_ms, false) &&
            control->active) {
            runtime_observe_exit(control, current_ms, false);
        }
        break;
    case TS_CLAW_RUNTIME_WAIT_EXIT_ACTIVE:
        runtime_observe_exit(control, current_ms, false);
        break;
    case TS_CLAW_RUNTIME_ROLLBACK_WAIT_CONNECTED:
    case TS_CLAW_RUNTIME_ROLLBACK_WAIT_EXIT_ACTIVE:
        runtime_advance_rollback(control, current_ms);
        break;
    case TS_CLAW_RUNTIME_RECONNECT_WAIT_CONNECTED:
        runtime_observe_reconnect(control, current_ms);
        break;
    case TS_CLAW_RUNTIME_IDLE:
    default:
        runtime_finish(control, ESP_ERR_INVALID_STATE, false, false);
        break;
    }
}

bool ts_claw_runtime_control_is_active(const ts_claw_runtime_control_t *control)
{
    return control != NULL && control->active;
}

bool ts_claw_runtime_control_take_completion(
    ts_claw_runtime_control_t *control,
    esp_err_t *operation_result,
    ts_claw_runtime_completion_t *completion)
{
    if (control == NULL || operation_result == NULL || completion == NULL ||
        !control->completion_ready) {
        return false;
    }
    *operation_result = control->operation_error;
    *completion = control->completion;
    control->completion_ready = false;
    return true;
}
