#include "ts_claw_policy.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static void test_route_classification(void)
{
    assert(ts_route_classify(0x64400001u, false) == TS_ROUTE_WG);
    assert(ts_route_classify(0xC0A80101u, true) == TS_ROUTE_STA);
    assert(ts_route_classify(0x08080808u, false) == TS_ROUTE_STA);
    assert(ts_route_classify(0x08080808u, true) == TS_ROUTE_WG);
}

static void test_cgnat_boundaries(void)
{
    assert(!ts_route_is_cgnat(0x643FFFFFu));
    assert(ts_route_is_cgnat(0x64400000u));
    assert(ts_route_is_cgnat(0x647FFFFFu));
    assert(!ts_route_is_cgnat(0x64800000u));

    assert(ts_route_classify(0x64400000u, false) == TS_ROUTE_WG);
    assert(ts_route_classify(0x647FFFFFu, false) == TS_ROUTE_WG);
}

static void test_rfc1918_boundaries(void)
{
    assert(!ts_route_is_private(0x09FFFFFFu));
    assert(ts_route_is_private(0x0A000000u));
    assert(ts_route_is_private(0x0AFFFFFFu));
    assert(!ts_route_is_private(0x0B000000u));

    assert(!ts_route_is_private(0xAC0FFFFFu));
    assert(ts_route_is_private(0xAC100000u));
    assert(ts_route_is_private(0xAC1FFFFFu));
    assert(!ts_route_is_private(0xAC200000u));

    assert(!ts_route_is_private(0xC0A7FFFFu));
    assert(ts_route_is_private(0xC0A80000u));
    assert(ts_route_is_private(0xC0A8FFFFu));
    assert(!ts_route_is_private(0xC0A90000u));

    assert(ts_route_classify(0x0A000000u, true) == TS_ROUTE_STA);
    assert(ts_route_classify(0xAC1FFFFFu, true) == TS_ROUTE_STA);
    assert(ts_route_classify(0xC0A8FFFFu, true) == TS_ROUTE_STA);
}

static void test_exit_activation_and_fallback_thresholds(void)
{
    ts_exit_policy_t policy;

    ts_exit_policy_init(&policy, true);
    assert(policy.state == TS_EXIT_PENDING);
    assert(policy.consecutive_successes == 0u);
    assert(policy.consecutive_failures == 0u);

    ts_exit_policy_set_tunnel(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    assert(policy.state == TS_EXIT_PENDING);
    ts_exit_policy_on_probe(&policy, true);
    assert(policy.state == TS_EXIT_PENDING);
    ts_exit_policy_on_probe(&policy, true);
    assert(policy.state == TS_EXIT_ACTIVE);
    assert(ts_exit_policy_routes_public(&policy));

    ts_exit_policy_on_probe(&policy, false);
    assert(policy.state == TS_EXIT_ACTIVE);
    ts_exit_policy_on_probe(&policy, false);
    assert(policy.state == TS_EXIT_ACTIVE);
    ts_exit_policy_on_probe(&policy, false);
    assert(policy.state == TS_EXIT_FALLBACK);
    assert(!ts_exit_policy_routes_public(&policy));
}

static void test_disabled_and_tunnel_loss(void)
{
    ts_exit_policy_t policy;

    ts_exit_policy_init(&policy, false);
    assert(policy.state == TS_EXIT_DISABLED);
    assert(!ts_exit_policy_routes_public(&policy));

    ts_exit_policy_init(&policy, true);
    ts_exit_policy_set_tunnel(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    assert(policy.state == TS_EXIT_ACTIVE);

    ts_exit_policy_set_tunnel(&policy, false);
    assert(policy.state == TS_EXIT_FALLBACK);
    assert(!ts_exit_policy_routes_public(&policy));
}

static void test_opposite_probe_resets_counters(void)
{
    ts_exit_policy_t policy;

    ts_exit_policy_init(&policy, true);
    ts_exit_policy_set_tunnel(&policy, true);

    ts_exit_policy_on_probe(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    assert(policy.consecutive_successes == 2u);
    ts_exit_policy_on_probe(&policy, false);
    assert(policy.consecutive_successes == 0u);
    assert(policy.consecutive_failures == 1u);
    assert(policy.state == TS_EXIT_PENDING);

    ts_exit_policy_on_probe(&policy, true);
    assert(policy.consecutive_successes == 1u);
    assert(policy.consecutive_failures == 0u);

    ts_exit_policy_on_probe(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    assert(policy.state == TS_EXIT_ACTIVE);

    ts_exit_policy_on_probe(&policy, false);
    ts_exit_policy_on_probe(&policy, false);
    assert(policy.consecutive_failures == 2u);
    ts_exit_policy_on_probe(&policy, true);
    assert(policy.consecutive_successes == 1u);
    assert(policy.consecutive_failures == 0u);
    assert(policy.state == TS_EXIT_ACTIVE);
}

static void test_null_policy_is_safe(void)
{
    ts_exit_policy_init(NULL, true);
    ts_exit_policy_set_tunnel(NULL, true);
    ts_exit_policy_on_probe(NULL, true);
    assert(!ts_exit_policy_routes_public(NULL));
}

int main(void)
{
    test_route_classification();
    test_cgnat_boundaries();
    test_rfc1918_boundaries();
    test_exit_activation_and_fallback_thresholds();
    test_disabled_and_tunnel_loss();
    test_opposite_probe_resets_counters();
    test_null_policy_is_safe();

    puts("ts_claw_policy: all tests passed");
    return 0;
}
