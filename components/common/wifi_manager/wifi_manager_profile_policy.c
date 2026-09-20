/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_manager_profile_policy.h"

#include <string.h>

int wifi_manager_profile_pick_next(const wifi_profiles_t *profiles,
                                   const wifi_manager_profile_visible_t *visible,
                                   size_t visible_count,
                                   size_t start_after)
{
    if (!profiles || !visible || start_after >= WIFI_PROFILES_MAX_COUNT) {
        return -1;
    }

    for (size_t profile_index = start_after;
         profile_index < WIFI_PROFILES_MAX_COUNT;
         ++profile_index) {
        const char *ssid = profiles->entries[profile_index].ssid;
        if (ssid[0] == '\0') {
            continue;
        }
        for (size_t visible_index = 0; visible_index < visible_count; ++visible_index) {
            if (strcmp(ssid, visible[visible_index].ssid) == 0) {
                return (int)profile_index;
            }
        }
    }
    return -1;
}

bool wifi_manager_profile_should_roam(bool sta_connected,
                                      int active_profile,
                                      int candidate_profile)
{
    (void)sta_connected;
    (void)active_profile;
    (void)candidate_profile;
    return false;
}

void wifi_manager_profile_attempt_begin(wifi_manager_profile_attempt_t *attempt)
{
    if (attempt) {
        attempt->next_index = 0;
    }
}

int wifi_manager_profile_attempt_next(wifi_manager_profile_attempt_t *attempt,
                                      const wifi_profiles_t *profiles,
                                      const wifi_manager_profile_visible_t *visible,
                                      size_t visible_count)
{
    if (!attempt) {
        return -1;
    }
    int selected = wifi_manager_profile_pick_next(profiles, visible, visible_count,
                                                  attempt->next_index);
    if (selected >= 0) {
        attempt->next_index = (size_t)selected + 1u;
    }
    return selected;
}
