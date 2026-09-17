#include "wifi_manager_ap_policy.h"

#include <assert.h>
#include <stdio.h>

#define IP4(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
     ((uint32_t)(c) << 8) | (uint32_t)(d))

int main(void)
{
    const uint32_t ap = IP4(192, 168, 237, 1);
    const uint32_t mask = IP4(255, 255, 255, 0);
    const uint32_t start = IP4(192, 168, 237, 10);
    const uint32_t end = IP4(192, 168, 237, 50);

    assert(wifi_manager_ap_network_is_valid(ap, mask, start, end, 100));
    assert(!wifi_manager_ap_network_is_valid(ap, 0, start, end, 100));
    assert(!wifi_manager_ap_network_is_valid(ap, UINT32_MAX, start, end, 100));
    assert(!wifi_manager_ap_network_is_valid(ap, IP4(255, 0, 255, 0), start, end, 100));
    assert(!wifi_manager_ap_network_is_valid(IP4(192, 168, 237, 0), mask, start, end, 100));
    assert(!wifi_manager_ap_network_is_valid(IP4(192, 168, 237, 255), mask, start, end, 100));
    assert(!wifi_manager_ap_network_is_valid(ap, mask, IP4(192, 168, 237, 0), end, 100));
    assert(!wifi_manager_ap_network_is_valid(ap, mask, start, IP4(192, 168, 237, 255), 100));
    assert(!wifi_manager_ap_network_is_valid(ap, mask, end, start, 100));
    assert(!wifi_manager_ap_network_is_valid(ap, mask, start, start, 100));
    assert(!wifi_manager_ap_network_is_valid(IP4(192, 168, 237, 20), mask, start, end, 100));
    assert(!wifi_manager_ap_network_is_valid(ap, mask,
                                             IP4(192, 168, 237, 10),
                                             IP4(192, 168, 237, 110), 100));
    assert(!wifi_manager_ap_network_is_valid(IP4(10, 0, 0, 1), IP4(255, 255, 255, 254),
                                             IP4(10, 0, 0, 0), IP4(10, 0, 0, 1), 100));
    assert(!wifi_manager_ap_network_is_valid(ap, mask, start, end, 0));

    puts("wifi_manager_ap_policy: all tests passed");
    return 0;
}
