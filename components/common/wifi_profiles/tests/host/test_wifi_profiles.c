#include "wifi_profiles.h"
#include "settings_store.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char key[16];
    char value[65];
} fake_setting_t;

static fake_setting_t s_settings[16];
static size_t s_setting_count;

esp_err_t settings_store_get_string(const char *key,
                                    char *buffer,
                                    size_t buffer_size,
                                    const char *default_value)
{
    for (size_t index = 0; index < s_setting_count; ++index) {
        if (strcmp(s_settings[index].key, key) == 0) {
            snprintf(buffer, buffer_size, "%s", s_settings[index].value);
            return ESP_OK;
        }
    }
    snprintf(buffer, buffer_size, "%s", default_value ? default_value : "");
    return ESP_OK;
}

esp_err_t settings_store_has_key(const char *key, bool *exists)
{
    *exists = false;
    for (size_t index = 0; index < s_setting_count; ++index) {
        if (strcmp(s_settings[index].key, key) == 0) {
            *exists = true;
            break;
        }
    }
    return ESP_OK;
}

esp_err_t settings_store_set_strings_batch(const settings_store_string_entry_t *entries,
                                           size_t count)
{
    assert(count <= 16);
    s_setting_count = count;
    for (size_t index = 0; index < count; ++index) {
        snprintf(s_settings[index].key, sizeof(s_settings[index].key), "%s", entries[index].key);
        snprintf(s_settings[index].value, sizeof(s_settings[index].value), "%s",
                 entries[index].value ? entries[index].value : "");
    }
    return ESP_OK;
}

static void copy_string(char *destination, size_t destination_size, const char *source)
{
    assert(snprintf(destination, destination_size, "%s", source) > 0);
}

int main(void)
{
    const char *message = NULL;
    wifi_profiles_t profiles = {0};

    copy_string(profiles.entries[0].ssid, sizeof(profiles.entries[0].ssid), "Home");
    copy_string(profiles.entries[0].password, sizeof(profiles.entries[0].password), "home-pass");
    copy_string(profiles.entries[1].ssid, sizeof(profiles.entries[1].ssid), "Office");
    copy_string(profiles.entries[1].password, sizeof(profiles.entries[1].password), "office-pass");
    assert(wifi_profiles_validate(&profiles, &message) == ESP_OK);

    copy_string(profiles.entries[2].ssid, sizeof(profiles.entries[2].ssid), "Home");
    assert(wifi_profiles_validate(&profiles, &message) == ESP_ERR_INVALID_ARG);
    assert(message != NULL);

    profiles.entries[2].ssid[0] = '\0';
    copy_string(profiles.entries[3].ssid, sizeof(profiles.entries[3].ssid), "BadPassword");
    copy_string(profiles.entries[3].password, sizeof(profiles.entries[3].password), "short");
    assert(wifi_profiles_validate(&profiles, &message) == ESP_ERR_INVALID_ARG);

    memset(&profiles, 0, sizeof(profiles));
    copy_string(profiles.entries[0].ssid, sizeof(profiles.entries[0].ssid), "Home");
    copy_string(profiles.entries[0].password, sizeof(profiles.entries[0].password), "home-pass");
    assert(wifi_profiles_save(&profiles) == ESP_OK);
    memset(&profiles, 0, sizeof(profiles));
    assert(wifi_profiles_load(&profiles) == ESP_OK);
    assert(strcmp(profiles.entries[0].ssid, "Home") == 0);
    assert(strcmp(profiles.entries[0].password, "home-pass") == 0);

    memset(&profiles, 0, sizeof(profiles));
    s_setting_count = 0;
    assert(wifi_profiles_migrate_legacy(&profiles, "Legacy", "legacy-pass") == ESP_OK);
    assert(strcmp(profiles.entries[0].ssid, "Legacy") == 0);
    copy_string(profiles.entries[0].ssid, sizeof(profiles.entries[0].ssid), "UserChoice");
    assert(wifi_profiles_migrate_legacy(&profiles, "Legacy", "legacy-pass") == ESP_OK);
    assert(strcmp(profiles.entries[0].ssid, "UserChoice") == 0);

    puts("wifi_profiles: validation tests passed");
    return 0;
}
