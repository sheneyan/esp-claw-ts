/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_profiles.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "settings_store.h"

#define WIFI_PROFILES_MIGRATION_KEY "wifi_prof_v1"

static const char *const s_ssid_keys[WIFI_PROFILES_MAX_COUNT] = {
    "wprof0_ssid", "wprof1_ssid", "wprof2_ssid", "wprof3_ssid", "wprof4_ssid",
};
static const char *const s_password_keys[WIFI_PROFILES_MAX_COUNT] = {
    "wprof0_pwd", "wprof1_pwd", "wprof2_pwd", "wprof3_pwd", "wprof4_pwd",
};

static esp_err_t wifi_profiles_write(const wifi_profiles_t *profiles, bool include_marker)
{
    settings_store_string_entry_t entries[WIFI_PROFILES_MAX_COUNT * 2u + 1u] = {0};
    size_t entry_count = 0;

    for (size_t index = 0; index < WIFI_PROFILES_MAX_COUNT; ++index) {
        entries[entry_count++] = (settings_store_string_entry_t) {
            .key = s_ssid_keys[index], .value = profiles->entries[index].ssid,
        };
        entries[entry_count++] = (settings_store_string_entry_t) {
            .key = s_password_keys[index], .value = profiles->entries[index].password,
        };
    }
    if (include_marker) {
        entries[entry_count++] = (settings_store_string_entry_t) {
            .key = WIFI_PROFILES_MIGRATION_KEY, .value = "1",
        };
    }
    return settings_store_set_strings_batch(entries, entry_count);
}

static bool wifi_profile_is_empty(const wifi_profile_t *profile)
{
    return profile->ssid[0] == '\0' && profile->password[0] == '\0';
}

esp_err_t wifi_profiles_validate(const wifi_profiles_t *profiles, const char **message)
{
    if (message) {
        *message = NULL;
    }
    if (!profiles) {
        if (message) {
            *message = "profiles are required";
        }
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t index = 0; index < WIFI_PROFILES_MAX_COUNT; ++index) {
        const wifi_profile_t *profile = &profiles->entries[index];
        size_t ssid_length = strlen(profile->ssid);
        size_t password_length = strlen(profile->password);

        if (wifi_profile_is_empty(profile)) {
            continue;
        }
        if (ssid_length == 0 || ssid_length >= WIFI_PROFILE_SSID_LEN) {
            if (message) {
                *message = "wifi profile SSID must be 1-32 characters";
            }
            return ESP_ERR_INVALID_ARG;
        }
        if (password_length != 0 &&
            (password_length < 8 || password_length >= WIFI_PROFILE_PASSWORD_LEN)) {
            if (message) {
                *message = "wifi profile password must be empty or 8-63 characters";
            }
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t prior = 0; prior < index; ++prior) {
            if (profiles->entries[prior].ssid[0] != '\0' &&
                strcmp(profile->ssid, profiles->entries[prior].ssid) == 0) {
                if (message) {
                    *message = "wifi profile SSIDs must be unique";
                }
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    return ESP_OK;
}

esp_err_t wifi_profiles_load(wifi_profiles_t *profiles)
{
    if (!profiles) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(profiles, 0, sizeof(*profiles));
    for (size_t index = 0; index < WIFI_PROFILES_MAX_COUNT; ++index) {
        esp_err_t err = settings_store_get_string(s_ssid_keys[index],
                                                  profiles->entries[index].ssid,
                                                  sizeof(profiles->entries[index].ssid), "");
        if (err != ESP_OK) {
            return err;
        }
        err = settings_store_get_string(s_password_keys[index],
                                        profiles->entries[index].password,
                                        sizeof(profiles->entries[index].password), "");
        if (err != ESP_OK) {
            return err;
        }
    }
    return wifi_profiles_validate(profiles, NULL);
}

esp_err_t wifi_profiles_save(const wifi_profiles_t *profiles)
{
    esp_err_t err = wifi_profiles_validate(profiles, NULL);
    if (err != ESP_OK) {
        return err;
    }
    return wifi_profiles_write(profiles, false);
}

esp_err_t wifi_profiles_migrate_legacy(wifi_profiles_t *profiles,
                                       const char *legacy_ssid,
                                       const char *legacy_password)
{
    bool migrated = false;
    esp_err_t err;

    if (!profiles) {
        return ESP_ERR_INVALID_ARG;
    }
    err = settings_store_has_key(WIFI_PROFILES_MIGRATION_KEY, &migrated);
    if (err != ESP_OK || migrated) {
        return err;
    }

    memset(profiles, 0, sizeof(*profiles));
    if (legacy_ssid && legacy_ssid[0] != '\0') {
        snprintf(profiles->entries[0].ssid, sizeof(profiles->entries[0].ssid), "%s", legacy_ssid);
        snprintf(profiles->entries[0].password, sizeof(profiles->entries[0].password), "%s",
                 legacy_password ? legacy_password : "");
    }
    err = wifi_profiles_validate(profiles, NULL);
    if (err != ESP_OK) {
        return err;
    }
    return wifi_profiles_write(profiles, true);
}
