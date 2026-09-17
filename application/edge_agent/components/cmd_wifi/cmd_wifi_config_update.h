/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_config.h"
#include "esp_err.h"

enum {
    CMD_WIFI_CONFIG_VALIDATION_MESSAGE_LEN = 64,
};

typedef enum {
    CMD_WIFI_CONFIG_UPDATE_NONE = 0,
    CMD_WIFI_CONFIG_UPDATE_INVALID_ARGUMENT,
    CMD_WIFI_CONFIG_UPDATE_ALLOCATION_FAILED,
    CMD_WIFI_CONFIG_UPDATE_LOAD_FAILED,
    CMD_WIFI_CONFIG_UPDATE_MISSING_SSID,
    CMD_WIFI_CONFIG_UPDATE_VALIDATION_FAILED,
    CMD_WIFI_CONFIG_UPDATE_SAVE_FAILED,
    CMD_WIFI_CONFIG_UPDATE_APPLY_FAILED,
    CMD_WIFI_CONFIG_UPDATE_COMPLETE,
} cmd_wifi_config_update_outcome_t;

typedef int (*cmd_wifi_config_apply_fn_t)(const app_config_t *config,
                                          void *ctx);
typedef void (*cmd_wifi_config_saved_fn_t)(const app_config_t *config,
                                           void *ctx);

typedef struct {
    cmd_wifi_config_update_outcome_t outcome;
    int apply_result;
    char validation_message[CMD_WIFI_CONFIG_VALIDATION_MESSAGE_LEN];
} cmd_wifi_config_update_result_t;

esp_err_t cmd_wifi_config_update(
    const char *ssid,
    const char *password,
    bool has_password,
    bool apply_now,
    cmd_wifi_config_apply_fn_t apply,
    cmd_wifi_config_saved_fn_t saved,
    void *ctx,
    cmd_wifi_config_update_result_t *result);
