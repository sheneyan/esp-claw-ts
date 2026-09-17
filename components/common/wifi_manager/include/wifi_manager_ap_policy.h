/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool wifi_manager_ap_network_is_valid(uint32_t ap_ip,
                                      uint32_t netmask,
                                      uint32_t dhcp_start,
                                      uint32_t dhcp_end,
                                      uint32_t max_leases);

#ifdef __cplusplus
}
#endif
