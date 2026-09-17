#include "ts_claw_policy.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_CHECK(condition)                                                       \
    do {                                                                            \
        if (!(condition)) {                                                         \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,      \
                    #condition);                                                    \
            exit(EXIT_FAILURE);                                                     \
        }                                                                           \
    } while (0)

static void test_route_classification(void)
{
    TEST_CHECK(ts_route_classify(0x64400001u, false) == TS_ROUTE_WG);
    TEST_CHECK(ts_route_classify(0xC0A80101u, true) == TS_ROUTE_STA);
    TEST_CHECK(ts_route_classify(0x08080808u, false) == TS_ROUTE_STA);
    TEST_CHECK(ts_route_classify(0x08080808u, true) == TS_ROUTE_WG);
}

static void test_cgnat_boundaries(void)
{
    TEST_CHECK(!ts_route_is_cgnat(0x643FFFFFu));
    TEST_CHECK(ts_route_is_cgnat(0x64400000u));
    TEST_CHECK(ts_route_is_cgnat(0x647FFFFFu));
    TEST_CHECK(!ts_route_is_cgnat(0x64800000u));

    TEST_CHECK(ts_route_classify(0x64400000u, false) == TS_ROUTE_WG);
    TEST_CHECK(ts_route_classify(0x647FFFFFu, false) == TS_ROUTE_WG);
}

static void test_rfc1918_boundaries(void)
{
    TEST_CHECK(!ts_route_is_private(0x09FFFFFFu));
    TEST_CHECK(ts_route_is_private(0x0A000000u));
    TEST_CHECK(ts_route_is_private(0x0AFFFFFFu));
    TEST_CHECK(!ts_route_is_private(0x0B000000u));

    TEST_CHECK(!ts_route_is_private(0xAC0FFFFFu));
    TEST_CHECK(ts_route_is_private(0xAC100000u));
    TEST_CHECK(ts_route_is_private(0xAC1FFFFFu));
    TEST_CHECK(!ts_route_is_private(0xAC200000u));

    TEST_CHECK(!ts_route_is_private(0xC0A7FFFFu));
    TEST_CHECK(ts_route_is_private(0xC0A80000u));
    TEST_CHECK(ts_route_is_private(0xC0A8FFFFu));
    TEST_CHECK(!ts_route_is_private(0xC0A90000u));

    TEST_CHECK(ts_route_classify(0x0A000000u, true) == TS_ROUTE_STA);
    TEST_CHECK(ts_route_classify(0xAC1FFFFFu, true) == TS_ROUTE_STA);
    TEST_CHECK(ts_route_classify(0xC0A8FFFFu, true) == TS_ROUTE_STA);
}

