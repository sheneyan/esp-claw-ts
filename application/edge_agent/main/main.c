/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "app_claw.h"
#include "app_capabilities.h"
#include "app_fs.h"
#include "claw_version.h"
#include "claw_paths.h"
#include "edge_agent_version.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "wifi_manager.h"
#include "time.h"
#include "nvs_flash.h"
#include "http_server.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_board_manager_includes.h"
#include "captive_dns.h"
#include "cmd_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_APP_CLAW_CAP_IM_WECHAT
#include "cap_im_wechat.h"
#endif
#include "app_config.h"
#if CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW
#include "cap_tailscale.h"
#include "provision_button.h"
#include "settings_store.h"
#include "tailscale_service.h"
#include "tailscale_persistence.h"
#include "ts_claw.h"
#include "lwip/def.h"
#endif

#define APP_ENABLE_MEM_LOG        (0)
#define MAIN_TAILSCALE_RUNTIME_TIMEOUT_MS 30000u

static const char *TAG = "app";

static app_config_t *s_config;
static app_claw_config_t *s_claw_config;
#if CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW
static bool s_ts_claw_initialized;
static tailscale_service_handle_t s_tailscale_service;
#endif

static esp_err_t app_allocate_runtime_state(void)
{
    if (!s_config) {
        s_config = calloc(1, sizeof(*s_config));
    }
    if (!s_claw_config) {
        s_claw_config = calloc(1, sizeof(*s_claw_config));
    }

    ESP_RETURN_ON_FALSE(s_config && s_claw_config, ESP_ERR_NO_MEM, TAG,
                        "Failed to allocate runtime state");

    return ESP_OK;
}

static void app_free_runtime_state(void)
{
    free(s_claw_config);
    s_claw_config = NULL;

    free(s_config);
    s_config = NULL;
}

static void log_wifi_startup_config(const app_config_t *config)
{
#if CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW
    const char *default_ap_behavior = "close_on_sta";
#else
    const char *default_ap_behavior = "keep";
#endif
    ESP_LOGI(TAG,
             "Wi-Fi startup STA: ssid=%s pwd_len=%u",
             config->wifi_ssid[0] ? config->wifi_ssid : "(empty)",
             (unsigned)strlen(config->wifi_password));

    ESP_LOGI(TAG,
             "Wi-Fi startup AP: ssid=%s pwd_len=%u behavior=%s",
             config->ap_ssid[0] ? config->ap_ssid : "(auto:mac-suffix)",
             (unsigned)strlen(config->ap_password),
             config->ap_behavior[0] ? config->ap_behavior : default_ap_behavior);
}

static void on_wifi_state_changed(bool connected, void *user_ctx)
{
    (void)user_ctx;

    wifi_manager_status_t status = {0};
    wifi_manager_get_status(&status);
    const char *ap_ssid = status.ap_active ? status.ap_ssid : NULL;

    ESP_LOGI(TAG, "Wi-Fi state: sta_connected=%d ap_active=%d mode=%s ap_ssid=%s",
             connected,
             status.ap_active,
             status.mode ? status.mode : "off",
             ap_ssid ? ap_ssid : "(none)");

    esp_err_t err = app_claw_set_network_status(connected, ap_ssid);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update network UI: %s", esp_err_to_name(err));
    }

#if CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW
    if (s_ts_claw_initialized) {
        esp_netif_t *sta_netif = connected ? wifi_manager_get_sta_netif() : NULL;
        err = ts_claw_notify_wifi(connected, sta_netif);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to notify TS-Claw of Wi-Fi state: %s",
                     esp_err_to_name(err));
        }
    }
#endif
}

static esp_err_t main_load_config(app_config_t *config)
{
    return app_config_load(config);
}

