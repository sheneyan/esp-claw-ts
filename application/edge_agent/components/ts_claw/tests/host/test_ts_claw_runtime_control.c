#include "ts_claw_runtime_control.h"

#include <stdbool.h>
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

enum {
    TEST_OLD_IP = 0x64400101u,
    TEST_NEW_IP = 0x64400102u,
    TEST_ERROR = 0x701,
};

typedef struct {
    char calls[128];
    size_t call_count;
    uint32_t desired_ip;
    esp_err_t retire_result;
    esp_err_t destroy_results[4];
    uint32_t destroy_advance_ms[4];
    size_t destroy_count;
    esp_err_t set_results[3];
    size_t set_count;
    esp_err_t start_results[3];
    size_t start_count;
    esp_err_t rebind_result;
    bool reconnect_connected_values[8];
    uint64_t reconnect_token_values[8];
    esp_err_t reconnect_results[8];
    size_t reconnect_count;
    bool connected_values[8];
    esp_err_t connected_results[8];
    size_t connected_count;
    bool exit_values[8];
    esp_err_t exit_results[8];
    size_t exit_count;
    uint64_t now_ms;
    bool ml_exists;
    ts_claw_runtime_destroy_retry_t destroy_retry;
} fake_runtime_t;

static void log_call(fake_runtime_t *fake, char call)
{
    TEST_CHECK(fake->call_count + 1u < sizeof(fake->calls));
    fake->calls[fake->call_count++] = call;
    fake->calls[fake->call_count] = '\0';
}

static esp_err_t fake_retire_probe(void *ctx)
{
    fake_runtime_t *fake = ctx;
    log_call(fake, 'P');
    return fake->retire_result;
}

static esp_err_t fake_destroy(void *ctx)
{
    fake_runtime_t *fake = ctx;
    log_call(fake, 'D');
    const size_t index = fake->destroy_count++;
    if (index < 4u) {
        fake->now_ms += fake->destroy_advance_ms[index];
    }
    const esp_err_t result = index < 4u ? fake->destroy_results[index] : ESP_OK;
    if (result == ESP_OK) {
        fake->ml_exists = false;
    } else {
        ts_claw_runtime_destroy_retry_schedule(
            &fake->destroy_retry, TS_CLAW_RUNTIME_DESTROY_RETRY_STOP,
            fake->now_ms, 1000u);
    }
    return result;
}

static esp_err_t fake_set_desired_ip(void *ctx, uint32_t desired_ip)
{
    fake_runtime_t *fake = ctx;
    log_call(fake, desired_ip == TEST_NEW_IP ? 'N' : desired_ip == TEST_OLD_IP ? 'O' : 'C');
    const size_t index = fake->set_count++;
    const esp_err_t result = index < 3u ? fake->set_results[index] : ESP_OK;
    if (result == ESP_OK) {
        fake->desired_ip = desired_ip;
    }
    return result;
}

static esp_err_t fake_start(void *ctx)
{
    fake_runtime_t *fake = ctx;
    log_call(fake, 'S');
    const size_t index = fake->start_count++;
    const esp_err_t result = index < 3u ? fake->start_results[index] : ESP_OK;
    fake->ml_exists = result == ESP_OK;
    return result;
}

static uint64_t fake_now_ms(void *ctx)
{
    return ((fake_runtime_t *)ctx)->now_ms;
}

static esp_err_t fake_rebind(void *ctx)
{
    fake_runtime_t *fake = ctx;
    log_call(fake, 'R');
    return fake->rebind_result;
}

static esp_err_t fake_observe_reconnect_status(void *ctx,
                                               bool *connected,
                                               uint64_t *control_rx_token)
{
    fake_runtime_t *fake = ctx;
    log_call(fake, 'Q');
    const size_t index = fake->reconnect_count++;
    *connected = index < 8u ? fake->reconnect_connected_values[index] : false;
    *control_rx_token = index < 8u ? fake->reconnect_token_values[index] : 0u;
    return index < 8u ? fake->reconnect_results[index] : ESP_OK;
}

static esp_err_t fake_observe_connected(void *ctx, bool *connected)
{
    fake_runtime_t *fake = ctx;
    log_call(fake, 'K');
    const size_t index = fake->connected_count++;
    *connected = index < 8u ? fake->connected_values[index] : false;
    return index < 8u ? fake->connected_results[index] : ESP_OK;
}