static void test_exit_activation_and_fallback_thresholds(void)
{
    ts_exit_policy_t policy;

    ts_exit_policy_init(&policy, true);
    TEST_CHECK(policy.state == TS_EXIT_PENDING);
    TEST_CHECK(policy.consecutive_successes == 0u);
    TEST_CHECK(policy.consecutive_failures == 0u);

    ts_exit_policy_set_tunnel(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    TEST_CHECK(policy.state == TS_EXIT_PENDING);
    ts_exit_policy_on_probe(&policy, true);
    TEST_CHECK(policy.state == TS_EXIT_PENDING);
    ts_exit_policy_on_probe(&policy, true);
    TEST_CHECK(policy.state == TS_EXIT_ACTIVE);
    TEST_CHECK(ts_exit_policy_routes_public(&policy));

    ts_exit_policy_on_probe(&policy, false);
    TEST_CHECK(policy.state == TS_EXIT_ACTIVE);
    ts_exit_policy_on_probe(&policy, false);
    TEST_CHECK(policy.state == TS_EXIT_ACTIVE);
    ts_exit_policy_on_probe(&policy, false);
    TEST_CHECK(policy.state == TS_EXIT_FALLBACK);
    TEST_CHECK(!ts_exit_policy_routes_public(&policy));
}

static void test_repeated_tunnel_up_preserves_probe_progress(void)
{
    ts_exit_policy_t policy;

    ts_exit_policy_init(&policy, true);
    ts_exit_policy_set_tunnel(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    TEST_CHECK(policy.consecutive_successes == 2u);

    ts_exit_policy_set_tunnel(&policy, true);
    TEST_CHECK(policy.consecutive_successes == 2u);
    ts_exit_policy_on_probe(&policy, true);
    TEST_CHECK(policy.state == TS_EXIT_ACTIVE);

    ts_exit_policy_on_probe(&policy, false);
    ts_exit_policy_on_probe(&policy, false);
    TEST_CHECK(policy.consecutive_failures == 2u);

    ts_exit_policy_set_tunnel(&policy, true);
    TEST_CHECK(policy.consecutive_failures == 2u);
    ts_exit_policy_on_probe(&policy, false);
    TEST_CHECK(policy.state == TS_EXIT_FALLBACK);
}

static void test_disabled_and_tunnel_loss(void)
{
    ts_exit_policy_t policy;

    ts_exit_policy_init(&policy, false);
    TEST_CHECK(policy.state == TS_EXIT_DISABLED);
    TEST_CHECK(!ts_exit_policy_routes_public(&policy));

    ts_exit_policy_init(&policy, true);
    ts_exit_policy_set_tunnel(&policy, false);
    TEST_CHECK(policy.state == TS_EXIT_FALLBACK);

    ts_exit_policy_init(&policy, true);
    ts_exit_policy_set_tunnel(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    TEST_CHECK(policy.state == TS_EXIT_ACTIVE);

    ts_exit_policy_set_tunnel(&policy, false);
    TEST_CHECK(policy.state == TS_EXIT_FALLBACK);
    TEST_CHECK(!ts_exit_policy_routes_public(&policy));
}

static void test_opposite_probe_resets_counters(void)
{
    ts_exit_policy_t policy;

    ts_exit_policy_init(&policy, true);
    ts_exit_policy_set_tunnel(&policy, true);

    ts_exit_policy_on_probe(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    TEST_CHECK(policy.consecutive_successes == 2u);
    ts_exit_policy_on_probe(&policy, false);
    TEST_CHECK(policy.consecutive_successes == 0u);
    TEST_CHECK(policy.consecutive_failures == 1u);
    TEST_CHECK(policy.state == TS_EXIT_PENDING);

    ts_exit_policy_on_probe(&policy, true);
    TEST_CHECK(policy.consecutive_successes == 1u);
    TEST_CHECK(policy.consecutive_failures == 0u);

    ts_exit_policy_on_probe(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    TEST_CHECK(policy.state == TS_EXIT_ACTIVE);

    ts_exit_policy_on_probe(&policy, false);
    ts_exit_policy_on_probe(&policy, false);
    TEST_CHECK(policy.consecutive_failures == 2u);
    ts_exit_policy_on_probe(&policy, true);
    TEST_CHECK(policy.consecutive_successes == 1u);
    TEST_CHECK(policy.consecutive_failures == 0u);
    TEST_CHECK(policy.state == TS_EXIT_ACTIVE);
}

static void test_null_policy_is_safe(void)
{
    ts_exit_policy_init(NULL, true);
    ts_exit_policy_set_tunnel(NULL, true);
    ts_exit_policy_on_probe(NULL, true);
    TEST_CHECK(!ts_exit_policy_routes_public(NULL));
}

static void test_resource_guard_thresholds_and_recovery(void)
{
    ts_resource_guard_t guard;

    ts_resource_guard_init(&guard);
    TEST_CHECK(ts_resource_guard_sample(&guard, 0u, 23u * 1024u, 32u * 1024u) ==
               TS_RESOURCE_GUARD_NONE);
    TEST_CHECK(ts_resource_guard_sample(&guard, 10000u, 23u * 1024u, 32u * 1024u) ==
               TS_RESOURCE_GUARD_NONE);

    /* Either low-water mark is enough, but a healthy sample resets the run. */
    TEST_CHECK(ts_resource_guard_sample(&guard, 20000u, 64u * 1024u, 7u * 1024u) ==
               TS_RESOURCE_GUARD_STOP);
    TEST_CHECK(ts_resource_guard_sample(&guard, 30000u, 64u * 1024u, 32u * 1024u) ==
               TS_RESOURCE_GUARD_NONE);
    TEST_CHECK(ts_resource_guard_sample(&guard, 70000u, 48u * 1024u, 16u * 1024u) ==
               TS_RESOURCE_GUARD_NONE);
    TEST_CHECK(ts_resource_guard_sample(&guard, 80000u, 48u * 1024u, 16u * 1024u) ==
               TS_RESOURCE_GUARD_RETRY);
}

static void test_resource_guard_healthy_sample_resets_before_stop(void)
{
    ts_resource_guard_t guard;

    ts_resource_guard_init(&guard);
    TEST_CHECK(ts_resource_guard_sample(&guard, 0u, 23u * 1024u, 32u * 1024u) ==
               TS_RESOURCE_GUARD_NONE);
    TEST_CHECK(ts_resource_guard_sample(&guard, 10000u, 64u * 1024u, 32u * 1024u) ==
               TS_RESOURCE_GUARD_NONE);
    TEST_CHECK(ts_resource_guard_sample(&guard, 20000u, 64u * 1024u, 7u * 1024u) ==
               TS_RESOURCE_GUARD_NONE);
    TEST_CHECK(ts_resource_guard_sample(&guard, 30000u, 64u * 1024u, 7u * 1024u) ==
               TS_RESOURCE_GUARD_NONE);
    TEST_CHECK(ts_resource_guard_sample(&guard, 40000u, 64u * 1024u, 7u * 1024u) ==
               TS_RESOURCE_GUARD_STOP);
}

static void test_resource_guard_failed_retry_waits_before_retrying_again(void)
{
    ts_resource_guard_t guard;

    ts_resource_guard_init(&guard);
    TEST_CHECK(ts_resource_guard_sample(&guard, 0u, 23u * 1024u, 32u * 1024u) ==
               TS_RESOURCE_GUARD_NONE);
    TEST_CHECK(ts_resource_guard_sample(&guard, 10000u, 23u * 1024u, 32u * 1024u) ==
               TS_RESOURCE_GUARD_NONE);
    TEST_CHECK(ts_resource_guard_sample(&guard, 20000u, 23u * 1024u, 32u * 1024u) ==
               TS_RESOURCE_GUARD_STOP);

    TEST_CHECK(ts_resource_guard_sample(&guard, 80000u, 48u * 1024u, 16u * 1024u) ==
               TS_RESOURCE_GUARD_RETRY);
    TEST_CHECK(guard.stopped);
    ts_resource_guard_retry_completed(&guard, 80000u, false);
    TEST_CHECK(guard.stopped);
    TEST_CHECK(ts_resource_guard_sample(&guard, 139999u, 48u * 1024u, 16u * 1024u) ==
               TS_RESOURCE_GUARD_NONE);
    TEST_CHECK(ts_resource_guard_sample(&guard, 140000u, 48u * 1024u, 16u * 1024u) ==
               TS_RESOURCE_GUARD_RETRY);

    ts_resource_guard_retry_completed(&guard, 140000u, true);
    TEST_CHECK(!guard.stopped);
}

int main(void)
{
    test_route_classification();
    test_cgnat_boundaries();
    test_rfc1918_boundaries();
    test_exit_activation_and_fallback_thresholds();
    test_repeated_tunnel_up_preserves_probe_progress();
    test_disabled_and_tunnel_loss();
    test_opposite_probe_resets_counters();
    test_null_policy_is_safe();
    test_resource_guard_thresholds_and_recovery();
    test_resource_guard_healthy_sample_resets_before_stop();
    test_resource_guard_failed_retry_waits_before_retrying_again();

    puts("ts_claw_policy: all tests passed");
    return 0;
}
