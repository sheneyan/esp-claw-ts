#include "wifi_profile_runtime.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static wifi_profiles_t s_profiles;
static int s_load_calls;
static int s_migrate_calls;

esp_err_t wifi_profiles_load(wifi_profiles_t *profiles)
{
    ++s_load_calls;
    *profiles = s_profiles;
    return ESP_OK;
}

esp_err_t wifi_profiles_migrate_legacy(wifi_profiles_t *profiles,
                                       const char *legacy_ssid,
                                       const char *legacy_password)
{
    (void)legacy_ssid;
    (void)legacy_password;
    ++s_migrate_calls;
    *profiles = s_profiles;
    return ESP_OK;
}

int main(void)
{
    wifi_profile_runtime_t runtime = {0};
    snprintf(s_profiles.entries[0].ssid, sizeof(s_profiles.entries[0].ssid), "Home");

    assert(wifi_profile_runtime_load(&runtime, "Legacy", "legacy-pass") == ESP_OK);
    assert(s_migrate_calls == 1);
    assert(s_load_calls == 1);
    assert(strcmp(wifi_profile_runtime_get(&runtime)->entries[0].ssid, "Home") == 0);
    assert(wifi_profile_runtime_select(&runtime, 0) == ESP_OK);
    assert(wifi_profile_runtime_select(&runtime, 1) == ESP_ERR_NOT_FOUND);
    puts("wifi_profile_runtime: all tests passed");
    return 0;
}
