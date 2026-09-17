/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_manager_ap_policy.h"

#include <limits.h>

static bool is_usable_host(uint32_t address, uint32_t network, uint32_t broadcast)
{
    return address > network && address < broadcast;
}

bool wifi_manager_ap_network_is_valid(uint32_t ap_ip,
                                      uint32_t netmask,
                                      uint32_t dhcp_start,
                                      uint32_t dhcp_end,
                                      uint32_t max_leases)
{
    if (netmask == 0 || netmask == UINT32_MAX || max_leases == 0) return false;

    const uint32_t host_bits = ~netmask;
    if ((host_bits & (host_bits + 1U)) != 0 || host_bits < 3U) return false;

    const uint32_t network = ap_ip & netmask;
    const uint32_t broadcast = network | host_bits;
    if (!is_usable_host(ap_ip, network, broadcast) ||
        !is_usable_host(dhcp_start, network, broadcast) ||
        !is_usable_host(dhcp_end, network, broadcast)) {
        return false;
    }
    if ((dhcp_start & netmask) != network || (dhcp_end & netmask) != network) return false;
    if (dhcp_start >= dhcp_end) return false;
    if (ap_ip >= dhcp_start && ap_ip <= dhcp_end) return false;

    return (dhcp_end - dhcp_start + 1U) <= max_leases;
}
