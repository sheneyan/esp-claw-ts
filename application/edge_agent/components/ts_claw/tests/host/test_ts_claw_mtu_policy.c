#include <stdbool.h>
#include <stdio.h>

#include "lwip/err.h"
#include "lwip/tcpip.h"
#include "ts_claw_mtu_policy.h"

static int s_failures;
static err_t s_callback_result = ERR_OK;
static bool s_run_callback = true;

#define TEST_CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
        s_failures++; \
    } \
} while (0)

err_t tcpip_callback_with_block(tcpip_callback_fn fn, void *ctx, u8_t block)
{
    TEST_CHECK(block == 1u);
    if (s_callback_result == ERR_OK && s_run_callback) {
        fn(ctx);
    }
    return s_callback_result;
}

static void test_applies_verified_mtu(void)
{
    struct netif first = {.mtu = 1420u};
    struct netif second = {.mtu = 1420u};

    TEST_CHECK(ts_claw_apply_wg_mtu(&first) == ESP_OK);
    TEST_CHECK(first.mtu == TS_CLAW_WG_MTU);
    TEST_CHECK(ts_claw_apply_wg_mtu(&first) == ESP_OK);
    TEST_CHECK(ts_claw_apply_wg_mtu(&second) == ESP_OK);
    TEST_CHECK(second.mtu == TS_CLAW_WG_MTU);
}

static void test_rejects_invalid_or_failed_apply(void)
{
    s_callback_result = ERR_OK;
    s_run_callback = true;
    TEST_CHECK(ts_claw_apply_wg_mtu(NULL) == ESP_ERR_INVALID_ARG);

    struct netif netif = {.mtu = 1420u};
    s_callback_result = ERR_MEM;
    TEST_CHECK(ts_claw_apply_wg_mtu(&netif) == ESP_FAIL);
    TEST_CHECK(netif.mtu == 1420u);

    s_callback_result = ERR_OK;
    s_run_callback = false;
    TEST_CHECK(ts_claw_apply_wg_mtu(&netif) == ESP_FAIL);
}

int main(void)
{
    test_applies_verified_mtu();
    test_rejects_invalid_or_failed_apply();
    return s_failures == 0 ? 0 : 1;
}
