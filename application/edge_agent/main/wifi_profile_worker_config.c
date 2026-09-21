/* SPDX-License-Identifier: Apache-2.0 */
#include "wifi_profile_worker_config.h"

#include <string.h>

static esp_err_t copy_optional(char *destination, size_t capacity, const char *source,
                               const char **config_value)
{
    if (!source || source[0] == '\0') {
        destination[0] = '\0';
        *config_value = NULL;
        return ESP_OK;
    }
    size_t length = strlen(source);
    if (length >= capacity) return ESP_ERR_INVALID_ARG;
    memcpy(destination, source, length + 1);
    *config_value = destination;
    return ESP_OK;
}

esp_err_t wifi_profile_worker_config_init(wifi_profile_worker_config_t *snapshot,
                                          const wifi_manager_config_t *source)
{
    if (!snapshot || !source) return ESP_ERR_INVALID_ARG;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->config.ap_channel = source->ap_channel;
    snapshot->config.ap_max_conn = source->ap_max_conn;

#define COPY_FIELD(field) do { \
        esp_err_t err = copy_optional(snapshot->field, sizeof(snapshot->field), \
                                      source->field, &snapshot->config.field); \
        if (err != ESP_OK) return err; \
    } while (0)

    COPY_FIELD(ap_ssid_prefix);
    COPY_FIELD(ap_ssid);
    COPY_FIELD(ap_password);
    COPY_FIELD(ap_behavior);
    COPY_FIELD(ap_ip);
    COPY_FIELD(ap_netmask);
    COPY_FIELD(dhcp_start);
    COPY_FIELD(dhcp_end);
#undef COPY_FIELD

    return ESP_OK;
}
