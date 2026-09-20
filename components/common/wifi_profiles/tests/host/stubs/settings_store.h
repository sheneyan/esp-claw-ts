#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    const char *key;
    const char *value;
} settings_store_string_entry_t;

esp_err_t settings_store_get_string(const char *key,
                                    char *buffer,
                                    size_t buffer_size,
                                    const char *default_value);
esp_err_t settings_store_has_key(const char *key, bool *exists);
esp_err_t settings_store_set_strings_batch(const settings_store_string_entry_t *entries,
                                           size_t count);
