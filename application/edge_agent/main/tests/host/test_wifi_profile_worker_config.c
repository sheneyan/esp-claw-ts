#include "wifi_profile_worker_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char ap_ssid[] = "esp-claw";
    char ap_password[] = "secret123";
    char ap_behavior[] = "close_on_sta";
    wifi_manager_config_t source = {
        .ap_ssid_prefix = "esp-claw",
        .ap_ssid = ap_ssid,
        .ap_password = ap_password,
        .ap_behavior = ap_behavior,
        .ap_ip = "192.168.237.1",
        .ap_netmask = "255.255.255.0",
        .dhcp_start = "192.168.237.10",
        .dhcp_end = "192.168.237.50",
        .ap_channel = 1,
        .ap_max_conn = 4,
    };
    wifi_profile_worker_config_t snapshot = {0};

    assert(wifi_profile_worker_config_init(&snapshot, &source) == ESP_OK);
    memset(ap_ssid, 'x', sizeof(ap_ssid) - 1);
    memset(ap_password, 'x', sizeof(ap_password) - 1);
    memset(ap_behavior, 'x', sizeof(ap_behavior) - 1);

    assert(strcmp(snapshot.config.ap_ssid, "esp-claw") == 0);
    assert(strcmp(snapshot.config.ap_password, "secret123") == 0);
    assert(strcmp(snapshot.config.ap_behavior, "close_on_sta") == 0);
    assert(strcmp(snapshot.config.ap_ip, "192.168.237.1") == 0);
    assert(snapshot.config.sta_ssid == NULL);
    assert(snapshot.config.sta_password == NULL);
    assert(snapshot.config.ap_channel == 1);
    assert(snapshot.config.ap_max_conn == 4);
    puts("wifi_profile_worker_config: all tests passed");
    return 0;
}
