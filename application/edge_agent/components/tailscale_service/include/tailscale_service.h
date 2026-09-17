/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ts_claw.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TAILSCALE_SERVICE_OK = 0,
    TAILSCALE_SERVICE_NOT_ENABLED,
    TAILSCALE_SERVICE_NOT_CONNECTED,
    TAILSCALE_SERVICE_INVALID_NODE,
    TAILSCALE_SERVICE_AMBIGUOUS_NODE,
    TAILSCALE_SERVICE_NODE_NOT_FOUND,
    TAILSCALE_SERVICE_NODE_OFFLINE,
    TAILSCALE_SERVICE_NOT_EXIT_NODE,
    TAILSCALE_SERVICE_SWITCH_TIMEOUT,
    TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED,
    TAILSCALE_SERVICE_PERSISTENCE_FAILED,
    TAILSCALE_SERVICE_ROLLBACK_FAILED,
    TAILSCALE_SERVICE_RECONNECT_FAILED,
    TAILSCALE_SERVICE_BUSY,
} tailscale_service_error_t;

typedef struct {
    esp_err_t (*get_diagnostics)(ts_claw_diagnostics_t *out, void *ctx);
    int (*list_exit_nodes)(ts_claw_peer_t *out, size_t capacity, void *ctx);
    esp_err_t (*apply_exit_node)(uint32_t ip, ts_claw_runtime_result_t *out, void *ctx);
    esp_err_t (*rebind)(void *ctx);
    esp_err_t (*load_persisted_exit)(char out[16], void *ctx);
    /* Return ESP_ERR_INVALID_RESPONSE when the final persisted value is unknown. */
    esp_err_t (*save_persisted_exit)(const char *ip, void *ctx);
    void *ctx;
} tailscale_service_ops_t;

typedef struct tailscale_service *tailscale_service_handle_t;

typedef struct {
    bool ok;
    tailscale_service_error_t error;
    char message[96];
    char selected_ip[16];
    char selected_hostname[96];
    ts_exit_state_t exit_state;
    char egress[16];
    bool persisted;
    bool rollback_attempted;
    bool rollback_recovered;
} tailscale_service_result_t;

esp_err_t tailscale_service_create(const tailscale_service_ops_t *ops,
                                   tailscale_service_handle_t *out);

/* The owner must not delete the service while any call is in progress. */
void tailscale_service_delete(tailscale_service_handle_t service);

/*
 * Reads do not take the mutation mutex. They use independent heap snapshots,
 * so they may run concurrently with a mutation when the supplied read ops are
 * themselves thread-safe. Every returned string is copied and bounded.
 */
esp_err_t tailscale_service_get_diagnostics(tailscale_service_handle_t service,
                                            ts_claw_diagnostics_t *out);
int tailscale_service_list_exit_nodes(tailscale_service_handle_t service,
                                      ts_claw_peer_t *out,
                                      size_t capacity);

/* Mutations acquire service ownership without waiting and otherwise report busy. */
esp_err_t tailscale_service_set_exit_node(tailscale_service_handle_t service,
                                          const char *selector,
                                          tailscale_service_result_t *out);
esp_err_t tailscale_service_clear_exit_node(tailscale_service_handle_t service,
                                            tailscale_service_result_t *out);
esp_err_t tailscale_service_reconnect(tailscale_service_handle_t service,
                                      tailscale_service_result_t *out);

const char *tailscale_service_error_name(tailscale_service_error_t error);

#ifdef __cplusplus
}
#endif
