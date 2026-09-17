#include "ts_claw_policy.h"

#include <stddef.h>

enum {
    TS_EXIT_PROBE_THRESHOLD = 3,
};

static bool address_matches(uint32_t ipv4, uint32_t network, uint32_t mask)
{
    return (ipv4 & mask) == network;
}

void ts_exit_policy_init(ts_exit_policy_t *policy, bool configured)
{
    if (policy == NULL) {
        return;
    }

    policy->state = configured ? TS_EXIT_PENDING : TS_EXIT_DISABLED;
    policy->consecutive_successes = 0u;
    policy->consecutive_failures = 0u;
    policy->configured = configured;
    policy->tunnel_up = false;
}

void ts_exit_policy_set_tunnel(ts_exit_policy_t *policy, bool tunnel_up)
{
    if (policy == NULL || !policy->configured) {
        return;
    }

    policy->tunnel_up = tunnel_up;
    policy->consecutive_successes = 0u;
    policy->consecutive_failures = 0u;

    if (!tunnel_up) {
        policy->state = TS_EXIT_FALLBACK;
    } else if (policy->state == TS_EXIT_FALLBACK) {
        policy->state = TS_EXIT_PENDING;
    }
}

void ts_exit_policy_on_probe(ts_exit_policy_t *policy, bool success)
{
    if (policy == NULL || !policy->configured || !policy->tunnel_up) {
        return;
    }

    if (success) {
        policy->consecutive_failures = 0u;
        if (policy->consecutive_successes < TS_EXIT_PROBE_THRESHOLD) {
            policy->consecutive_successes++;
        }
        if (policy->consecutive_successes == TS_EXIT_PROBE_THRESHOLD) {
            policy->state = TS_EXIT_ACTIVE;
        }
        return;
    }

    policy->consecutive_successes = 0u;
    if (policy->consecutive_failures < TS_EXIT_PROBE_THRESHOLD) {
        policy->consecutive_failures++;
    }
    if (policy->state == TS_EXIT_ACTIVE &&
        policy->consecutive_failures == TS_EXIT_PROBE_THRESHOLD) {
        policy->state = TS_EXIT_FALLBACK;
    }
}

bool ts_exit_policy_routes_public(const ts_exit_policy_t *policy)
{
    return policy != NULL && policy->state == TS_EXIT_ACTIVE;
}

bool ts_route_is_cgnat(uint32_t ipv4)
{
    return address_matches(ipv4, 0x64400000u, 0xFFC00000u);
}

bool ts_route_is_private(uint32_t ipv4)
{
    return address_matches(ipv4, 0x0A000000u, 0xFF000000u) ||
           address_matches(ipv4, 0xAC100000u, 0xFFF00000u) ||
           address_matches(ipv4, 0xC0A80000u, 0xFFFF0000u);
}

ts_route_target_t ts_route_classify(uint32_t ipv4, bool exit_active)
{
    if (ts_route_is_cgnat(ipv4)) {
        return TS_ROUTE_WG;
    }
    if (ts_route_is_private(ipv4)) {
        return TS_ROUTE_STA;
    }
    return exit_active ? TS_ROUTE_WG : TS_ROUTE_STA;
}
