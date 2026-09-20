/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_profile_runtime.h"

#include <string.h>

esp_err_t wifi_profile_runtime_load(wifi_profile_runtime_t *runtime,
                                    const char *legacy_ssid,
                                    const char *legacy_password)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->selected_index = -1;
    esp_err_t err = wifi_profiles_migrate_legacy(&runtime->profiles, legacy_ssid, legacy_password);
    if (err != ESP_OK) {
        return err;
    }
    return wifi_profiles_load(&runtime->profiles);
}

const wifi_profiles_t *wifi_profile_runtime_get(const wifi_profile_runtime_t *runtime)
{
    return runtime ? &runtime->profiles : NULL;
}

esp_err_t wifi_profile_runtime_select(wifi_profile_runtime_t *runtime, size_t index)
{
    if (!runtime || index >= WIFI_PROFILES_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (runtime->profiles.entries[index].ssid[0] == '\0') {
        return ESP_ERR_NOT_FOUND;
    }
    runtime->selected_index = (int)index;
    return ESP_OK;
}
