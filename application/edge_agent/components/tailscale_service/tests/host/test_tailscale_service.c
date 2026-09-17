#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tailscale_service.h"

#define TEST_IP_ALPHA UINT32_C(0x64400101)
#define TEST_IP_ALPINE UINT32_C(0x64400102)
#define TEST_IP_BETA UINT32_C(0x64400103)
#define TEST_IP_OFFLINE UINT32_C(0x64400104)
#define TEST_IP_NON_EXIT UINT32_C(0x64400105)
#define ARRAY_SIZE(values) (sizeof(values) / sizeof((values)[0]))

static int s_failures;
int freertos_test_fail_next_mutex_create;

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            ++s_failures;                                                              \
        }                                                                              \
    } while (0)

typedef struct {
    ts_claw_diagnostics_t diagnostics;
    esp_err_t diagnostics_result;
    ts_claw_peer_t peers[20];
    int peer_count;
    int list_result;
    char persisted[16];
    esp_err_t load_result;
    esp_err_t save_results[4];
    size_t save_result_count;
    size_t save_index;
    esp_err_t apply_results[4];
    ts_claw_runtime_result_t apply_outputs[4];
    size_t apply_result_count;
    size_t apply_index;
    esp_err_t rebind_result;
    int diagnostics_calls;
    int list_calls;
    int load_calls;
    int save_calls;
    int apply_calls;
    int rebind_calls;
    uint32_t applied_ips[4];
    char saved_values[4][16];
    char calls[256];
    pthread_mutex_t block_lock;
    pthread_cond_t block_cond;
    bool block_apply;
    bool apply_entered;
    bool release_apply;
} fake_ctx_t;

static void append_call(fake_ctx_t *ctx, const char *name)
{
    size_t used = strlen(ctx->calls);
    if (used != 0u && used + 1u < sizeof(ctx->calls)) {
        ctx->calls[used++] = ',';
        ctx->calls[used] = '\0';
    }
    if (used < sizeof(ctx->calls)) {
        (void)snprintf(ctx->calls + used, sizeof(ctx->calls) - used, "%s", name);
    }
}

static ts_claw_peer_t make_peer(uint32_t ip, const char *hostname,
                                bool online, bool is_exit_node)
{
    ts_claw_peer_t peer = {
        .vpn_ip = ip,
        .online = online,
        .direct = true,
        .is_exit_node = is_exit_node,
        .derp_region = 1,
    };
    (void)snprintf(peer.hostname, sizeof(peer.hostname), "%s", hostname);
    (void)snprintf(peer.derp_region_name, sizeof(peer.derp_region_name), "test-region");
    return peer;
}

static ts_claw_runtime_result_t runtime_set(uint32_t ip)
{
    ts_claw_runtime_result_t out = {
        .selected_exit_node_ip = ip,
        .exit_state = TS_EXIT_ACTIVE,
    };
    (void)snprintf(out.egress, sizeof(out.egress), "exit");
    return out;
}

static ts_claw_runtime_result_t runtime_clear(void)
{
    ts_claw_runtime_result_t out = {
        .selected_exit_node_ip = 0u,
        .exit_state = TS_EXIT_DISABLED,
    };
    (void)snprintf(out.egress, sizeof(out.egress), "sta");
    return out;
}

static void fake_init(fake_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->diagnostics.status.enabled = true;
    ctx->diagnostics.status.connected = true;
    ctx->diagnostics.status.exit_state = TS_EXIT_DISABLED;
    (void)snprintf(ctx->diagnostics.status.egress,
                   sizeof(ctx->diagnostics.status.egress), "sta");
    ctx->peers[0] = make_peer(TEST_IP_ALPHA, "Alpha.tailnet.example.ts.net", true, true);
    ctx->peers[1] = make_peer(TEST_IP_ALPINE, "alpine.tailnet.example.ts.net", true, true);
    ctx->peers[2] = make_peer(TEST_IP_BETA, "beta.tailnet.example.ts.net", true, true);
    ctx->peers[3] = make_peer(TEST_IP_OFFLINE, "offline.tailnet.example.ts.net", false, true);
    ctx->peers[4] = make_peer(TEST_IP_NON_EXIT, "printer.tailnet.example.ts.net", true, false);
    ctx->peer_count = 5;
    ctx->list_result = INT_MIN;
    ctx->apply_results[0] = ESP_OK;
    ctx->apply_outputs[0] = runtime_set(TEST_IP_BETA);
    ctx->apply_result_count = 1;
    ctx->save_results[0] = ESP_OK;
    ctx->save_result_count = 1;
    CHECK(pthread_mutex_init(&ctx->block_lock, NULL) == 0);
    CHECK(pthread_cond_init(&ctx->block_cond, NULL) == 0);
}

