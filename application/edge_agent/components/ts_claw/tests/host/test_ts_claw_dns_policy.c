#include "ts_claw_dns_runtime.h"

#include "lwip/dns.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_check(bool condition, const char *expression, const char *file, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: test failed: %s\n", file, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define TEST_CHECK(condition) test_check((condition), #condition, __FILE__, __LINE__)

static ip_addr_t s_dns_servers[DNS_MAX_SERVERS];
static uint32_t s_applied[TS_CLAW_DNS_BYPASS_MAX];
static size_t s_applied_count;

const ip_addr_t *dns_getserver(u8_t index)
{
    return index < DNS_MAX_SERVERS ? &s_dns_servers[index] : NULL;
}

static void capture_apply(const uint32_t *addresses, size_t count)
{
    s_applied_count = count;
    for (size_t i = 0; i < TS_CLAW_DNS_BYPASS_MAX; ++i) {
        s_applied[i] = i < count ? addresses[i] : 0u;
    }
}

static void reset_fixture(void)
{
    memset(s_dns_servers, 0, sizeof(s_dns_servers));
    memset(s_applied, 0, sizeof(s_applied));
    s_applied_count = 0u;
}

static void set_ipv4(size_t index, uint32_t host_order_ip)
{
    s_dns_servers[index].type = IPADDR_TYPE_V4;
    s_dns_servers[index].u_addr.ip4.addr = lwip_htonl(host_order_ip);
}

static void set_ipv6(size_t index)
{
    s_dns_servers[index].type = IPADDR_TYPE_V6;
    s_dns_servers[index].u_addr.ip4.addr = 0xffffffffu;
}

static ts_claw_dns_runtime_result_t refresh_active(void)
{
    ts_claw_dns_runtime_result_t result = {0};
    ts_claw_dns_runtime_refresh(true, capture_apply, &result);
    return result;
}

static void test_active_exit_resolver_modes(void)
{
    ts_claw_dns_runtime_result_t result;

    reset_fixture();
    set_ipv4(0, 0xC0A80101u);
    result = refresh_active();
    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_STA);
    TEST_CHECK(!result.bypass_active && result.bypass_count == 0u);

    reset_fixture();
    set_ipv4(0, 0x08080808u);
    result = refresh_active();
    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_STA);
    TEST_CHECK(result.bypass_active && result.bypass_count == 1u);
    TEST_CHECK(s_applied[0] == 0x08080808u);

    reset_fixture();
    set_ipv4(0, 0x64646464u);
    result = refresh_active();
    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_EXIT);
    TEST_CHECK(!result.bypass_active && result.bypass_count == 0u);

    reset_fixture();
    set_ipv4(0, 0xA9FE0101u);
    set_ipv4(1, 0x64646464u);
    result = refresh_active();
    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_MIXED);
    TEST_CHECK(!result.bypass_active && result.bypass_count == 0u);

    reset_fixture();
    set_ipv4(0, 0x01010101u);
    set_ipv4(1, 0x64646464u);
    result = refresh_active();
    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_MIXED);
    TEST_CHECK(result.bypass_active && result.bypass_count == 1u);

    reset_fixture();
    result = refresh_active();
    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_UNAVAILABLE);
    TEST_CHECK(!result.bypass_active && result.bypass_count == 0u);
}

static void test_lwip_conversion_filter_dedup_and_bound(void)
{
    reset_fixture();
    set_ipv6(0);
    set_ipv4(1, 0u);
    set_ipv4(2, 0x08080808u);
    set_ipv4(3, 0x08080808u);
    set_ipv4(4, 0x01010101u);
    set_ipv4(5, 0x09090909u);
    set_ipv4(6, 0xD043DEDEu);
    set_ipv4(7, 0x64646464u);

    const ts_claw_dns_runtime_result_t result = refresh_active();

    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_MIXED);
    TEST_CHECK(result.bypass_count == TS_CLAW_DNS_BYPASS_MAX);
    TEST_CHECK(result.bypass[0] == 0x08080808u);
    TEST_CHECK(result.bypass[1] == 0x01010101u);
    TEST_CHECK(result.bypass[2] == 0x09090909u);
    TEST_CHECK(s_applied_count == TS_CLAW_DNS_BYPASS_MAX);
}

static void test_refresh_updates_clears_and_inactive_mode(void)
{
    ts_claw_dns_runtime_result_t result = {0};

    reset_fixture();
    set_ipv4(0, 0x08080808u);
    ts_claw_dns_runtime_refresh(true, capture_apply, &result);
    TEST_CHECK(s_applied[0] == 0x08080808u && result.bypass_count == 1u);

    set_ipv4(0, 0x01010101u);
    ts_claw_dns_runtime_refresh(true, capture_apply, &result);
    TEST_CHECK(s_applied[0] == 0x01010101u && result.bypass_count == 1u);

    ts_claw_dns_runtime_refresh(false, capture_apply, &result);
    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_STA);
    TEST_CHECK(!result.bypass_active && result.bypass_count == 0u);
    TEST_CHECK(s_applied_count == 0u && s_applied[0] == 0u);

    reset_fixture();
    ts_claw_dns_runtime_refresh(true, capture_apply, &result);
    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_UNAVAILABLE);
    TEST_CHECK(s_applied_count == 0u);
}

static void test_mode_strings(void)
{
    TEST_CHECK(strcmp(ts_claw_dns_egress_name(TS_CLAW_DNS_EGRESS_STA), "sta") == 0);
    TEST_CHECK(strcmp(ts_claw_dns_egress_name(TS_CLAW_DNS_EGRESS_EXIT), "exit") == 0);
    TEST_CHECK(strcmp(ts_claw_dns_egress_name(TS_CLAW_DNS_EGRESS_MIXED), "mixed") == 0);
    TEST_CHECK(strcmp(ts_claw_dns_egress_name(TS_CLAW_DNS_EGRESS_UNAVAILABLE),
                      "unavailable") == 0);
}

int main(void)
{
    test_active_exit_resolver_modes();
    test_lwip_conversion_filter_dedup_and_bound();
    test_refresh_updates_clears_and_inactive_mode();
    test_mode_strings();

    puts("ts_claw_dns_runtime: all tests passed");
    return 0;
}
