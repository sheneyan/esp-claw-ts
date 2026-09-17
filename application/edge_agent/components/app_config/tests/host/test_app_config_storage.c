#include "app_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "settings_store.h"

static int s_failures;
static int s_batch_calls;
static int s_single_set_calls;
static int s_verified_set_calls;
static int s_commit_calls;
static size_t s_last_batch_count;
static char s_last_single_key[16];
static char s_last_single_value[16];
static const char *s_fail_key;
static esp_err_t s_verified_result;
static settings_store_write_state_t s_verified_state;
static char s_committed_exit[16];
static char s_committed_wifi[APP_CONFIG_STR_LEN];

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
        ++s_failures; \
    } \
} while (0)

static void reset_fake(void)
{
    s_batch_calls = 0;
    s_single_set_calls = 0;
    s_verified_set_calls = 0;
    s_commit_calls = 0;
    s_last_batch_count = 0u;
    s_last_single_key[0] = '\0';
    s_last_single_value[0] = '\0';
    s_fail_key = NULL;
    s_verified_result = ESP_OK;
    s_verified_state = SETTINGS_STORE_WRITE_APPLIED;
    (void)snprintf(s_committed_exit, sizeof(s_committed_exit), "100.64.0.1");
    (void)snprintf(s_committed_wifi, sizeof(s_committed_wifi), "old-wifi");
}

