#include "ts_claw_dns_runtime.h"

#include "esp_netif.h"
#include "lwip/ip4_addr.h"

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

static esp_netif_t s_sta_netif;
static esp_netif_t s_other_netif;
static esp_netif_dns_info_t s_dns_servers[ESP_NETIF_DNS_MAX];
static esp_err_t s_dns_results[ESP_NETIF_DNS_MAX];
static esp_netif_t *s_last_netif;
static esp_netif_dns_type_t s_requested_types[16];
static size_t s_request_count;
static uint32_t s_applied[TS_CLAW_DNS_BYPASS_MAX];
static size_t s_applied_count;

esp_err_t esp_netif_get_dns_info(esp_netif_t *netif,
                                 esp_netif_dns_type_t type,
                                 esp_netif_dns_info_t *dns)
{
    s_last_netif = netif;
    if ((size_t)type >= ESP_NETIF_DNS_MAX || dns == NULL) {
        return ESP_FAIL;
    }
    if (s_request_count < sizeof(s_requested_types) / sizeof(s_requested_types[0])) {
        s_requested_types[s_request_count] = type;
    }
    s_request_count++;
    if (s_dns_results[type] != ESP_OK) {
        return s_dns_results[type];
    }
    *dns = s_dns_servers[type];
    return ESP_OK;
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
    memset(s_dns_results, 0, sizeof(s_dns_results));
    memset(s_requested_types, 0, sizeof(s_requested_types));
    memset(s_applied, 0, sizeof(s_applied));
    s_last_netif = NULL;
    s_request_count = 0u;
    s_applied_count = 0u;
}

static void set_ipv4(size_t index, uint32_t host_order_ip)
{
    s_dns_servers[index].ip.type = ESP_IPADDR_TYPE_V4;
    s_dns_servers[index].ip.u_addr.ip4.addr = lwip_htonl(host_order_ip);
}

static void set_ipv6(size_t index)
{
    s_dns_servers[index].ip.type = ESP_IPADDR_TYPE_V6;
    s_dns_servers[index].ip.u_addr.ip6.addr[0] = 0xffffffffu;
}

static ts_claw_dns_runtime_result_t refresh_active(void)
{
    ts_claw_dns_runtime_result_t result = {0};
    ts_claw_dns_runtime_refresh(&s_sta_netif, true, capture_apply, &result);
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

static void test_netif_conversion_filter_dedup_and_three_slots(void)
{
    reset_fixture();
    set_ipv6(0);
    set_ipv4(1, 0u);
    set_ipv4(2, 0x08080808u);

    const ts_claw_dns_runtime_result_t result = refresh_active();

    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_STA);
    TEST_CHECK(result.bypass_count == 1u);
    TEST_CHECK(result.bypass[0] == 0x08080808u);
    TEST_CHECK(s_applied_count == 1u);
    TEST_CHECK(s_last_netif == &s_sta_netif);
    TEST_CHECK(s_request_count == ESP_NETIF_DNS_MAX);
    TEST_CHECK(s_requested_types[0] == ESP_NETIF_DNS_MAIN);
    TEST_CHECK(s_requested_types[1] == ESP_NETIF_DNS_BACKUP);
    TEST_CHECK(s_requested_types[2] == ESP_NETIF_DNS_FALLBACK);
}

static void test_read_failures_are_ignored(void)
{
    reset_fixture();
    set_ipv4(0, 0x08080808u);
    set_ipv4(1, 0x01010101u);
    set_ipv4(2, 0x64646464u);
    s_dns_results[ESP_NETIF_DNS_BACKUP] = ESP_FAIL;

    const ts_claw_dns_runtime_result_t result = refresh_active();

    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_MIXED);
    TEST_CHECK(result.bypass_count == 1u);
    TEST_CHECK(result.bypass[0] == 0x08080808u);
}

static void test_refresh_updates_clears_and_inactive_mode(void)
{
    ts_claw_dns_runtime_result_t result = {0};

    reset_fixture();
    set_ipv4(0, 0x08080808u);
    ts_claw_dns_runtime_refresh(&s_sta_netif, true, capture_apply, &result);
    TEST_CHECK(s_applied[0] == 0x08080808u && result.bypass_count == 1u);

    set_ipv4(0, 0x01010101u);
    ts_claw_dns_runtime_refresh(&s_sta_netif, true, capture_apply, &result);
    TEST_CHECK(s_applied[0] == 0x01010101u && result.bypass_count == 1u);

    ts_claw_dns_runtime_refresh(&s_other_netif, false, capture_apply, &result);
    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_STA);
    TEST_CHECK(!result.bypass_active && result.bypass_count == 0u);
    TEST_CHECK(s_applied_count == 0u && s_applied[0] == 0u);

    reset_fixture();
    ts_claw_dns_runtime_refresh(NULL, true, capture_apply, &result);
    TEST_CHECK(result.egress == TS_CLAW_DNS_EGRESS_UNAVAILABLE);
    TEST_CHECK(s_applied_count == 0u);
    TEST_CHECK(s_request_count == 0u);
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
    test_netif_conversion_filter_dedup_and_three_slots();
    test_read_failures_are_ignored();
    test_refresh_updates_clears_and_inactive_mode();
    test_mode_strings();

    puts("ts_claw_dns_runtime: all tests passed");
    return 0;
}
