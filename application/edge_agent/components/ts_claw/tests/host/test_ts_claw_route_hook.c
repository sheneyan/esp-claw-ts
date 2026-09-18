#include "ts_claw_route_hook.h"
#include "ts_claw_policy.h"

#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

#include <stddef.h>
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

static struct netif s_default_netif = {.marker = 1};
static struct netif s_sta_netif = {.marker = 2};
static struct netif s_wg_netif = {.marker = 3};
static unsigned int s_real_hook_calls;

bool ts_route_is_loopback(uint32_t host_order_ip)
{
    return (host_order_ip & 0xFF000000u) == 0x7F000000u;
}

bool ts_route_is_cgnat(uint32_t host_order_ip)
{
    return (host_order_ip & 0xFFC00000u) == 0x64400000u;
}

bool ts_route_is_local_bypass(uint32_t host_order_ip)
{
    const bool private_lan =
        (host_order_ip & 0xFF000000u) == 0x0A000000u ||
        (host_order_ip & 0xFFF00000u) == 0xAC100000u ||
        (host_order_ip & 0xFFFF0000u) == 0xC0A80000u;
    const bool link_local =
        (host_order_ip & 0xFFFF0000u) == 0xA9FE0000u;

    /* Deliberately simulate future helper drift: the runtime guard must win. */
    return private_lan || link_local || ts_route_is_loopback(host_order_ip);
}

bool ts_route_is_public_unicast(uint32_t host_order_ip)
{
    return host_order_ip == 0x08080808u || host_order_ip == 0x01010101u;
}

struct netif *__real_ip4_route_src_hook(const ip4_addr_t *src,
                                        const ip4_addr_t *dest)
{
    (void)src;
    (void)dest;
    s_real_hook_calls++;
    return &s_default_netif;
}

struct netif *__wrap_ip4_route_src_hook(const ip4_addr_t *src,
                                        const ip4_addr_t *dest);
void ts_claw_route_hook_set_dns_bypass(const uint32_t *host_order_ips,
                                       size_t count);

static ip4_addr_t network_address(uint32_t host_order_ip)
{
    return (ip4_addr_t) {.addr = lwip_htonl(host_order_ip)};
}

static void test_loopback_delegates_to_lwip(void)
{
    const ip4_addr_t loopback = network_address(0x7F000001u);

    ts_claw_route_hook_reset();
    ts_claw_route_hook_set_netifs(&s_sta_netif, &s_wg_netif);
    s_real_hook_calls = 0u;

    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &loopback) == &s_default_netif);
    TEST_CHECK(s_real_hook_calls == 1u);
}

static void test_existing_route_targets_are_preserved(void)
{
    const ip4_addr_t private_lan = network_address(0xC0A80132u);
    const ip4_addr_t cgnat_peer = network_address(0x6457967Au);
    const ip4_addr_t public_ip = network_address(0x08080808u);

    ts_claw_route_hook_reset();
    ts_claw_route_hook_set_netifs(&s_sta_netif, &s_wg_netif);
    s_real_hook_calls = 0u;

    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &private_lan) == &s_sta_netif);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &cgnat_peer) == &s_wg_netif);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &public_ip) == &s_default_netif);
    TEST_CHECK(s_real_hook_calls == 1u);

    ts_claw_route_hook_set_tunnel_available(true);
    ts_claw_route_hook_set_upstream_pinned(true);
    ts_claw_route_hook_set_exit_active(true);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &public_ip) == &s_wg_netif);
    TEST_CHECK(s_real_hook_calls == 1u);
}

static void test_null_destination_delegates_to_lwip(void)
{
    s_real_hook_calls = 0u;
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, NULL) == &s_default_netif);
    TEST_CHECK(s_real_hook_calls == 1u);
}

static void test_active_probe_keeps_cgnat_fail_closed_during_wg_loss(void)
{
    const ip4_addr_t cgnat_peer = network_address(0x6457967Au);

    ts_claw_route_hook_reset();
    ts_claw_route_hook_set_netifs(&s_sta_netif, &s_wg_netif);
    ts_claw_route_hook_set_tunnel_available(true);
    ts_claw_route_hook_set_upstream_pinned(true);
    ts_claw_route_hook_set_probe_active(true);

    /* A disappearing WG netif must not let the unbound probe fall via STA. */
    ts_claw_route_hook_set_netifs(&s_sta_netif, NULL);
    ts_claw_route_hook_set_tunnel_available(false);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &cgnat_peer) == &s_wg_netif);

    /* Only a fully retired probe permits clearing the WG route state. */
    ts_claw_route_hook_set_probe_active(false);
    ts_claw_route_hook_set_netifs(&s_sta_netif, NULL);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &cgnat_peer) == &s_default_netif);
}