static esp_err_t fake_observe_exit_active(void *ctx, bool *active)
{
    fake_runtime_t *fake = ctx;
    log_call(fake, 'E');
    const size_t index = fake->exit_count++;
    *active = index < 8u ? fake->exit_values[index] : false;
    return index < 8u ? fake->exit_results[index] : ESP_OK;
}

static const ts_claw_runtime_ops_t s_ops = {
    .retire_probe = fake_retire_probe,
    .destroy = fake_destroy,
    .set_desired_ip = fake_set_desired_ip,
    .start = fake_start,
    .rebind = fake_rebind,
    .observe_reconnect_status = fake_observe_reconnect_status,
    .observe_connected = fake_observe_connected,
    .observe_exit_active = fake_observe_exit_active,
    .now_ms = fake_now_ms,
};

static void fake_init(fake_runtime_t *fake, uint32_t desired_ip)
{
    memset(fake, 0, sizeof(*fake));
    fake->desired_ip = desired_ip;
    fake->ml_exists = true;
}

static void test_wifi_pending_defers_up_during_runtime_operation(void)
{
    ts_claw_runtime_wifi_pending_t pending = {0};
    bool has_ip = true;
    void *netif = (void *)(uintptr_t)1u;
    void *const first_netif = (void *)(uintptr_t)2u;
    void *const latest_netif = (void *)(uintptr_t)3u;

    TEST_CHECK(ts_claw_runtime_wifi_pending_record(&pending, false, NULL));
    TEST_CHECK(ts_claw_runtime_wifi_pending_take(
        &pending, true, false, &has_ip, &netif));
    TEST_CHECK(!has_ip && netif == NULL);

    TEST_CHECK(ts_claw_runtime_wifi_pending_record(
        &pending, true, first_netif));
    TEST_CHECK(!ts_claw_runtime_wifi_pending_take(
        &pending, true, false, &has_ip, &netif));
    TEST_CHECK(pending.pending);
    TEST_CHECK(!ts_claw_runtime_wifi_pending_record(
        &pending, true, latest_netif));
    TEST_CHECK(ts_claw_runtime_wifi_pending_take(
        &pending, false, false, &has_ip, &netif));
    TEST_CHECK(has_ip && netif == latest_netif);
    TEST_CHECK(!pending.pending);
}

static void test_wifi_pending_down_supersedes_deferred_up(void)
{
    ts_claw_runtime_wifi_pending_t pending = {0};
    bool has_ip = true;
    void *netif = (void *)(uintptr_t)1u;

    TEST_CHECK(ts_claw_runtime_wifi_pending_record(
        &pending, true, (void *)(uintptr_t)2u));
    TEST_CHECK(!ts_claw_runtime_wifi_pending_take(
        &pending, true, false, &has_ip, &netif));
    TEST_CHECK(!ts_claw_runtime_wifi_pending_record(&pending, false, NULL));
    TEST_CHECK(ts_claw_runtime_wifi_pending_take(
        &pending, true, false, &has_ip, &netif));
    TEST_CHECK(!has_ip && netif == NULL);
}

static ts_claw_runtime_completion_t take_completion(ts_claw_runtime_control_t *control,
                                                     esp_err_t expected_error)
{
    ts_claw_runtime_completion_t completion;
    esp_err_t error = ESP_OK;
    memset(&completion, 0, sizeof(completion));
    TEST_CHECK(ts_claw_runtime_control_take_completion(control, &error, &completion));
    TEST_CHECK(error == expected_error);
    TEST_CHECK(!ts_claw_runtime_control_take_completion(control, &error, &completion));
    return completion;
}

static void test_set_success_sequence(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.connected_values[0] = true;
    fake.exit_values[0] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 50u) == ESP_OK);
    TEST_CHECK(strcmp(fake.calls, "PDNS") == 0);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    ts_claw_runtime_control_advance(&control, 50u);
    TEST_CHECK(strcmp(fake.calls, "PDNSKE") == 0);

    const ts_claw_runtime_completion_t completion = take_completion(&control, ESP_OK);
    TEST_CHECK(completion.selected_exit_node_ip == TEST_NEW_IP);
    TEST_CHECK(completion.exit_active);
    TEST_CHECK(!completion.rollback_attempted);
    TEST_CHECK(!completion.rollback_recovered);
}

