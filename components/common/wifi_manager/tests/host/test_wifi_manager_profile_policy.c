#include "wifi_manager_profile_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void set_ssid(char *destination, size_t destination_size, const char *ssid)
{
    assert(snprintf(destination, destination_size, "%s", ssid) > 0);
}

int main(void)
{
    wifi_profiles_t profiles = {0};
    wifi_manager_profile_visible_t visible[3] = {0};

    set_ssid(profiles.entries[0].ssid, sizeof(profiles.entries[0].ssid), "Home");
    set_ssid(profiles.entries[1].ssid, sizeof(profiles.entries[1].ssid), "Office");
    set_ssid(profiles.entries[2].ssid, sizeof(profiles.entries[2].ssid), "Phone");
    set_ssid(visible[0].ssid, sizeof(visible[0].ssid), "Office");
    set_ssid(visible[1].ssid, sizeof(visible[1].ssid), "Phone");
    set_ssid(visible[2].ssid, sizeof(visible[2].ssid), "Home");

    assert(wifi_manager_profile_pick_next(&profiles, visible, 3, 0) == 0);
    assert(wifi_manager_profile_pick_next(&profiles, visible, 3, 1) == 1);
    assert(wifi_manager_profile_pick_next(&profiles, visible, 3, 3) == -1);
    assert(!wifi_manager_profile_should_roam(true, 0, 1));
    assert(!wifi_manager_profile_should_roam(false, 0, 1));

    puts("wifi_manager_profile_policy: all tests passed");
    return 0;
}