static esp_err_t main_save_config(const app_config_t *config)
{
    esp_err_t err;
    app_claw_config_t *claw_config = NULL;

    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_ERROR(app_config_validate_wifi(config, NULL), TAG, "Invalid Wi-Fi config");

    err = app_config_save(config);
    if (err != ESP_OK) {
        return err;
    }

    claw_config = calloc(1, sizeof(*claw_config));
    if (!claw_config) {
        ESP_LOGW(TAG, "Failed to allocate Claw config for runtime update");
        return ESP_OK;
    }
    app_config_to_claw(config, claw_config);
    err = app_claw_update_config(claw_config);
    free(claw_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to update running Claw config: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

static void main_copy_claw_to_app_config(const app_claw_config_t *src, app_config_t *dst)
{
    strlcpy(dst->llm_api_key, src->llm_api_key, sizeof(dst->llm_api_key));
    strlcpy(dst->llm_backend_type, src->llm_backend_type, sizeof(dst->llm_backend_type));
    strlcpy(dst->llm_model, src->llm_model, sizeof(dst->llm_model));
    strlcpy(dst->llm_base_url, src->llm_base_url, sizeof(dst->llm_base_url));
    strlcpy(dst->llm_auth_type, src->llm_auth_type, sizeof(dst->llm_auth_type));
    strlcpy(dst->llm_timeout_ms, src->llm_timeout_ms, sizeof(dst->llm_timeout_ms));
    strlcpy(dst->llm_max_tokens, src->llm_max_tokens, sizeof(dst->llm_max_tokens));
    strlcpy(dst->llm_default_image_max_bytes,
            src->llm_default_image_max_bytes,
            sizeof(dst->llm_default_image_max_bytes));
    strlcpy(dst->llm_max_tokens_field, src->llm_max_tokens_field, sizeof(dst->llm_max_tokens_field));
    strlcpy(dst->llm_supports_tools, src->llm_supports_tools, sizeof(dst->llm_supports_tools));
    strlcpy(dst->llm_supports_vision, src->llm_supports_vision, sizeof(dst->llm_supports_vision));
    strlcpy(dst->llm_image_remote_url_only,
            src->llm_image_remote_url_only,
            sizeof(dst->llm_image_remote_url_only));
}

static esp_err_t main_save_claw_config(const app_claw_config_t *config, void *user_ctx)
{
    esp_err_t err;
    app_config_t *app_config = NULL;

    (void)user_ctx;
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "config is NULL");

    app_config = calloc(1, sizeof(*app_config));
    ESP_RETURN_ON_FALSE(app_config, ESP_ERR_NO_MEM, TAG, "Failed to allocate app config for Claw save");

    err = app_config_load(app_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load config for Claw save: %s", esp_err_to_name(err));
        free(app_config);
        return err;
    }
    main_copy_claw_to_app_config(config, app_config);
    err = app_config_save(app_config);
    free(app_config);
    return err;
}

static esp_err_t main_get_wifi_status(http_server_wifi_status_t *status)
{
    ESP_RETURN_ON_FALSE(status, ESP_ERR_INVALID_ARG, TAG, "status is NULL");

    wifi_manager_status_t wifi_status = {0};
    wifi_manager_get_status(&wifi_status);
    status->wifi_connected = wifi_status.sta_connected;
    status->ip = wifi_status.sta_ip;
    status->ap_active = wifi_status.ap_active;
    status->ap_ssid = wifi_status.ap_ssid;
    status->ap_ip = wifi_status.ap_ip;
    status->wifi_mode = wifi_status.mode;
    return ESP_OK;
}

static void main_restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static esp_err_t main_restart_device(void)
{
    BaseType_t ok = xTaskCreate(main_restart_task, "http_restart", 2048, NULL, 5, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "Failed to create restart task");
    return ESP_OK;
}

#if CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW
static bool main_parse_uint8_range(const char *value, uint8_t minimum,
                                   uint8_t maximum, uint8_t *out)
{
    unsigned parsed = 0;

    if (!value || !value[0] || !out) {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        parsed = parsed * 10U + (unsigned)(*cursor - '0');
        if (parsed > maximum) {
            return false;
        }
    }
    if (parsed < minimum) {
        return false;
    }
    *out = (uint8_t)parsed;
    return true;
}

static bool main_parse_cgnat_ipv4(const char *value, uint32_t *out)
{
    esp_ip4_addr_t parsed = {0};

    if (!value || !value[0] || !out || esp_netif_str_to_ip4(value, &parsed) != ESP_OK) {
        return false;
    }
    const uint32_t host_order = lwip_ntohl(parsed.addr);
    if (host_order < UINT32_C(0x64400000) || host_order > UINT32_C(0x647fffff)) {
        return false;
    }
    *out = host_order;
    return true;
}

static esp_err_t main_build_ts_claw_config(const app_config_t *config,
                                           ts_claw_config_t *out)
{
    char validation_message[96] = {0};

    ESP_RETURN_ON_FALSE(config && out, ESP_ERR_INVALID_ARG, TAG,
                        "Missing TS-Claw startup config");
    ESP_RETURN_ON_ERROR(app_config_validate_tailscale(config, validation_message,
                                                      sizeof(validation_message)),
                        TAG, "Invalid TS-Claw settings: %s", validation_message);

    memset(out, 0, sizeof(*out));
    out->enabled = strcmp(config->tailscale_enabled, "true") == 0 ||
                   strcmp(config->tailscale_enabled, "1") == 0;
    out->auth_key = config->tailscale_auth_key;
    out->hostname = config->tailscale_hostname;
    out->login_server = config->tailscale_login_server;

    if (!main_parse_uint8_range(config->tailscale_max_peers, 1, 64, &out->max_peers)) {
        if (out->enabled) {
            return ESP_ERR_INVALID_ARG;
        }
        out->max_peers = 16;
    }

    if (!out->enabled) {
        return ESP_OK;
    }
    if (!out->auth_key[0] || !out->hostname[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->tailscale_exit_node[0] &&
        !main_parse_cgnat_ipv4(config->tailscale_exit_node, &out->exit_node_ip)) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t main_init_ts_claw(const app_config_t *config)
{
    ts_claw_config_t runtime_config = {0};
    esp_err_t err = main_build_ts_claw_config(config, &runtime_config);
    if (err == ESP_OK) {
        err = ts_claw_init(&runtime_config);
    }
    if (err == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGE(TAG,
             "TS-Claw settings could not be activated (%s); starting disabled for LAN recovery",
             esp_err_to_name(err));
    const ts_claw_config_t disabled_config = {
        .enabled = false,
        .auth_key = "",
        .hostname = "",
        .login_server = "",
        .exit_node_ip = 0,
        .max_peers = 16,
    };
    return ts_claw_init(&disabled_config);
}

static const char *main_tailscale_exit_state_name(ts_exit_state_t state)
{
    switch (state) {
    case TS_EXIT_DISABLED:
        return "disabled";
    case TS_EXIT_PENDING:
        return "pending";
    case TS_EXIT_ACTIVE:
        return "active";
    case TS_EXIT_FALLBACK:
        return "fallback";
    default:
        return "unknown";
    }
}

static void main_format_host_order_ipv4(uint32_t ip, char *out, size_t capacity)
{
    if (capacity == 0u) {
        return;
    }
    snprintf(out, capacity, "%u.%u.%u.%u",
             (unsigned)((ip >> 24) & 0xFFu),
             (unsigned)((ip >> 16) & 0xFFu),
             (unsigned)((ip >> 8) & 0xFFu),
             (unsigned)(ip & 0xFFu));
}

static esp_err_t main_get_tailscale_status(http_server_tailscale_status_t *status)
{
    ESP_RETURN_ON_FALSE(status, ESP_ERR_INVALID_ARG, TAG, "status is NULL");

    ts_claw_status_t *snapshot = calloc(1, sizeof(*snapshot));
    if (!snapshot) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ts_claw_get_status(snapshot);
    if (err != ESP_OK) {
        free(snapshot);
        return err;
    }

    memset(status, 0, sizeof(*status));
    status->enabled = snapshot->enabled;
    status->connected = snapshot->connected;
    status->auth_key_set = snapshot->auth_key_set;
    if (snapshot->vpn_ip != 0u) {
        main_format_host_order_ipv4(snapshot->vpn_ip, status->vpn_ip,
                                    sizeof(status->vpn_ip));
    } else {
        strlcpy(status->vpn_ip, "0.0.0.0", sizeof(status->vpn_ip));
    }
    strlcpy(status->path,
            !snapshot->connected ? "unavailable" :
            snapshot->direct_path_available ? "direct" : "derp",
            sizeof(status->path));
    status->peer_count = snapshot->peer_count;
    status->peer_online = snapshot->peer_online;
    if (snapshot->exit_node_ip != 0u) {
        main_format_host_order_ipv4(snapshot->exit_node_ip, status->exit_node,
                                    sizeof(status->exit_node));
    }
    strlcpy(status->exit_state, main_tailscale_exit_state_name(snapshot->exit_state),
            sizeof(status->exit_state));
    strlcpy(status->egress, snapshot->egress, sizeof(status->egress));
    strlcpy(status->last_error, snapshot->last_error, sizeof(status->last_error));
    status->heap_internal_free = snapshot->internal_free;
    status->heap_internal_largest = snapshot->internal_largest;
    status->heap_psram_free = snapshot->psram_free;

    free(snapshot);
    return ESP_OK;
}

static esp_err_t main_tailscale_service_get_diagnostics(ts_claw_diagnostics_t *out,
                                                        void *ctx)
{
    (void)ctx;
    return ts_claw_get_diagnostics(out);
}

static int main_tailscale_service_list_exit_nodes(ts_claw_peer_t *out,
                                                  size_t capacity,
                                                  void *ctx)
{
    (void)ctx;
    return ts_claw_list_exit_nodes(out, capacity);
}

static esp_err_t main_tailscale_service_apply_exit_node(
    uint32_t ip, ts_claw_runtime_result_t *out, void *ctx)
{
    (void)ctx;
    return ts_claw_set_exit_node(ip, MAIN_TAILSCALE_RUNTIME_TIMEOUT_MS, out);
}

static esp_err_t main_tailscale_service_rebind(void *ctx)
{
    (void)ctx;
    return ts_claw_reconnect(MAIN_TAILSCALE_RUNTIME_TIMEOUT_MS);
}

static esp_err_t main_tailscale_service_load_persisted_exit(char out[16],
                                                            void *ctx)
{
    app_config_t *config;
    esp_err_t err;

    (void)ctx;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    config = calloc(1, sizeof(*config));
    if (!config) {
        return ESP_ERR_NO_MEM;
    }
    err = app_config_load(config);
    if (err == ESP_OK) {
        strlcpy(out, config->tailscale_exit_node, 16);
    }
    free(config);
    return err;
}

static esp_err_t main_cap_tailscale_get_status(cap_tailscale_status_t *out,
                                               void *ctx)
{
    tailscale_service_handle_t service = ctx;
    ts_claw_diagnostics_t *diagnostics;
    esp_err_t err;

    if (!service || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    diagnostics = calloc(1, sizeof(*diagnostics));
    if (!diagnostics) {
        return ESP_ERR_NO_MEM;
    }
    err = tailscale_service_get_diagnostics(service, diagnostics);
    if (err != ESP_OK) {
        free(diagnostics);
        return err;
    }

    out->enabled = diagnostics->status.enabled;
    out->connected = diagnostics->status.connected;
    strlcpy(out->hostname, diagnostics->hostname, sizeof(out->hostname));
    if (diagnostics->status.vpn_ip != 0u) {
        main_format_host_order_ipv4(diagnostics->status.vpn_ip,
                                    out->vpn_ip, sizeof(out->vpn_ip));
    } else {
        strlcpy(out->vpn_ip, "0.0.0.0", sizeof(out->vpn_ip));
    }
    strlcpy(out->path,
            !diagnostics->status.connected ? "unavailable" :
            diagnostics->status.direct_path_available ? "direct" : "derp",
            sizeof(out->path));
    out->peer_count = diagnostics->status.peer_count;
    out->peer_online = diagnostics->status.peer_online;
    if (diagnostics->status.exit_node_ip != 0u) {
        main_format_host_order_ipv4(diagnostics->status.exit_node_ip,
                                    out->exit_node, sizeof(out->exit_node));
    }
    strlcpy(out->exit_state,
            main_tailscale_exit_state_name(diagnostics->status.exit_state),
            sizeof(out->exit_state));
    strlcpy(out->egress, diagnostics->status.egress, sizeof(out->egress));
    strlcpy(out->last_error, diagnostics->status.last_error,
            sizeof(out->last_error));
    out->derp_active.id = diagnostics->derp_active_region;
    strlcpy(out->derp_active.name, diagnostics->derp_active_name,
            sizeof(out->derp_active.name));
    out->derp_default.id = diagnostics->derp_default_region;
    strlcpy(out->derp_default.name, diagnostics->derp_default_name,
            sizeof(out->derp_default.name));
    out->derp_rtt_count = diagnostics->derp_rtt_count < CAP_TAILSCALE_MAX_DERP_RTTS
                              ? diagnostics->derp_rtt_count
                              : CAP_TAILSCALE_MAX_DERP_RTTS;
    for (size_t i = 0; i < out->derp_rtt_count; ++i) {
        out->derp_rtts[i].region.id = diagnostics->derp_rtts[i].region_id;
        strlcpy(out->derp_rtts[i].region.name,
                diagnostics->derp_rtts[i].region_name,
                sizeof(out->derp_rtts[i].region.name));
        out->derp_rtts[i].rtt_ms = diagnostics->derp_rtts[i].rtt_ms;
        out->derp_rtts[i].timed_out = diagnostics->derp_rtts[i].timed_out;
    }
    out->derp_heartbeat_age_ms = diagnostics->derp_heartbeat_age_ms;
    out->control_rx_age_ms = diagnostics->control_rx_age_ms;
    out->reconnect_coord_watchdog = diagnostics->rc_coord_stream_wd;
    out->reconnect_coord_transport = diagnostics->rc_coord_transport;
    out->reconnect_derp_watchdog = diagnostics->rc_derp_rx_wd;
    out->reconnect_derp_retry = diagnostics->rc_derp_retry;
    free(diagnostics);
    return ESP_OK;
}

static int main_cap_tailscale_list_exit_nodes(cap_tailscale_exit_node_t *out,
                                              size_t capacity,
                                              void *ctx)
{
    tailscale_service_handle_t service = ctx;
    ts_claw_peer_t *peers;
    size_t bounded_capacity;
    int count;

    if (!service || (capacity > 0u && !out)) {
        return -ESP_ERR_INVALID_ARG;
    }
    bounded_capacity = capacity < CAP_TAILSCALE_MAX_EXIT_NODES
                           ? capacity
                           : CAP_TAILSCALE_MAX_EXIT_NODES;
    if (bounded_capacity == 0u) {
        return 0;
    }
    memset(out, 0, bounded_capacity * sizeof(*out));
    peers = calloc(bounded_capacity, sizeof(*peers));
    if (!peers) {
        return -ESP_ERR_NO_MEM;
    }
    count = tailscale_service_list_exit_nodes(service, peers, bounded_capacity);
    if (count >= 0) {
        for (int i = 0; i < count; ++i) {
            main_format_host_order_ipv4(peers[i].vpn_ip, out[i].ip,
                                        sizeof(out[i].ip));
            strlcpy(out[i].hostname, peers[i].hostname,
                    sizeof(out[i].hostname));
            out[i].online = peers[i].online;
            out[i].direct = peers[i].direct;
            out[i].derp_region.id = peers[i].derp_region;
            strlcpy(out[i].derp_region.name, peers[i].derp_region_name,
                    sizeof(out[i].derp_region.name));
        }
    }
    free(peers);
    return count;
}

static void main_cap_tailscale_copy_mutation(
    const tailscale_service_result_t *source,
    cap_tailscale_mutation_result_t *destination)
{
    memset(destination, 0, sizeof(*destination));
    destination->ok = source->ok;
    strlcpy(destination->error, tailscale_service_error_name(source->error),
            sizeof(destination->error));
    strlcpy(destination->message, source->message,
            sizeof(destination->message));
    strlcpy(destination->selected_ip, source->selected_ip,
            sizeof(destination->selected_ip));
    strlcpy(destination->selected_hostname, source->selected_hostname,
            sizeof(destination->selected_hostname));
    strlcpy(destination->exit_state,
            main_tailscale_exit_state_name(source->exit_state),
            sizeof(destination->exit_state));
    strlcpy(destination->egress, source->egress,
            sizeof(destination->egress));
    destination->persisted = source->persisted;
}

static esp_err_t main_cap_tailscale_set_exit_node(
    const char *selector, cap_tailscale_mutation_result_t *out, void *ctx)
{
    tailscale_service_handle_t service = ctx;
    tailscale_service_result_t *result;
    esp_err_t err;

    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!service || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    result = calloc(1, sizeof(*result));
    if (!result) {
        return ESP_ERR_NO_MEM;
    }
    err = tailscale_service_set_exit_node(service, selector, result);
    if (err == ESP_OK) {
        main_cap_tailscale_copy_mutation(result, out);
    }
    free(result);
    return err;
}

static esp_err_t main_cap_tailscale_clear_exit_node(
    cap_tailscale_mutation_result_t *out, void *ctx)
{
    tailscale_service_handle_t service = ctx;
    tailscale_service_result_t *result;
    esp_err_t err;

    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!service || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    result = calloc(1, sizeof(*result));
    if (!result) {
        return ESP_ERR_NO_MEM;
    }
    err = tailscale_service_clear_exit_node(service, result);
    if (err == ESP_OK) {
        main_cap_tailscale_copy_mutation(result, out);
    }
    free(result);
    return err;
}

static esp_err_t main_cap_tailscale_reconnect(cap_tailscale_status_t *out,
                                              void *ctx)
{
    tailscale_service_handle_t service = ctx;
    tailscale_service_result_t *result;
    esp_err_t err;

    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!service || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    result = calloc(1, sizeof(*result));
    if (!result) {
        return ESP_ERR_NO_MEM;
    }
    err = tailscale_service_reconnect(service, result);
    if (err == ESP_OK && !result->ok) {
        err = ESP_FAIL;
    }
    free(result);
    return err == ESP_OK ? main_cap_tailscale_get_status(out, service) : err;
}

static esp_err_t main_register_tailscale_capability(
    const app_claw_config_t *config,
    const app_claw_storage_paths_t *paths)
{
    (void)config;
    (void)paths;
    return cap_tailscale_register_group();
}

static esp_err_t main_init_tailscale_capability(void)
{
    const tailscale_service_ops_t service_ops = {
        .get_diagnostics = main_tailscale_service_get_diagnostics,
        .list_exit_nodes = main_tailscale_service_list_exit_nodes,
        .apply_exit_node = main_tailscale_service_apply_exit_node,
        .rebind = main_tailscale_service_rebind,
        .load_persisted_exit = main_tailscale_service_load_persisted_exit,
        .save_persisted_exit = main_tailscale_save_persisted_exit,
        .ctx = NULL,
    };
    esp_err_t err = tailscale_service_create(&service_ops,
                                             &s_tailscale_service);
    if (err != ESP_OK) {
        return err;
    }
    const cap_tailscale_provider_t provider = {
        .get_status = main_cap_tailscale_get_status,
        .list_exit_nodes = main_cap_tailscale_list_exit_nodes,
        .set_exit_node = main_cap_tailscale_set_exit_node,
        .clear_exit_node = main_cap_tailscale_clear_exit_node,
        .reconnect = main_cap_tailscale_reconnect,
        .ctx = s_tailscale_service,
    };
    err = cap_tailscale_set_provider(&provider);
    if (err != ESP_OK) {
        tailscale_service_delete(s_tailscale_service);
        s_tailscale_service = NULL;
        return err;
    }
    err = app_capabilities_register_external_group(
        &(app_capability_external_group_t) {
            .group_id = "cap_tailscale",
            .display_name = "Tailscale",
            .llm_visible_by_default = true,
            .reg = main_register_tailscale_capability,
        });
    if (err != ESP_OK) {
        (void)cap_tailscale_set_provider(NULL);
        tailscale_service_delete(s_tailscale_service);
        s_tailscale_service = NULL;
    }
    return err;
}

static int main_get_tailscale_exit_nodes(http_server_tailscale_exit_node_t *nodes, int capacity)
{
    if (capacity < 0 || (capacity > 0 && !nodes)) {
        return -ESP_ERR_INVALID_ARG;
    }
    if (capacity == 0) {
        return 0;
    }

    ts_claw_peer_t *peers = calloc((size_t)capacity, sizeof(*peers));
    if (!peers) {
        return -ESP_ERR_NO_MEM;
    }

    int count = ts_claw_list_exit_nodes(peers, (size_t)capacity);
    if (count >= 0) {
        if (count > capacity) {
            count = -ESP_ERR_INVALID_SIZE;
        } else {
            for (int i = 0; i < count; ++i) {
                main_format_host_order_ipv4(peers[i].vpn_ip, nodes[i].ip,
                                            sizeof(nodes[i].ip));
                strlcpy(nodes[i].hostname, peers[i].hostname, sizeof(nodes[i].hostname));
                nodes[i].online = peers[i].online;
                nodes[i].direct = peers[i].direct;
                nodes[i].derp_region = peers[i].derp_region;
            }
        }
    }

    free(peers);
    return count;
}

static esp_err_t main_factory_reset(void)
{
    esp_err_t err = settings_store_begin_factory_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to block application settings writes: %s", esp_err_to_name(err));
        return err;
    }

    err = ts_claw_factory_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset TS-Claw identity: %s", esp_err_to_name(err));
        esp_err_t cancel_err = settings_store_cancel_factory_reset();
        if (cancel_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to release settings reset gate: %s",
                     esp_err_to_name(cancel_err));
            return cancel_err;
        }
        return err;
    }

    err = settings_store_erase_all();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase application settings: %s", esp_err_to_name(err));
    }
    return err;
}
#endif

#if CONFIG_APP_CLAW_CAP_IM_WECHAT
static esp_err_t main_wechat_login_start(const char *account_id, bool force)
{
    return cap_im_wechat_qr_login_start(account_id, force);
}

static esp_err_t main_wechat_login_get_status(http_server_wechat_login_status_t *status)
{
    esp_err_t ret = ESP_OK;
    cap_im_wechat_qr_login_status_t *raw = NULL;

    ESP_RETURN_ON_FALSE(status, ESP_ERR_INVALID_ARG, TAG, "status is NULL");

    raw = calloc(1, sizeof(*raw));
    ESP_RETURN_ON_FALSE(raw, ESP_ERR_NO_MEM, TAG, "Failed to allocate login status");

    ESP_GOTO_ON_ERROR(cap_im_wechat_qr_login_get_status(raw), cleanup, TAG,
                      "Failed to query WeChat login status");

    memset(status, 0, sizeof(*status));
    status->active = raw->active;
    status->configured = raw->configured;
    status->completed = raw->completed;
    status->persisted = raw->persisted;
    strlcpy(status->session_key, raw->session_key, sizeof(status->session_key));
    strlcpy(status->status, raw->status, sizeof(status->status));
    strlcpy(status->message, raw->message, sizeof(status->message));
    strlcpy(status->qr_data_url, raw->qr_data_url, sizeof(status->qr_data_url));
    strlcpy(status->account_id, raw->account_id, sizeof(status->account_id));
    strlcpy(status->user_id, raw->user_id, sizeof(status->user_id));
    strlcpy(status->token, raw->token, sizeof(status->token));
    strlcpy(status->base_url, raw->base_url, sizeof(status->base_url));

cleanup:
    free(raw);
    return ret;
}

static esp_err_t main_wechat_login_cancel(void)
{
    return cap_im_wechat_qr_login_cancel();
}

static esp_err_t main_wechat_login_mark_persisted(void)
{
    return cap_im_wechat_qr_login_mark_persisted();
}
#endif

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t init_timezone(const char *timezone)
{
    esp_err_t ret = ESP_OK;

    ESP_GOTO_ON_FALSE(timezone && timezone[0] != '\0', ESP_ERR_INVALID_ARG, tz_default, TAG,
                      "Timezone is empty.");
    ESP_GOTO_ON_FALSE(setenv("TZ", timezone, 1) == 0, ESP_FAIL, tz_default, TAG,
                      "Failed to set TZ env");
    tzset();
    ESP_LOGI(TAG, "Timezone set to %s", timezone);
    return ESP_OK;

tz_default:
    assert(setenv("TZ", "CST-8", 1) == 0);
    tzset();
    ESP_LOGI(TAG, "Timezone set to default: CST-8");
    return ret;
}

#if APP_ENABLE_MEM_LOG

static void print_task_stack_info(void)
{
#ifdef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    static TaskStatus_t s_task_status_snapshot[24];
    UBaseType_t count = uxTaskGetSystemState(s_task_status_snapshot,
                                             sizeof(s_task_status_snapshot) / sizeof(s_task_status_snapshot[0]),
                                             NULL);

    for (UBaseType_t i = 0; i < count; i++) {
        ESP_LOGI(TAG,
                 "Task %s  %u",
                 s_task_status_snapshot[i].pcTaskName,
                 s_task_status_snapshot[i].usStackHighWaterMark);
    }
#endif
}

/* Periodic task: print internal free, minimum free, and PSRAM free every 20s */
static void memory_monitor_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "Memory: internal_free=%u bytes, internal_min_free=%u bytes, psram_free=%u bytes",
                 (unsigned)internal_free, (unsigned)internal_min, (unsigned)psram_free);
        print_task_stack_info();
    }
}

