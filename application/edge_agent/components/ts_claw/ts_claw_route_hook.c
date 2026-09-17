#include "ts_claw_route_hook.h"

#include "freertos/FreeRTOS.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "ts_claw_policy.h"

static portMUX_TYPE s_route_lock = portMUX_INITIALIZER_UNLOCKED;
static ts_claw_route_state_t s_route_state;

void ts_claw_route_hook_reset(void)
{
    portENTER_CRITICAL(&s_route_lock);
    s_route_state = (ts_claw_route_state_t) {0};
    portEXIT_CRITICAL(&s_route_lock);
}

void ts_claw_route_hook_set_netifs(struct netif *sta_netif, struct netif *wg_netif)
{
    portENTER_CRITICAL(&s_route_lock);
    s_route_state.sta_netif = sta_netif;
    s_route_state.wg_netif = wg_netif;
    if (sta_netif == NULL || wg_netif == NULL) {
        s_route_state.exit_active = false;
    }
    portEXIT_CRITICAL(&s_route_lock);
}

void ts_claw_route_hook_set_tunnel_available(bool available)
{
    portENTER_CRITICAL(&s_route_lock);
    s_route_state.tunnel_available = available;
    if (!available) {
        s_route_state.exit_active = false;
    }
    portEXIT_CRITICAL(&s_route_lock);
}

void ts_claw_route_hook_set_upstream_pinned(bool pinned)
{
    portENTER_CRITICAL(&s_route_lock);
    s_route_state.upstream_pinned = pinned;
    if (!pinned) {
        s_route_state.exit_active = false;
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
    const ts_claw_route_state_t state = ts_claw_route_hook_get_state();

    if (ts_route_is_cgnat(destination) && state.wg_netif != NULL) {
        return state.wg_netif;
    }
    if (ts_route_is_local_bypass(destination) && state.sta_netif != NULL) {
        return state.sta_netif;
    }
    if (ts_route_is_public_unicast(destination) && state.exit_active &&
        state.tunnel_available && state.upstream_pinned && state.wg_netif != NULL) {
        return state.wg_netif;
    }
    return __real_ip4_route_src_hook(src, dest);
}
