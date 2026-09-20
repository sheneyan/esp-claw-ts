/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "wifi_profiles.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[WIFI_PROFILE_SSID_LEN];
} wifi_manager_profile_visible_t;

typedef struct {
    size_t next_index;
} wifi_manager_profile_attempt_t;

int wifi_manager_profile_pick_next(const wifi_profiles_t *profiles,
                                   const wifi_manager_profile_visible_t *visible,
                                   size_t visible_count,
                                   size_t start_after);
bool wifi_manager_profile_should_roam(bool sta_connected,
                                      int active_profile,
                                      int candidate_profile);
void wifi_manager_profile_attempt_begin(wifi_manager_profile_attempt_t *attempt);
int wifi_manager_profile_attempt_next(wifi_manager_profile_attempt_t *attempt,
                                      const wifi_profiles_t *profiles,
                                      const wifi_manager_profile_visible_t *visible,
                                      size_t visible_count);

#ifdef __cplusplus
}
#endif
