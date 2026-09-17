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

#ifdef __cplusplus
extern "C" {
#endif

#define CAP_TAILSCALE_HOSTNAME_LEN 96
#define CAP_TAILSCALE_IP_LEN 16
#define CAP_TAILSCALE_REGION_NAME_LEN 48
#define CAP_TAILSCALE_ERROR_LEN 96
#define CAP_TAILSCALE_MAX_EXIT_NODES 16
#define CAP_TAILSCALE_MAX_DERP_RTTS 8

typedef struct {
    uint16_t id;
    char name[CAP_TAILSCALE_REGION_NAME_LEN];
} cap_tailscale_region_t;

typedef struct {
    cap_tailscale_region_t region;
    uint32_t latency_ms;
} cap_tailscale_derp_rtt_t;

typedef struct {
    char hostname[CAP_TAILSCALE_HOSTNAME_LEN];
    char ip[CAP_TAILSCALE_IP_LEN];
    char exit_state[CAP_TAILSCALE_ERROR_LEN];
    char egress[CAP_TAILSCALE_ERROR_LEN];
    bool connected;
    bool exit_node_active;
    cap_tailscale_derp_rtt_t derp_rtts[CAP_TAILSCALE_MAX_DERP_RTTS];
    size_t derp_rtt_count;
} cap_tailscale_status_t;

typedef struct {
    char hostname[CAP_TAILSCALE_HOSTNAME_LEN];
    char ip[CAP_TAILSCALE_IP_LEN];
    cap_tailscale_region_t region;
    bool online;
    bool selected;
} cap_tailscale_exit_node_t;

typedef struct {
    bool ok;
    char error[CAP_TAILSCALE_ERROR_LEN];
    char message[CAP_TAILSCALE_ERROR_LEN];
    char selected_ip[CAP_TAILSCALE_IP_LEN];
    char selected_hostname[CAP_TAILSCALE_HOSTNAME_LEN];
    char exit_state[CAP_TAILSCALE_ERROR_LEN];
    char egress[CAP_TAILSCALE_ERROR_LEN];
    bool persisted;
} cap_tailscale_mutation_result_t;

typedef esp_err_t (*cap_tailscale_get_status_fn)(cap_tailscale_status_t *out_status, void *user_ctx);
typedef esp_err_t (*cap_tailscale_list_exit_nodes_fn)(cap_tailscale_exit_node_t *out_nodes,
                                                       size_t max_nodes,
                                                       size_t *out_count,
                                                       void *user_ctx);
typedef esp_err_t (*cap_tailscale_set_exit_node_fn)(const char *selector,
                                                     cap_tailscale_mutation_result_t *out_result,
                                                     void *user_ctx);
typedef esp_err_t (*cap_tailscale_clear_exit_node_fn)(cap_tailscale_mutation_result_t *out_result,
                                                       void *user_ctx);
typedef esp_err_t (*cap_tailscale_reconnect_fn)(cap_tailscale_mutation_result_t *out_result,
                                                 void *user_ctx);

/**
 * Provider callbacks return transport/runtime failures as esp_err_t. For a
 * request that executes but is rejected semantically, return ESP_OK and
 * describe that rejection in out_result with ok set to false.
 */
typedef struct {
    cap_tailscale_get_status_fn get_status;
    cap_tailscale_list_exit_nodes_fn list_exit_nodes;
    cap_tailscale_set_exit_node_fn set_exit_node;
    cap_tailscale_clear_exit_node_fn clear_exit_node;
    cap_tailscale_reconnect_fn reconnect;
    void *user_ctx;
} cap_tailscale_provider_t;

esp_err_t cap_tailscale_set_provider(const cap_tailscale_provider_t *provider);
esp_err_t cap_tailscale_register_group(void);

#ifdef __cplusplus
}
#endif
