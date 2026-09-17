#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define SETTINGS_STORE_BATCH_MAX_ENTRIES 64u

typedef struct {
    const char *namespace_name;
} settings_store_config_t;

typedef struct {
    const char *key;
    const char *value;
} settings_store_string_entry_t;

typedef enum {
    SETTINGS_STORE_WRITE_NOT_APPLIED = 0,
    SETTINGS_STORE_WRITE_APPLIED,
    SETTINGS_STORE_WRITE_UNVERIFIED,
} settings_store_write_state_t;

esp_err_t settings_store_init(const settings_store_config_t *config);
esp_err_t settings_store_get_string(const char *key, char *buf,
                                    size_t buf_size, const char *default_value);
esp_err_t settings_store_has_key(const char *key, bool *exists);
esp_err_t settings_store_set_string(const char *key, const char *value);
esp_err_t settings_store_set_strings_batch(
    const settings_store_string_entry_t *entries, size_t count);
esp_err_t settings_store_set_string_verified(
    const char *key, const char *value, settings_store_write_state_t *out_state);
esp_err_t settings_store_erase_key(const char *key);
esp_err_t settings_store_commit(void);