#endif

void app_main(void)
{
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);
    esp_log_level_set("http_reuse", ESP_LOG_WARN);

    ESP_LOGI(TAG, "Starting app");
    ESP_LOGI(TAG, "ESP-Claw version: %s", claw_get_version());
    ESP_LOGI(TAG, "ESP-Claw git version: %s", claw_get_git_version());
    ESP_LOGI(TAG, "Edge Agent version: %s", edge_agent_get_version());
    ESP_ERROR_CHECK(app_allocate_runtime_state());
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(app_config_init());
    ESP_ERROR_CHECK(app_config_load(s_config));
    app_config_to_claw(s_config, s_claw_config);
    init_timezone(app_config_get_timezone(s_config)); // no need to check error
    ESP_ERROR_CHECK(esp_board_manager_init());
    ESP_ERROR_CHECK(app_fs_init());

    /* Publish the resolved storage roots so any component can compose paths
     * without knowing whether data lives on flash or an SD card. */
    ESP_ERROR_CHECK(claw_paths_set(CLAW_PATH_DATA, app_fs_storage_base_path()));
    ESP_ERROR_CHECK(claw_paths_set(CLAW_PATH_SYSTEM, app_fs_system_base_path()));

    ESP_ERROR_CHECK(wifi_manager_init());
