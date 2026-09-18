#include "ts_claw_dns_policy.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void test_check(bool condition, const char *expression, const char *file, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: test failed: %s\n", file, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define TEST_CHECK(condition) test_check((condition), #condition, __FILE__, __LINE__)

typedef struct {
    const ts_claw_dns_server_t *servers;
    size_t count;
} reader_fixture_t;

typedef struct {
    uint32_t addresses[TS_CLAW_DNS_BYPASS_MAX];
    size_t count;
} applied_fixture_t;

static applied_fixture_t s_applied;

static bool fixture_reader(size_t index, ts_claw_dns_server_t *server, void *ctx)
{
    const reader_fixture_t *fixture = ctx;
    if (index >= fixture->count) {
        return false;
    }
    *server = fixture->servers[index];
    return true;
}

static void capture_apply(const uint32_t *addresses, size_t count)
{
    s_applied.count = count;
    for (size_t i = 0; i < TS_CLAW_DNS_BYPASS_MAX; ++i) {
        s_applied.addresses[i] = i < count ? addresses[i] : 0u;
    }
}

static void test_capture_filters_non_public_and_deduplicates(void)
{
    const ts_claw_dns_server_t servers[] = {
        {.present = false, .ipv4 = true, .host_order_ip = 0x08080808u},
        {.present = true, .ipv4 = false, .host_order_ip = 0x08080808u},
        {.present = true, .ipv4 = true, .host_order_ip = 0u},
        {.present = true, .ipv4 = true, .host_order_ip = 0xC0A80101u},
        {.present = true, .ipv4 = true, .host_order_ip = 0xA9FE0101u},
        {.present = true, .ipv4 = true, .host_order_ip = 0x64646464u},
        {.present = true, .ipv4 = true, .host_order_ip = 0x08080808u},
        {.present = true, .ipv4 = true, .host_order_ip = 0x08080808u},
        {.present = true, .ipv4 = true, .host_order_ip = 0x01010101u},
    };
    const reader_fixture_t fixture = {
        .servers = servers,
        .count = sizeof(servers) / sizeof(servers[0]),
    };
    ts_claw_dns_refresh_result_t result = {0};

    ts_claw_dns_refresh(fixture_reader, (void *)&fixture, fixture.count,
                        capture_apply, &result);

    TEST_CHECK(result.bypass_count == 2u);
    TEST_CHECK(result.bypass[0] == 0x08080808u);
    TEST_CHECK(result.bypass[1] == 0x01010101u);
    TEST_CHECK(s_applied.count == 2u);
    TEST_CHECK(s_applied.addresses[0] == 0x08080808u);
    TEST_CHECK(s_applied.addresses[1] == 0x01010101u);
}

static void test_capture_is_bounded_and_preserves_reader_order(void)
{
    const ts_claw_dns_server_t servers[] = {
        {.present = true, .ipv4 = true, .host_order_ip = 0x01010101u},
        {.present = true, .ipv4 = true, .host_order_ip = 0x08080808u},
        {.present = true, .ipv4 = true, .host_order_ip = 0x09090909u},
        {.present = true, .ipv4 = true, .host_order_ip = 0xD043DEDEu},
    };
    const reader_fixture_t fixture = {
        .servers = servers,
        .count = sizeof(servers) / sizeof(servers[0]),
    };
    ts_claw_dns_refresh_result_t result = {0};

    ts_claw_dns_refresh(fixture_reader, (void *)&fixture, fixture.count,
                        capture_apply, &result);

    TEST_CHECK(result.bypass_count == TS_CLAW_DNS_BYPASS_MAX);
    TEST_CHECK(result.bypass[0] == 0x01010101u);
    TEST_CHECK(result.bypass[1] == 0x08080808u);
    TEST_CHECK(result.bypass[2] == 0x09090909u);
    TEST_CHECK(s_applied.count == TS_CLAW_DNS_BYPASS_MAX);
}

static void test_refresh_updates_and_clears_applied_route_list(void)
{
    const ts_claw_dns_server_t first_servers[] = {
        {.present = true, .ipv4 = true, .host_order_ip = 0x08080808u},
    };
    const ts_claw_dns_server_t second_servers[] = {
        {.present = true, .ipv4 = true, .host_order_ip = 0x01010101u},
    };
    const reader_fixture_t first = {.servers = first_servers, .count = 1u};
    const reader_fixture_t second = {.servers = second_servers, .count = 1u};
    const reader_fixture_t empty = {.servers = NULL, .count = 0u};
    ts_claw_dns_refresh_result_t result = {0};

    ts_claw_dns_refresh(fixture_reader, (void *)&first, first.count,
                        capture_apply, &result);
    TEST_CHECK(result.bypass_count == 1u);
    TEST_CHECK(s_applied.addresses[0] == 0x08080808u);

    ts_claw_dns_refresh(fixture_reader, (void *)&second, second.count,
                        capture_apply, &result);
    TEST_CHECK(result.bypass_count == 1u);
    TEST_CHECK(s_applied.addresses[0] == 0x01010101u);

    ts_claw_dns_refresh(fixture_reader, (void *)&empty, empty.count,
                        capture_apply, &result);
    TEST_CHECK(result.bypass_count == 0u);
    TEST_CHECK(s_applied.count == 0u);
    TEST_CHECK(s_applied.addresses[0] == 0u);
}

int main(void)
{
    test_capture_filters_non_public_and_deduplicates();
    test_capture_is_bounded_and_preserves_reader_order();
    test_refresh_updates_and_clears_applied_route_list();

    puts("ts_claw_dns_policy: all tests passed");
    return 0;
}
