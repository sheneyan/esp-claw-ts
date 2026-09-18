#include "ts_claw_route_hook.h"

#include "freertos/FreeRTOS.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "ts_claw_policy.h"

static portMUX_TYPE s_route_lock = portMUX_INITIALIZER_UNLOCKED;
static ts_claw_route_state_t s_route_state;

static void clear_dns_bypass_locked(void)
{
    for (size_t i = 0; i < TS_CLAW_DNS_BYPASS_MAX; ++i) {
        s_route_state.dns_bypass[i] = 0u;
    }
    s_route_state.dns_bypass_count = 0u;
}

void ts_claw_route_hook_reset(void)
{
    portENTER_CRITICAL(&s_route_lock);
    s_route_state = (ts_claw_route_state_t) {0};
    portEXIT_CRITICAL(&s_route_lock);
}

void ts_claw_route_hook_set_netifs(struct netif *sta_netif, struct netif *wg_netif)
{
    portENTER_CRITICAL(&s_route_lock);
    if (s_route_state.probe_active && s_route_state.wg_netif != NULL &&
        wg_netif != s_route_state.wg_netif) {
        portEXIT_CRITICAL(&s_route_lock);
        return;
    }
    s_route_state.sta_netif = sta_netif;
    s_route_state.wg_netif = wg_netif;
    if (sta_netif == NULL || wg_netif == NULL) {
        s_route_state.exit_active = false;
        clear_dns_bypass_locked();
    }
    portEXIT_CRITICAL(&s_route_lock);
}

void ts_claw_route_hook_set_probe_active(bool active)
{
    portENTER_CRITICAL(&s_route_lock);
    s_route_state.probe_active = active;
    portEXIT_CRITICAL(&s_route_lock);
}

void ts_claw_route_hook_set_tunnel_available(bool available)
{
    portENTER_CRITICAL(&s_route_lock);
    s_route_state.tunnel_available = available;
    if (!available) {
        s_route_state.exit_active = false;
        clear_dns_bypass_locked();
    }
    portEXIT_CRITICAL(&s_route_lock);
}

void ts_claw_route_hook_set_upstream_pinned(bool pinned)
{
    portENTER_CRITICAL(&s_route_lock);
    s_route_state.upstream_pinned = pinned;
    if (!pinned) {
        s_route_state.exit_active = false;
        clear_dns_bypass_locked();
    }
    portEXIT_CRITICAL(&s_route_lock);
}

void ts_claw_route_hook_set_exit_active(bool active)
{
    portENTER_CRITICAL(&s_route_lock);
    s_route_state.exit_active = active && s_route_state.tunnel_available &&
                                s_route_state.upstream_pinned &&
                                s_route_state.sta_netif != NULL &&
                                s_route_state.wg_netif != NULL;
    if (!s_route_state.exit_active) {
        clear_dns_bypass_locked();
    }
    portEXIT_CRITICAL(&s_route_lock);
}

void ts_claw_route_hook_set_dns_bypass(const uint32_t *host_order_ips,
                                       size_t count)
{
    const size_t bounded_count =
        host_order_ips != NULL && count < TS_CLAW_DNS_BYPASS_MAX
            ? count
            : (host_order_ips != NULL ? TS_CLAW_DNS_BYPASS_MAX : 0u);

    portENTER_CRITICAL(&s_route_lock);
    for (size_t i = 0; i < bounded_count; ++i) {
        s_route_state.dns_bypass[i] = host_order_ips[i];
    }
    for (size_t i = bounded_count; i < TS_CLAW_DNS_BYPASS_MAX; ++i) {
        s_route_state.dns_bypass[i] = 0u;
    }
    s_route_state.dns_bypass_count = bounded_count;
    portEXIT_CRITICAL(&s_route_lock);
}

ts_claw_route_state_t ts_claw_route_hook_get_state(void)
{
    portENTER_CRITICAL(&s_route_lock);
    ts_claw_route_state_t state = s_route_state;
    portEXIT_CRITICAL(&s_route_lock);
    return state;
}

extern struct netif *__real_ip4_route_src_hook(const ip4_addr_t *src,
                                                const ip4_addr_t *dest);

struct netif *__wrap_ip4_route_src_hook(const ip4_addr_t *src,
                                        const ip4_addr_t *dest)
{
    if (dest == NULL) {
        return __real_ip4_route_src_hook(src, dest);
    }

    const uint32_t destination = lwip_ntohl(ip4_addr_get_u32(dest));
    if (ts_route_is_loopback(destination)) {
        return __real_ip4_route_src_hook(src, dest);
    }

    const ts_claw_route_state_t state = ts_claw_route_hook_get_state();

    if (ts_route_is_cgnat(destination) && state.wg_netif != NULL) {
        return state.wg_netif;
    }
    if (ts_route_is_local_bypass(destination) && state.sta_netif != NULL) {
        return state.sta_netif;
    }
    if (state.exit_active && state.sta_netif != NULL) {
        for (size_t i = 0; i < state.dns_bypass_count; ++i) {
            if (state.dns_bypass[i] == destination) {
                return state.sta_netif;
            }
        }
    }
    if (ts_route_is_public_unicast(destination) && state.exit_active &&
        state.tunnel_available && state.upstream_pinned && state.wg_netif != NULL) {
        return state.wg_netif;
    }
    return __real_ip4_route_src_hook(src, dest);
}
