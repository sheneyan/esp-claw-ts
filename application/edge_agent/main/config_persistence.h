/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t main_save_config_changes(const app_config_t *before,
                                   const app_config_t *after);

#ifdef __cplusplus
}
#endif
