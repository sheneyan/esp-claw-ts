/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "wifi_manager.h"

typedef struct {
    wifi_manager_config_t config;
    char ap_ssid_prefix[33];
    char ap_ssid[33];
    char ap_password[65];
    char ap_behavior[16];
    char ap_ip[16];
    char ap_netmask[16];
    char dhcp_start[16];
    char dhcp_end[16];
} wifi_profile_worker_config_t;

esp_err_t wifi_profile_worker_config_init(wifi_profile_worker_config_t *snapshot,
                                          const wifi_manager_config_t *source);