static void test_clear_success_stops_after_connected(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.connected_values[0] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, 0u, 1000u, 20u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, 20u);
    TEST_CHECK(strcmp(fake.calls, "PDCSK") == 0);

    const ts_claw_runtime_completion_t completion = take_completion(&control, ESP_OK);
    TEST_CHECK(completion.selected_exit_node_ip == 0u);
    TEST_CHECK(!completion.exit_active);
    TEST_CHECK(fake.exit_count == 0u);
}

static void assert_recovered_failure(fake_runtime_t *fake,
                                     ts_claw_runtime_control_t *control,
                                     esp_err_t expected_error)
{
    const ts_claw_runtime_completion_t completion = take_completion(control, expected_error);
    TEST_CHECK(fake->desired_ip == TEST_OLD_IP);
    TEST_CHECK(completion.selected_exit_node_ip == TEST_OLD_IP);
    TEST_CHECK(completion.rollback_attempted);
    TEST_CHECK(completion.rollback_recovered);
}

static void test_destroy_failure_rolls_back_once(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.destroy_results[0] = TEST_ERROR;
    fake.connected_values[0] = true;
    fake.exit_values[0] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    TEST_CHECK(strcmp(fake.calls, "PDPDOS") == 0);
    ts_claw_runtime_control_advance(&control, 1u);
    TEST_CHECK(strcmp(fake.calls, "PDPDOSKE") == 0);
    assert_recovered_failure(&fake, &control, TEST_ERROR);
    TEST_CHECK(fake.start_count == 1u);
}

static void test_two_destroy_timeouts_leave_one_cleanup_retry_until_success(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.destroy_results[0] = TEST_ERROR;
    fake.destroy_results[1] = TEST_ERROR;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    const ts_claw_runtime_completion_t completion =
        take_completion(&control, TEST_ERROR);
    TEST_CHECK(completion.rollback_attempted);
    TEST_CHECK(!completion.rollback_recovered);
    TEST_CHECK(fake.destroy_count == 2u);
    TEST_CHECK(fake.start_count == 0u);
    TEST_CHECK(fake.destroy_retry.mode == TS_CLAW_RUNTIME_DESTROY_RETRY_STOP);
    TEST_CHECK(fake.ml_exists);

    fake.now_ms = 1000u;
    TEST_CHECK(ts_claw_runtime_destroy_retry_due(
        &fake.destroy_retry, fake.now_ms));
    TEST_CHECK(fake_destroy(&fake) == ESP_OK);
    ts_claw_runtime_destroy_retry_clear(&fake.destroy_retry);
    TEST_CHECK(!fake.ml_exists);
    TEST_CHECK(fake.destroy_count == 3u);
    TEST_CHECK(fake.start_count == 0u);
    TEST_CHECK(fake.destroy_retry.mode == TS_CLAW_RUNTIME_DESTROY_RETRY_NONE);
}

static void test_cleanup_precedes_deferred_wifi_up_replay(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    ts_claw_runtime_wifi_pending_t pending = {0};
    bool has_ip = false;
    void *netif = NULL;
    unsigned pin_count = 0u;
    unsigned rebind_count = 0u;
    fake_init(&fake, TEST_OLD_IP);
    fake.destroy_results[0] = TEST_ERROR;
    fake.destroy_results[1] = TEST_ERROR;
    fake.destroy_results[2] = TEST_ERROR;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_wifi_pending_record(
        &pending, true, (void *)(uintptr_t)2u));
    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    const ts_claw_runtime_completion_t completion =
        take_completion(&control, TEST_ERROR);
    TEST_CHECK(completion.rollback_attempted);
    TEST_CHECK(!completion.rollback_recovered);
    TEST_CHECK(fake.destroy_count == 2u);
    TEST_CHECK(fake.start_count == 0u);
    TEST_CHECK(fake.ml_exists);

    fake.now_ms = 999u;
    TEST_CHECK(!ts_claw_runtime_wifi_pending_take(
        &pending, false, true, &has_ip, &netif));
    TEST_CHECK(!ts_claw_runtime_destroy_retry_due(
        &fake.destroy_retry, fake.now_ms));

    fake.now_ms = 1000u;
    TEST_CHECK(!ts_claw_runtime_wifi_pending_take(
        &pending, false, true, &has_ip, &netif));
    TEST_CHECK(ts_claw_runtime_destroy_retry_due(
        &fake.destroy_retry, fake.now_ms));
    TEST_CHECK(fake_destroy(&fake) == TEST_ERROR);
    TEST_CHECK(fake.destroy_count == 3u);
    TEST_CHECK(fake.ml_exists && pending.pending);

    fake.now_ms = 1999u;
    TEST_CHECK(!ts_claw_runtime_destroy_retry_due(
        &fake.destroy_retry, fake.now_ms));
    fake.now_ms = 2000u;
    TEST_CHECK(ts_claw_runtime_destroy_retry_due(
        &fake.destroy_retry, fake.now_ms));
    TEST_CHECK(fake_destroy(&fake) == ESP_OK);
    ts_claw_runtime_destroy_retry_clear(&fake.destroy_retry);
    TEST_CHECK(!fake.ml_exists && pending.pending);
    TEST_CHECK(fake.destroy_count == 4u);
    TEST_CHECK(fake.start_count == 0u);

    fake.now_ms = 2001u;
    TEST_CHECK(ts_claw_runtime_wifi_pending_take(
        &pending, false, false, &has_ip, &netif));
    if (fake.ml_exists) {
        pin_count++;
        rebind_count++;
    } else {
        TEST_CHECK(fake_start(&fake) == ESP_OK);
    }
    TEST_CHECK(has_ip && netif == (void *)(uintptr_t)2u);
    TEST_CHECK(pin_count == 0u && rebind_count == 0u);
    TEST_CHECK(fake.start_count == 1u);
    TEST_CHECK(fake.ml_exists);
}

