#pragma once

#include "esp_err.h"

#define APP_CONFIG_STR_LEN 320

typedef struct {
    char wifi_ssid[APP_CONFIG_STR_LEN];
    char wifi_password[APP_CONFIG_STR_LEN];
    char tailscale_exit_node[16];
} app_config_t;

esp_err_t app_config_load(app_config_t *config);
esp_err_t app_config_validate_wifi(const app_config_t *config,
                                   const char **message);
esp_err_t app_config_save_changed(const app_config_t *before,
                                  const app_config_t *after);
