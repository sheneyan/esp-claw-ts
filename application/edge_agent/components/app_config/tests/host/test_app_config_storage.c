#include "app_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "settings_store.h"

static int s_failures;
static int s_batch_calls;
static int s_single_set_calls;
static int s_commit_calls;
static size_t s_last_batch_count;
static const char *s_fail_key;
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
    s_commit_calls = 0;
    s_last_batch_count = 0u;
    s_fail_key = NULL;
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

esp_err_t settings_store_set_strings_atomic(
    const settings_store_string_entry_t *entries, size_t count)
{
    char staged_exit[sizeof(s_committed_exit)];
    char staged_wifi[sizeof(s_committed_wifi)];

    ++s_batch_calls;
    s_last_batch_count = count;
    (void)snprintf(staged_exit, sizeof(staged_exit), "%s", s_committed_exit);
    (void)snprintf(staged_wifi, sizeof(staged_wifi), "%s", s_committed_wifi);
    for (size_t index = 0u; index < count; ++index) {
        if (strcmp(entries[index].key, "ts_exit_node") == 0) {
            (void)snprintf(staged_exit, sizeof(staged_exit), "%s",
                           entries[index].value);
        } else if (strcmp(entries[index].key, "wifi_ssid") == 0) {
            (void)snprintf(staged_wifi, sizeof(staged_wifi), "%s",
                           entries[index].value);
        }
        if (s_fail_key != NULL && strcmp(entries[index].key, s_fail_key) == 0) {
            return ESP_FAIL;
        }
    }
    (void)snprintf(s_committed_exit, sizeof(s_committed_exit), "%s",
                   staged_exit);
    (void)snprintf(s_committed_wifi, sizeof(s_committed_wifi), "%s",
                   staged_wifi);
    return ESP_OK;
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

static void test_full_save_is_one_atomic_batch(void)
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

static void test_failure_after_exit_node_commits_nothing(void)
{
    app_config_t config = make_config();

    reset_fake();
    s_fail_key = "ts_max_peers";
    CHECK(app_config_save(&config) == ESP_FAIL);
    CHECK(s_batch_calls == 1);
    CHECK(s_single_set_calls == 0);
    CHECK(s_commit_calls == 0);
    CHECK(strcmp(s_committed_exit, "100.64.0.1") == 0);
    CHECK(strcmp(s_committed_wifi, "old-wifi") == 0);
}

static void test_null_config_is_rejected_without_writes(void)
{
    reset_fake();
    CHECK(app_config_save(NULL) == ESP_ERR_INVALID_ARG);
    CHECK(s_batch_calls == 0);
    CHECK(s_single_set_calls == 0);
    CHECK(s_commit_calls == 0);
}

int main(void)
{
    test_full_save_is_one_atomic_batch();
    test_failure_after_exit_node_commits_nothing();
    test_null_config_is_rejected_without_writes();

    if (s_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", s_failures);
        return 1;
    }
    puts("app_config storage tests passed");
    return 0;
}
