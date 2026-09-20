/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_PROFILES_MAX_COUNT 5u
#define WIFI_PROFILE_SSID_LEN   33u
#define WIFI_PROFILE_PASSWORD_LEN 65u

typedef struct {
    char ssid[WIFI_PROFILE_SSID_LEN];
    char password[WIFI_PROFILE_PASSWORD_LEN];
} wifi_profile_t;

typedef struct {
    wifi_profile_t entries[WIFI_PROFILES_MAX_COUNT];
} wifi_profiles_t;

esp_err_t wifi_profiles_validate(const wifi_profiles_t *profiles, const char **message);
esp_err_t wifi_profiles_load(wifi_profiles_t *profiles);
esp_err_t wifi_profiles_save(const wifi_profiles_t *profiles);
esp_err_t wifi_profiles_migrate_legacy(wifi_profiles_t *profiles,
                                       const char *legacy_ssid,
                                       const char *legacy_password);

#ifdef __cplusplus
}
#endif