static void fake_deinit(fake_ctx_t *ctx)
{
    CHECK(pthread_cond_destroy(&ctx->block_cond) == 0);
    CHECK(pthread_mutex_destroy(&ctx->block_lock) == 0);
}

static esp_err_t fake_get_diagnostics(ts_claw_diagnostics_t *out, void *opaque)
{
    fake_ctx_t *ctx = opaque;
    ++ctx->diagnostics_calls;
    append_call(ctx, "diag");
    if (ctx->diagnostics_result == ESP_OK) {
        *out = ctx->diagnostics;
    } else {
        memset(out, 0xA5, sizeof(*out));
    }
    return ctx->diagnostics_result;
}

static int fake_list_exit_nodes(ts_claw_peer_t *out, size_t capacity, void *opaque)
{
    fake_ctx_t *ctx = opaque;
    ++ctx->list_calls;
    append_call(ctx, "list");
    int result = ctx->list_result == INT_MIN ? ctx->peer_count : ctx->list_result;
    size_t copy_count = ctx->peer_count > 0 ? (size_t)ctx->peer_count : 0u;
    if (copy_count > capacity) {
        copy_count = capacity;
    }
    if (out != NULL && copy_count > 0u) {
        memcpy(out, ctx->peers, copy_count * sizeof(*out));
    }
    return result;
}

static esp_err_t fake_apply_exit_node(uint32_t ip, ts_claw_runtime_result_t *out,
                                      void *opaque)
{
    fake_ctx_t *ctx = opaque;
    size_t index = ctx->apply_index++;
    ++ctx->apply_calls;
    append_call(ctx, "apply");
    if ((size_t)ctx->apply_calls <= ARRAY_SIZE(ctx->applied_ips)) {
        ctx->applied_ips[ctx->apply_calls - 1] = ip;
    }
    if (ctx->block_apply) {
        CHECK(pthread_mutex_lock(&ctx->block_lock) == 0);
        ctx->apply_entered = true;
        CHECK(pthread_cond_broadcast(&ctx->block_cond) == 0);
        while (!ctx->release_apply) {
            CHECK(pthread_cond_wait(&ctx->block_cond, &ctx->block_lock) == 0);
        }
        CHECK(pthread_mutex_unlock(&ctx->block_lock) == 0);
    }
    if (index >= ctx->apply_result_count) {
        memset(out, 0, sizeof(*out));
        return ESP_FAIL;
    }
    *out = ctx->apply_outputs[index];
    return ctx->apply_results[index];
}

static esp_err_t fake_rebind(void *opaque)
{
    fake_ctx_t *ctx = opaque;
    ++ctx->rebind_calls;
    append_call(ctx, "rebind");
    return ctx->rebind_result;
}

static esp_err_t fake_load_persisted_exit(char out[16], void *opaque)
{
    fake_ctx_t *ctx = opaque;
    ++ctx->load_calls;
    append_call(ctx, "load");
    if (ctx->load_result == ESP_OK) {
        memcpy(out, ctx->persisted, sizeof(ctx->persisted));
    } else {
        memset(out, 'X', 16);
    }
    return ctx->load_result;
}

static esp_err_t fake_save_persisted_exit(const char *ip, void *opaque)
{
    fake_ctx_t *ctx = opaque;
    size_t index = ctx->save_index++;
    esp_err_t result;
    ++ctx->save_calls;
    append_call(ctx, "save");
    if ((size_t)ctx->save_calls <= ARRAY_SIZE(ctx->saved_values)) {
        (void)snprintf(ctx->saved_values[ctx->save_calls - 1],
                       sizeof(ctx->saved_values[0]), "%s", ip);
    }
    result = index < ctx->save_result_count ? ctx->save_results[index] : ESP_FAIL;
    if (result == ESP_OK || result == ESP_ERR_INVALID_RESPONSE) {
        (void)snprintf(ctx->persisted, sizeof(ctx->persisted), "%s", ip);
    }
    return result;
}

