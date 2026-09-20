/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "wifi_profiles.h"

typedef struct {
    wifi_profiles_t profiles;
    int selected_index;
} wifi_profile_runtime_t;

esp_err_t wifi_profile_runtime_load(wifi_profile_runtime_t *runtime,
                                    const char *legacy_ssid,
                                    const char *legacy_password);
const wifi_profiles_t *wifi_profile_runtime_get(const wifi_profile_runtime_t *runtime);
esp_err_t wifi_profile_runtime_select(wifi_profile_runtime_t *runtime, size_t index);
