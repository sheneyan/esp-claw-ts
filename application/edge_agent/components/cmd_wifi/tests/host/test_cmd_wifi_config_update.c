#include "cmd_wifi_config_update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    app_config_t persisted;
    int allocation_calls;
    int allocation_fail_call;
    int release_calls;
    int load_calls;
    esp_err_t load_result;
    int validate_calls;
    esp_err_t validate_result;
    bool validation_message_from_config;
    int save_calls;
    esp_err_t save_result;
    int apply_calls;
    int apply_result;
    int saved_calls;
    int sequence;
    int save_sequence;
    int apply_sequence;
    bool targeted_write_after_load;
    size_t changed_count;
    bool changed_ssid;
    bool changed_password;
    bool changed_exit;
    char applied_ssid[APP_CONFIG_STR_LEN];
} fake_context_t;

static int s_failures;
static fake_context_t *s_context;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                __FILE__, __LINE__, #condition); \
        ++s_failures; \
    } \
} while (0)

void *cmd_wifi_config_update_test_allocate(size_t size)
{
    fake_context_t *ctx = s_context;
    ++ctx->allocation_calls;
    if (ctx->allocation_calls == ctx->allocation_fail_call) {
        return NULL;
    }
    return calloc(1u, size);
}

void cmd_wifi_config_update_test_release(void *ptr)
{
    fake_context_t *ctx = s_context;
    ++ctx->release_calls;
    free(ptr);
}

esp_err_t app_config_load(app_config_t *config)
{
    fake_context_t *ctx = s_context;
    ++ctx->load_calls;
    if (ctx->load_result != ESP_OK) {
        return ctx->load_result;
    }
    *config = ctx->persisted;
    if (ctx->targeted_write_after_load) {
        (void)snprintf(ctx->persisted.tailscale_exit_node,
                       sizeof(ctx->persisted.tailscale_exit_node),
                       "100.64.0.2");
    }
    return ESP_OK;
}

esp_err_t app_config_validate_wifi(const app_config_t *config,
                                   const char **message)
{
    fake_context_t *ctx = s_context;
    ++ctx->validate_calls;
    if (ctx->validate_result != ESP_OK) {
        *message = ctx->validation_message_from_config ? config->wifi_ssid :
                                                        "invalid_wifi";
    }
    return ctx->validate_result;
}

esp_err_t app_config_save_changed(const app_config_t *before,
                                  const app_config_t *after)
{
    fake_context_t *ctx = s_context;
    ++ctx->save_calls;
    ctx->save_sequence = ++ctx->sequence;
    ctx->changed_ssid = memcmp(before->wifi_ssid, after->wifi_ssid,
                               sizeof(before->wifi_ssid)) != 0;
    ctx->changed_password =
        memcmp(before->wifi_password, after->wifi_password,
               sizeof(before->wifi_password)) != 0;
    ctx->changed_exit =
        memcmp(before->tailscale_exit_node, after->tailscale_exit_node,
               sizeof(before->tailscale_exit_node)) != 0;
    ctx->changed_count = (size_t)ctx->changed_ssid +
                         (size_t)ctx->changed_password +
                         (size_t)ctx->changed_exit;
    if (ctx->save_result != ESP_OK) {
        return ctx->save_result;
    }
    if (ctx->changed_ssid) {
        (void)snprintf(ctx->persisted.wifi_ssid,
                       sizeof(ctx->persisted.wifi_ssid), "%s",
                       after->wifi_ssid);
    }
    if (ctx->changed_password) {
        (void)snprintf(ctx->persisted.wifi_password,
                       sizeof(ctx->persisted.wifi_password), "%s",
                       after->wifi_password);
    }
    if (ctx->changed_exit) {
        (void)snprintf(ctx->persisted.tailscale_exit_node,
                       sizeof(ctx->persisted.tailscale_exit_node), "%s",
                       after->tailscale_exit_node);
    }
    return ESP_OK;
}

static int fake_apply(const app_config_t *config, void *opaque)
{
    fake_context_t *ctx = opaque;
    ++ctx->apply_calls;
    ctx->apply_sequence = ++ctx->sequence;
    (void)snprintf(ctx->applied_ssid, sizeof(ctx->applied_ssid), "%s",
                   config->wifi_ssid);
    return ctx->apply_result;
}

