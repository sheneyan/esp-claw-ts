/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "config_persistence.h"

#include <stdlib.h>

#include "app_claw.h"
#include "esp_log.h"

static const char *TAG = "config_persistence";

esp_err_t main_save_config_changes(const app_config_t *before,
                                   const app_config_t *after)
{
    app_claw_config_t *claw_config;
    esp_err_t err;

    if (!before || !after) {
        return ESP_ERR_INVALID_ARG;
    }
    err = app_config_validate_wifi(after, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = app_config_save_changed(before, after);
    if (err != ESP_OK) {
        return err;
    }

    claw_config = calloc(1, sizeof(*claw_config));
    if (!claw_config) {
        ESP_LOGW(TAG, "Failed to allocate Claw config for runtime update");
        return ESP_OK;
    }
    app_config_to_claw(after, claw_config);
    err = app_claw_update_config(claw_config);
    free(claw_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to update running Claw config: %s",
                 esp_err_to_name(err));
    }
    return ESP_OK;
}
