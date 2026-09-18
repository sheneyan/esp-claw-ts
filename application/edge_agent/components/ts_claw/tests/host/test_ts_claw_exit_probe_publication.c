#include "ts_claw_exit_probe_publication.h"
#include "ts_claw_policy.h"
#include "ts_claw_route_hook.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void check(bool condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "line %d: test failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

struct netif *__real_ip4_route_src_hook(const ip4_addr_t *src,
                                        const ip4_addr_t *dest)
{
    (void)src;
    (void)dest;
    return NULL;
}

static void test_stale_callback_cannot_publish_after_clear(void)
{
    struct netif sta = {0};
    struct netif wg = {0};
    const uint32_t resolver = 0x08080808u;
    ts_claw_route_hook_reset();
    ts_claw_route_hook_set_netifs(&sta, &wg);
    ts_claw_route_hook_set_tunnel_available(true);
    ts_claw_route_hook_set_upstream_pinned(true);
    ts_claw_route_hook_set_probe_active(true);
    ts_claw_route_hook_set_dns_bypass(&resolver, 1u);
    ts_claw_route_hook_set_exit_active(true);

    ts_claw_exit_probe_publication_t state;
    ts_claw_exit_probe_publication_init(&state);
    ts_claw_exit_probe_publication_set_accept(&state, true);
    ts_exit_policy_t policy;
    ts_exit_policy_init(&policy, true);
    ts_exit_policy_set_tunnel(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    ts_exit_policy_on_probe(&policy, true);
    CHECK(ts_exit_policy_routes_public(&policy));

    const uint32_t callback_generation =
        ts_claw_exit_probe_publication_begin(&state, true);
    CHECK(callback_generation != 0u);

    /* Teardown completes while an already-accepted callback is paused. */
    ts_claw_exit_probe_publication_set_accept(&state, false);
    ts_exit_policy_set_tunnel(&policy, false);
    ts_claw_route_hook_set_exit_active(false);
    ts_claw_route_hook_set_dns_bypass(NULL, 0u);

    CHECK(!ts_claw_exit_probe_publication_complete(
        &state, callback_generation, true));
    bool success = true;
    CHECK(!ts_claw_exit_probe_publication_take(&state, &success));
    const ts_claw_route_state_t route = ts_claw_route_hook_get_state();
    CHECK(!route.exit_active);
    CHECK(route.dns_bypass_count == 0u);
    /* update_exit_status_locked derives STA fallback from this cleared policy. */
    CHECK(!ts_exit_policy_routes_public(&policy));
}

static void test_worker_consumes_current_result_once(void)
{
    ts_claw_exit_probe_publication_t state;
    ts_claw_exit_probe_publication_init(&state);
    ts_claw_exit_probe_publication_set_accept(&state, true);

    const uint32_t generation =
        ts_claw_exit_probe_publication_begin(&state, true);
    CHECK(ts_claw_exit_probe_publication_complete(&state, generation, false));

    bool success = true;
    CHECK(ts_claw_exit_probe_publication_take(&state, &success));
    CHECK(!success);
    CHECK(!ts_claw_exit_probe_publication_take(&state, &success));
}

static void test_old_session_callback_is_rejected_after_reenable(void)
{
    ts_claw_exit_probe_publication_t state;
    ts_claw_exit_probe_publication_init(&state);
    ts_claw_exit_probe_publication_set_accept(&state, true);
    const uint32_t old_generation =
        ts_claw_exit_probe_publication_begin(&state, true);

    ts_claw_exit_probe_publication_set_accept(&state, false);
    ts_claw_exit_probe_publication_set_accept(&state, true);

    CHECK(!ts_claw_exit_probe_publication_complete(
        &state, old_generation, true));
}

int main(void)
{
    test_stale_callback_cannot_publish_after_clear();
    test_worker_consumes_current_result_once();
    test_old_session_callback_is_rejected_after_reenable();
    puts("ts_claw_exit_probe_publication: all tests passed");
    return 0;
}
