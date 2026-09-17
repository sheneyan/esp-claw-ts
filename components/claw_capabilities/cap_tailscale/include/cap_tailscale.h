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
    uint16_t rtt_ms;
    bool timed_out;
} cap_tailscale_derp_rtt_t;

typedef struct {
    bool enabled;
    bool connected;
    char hostname[CAP_TAILSCALE_HOSTNAME_LEN];
    char vpn_ip[CAP_TAILSCALE_IP_LEN];
    char path[16];
    int peer_count;
    int peer_online;
    char exit_node[CAP_TAILSCALE_IP_LEN];
    char exit_state[16];
    char egress[16];
    char last_error[CAP_TAILSCALE_ERROR_LEN];
    cap_tailscale_region_t derp_active;
    cap_tailscale_region_t derp_default;
    cap_tailscale_derp_rtt_t derp_rtts[CAP_TAILSCALE_MAX_DERP_RTTS];
    size_t derp_rtt_count;
    uint64_t derp_heartbeat_age_ms;
    uint64_t control_rx_age_ms;
    uint32_t reconnect_coord_watchdog;
    uint32_t reconnect_coord_transport;
    uint32_t reconnect_derp_watchdog;
    uint32_t reconnect_derp_retry;
} cap_tailscale_status_t;

typedef struct {
    char ip[CAP_TAILSCALE_IP_LEN];
    char hostname[CAP_TAILSCALE_HOSTNAME_LEN];
    bool online;
    bool direct;
    cap_tailscale_region_t derp_region;
} cap_tailscale_exit_node_t;

typedef struct {
    bool ok;
    char error[32];
    char message[CAP_TAILSCALE_ERROR_LEN];
    char selected_ip[CAP_TAILSCALE_IP_LEN];
    char selected_hostname[CAP_TAILSCALE_HOSTNAME_LEN];
    char exit_state[16];
    char egress[16];
    bool persisted;
} cap_tailscale_mutation_result_t;

/**
 * The set and clear callbacks return transport/runtime failures as esp_err_t.
 * For a request that executes but is rejected semantically, return ESP_OK and
 * describe that rejection in out with ok set to false.
 */
typedef struct {
    esp_err_t (*get_status)(cap_tailscale_status_t *out, void *ctx);
    int (*list_exit_nodes)(cap_tailscale_exit_node_t *out, size_t capacity, void *ctx);
    esp_err_t (*set_exit_node)(const char *selector,
                               cap_tailscale_mutation_result_t *out,
                               void *ctx);
    esp_err_t (*clear_exit_node)(cap_tailscale_mutation_result_t *out, void *ctx);
    esp_err_t (*reconnect)(cap_tailscale_status_t *out, void *ctx);
    void *ctx;
} cap_tailscale_provider_t;

esp_err_t cap_tailscale_set_provider(const cap_tailscale_provider_t *provider);
esp_err_t cap_tailscale_register_group(void);

#ifdef __cplusplus
}
#endif