static void test_start_failure_rolls_back_once(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.start_results[0] = TEST_ERROR;
    fake.connected_values[0] = true;
    fake.exit_values[0] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    TEST_CHECK(strcmp(fake.calls, "PDNSPDOS") == 0);
    ts_claw_runtime_control_advance(&control, 1u);
    assert_recovered_failure(&fake, &control, TEST_ERROR);
    TEST_CHECK(fake.start_count == 2u);
}

static void test_desired_ip_failure_rolls_back_once(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.set_results[0] = TEST_ERROR;
    fake.connected_values[0] = true;
    fake.exit_values[0] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    TEST_CHECK(strcmp(fake.calls, "PDNPDOS") == 0);
    ts_claw_runtime_control_advance(&control, 1u);
    assert_recovered_failure(&fake, &control, TEST_ERROR);
}

static void test_retire_failure_restores_old_desired_without_unsafe_destroy(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.retire_result = TEST_ERROR;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    const ts_claw_runtime_completion_t completion = take_completion(&control, TEST_ERROR);
    TEST_CHECK(strcmp(fake.calls, "PPO") == 0);
    TEST_CHECK(fake.destroy_count == 0u);
    TEST_CHECK(fake.start_count == 0u);
    TEST_CHECK(fake.desired_ip == TEST_OLD_IP);
    TEST_CHECK(completion.rollback_attempted);
    TEST_CHECK(!completion.rollback_recovered);
}

static void test_connection_observation_failure_rolls_back(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.connected_results[0] = TEST_ERROR;
    fake.connected_values[1] = true;
    fake.exit_values[0] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, 10u);
    TEST_CHECK(strcmp(fake.calls, "PDNSKPDOS") == 0);
    ts_claw_runtime_control_advance(&control, 11u);
    TEST_CHECK(strcmp(fake.calls, "PDNSKPDOSKE") == 0);
    assert_recovered_failure(&fake, &control, TEST_ERROR);
}

static void test_exit_observation_failure_rolls_back(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.connected_values[0] = true;
    fake.connected_values[1] = true;
    fake.exit_results[0] = TEST_ERROR;
    fake.exit_values[1] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, 10u);
    TEST_CHECK(strcmp(fake.calls, "PDNSKEPDOS") == 0);
    ts_claw_runtime_control_advance(&control, 11u);
    TEST_CHECK(strcmp(fake.calls, "PDNSKEPDOSKE") == 0);
    assert_recovered_failure(&fake, &control, TEST_ERROR);
}

static void test_failed_recovery_never_reports_success(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.start_results[0] = TEST_ERROR;
    fake.start_results[1] = TEST_ERROR;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    const ts_claw_runtime_completion_t completion = take_completion(&control, TEST_ERROR);
    TEST_CHECK(completion.rollback_attempted);
    TEST_CHECK(!completion.rollback_recovered);
    TEST_CHECK(completion.selected_exit_node_ip == TEST_OLD_IP);
    TEST_CHECK(fake.desired_ip == TEST_OLD_IP);
}

