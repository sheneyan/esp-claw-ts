#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"
#include "../ts_claw_diagnostics.h"
#include "ts_claw_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    const char *auth_key;
    const char *hostname;
    const char *login_server;
    uint32_t exit_node_ip;
    uint8_t max_peers;
} ts_claw_config_t;

typedef struct {
    bool enabled;
    bool connected;
    bool direct_path_available;
    bool auth_key_set;
    uint32_t vpn_ip;
    uint32_t exit_node_ip;
    int peer_count;
    int peer_online;
    ts_exit_state_t exit_state;
    char egress[16];
    char last_error[96];
    size_t internal_free;
    size_t internal_largest;
    size_t psram_free;
} ts_claw_status_t;

typedef struct {
    ts_claw_status_t status;
    char hostname[TS_CLAW_PEER_HOSTNAME_LEN];
    uint16_t derp_active_region;
    char derp_active_name[TS_CLAW_REGION_NAME_LEN];
    uint16_t derp_default_region;
    char derp_default_name[TS_CLAW_REGION_NAME_LEN];
    ts_claw_derp_rtt_t derp_rtts[TS_CLAW_MAX_DERP_RTTS];
    size_t derp_rtt_count;
    uint64_t derp_heartbeat_age_ms;
    uint64_t control_rx_age_ms;
    /* Current-instance reconnect causes; wd denotes watchdog recovery. */
    uint32_t rc_coord_stream_wd;
    uint32_t rc_coord_transport;
    uint32_t rc_derp_rx_wd;
    uint32_t rc_derp_retry;
} ts_claw_diagnostics_t;

typedef struct {
    uint32_t vpn_ip;
    char hostname[TS_CLAW_PEER_HOSTNAME_LEN];
    bool online;
    bool direct;
    bool is_exit_node;
    uint16_t derp_region;
    char derp_region_name[TS_CLAW_REGION_NAME_LEN];
} ts_claw_peer_t;

typedef struct {
    uint32_t selected_exit_node_ip;
    ts_exit_state_t exit_state;
    char egress[16];
    bool rollback_attempted;
    bool rollback_recovered;
} ts_claw_runtime_result_t;

esp_err_t ts_claw_init(const ts_claw_config_t *config);
/*
 * sta_netif is a borrowed application-lifetime object. It must remain valid
 * from the first online notification until device restart or a future deinit
 * API. A synchronous down notification unpins the link and updates status; it
 * does not authorize destroying or recreating sta_netif.
 */
esp_err_t ts_claw_notify_wifi(bool sta_has_ip, esp_netif_t *sta_netif);
esp_err_t ts_claw_get_status(ts_claw_status_t *out_status);
esp_err_t ts_claw_get_diagnostics(ts_claw_diagnostics_t *out);
/* Returns the number of copied exit nodes, or a negative error value. */
int ts_claw_list_exit_nodes(ts_claw_peer_t *out, size_t capacity);
/* timeout_ms == 0 performs one immediate observation before timing out. */
esp_err_t ts_claw_set_exit_node(uint32_t exit_node_ip,
                                uint32_t timeout_ms,
                                ts_claw_runtime_result_t *out);
esp_err_t ts_claw_reconnect(uint32_t timeout_ms);
/* Stops the runtime, erases its identity, and keeps it stopped until reboot. */
esp_err_t ts_claw_factory_reset(void);

#ifdef __cplusplus
}
#endif
