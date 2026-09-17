#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef struct {
    char unrelated_secret[64];
    char tailscale_exit_node[16];
} app_config_t;

esp_err_t app_config_load(app_config_t *config);
esp_err_t app_config_validate_tailscale(const app_config_t *config,
                                        char *message,
                                        size_t message_size);
esp_err_t app_config_save_tailscale_exit_node(const char *value);