static void test_failed_recovery_destroy_never_reports_success(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.start_results[0] = TEST_ERROR;
    fake.destroy_results[1] = TEST_ERROR;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    const ts_claw_runtime_completion_t completion = take_completion(&control, TEST_ERROR);
    TEST_CHECK(strcmp(fake.calls, "PDNSPDO") == 0);
    TEST_CHECK(completion.rollback_attempted);
    TEST_CHECK(!completion.rollback_recovered);
    TEST_CHECK(fake.desired_ip == TEST_OLD_IP);
    TEST_CHECK(fake.start_count == 1u);
}

static void test_clock_wrap_before_deadline_is_deterministic(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.connected_values[0] = false;
    fake.connected_values[1] = true;
    fake.exit_values[0] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 10u, UINT64_MAX - 5u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, UINT64_MAX - 1u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    ts_claw_runtime_control_advance(&control, 3u);
    TEST_CHECK(take_completion(&control, ESP_OK).selected_exit_node_ip == TEST_NEW_IP);
}

static void test_timeout_starts_a_fresh_rollback_window(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.connected_values[0] = true;
    fake.exit_values[0] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    fake.now_ms = 1000u;
    ts_claw_runtime_control_advance(&control, 1000u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    TEST_CHECK(strcmp(fake.calls, "PDNSPDOS") == 0);
    TEST_CHECK(fake.connected_count == 0u);

    ts_claw_runtime_control_advance(&control, 1250u);
    TEST_CHECK(strcmp(fake.calls, "PDNSPDOSKE") == 0);
    assert_recovered_failure(&fake, &control, ESP_ERR_TIMEOUT);
}

static void test_blocking_failure_samples_rollback_clock_after_rebuild(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.destroy_results[0] = TEST_ERROR;
    fake.destroy_advance_ms[0] = 900u;
    fake.connected_values[0] = true;
    fake.exit_values[0] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    TEST_CHECK(control.rollback_started_ms == 900u);
    fake.now_ms = 1001u;
    ts_claw_runtime_control_advance(&control, 1001u);
    assert_recovered_failure(&fake, &control, TEST_ERROR);
}

static void test_positive_deadline_precedes_late_connection_success(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.connected_values[0] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 10u, 1000u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, 1010u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    TEST_CHECK(fake.connected_count == 0u);
    TEST_CHECK(strcmp(fake.calls, "PDNSPDOS") == 0);

    ts_claw_runtime_control_advance(&control, 1020u);
    const ts_claw_runtime_completion_t completion =
        take_completion(&control, ESP_ERR_TIMEOUT);
    TEST_CHECK(!completion.rollback_recovered);
    TEST_CHECK(fake.connected_count == 0u);
}

static void test_positive_deadline_precedes_late_exit_success(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.connected_values[0] = true;
    fake.exit_values[0] = false;
    fake.exit_values[1] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 10u, 1000u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, 1009u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    TEST_CHECK(fake.exit_count == 1u);
    ts_claw_runtime_control_advance(&control, 1010u);
    TEST_CHECK(fake.exit_count == 1u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    TEST_CHECK(strstr(fake.calls, "PDOS") != NULL);
}

static void test_timeout_zero_observes_once_then_rolls_back(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, 0u);
    fake.connected_values[0] = false;
    fake.connected_values[1] = true;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, 0u, TEST_NEW_IP, 0u, 100u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, 100u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    TEST_CHECK(strcmp(fake.calls, "PDNSKPDCS") == 0);
    ts_claw_runtime_control_advance(&control, 101u);
    const ts_claw_runtime_completion_t completion = take_completion(&control, ESP_ERR_TIMEOUT);
    TEST_CHECK(completion.rollback_attempted);
    TEST_CHECK(completion.rollback_recovered);
    TEST_CHECK(completion.selected_exit_node_ip == 0u);
    TEST_CHECK(strcmp(fake.calls, "PDNSKPDCSK") == 0);
    TEST_CHECK(fake.connected_count == 2u);
    TEST_CHECK(fake.exit_count == 0u);
}

static void test_second_mutation_is_rejected_without_state_damage(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, 0u, 1000u, 1u) == ESP_ERR_INVALID_STATE);
    TEST_CHECK(ts_claw_runtime_control_begin_reconnect(
                   &control, 1000u, 1u) == ESP_ERR_INVALID_STATE);
    TEST_CHECK(strcmp(fake.calls, "PDNS") == 0);
    TEST_CHECK(fake.desired_ip == TEST_NEW_IP);
}

