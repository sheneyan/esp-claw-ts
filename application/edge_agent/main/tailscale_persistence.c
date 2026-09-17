/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tailscale_persistence.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"

esp_err_t main_tailscale_save_persisted_exit(const char *ip, void *ctx)
{
    app_config_t *config;
    esp_err_t err;
    char validation_message[96] = {0};

    (void)ctx;
    if (!ip || strlen(ip) >= sizeof(config->tailscale_exit_node)) {
        return ESP_ERR_INVALID_ARG;
    }
    config = calloc(1, sizeof(*config));
    if (!config) {
        return ESP_ERR_NO_MEM;
    }
    err = app_config_load(config);
    if (err == ESP_OK) {
        (void)snprintf(config->tailscale_exit_node,
                       sizeof(config->tailscale_exit_node), "%s", ip);
        err = app_config_validate_tailscale(config, validation_message,
                                            sizeof(validation_message));
    }
    if (err == ESP_OK) {
        err = app_config_save_tailscale_exit_node(ip);
    }
    free(config);
    return err;
}
