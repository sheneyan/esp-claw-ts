#pragma once

#include <stdbool.h>

typedef struct {
    bool tunnel_available;
    bool upstream_pinned;
    bool exit_active;
} ts_claw_route_state_t;

void ts_claw_route_hook_reset(void);
void ts_claw_route_hook_set_tunnel_available(bool available);
void ts_claw_route_hook_set_upstream_pinned(bool pinned);
ts_claw_route_state_t ts_claw_route_hook_get_state(void);