static void activate_exit(void)
{
    ts_claw_route_hook_set_netifs(&s_sta_netif, &s_wg_netif);
    ts_claw_route_hook_set_tunnel_available(true);
    ts_claw_route_hook_set_upstream_pinned(true);
    ts_claw_route_hook_set_exit_active(true);
}

static void test_public_dns_bypass_is_exact_and_exit_scoped(void)
{
    const uint32_t resolvers[] = {0x08080808u};
    const ip4_addr_t captured_dns = network_address(0x08080808u);
    const ip4_addr_t other_public = network_address(0x01010101u);

    ts_claw_route_hook_reset();
    activate_exit();
    ts_claw_route_hook_set_dns_bypass(resolvers, 1u);

    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &captured_dns) == &s_sta_netif);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &other_public) == &s_wg_netif);

    ts_claw_route_hook_set_exit_active(false);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &captured_dns) == &s_default_netif);
}

static void test_cgnat_resolver_never_bypasses_wireguard(void)
{
    const uint32_t resolvers[] = {0x64646464u}; /* 100.100.100.100 */
    const ip4_addr_t magic_dns = network_address(resolvers[0]);

    ts_claw_route_hook_reset();
    activate_exit();
    ts_claw_route_hook_set_dns_bypass(resolvers, 1u);

    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &magic_dns) == &s_wg_netif);
}

static void test_dns_bypass_update_and_clear_remove_old_entries(void)
{
    const uint32_t old_resolver[] = {0x08080808u};
    const uint32_t new_resolver[] = {0x01010101u};
    const ip4_addr_t old_dns = network_address(old_resolver[0]);
    const ip4_addr_t new_dns = network_address(new_resolver[0]);

    ts_claw_route_hook_reset();
    activate_exit();
    ts_claw_route_hook_set_dns_bypass(old_resolver, 1u);
    ts_claw_route_hook_set_dns_bypass(new_resolver, 1u);

    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &old_dns) == &s_wg_netif);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &new_dns) == &s_sta_netif);

    ts_claw_route_hook_set_dns_bypass(NULL, 0u);
    TEST_CHECK(__wrap_ip4_route_src_hook(NULL, &new_dns) == &s_wg_netif);
}

static void test_dns_bypass_is_cleared_by_exit_lifecycle(void)
{
    const uint32_t resolver[] = {0x08080808u};

    ts_claw_route_hook_reset();
    activate_exit();
    ts_claw_route_hook_set_dns_bypass(resolver, 1u);
    TEST_CHECK(ts_claw_route_hook_get_state().dns_bypass_count == 1u);

    ts_claw_route_hook_set_tunnel_available(false);
    TEST_CHECK(ts_claw_route_hook_get_state().dns_bypass_count == 0u);

    activate_exit();
    ts_claw_route_hook_set_dns_bypass(resolver, 1u);
    ts_claw_route_hook_set_netifs(NULL, NULL);
    TEST_CHECK(ts_claw_route_hook_get_state().dns_bypass_count == 0u);

    activate_exit();
    ts_claw_route_hook_set_dns_bypass(resolver, 1u);
    ts_claw_route_hook_set_upstream_pinned(false);
    TEST_CHECK(ts_claw_route_hook_get_state().dns_bypass_count == 0u);

    activate_exit();
    ts_claw_route_hook_set_dns_bypass(resolver, 1u);
    ts_claw_route_hook_set_exit_active(false);
    TEST_CHECK(ts_claw_route_hook_get_state().dns_bypass_count == 0u);
}

int main(void)
{
    test_loopback_delegates_to_lwip();
    test_existing_route_targets_are_preserved();
    test_null_destination_delegates_to_lwip();
    test_active_probe_keeps_cgnat_fail_closed_during_wg_loss();
    test_public_dns_bypass_is_exact_and_exit_scoped();
    test_cgnat_resolver_never_bypasses_wireguard();
    test_dns_bypass_update_and_clear_remove_old_entries();
    test_dns_bypass_is_cleared_by_exit_lifecycle();

    puts("ts_claw_route_hook: all tests passed");
    return 0;
}
