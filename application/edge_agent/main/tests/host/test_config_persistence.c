#include "config_persistence.h"

#include <stdio.h>
#include <string.h>

#include "app_claw.h"
#include "app_config.h"

static int s_failures;
static int s_save_changed_calls;
static int s_runtime_update_calls;
static esp_err_t s_save_result;
static char s_persisted_exit[16];
static char s_persisted_wifi[64];
static char s_runtime_wifi[64];

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
        ++s_failures; \
    } \
} while (0)

static void reset_fake(void)
{
    s_save_changed_calls = 0;
    s_runtime_update_calls = 0;
    s_save_result = ESP_OK;
    (void)snprintf(s_persisted_exit, sizeof(s_persisted_exit), "100.64.0.2");
    (void)snprintf(s_persisted_wifi, sizeof(s_persisted_wifi), "old-wifi");
    s_runtime_wifi[0] = '\0';
}

esp_err_t app_config_validate_wifi(const app_config_t *config,
                                   const char **message)
{
    if (message) {
        *message = NULL;
    }
    return config ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t app_config_save_changed(const app_config_t *before,
                                  const app_config_t *after)
{
    ++s_save_changed_calls;
    CHECK(before != NULL);
    CHECK(after != NULL);
    if (s_save_result != ESP_OK) {
        return s_save_result;
    }
    if (memcmp(before->wifi_ssid, after->wifi_ssid,
               sizeof(before->wifi_ssid)) != 0) {
        (void)snprintf(s_persisted_wifi, sizeof(s_persisted_wifi), "%s",
                       after->wifi_ssid);
    }
    if (memcmp(before->tailscale_exit_node, after->tailscale_exit_node,
               sizeof(before->tailscale_exit_node)) != 0) {
        (void)snprintf(s_persisted_exit, sizeof(s_persisted_exit), "%s",
                       after->tailscale_exit_node);
    }
    return ESP_OK;
}

void app_config_to_claw(const app_config_t *config, app_claw_config_t *out)
{
    (void)snprintf(out->source_wifi, sizeof(out->source_wifi), "%s",
                   config->wifi_ssid);
}

esp_err_t app_claw_update_config(const app_claw_config_t *config)
{
    ++s_runtime_update_calls;
    (void)snprintf(s_runtime_wifi, sizeof(s_runtime_wifi), "%s",
                   config->source_wifi);
    return ESP_OK;
}

static app_config_t snapshot(const char *wifi, const char *exit_node)
{
    app_config_t config = {0};
    (void)snprintf(config.wifi_ssid, sizeof(config.wifi_ssid), "%s", wifi);
    (void)snprintf(config.tailscale_exit_node,
                   sizeof(config.tailscale_exit_node), "%s", exit_node);
    return config;
}

static void test_wifi_patch_preserves_newer_targeted_exit(void)
{
    app_config_t before = snapshot("old-wifi", "100.64.0.1");
    app_config_t after = before;
    (void)snprintf(after.wifi_ssid, sizeof(after.wifi_ssid), "patched-wifi");

    reset_fake();
    CHECK(main_save_config_changes(&before, &after) == ESP_OK);
    CHECK(s_save_changed_calls == 1);
    CHECK(strcmp(s_persisted_exit, "100.64.0.2") == 0);
    CHECK(strcmp(s_persisted_wifi, "patched-wifi") == 0);
    CHECK(s_runtime_update_calls == 1);
    CHECK(strcmp(s_runtime_wifi, "patched-wifi") == 0);
}

static void test_explicit_exit_patch_is_last_writer(void)
{
    app_config_t before = snapshot("old-wifi", "100.64.0.1");
    app_config_t after = before;
    (void)snprintf(after.tailscale_exit_node,
                   sizeof(after.tailscale_exit_node), "100.64.0.3");

    reset_fake();
    CHECK(main_save_config_changes(&before, &after) == ESP_OK);
    CHECK(strcmp(s_persisted_exit, "100.64.0.3") == 0);
}

static void test_persistence_failure_skips_runtime_update(void)
{
    app_config_t before = snapshot("old-wifi", "100.64.0.1");
    app_config_t after = snapshot("patched-wifi", "100.64.0.1");

    reset_fake();
    s_save_result = ESP_FAIL;
    CHECK(main_save_config_changes(&before, &after) == ESP_FAIL);
    CHECK(s_runtime_update_calls == 0);
    CHECK(main_save_config_changes(NULL, &after) == ESP_ERR_INVALID_ARG);
    CHECK(main_save_config_changes(&before, NULL) == ESP_ERR_INVALID_ARG);
}

int main(void)
{
    test_wifi_patch_preserves_newer_targeted_exit();
    test_explicit_exit_patch_is_last_writer();
    test_persistence_failure_skips_runtime_update();

    if (s_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", s_failures);
        return 1;
    }
    puts("config persistence tests passed");
    return 0;
}
