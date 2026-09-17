/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cmd_wifi_config_update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CMD_WIFI_CONFIG_UPDATE_HOST_TEST
extern void *cmd_wifi_config_update_test_allocate(size_t size);
extern void cmd_wifi_config_update_test_release(void *ptr);
#define config_allocate cmd_wifi_config_update_test_allocate
#define config_release  cmd_wifi_config_update_test_release
#else
static void *config_allocate(size_t size)
{
    return calloc(1u, size);
}

static void config_release(void *ptr)
{
    free(ptr);
}
#endif

static void copy_string(char *destination, size_t capacity, const char *source)
{
    (void)snprintf(destination, capacity, "%s", source);
}

esp_err_t cmd_wifi_config_update(
    const char *ssid,
    const char *password,
    bool has_password,
    bool apply_now,
    cmd_wifi_config_apply_fn_t apply,
    cmd_wifi_config_saved_fn_t saved,
    void *ctx,
    cmd_wifi_config_update_result_t *result)
{
    app_config_t *before = NULL;
    app_config_t *after = NULL;
    const char *validation_message = NULL;
    esp_err_t err = ESP_OK;

    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    if (apply_now && apply == NULL) {
        result->outcome = CMD_WIFI_CONFIG_UPDATE_INVALID_ARGUMENT;
        return ESP_ERR_INVALID_ARG;
    }

    before = config_allocate(sizeof(*before));
    if (before == NULL) {
        result->outcome = CMD_WIFI_CONFIG_UPDATE_ALLOCATION_FAILED;
        return ESP_ERR_NO_MEM;
    }
    memset(before, 0, sizeof(*before));
    after = config_allocate(sizeof(*after));
    if (after == NULL) {
        result->outcome = CMD_WIFI_CONFIG_UPDATE_ALLOCATION_FAILED;
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    memset(after, 0, sizeof(*after));

    err = app_config_load(before);
    if (err != ESP_OK) {
        result->outcome = CMD_WIFI_CONFIG_UPDATE_LOAD_FAILED;
        goto cleanup;
    }
    if (ssid == NULL) {
        result->outcome = CMD_WIFI_CONFIG_UPDATE_MISSING_SSID;
        err = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    memcpy(after, before, sizeof(*after));
    copy_string(after->wifi_ssid, sizeof(after->wifi_ssid), ssid);
    if (has_password) {
        copy_string(after->wifi_password, sizeof(after->wifi_password),
                    password != NULL ? password : "");
    }

    err = app_config_validate_wifi(after, &validation_message);
    if (err != ESP_OK) {
        result->outcome = CMD_WIFI_CONFIG_UPDATE_VALIDATION_FAILED;
        if (validation_message != NULL) {
            copy_string(result->validation_message,
                        sizeof(result->validation_message),
                        validation_message);
        }
        goto cleanup;
    }
    err = app_config_save_changed(before, after);
    if (err != ESP_OK) {
        result->outcome = CMD_WIFI_CONFIG_UPDATE_SAVE_FAILED;
        goto cleanup;
    }

    if (apply_now) {
        result->apply_result = apply(after, ctx);
        if (result->apply_result != 0) {
            result->outcome = CMD_WIFI_CONFIG_UPDATE_APPLY_FAILED;
        } else {
            result->outcome = CMD_WIFI_CONFIG_UPDATE_COMPLETE;
        }
    } else {
        if (saved != NULL) {
            saved(after, ctx);
        }
        result->outcome = CMD_WIFI_CONFIG_UPDATE_COMPLETE;
    }

cleanup:
    if (after != NULL) {
        config_release(after);
    }
    config_release(before);
    return err;
}
