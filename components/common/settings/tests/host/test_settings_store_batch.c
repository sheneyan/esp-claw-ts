#include "settings_store.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvs.h"

#define ARRAY_SIZE(values) (sizeof(values) / sizeof((values)[0]))

typedef struct {
    char key[16];
    char value[64];
} fake_value_t;

static int s_failures;
static fake_value_t s_committed[8];
static size_t s_committed_count;
static int s_open_calls;
static int s_close_calls;
static int s_set_calls;
static int s_commit_calls;
static int s_fail_set_call;
static int s_fail_after_write_call;
static int s_fail_get_call;
static int s_get_calls;
static bool s_fail_commit;
static pthread_mutex_t s_block_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_block_cond = PTHREAD_COND_INITIALIZER;
static bool s_block_first_set;
static bool s_first_set_entered;
static bool s_release_first_set;
static int s_mutex_take_attempts;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
        ++s_failures; \
    } \
} while (0)

static void copy_value(fake_value_t *destination, const char *key,
                       const char *value)
{
    (void)snprintf(destination->key, sizeof(destination->key), "%s", key);
    (void)snprintf(destination->value, sizeof(destination->value), "%s", value);
}

static const char *committed_value(const char *key)
{
    for (size_t index = 0u; index < s_committed_count; ++index) {
        if (strcmp(s_committed[index].key, key) == 0) {
            return s_committed[index].value;
        }
    }
    return NULL;
}

static void reset_fake(void)
{
    memset(s_committed, 0, sizeof(s_committed));
    s_committed_count = 2u;
    copy_value(&s_committed[0], "ts_exit_node", "100.64.0.1");
    copy_value(&s_committed[1], "wifi_ssid", "old-wifi");
    s_open_calls = 0;
    s_close_calls = 0;
    s_set_calls = 0;
    s_commit_calls = 0;
    s_fail_set_call = 0;
    s_fail_after_write_call = 0;
    s_fail_get_call = 0;
    s_get_calls = 0;
    s_fail_commit = false;
    s_block_first_set = false;
    s_first_set_entered = false;
    s_release_first_set = false;
    s_mutex_take_attempts = 0;
}

void settings_store_test_before_mutex_take(void)
{
    CHECK(pthread_mutex_lock(&s_block_lock) == 0);
    ++s_mutex_take_attempts;
    CHECK(pthread_cond_broadcast(&s_block_cond) == 0);
    CHECK(pthread_mutex_unlock(&s_block_lock) == 0);
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t mode,
                   nvs_handle_t *handle)
{
    CHECK(namespace_name != NULL);
    CHECK(mode == NVS_READWRITE || mode == NVS_READONLY);
    ++s_open_calls;
    *handle = 1;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    CHECK(handle == 1);
    ++s_close_calls;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out,
                      size_t *length)
{
    const char *value;
    size_t required_size;

    CHECK(handle == 1);
    CHECK(length != NULL);
    ++s_get_calls;
    if (s_fail_get_call != 0 && s_get_calls == s_fail_get_call) {
        return ESP_FAIL;
    }
    value = committed_value(key);
    if (!value) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    required_size = strlen(value) + 1u;
    if (!out) {
        *length = required_size;
        return ESP_OK;
    }
    if (*length < required_size) {
        *length = required_size;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out, value, required_size);
    *length = required_size;
    return ESP_OK;
}

