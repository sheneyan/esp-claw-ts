#include "ts_claw_policy.h"

#include <stddef.h>

enum {
    TS_EXIT_PROBE_THRESHOLD = 3,
    TS_RESOURCE_LOW_FREE_BYTES = 24 * 1024,
    TS_RESOURCE_LOW_LARGEST_BYTES = 8 * 1024,
    TS_RESOURCE_RECOVER_FREE_BYTES = 48 * 1024,
    TS_RESOURCE_RECOVER_LARGEST_BYTES = 16 * 1024,
    TS_RESOURCE_LOW_SAMPLE_THRESHOLD = 3,
    TS_RESOURCE_RETRY_DELAY_MS = 60 * 1000,
    TS_START_RETRY_DELAY_MS = 30 * 1000,
};

static bool address_matches(uint32_t host_order_ip, uint32_t network, uint32_t mask)
{
    return (host_order_ip & mask) == network;
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

    if (policy->tunnel_up == tunnel_up) {
        if (!tunnel_up) {
            policy->state = TS_EXIT_FALLBACK;
        }
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

bool ts_route_is_cgnat(uint32_t host_order_ip)
{
    return address_matches(host_order_ip, 0x64400000u, 0xFFC00000u);
}

bool ts_route_is_private(uint32_t host_order_ip)
{
    return address_matches(host_order_ip, 0x0A000000u, 0xFF000000u) ||
           address_matches(host_order_ip, 0xAC100000u, 0xFFF00000u) ||
           address_matches(host_order_ip, 0xC0A80000u, 0xFFFF0000u);
}

bool ts_route_is_local_bypass(uint32_t host_order_ip)
{
    return ts_route_is_private(host_order_ip) ||
           address_matches(host_order_ip, 0x7F000000u, 0xFF000000u) ||
           address_matches(host_order_ip, 0xA9FE0000u, 0xFFFF0000u);
}

ts_route_target_t ts_route_classify(uint32_t host_order_ip, bool exit_active)
{
    if (ts_route_is_cgnat(host_order_ip)) {
        return TS_ROUTE_WG;
    }
    if (ts_route_is_local_bypass(host_order_ip)) {
        return TS_ROUTE_STA;
    }
    return exit_active ? TS_ROUTE_WG : TS_ROUTE_STA;
}

void ts_resource_guard_init(ts_resource_guard_t *guard)
{
    if (guard == NULL) {
        return;
    }

    *guard = (ts_resource_guard_t) {0};
}

ts_resource_guard_action_t ts_resource_guard_sample(ts_resource_guard_t *guard,
                                                    uint64_t now_ms,
                                                    size_t internal_free,
                                                    size_t internal_largest)
{
    if (guard == NULL) {
        return TS_RESOURCE_GUARD_NONE;
    }

    if (guard->stopped) {
        const bool retry_delay_elapsed =
            now_ms - guard->stopped_at_ms >= TS_RESOURCE_RETRY_DELAY_MS;
        const bool recovered = internal_free >= TS_RESOURCE_RECOVER_FREE_BYTES &&
                               internal_largest >= TS_RESOURCE_RECOVER_LARGEST_BYTES;
        if (!retry_delay_elapsed || !recovered) {
            return TS_RESOURCE_GUARD_NONE;
        }
        return TS_RESOURCE_GUARD_RETRY;
    }

    const bool low = internal_free < TS_RESOURCE_LOW_FREE_BYTES ||
                     internal_largest < TS_RESOURCE_LOW_LARGEST_BYTES;
    if (!low) {
        guard->consecutive_low_samples = 0u;
        return TS_RESOURCE_GUARD_NONE;
    }

    if (guard->consecutive_low_samples < TS_RESOURCE_LOW_SAMPLE_THRESHOLD) {
        guard->consecutive_low_samples++;
    }
    if (guard->consecutive_low_samples < TS_RESOURCE_LOW_SAMPLE_THRESHOLD) {
        return TS_RESOURCE_GUARD_NONE;
    }

    guard->stopped = true;
    guard->stopped_at_ms = now_ms;
    return TS_RESOURCE_GUARD_STOP;
}

void ts_resource_guard_retry_completed(ts_resource_guard_t *guard,
                                       uint64_t now_ms,
                                       bool succeeded)
{
    if (guard == NULL || !guard->stopped) {
        return;
    }

    if (succeeded) {
        guard->stopped = false;
        guard->stopped_at_ms = 0u;
        guard->consecutive_low_samples = 0u;
        return;
    }

    guard->stopped_at_ms = now_ms;
}

uint64_t ts_start_retry_schedule(uint64_t now_ms)
{
    return now_ms + TS_START_RETRY_DELAY_MS;
}

bool ts_start_retry_due(uint64_t next_retry_ms, uint64_t now_ms)
{
    return next_retry_ms != 0u && now_ms >= next_retry_ms;
}
