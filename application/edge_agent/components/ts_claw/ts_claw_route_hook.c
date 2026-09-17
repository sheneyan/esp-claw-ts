#include "ts_claw_route_hook.h"

#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_route_lock = portMUX_INITIALIZER_UNLOCKED;
static ts_claw_route_state_t s_route_state;

void ts_claw_route_hook_reset(void)
{
    portENTER_CRITICAL(&s_route_lock);
    s_route_state = (ts_claw_route_state_t) {0};
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

ts_claw_route_state_t ts_claw_route_hook_get_state(void)
{
    portENTER_CRITICAL(&s_route_lock);
    ts_claw_route_state_t state = s_route_state;
    portEXIT_CRITICAL(&s_route_lock);
    return state;
}