#if CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW
    esp_err_t ts_claw_err = main_init_ts_claw(s_config);
    if (ts_claw_err == ESP_OK) {
        s_ts_claw_initialized = true;
    } else {
        ESP_LOGE(TAG, "TS-Claw startup unavailable: %s; continuing with LAN services",
                 esp_err_to_name(ts_claw_err));
    }
    ESP_ERROR_CHECK(main_init_tailscale_capability());
#endif

    ESP_ERROR_CHECK(app_claw_ui_start());

    ESP_ERROR_CHECK(http_server_init(&(http_server_config_t) {
        .storage_base_path = app_fs_storage_base_path(),
        .services = {
            .load_config = main_load_config,
            .save_config = main_save_config,
            .get_wifi_status = main_get_wifi_status,
            .restart_device = main_restart_device,
#if CONFIG_APP_CLAW_CAP_IM_WECHAT
            .wechat_login_start = main_wechat_login_start,
            .wechat_login_get_status = main_wechat_login_get_status,
            .wechat_login_cancel = main_wechat_login_cancel,
            .wechat_login_mark_persisted = main_wechat_login_mark_persisted,
#endif
#if CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW
            .get_tailscale_status = main_get_tailscale_status,
            .get_tailscale_exit_nodes = main_get_tailscale_exit_nodes,
#endif
        },
    }));
    ESP_ERROR_CHECK(wifi_manager_register_state_callback(on_wifi_state_changed, NULL));

    log_wifi_startup_config(s_config);

    wifi_manager_config_t wifi_config = {
        .sta_ssid = s_config->wifi_ssid,
        .sta_password = s_config->wifi_password,
        .ap_ssid = s_config->ap_ssid[0] ? s_config->ap_ssid : NULL,
        .ap_password = s_config->ap_password[0] ? s_config->ap_password : NULL,
        .ap_behavior = s_config->ap_behavior,
    };