esp_err_t settings_store_init(const settings_store_config_t *config)
{
    return config != NULL ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t settings_store_get_string(const char *key, char *buf,
                                    size_t buf_size, const char *default_value)
{
    (void)key;
    if (buf == NULL || buf_size == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)snprintf(buf, buf_size, "%s", default_value ? default_value : "");
    return ESP_OK;
}

esp_err_t settings_store_has_key(const char *key, bool *exists)
{
    (void)key;
    if (exists == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *exists = true;
    return ESP_OK;
}

esp_err_t settings_store_set_string(const char *key, const char *value)
{
    (void)key;
    (void)value;
    ++s_single_set_calls;
    return ESP_OK;
}

esp_err_t settings_store_set_strings_batch(
    const settings_store_string_entry_t *entries, size_t count)
{
    ++s_batch_calls;
    s_last_batch_count = count;
    if (count == 1u) {
        (void)snprintf(s_last_single_key, sizeof(s_last_single_key), "%s",
                       entries[0].key);
        (void)snprintf(s_last_single_value, sizeof(s_last_single_value), "%s",
                       entries[0].value ? entries[0].value : "");
    }
    for (size_t index = 0u; index < count; ++index) {
        if (s_fail_key != NULL && strcmp(entries[index].key, s_fail_key) == 0) {
            return ESP_FAIL;
        }
        if (strcmp(entries[index].key, "ts_exit_node") == 0) {
            (void)snprintf(s_committed_exit, sizeof(s_committed_exit), "%s",
                           entries[index].value);
        } else if (strcmp(entries[index].key, "wifi_ssid") == 0) {
            (void)snprintf(s_committed_wifi, sizeof(s_committed_wifi), "%s",
                           entries[index].value);
        }
    }
    return ESP_OK;
}

esp_err_t settings_store_set_string_verified(
    const char *key, const char *value, settings_store_write_state_t *out_state)
{
    ++s_verified_set_calls;
    (void)snprintf(s_last_single_key, sizeof(s_last_single_key), "%s", key);
    (void)snprintf(s_last_single_value, sizeof(s_last_single_value), "%s", value);
    *out_state = s_verified_state;
    if (s_verified_result == ESP_OK) {
        (void)snprintf(s_committed_exit, sizeof(s_committed_exit), "%s", value);
    }
    return s_verified_result;
}

esp_err_t settings_store_erase_key(const char *key)
{
    (void)key;
    return ESP_OK;
}

esp_err_t settings_store_commit(void)
{
    ++s_commit_calls;
    return ESP_OK;
}

static app_config_t make_config(void)
{
    app_config_t config;
    app_config_load_defaults(&config);
    (void)snprintf(config.wifi_ssid, sizeof(config.wifi_ssid), "new-wifi");
    (void)snprintf(config.tailscale_exit_node,
                   sizeof(config.tailscale_exit_node), "100.64.0.2");
    return config;
}

static void test_full_save_is_one_serialized_batch(void)
{
    app_config_t config = make_config();

    reset_fake();
    CHECK(app_config_save(&config) == ESP_OK);
    CHECK(s_batch_calls == 1);
    CHECK(s_last_batch_count > 35u);
    CHECK(s_single_set_calls == 0);
    CHECK(s_commit_calls == 0);
    CHECK(strcmp(s_committed_exit, "100.64.0.2") == 0);
    CHECK(strcmp(s_committed_wifi, "new-wifi") == 0);
}

static void test_failure_after_exit_node_keeps_prior_immediate_writes(void)
{
    app_config_t config = make_config();

    reset_fake();
    s_fail_key = "ts_max_peers";
    CHECK(app_config_save(&config) == ESP_FAIL);
    CHECK(s_batch_calls == 1);
    CHECK(s_single_set_calls == 0);
    CHECK(s_commit_calls == 0);
    CHECK(strcmp(s_committed_exit, "100.64.0.2") == 0);
    CHECK(strcmp(s_committed_wifi, "new-wifi") == 0);
}

static void test_null_config_is_rejected_without_writes(void)
{
    reset_fake();
    CHECK(app_config_save(NULL) == ESP_ERR_INVALID_ARG);
    CHECK(s_batch_calls == 0);
    CHECK(s_single_set_calls == 0);
    CHECK(s_commit_calls == 0);
}

static void test_targeted_exit_node_save_writes_only_one_key(void)
{
    reset_fake();
    CHECK(app_config_save_tailscale_exit_node("100.64.0.3") == ESP_OK);
    CHECK(s_batch_calls == 0);
    CHECK(s_verified_set_calls == 1);
    CHECK(strcmp(s_last_single_key, "ts_exit_node") == 0);
    CHECK(strcmp(s_last_single_value, "100.64.0.3") == 0);
    CHECK(strcmp(s_committed_exit, "100.64.0.3") == 0);
    CHECK(strcmp(s_committed_wifi, "old-wifi") == 0);
    CHECK(s_single_set_calls == 0);
    CHECK(s_commit_calls == 0);

    reset_fake();
    CHECK(app_config_save_tailscale_exit_node("") == ESP_OK);
    CHECK(s_verified_set_calls == 1);
    CHECK(strcmp(s_last_single_value, "") == 0);
}

static void test_targeted_exit_node_save_failure_and_validation(void)
{
    static const char *const invalid[] = {
        "100.064.0.3", "100.63.255.255", "100.128.0.0",
        "100.64.0", "exit.example", NULL,
    };

    reset_fake();
    s_verified_result = ESP_FAIL;
    s_verified_state = SETTINGS_STORE_WRITE_NOT_APPLIED;
    CHECK(app_config_save_tailscale_exit_node("100.64.0.3") == ESP_FAIL);
    CHECK(s_batch_calls == 0);
    CHECK(s_verified_set_calls == 1);
    CHECK(strcmp(s_committed_exit, "100.64.0.1") == 0);
    CHECK(strcmp(s_committed_wifi, "old-wifi") == 0);

    reset_fake();
    s_verified_result = ESP_ERR_INVALID_RESPONSE;
    s_verified_state = SETTINGS_STORE_WRITE_UNVERIFIED;
    CHECK(app_config_save_tailscale_exit_node("100.64.0.3") ==
          ESP_ERR_INVALID_RESPONSE);
    CHECK(s_verified_set_calls == 1);

    for (size_t index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        reset_fake();
        CHECK(app_config_save_tailscale_exit_node(invalid[index]) == ESP_ERR_INVALID_ARG);
        CHECK(s_batch_calls == 0);
        CHECK(s_verified_set_calls == 0);
    }
}

int main(void)
{
    test_full_save_is_one_serialized_batch();
    test_failure_after_exit_node_keeps_prior_immediate_writes();
    test_null_config_is_rejected_without_writes();
    test_targeted_exit_node_save_writes_only_one_key();
    test_targeted_exit_node_save_failure_and_validation();

    if (s_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", s_failures);
        return 1;
    }
    puts("app_config storage tests passed");
    return 0;
}