static tailscale_service_ops_t fake_ops(fake_ctx_t *ctx)
{
    return (tailscale_service_ops_t) {
        .get_diagnostics = fake_get_diagnostics,
        .list_exit_nodes = fake_list_exit_nodes,
        .apply_exit_node = fake_apply_exit_node,
        .rebind = fake_rebind,
        .load_persisted_exit = fake_load_persisted_exit,
        .save_persisted_exit = fake_save_persisted_exit,
        .ctx = ctx,
    };
}

static tailscale_service_handle_t make_service(fake_ctx_t *ctx)
{
    tailscale_service_handle_t service = NULL;
    tailscale_service_ops_t ops = fake_ops(ctx);
    CHECK(tailscale_service_create(&ops, &service) == ESP_OK);
    CHECK(service != NULL);
    return service;
}

static void expect_set_ok(tailscale_service_handle_t service, const char *selector,
                          uint32_t expected_ip, const char *expected_hostname,
                          fake_ctx_t *ctx)
{
    tailscale_service_result_t result;
    memset(&result, 0xA5, sizeof(result));
    CHECK(tailscale_service_set_exit_node(service, selector, &result) == ESP_OK);
    CHECK(result.ok);
    CHECK(result.error == TAILSCALE_SERVICE_OK);
    CHECK(strcmp(result.selected_ip,
                 expected_ip == TEST_IP_ALPHA ? "100.64.1.1" :
                 expected_ip == TEST_IP_ALPINE ? "100.64.1.2" : "100.64.1.3") == 0);
    CHECK(strcmp(result.selected_hostname, expected_hostname) == 0);
    CHECK(result.exit_state == TS_EXIT_ACTIVE);
    CHECK(strcmp(result.egress, "exit") == 0);
    CHECK(result.persisted);
    CHECK(!result.rollback_attempted);
    CHECK(!result.rollback_recovered);
    CHECK(ctx->applied_ips[0] == expected_ip);
}

static void test_selector_resolution(void)
{
    const struct {
        const char *selector;
        uint32_t ip;
        const char *hostname;
    } cases[] = {
        {"100.64.1.1", TEST_IP_ALPHA, "Alpha.tailnet.example.ts.net"},
        {"BeTa.TAILNET.example.TS.NET", TEST_IP_BETA, "beta.tailnet.example.ts.net"},
        {"BET", TEST_IP_BETA, "beta.tailnet.example.ts.net"},
    };
    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
        fake_ctx_t ctx;
        fake_init(&ctx);
        ctx.apply_outputs[0] = runtime_set(cases[i].ip);
        tailscale_service_handle_t service = make_service(&ctx);
        expect_set_ok(service, cases[i].selector, cases[i].ip, cases[i].hostname, &ctx);
        CHECK(strcmp(ctx.calls, "diag,list,load,apply,save") == 0);
        tailscale_service_delete(service);
        fake_deinit(&ctx);
    }
}