static void persist_value(const char *key, const char *value)
{
    for (size_t index = 0u; index < s_committed_count; ++index) {
        if (strcmp(s_committed[index].key, key) == 0) {
            copy_value(&s_committed[index], key, value);
            return;
        }
    }
    CHECK(s_committed_count < ARRAY_SIZE(s_committed));
    copy_value(&s_committed[s_committed_count++], key, value);
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    CHECK(handle == 1);
    ++s_set_calls;
    if (s_block_first_set && s_set_calls == 1) {
        CHECK(pthread_mutex_lock(&s_block_lock) == 0);
        s_first_set_entered = true;
        CHECK(pthread_cond_broadcast(&s_block_cond) == 0);
        while (!s_release_first_set) {
            CHECK(pthread_cond_wait(&s_block_cond, &s_block_lock) == 0);
        }
        CHECK(pthread_mutex_unlock(&s_block_lock) == 0);
    }
    if (s_fail_set_call != 0 && s_set_calls == s_fail_set_call) {
        return ESP_FAIL;
    }
    persist_value(key, value);
    if (s_fail_after_write_call != 0 &&
        s_set_calls == s_fail_after_write_call) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    CHECK(handle == 1);
    ++s_commit_calls;
    if (s_fail_commit) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle;
    (void)key;
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_erase_all(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

static void test_batch_success_and_single_delegate(void)
{
    const settings_store_string_entry_t entries[] = {
        {.key = "ts_exit_node", .value = "100.64.0.2"},
        {.key = "wifi_ssid", .value = "new-wifi"},
    };

    reset_fake();
    CHECK(settings_store_set_strings_batch(entries, ARRAY_SIZE(entries)) == ESP_OK);
    CHECK(s_open_calls == 1);
    CHECK(s_set_calls == 2);
    CHECK(s_commit_calls == 1);
    CHECK(s_close_calls == 1);
    CHECK(strcmp(committed_value("ts_exit_node"), "100.64.0.2") == 0);
    CHECK(strcmp(committed_value("wifi_ssid"), "new-wifi") == 0);

    reset_fake();
    CHECK(settings_store_set_string("wifi_ssid", NULL) == ESP_OK);
    CHECK(s_open_calls == 1);
    CHECK(s_set_calls == 1);
    CHECK(s_commit_calls == 1);
    CHECK(strcmp(committed_value("wifi_ssid"), "") == 0);
}

static void test_batch_failure_stops_but_does_not_rollback_prior_writes(void)
{
    const settings_store_string_entry_t entries[] = {
        {.key = "ts_exit_node", .value = "100.64.0.2"},
        {.key = "wifi_ssid", .value = "new-wifi"},
    };

    reset_fake();
    s_fail_set_call = 2;
    CHECK(settings_store_set_strings_batch(entries, ARRAY_SIZE(entries)) == ESP_FAIL);
    CHECK(s_set_calls == 2);
    CHECK(s_commit_calls == 0);
    CHECK(s_close_calls == 1);
    CHECK(strcmp(committed_value("ts_exit_node"), "100.64.0.2") == 0);
    CHECK(strcmp(committed_value("wifi_ssid"), "old-wifi") == 0);

    reset_fake();
    s_fail_commit = true;
    CHECK(settings_store_set_strings_batch(entries, ARRAY_SIZE(entries)) == ESP_FAIL);
    CHECK(s_commit_calls == 1);
    CHECK(s_close_calls == 1);
    CHECK(strcmp(committed_value("ts_exit_node"), "100.64.0.2") == 0);
    CHECK(strcmp(committed_value("wifi_ssid"), "new-wifi") == 0);
}

typedef struct {
    const settings_store_string_entry_t *entries;
    size_t count;
    bool verified;
    esp_err_t result;
} writer_thread_t;

static void *run_writer(void *opaque)
{
    writer_thread_t *writer = opaque;
    if (writer->verified) {
        settings_store_write_state_t state;
        writer->result = settings_store_set_string_verified(
            writer->entries[0].key, writer->entries[0].value, &state);
        CHECK(state == SETTINGS_STORE_WRITE_APPLIED);
    } else {
        writer->result = settings_store_set_strings_batch(writer->entries,
                                                           writer->count);
    }
    return NULL;
}

static void test_batches_cannot_interleave(void)
{
    const settings_store_string_entry_t whole[] = {
        {.key = "ts_exit_node", .value = "100.64.0.2"},
        {.key = "wifi_ssid", .value = "whole-wifi"},
    };
    const settings_store_string_entry_t targeted[] = {
        {.key = "ts_exit_node", .value = "100.64.0.3"},
    };
    writer_thread_t first = {.entries = whole, .count = ARRAY_SIZE(whole)};
    writer_thread_t second = {
        .entries = targeted,
        .count = ARRAY_SIZE(targeted),
        .verified = true,
    };
    pthread_t first_thread;
    pthread_t second_thread;

    reset_fake();
    s_block_first_set = true;
    CHECK(pthread_create(&first_thread, NULL, run_writer, &first) == 0);
    CHECK(pthread_mutex_lock(&s_block_lock) == 0);
    while (!s_first_set_entered) {
        CHECK(pthread_cond_wait(&s_block_cond, &s_block_lock) == 0);
    }
    CHECK(pthread_mutex_unlock(&s_block_lock) == 0);
    CHECK(pthread_create(&second_thread, NULL, run_writer, &second) == 0);

    CHECK(pthread_mutex_lock(&s_block_lock) == 0);
    while (s_mutex_take_attempts < 2) {
        CHECK(pthread_cond_wait(&s_block_cond, &s_block_lock) == 0);
    }
    CHECK(s_open_calls == 1);
    CHECK(s_set_calls == 1);
    s_release_first_set = true;
    CHECK(pthread_cond_broadcast(&s_block_cond) == 0);
    CHECK(pthread_mutex_unlock(&s_block_lock) == 0);

    CHECK(pthread_join(first_thread, NULL) == 0);
    CHECK(pthread_join(second_thread, NULL) == 0);
    CHECK(first.result == ESP_OK);
    CHECK(second.result == ESP_OK);
    CHECK(s_open_calls == 2);
    CHECK(s_commit_calls == 2);
    CHECK(strcmp(committed_value("wifi_ssid"), "whole-wifi") == 0);
    CHECK(strcmp(committed_value("ts_exit_node"), "100.64.0.3") == 0);
}

static void test_verified_replace_classifies_real_pinned_nvs_outcomes(void)
{
    settings_store_write_state_t state;

    reset_fake();
    s_fail_set_call = 1;
    CHECK(settings_store_set_string_verified("ts_exit_node", "100.64.0.2",
                                             &state) == ESP_FAIL);
    CHECK(state == SETTINGS_STORE_WRITE_NOT_APPLIED);
    CHECK(strcmp(committed_value("ts_exit_node"), "100.64.0.1") == 0);

    reset_fake();
    s_fail_after_write_call = 1;
    CHECK(settings_store_set_string_verified("ts_exit_node", "100.64.0.2",
                                             &state) == ESP_OK);
    CHECK(state == SETTINGS_STORE_WRITE_APPLIED);
    CHECK(strcmp(committed_value("ts_exit_node"), "100.64.0.2") == 0);

    reset_fake();
    s_fail_after_write_call = 1;
    s_fail_get_call = 3;
    CHECK(settings_store_set_string_verified("ts_exit_node", "100.64.0.2",
                                             &state) == ESP_ERR_INVALID_RESPONSE);
    CHECK(state == SETTINGS_STORE_WRITE_UNVERIFIED);
    CHECK(strcmp(committed_value("ts_exit_node"), "100.64.0.2") == 0);
}

static void test_invalid_arguments_do_not_touch_nvs(void)
{
    settings_store_string_entry_t entry = {.key = "", .value = "value"};
    settings_store_write_state_t state;
    char oversized_key[17];

    reset_fake();
    memset(oversized_key, 'k', sizeof(oversized_key) - 1u);
    oversized_key[sizeof(oversized_key) - 1u] = '\0';
    CHECK(settings_store_set_strings_batch(NULL, 1) == ESP_ERR_INVALID_ARG);
    CHECK(settings_store_set_strings_batch(&entry, 0) == ESP_ERR_INVALID_ARG);
    CHECK(settings_store_set_strings_batch(&entry,
          SETTINGS_STORE_BATCH_MAX_ENTRIES + 1u) == ESP_ERR_INVALID_SIZE);
    CHECK(settings_store_set_string(NULL, "value") == ESP_ERR_INVALID_ARG);
    CHECK(settings_store_set_string_verified(NULL, "value", &state) ==
          ESP_ERR_INVALID_ARG);
    CHECK(settings_store_set_string_verified("key", "value", NULL) ==
          ESP_ERR_INVALID_ARG);
    entry.key = oversized_key;
    CHECK(settings_store_set_strings_batch(&entry, 1) == ESP_ERR_INVALID_SIZE);
    CHECK(s_open_calls == 0);
}

int main(void)
{
    CHECK(settings_store_init(&(settings_store_config_t) {
        .namespace_name = "app",
    }) == ESP_OK);
    test_batch_success_and_single_delegate();
    test_batch_failure_stops_but_does_not_rollback_prior_writes();
    test_batches_cannot_interleave();
    test_verified_replace_classifies_real_pinned_nvs_outcomes();
    test_invalid_arguments_do_not_touch_nvs();

    if (s_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", s_failures);
        return 1;
    }
    puts("settings_store batch tests passed");
    return 0;
}
