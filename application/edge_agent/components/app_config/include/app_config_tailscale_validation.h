/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *enabled;
    const char *auth_key;
    const char *hostname;
    const char *login_server;
    const char *exit_node;
    const char *max_peers;
} app_config_tailscale_view_t;

bool app_config_tailscale_validate(const app_config_tailscale_view_t *cfg,
                                   char *message,
                                   size_t message_size);

bool app_config_tailscale_exit_node_validate(const char *value);

bool app_config_string_update_validate(bool is_string,
                                       const char *value,
                                       size_t capacity,
                                       char *message,
                                       size_t message_size);

#ifdef __cplusplus
}
#endif
