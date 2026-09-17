/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *namespace_name;
} settings_store_config_t;

#define SETTINGS_STORE_KEY_MAX_LENGTH       15u
#define SETTINGS_STORE_BATCH_MAX_ENTRIES    64u

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
esp_err_t settings_store_get_string(const char *key,
                                    char *buf,
                                    size_t buf_size,
                                    const char *default_value);
esp_err_t settings_store_has_key(const char *key, bool *exists);
esp_err_t settings_store_set_string(const char *key, const char *value);
/*
 * Serializes the complete batch under one settings mutex and NVS handle.
 * ESP-IDF v5.5 writes each NVS value immediately, so a later entry failure
 * can leave earlier entries persisted. The batch prevents interleaving; it
 * does not provide multi-key rollback or power-loss atomicity.
 */
esp_err_t settings_store_set_strings_batch(
    const settings_store_string_entry_t *entries, size_t count);
/*
 * Replaces one string while holding the settings mutex through readback.
 * out_state distinguishes a verified applied value, a verified unchanged
 * value, and a value whose persisted state could not be determined.
 */
esp_err_t settings_store_set_string_verified(
    const char *key, const char *value, settings_store_write_state_t *out_state);
esp_err_t settings_store_erase_key(const char *key);
esp_err_t settings_store_begin_factory_reset(void);
esp_err_t settings_store_cancel_factory_reset(void);
esp_err_t settings_store_erase_all(void);
esp_err_t settings_store_commit(void);

#ifdef __cplusplus
}
#endif