static void fake_saved(const app_config_t *config, void *opaque)
{
    fake_context_t *ctx = opaque;
    ++ctx->saved_calls;
    CHECK(strcmp(config->wifi_ssid, "new-wifi") == 0);
}

static void init_context(fake_context_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    s_context = ctx;
    (void)snprintf(ctx->persisted.wifi_ssid,
                   sizeof(ctx->persisted.wifi_ssid), "old-wifi");
    (void)snprintf(ctx->persisted.wifi_password,
                   sizeof(ctx->persisted.wifi_password), "old-password");
    (void)snprintf(ctx->persisted.tailscale_exit_node,
                   sizeof(ctx->persisted.tailscale_exit_node), "100.64.0.1");
}

static void test_wifi_only_update_does_not_rewrite_targeted_exit(void)
{
    fake_context_t ctx;
    cmd_wifi_config_update_result_t result;

    init_context(&ctx);
    ctx.targeted_write_after_load = true;

    CHECK(cmd_wifi_config_update("new-wifi", "new-password", true, false,
                                 fake_apply, fake_saved, &ctx, &result) ==
          ESP_OK);
    CHECK(result.outcome == CMD_WIFI_CONFIG_UPDATE_COMPLETE);
    CHECK(ctx.changed_count == 2u);
    CHECK(ctx.changed_ssid);
    CHECK(ctx.changed_password);
    CHECK(!ctx.changed_exit);
    CHECK(strcmp(ctx.persisted.tailscale_exit_node, "100.64.0.2") == 0);
    CHECK(strcmp(ctx.persisted.wifi_ssid, "new-wifi") == 0);
    CHECK(strcmp(ctx.persisted.wifi_password, "new-password") == 0);
    CHECK(ctx.saved_calls == 1);
    CHECK(ctx.release_calls == 2);
}

static void test_apply_uses_modified_snapshot_after_persistence(void)
{
    fake_context_t ctx;
    cmd_wifi_config_update_result_t result;

    init_context(&ctx);

    CHECK(cmd_wifi_config_update("new-wifi", NULL, false, true,
                                 fake_apply, fake_saved, &ctx, &result) ==
          ESP_OK);
    CHECK(result.outcome == CMD_WIFI_CONFIG_UPDATE_COMPLETE);
    CHECK(ctx.save_calls == 1);
    CHECK(ctx.apply_calls == 1);
    CHECK(ctx.save_sequence < ctx.apply_sequence);
    CHECK(strcmp(ctx.applied_ssid, "new-wifi") == 0);
    CHECK(strcmp(ctx.persisted.wifi_password, "old-password") == 0);
    CHECK(ctx.saved_calls == 0);
    CHECK(ctx.release_calls == 2);
}

static void test_save_failure_prevents_apply(void)
{
    fake_context_t ctx;
    cmd_wifi_config_update_result_t result;

    init_context(&ctx);
    ctx.save_result = ESP_FAIL;

    CHECK(cmd_wifi_config_update("new-wifi", "new-password", true, true,
                                 fake_apply, fake_saved, &ctx, &result) ==
          ESP_FAIL);
    CHECK(result.outcome == CMD_WIFI_CONFIG_UPDATE_SAVE_FAILED);
    CHECK(ctx.apply_calls == 0);
    CHECK(ctx.saved_calls == 0);
    CHECK(ctx.release_calls == 2);
}

static void test_apply_failure_is_returned_after_successful_persistence(void)
{
    fake_context_t ctx;
    cmd_wifi_config_update_result_t result;

    init_context(&ctx);
    ctx.apply_result = 7;

    CHECK(cmd_wifi_config_update("new-wifi", NULL, false, true,
                                 fake_apply, fake_saved, &ctx, &result) ==
          ESP_OK);
    CHECK(result.outcome == CMD_WIFI_CONFIG_UPDATE_APPLY_FAILED);
    CHECK(result.apply_result == 7);
    CHECK(ctx.save_calls == 1);
    CHECK(ctx.apply_calls == 1);
    CHECK(ctx.save_sequence < ctx.apply_sequence);
    CHECK(ctx.release_calls == 2);
}

