#pragma once

#include <stdbool.h>

struct netif;

typedef struct {
    struct netif *sta_netif;
    struct netif *wg_netif;
    bool tunnel_available;
    bool upstream_pinned;
    bool exit_active;
    bool probe_active;
} ts_claw_route_state_t;

void ts_claw_route_hook_reset(void);
void ts_claw_route_hook_set_netifs(struct netif *sta_netif, struct netif *wg_netif);
void ts_claw_route_hook_set_tunnel_available(bool available);
void ts_claw_route_hook_set_upstream_pinned(bool pinned);
void ts_claw_route_hook_set_exit_active(bool active);
void ts_claw_route_hook_set_probe_active(bool active);
ts_claw_route_state_t ts_claw_route_hook_get_state(void);