static void test_set_resets_reconnect_generation_state(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    ts_claw_runtime_control_init(&control, &s_ops, &fake);
    control.reconnect_baseline_ctrl_rx = 999u;
    control.reconnect_disconnect_seen = true;

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1000u, 0u) == ESP_OK);
    TEST_CHECK(control.reconnect_baseline_ctrl_rx == 0u);
    TEST_CHECK(!control.reconnect_disconnect_seen);
}

static void test_null_and_callback_error_boundaries(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    ts_claw_runtime_ops_t incomplete = s_ops;
    fake_init(&fake, TEST_OLD_IP);

    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   NULL, TEST_OLD_IP, TEST_NEW_IP, 1u, 0u) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(ts_claw_runtime_control_begin_reconnect(NULL, 1u, 0u) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(!ts_claw_runtime_control_take_completion(NULL, NULL, NULL));

    incomplete.start = NULL;
    ts_claw_runtime_control_init(&control, &incomplete, &fake);
    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 1u, 0u) == ESP_ERR_INVALID_STATE);

    ts_claw_runtime_control_init(&control, &s_ops, &fake);
    fake.retire_result = TEST_ERROR;
    TEST_CHECK(ts_claw_runtime_control_begin_set(
                   &control, TEST_OLD_IP, TEST_NEW_IP, 10u, 0u) == ESP_OK);
    TEST_CHECK(fake.desired_ip == TEST_OLD_IP);
    TEST_CHECK(strncmp(fake.calls, "P", 1u) == 0);
}

