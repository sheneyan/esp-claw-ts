#define TS_CLAW_DIAGNOSTICS_INTERNAL
#include "ts_claw_diagnostics.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CHECK(condition)                                                       \
    do {                                                                            \
        if (!(condition)) {                                                         \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,      \
                    #condition);                                                    \
            exit(EXIT_FAILURE);                                                     \
        }                                                                           \
    } while (0)

static void test_timestamp_age_is_bounded(void)
{
    TEST_CHECK(ts_claw_timestamp_age_ms(1000, 0) == 0);
    TEST_CHECK(ts_claw_timestamp_age_ms(1000, 750) == 250);
    TEST_CHECK(ts_claw_timestamp_age_ms(750, 1000) == 0);
    TEST_CHECK(ts_claw_timestamp_age_ms(1000, 1000) == 0);
    TEST_CHECK(ts_claw_timestamp_age_ms(1000, UINT64_MAX) == 0);
}

static void test_derp_rtt_conversion_respects_capacity_and_canary(void)
{
    const ts_claw_derp_rtt_source_t source[] = {
        {.region_id = 9, .rtt_ms = 224, .region_name = "Tokyo"},
        {.region_id = 2, .rtt_ms = 168, .region_name = "New York City"},
        {.region_id = 7, .rtt_ms = 0, .region_name = NULL},
    };
    ts_claw_derp_rtt_t out[3];
    ts_claw_derp_rtt_t canary;

    memset(&canary, 0xA5, sizeof(canary));
    memset(&out[2], 0xA5, sizeof(out[2]));

    const size_t count = ts_claw_convert_derp_rtts(out, 2, source, 3);

    TEST_CHECK(count == 2);
    TEST_CHECK(out[0].region_id == 9);
    TEST_CHECK(out[1].region_id == 2);
    TEST_CHECK(memcmp(&out[2], &canary, sizeof(canary)) == 0);
}

static void test_derp_rtt_conversion_rejects_empty_inputs(void)
{
    const ts_claw_derp_rtt_source_t source = {
        .region_id = 9,
        .rtt_ms = 224,
        .region_name = "Tokyo",
    };
    ts_claw_derp_rtt_t out;

    TEST_CHECK(ts_claw_convert_derp_rtts(NULL, 1, &source, 1) == 0);
    TEST_CHECK(ts_claw_convert_derp_rtts(&out, 0, &source, 1) == 0);
    TEST_CHECK(ts_claw_convert_derp_rtts(&out, 1, NULL, 1) == 0);
}

static void test_derp_rtt_conversion_preserves_order_and_timeout_semantics(void)
{
    char first_name[] = "Tokyo";
    const ts_claw_derp_rtt_source_t source[] = {
        {.region_id = 9, .rtt_ms = 224, .region_name = first_name},
        {.region_id = 2, .rtt_ms = 168, .region_name = "New York City"},
        {.region_id = 7, .rtt_ms = 0, .region_name = NULL},
    };
    ts_claw_derp_rtt_t out[3];

    const size_t count = ts_claw_convert_derp_rtts(out, 3, source, 3);

    TEST_CHECK(count == 3);
    TEST_CHECK(out[0].region_id == 9);
    TEST_CHECK(out[0].rtt_ms == 224);
    TEST_CHECK(!out[0].timed_out);
    TEST_CHECK(strcmp(out[0].region_name, "Tokyo") == 0);
    TEST_CHECK(out[1].region_id == 2);
    TEST_CHECK(out[1].rtt_ms == 168);
    TEST_CHECK(!out[1].timed_out);
    TEST_CHECK(strcmp(out[1].region_name, "New York City") == 0);
    TEST_CHECK(out[2].region_id == 7);
    TEST_CHECK(out[2].rtt_ms == 0);
    TEST_CHECK(out[2].timed_out);
    TEST_CHECK(out[2].region_name[0] == '\0');

    first_name[0] = 'X';
    TEST_CHECK(strcmp(out[0].region_name, "Tokyo") == 0);
}

static void test_derp_rtt_conversion_clamps_and_terminates_names(void)
{
    char long_name[TS_CLAW_REGION_NAME_LEN + 16];
    ts_claw_derp_rtt_source_t source[TS_CLAW_MAX_DERP_RTTS + 2];
    ts_claw_derp_rtt_t out[TS_CLAW_MAX_DERP_RTTS + 2];

    memset(long_name, 'a', sizeof(long_name));
    long_name[sizeof(long_name) - 1] = '\0';
    for (size_t i = 0; i < TS_CLAW_MAX_DERP_RTTS + 2; ++i) {
        source[i].region_id = (uint16_t)(i + 1);
        source[i].rtt_ms = (uint16_t)(i + 10);
        source[i].region_name = long_name;
    }

    const size_t count = ts_claw_convert_derp_rtts(
        out, TS_CLAW_MAX_DERP_RTTS + 2, source, TS_CLAW_MAX_DERP_RTTS + 2);

    TEST_CHECK(count == TS_CLAW_MAX_DERP_RTTS);
    TEST_CHECK(out[TS_CLAW_MAX_DERP_RTTS - 1].region_id == TS_CLAW_MAX_DERP_RTTS);
    for (size_t i = 0; i < count; ++i) {
        TEST_CHECK(out[i].region_name[TS_CLAW_REGION_NAME_LEN - 1] == '\0');
    }
}

int main(void)
{
    test_timestamp_age_is_bounded();
    test_derp_rtt_conversion_preserves_order_and_timeout_semantics();
    test_derp_rtt_conversion_respects_capacity_and_canary();
    test_derp_rtt_conversion_rejects_empty_inputs();
    test_derp_rtt_conversion_clamps_and_terminates_names();

    puts("ts_claw_diagnostics: all tests passed");
    return 0;
}
