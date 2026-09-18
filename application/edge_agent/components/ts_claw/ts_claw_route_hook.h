#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct netif;

#define TS_CLAW_DNS_BYPASS_MAX 3u

typedef struct {
    struct netif *sta_netif;
    struct netif *wg_netif;
    bool tunnel_available;
    bool upstream_pinned;
    bool exit_active;
    bool probe_active;
    uint32_t dns_bypass[TS_CLAW_DNS_BYPASS_MAX];
    size_t dns_bypass_count;
} ts_claw_route_state_t;

void ts_claw_route_hook_reset(void);
void ts_claw_route_hook_set_netifs(struct netif *sta_netif, struct netif *wg_netif);
void ts_claw_route_hook_set_tunnel_available(bool available);
void ts_claw_route_hook_set_upstream_pinned(bool pinned);
void ts_claw_route_hook_set_exit_active(bool active);
void ts_claw_route_hook_set_probe_active(bool active);
void ts_claw_route_hook_set_dns_bypass(const uint32_t *host_order_ips,
                                       size_t count);
ts_claw_route_state_t ts_claw_route_hook_get_state(void);