#if CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW
    wifi_config.ap_behavior = s_config->ap_behavior[0] ? s_config->ap_behavior : "close_on_sta";
    wifi_config.ap_ip = "192.168.237.1";
    wifi_config.ap_netmask = "255.255.255.0";
    wifi_config.dhcp_start = "192.168.237.10";
    wifi_config.dhcp_end = "192.168.237.50";
#endif
    esp_err_t wifi_err = wifi_manager_start(&wifi_config);
#if CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW
    ESP_ERROR_CHECK(provision_button_start(main_factory_reset));
#endif
    if (wifi_err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(wifi_err));
    } else {
        ESP_ERROR_CHECK(http_server_start());
        if (captive_dns_start(&(captive_dns_config_t) {
                .ap_netif = wifi_manager_get_ap_netif(),
                .configure_dhcp_dns = true,
            }) != ESP_OK) {
            ESP_LOGW(TAG, "Captive DNS could not start, portal pop-up disabled");
        }

        if (s_config->wifi_ssid[0] != '\0') {
            esp_err_t wait_err = wifi_manager_wait_connected(30000);
            if (wait_err == ESP_OK) {
                wifi_manager_status_t status = {0};
                wifi_manager_get_status(&status);
                ESP_LOGI(TAG, "Wi-Fi STA ready: %s", status.sta_ip);
            } else if (wait_err == ESP_ERR_TIMEOUT) {
                wifi_manager_status_t status = {0};
                wifi_manager_get_status(&status);
                ESP_LOGW(TAG,
                         "Wi-Fi STA not connected within wait window; retrying in background: mode=%s ap_active=%d ap_ip=%s",
                         status.mode ? status.mode : "off",
                         status.ap_active,
                         status.ap_ip ? status.ap_ip : "0.0.0.0");
            } else {
                ESP_LOGW(TAG, "Wi-Fi STA wait returned error: %s", esp_err_to_name(wait_err));
            }
        }

        wifi_manager_status_t status = {0};
        wifi_manager_get_status(&status);
        if (status.ap_active) {
            const char *portal_auth = s_config->ap_password[0] ? "wpa2" : "open";
            ESP_LOGW(TAG,
                     "*** Provisioning portal: SSID=\"%s\" (auth=%s) IP=%s URL=http://%s/ ***",
                     status.ap_ssid,
                     portal_auth,
                     status.ap_ip,
                     status.ap_ip);
        }
    }

    ESP_ERROR_CHECK(app_claw_set_save_config_callback(main_save_claw_config, NULL));
    ESP_ERROR_CHECK(app_claw_start(s_claw_config));
#if CONFIG_APP_CLAW_CAP_IM_LOCAL
    ESP_ERROR_CHECK(http_server_webim_bind_im());
#endif

    register_wifi_command();

#if APP_ENABLE_MEM_LOG
    /* Start memory monitor: print internal free, min free, PSRAM free every 20s */
    xTaskCreate(memory_monitor_task, "mem_mon", 4096, NULL, 1, NULL);
#endif

    app_free_runtime_state();
}
