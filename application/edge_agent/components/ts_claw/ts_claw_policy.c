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

typedef struct {
    uint32_t network;
    uint32_t mask;
} ts_ipv4_range_t;

/*
 * IANA IPv4 Special-Purpose Address Registry entries modeled here with
 * Globally Reachable = True. These must win over broader exclusions below.
 */
static const ts_ipv4_range_t s_iana_global_special_ranges[] = {
    {0xC0000009u, 0xFFFFFFFFu}, /* 192.0.0.9/32: PCP anycast. */
    {0xC000000Au, 0xFFFFFFFFu}, /* 192.0.0.10/32: TURN anycast. */
    {0xC01FC400u, 0xFFFFFF00u}, /* 192.31.196.0/24: AS112-v4. */
    {0xC034C100u, 0xFFFFFF00u}, /* 192.52.193.0/24: AMT. */
    {0xC0AF3000u, 0xFFFFFF00u}, /* 192.175.48.0/24: AS112 direct. */
};

/*
 * IANA special-purpose ranges modeled here with Globally Reachable = False.
 * 224/3 combines the IPv4 multicast 224/4 and reserved 240/4 blocks.
 */
static const ts_ipv4_range_t s_iana_non_global_ranges[] = {
    {0x00000000u, 0xFF000000u}, /* 0.0.0.0/8: this network. */
    {0xC0000000u, 0xFFFFFF00u}, /* 192.0.0.0/24: IETF protocol assignments. */
    {0xC0000200u, 0xFFFFFF00u}, /* 192.0.2.0/24: TEST-NET-1. */
    {0xC0586300u, 0xFFFFFF00u}, /* 192.88.99.0/24: deprecated 6to4 relay. */
    {0xC6120000u, 0xFFFE0000u}, /* 198.18.0.0/15: benchmarking. */
    {0xC6336400u, 0xFFFFFF00u}, /* 198.51.100.0/24: TEST-NET-2. */
    {0xCB007100u, 0xFFFFFF00u}, /* 203.0.113.0/24: TEST-NET-3. */
    {0xE0000000u, 0xE0000000u}, /* 224.0.0.0/3: multicast and reserved. */
};

static bool address_matches_any(uint32_t host_order_ip,
                                const ts_ipv4_range_t *ranges,
                                size_t range_count)
{
    for (size_t i = 0; i < range_count; ++i) {
        if (address_matches(host_order_ip, ranges[i].network, ranges[i].mask)) {
            return true;
        }
    }
    return false;
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

bool ts_route_is_loopback(uint32_t host_order_ip)
{
    return address_matches(host_order_ip, 0x7F000000u, 0xFF000000u);
}

bool ts_route_is_local_bypass(uint32_t host_order_ip)
{
    return ts_route_is_private(host_order_ip) ||
           address_matches(host_order_ip, 0xA9FE0000u, 0xFFFF0000u);
}

bool ts_route_is_public_unicast(uint32_t host_order_ip)
{
    if (ts_route_is_cgnat(host_order_ip) ||
        ts_route_is_loopback(host_order_ip) ||
        ts_route_is_local_bypass(host_order_ip)) {
        return false;
    }

    if (address_matches_any(host_order_ip, s_iana_global_special_ranges,
                            sizeof(s_iana_global_special_ranges) /
                                sizeof(s_iana_global_special_ranges[0]))) {
        return true;
    }

    return !address_matches_any(host_order_ip, s_iana_non_global_ranges,
                                sizeof(s_iana_non_global_ranges) /
                                    sizeof(s_iana_non_global_ranges[0]));
}

ts_route_target_t ts_route_classify(uint32_t host_order_ip, bool exit_active)
{
    if (ts_route_is_loopback(host_order_ip)) {
        return TS_ROUTE_DEFAULT;
    }
    if (ts_route_is_cgnat(host_order_ip)) {
        return TS_ROUTE_WG;
    }
    if (ts_route_is_local_bypass(host_order_ip)) {
        return TS_ROUTE_STA;
    }
    if (!ts_route_is_public_unicast(host_order_ip)) {
        return TS_ROUTE_DEFAULT;
    }
    return exit_active ? TS_ROUTE_WG : TS_ROUTE_STA;
}

uint32_t ts_exit_probe_interface_binding(void)
{
    /*
     * CGNAT destinations are already forced onto the WireGuard netif by the
     * route hook.  Binding ESP-IDF's raw ICMP socket to the custom netif makes
     * sendto() return zero bytes on hardware, so leave the socket unbound.
     */
    return 0u;
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