static void test_reconnect_uses_rebind_and_preserves_desired_ip(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.reconnect_connected_values[0] = true;
    fake.reconnect_token_values[0] = 100u;
    fake.reconnect_connected_values[1] = true;
    fake.reconnect_token_values[1] = 101u;
    fake.reconnect_connected_values[2] = false;
    fake.reconnect_token_values[2] = 102u;
    fake.reconnect_connected_values[3] = true;
    fake.reconnect_token_values[3] = 103u;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_reconnect(&control, 100u, 10u) == ESP_OK);
    TEST_CHECK(strcmp(fake.calls, "QR") == 0);
    ts_claw_runtime_control_advance(&control, 20u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    TEST_CHECK(strcmp(fake.calls, "QRQ") == 0);
    ts_claw_runtime_control_advance(&control, 30u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    ts_claw_runtime_control_advance(&control, 40u);
    (void)take_completion(&control, ESP_OK);
    TEST_CHECK(strcmp(fake.calls, "QRQQQ") == 0);
    TEST_CHECK(fake.desired_ip == TEST_OLD_IP);
    TEST_CHECK(fake.destroy_count == 0u);
    TEST_CHECK(fake.start_count == 0u);
}

static void test_reconnect_disconnected_then_new_control_rx_succeeds(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.reconnect_connected_values[0] = true;
    fake.reconnect_token_values[0] = 200u;
    fake.reconnect_connected_values[1] = false;
    fake.reconnect_token_values[1] = 201u;
    fake.reconnect_connected_values[2] = true;
    fake.reconnect_token_values[2] = 202u;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_reconnect(&control, 100u, 0u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, 1u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    ts_claw_runtime_control_advance(&control, 2u);
    (void)take_completion(&control, ESP_OK);
}

static void test_reconnect_zero_baseline_requires_nonzero_new_control_rx(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.reconnect_connected_values[0] = true;
    fake.reconnect_connected_values[1] = false;
    fake.reconnect_connected_values[2] = true;
    fake.reconnect_connected_values[3] = true;
    fake.reconnect_token_values[3] = 1u;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_reconnect(&control, 100u, 0u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, 1u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    ts_claw_runtime_control_advance(&control, 2u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    ts_claw_runtime_control_advance(&control, 3u);
    (void)take_completion(&control, ESP_OK);
}

static void test_reconnect_disconnect_without_new_control_rx_stays_pending(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.reconnect_connected_values[0] = true;
    fake.reconnect_token_values[0] = 400u;
    fake.reconnect_connected_values[1] = false;
    fake.reconnect_token_values[1] = 401u;
    fake.reconnect_connected_values[2] = true;
    fake.reconnect_token_values[2] = 400u;
    fake.reconnect_connected_values[3] = true;
    fake.reconnect_token_values[3] = 0u;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_reconnect(&control, 10u, 0u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, 1u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    ts_claw_runtime_control_advance(&control, 2u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    ts_claw_runtime_control_advance(&control, 3u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    ts_claw_runtime_control_advance(&control, 10u);
    (void)take_completion(&control, ESP_ERR_TIMEOUT);
}

static void test_reconnect_timeout_zero_observes_once_after_rebind(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;
    fake_init(&fake, TEST_OLD_IP);
    fake.reconnect_connected_values[0] = true;
    fake.reconnect_token_values[0] = 500u;
    fake.reconnect_connected_values[1] = true;
    fake.reconnect_token_values[1] = 501u;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);

    TEST_CHECK(ts_claw_runtime_control_begin_reconnect(&control, 0u, 100u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, 100u);
    (void)take_completion(&control, ESP_ERR_TIMEOUT);
    TEST_CHECK(fake.reconnect_count == 2u);
    TEST_CHECK(!control.reconnect_disconnect_seen);
    TEST_CHECK(control.reconnect_baseline_ctrl_rx == 0u);
}

static void test_reconnect_failures_and_timeout(void)
{
    fake_runtime_t fake;
    ts_claw_runtime_control_t control;

    fake_init(&fake, TEST_OLD_IP);
    fake.rebind_result = TEST_ERROR;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);
    TEST_CHECK(ts_claw_runtime_control_begin_reconnect(&control, 10u, 0u) == ESP_OK);
    (void)take_completion(&control, TEST_ERROR);

    fake_init(&fake, TEST_OLD_IP);
    fake.reconnect_connected_values[0] = true;
    fake.reconnect_token_values[0] = 300u;
    fake.reconnect_connected_values[1] = true;
    fake.reconnect_token_values[1] = 301u;
    fake.reconnect_connected_values[2] = true;
    fake.reconnect_token_values[2] = 302u;
    ts_claw_runtime_control_init(&control, &s_ops, &fake);
    TEST_CHECK(ts_claw_runtime_control_begin_reconnect(&control, 10u, 100u) == ESP_OK);
    ts_claw_runtime_control_advance(&control, 109u);
    TEST_CHECK(ts_claw_runtime_control_is_active(&control));
    ts_claw_runtime_control_advance(&control, 110u);
    const ts_claw_runtime_completion_t completion = take_completion(&control, ESP_ERR_TIMEOUT);
    TEST_CHECK(!completion.rollback_attempted);
    TEST_CHECK(fake.desired_ip == TEST_OLD_IP);
    TEST_CHECK(fake.reconnect_count == 2u);
}

int main(void)
{
    test_wifi_pending_defers_up_during_runtime_operation();
    test_wifi_pending_down_supersedes_deferred_up();
    test_set_success_sequence();
    test_clear_success_stops_after_connected();
    test_destroy_failure_rolls_back_once();
    test_two_destroy_timeouts_leave_one_cleanup_retry_until_success();
    test_cleanup_precedes_deferred_wifi_up_replay();
    test_start_failure_rolls_back_once();
    test_desired_ip_failure_rolls_back_once();
    test_retire_failure_restores_old_desired_without_unsafe_destroy();
    test_connection_observation_failure_rolls_back();
    test_exit_observation_failure_rolls_back();
    test_failed_recovery_never_reports_success();
    test_failed_recovery_destroy_never_reports_success();
    test_clock_wrap_before_deadline_is_deterministic();
    test_timeout_starts_a_fresh_rollback_window();
    test_blocking_failure_samples_rollback_clock_after_rebuild();
    test_positive_deadline_precedes_late_connection_success();
    test_positive_deadline_precedes_late_exit_success();
    test_timeout_zero_observes_once_then_rolls_back();
    test_second_mutation_is_rejected_without_state_damage();
    test_set_resets_reconnect_generation_state();
    test_null_and_callback_error_boundaries();
    test_reconnect_uses_rebind_and_preserves_desired_ip();
    test_reconnect_disconnected_then_new_control_rx_succeeds();
    test_reconnect_zero_baseline_requires_nonzero_new_control_rx();
    test_reconnect_disconnect_without_new_control_rx_stays_pending();
    test_reconnect_timeout_zero_observes_once_after_rebind();
    test_reconnect_failures_and_timeout();

    puts("ts_claw_runtime_control: all tests passed");
    return 0;
}
