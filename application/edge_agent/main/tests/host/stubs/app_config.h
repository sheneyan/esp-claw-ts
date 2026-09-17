#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef struct {
    char wifi_ssid[64];
    char unrelated_secret[64];
    char tailscale_exit_node[16];
} app_config_t;

esp_err_t app_config_load(app_config_t *config);
esp_err_t app_config_validate_tailscale(const app_config_t *config,
                                        char *message,
                                        size_t message_size);
esp_err_t app_config_validate_wifi(const app_config_t *config,
                                   const char **message);
esp_err_t app_config_save_changed(const app_config_t *before,
                                  const app_config_t *after);
esp_err_t app_config_save_tailscale_exit_node(const char *value);

struct app_claw_config;
void app_config_to_claw(const app_config_t *config,
                        struct app_claw_config *out);
