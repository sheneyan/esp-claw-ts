/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool wifi_connected;
    const char *ip;
    bool ap_active;
    const char *ap_ssid;
    const char *ap_ip;
    const char *wifi_mode;
} http_server_wifi_status_t;

typedef struct {
    bool active;
    bool configured;
    bool completed;
    bool persisted;
    char session_key[64];
    char status[32];
    char message[160];
    char qr_data_url[256];
    char account_id[64];
    char user_id[96];
    char token[256];
    char base_url[160];
} http_server_wechat_login_status_t;

#define HTTP_SERVER_TAILSCALE_IP_LEN             16
#define HTTP_SERVER_TAILSCALE_HOSTNAME_LEN       64
#define HTTP_SERVER_TAILSCALE_STATE_LEN          16
#define HTTP_SERVER_TAILSCALE_LAST_ERROR_LEN     96
#define HTTP_SERVER_TAILSCALE_MAX_EXIT_NODES     64

typedef struct {
    bool enabled;
    bool connected;
    bool auth_key_set;
    char vpn_ip[HTTP_SERVER_TAILSCALE_IP_LEN];
    char path[HTTP_SERVER_TAILSCALE_STATE_LEN];
    int peer_count;
    int peer_online;
    char exit_node[HTTP_SERVER_TAILSCALE_IP_LEN];
    char exit_state[HTTP_SERVER_TAILSCALE_STATE_LEN];
    char egress[HTTP_SERVER_TAILSCALE_STATE_LEN];
    char last_error[HTTP_SERVER_TAILSCALE_LAST_ERROR_LEN];
    size_t heap_internal_free;
    size_t heap_internal_largest;
    size_t heap_psram_free;
} http_server_tailscale_status_t;

typedef struct {
    char ip[HTTP_SERVER_TAILSCALE_IP_LEN];
    char hostname[HTTP_SERVER_TAILSCALE_HOSTNAME_LEN];
    bool online;
    bool direct;
    uint16_t derp_region;
} http_server_tailscale_exit_node_t;

typedef struct {
    esp_err_t (*load_config)(app_config_t *config);
    esp_err_t (*save_config)(const app_config_t *before,
                             const app_config_t *after);
    esp_err_t (*get_wifi_status)(http_server_wifi_status_t *status);
    esp_err_t (*restart_device)(void);
    esp_err_t (*wechat_login_start)(const char *account_id, bool force);
    esp_err_t (*wechat_login_get_status)(http_server_wechat_login_status_t *status);
    esp_err_t (*wechat_login_cancel)(void);
    esp_err_t (*wechat_login_mark_persisted)(void);
    esp_err_t (*get_tailscale_status)(http_server_tailscale_status_t *status);
    /* Returns the number of copied entries, or a negative esp_err_t value. */
    int (*get_tailscale_exit_nodes)(http_server_tailscale_exit_node_t *nodes, int capacity);
} http_server_services_t;

typedef struct {
    const char *storage_base_path;
    http_server_services_t services;
} http_server_config_t;

esp_err_t http_server_init(const http_server_config_t *config);
esp_err_t http_server_start(void);
esp_err_t http_server_stop(void);
esp_err_t http_server_webim_bind_im(void);

#ifdef __cplusplus
}
#endif
