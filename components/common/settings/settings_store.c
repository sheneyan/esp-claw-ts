/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "settings_store.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "settings_store";

static char s_namespace[16];
static bool s_initialized;
static SemaphoreHandle_t s_write_mutex;
static bool s_factory_reset_latched;
static TaskHandle_t s_factory_reset_owner;

static esp_err_t settings_store_lock(void)
{
    if (!s_write_mutex) return ESP_ERR_INVALID_STATE;
    return xSemaphoreTake(s_write_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_FAIL;
}

static void settings_store_unlock(void)
{
    xSemaphoreGive(s_write_mutex);
}

static esp_err_t settings_store_check_write_allowed(void)
{
    return s_factory_reset_latched ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static esp_err_t settings_store_open(nvs_open_mode_t mode, nvs_handle_t *handle)
{
    if (!s_initialized || s_namespace[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    return nvs_open(s_namespace, mode, handle);
}

esp_err_t settings_store_init(const settings_store_config_t *config)
{
    if (!config || !config->namespace_name || config->namespace_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlcpy(s_namespace, config->namespace_name, sizeof(s_namespace)) >= sizeof(s_namespace)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!s_write_mutex) {
        s_write_mutex = xSemaphoreCreateMutex();
        if (!s_write_mutex) return ESP_ERR_NO_MEM;
    }

    s_initialized = true;

    nvs_handle_t handle;
    esp_err_t err = settings_store_open(NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_close(handle);
        return ESP_OK;
    }

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = settings_store_open(NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            nvs_close(handle);
        }
    }

    return err;
}

esp_err_t settings_store_get_string(const char *key,
                                    char *buf,
                                    size_t buf_size,
                                    const char *default_value)
{
    if (!key || !buf || buf_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    buf[0] = '\0';

    nvs_handle_t handle;
    esp_err_t err = settings_store_open(NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        if (default_value) {
            strlcpy(buf, default_value, buf_size);
        }
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = buf_size;
    err = nvs_get_str(handle, key, buf, &required_size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        if (default_value) {
            strlcpy(buf, default_value, buf_size);
        }
        return ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str(%s) failed: %s", key, esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t settings_store_has_key(const char *key, bool *exists)
{
    if (!key || !exists) {
        return ESP_ERR_INVALID_ARG;
    }

    *exists = false;

    nvs_handle_t handle;
    esp_err_t err = settings_store_open(NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = 0;
    err = nvs_get_str(handle, key, NULL, &required_size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str(%s) failed: %s", key, esp_err_to_name(err));
        return err;
    }

    *exists = true;
    return ESP_OK;
}

static esp_err_t settings_store_validate_string_entries(
    const settings_store_string_entry_t *entries, size_t count)
{
    if (!entries || count == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (count > SETTINGS_STORE_BATCH_MAX_ENTRIES) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t index = 0u; index < count; ++index) {
        size_t key_length = 0u;
        if (!entries[index].key || entries[index].key[0] == '\0') {
            return ESP_ERR_INVALID_ARG;
        }
        while (key_length <= SETTINGS_STORE_KEY_MAX_LENGTH &&
               entries[index].key[key_length] != '\0') {
            ++key_length;
        }
        if (key_length > SETTINGS_STORE_KEY_MAX_LENGTH) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    return ESP_OK;
}

esp_err_t settings_store_set_strings_batch(
    const settings_store_string_entry_t *entries, size_t count)
{
    nvs_handle_t handle;
    esp_err_t err = settings_store_validate_string_entries(entries, count);
    if (err != ESP_OK) {
        return err;
    }

    err = settings_store_lock();
    if (err != ESP_OK) return err;
    err = settings_store_check_write_allowed();
    if (err != ESP_OK) {
        settings_store_unlock();
        return err;
    }

    err = settings_store_open(NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        settings_store_unlock();
        return err;
    }

    for (size_t index = 0u; index < count; ++index) {
        err = nvs_set_str(handle, entries[index].key,
                          entries[index].value ? entries[index].value : "");
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_str(%s) failed: %s",
                     entries[index].key, esp_err_to_name(err));
            break;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        }
    }

    nvs_close(handle);
    settings_store_unlock();
    return err;
}

esp_err_t settings_store_set_string(const char *key, const char *value)
{
    const settings_store_string_entry_t entry = {
        .key = key,
        .value = value,
    };
    return settings_store_set_strings_batch(&entry, 1u);
}

static esp_err_t settings_store_read_allocated(nvs_handle_t handle,
                                               const char *key,
                                               char **out,
                                               bool *exists)
{
    size_t required_size = 0u;
    esp_err_t err;

    *out = NULL;
    *exists = false;
    err = nvs_get_str(handle, key, NULL, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (required_size == 0u) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out = malloc(required_size);
    if (!*out) {
        return ESP_ERR_NO_MEM;
    }
    err = nvs_get_str(handle, key, *out, &required_size);
    if (err != ESP_OK) {
        free(*out);
        *out = NULL;
        return err;
    }
    *exists = true;
    return ESP_OK;
}

esp_err_t settings_store_set_string_verified(
    const char *key, const char *value, settings_store_write_state_t *out_state)
{
    const char *requested = value ? value : "";
    char *old_value = NULL;
    char *readback = NULL;
    bool old_exists = false;
    bool readback_exists = false;
    nvs_handle_t handle;
    esp_err_t set_err;
    esp_err_t err;

    if (out_state) {
        *out_state = SETTINGS_STORE_WRITE_NOT_APPLIED;
    }
    if (!key || key[0] == '\0' || !out_state) {
        return ESP_ERR_INVALID_ARG;
    }
    settings_store_string_entry_t entry = {.key = key, .value = requested};
    err = settings_store_validate_string_entries(&entry, 1u);
    if (err != ESP_OK) {
        return err;
    }
    err = settings_store_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = settings_store_check_write_allowed();
    if (err != ESP_OK) {
        settings_store_unlock();
        return err;
    }
    err = settings_store_open(NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        settings_store_unlock();
        return err;
    }
    err = settings_store_read_allocated(handle, key, &old_value, &old_exists);
    if (err != ESP_OK) {
        goto done;
    }

    set_err = nvs_set_str(handle, key, requested);
    if (set_err == ESP_OK) {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit failed for key %s: %s",
                     key, esp_err_to_name(err));
            *out_state = SETTINGS_STORE_WRITE_UNVERIFIED;
            err = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
    } else {
        ESP_LOGE(TAG, "nvs_set_str(%s) failed: %s", key,
                 esp_err_to_name(set_err));
    }

    err = settings_store_read_allocated(handle, key, &readback,
                                        &readback_exists);
    if (err != ESP_OK) {
        *out_state = SETTINGS_STORE_WRITE_UNVERIFIED;
        err = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    if (readback_exists && strcmp(readback, requested) == 0) {
        *out_state = SETTINGS_STORE_WRITE_APPLIED;
        err = ESP_OK;
    } else if (readback_exists == old_exists &&
               (!old_exists || strcmp(readback, old_value) == 0)) {
        *out_state = SETTINGS_STORE_WRITE_NOT_APPLIED;
        err = set_err == ESP_OK ? ESP_FAIL : set_err;
    } else {
        *out_state = SETTINGS_STORE_WRITE_UNVERIFIED;
        err = ESP_ERR_INVALID_RESPONSE;
    }

done:
    free(readback);
    free(old_value);
    nvs_close(handle);
    settings_store_unlock();
    return err;
}

esp_err_t settings_store_erase_key(const char *key)
{
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = settings_store_lock();
    if (err != ESP_OK) return err;
    err = settings_store_check_write_allowed();
    if (err != ESP_OK) {
        settings_store_unlock();
        return err;
    }

    nvs_handle_t handle;
    err = settings_store_open(NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        settings_store_unlock();
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        settings_store_unlock();
        return err;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_key(%s) failed: %s", key, esp_err_to_name(err));
        nvs_close(handle);
        settings_store_unlock();
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    settings_store_unlock();
    return err;
}

esp_err_t settings_store_begin_factory_reset(void)
{
    esp_err_t err = settings_store_lock();
    if (err != ESP_OK) return err;

    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    if (s_factory_reset_latched) {
        err = s_factory_reset_owner == caller ? ESP_OK : ESP_ERR_INVALID_STATE;
    } else {
        s_factory_reset_latched = true;
        s_factory_reset_owner = caller;
        err = ESP_OK;
    }

    settings_store_unlock();
    return err;
}

esp_err_t settings_store_cancel_factory_reset(void)
{
    esp_err_t err = settings_store_lock();
    if (err != ESP_OK) return err;

    if (!s_factory_reset_latched || s_factory_reset_owner != xTaskGetCurrentTaskHandle()) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        s_factory_reset_latched = false;
        s_factory_reset_owner = NULL;
        err = ESP_OK;
    }

    settings_store_unlock();
    return err;
}

esp_err_t settings_store_erase_all(void)
{
    esp_err_t err = settings_store_lock();
    if (err != ESP_OK) return err;
    if (s_factory_reset_latched && s_factory_reset_owner != xTaskGetCurrentTaskHandle()) {
        settings_store_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    err = settings_store_open(NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        settings_store_unlock();
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        settings_store_unlock();
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase settings namespace: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
    settings_store_unlock();
    return err;
}

esp_err_t settings_store_commit(void)
{
    esp_err_t err = settings_store_lock();
    if (err != ESP_OK) return err;
    err = settings_store_check_write_allowed();
    settings_store_unlock();
    return err;
}