static void test_allocation_and_early_errors_release_owned_snapshots(void)
{
    fake_context_t ctx;
    cmd_wifi_config_update_result_t result;

    init_context(&ctx);
    ctx.allocation_fail_call = 1;
    CHECK(cmd_wifi_config_update("new-wifi", NULL, false, false,
                                 fake_apply, fake_saved, &ctx, &result) ==
          ESP_ERR_NO_MEM);
    CHECK(result.outcome == CMD_WIFI_CONFIG_UPDATE_ALLOCATION_FAILED);
    CHECK(ctx.release_calls == 0);

    init_context(&ctx);
    ctx.allocation_fail_call = 2;
    CHECK(cmd_wifi_config_update("new-wifi", NULL, false, false,
                                 fake_apply, fake_saved, &ctx, &result) ==
          ESP_ERR_NO_MEM);
    CHECK(result.outcome == CMD_WIFI_CONFIG_UPDATE_ALLOCATION_FAILED);
    CHECK(ctx.release_calls == 1);

    init_context(&ctx);
    ctx.load_result = ESP_FAIL;
    CHECK(cmd_wifi_config_update("new-wifi", NULL, false, false,
                                 fake_apply, fake_saved, &ctx, &result) ==
          ESP_FAIL);
    CHECK(result.outcome == CMD_WIFI_CONFIG_UPDATE_LOAD_FAILED);
    CHECK(ctx.release_calls == 2);

    init_context(&ctx);
    CHECK(cmd_wifi_config_update(NULL, NULL, false, false,
                                 fake_apply, fake_saved, &ctx, &result) ==
          ESP_ERR_INVALID_ARG);
    CHECK(result.outcome == CMD_WIFI_CONFIG_UPDATE_MISSING_SSID);
    CHECK(ctx.load_calls == 1);
    CHECK(ctx.release_calls == 2);

    init_context(&ctx);
    ctx.validate_result = ESP_ERR_INVALID_ARG;
    CHECK(cmd_wifi_config_update("new-wifi", NULL, false, false,
                                 fake_apply, fake_saved, &ctx, &result) ==
          ESP_ERR_INVALID_ARG);
    CHECK(result.outcome == CMD_WIFI_CONFIG_UPDATE_VALIDATION_FAILED);
    CHECK(strcmp(result.validation_message, "invalid_wifi") == 0);
    CHECK(ctx.save_calls == 0);
    CHECK(ctx.release_calls == 2);
}

static void test_invalid_api_arguments_are_safe(void)
{
    fake_context_t ctx;
    cmd_wifi_config_update_result_t result;

    init_context(&ctx);
    CHECK(cmd_wifi_config_update("new-wifi", NULL, false, false,
                                 fake_apply, fake_saved, &ctx, NULL) ==
          ESP_ERR_INVALID_ARG);
    CHECK(cmd_wifi_config_update("new-wifi", NULL, false, true,
                                 NULL, fake_saved, &ctx, &result) ==
          ESP_ERR_INVALID_ARG);
    CHECK(result.outcome == CMD_WIFI_CONFIG_UPDATE_INVALID_ARGUMENT);
    CHECK(ctx.allocation_calls == 0);
}

static void test_validation_message_is_copied_before_snapshots_are_released(void)
{
    fake_context_t ctx;
    cmd_wifi_config_update_result_t result;

    init_context(&ctx);
    ctx.validate_result = ESP_ERR_INVALID_ARG;
    ctx.validation_message_from_config = true;

    CHECK(cmd_wifi_config_update("new-wifi", NULL, false, false,
                                 fake_apply, fake_saved, &ctx, &result) ==
          ESP_ERR_INVALID_ARG);
    CHECK(result.outcome == CMD_WIFI_CONFIG_UPDATE_VALIDATION_FAILED);
    CHECK(sizeof(result.validation_message) > sizeof(char *));
    CHECK(strcmp(result.validation_message, "new-wifi") == 0);
}

int main(void)
{
    test_wifi_only_update_does_not_rewrite_targeted_exit();
    test_apply_uses_modified_snapshot_after_persistence();
    test_save_failure_prevents_apply();
    test_apply_failure_is_returned_after_successful_persistence();
    test_allocation_and_early_errors_release_owned_snapshots();
    test_invalid_api_arguments_are_safe();
    test_validation_message_is_copied_before_snapshots_are_released();

    if (s_failures != 0) {
        fprintf(stderr, "%d test checks failed\n", s_failures);
        return 1;
    }
    puts("cmd_wifi config update tests passed");
    return 0;
}