static void expect_rejected(const char *selector, tailscale_service_error_t error,
                            int expected_diagnostics, int expected_lists)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    tailscale_service_handle_t service = make_service(&ctx);
    tailscale_service_result_t result;
    memset(&result, 0xA5, sizeof(result));
    CHECK(tailscale_service_set_exit_node(service, selector, &result) == ESP_OK);
    CHECK(!result.ok);
    CHECK(result.error == error);
    CHECK(result.message[sizeof(result.message) - 1] == '\0');
    CHECK(result.selected_ip[sizeof(result.selected_ip) - 1] == '\0');
    CHECK(result.selected_hostname[sizeof(result.selected_hostname) - 1] == '\0');
    CHECK(result.egress[sizeof(result.egress) - 1] == '\0');
    CHECK(ctx.diagnostics_calls == expected_diagnostics);
    CHECK(ctx.list_calls == expected_lists);
    CHECK(ctx.apply_calls == 0);
    CHECK(ctx.load_calls == 0);
    CHECK(ctx.save_calls == 0);
    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_selector_rejections(void)
{
    expect_rejected("alp", TAILSCALE_SERVICE_AMBIGUOUS_NODE, 1, 1);
    expect_rejected("missing", TAILSCALE_SERVICE_NODE_NOT_FOUND, 1, 1);
    expect_rejected("offline", TAILSCALE_SERVICE_NODE_OFFLINE, 1, 1);
    expect_rejected("printer", TAILSCALE_SERVICE_NOT_EXIT_NODE, 1, 1);
    expect_rejected("100.64.1", TAILSCALE_SERVICE_INVALID_NODE, 0, 0);
    expect_rejected("100.064.1.1", TAILSCALE_SERVICE_INVALID_NODE, 0, 0);
    expect_rejected("", TAILSCALE_SERVICE_INVALID_NODE, 0, 0);
    expect_rejected(NULL, TAILSCALE_SERVICE_INVALID_NODE, 0, 0);

    fake_ctx_t ctx;
    fake_init(&ctx);
    tailscale_service_handle_t service = make_service(&ctx);
    tailscale_service_result_t result;
    CHECK(tailscale_service_set_exit_node(service, "alp", &result) == ESP_OK);
    CHECK(strstr(result.message, "Alpha") != NULL);
    CHECK(strstr(result.message, "100.64.1.1") != NULL);
    CHECK(strstr(result.message, "alpine") != NULL);
    CHECK(strstr(result.message, "100.64.1.2") != NULL);
    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_diagnostic_gates_and_failures(void)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    tailscale_service_handle_t service = make_service(&ctx);
    tailscale_service_result_t result;

    ctx.diagnostics.status.enabled = false;
    CHECK(tailscale_service_set_exit_node(service, "beta", &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_NOT_ENABLED);
    CHECK(strcmp(ctx.calls, "diag") == 0);

    memset(ctx.calls, 0, sizeof(ctx.calls));
    ctx.diagnostics.status.enabled = true;
    ctx.diagnostics.status.connected = false;
    CHECK(tailscale_service_clear_exit_node(service, &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_NOT_CONNECTED);
    CHECK(strcmp(ctx.calls, "diag") == 0);

    memset(ctx.calls, 0, sizeof(ctx.calls));
    ctx.diagnostics_result = ESP_FAIL;
    CHECK(tailscale_service_set_exit_node(service, "beta", &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_NOT_CONNECTED);
    CHECK(strcmp(ctx.calls, "diag") == 0);

    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_runtime_failure_never_saves(void)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    (void)snprintf(ctx.persisted, sizeof(ctx.persisted), "100.64.1.1");
    ctx.apply_results[0] = ESP_ERR_TIMEOUT;
    ctx.apply_outputs[0] = runtime_set(TEST_IP_ALPHA);
    ctx.apply_outputs[0].rollback_attempted = true;
    ctx.apply_outputs[0].rollback_recovered = true;
    tailscale_service_handle_t service = make_service(&ctx);
    tailscale_service_result_t result;
    CHECK(tailscale_service_set_exit_node(service, "beta", &result) == ESP_OK);
    CHECK(!result.ok);
    CHECK(result.error == TAILSCALE_SERVICE_SWITCH_TIMEOUT);
    CHECK(result.rollback_attempted);
    CHECK(result.rollback_recovered);
    CHECK(ctx.save_calls == 0);
    CHECK(strcmp(ctx.calls, "diag,list,load,apply") == 0);

    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_inconsistent_runtime_success_never_saves(void)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    ctx.apply_outputs[0] = runtime_set(TEST_IP_ALPHA);
    tailscale_service_handle_t service = make_service(&ctx);
    tailscale_service_result_t result;
    CHECK(tailscale_service_set_exit_node(service, "beta", &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED);
    CHECK(ctx.save_calls == 0);
    tailscale_service_delete(service);
    fake_deinit(&ctx);

    fake_init(&ctx);
    ctx.apply_outputs[0] = runtime_clear();
    ctx.apply_outputs[0].exit_state = TS_EXIT_FALLBACK;
    service = make_service(&ctx);
    CHECK(tailscale_service_clear_exit_node(service, &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED);
    CHECK(ctx.save_calls == 0);
    tailscale_service_delete(service);
    fake_deinit(&ctx);

    fake_init(&ctx);
    ctx.apply_outputs[0] = runtime_set(TEST_IP_BETA);
    memset(ctx.apply_outputs[0].egress, 'X',
           sizeof(ctx.apply_outputs[0].egress));
    service = make_service(&ctx);
    CHECK(tailscale_service_set_exit_node(service, "beta", &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED);
    CHECK(result.egress[sizeof(result.egress) - 1] == '\0');
    CHECK(ctx.save_calls == 0);
    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_persistence_failure_rolls_back(void)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    (void)snprintf(ctx.persisted, sizeof(ctx.persisted), "100.64.1.1");
    ctx.apply_result_count = 2;
    ctx.apply_outputs[0] = runtime_set(TEST_IP_BETA);
    ctx.apply_outputs[1] = runtime_set(TEST_IP_ALPHA);
    ctx.save_results[0] = ESP_FAIL;
    tailscale_service_handle_t service = make_service(&ctx);
    tailscale_service_result_t result;
    CHECK(tailscale_service_set_exit_node(service, "beta", &result) == ESP_OK);
    CHECK(!result.ok);
    CHECK(result.error == TAILSCALE_SERVICE_PERSISTENCE_FAILED);
    CHECK(result.rollback_attempted);
    CHECK(result.rollback_recovered);
    CHECK(!result.persisted);
    CHECK(strcmp(result.selected_ip, "100.64.1.1") == 0);
    CHECK(ctx.applied_ips[0] == TEST_IP_BETA);
    CHECK(ctx.applied_ips[1] == TEST_IP_ALPHA);
    CHECK(strcmp(ctx.saved_values[0], "100.64.1.3") == 0);
    CHECK(strcmp(ctx.persisted, "100.64.1.1") == 0);
    CHECK(strcmp(ctx.calls, "diag,list,load,apply,save,apply") == 0);

    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_unverified_persistence_reports_reboot_risk(void)
{
    fake_ctx_t ctx;
    tailscale_service_result_t result;
    fake_init(&ctx);
    (void)snprintf(ctx.persisted, sizeof(ctx.persisted), "100.64.1.1");

    ctx.save_results[0] = ESP_ERR_INVALID_RESPONSE;
    ctx.save_result_count = 1u;
    ctx.apply_results[0] = ESP_OK;
    ctx.apply_outputs[0] = runtime_set(TEST_IP_BETA);
    ctx.apply_results[1] = ESP_OK;
    ctx.apply_outputs[1] = runtime_set(TEST_IP_ALPHA);
    ctx.apply_result_count = 2u;
    tailscale_service_handle_t service = make_service(&ctx);

    CHECK(tailscale_service_set_exit_node(service, "beta",
                                          &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_PERSISTENCE_FAILED);
    CHECK(result.rollback_attempted);
    CHECK(result.rollback_recovered);
    CHECK(!result.persisted);
    CHECK(strstr(result.message, "reboot may use a different") != NULL);
    CHECK(strcmp(ctx.persisted, "100.64.1.3") == 0);

    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_persistence_and_rollback_failure(void)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    ctx.persisted[0] = '\0';
    ctx.apply_result_count = 2;
    ctx.apply_outputs[0] = runtime_set(TEST_IP_BETA);
    ctx.apply_results[1] = ESP_FAIL;
    ctx.apply_outputs[1] = runtime_set(TEST_IP_BETA);
    ctx.save_results[0] = ESP_FAIL;
    tailscale_service_handle_t service = make_service(&ctx);
    tailscale_service_result_t result;
    CHECK(tailscale_service_set_exit_node(service, "beta", &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_ROLLBACK_FAILED);
    CHECK(result.rollback_attempted);
    CHECK(!result.rollback_recovered);
    CHECK(ctx.applied_ips[1] == 0u);

    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_invalid_or_unavailable_persistence_aborts_before_apply(void)
{
    const char *old_values[] = {"100.64.01.1", "100.128.1.1", "not-an-ip"};
    for (size_t i = 0; i < ARRAY_SIZE(old_values); ++i) {
        fake_ctx_t ctx;
        fake_init(&ctx);
        (void)snprintf(ctx.persisted, sizeof(ctx.persisted), "%s", old_values[i]);
        tailscale_service_handle_t service = make_service(&ctx);
        tailscale_service_result_t result;
        CHECK(tailscale_service_set_exit_node(service, "beta", &result) == ESP_OK);
        CHECK(result.error == TAILSCALE_SERVICE_PERSISTENCE_FAILED);
        CHECK(ctx.apply_calls == 0);
        CHECK(ctx.save_calls == 0);
        tailscale_service_delete(service);
        fake_deinit(&ctx);
    }

    fake_ctx_t ctx;
    fake_init(&ctx);
    ctx.load_result = ESP_FAIL;
    tailscale_service_handle_t service = make_service(&ctx);
    tailscale_service_result_t result;
    CHECK(tailscale_service_clear_exit_node(service, &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_PERSISTENCE_FAILED);
    CHECK(ctx.apply_calls == 0);
    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_clear_only_persists_proven_sta_result(void)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    (void)snprintf(ctx.persisted, sizeof(ctx.persisted), "100.64.1.1");
    ctx.apply_outputs[0] = runtime_clear();
    tailscale_service_handle_t service = make_service(&ctx);
    tailscale_service_result_t result;
    CHECK(tailscale_service_clear_exit_node(service, &result) == ESP_OK);
    CHECK(result.ok);
    CHECK(result.persisted);
    CHECK(strcmp(ctx.saved_values[0], "") == 0);
    CHECK(ctx.applied_ips[0] == 0u);
    CHECK(strcmp(ctx.calls, "diag,load,apply,save") == 0);
    tailscale_service_delete(service);
    fake_deinit(&ctx);

    fake_init(&ctx);
    ctx.apply_outputs[0] = runtime_clear();
    (void)snprintf(ctx.apply_outputs[0].egress,
                   sizeof(ctx.apply_outputs[0].egress), "exit");
    service = make_service(&ctx);
    CHECK(tailscale_service_clear_exit_node(service, &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED);
    CHECK(ctx.save_calls == 0);
    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_clear_save_failure_restores_old_selection(void)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    (void)snprintf(ctx.persisted, sizeof(ctx.persisted), "100.64.1.1");
    ctx.apply_result_count = 2;
    ctx.apply_outputs[0] = runtime_clear();
    ctx.apply_outputs[1] = runtime_set(TEST_IP_ALPHA);
    ctx.save_results[0] = ESP_FAIL;
    tailscale_service_handle_t service = make_service(&ctx);
    tailscale_service_result_t result;
    CHECK(tailscale_service_clear_exit_node(service, &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_PERSISTENCE_FAILED);
    CHECK(result.rollback_attempted);
    CHECK(result.rollback_recovered);
    CHECK(ctx.applied_ips[1] == TEST_IP_ALPHA);
    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_reconnect_never_touches_persistence_or_selector(void)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    ctx.diagnostics.status.connected = false;
    tailscale_service_handle_t service = make_service(&ctx);
    tailscale_service_result_t result;
    CHECK(tailscale_service_reconnect(service, &result) == ESP_OK);
    CHECK(result.ok);
    CHECK(result.error == TAILSCALE_SERVICE_OK);
    CHECK(!result.persisted);
    CHECK(ctx.rebind_calls == 1);
    CHECK(ctx.load_calls == 0);
    CHECK(ctx.save_calls == 0);
    CHECK(ctx.apply_calls == 0);
    CHECK(strcmp(ctx.calls, "diag,rebind") == 0);

    memset(ctx.calls, 0, sizeof(ctx.calls));
    ctx.rebind_result = ESP_FAIL;
    CHECK(tailscale_service_reconnect(service, &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_RECONNECT_FAILED);
    CHECK(!result.persisted);
    CHECK(ctx.rebind_calls == 2);

    memset(ctx.calls, 0, sizeof(ctx.calls));
    ctx.diagnostics.status.enabled = false;
    CHECK(tailscale_service_reconnect(service, &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_NOT_ENABLED);
    CHECK(!result.persisted);
    CHECK(strcmp(ctx.calls, "diag") == 0);

    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

typedef struct {
    tailscale_service_handle_t service;
    tailscale_service_result_t result;
    esp_err_t err;
} mutation_thread_t;

static void *run_set_thread(void *opaque)
{
    mutation_thread_t *thread = opaque;
    thread->err = tailscale_service_set_exit_node(thread->service, "beta", &thread->result);
    return NULL;
}

static void test_concurrent_mutation_is_busy_without_ops(void)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    ctx.block_apply = true;
    tailscale_service_handle_t service = make_service(&ctx);
    mutation_thread_t thread_ctx = {.service = service};
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, run_set_thread, &thread_ctx) == 0);
    CHECK(pthread_mutex_lock(&ctx.block_lock) == 0);
    while (!ctx.apply_entered) {
        CHECK(pthread_cond_wait(&ctx.block_cond, &ctx.block_lock) == 0);
    }
    CHECK(pthread_mutex_unlock(&ctx.block_lock) == 0);

    int diagnostics_before = ctx.diagnostics_calls;
    int apply_before = ctx.apply_calls;
    tailscale_service_result_t result;
    CHECK(tailscale_service_clear_exit_node(service, &result) == ESP_OK);
    CHECK(result.error == TAILSCALE_SERVICE_BUSY);
    CHECK(ctx.diagnostics_calls == diagnostics_before);
    CHECK(ctx.apply_calls == apply_before);
    CHECK(ctx.load_calls == 1);
    CHECK(ctx.save_calls == 0);

    CHECK(pthread_mutex_lock(&ctx.block_lock) == 0);
    ctx.release_apply = true;
    CHECK(pthread_cond_broadcast(&ctx.block_cond) == 0);
    CHECK(pthread_mutex_unlock(&ctx.block_lock) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(thread_ctx.err == ESP_OK);
    CHECK(thread_ctx.result.ok);

    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_read_wrappers_bound_and_initialize_outputs(void)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    tailscale_service_handle_t service = make_service(&ctx);
    ts_claw_diagnostics_t diagnostics;
    memset(&diagnostics, 0xA5, sizeof(diagnostics));
    ctx.diagnostics_result = ESP_FAIL;
    CHECK(tailscale_service_get_diagnostics(service, &diagnostics) == ESP_FAIL);
    const unsigned char *bytes = (const unsigned char *)&diagnostics;
    for (size_t i = 0; i < sizeof(diagnostics); ++i) {
        CHECK(bytes[i] == 0u);
    }

    ts_claw_peer_t peers[2];
    memset(peers, 0xA5, sizeof(peers));
    ctx.list_result = ESP_FAIL;
    CHECK(tailscale_service_list_exit_nodes(service, peers, ARRAY_SIZE(peers)) < 0);
    CHECK(peers[0].hostname[0] == '\0');
    CHECK(peers[1].hostname[0] == '\0');

    ctx.list_result = 20;
    memset(peers, 0xA5, sizeof(peers));
    CHECK(tailscale_service_list_exit_nodes(service, peers, ARRAY_SIZE(peers)) < 0);
    CHECK(peers[0].hostname[0] == '\0');

    memset(&ctx.diagnostics, 'X', sizeof(ctx.diagnostics));
    ctx.diagnostics.derp_rtt_count = SIZE_MAX;
    ctx.diagnostics_result = ESP_OK;
    CHECK(tailscale_service_get_diagnostics(service, &diagnostics) == ESP_OK);
    CHECK(diagnostics.status.egress[sizeof(diagnostics.status.egress) - 1] == '\0');
    CHECK(diagnostics.status.last_error[sizeof(diagnostics.status.last_error) - 1] == '\0');
    CHECK(diagnostics.hostname[sizeof(diagnostics.hostname) - 1] == '\0');
    CHECK(diagnostics.derp_active_name[sizeof(diagnostics.derp_active_name) - 1] == '\0');
    CHECK(diagnostics.derp_default_name[sizeof(diagnostics.derp_default_name) - 1] == '\0');
    CHECK(diagnostics.derp_rtt_count == TS_CLAW_MAX_DERP_RTTS);
    for (size_t i = 0u; i < diagnostics.derp_rtt_count; ++i) {
        CHECK(diagnostics.derp_rtts[i].region_name[
                  sizeof(diagnostics.derp_rtts[i].region_name) - 1] == '\0');
    }

    ctx.list_result = INT_MIN;
    CHECK(tailscale_service_list_exit_nodes(service, peers, ARRAY_SIZE(peers)) == 2);
    CHECK(strcmp(peers[0].hostname, "Alpha.tailnet.example.ts.net") == 0);
    CHECK(strcmp(peers[1].hostname, "alpine.tailnet.example.ts.net") == 0);

    CHECK(tailscale_service_get_diagnostics(NULL, &diagnostics) == ESP_ERR_INVALID_ARG);
    CHECK(tailscale_service_get_diagnostics(service, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(tailscale_service_list_exit_nodes(NULL, peers, 2) == -ESP_ERR_INVALID_ARG);
    CHECK(tailscale_service_list_exit_nodes(service, NULL, 1) == -ESP_ERR_INVALID_ARG);

    tailscale_service_delete(service);
    fake_deinit(&ctx);
}

static void test_create_and_null_arguments(void)
{
    fake_ctx_t ctx;
    fake_init(&ctx);
    tailscale_service_ops_t ops = fake_ops(&ctx);
    tailscale_service_handle_t service = (tailscale_service_handle_t)(uintptr_t)1u;
    CHECK(tailscale_service_create(NULL, &service) == ESP_ERR_INVALID_ARG);
    CHECK(service == NULL);
    CHECK(tailscale_service_create(&ops, NULL) == ESP_ERR_INVALID_ARG);

    tailscale_service_ops_t missing = ops;
    missing.rebind = NULL;
    CHECK(tailscale_service_create(&missing, &service) == ESP_ERR_INVALID_ARG);
    CHECK(service == NULL);

    freertos_test_fail_next_mutex_create = 1;
    CHECK(tailscale_service_create(&ops, &service) == ESP_ERR_NO_MEM);
    CHECK(service == NULL);

    tailscale_service_result_t result;
    memset(&result, 0xA5, sizeof(result));
    CHECK(tailscale_service_set_exit_node(NULL, "beta", &result) == ESP_ERR_INVALID_ARG);
    CHECK(!result.ok);
    CHECK(result.message[sizeof(result.message) - 1] == '\0');
    CHECK(tailscale_service_clear_exit_node(NULL, &result) == ESP_ERR_INVALID_ARG);
    CHECK(tailscale_service_reconnect(NULL, &result) == ESP_ERR_INVALID_ARG);
    CHECK(tailscale_service_set_exit_node(NULL, "beta", NULL) == ESP_ERR_INVALID_ARG);

    fake_deinit(&ctx);
}

static void test_error_names_are_stable(void)
{
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_OK), "ok") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_NOT_ENABLED), "not_enabled") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_NOT_CONNECTED), "not_connected") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_INVALID_NODE), "invalid_node") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_AMBIGUOUS_NODE), "ambiguous_node") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_NODE_NOT_FOUND), "node_not_found") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_NODE_OFFLINE), "node_offline") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_NOT_EXIT_NODE), "not_exit_node") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_SWITCH_TIMEOUT), "switch_timeout") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_RUNTIME_APPLY_FAILED), "runtime_apply_failed") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_PERSISTENCE_FAILED), "persistence_failed") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_ROLLBACK_FAILED), "rollback_failed") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_RECONNECT_FAILED), "reconnect_failed") == 0);
    CHECK(strcmp(tailscale_service_error_name(TAILSCALE_SERVICE_BUSY), "busy") == 0);
    CHECK(strcmp(tailscale_service_error_name((tailscale_service_error_t)999), "unknown") == 0);
}

int main(void)
{
    test_selector_resolution();
    test_selector_rejections();
    test_diagnostic_gates_and_failures();
    test_runtime_failure_never_saves();
    test_inconsistent_runtime_success_never_saves();
    test_persistence_failure_rolls_back();
    test_unverified_persistence_reports_reboot_risk();
    test_persistence_and_rollback_failure();
    test_invalid_or_unavailable_persistence_aborts_before_apply();
    test_clear_only_persists_proven_sta_result();
    test_clear_save_failure_restores_old_selection();
    test_reconnect_never_touches_persistence_or_selector();
    test_concurrent_mutation_is_busy_without_ops();
    test_read_wrappers_bound_and_initialize_outputs();
    test_create_and_null_arguments();
    test_error_names_are_stable();

    if (s_failures != 0) {
        fprintf(stderr, "%d checks failed\n", s_failures);
        return 1;
    }
    puts("tailscale_service host tests passed");
    return 0;
}
