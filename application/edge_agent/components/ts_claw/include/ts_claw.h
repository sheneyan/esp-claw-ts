#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"
#include "microlink.h"
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

esp_err_t ts_claw_init(const ts_claw_config_t *config);
/*
 * sta_netif is a borrowed application-lifetime object. It must remain valid
 * from the first online notification until device restart or a future deinit
 * API. A synchronous down notification unpins the link and updates status; it
 * does not authorize destroying or recreating sta_netif.
 */
esp_err_t ts_claw_notify_wifi(bool sta_has_ip, esp_netif_t *sta_netif);
esp_err_t ts_claw_get_status(ts_claw_status_t *out_status);
/* Returns the number of copied exit nodes, or a negative error value. */
int ts_claw_get_exit_nodes(microlink_peer_info_t *out_nodes, int capacity);
/* Stops the runtime, erases its identity, and keeps it stopped until reboot. */
esp_err_t ts_claw_factory_reset(void);

#ifdef __cplusplus
}
#endif
